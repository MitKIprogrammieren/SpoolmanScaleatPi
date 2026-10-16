#include "Preferences.h"

#include <dirent.h>
#include <map>
#include <mutex>
#include <string>

#include "pi_paths.h"

// Format: one line per key, "key<TAB>type<TAB>hex-encoded value".
// type: i (integer), f (float/double as text), s (string), b (bytes).
namespace {
struct Entry {
  char type;
  std::string value;
};
typedef std::map<std::string, Entry> Ns;

std::mutex s_mu;
std::map<std::string, Ns> s_cache;

std::string pathFor(const std::string& ns) { return piDataPath(("nvs_" + ns + ".txt").c_str()); }

std::string hex(const std::string& s) {
  static const char* d = "0123456789abcdef";
  std::string o;
  for (unsigned char c : s) { o += d[c >> 4]; o += d[c & 15]; }
  return o;
}
std::string unhex(const std::string& s) {
  std::string o;
  for (size_t i = 0; i + 1 < s.size(); i += 2) o += (char)strtol(s.substr(i, 2).c_str(), nullptr, 16);
  return o;
}

Ns& load(const std::string& ns) {
  auto it = s_cache.find(ns);
  if (it != s_cache.end()) return it->second;
  Ns& n = s_cache[ns];
  FILE* f = fopen(pathFor(ns).c_str(), "r");
  if (!f) return n;
  char* line = nullptr;
  size_t cap = 0;
  while (getline(&line, &cap, f) > 0) {
    std::string l(line);
    while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
    size_t t1 = l.find('\t'), t2 = l.find('\t', t1 + 1);
    if (t1 == std::string::npos || t2 == std::string::npos) continue;
    n[l.substr(0, t1)] = Entry{l[t1 + 1], unhex(l.substr(t2 + 1))};
  }
  free(line);
  fclose(f);
  return n;
}

bool save(const std::string& ns) {
  const std::string path = pathFor(ns);
  const std::string tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "w");
  if (!f) return false;
  for (auto& kv : load(ns)) fprintf(f, "%s\t%c\t%s\n", kv.first.c_str(), kv.second.type, hex(kv.second.value).c_str());
  fflush(f);
  fsync(fileno(f));
  fclose(f);
  return rename(tmp.c_str(), path.c_str()) == 0;
}
}  // namespace

bool Preferences::begin(const char* ns, bool read_only, const char*) {
  if (!ns || !*ns) return false;
  std::lock_guard<std::mutex> lk(s_mu);
  ns_ = ns;
  ro_ = read_only;
  open_ = true;
  load(ns_);
  return true;
}

void Preferences::end() { open_ = false; }

bool Preferences::clear() {
  if (!open_ || ro_) return false;
  std::lock_guard<std::mutex> lk(s_mu);
  load(ns_).clear();
  return save(ns_);
}

bool Preferences::remove(const char* key) {
  if (!open_ || ro_) return false;
  std::lock_guard<std::mutex> lk(s_mu);
  load(ns_).erase(key);
  return save(ns_);
}

bool Preferences::isKey(const char* key) {
  if (!open_) return false;
  std::lock_guard<std::mutex> lk(s_mu);
  return load(ns_).count(key) != 0;
}

size_t Preferences::putI(const char* k, int64_t v, size_t width) {
  if (!open_ || ro_ || !k) return 0;
  std::lock_guard<std::mutex> lk(s_mu);
  load(ns_)[k] = Entry{'i', std::to_string(v)};
  return save(ns_) ? width : 0;
}

int64_t Preferences::getI(const char* k, int64_t d) {
  if (!open_ || !k) return d;
  std::lock_guard<std::mutex> lk(s_mu);
  Ns& n = load(ns_);
  auto it = n.find(k);
  if (it == n.end() || it->second.type != 'i') return d;
  return strtoll(it->second.value.c_str(), nullptr, 10);
}

size_t Preferences::putFloat(const char* k, float v) {
  if (!open_ || ro_ || !k) return 0;
  char b[64];
  snprintf(b, sizeof(b), "%.9g", (double)v);
  std::lock_guard<std::mutex> lk(s_mu);
  load(ns_)[k] = Entry{'f', b};
  return save(ns_) ? 4 : 0;
}
size_t Preferences::putDouble(const char* k, double v) {
  if (!open_ || ro_ || !k) return 0;
  char b[64];
  snprintf(b, sizeof(b), "%.17g", v);
  std::lock_guard<std::mutex> lk(s_mu);
  load(ns_)[k] = Entry{'f', b};
  return save(ns_) ? 8 : 0;
}
float Preferences::getFloat(const char* k, float d) { return (float)getDouble(k, d); }
double Preferences::getDouble(const char* k, double d) {
  if (!open_ || !k) return d;
  std::lock_guard<std::mutex> lk(s_mu);
  Ns& n = load(ns_);
  auto it = n.find(k);
  if (it == n.end() || it->second.type != 'f') return d;
  return strtod(it->second.value.c_str(), nullptr);
}

size_t Preferences::putString(const char* k, const char* v) {
  if (!open_ || ro_ || !k || !v) return 0;
  std::lock_guard<std::mutex> lk(s_mu);
  load(ns_)[k] = Entry{'s', v};
  return save(ns_) ? strlen(v) + 1 : 0;
}
String Preferences::getString(const char* k, const String& d) {
  if (!open_ || !k) return d;
  std::lock_guard<std::mutex> lk(s_mu);
  Ns& n = load(ns_);
  auto it = n.find(k);
  if (it == n.end() || it->second.type != 's') return d;
  return String(it->second.value);
}
size_t Preferences::getString(const char* k, char* out, size_t max) {
  if (!out || !max) return 0;
  String s = getString(k, String());
  if (!isKey(k)) return 0;
  s.toCharArray(out, (unsigned)max);
  return s.length() + 1;
}

size_t Preferences::putBytes(const char* k, const void* v, size_t len) {
  if (!open_ || ro_ || !k || (!v && len)) return 0;
  std::lock_guard<std::mutex> lk(s_mu);
  load(ns_)[k] = Entry{'b', std::string((const char*)v, len)};
  return save(ns_) ? len : 0;
}
size_t Preferences::getBytesLength(const char* k) {
  if (!open_ || !k) return 0;
  std::lock_guard<std::mutex> lk(s_mu);
  Ns& n = load(ns_);
  auto it = n.find(k);
  return (it == n.end() || it->second.type != 'b') ? 0 : it->second.value.size();
}
size_t Preferences::getBytes(const char* k, void* out, size_t max) {
  if (!open_ || !k || !out) return 0;
  std::lock_guard<std::mutex> lk(s_mu);
  Ns& n = load(ns_);
  auto it = n.find(k);
  if (it == n.end() || it->second.type != 'b' || it->second.value.size() > max) return 0;
  memcpy(out, it->second.value.data(), it->second.value.size());
  return it->second.value.size();
}

void piPreferencesEraseAll() {
  std::lock_guard<std::mutex> lk(s_mu);
  s_cache.clear();
  DIR* d = opendir(piDataDir().c_str());
  if (!d) return;
  while (struct dirent* e = readdir(d)) {
    std::string n = e->d_name;
    if (n.rfind("nvs_", 0) == 0) unlink(piDataPath(n.c_str()).c_str());
  }
  closedir(d);
}
