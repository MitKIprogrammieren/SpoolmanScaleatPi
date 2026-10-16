#include "Arduino.h"

#include <arpa/inet.h>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <random>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <thread>

static const auto s_t0 = std::chrono::steady_clock::now();

extern "C" unsigned long millis(void) {
  return (unsigned long)(uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - s_t0).count();
}
unsigned long micros() {
  return (unsigned long)(uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - s_t0).count();
}
void delay(unsigned long ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
void delayMicroseconds(unsigned int us) { std::this_thread::sleep_for(std::chrono::microseconds(us)); }
void yield() { std::this_thread::yield(); }

// GPIO is not used by the Pi build (PN532 on UART needs no reset line,
// backlight is handled by the display driver). Kept as no-ops.
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t, uint8_t) {}
int digitalRead(uint8_t) { return HIGH; }
int analogRead(uint8_t) { return 0; }
void analogWrite(uint8_t, int) {}

static std::mt19937& rng() {
  static std::mt19937 g{std::random_device{}()};
  return g;
}
long random(long mx) { return mx <= 0 ? 0 : (long)(rng()() % (unsigned long)mx); }
long random(long mn, long mx) { return mx <= mn ? mn : mn + random(mx - mn); }
void randomSeed(unsigned long seed) { rng().seed((uint32_t)seed); }
long map(long x, long in_min, long in_max, long out_min, long out_max) {
  if (in_max == in_min) return out_min;
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
uint16_t makeWord(uint16_t w) { return w; }
uint16_t makeWord(uint8_t h, uint8_t l) { return (uint16_t)((h << 8) | l); }

// ---- Serial ---------------------------------------------------------------
HardwareSerial Serial;

size_t HardwareSerial::write(uint8_t c) { return fwrite(&c, 1, 1, stdout); }
size_t HardwareSerial::write(const uint8_t* buf, size_t n) {
  size_t w = fwrite(buf, 1, n, stdout);
  if (n && buf[n - 1] == '\n') fflush(stdout);
  return w;
}
void HardwareSerial::flush() { fflush(stdout); }
int HardwareSerial::available() {
  if (peeked_ >= 0) return 1;
  if (!isatty(0)) return 0;
  struct pollfd p = {0, POLLIN, 0};
  return poll(&p, 1, 0) > 0 && (p.revents & POLLIN) ? 1 : 0;
}
int HardwareSerial::read() {
  if (peeked_ >= 0) { int c = peeked_; peeked_ = -1; return c; }
  if (!available()) return -1;
  unsigned char c;
  return ::read(0, &c, 1) == 1 ? c : -1;
}
int HardwareSerial::peek() {
  if (peeked_ < 0) peeked_ = read();
  return peeked_;
}

// ---- ESP ------------------------------------------------------------------
EspClass ESP;

uint32_t EspClass::getFreeHeap() {
  struct sysinfo si;
  if (sysinfo(&si) != 0) return 64u * 1024 * 1024;
  unsigned long long b = (unsigned long long)si.freeram * si.mem_unit;
  return b > 0x7fffffffULL ? 0x7fffffffU : (uint32_t)b;
}
uint32_t EspClass::getHeapSize() {
  struct sysinfo si;
  if (sysinfo(&si) != 0) return 0;
  unsigned long long b = (unsigned long long)si.totalram * si.mem_unit;
  return b > 0x7fffffffULL ? 0x7fffffffU : (uint32_t)b;
}

uint64_t EspClass::getEfuseMac() {
  // The MAC of the first network interface, in the byte order the ESP core uses.
  const char* ifs[] = {"/sys/class/net/eth0/address", "/sys/class/net/wlan0/address",
                       "/sys/class/net/end0/address"};
  for (const char* path : ifs) {
    FILE* f = fopen(path, "r");
    if (!f) continue;
    unsigned int m[6];
    int n = fscanf(f, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]);
    fclose(f);
    if (n == 6) {
      uint64_t v = 0;
      for (int i = 5; i >= 0; i--) v = (v << 8) | m[i];
      return v;
    }
  }
  return 0x0000a1b2c3d4e5f6ULL;
}

uint32_t EspClass::getSketchSize() {
  struct stat st;
  if (stat("/proc/self/exe", &st) != 0) return 0;
  return st.st_size > 0x5fffff ? 0x5fffff : (uint32_t)st.st_size;
}

void EspClass::restart() {
  // systemd (Restart=always) brings the program back up.
  fflush(stdout);
  fprintf(stderr, "ESP.restart(): exiting for a restart by systemd\n");
  _exit(3);
}

// ---- IPAddress ------------------------------------------------------------
const IPAddress INADDR_NONE_IP(0, 0, 0, 0);

bool IPAddress::fromString(const char* s) {
  struct in_addr a;
  if (!s || inet_pton(AF_INET, s, &a) != 1) return false;
  v_ = a.s_addr;
  return true;
}
String IPAddress::toString() const {
  char b[20];
  snprintf(b, sizeof(b), "%u.%u.%u.%u", (*this)[0], (*this)[1], (*this)[2], (*this)[3]);
  return String(b);
}
