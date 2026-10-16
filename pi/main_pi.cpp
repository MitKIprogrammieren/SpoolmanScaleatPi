// Entry point of the Raspberry Pi build: the Arduino setup()/loop() pair of
// the firmware, run by a plain main().

#include <Arduino.h>
#include <csignal>

#include "app/app_boot.h"
#include "app/app_loop.h"
#include "hardware/sd_logger.h"

static volatile sig_atomic_t s_stop = 0;

static void onSignal(int) { s_stop = 1; }

int main() {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGTERM, onSignal);
  signal(SIGINT, onSignal);

  logRingInit();
  appSetup();
  while (!s_stop) {
    appLoop();
    // The ESP loop task yields once per pass as well; this keeps an idle
    // scale near 0 % CPU without slowing LVGL (it refreshes every 16 ms).
    delay(2);
  }
  fprintf(stderr, "SpoolmanScale: stopping\n");
  exit(0);
}
