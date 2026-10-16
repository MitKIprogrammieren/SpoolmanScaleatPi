#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "Arduino.h"
#include "Preferences.h"
#include "esp_mac.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "pi_paths.h"

esp_reset_reason_t esp_reset_reason() {
  // systemd restarts after ESP.restart() look like a software reset.
  const char* r = getenv("SPOOLMANSCALE_RESTARTED");
  return r ? ESP_RST_SW : ESP_RST_POWERON;
}
void esp_restart() { ESP.restart(); }
uint32_t esp_get_free_heap_size() { return ESP.getFreeHeap(); }
uint32_t esp_get_minimum_free_heap_size() { return ESP.getFreeHeap(); }

uint32_t esp_random() {
  static std::random_device rd;
  static std::mutex mu;
  std::lock_guard<std::mutex> lk(mu);
  return rd();
}
void esp_fill_random(void* buf, size_t len) {
  uint8_t* b = (uint8_t*)buf;
  for (size_t i = 0; i < len; i++) b[i] = (uint8_t)esp_random();
}

esp_err_t esp_read_mac(uint8_t* mac, esp_mac_type_t) {
  uint64_t m = ESP.getEfuseMac();
  for (int i = 0; i < 6; i++) mac[i] = (uint8_t)(m >> (8 * i));
  return ESP_OK;
}
esp_err_t esp_efuse_mac_get_default(uint8_t* mac) { return esp_read_mac(mac, ESP_MAC_WIFI_STA); }

void esp_deep_sleep_start() {
  fprintf(stderr, "esp_deep_sleep_start(): no deep sleep on the Pi, staying up\n");
}

int64_t esp_timer_get_time() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

// ---- partitions as files ------------------------------------------------------
namespace {
struct PartDef {
  const char* label;
  esp_partition_type_t type;
  esp_partition_subtype_t subtype;
  uint32_t size;
};
// The data partitions of partitions.csv that the firmware opens by itself.
const PartDef kParts[] = {
    {"coredump", ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, 0x10000},
    {"spiffs", ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, 0x3E0000},
};
std::mutex s_part_mu;
std::map<std::string, esp_partition_t*> s_open;

int partFd(const esp_partition_t* p) {
  std::string path = piDataPath((std::string("part_") + p->label + ".bin").c_str());
  int fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  if (fd < 0) return -1;
  struct stat st;
  if (fstat(fd, &st) == 0 && (uint32_t)st.st_size < p->size) {
    // Fresh flash reads as 0xff.
    std::string ff(64 * 1024, (char)0xff);
    for (off_t o = st.st_size; o < (off_t)p->size; o += (off_t)ff.size()) {
      size_t n = (size_t)((off_t)p->size - o < (off_t)ff.size() ? (off_t)p->size - o : (off_t)ff.size());
      if (pwrite(fd, ff.data(), n, o) != (ssize_t)n) break;
    }
  }
  return fd;
}
}  // namespace

const esp_partition_t* esp_partition_find_first(esp_partition_type_t type, esp_partition_subtype_t subtype,
                                                const char* label) {
  std::lock_guard<std::mutex> lk(s_part_mu);
  for (const PartDef& d : kParts) {
    if (type != ESP_PARTITION_TYPE_ANY && d.type != type) continue;
    if (subtype != ESP_PARTITION_SUBTYPE_ANY && d.subtype != subtype && !label) continue;
    if (label && strcmp(label, d.label) != 0) continue;
    auto it = s_open.find(d.label);
    if (it != s_open.end()) return it->second;
    esp_partition_t* p = new esp_partition_t();
    p->type = d.type;
    p->subtype = d.subtype;
    p->size = d.size;
    p->erase_size = 4096;
    snprintf(p->label, sizeof(p->label), "%s", d.label);
    s_open[d.label] = p;
    return p;
  }
  return nullptr;
}

esp_err_t esp_partition_read(const esp_partition_t* p, size_t off, void* dst, size_t len) {
  if (!p || off + len > p->size) return ESP_ERR_INVALID_SIZE;
  std::lock_guard<std::mutex> lk(s_part_mu);
  int fd = partFd(p);
  if (fd < 0) return ESP_FAIL;
  ssize_t r = pread(fd, dst, len, (off_t)off);
  close(fd);
  return r == (ssize_t)len ? ESP_OK : ESP_FAIL;
}

esp_err_t esp_partition_write(const esp_partition_t* p, size_t off, const void* src, size_t len) {
  if (!p || off + len > p->size) return ESP_ERR_INVALID_SIZE;
  std::lock_guard<std::mutex> lk(s_part_mu);
  int fd = partFd(p);
  if (fd < 0) return ESP_FAIL;
  // NOR flash only clears bits; emulate that so code relying on it behaves.
  std::string cur(len, '\0');
  if (pread(fd, &cur[0], len, (off_t)off) != (ssize_t)len) { close(fd); return ESP_FAIL; }
  const uint8_t* s = (const uint8_t*)src;
  for (size_t i = 0; i < len; i++) cur[i] = (char)((uint8_t)cur[i] & s[i]);
  ssize_t w = pwrite(fd, cur.data(), len, (off_t)off);
  fsync(fd);
  close(fd);
  return w == (ssize_t)len ? ESP_OK : ESP_FAIL;
}

esp_err_t esp_partition_erase_range(const esp_partition_t* p, size_t off, size_t len) {
  if (!p || off + len > p->size) return ESP_ERR_INVALID_SIZE;
  std::lock_guard<std::mutex> lk(s_part_mu);
  int fd = partFd(p);
  if (fd < 0) return ESP_FAIL;
  std::string ff(len, (char)0xff);
  ssize_t w = pwrite(fd, ff.data(), len, (off_t)off);
  close(fd);
  return w == (ssize_t)len ? ESP_OK : ESP_FAIL;
}

// ---- NVS (raw API) on top of Preferences ---------------------------------------
namespace {
std::map<nvs_handle_t, std::string> s_nvs;
nvs_handle_t s_nvs_next = 1;
}
esp_err_t nvs_open(const char* ns, nvs_open_mode_t, nvs_handle_t* out) {
  *out = s_nvs_next++;
  s_nvs[*out] = ns;
  return ESP_OK;
}
void nvs_close(nvs_handle_t h) { s_nvs.erase(h); }
esp_err_t nvs_get_u8(nvs_handle_t h, const char* key, uint8_t* out) {
  Preferences p;
  if (!s_nvs.count(h) || !p.begin(s_nvs[h].c_str(), true)) return ESP_FAIL;
  if (!p.isKey(key)) { p.end(); return ESP_ERR_NVS_NOT_FOUND; }
  *out = p.getUChar(key, 0);
  p.end();
  return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t h, const char* key, uint8_t v) {
  Preferences p;
  if (!s_nvs.count(h) || !p.begin(s_nvs[h].c_str(), false)) return ESP_FAIL;
  p.putUChar(key, v);
  p.end();
  return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }
esp_err_t nvs_flash_init() { return ESP_OK; }
esp_err_t nvs_flash_erase() {
  piPreferencesEraseAll();
  return ESP_OK;
}

// The CA bundle the firmware links in; the Pi uses the system CA store.
extern "C" {
const uint8_t pi_x509_bundle_start[1] __asm__("_binary_x509_crt_bundle_start") = {0};
const uint8_t pi_x509_bundle_end[1] __asm__("_binary_x509_crt_bundle_end") = {0};
}
