#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

#include "ESPmDNS.h"
#include "Update.h"
#include "mdns.h"

UpdateClass Update;
MDNSResponder MDNS;

bool MDNSResponder::begin(const char* hostname) {
  host_ = hostname ? hostname : "";
  return true;
}

void MDNSResponder::end() {
  if (pid_ > 0) {
    kill(pid_, SIGTERM);
    waitpid(pid_, nullptr, 0);
  }
  pid_ = -1;
  service_.clear();
  txt_.clear();
}

bool MDNSResponder::addService(const char* service, const char* proto, uint16_t port) {
  service_ = service ? service : "http";
  proto_ = proto ? proto : "tcp";
  if (!service_.empty() && service_[0] == '_') service_.erase(0, 1);
  if (!proto_.empty() && proto_[0] == '_') proto_.erase(0, 1);
  port_ = port;
  txt_.clear();
  republish();
  return true;
}

bool MDNSResponder::addServiceTxt(const char*, const char*, const char* key, const char* value) {
  std::string kv = std::string(key ? key : "") + "=" + (value ? value : "");
  for (auto& t : txt_) {
    if (t.compare(0, t.find('='), key ? key : "") == 0) { t = kv; republish(); return true; }
  }
  txt_.push_back(kv);
  republish();
  return true;
}

void MDNSResponder::republish() {
  if (pid_ > 0) {
    kill(pid_, SIGTERM);
    waitpid(pid_, nullptr, 0);
    pid_ = -1;
  }
  if (service_.empty() || access("/usr/bin/avahi-publish-service", X_OK) != 0) return;
  std::string type = "_" + service_ + "._" + proto_;
  std::string port = std::to_string(port_);
  std::vector<std::string> args = {"avahi-publish-service", instance_, type, port};
  for (auto& t : txt_) args.push_back(t);
  pid_t pid = fork();
  if (pid == 0) {
    std::vector<char*> a;
    for (auto& s : args) a.push_back(const_cast<char*>(s.c_str()));
    a.push_back(nullptr);
    setpgid(0, 0);
    execv("/usr/bin/avahi-publish-service", a.data());
    _exit(127);
  }
  pid_ = pid;
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
