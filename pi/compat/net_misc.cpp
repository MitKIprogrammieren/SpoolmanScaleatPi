#include <condition_variable>
#include <csignal>
#include <fcntl.h>
#include <mutex>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

#include "ESPmDNS.h"
#include "Update.h"
#include "mdns.h"

UpdateClass Update;
MDNSResponder MDNS;

// The announcement runs as an avahi-publish-service child process. The
// firmware changes the TXT records several times in a row; each change only
// updates the wanted state here, and a worker thread (re)starts the child
// once things have been quiet for a moment. The main loop never waits on a
// process.
namespace {
struct MdnsWanted {
  std::string instance = "SpoolmanScale";
  std::string type;   // "_http._tcp", empty = nothing published
  uint16_t port = 0;
  std::vector<std::string> txt;
  bool operator==(const MdnsWanted& o) const {
    return instance == o.instance && type == o.type && port == o.port && txt == o.txt;
  }
};

std::mutex s_mdns_mu;
std::condition_variable s_mdns_cv;
MdnsWanted s_mdns_want;
unsigned long s_mdns_changed = 0;
bool s_mdns_worker = false;

pid_t spawnAvahi(const MdnsWanted& w) {
  const char* bin = "/usr/bin/avahi-publish-service";
  if (w.type.empty() || access(bin, X_OK) != 0) return -1;
  std::vector<std::string> args = {"avahi-publish-service", w.instance, w.type, std::to_string(w.port)};
  for (auto& t : w.txt) args.push_back(t);
  std::vector<char*> argv;
  for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
  argv.push_back(nullptr);
  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  sigset_t def;
  sigemptyset(&def);
  sigaddset(&def, SIGTERM);
  sigaddset(&def, SIGINT);
  sigaddset(&def, SIGPIPE);
  posix_spawnattr_setsigdefault(&attr, &def);
  sigset_t none;
  sigemptyset(&none);
  posix_spawnattr_setsigmask(&attr, &none);
  posix_spawnattr_setpgroup(&attr, 0);
  posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETPGROUP);
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
  posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
  pid_t pid = -1;
  if (posix_spawn(&pid, bin, &fa, &attr, argv.data(), environ) != 0) pid = -1;
  posix_spawn_file_actions_destroy(&fa);
  posix_spawnattr_destroy(&attr);
  return pid;
}

void stopChild(pid_t& pid) {
  if (pid <= 0) return;
  kill(pid, SIGTERM);
  for (int i = 0; i < 20; i++) {
    if (waitpid(pid, nullptr, WNOHANG) == pid) { pid = -1; return; }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  kill(pid, SIGKILL);
  waitpid(pid, nullptr, 0);
  pid = -1;
}

void mdnsWorker() {
  pid_t child = -1;
  MdnsWanted running;
  for (;;) {
    MdnsWanted want;
    {
      std::unique_lock<std::mutex> lk(s_mdns_mu);
      s_mdns_cv.wait_for(lk, std::chrono::seconds(5));
      // Let a burst of changes settle first.
      if (millis() - s_mdns_changed < 500) {
        lk.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        lk.lock();
      }
      want = s_mdns_want;
    }
    // Reap a child that ended by itself (avahi restarted, for example).
    if (child > 0 && waitpid(child, nullptr, WNOHANG) == child) child = -1;
    if (want == running && (child > 0 || want.type.empty())) continue;
    stopChild(child);
    running = want;
    child = spawnAvahi(want);
  }
}

void mdnsChanged() {
  s_mdns_changed = millis();
  if (!s_mdns_worker) {
    s_mdns_worker = true;
    std::thread(mdnsWorker).detach();
  }
  s_mdns_cv.notify_all();
}
}  // namespace

bool MDNSResponder::begin(const char*) { return true; }

void MDNSResponder::end() {
  std::lock_guard<std::mutex> lk(s_mdns_mu);
  s_mdns_want.type.clear();
  s_mdns_want.txt.clear();
  mdnsChanged();
}

void MDNSResponder::setInstanceName(const String& n) {
  std::lock_guard<std::mutex> lk(s_mdns_mu);
  s_mdns_want.instance = n.c_str();
  mdnsChanged();
}

bool MDNSResponder::addService(const char* service, const char* proto, uint16_t port) {
  std::string sv = service ? service : "http", pr = proto ? proto : "tcp";
  if (!sv.empty() && sv[0] == '_') sv.erase(0, 1);
  if (!pr.empty() && pr[0] == '_') pr.erase(0, 1);
  std::lock_guard<std::mutex> lk(s_mdns_mu);
  s_mdns_want.type = "_" + sv + "._" + pr;
  s_mdns_want.port = port;
  s_mdns_want.txt.clear();
  mdnsChanged();
  return true;
}

bool MDNSResponder::addServiceTxt(const char*, const char*, const char* key, const char* value) {
  const std::string k = key ? key : "";
  const std::string kv = k + "=" + (value ? value : "");
  std::lock_guard<std::mutex> lk(s_mdns_mu);
  bool found = false;
  for (auto& t : s_mdns_want.txt)
    if (t.compare(0, t.find('='), k) == 0) { t = kv; found = true; }
  if (!found) s_mdns_want.txt.push_back(kv);
  mdnsChanged();
  return true;
}

esp_err_t mdns_service_remove(const char*, const char*) {
  MDNS.end();
  return ESP_OK;
}
esp_err_t mdns_service_txt_item_set(const char* s, const char* p, const char* k, const char* v) {
  MDNS.addServiceTxt(s, p, k, v);
  return ESP_OK;
}
esp_err_t mdns_hostname_set(const char*) { return ESP_OK; }
void mdns_free() { MDNS.end(); }
