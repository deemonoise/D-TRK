#pragma once
#include <stdint.h>

// WDOG1: resets the board when loop() stops coming round (SdFat's FIFO SDIO waits have no timeout:
// a card that stops answering mid-transfer would hang the board for good). The reset reason is
// logged at the next boot.
namespace wdog {

constexpr uint32_t kTimeoutMs = 8000;  // longer than any loop() pass (a full FAT scan, a flash erase)

// setup(), first: reads (and clears) the reset cause.
void captureReset();
// The last reset was the watchdog's (logged at boot and on every Hello: the USB port may not be open
// at boot).
bool resetByWatchdog();
// setup(), last: starts it; it cannot be stopped again.
void begin();
// loop(), every pass; also from RAM code holding interrupts off (a firmware move): inline, no flash.
inline void feed() {
  *reinterpret_cast<volatile uint16_t*>(0x400B8002) = 0x5555;  // WDOG1_WSR
  *reinterpret_cast<volatile uint16_t*>(0x400B8002) = 0xAAAA;
}

}  // namespace wdog
