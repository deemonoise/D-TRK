#include "clock.h"
#include <Arduino.h>

uint64_t micros64() {
  static uint32_t last = 0, hi = 0;
  uint32_t primask;
  __asm__ volatile("mrs %0, primask" : "=r"(primask));
  __disable_irq();
  const uint32_t m = micros();
  if (m < last) hi++;
  last = m;
  const uint64_t r = (static_cast<uint64_t>(hi) << 32) | m;
  if (!primask) __enable_irq();
  return r;
}
