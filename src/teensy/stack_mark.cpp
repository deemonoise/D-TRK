#include "stack_mark.h"
#include <Arduino.h>

extern "C" unsigned long _ebss;  // the end of .bss in DTCM: the stack's floor (startup.c guards 32 B there)

namespace stackmark {
namespace {

constexpr uint32_t kPaint = 0xC5AC5AC5;
constexpr uint32_t kMargin = 64;  // left unpainted below paint()'s own frame
uint32_t* floor_ = nullptr;       // the first painted word, above the MPU guard
uint32_t* top_ = nullptr;         // one past the last painted word

}  // namespace

FLASHMEM void paint() {
  // Above the guard: startup.c puts a 32-byte no-access region at _ebss (rounded to 32 by the MPU).
  const uint32_t lo = ((reinterpret_cast<uint32_t>(&_ebss) + 31) & ~31u) + 32;
  uint32_t sp;
  asm volatile("mov %0, sp" : "=r"(sp));
  floor_ = reinterpret_cast<uint32_t*>(lo);
  top_ = reinterpret_cast<uint32_t*>((sp - kMargin) & ~3u);
  // Everything below sp is free, but only while nothing pushes there: no interrupt frame (irq off)
  // and no call (volatile keeps the loop from becoming a memset, whose frame would be painted over).
  __disable_irq();
  for (volatile uint32_t* p = floor_; p < top_; ++p) *p = kPaint;
  __enable_irq();
}

// From the floor up to the first word written over: the deepest the stack has reached.
FLASHMEM uint32_t minFree() {
  if (!floor_) return 0xFFFFFFFF;
  const uint32_t* p = floor_;
  while (p < top_ && *p == kPaint) ++p;
  return static_cast<uint32_t>(reinterpret_cast<const uint8_t*>(p) - reinterpret_cast<const uint8_t*>(floor_));
}

}  // namespace stackmark
