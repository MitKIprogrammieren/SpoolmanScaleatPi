#include "WiFi.h"
#include "Network.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <atomic>
#include <ifaddrs.h>
#include <mutex>
#include <net/if.h>
#include <netdb.h>
#include <sys/wait.h>
#include <thread>
#include <vector>

WiFiClass WiFi;
NetworkClass Network;

namespace {
struct ScanRow {
  std::string ssid;
  int rssi;
  bool open;
  uint8_t bssid[6];
  int channel;
};

std::mutex s_mu;
std::vector<ScanRow> s_scan;
std::atomic<int> s_scan_state{WIFI_SCAN_FAILED};   // count, RUNNING or FAILED

struct Cb {
  arduino_event_id_t ev;
  WiFiEventFuncCb fn;
};
std::vector<Cb> s_cbs;
std::atomic<bool> s_monitor{false};
std::atomic<int> s_pending_gotip{0};

// Runs a program without a shell and returns its stdout.
std::string run(const std::vector<std::string>& argv, int* status = nullptr) {
  int pipefd[2];
  if (pipe(pipefd) != 0) return "";
  pid_t pid = fork();
  if (pid == 0) {
    dup2(pipefd[1], 1);
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) dup2(devnull, 2);
    close(pipefd[0]);
    close(pipefd[1]);
    std::vector<char*> a;
    for (auto& s : argv) a.push_back(const_cast<char*>(s.c_str()));
    a.push_back(nullptr);
    execvp(a[0], a.data());
    _exit(127);
  }
  close(pipefd[1]);
  std::string out;
  char buf[4096];
  ssize_t n;
  while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) out.append(buf, (size_t)n);
  close(pipefd[0]);
  int st = 0;
  waitpid(pid, &st, 0);
  if (status) *status = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
  return out;
}

// The interface that carries the default route, with its gateway.
bool defaultRoute(std::string& ifname, uint32_t& gw) {
  FILE* f = fopen("/proc/net/route", "r");
  if (!f) return false;
  char line[256];
  bool found = false;
  if (!fgets(line, sizeof(line), f)) { fclose(f); return false; }   // header
  while (fgets(line, sizeof(line), f)) {
    char ifn[64];
    unsigned dest, gate, flags;
    if (sscanf(line, "%63s %x %x %x", ifn, &dest, &gate, &flags) == 4 && dest == 0 && (flags & 1)) {
      ifname = ifn;
      gw = gate;
      found = true;
      break;
    }
  }
  fclose(f);
  return found;
}

uint32_t ifaceIp(const std::string& ifname) {
  struct ifaddrs* ifa = nullptr;
  if (getifaddrs(&ifa) != 0) return 0;
  uint32_t ip = 0;
  for (auto* p = ifa; p; p = p->ifa_next) {
    if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
    if (ifname != p->ifa_name) continue;
    ip = ((struct sockaddr_in*)p->ifa_addr)->sin_addr.s_addr;
    break;
  }
  freeifaddrs(ifa);
  return ip;
}

bool isWireless(const std::string& ifname) {
  std::string p = "/sys/class/net/" + ifname + "/wireless";
  return access(p.c_str(), F_OK) == 0;
}

std::string currentIface() {
  std::string ifn;
  uint32_t gw;
  if (defaultRoute(ifn, gw)) return ifn;
  return "wlan0";
}

// "Ethernet" for a wired link, the SSID for a wireless one; cached briefly
// because nmcli takes a moment.
std::string s_ssid_cache;
unsigned long s_ssid_at = 0;

std::string currentSsid() {
  std::lock_guard<std::mutex> lk(s_mu);
  if (s_ssid_at && millis() - s_ssid_at < 10000) return s_ssid_cache;
  std::string ifn = currentIface();
  std::string ssid;
  if (!isWireless(ifn)) {
    ssid = "Ethernet";
  } else {
    std::string out = run({"iwgetid", "-r"});
    if (out.empty()) out = run({"nmcli", "-t", "-f", "active,ssid", "dev", "wifi"});
    if (out.rfind("yes:", 0) == 0 || out.find("\nyes:") != std::string::npos) {
      size_t p = out.rfind("yes:", 0) == 0 ? 0 : out.find("\nyes:") + 1;
      size_t e = out.find('\n', p);
      out = out.substr(p + 4, e == std::string::npos ? std::string::npos : e - p - 4);
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    ssid = out;
  }
  s_ssid_cache = ssid;
  s_ssid_at = millis();
  return ssid;
}

void fire(arduino_event_id_t ev, uint8_t reason = 0) {
  arduino_event_info_t info = {};
  info.wifi_sta_disconnected.reason = reason;
  std::vector<Cb> cbs;
  {
    std::lock_guard<std::mutex> lk(s_mu);
    cbs = s_cbs;
  }
  for (auto& c : cbs)
    if (c.ev == ev || c.ev == ARDUINO_EVENT_MAX) c.fn(ev, info);
}

void startMonitor() {
  bool expected = false;
  if (!s_monitor.compare_exchange_strong(expected, true)) return;
  std::thread([] {
    bool was = false;
    for (;;) {
      bool now = WiFi.status() == WL_CONNECTED;
      if (now && !was) fire(ARDUINO_EVENT_WIFI_STA_GOT_IP);
      else if (!now && was) fire(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, 1);
      else if (now && s_pending_gotip.exchange(0)) fire(ARDUINO_EVENT_WIFI_STA_GOT_IP);
      was = now;
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }).detach();
}

bool parseBssid(const std::string& s, uint8_t out[6]) {
  unsigned b[6];
  if (sscanf(s.c_str(), "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) return false;
  for (int i = 0; i < 6; i++) out[i] = (uint8_t)b[i];
  return true;
}

// nmcli -t escapes ':' inside fields as '\:'.
std::vector<std::string> splitTerse(const std::string& line) {
  std::vector<std::string> f;
  std::string cur;
  for (size_t i = 0; i < line.size(); i++) {
    if (line[i] == '\\' && i + 1 < line.size()) { cur += line[++i]; continue; }
    if (line[i] == ':') { f.push_back(cur); cur.clear(); continue; }
    cur += line[i];
  }
  f.push_back(cur);
  return f;
}

void doScan(std::string only_ssid) {
  std::string out = run({"nmcli", "-t", "-f", "SSID,SIGNAL,SECURITY,BSSID,CHAN", "dev", "wifi", "list",
                         "--rescan", "yes"});
  std::vector<ScanRow> rows;
  size_t p = 0;
  while (p < out.size()) {
    size_t e = out.find('\n', p);
    std::string line = out.substr(p, e == std::string::npos ? std::string::npos : e - p);
    p = e == std::string::npos ? out.size() : e + 1;
    auto f = splitTerse(line);
    if (f.size() < 5 || f[0].empty()) continue;
    if (!only_ssid.empty() && f[0] != only_ssid) continue;
    ScanRow r;
    r.ssid = f[0];
    r.rssi = atoi(f[1].c_str()) / 2 - 100;
    r.open = f[2].empty() || f[2] == "--";
    memset(r.bssid, 0, 6);
    parseBssid(f[3], r.bssid);
    r.channel = atoi(f[4].c_str());
    rows.push_back(r);
  }
  std::lock_guard<std::mutex> lk(s_mu);
  s_scan = rows;
  s_scan_state = (int)rows.size();
}
}  // namespace

wl_status_t WiFiClass::begin(const char* ssid, const char* pass, int32_t, const uint8_t*, bool connect) {
  startMonitor();
  if (!connect) return status();
  const std::string want = ssid ? ssid : "";
  const bool online = status() == WL_CONNECTED;
  if (online) {
    std::string have = currentSsid();
    // Already on that network (or on a wired link the firmware calls
    // "Ethernet"): report it like a fresh connection, change nothing.
    if (want.empty() || want == have || want == "Ethernet") {
      s_pending_gotip = 1;
      return WL_CONNECTED;
    }
  }
  if (want.empty() || want == "Ethernet") return status();
  // Ask NetworkManager to join the network chosen on the scale.
  std::string pw = pass ? pass : "";
  {
    std::lock_guard<std::mutex> lk(s_mu);
    s_ssid_at = 0;   // the SSID will change
  }
  std::thread([want, pw] {
    std::vector<std::string> a = {"nmcli", "dev", "wifi", "connect", want};
    if (!pw.empty()) { a.push_back("password"); a.push_back(pw); }
    int st = 0;
    run(a, &st);
    {
      std::lock_guard<std::mutex> lk(s_mu);
      s_ssid_at = 0;
    }
    if (st == 0) s_pending_gotip = 1;
    else fire(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, 201);
  }).detach();
  return WL_DISCONNECTED;
}

bool WiFiClass::disconnect(bool, bool) {
  // The network belongs to the OS; the firmware's disconnect-before-scan is
  // not passed on, or the Pi would drop its own connection.
  return true;
}

wl_status_t WiFiClass::status() {
  std::string ifn;
  uint32_t gw;
  if (!defaultRoute(ifn, gw)) return WL_DISCONNECTED;
  return ifaceIp(ifn) ? WL_CONNECTED : WL_DISCONNECTED;
}

const char* WiFiClass::getHostname() {
  static char h[65];
  gethostname(h, sizeof(h));
  return h;
}

IPAddress WiFiClass::localIP() { return IPAddress(ifaceIp(currentIface())); }
IPAddress WiFiClass::gatewayIP() {
  std::string ifn;
  uint32_t gw = 0;
  defaultRoute(ifn, gw);
  return IPAddress(gw);
}
IPAddress WiFiClass::subnetMask() { return IPAddress(255, 255, 255, 0); }
IPAddress WiFiClass::dnsIP(uint8_t n) {
  FILE* f = fopen("/etc/resolv.conf", "r");
  if (!f) return IPAddress();
  char line[256];
  int i = 0;
  IPAddress ip;
  while (fgets(line, sizeof(line), f)) {
    char a[64];
    if (sscanf(line, "nameserver %63s", a) == 1 && ip.fromString(a)) {
      if (i++ == n) break;
      ip = IPAddress();
    }
  }
  fclose(f);
  return ip;
}

String WiFiClass::macAddress() {
  std::string p = "/sys/class/net/" + currentIface() + "/address";
  FILE* f = fopen(p.c_str(), "r");
  char b[32] = "";
  if (f) { if (!fgets(b, sizeof(b), f)) b[0] = 0; fclose(f); }
  String s(b);
  s.trim();
  s.toUpperCase();
  return s;
}
uint8_t* WiFiClass::macAddress(uint8_t* mac) {
  parseBssid(macAddress().c_str(), mac);
  return mac;
}

String WiFiClass::SSID() { return String(currentSsid()); }

int8_t WiFiClass::RSSI() {
  std::string ifn = currentIface();
  if (!isWireless(ifn)) return -40;
  FILE* f = fopen("/proc/net/wireless", "r");
  if (!f) return -60;
  char line[256];
  int rssi = -60;
  while (fgets(line, sizeof(line), f)) {
    char name[64];
    unsigned st;
    float link, level;
    if (sscanf(line, " %63[^:]: %x %f %f", name, &st, &link, &level) == 4 && ifn == name) {
      rssi = (int)level;
      break;
    }
  }
  fclose(f);
  return (int8_t)rssi;
}

uint8_t* WiFiClass::BSSID() {
  static uint8_t b[6] = {0};
  return b;
}
String WiFiClass::BSSIDstr() { return String("00:00:00:00:00:00"); }
int32_t WiFiClass::channel() { return 0; }

int16_t WiFiClass::scanNetworks(bool async, bool, bool, uint32_t, uint8_t, const char* ssid, const uint8_t*) {
  std::string only = ssid ? ssid : "";
  s_scan_state = WIFI_SCAN_RUNNING;
  if (async) {
    std::thread([only] { doScan(only); }).detach();
    return WIFI_SCAN_RUNNING;
  }
  doScan(only);
  return (int16_t)s_scan_state.load();
}
int16_t WiFiClass::scanComplete() { return (int16_t)s_scan_state.load(); }
void WiFiClass::scanDelete() {
  std::lock_guard<std::mutex> lk(s_mu);
  s_scan.clear();
  if (s_scan_state != WIFI_SCAN_RUNNING) s_scan_state = WIFI_SCAN_FAILED;
}

String WiFiClass::SSID(uint8_t i) {
  std::lock_guard<std::mutex> lk(s_mu);
  return i < s_scan.size() ? String(s_scan[i].ssid) : String();
}
int32_t WiFiClass::RSSI(uint8_t i) {
  std::lock_guard<std::mutex> lk(s_mu);
  return i < s_scan.size() ? s_scan[i].rssi : -127;
}
uint8_t* WiFiClass::BSSID(uint8_t i) {
  static uint8_t b[6];
  std::lock_guard<std::mutex> lk(s_mu);
  if (i < s_scan.size()) memcpy(b, s_scan[i].bssid, 6);
  else memset(b, 0, 6);
  return b;
}
int32_t WiFiClass::channel(uint8_t i) {
  std::lock_guard<std::mutex> lk(s_mu);
  return i < s_scan.size() ? s_scan[i].channel : 0;
}
wifi_auth_mode_t WiFiClass::encryptionType(uint8_t i) {
  std::lock_guard<std::mutex> lk(s_mu);
  return (i < s_scan.size() && s_scan[i].open) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
}

int WiFiClass::hostByName(const char* host, IPAddress& out) {
  if (!host || !*host) return 0;
  if (out.fromString(host)) return 1;
  struct addrinfo hints = {}, *res = nullptr;
  hints.ai_family = AF_INET;
  if (getaddrinfo(host, nullptr, &hints, &res) != 0 || !res) return 0;
  out = IPAddress((uint32_t)((struct sockaddr_in*)res->ai_addr)->sin_addr.s_addr);
  freeaddrinfo(res);
  return 1;
}

int WiFiClass::onEvent(WiFiEventFuncCb cb, arduino_event_id_t ev) {
  {
    std::lock_guard<std::mutex> lk(s_mu);
    s_cbs.push_back({ev, cb});
  }
  startMonitor();
  return (int)s_cbs.size();
}
int WiFiClass::onEvent(WiFiEventCb cb, arduino_event_id_t ev) {
  return onEvent(WiFiEventFuncCb([cb](arduino_event_id_t e, arduino_event_info_t) { cb(e); }), ev);
}
int WiFiClass::onEvent(WiFiEventSysCb cb, arduino_event_id_t ev) {
  return onEvent(WiFiEventFuncCb([cb](arduino_event_id_t e, arduino_event_info_t i) {
    arduino_event_t x{e, i};
    cb(&x);
  }), ev);
}
