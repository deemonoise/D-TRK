#include "wdog.h"
#include <Arduino.h>

namespace wdog {

namespace {
bool byWatchdog = false;
}

void captureReset() {
  byWatchdog = SRC_SRSR & SRC_SRSR_WDOG_RST_B;
  SRC_SRSR = SRC_SRSR_WDOG_RST_B;  // write 1 to clear: the next boot reports its own cause
}

bool resetByWatchdog() { return byWatchdog; }

void begin() {
  CCM_CCGR3 |= CCM_CCGR3_WDOG1(3);
  WDOG1_WMCR = 0;  // no power-down counter (it resets 16 s after boot unless cleared)
  // WT: (n + 1) x 0.5 s. SRS and WDA stay 1 (0 would reset at once); WDE is write-once.
  WDOG1_WCR = WDOG_WCR_WT(kTimeoutMs / 500 - 1) | WDOG_WCR_SRS | WDOG_WCR_WDA | WDOG_WCR_WDZST | WDOG_WCR_WDE;
  feed();
}

}  // namespace wdog
