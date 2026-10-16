#!/usr/bin/env python3
"""FilamentLager bridge: Home Assistant's filament list, served as Spoolman.

The scale speaks Spoolman. This small server answers the part of Spoolman's
API the scale uses, and keeps the data in the todo list of the Home Assistant
"Ernsthaft gelagert" FilamentLager card (todo.filament): one item per filament
kind, the details as JSON in its description:

    {"v":2, "hersteller":"Bambu Lab", "material":"PLA", "farbe":"Grün",
     "hex":"#00ae42", "gewicht":1000, "spulen":3, "reste":[420], ...}

    spulen   spools on hand, opened ones included
    reste    grams left on each opened spool (fewer entries than spulen;
             the others are full)

How the two models meet:

* Every list item is one spool in Spoolman terms, its id kept stable in the
  bridge's state file. Items without stock (spulen 0) are "archived".
* An item can stand for several physical spools, so it can carry several NFC
  tags (Spoolman's tag relation). Each tag remembers the grams last weighed on
  its spool, which tells which entry of "reste" belongs to it.
* Writing a weight updates that entry: a lighter spool replaces its rest, a
  full one that was opened adds a rest, an empty one (under 5 g) removes its
  rest and lowers spulen by one - the same bookkeeping the card's print
  deduction (script.filament_abbuchen) uses. Every other key in the item stays
  as it is.
* Things Home Assistant has no field for - tags, empty spool weights, the
  drying date - live in the state file on the Pi.

Setup:     sudo filamentlager-ha-bridge --setup
Service:   filamentlager-ha-bridge.service, listening on 127.0.0.1:7913

Only the Python standard library is used.
"""

import argparse
import json
import os
import re
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

CONFIG_PATH = os.environ.get("HA_BRIDGE_CONFIG", "/etc/filamentlager/ha.json")
STATE_PATH = os.environ.get("HA_BRIDGE_STATE", "/var/lib/spoolmanscale/ha_bridge_state.json")
LISTEN = os.environ.get("HA_BRIDGE_LISTEN", "127.0.0.1:7913")

EMPTY_G = 5          # below this a spool counts as empty
FULL_SLACK_G = 3     # within this of the nominal weight a spool counts as full
CACHE_S = 3.0        # how long a read of the list is reused
CONTEXT_S = 900      # how long a scanned tag decides which spool a write is for
REGISTERED = "2026-01-01T00:00:00Z"
VERSION = "0.27.0"   # what /info reports; >= 0.27 has the tag relation

# Empty spool weights the user has not set, by manufacturer (lower case).
DEFAULT_VENDOR_SPOOL_G = {"bambu lab": 250}


def log(*a):
    print(*a, file=sys.stderr, flush=True)


def norm_uid(s):
    """Upper case hex without separators or quotes: "04:a1:.." -> "04A1.."."""
    return re.sub(r"[^0-9A-Fa-f]", "", str(s or "")).upper()


def now_iso():
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


# ---------------------------------------------------------------- config ----

def load_config():
    try:
        with open(CONFIG_PATH) as f:
            c = json.load(f)
    except (OSError, ValueError):
        c = {}
    c.setdefault("url", "")
    c.setdefault("token", "")
    c.setdefault("entity", "todo.filament")
    c["url"] = c["url"].strip().rstrip("/")
    c["token"] = c["token"].strip()
    # "Filament Liste" or "filament" -> "todo.filament_liste" / "todo.filament"
    ent = re.sub(r"\s+", "_", str(c["entity"] or "todo.filament").strip().lower())
    if not ent.startswith("todo."):
        ent = "todo." + ent.split(".")[-1]
    c["entity"] = ent
    return c


def save_config(c):
    os.makedirs(os.path.dirname(CONFIG_PATH), exist_ok=True)
    tmp = CONFIG_PATH + ".tmp"
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        json.dump(c, f, indent=2)
    os.replace(tmp, CONFIG_PATH)


# ------------------------------------------------------------------ state ---

class State:
    """Ids, tags and the values Home Assistant has no field for."""

    def __init__(self, path):
        self.path = path
        self.lock = threading.RLock()
        try:
            with open(path) as f:
                d = json.load(f)
        except (OSError, ValueError):
            d = {}
        self.ids = d.get("ids", {})            # ha uid -> int id
        self.next_id = d.get("next_id", 1)
        self.tags = d.get("tags", {})          # HEX -> {"item", "rest", "raw"}
        self.spool_w = d.get("spool_weight", {})    # ha uid -> g
        self.vendor_w = d.get("vendor_weight", {})  # vendor (lower) -> g
        self.extra = d.get("extra", {})        # ha uid -> {key: raw json string}
        self.vendor_ids = d.get("vendor_ids", {})   # vendor (lower) -> int
        self.context = {}                      # ha uid -> (HEX, time)

    def save(self):
        with self.lock:
            d = {"ids": self.ids, "next_id": self.next_id, "tags": self.tags,
                 "spool_weight": self.spool_w, "vendor_weight": self.vendor_w,
                 "extra": self.extra, "vendor_ids": self.vendor_ids}
            os.makedirs(os.path.dirname(self.path), exist_ok=True)
            tmp = self.path + ".tmp"
            with open(tmp, "w") as f:
                json.dump(d, f, indent=1)
            os.replace(tmp, self.path)

    def id_for(self, uid):
        with self.lock:
            if uid not in self.ids:
                self.ids[uid] = self.next_id
                self.next_id += 1
                self.save()
            return self.ids[uid]

    def vendor_id(self, name):
        key = (name or "").strip().lower()
        with self.lock:
            if key not in self.vendor_ids:
                self.vendor_ids[key] = len(self.vendor_ids) + 1
                self.save()
            return self.vendor_ids[key]

    def vendor_by_id(self, vid):
        for k, v in self.vendor_ids.items():
            if v == vid:
                return k
        return None

    def tags_of(self, uid):
        return [h for h, t in self.tags.items() if t.get("item") == uid]

    def set_context(self, uid, hexuid):
        self.context[uid] = (hexuid, time.time())

    def context_tag(self, uid):
        c = self.context.get(uid)
        if c and time.time() - c[1] < CONTEXT_S and c[0] in self.tags \
                and self.tags[c[0]].get("item") == uid:
            return c[0]
        return None


# ------------------------------------------------------- home assistant -----

class HAError(Exception):
    pass


class HomeAssistant:
    def __init__(self):
        self.lock = threading.Lock()
        self.items = None
        self.at = 0.0
        self.last_error = ""

    def cfg(self):
        return load_config()

    def call(self, service, data, response=False, timeout=10):
        c = self.cfg()
        if not c["url"] or not c["token"]:
            raise HAError("not configured: run filamentlager-ha-bridge --setup")
        url = "%s/api/services/todo/%s%s" % (c["url"], service, "?return_response" if response else "")
        body = json.dumps(dict(data, entity_id=c["entity"])).encode()
        req = urllib.request.Request(url, data=body, method="POST", headers={
            "Authorization": "Bearer " + c["token"], "Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                txt = r.read().decode()
        except urllib.error.HTTPError as e:
            hint = ""
            if e.code == 400:
                hint = " - gibt es die Liste %s? (Entwicklerwerkzeuge -> Zustände)" % c["entity"]
            elif e.code == 401:
                hint = " - Token falsch oder abgelaufen"
            raise HAError("HTTP %d from Home Assistant (%s)%s" % (
                e.code, e.read()[:200].decode(errors="replace"), hint))
        except (urllib.error.URLError, OSError) as e:
            raise HAError("Home Assistant not reachable: %s" % e)
        return json.loads(txt) if txt else {}

    def get_items(self, fresh=False):
        with self.lock:
            if not fresh and self.items is not None and time.time() - self.at < CACHE_S:
                return self.items
            try:
                r = self.call("get_items", {"status": "needs_action"}, response=True)
            except HAError as e:
                self.last_error = str(e)
                raise
            resp = r.get("service_response", r)
            entity = self.cfg()["entity"]
            raw = (resp.get(entity) or {}).get("items", [])
            items = []
            for it in raw:
                try:
                    d = json.loads(it.get("description") or "{}")
                except ValueError:
                    continue
                if not isinstance(d, dict) or "typ" in d or "material" not in d:
                    continue    # the card's notes ("offen", ...) are not filament
                items.append({"uid": it["uid"], "summary": it.get("summary", ""), "d": d})
            self.items = items
            self.at = time.time()
            self.last_error = ""
            return items

    def update(self, uid, d):
        self.call("update_item", {"item": uid, "description": json.dumps(d, ensure_ascii=False,
                                                                         separators=(",", ":"))})
        with self.lock:
            self.items = None


# ------------------------------------------------------------ the model ----

def gw_of(d):
    try:
        return float(d.get("gewicht") or 1000)
    except (TypeError, ValueError):
        return 1000.0


def rests_of(d):
    gw = gw_of(d)
    sp = int(d.get("spulen") or 0)
    rq = d.get("reste")
    if not isinstance(rq, list):
        rq = [d["rest"]] if d.get("rest") not in (None, "") else []
    out = []
    for x in rq:
        try:
            x = float(x)
        except (TypeError, ValueError):
            continue
        if 0 < x < gw:
            out.append(x)
    return sorted(out)[:max(sp, 0)]


def rest_index_for(d, remembered):
    """Index in the sorted rests of the spool last weighed at `remembered` g."""
    rs = rests_of(d)
    if remembered is None or not rs:
        return None
    for i, x in enumerate(rs):
        if abs(x - remembered) < 0.5:
            return i
    # printed from since: the largest rest below what it weighed
    below = [i for i, x in enumerate(rs) if x <= remembered]
    return below[-1] if below else None


def apply_weight(d, idx, w, unarchive=False):
    """New description after the spool at rest index idx (None: a full one)
    was weighed at w grams net. Returns (new d, grams to remember or None)."""
    d = dict(d)
    d.pop("rest", None)
    gw = gw_of(d)
    sp = int(d.get("spulen") or 0)
    if unarchive and sp <= 0:
        sp = 1
    rs = rests_of(dict(d, spulen=sp))
    remember = None
    if w < EMPTY_G:
        if idx is not None and idx < len(rs):
            rs.pop(idx)
        sp = max(sp - 1, 0)
    elif w >= gw - FULL_SLACK_G:
        if idx is not None and idx < len(rs):
            rs.pop(idx)          # full again: refilled
    else:
        remember = float(round(w))
        if idx is not None and idx < len(rs):
            rs[idx] = remember
        elif len(rs) < sp:
            rs.append(remember)
        elif rs:
            j = min(range(len(rs)), key=lambda k: abs(rs[k] - w))
            rs[j] = remember
        else:
            sp = max(sp, 1)
            rs.append(remember)
    rs = sorted(int(round(x)) for x in rs if 0 < x < gw)[:sp]
    d.update(v=2, spulen=sp, reste=rs, refills=min(int(d.get("refills") or 0), sp))
    return d, remember


class Bridge:
    def __init__(self):
        self.ha = HomeAssistant()
        self.st = State(STATE_PATH)
        self.wlock = threading.Lock()

    # ---- reading ----------------------------------------------------------

    def items(self, fresh=False):
        return self.ha.get_items(fresh)

    def by_id(self, sid, fresh=False):
        for it in self.items(fresh):
            if self.st.id_for(it["uid"]) == sid:
                return it
        return None

    def by_tag(self, hexuid):
        t = self.st.tags.get(hexuid)
        if not t:
            return None
        for it in self.items():
            if it["uid"] == t["item"]:
                return it
        return None

    def remaining(self, it, hexuid=None):
        d = it["d"]
        gw = gw_of(d)
        sp = int(d.get("spulen") or 0)
        if sp <= 0:
            return 0.0
        rs = rests_of(d)
        hexuid = hexuid or self.st.context_tag(it["uid"])
        if hexuid:
            idx = rest_index_for(d, self.st.tags[hexuid].get("rest"))
            if idx is not None:
                return rs[idx]
            if sp > len(rs):
                return gw
            return rs[0] if rs else gw
        # no tag to go by: a full spool if there is one, else the fullest rest
        if sp > len(rs):
            return gw
        return rs[-1] if rs else gw

    def spool(self, it, hexuid=None):
        d = it["d"]
        uid = it["uid"]
        sid = self.st.id_for(uid)
        gw = gw_of(d)
        sp = int(d.get("spulen") or 0)
        rs = rests_of(d)
        hexuid = hexuid or self.st.context_tag(uid)
        rem = self.remaining(it, hexuid)
        vendor = d.get("hersteller") or ""
        vkey = vendor.strip().lower()
        name = " ".join([str(d.get("farbe") or "")] + [str(x) for x in (d.get("besonderheiten") or [])]).strip()
        tags = self.st.tags_of(uid)
        if hexuid in tags:              # the scanned one first
            tags.remove(hexuid)
            tags.insert(0, hexuid)
        extra = dict(self.st.extra.get(uid, {}))
        if tags:
            first = self.st.tags[tags[0]]
            raw = first.get("raw") or tags[0]
            for k in ("tag", "nfc_id"):
                extra.setdefault(k, json.dumps(raw if k == "tag" else tags[0]))
            extra.setdefault("card_uids", json.dumps(",".join(tags)))
        hexcol = str(d.get("hex") or "").lstrip("#")[:6]
        opened = len(rs)
        loc = "%d %s" % (sp, "Rolle" if sp == 1 else "Rollen")
        if opened:
            loc += " (%d offen)" % opened
        return {
            "id": sid,
            "registered": REGISTERED,
            "first_used": None,
            "last_used": None,
            "filament": {
                "id": sid,
                "registered": REGISTERED,
                "name": name or it["summary"],
                "vendor": {"id": self.st.vendor_id(vendor), "registered": REGISTERED, "name": vendor,
                           "empty_spool_weight": self.st.vendor_w.get(vkey, DEFAULT_VENDOR_SPOOL_G.get(vkey)),
                           "extra": {}},
                "material": d.get("material") or "",
                "density": 1.24,
                "diameter": d.get("durchmesser") or 1.75,
                "weight": gw,
                "spool_weight": None,
                "article_number": d.get("fcode") or None,
                "color_hex": hexcol or None,
                "settings_extruder_temp": d.get("duese_max") or None,
                "settings_bed_temp": d.get("bett_max") or None,
                "extra": {},
            },
            "remaining_weight": rem,
            "used_weight": max(gw - rem, 0),
            "initial_weight": gw,
            "spool_weight": self.st.spool_w.get(uid),
            "location": loc,
            "lot_nr": None,
            "comment": it["summary"],
            "archived": sp <= 0,
            "extra": extra,
            "tags": [{"uid": (self.st.tags[h].get("raw") or h), "spool_id": sid, "format": None}
                     for h in tags],
        }

    def lager(self):
        """The whole stock in one compact answer, for the scale's Lager screen:
        sorted by manufacturer, material, name; grams as the card counts them."""
        out = []
        for it in self.items():
            d = it["d"]
            gw = gw_of(d)
            sp = int(d.get("spulen") or 0)
            rs = rests_of(d)
            out.append({
                "id": self.st.id_for(it["uid"]),
                "n": it["summary"],
                "v": d.get("hersteller") or "",
                "m": d.get("material") or "",
                "c": str(d.get("hex") or "").lstrip("#")[:6],
                "s": max(sp, 0),
                "r": [int(round(x)) for x in rs],
                "w": int(gw),
                "g": int(round((max(sp, 0) - len(rs)) * gw + sum(rs))) if sp > 0 else 0,
                "t": len(self.st.tags_of(it["uid"])),
            })
        out.sort(key=lambda e: (e["v"].lower(), e["m"].lower(), e["n"].lower()))
        return out

    # ---- writing -----------------------------------------------------------

    def write_weight(self, it, w, unarchive=False):
        uid = it["uid"]
        hexuid = self.st.context_tag(uid)
        with self.wlock:
            fresh = self.by_id(self.st.id_for(uid), fresh=True) or it
            d = fresh["d"]
            remembered = self.st.tags[hexuid].get("rest") if hexuid else None
            idx = rest_index_for(d, remembered) if remembered is not None else None
            if idx is None and remembered is None:
                # Nothing remembered for this spool: the rest closest to the
                # new weight from above that no other tag stands for, or a
                # full spool being opened when there is none.
                rs = rests_of(d)
                claimed = set()
                for h in self.st.tags_of(uid):
                    if h != hexuid:
                        j = rest_index_for(d, self.st.tags[h].get("rest"))
                        if j is not None and self.st.tags[h].get("rest") is not None:
                            claimed.add(j)
                cands = [i for i, x in enumerate(rs) if x >= w - 15 and i not in claimed]
                idx = min(cands, key=lambda i: rs[i] - w) if cands else None
            new, remember = apply_weight(d, idx, w, unarchive)
            self.ha.update(uid, new)
            if hexuid:
                self.st.tags[hexuid]["rest"] = remember
                self.st.save()
            log("weight %.0f g -> %s: spulen %s reste %s" % (w, fresh["summary"], new["spulen"], new["reste"]))

    def link_tag(self, it, raw):
        h = norm_uid(raw)
        if not h:
            return 400, {"detail": "no uid"}
        t = self.st.tags.get(h)
        if t and t.get("item") != it["uid"]:
            other = self.st.ids.get(t["item"], 0)
            return 409, {"detail": "tag is linked to another spool", "spool_id": other,
                         "filament_id": other}
        if not t:
            self.st.tags[h] = {"item": it["uid"], "rest": None, "raw": str(raw).strip().strip('"')}
            self.st.save()
        self.st.set_context(it["uid"], h)
        return 200, {"uid": self.st.tags[h]["raw"], "spool_id": self.st.id_for(it["uid"])}

    def unlink_tag(self, it, raw):
        h = norm_uid(raw)
        t = self.st.tags.get(h)
        if t and t.get("item") == it["uid"]:
            del self.st.tags[h]
            self.st.save()
            return 204
        return 404


B = None


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def send(self, code, obj=None, hdr=None):
        b = b"" if obj is None else json.dumps(obj, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b)))
        for k, v in (hdr or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if b:
            self.wfile.write(b)

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(n) if n else b""
        try:
            return json.loads(raw) if raw else {}
        except ValueError:
            return {}

    def handle_any(self, method):
        try:
            self.route(method)
        except HAError as e:
            log("HA:", e)
            self.send(503, {"detail": str(e)})
        except Exception as e:     # keep serving whatever one request did
            log("error:", repr(e))
            self.send(500, {"detail": repr(e)})

    def do_GET(self):
        self.handle_any("GET")

    def do_POST(self):
        self.handle_any("POST")

    def do_PATCH(self):
        self.handle_any("PATCH")

    def do_PUT(self):
        self.handle_any("PUT")

    def do_DELETE(self):
        self.handle_any("DELETE")

    def route(self, method):
        u = urllib.parse.urlparse(self.path)
        p = u.path.rstrip("/")
        q = urllib.parse.parse_qs(u.query)
        st = B.st

        if p == "/api/v1/health":
            B.items()          # raises 503 when Home Assistant cannot be read
            return self.send(200, {"status": "healthy"})
        if p == "/api/v1/info":
            return self.send(200, {"version": VERSION, "debug_mode": False, "automatic_backups": False,
                                   "data_dir": "", "logs_dir": "", "backups_dir": "", "db_type": "home-assistant",
                                   "git_commit": "filamentlager-ha-bridge", "build_date": ""})
        if p == "/api/v1/lager":
            return self.send(200, B.lager())
        if p == "/api/v1/tag/reader":
            return self.send(200, [])
        if p == "/api/v1/tag/scan" and method == "POST":
            b = self.body()
            h = norm_uid(b.get("uid"))
            it = B.by_tag(h) if h else None
            if it:
                st.set_context(it["uid"], h)
                sp = B.spool(it, h)
                return self.send(200, {"uid": h, "matched_spool_id": sp["id"], "matched_filament_id": None,
                                       "spool": sp})
            return self.send(200, {"uid": h, "matched_spool_id": None, "matched_filament_id": None,
                                   "spool": None})
        if p.startswith("/api/v1/field/"):
            if p == "/api/v1/field/spool" and method == "GET":
                return self.send(200, [
                    {"name": n, "key": k, "entity_type": "spool", "field_type": "text", "order": i,
                     "default_value": None, "unit": None, "choices": None, "multi_choice": None}
                    for i, (k, n) in enumerate([("tag", "Tag"), ("nfc_id", "NFC ID"),
                                                ("card_uids", "Card UIDs"), ("last_dried", "Last dried")])])
            if method in ("POST", "PUT"):
                return self.send(200, [])
            return self.send(200, [])
        if p.startswith("/api/v1/setting"):
            return self.send(200, {})
        if p in ("/api/v1/location", "/api/v1/material"):
            return self.send(200, [])

        if p == "/api/v1/spool" and method == "GET":
            return self.spool_list(q)
        m = re.fullmatch(r"/api/v1/spool/(\d+)(/tag(?:/(.+))?)?", p)
        if m:
            it = B.by_id(int(m.group(1)))
            if not it:
                return self.send(404, {"detail": "Item not found"})
            if m.group(2):
                if method == "POST":
                    code, obj = B.link_tag(it, self.body().get("uid", ""))
                    return self.send(code, obj)
                if method == "DELETE" and m.group(3):
                    code = B.unlink_tag(it, urllib.parse.unquote(m.group(3)))
                    return self.send(code, None if code == 204 else {"detail": "not linked"})
                return self.send(200, B.spool(it)["tags"])
            if method == "GET":
                return self.send(200, B.spool(it))
            if method == "PATCH":
                return self.patch_spool(it, self.body())
            return self.send(405, {"detail": "Method not allowed"})
        if p == "/api/v1/spool" and method == "POST":
            return self.send(400, {"detail": "Neue Spulen bitte in der FilamentLager-Karte anlegen"})

        m = re.fullmatch(r"/api/v1/filament/(\d+)", p)
        if m:
            it = B.by_id(int(m.group(1)))
            if not it:
                return self.send(404, {"detail": "Item not found"})
            if method == "PATCH":
                b = self.body()
                if "spool_weight" in b:
                    st.spool_w[it["uid"]] = b["spool_weight"]
                    st.save()
            return self.send(200, B.spool(it)["filament"])
        if p == "/api/v1/filament":
            seen = [B.spool(it)["filament"] for it in B.items()]
            return self.send(200, seen, {"x-total-count": str(len(seen))})

        m = re.fullmatch(r"/api/v1/vendor/(\d+)", p)
        if m:
            name = st.vendor_by_id(int(m.group(1)))
            if name is None:
                return self.send(404, {"detail": "Item not found"})
            if method == "PATCH":
                b = self.body()
                if "empty_spool_weight" in b:
                    st.vendor_w[name] = b["empty_spool_weight"]
                    st.save()
            return self.send(200, {"id": int(m.group(1)), "registered": REGISTERED, "name": name,
                                   "empty_spool_weight": st.vendor_w.get(name, DEFAULT_VENDOR_SPOOL_G.get(name)),
                                   "extra": {}})
        if p == "/api/v1/vendor":
            vs = [{"id": v, "registered": REGISTERED, "name": k, "extra": {},
                   "empty_spool_weight": st.vendor_w.get(k, DEFAULT_VENDOR_SPOOL_G.get(k))}
                  for k, v in st.vendor_ids.items()]
            return self.send(200, vs, {"x-total-count": str(len(vs))})

        return self.send(404, {"detail": "Not Found"})

    def spool_list(self, q):
        allow_archived = (q.get("allow_archived", ["false"])[0].lower() == "true")
        items = B.items()
        tag_filter = None
        for k, v in q.items():
            if k == "tag" or k.startswith("extra."):
                tag_filter = norm_uid(urllib.parse.unquote(v[0]))
        out = []
        for it in items:
            sp_count = int(it["d"].get("spulen") or 0)
            if tag_filter is not None:
                if B.st.tags.get(tag_filter, {}).get("item") != it["uid"]:
                    continue
                B.st.set_context(it["uid"], tag_filter)
                out.append(B.spool(it, tag_filter))
                continue
            if sp_count <= 0 and not allow_archived:
                continue
            out.append(B.spool(it))
        total = len(out)
        sort = q.get("sort", [""])[0]
        if sort.startswith("id"):
            out.sort(key=lambda s: s["id"], reverse=sort.endswith(":desc"))
        lim = q.get("limit", [None])[0]
        off = int(q.get("offset", ["0"])[0] or 0)
        if lim:
            out = out[off:off + int(lim)]
        return self.send(200, out, {"x-total-count": str(total)})

    def patch_spool(self, it, b):
        st = B.st
        uid = it["uid"]
        extra = b.get("extra") or {}
        for k, v in extra.items():
            if k in ("tag", "nfc_id", "card_uids", "rfid_tag"):
                # tags written into an extra field: each uid in it is linked
                vals = [x for x in re.split(r"[,\s]+", str(v).strip().strip('"')) if norm_uid(x)]
                if not vals:      # emptied: unlink the tag in hand
                    h = st.context_tag(uid)
                    if h:
                        del st.tags[h]
                for x in vals:
                    code, _ = B.link_tag(it, x)
                    if code == 409:
                        h = norm_uid(x)
                        st.tags[h]["item"] = uid    # an extra field moves it
                        st.set_context(uid, h)
            else:
                st.extra.setdefault(uid, {})[k] = v
        if extra:
            st.save()
        if "spool_weight" in b:
            st.spool_w[uid] = b["spool_weight"]
            st.save()
        if "remaining_weight" in b or b.get("archived") is not None:
            w = float(b.get("remaining_weight") if b.get("remaining_weight") is not None else 0)
            if b.get("archived") is True:
                w = 0.0
            B.write_weight(it, w, unarchive=(b.get("archived") is False))
        it2 = B.by_id(st.id_for(uid), fresh=True) or it
        return self.send(200, B.spool(it2))


# ------------------------------------------------------------------ setup ---

def setup():
    c = load_config()
    print("FilamentLager - Verbindung zu Home Assistant\n")
    url = input("Adresse von Home Assistant [%s]: " % (c["url"] or "http://homeassistant.local:8123")).strip()
    if url:
        c["url"] = url.rstrip("/")
    elif not c["url"]:
        c["url"] = "http://homeassistant.local:8123"
    if not re.match(r"https?://", c["url"]):
        c["url"] = "http://" + c["url"]
    print("\nLangzeit-Zugriffstoken: in Home Assistant unten links auf das Profil,")
    print("Reiter \"Sicherheit\", ganz unten \"Token erstellen\".")
    tok = input("Token%s: " % (" (Enter = bisheriges behalten)" if c["token"] else "")).strip()
    if tok:
        c["token"] = tok
    ent = input("Filamentliste [%s]: " % c["entity"]).strip()
    if ent:
        c["entity"] = ent
    save_config(c)
    print("\nGespeichert in", CONFIG_PATH)
    try:
        items = HomeAssistant().get_items(fresh=True)
        print("Verbindung klappt: %d Filamentsorten in %s." % (len(items), c["entity"]))
        return 0
    except HAError as e:
        print("Verbindung klappt noch nicht:", e)
        return 1


def main():
    global B
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--setup", action="store_true", help="Home Assistant address and token")
    ap.add_argument("--check", action="store_true", help="test the connection")
    a = ap.parse_args()
    if a.setup:
        sys.exit(setup())
    if a.check:
        try:
            items = HomeAssistant().get_items(fresh=True)
            print("ok: %d Filamentsorten" % len(items))
            sys.exit(0)
        except HAError as e:
            print("Fehler:", e)
            sys.exit(1)
    B = Bridge()
    host, port = LISTEN.rsplit(":", 1)
    srv = ThreadingHTTPServer((host, int(port)), Handler)
    srv.daemon_threads = True
    c = load_config()
    log("FilamentLager bridge on %s -> %s (%s)" % (LISTEN, c["url"] or "(not set up)", c["entity"]))
    srv.serve_forever()


if __name__ == "__main__":
    main()
