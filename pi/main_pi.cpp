// Entry point of the Raspberry Pi build: the Arduino setup()/loop() pair of
// the firmware, run by a plain main().

#include <Arduino.h>
#include <csignal>

#include "app/app_boot.h"
#include "app/app_loop.h"
#include "hardware/sd_logger.h"

#include <atomic>
#include <thread>

const char* crumbCurrent();   // services/breadcrumb.cpp, Pi build

static volatile sig_atomic_t s_stop = 0;
static std::atomic<unsigned long> s_loop_passes{0};

// Says so in the log when the main loop stops going round, and where it is
// (the firmware's breadcrumb). A frozen loop freezes the display and touch.
static void stallWatchdog() {
  std::thread([] {
    unsigned long last = s_loop_passes.load();
    int stuck_s = 0;
    for (;;) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      const unsigned long now = s_loop_passes.load();
      if (now != last) {
        if (stuck_s >= 5) fprintf(stderr, "Pi: main loop running again after %d s\n", stuck_s);
        last = now;
        stuck_s = 0;
        continue;
      }
      stuck_s++;
      if (stuck_s == 5 || (stuck_s > 5 && stuck_s % 30 == 0))
        fprintf(stderr, "Pi: main loop stuck for %d s at '%s'\n", stuck_s, crumbCurrent());
    }
  }).detach();
}

static void onSignal(int) { s_stop = 1; }

int main() {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGTERM, onSignal);
  signal(SIGINT, onSignal);

  logRingInit();
  stallWatchdog();
  appSetup();
  fprintf(stderr, "Pi: setup done, main loop running\n");
  while (!s_stop) {
    appLoop();
    s_loop_passes++;
    // The ESP loop task yields once per pass as well; this keeps an idle
    // scale near 0 % CPU without slowing LVGL (it refreshes every 16 ms).
    delay(2);
  }
  fprintf(stderr, "SpoolmanScale: stopping\n");
  exit(0);
}
