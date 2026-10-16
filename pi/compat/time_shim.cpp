// esp32-hal-time on Linux: the clock is kept by the OS (systemd-timesyncd),
// so configTime() only sets the time zone.
#include "Arduino.h"

#include <cstdlib>
#include <ctime>

bool getLocalTime(struct tm* info, uint32_t ms) {
  const unsigned long t0 = millis();
  for (;;) {
    time_t now = time(nullptr);
    localtime_r(&now, info);
    // Like the ESP: a clock that was never set reads 1970.
    if (info->tm_year > (2016 - 1900)) return true;
    if (millis() - t0 >= ms) return false;
    delay(10);
  }
}

void configTime(long gmt_offset_sec, int daylight_offset_sec, const char*, const char*, const char*) {
  // POSIX TZ offsets count west of Greenwich as positive.
  char tz[32];
  long off = -(gmt_offset_sec + daylight_offset_sec);
  snprintf(tz, sizeof(tz), "UTC%+ld:%02ld", off / 3600, labs(off % 3600) / 60);
  setenv("TZ", tz, 1);
  tzset();
}

void configTzTime(const char* tz, const char*, const char*, const char*) {
  if (tz && *tz) setenv("TZ", tz, 1);
  tzset();
}
