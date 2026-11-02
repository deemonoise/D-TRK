#include "synth_osc.h"
#include "fastmath.h"
#include "hot.h"
#include <math.h>

namespace mt {

float gSine[kSineLen + 1];
namespace {
struct SineInit {
  SineInit() {
    for (int i = 0; i <= kSineLen; ++i) gSine[i] = sinf(6.2831853f * i / kSineLen);
  }
} sineInit;
}  // namespace

namespace {
constexpr uint32_t ayClock(uint32_t s) {
  const uint32_t next = ((s >> 1) | (((s ^ (s >> 3)) & 1u) << 16)) & 0x1ffffu;
  return next ? next : 1;
}
// Fibonacci output window -> equivalent Galois register (and back: this map is its own inverse).
constexpr uint32_t ayBasis(uint32_t s) { return s ^ ((s & 7u) << 14); }
constexpr uint32_t ayMultiply(uint32_t a, uint32_t b) {
  uint32_t result = 0;
  while (b) {
    if (b & 1u) result ^= a;
    a = (a << 1) ^ ((0u - (a >> 16)) & 0x24001u);
    b >>= 1;
  }
  return result;
}
struct AyPowers {
  uint32_t value[17] = {};
  constexpr AyPowers() {
    value[0] = 0x12000u; // x^-1 modulo x^17+x^14+1: one Galois clock.
    for (int i = 1; i < 17; ++i) value[i] = ayMultiply(value[i-1], value[i-1]);
  }
};
constexpr AyPowers kAyPowers;
}

MT_HOT __attribute__((noinline)) uint32_t chipAyAdvance(uint32_t state, uint32_t clocks) {
  if (!clocks) return state;
  // Recover exactly as the first old clock after SID/another register family or seed zero.
  if (!state || state > 0x1ffffu) {
    state = ayClock(state);
    --clocks;
  }
  if (clocks >= 131071u) clocks %= 131071u;
  if (clocks >= 128u) {
    uint32_t g = ayBasis(state);
    while (clocks >= 128u) {
      const unsigned bit = 31u - static_cast<unsigned>(__builtin_clz(clocks));
      g = ayMultiply(g, kAyPowers.value[bit]);
      clocks -= 1u << bit;
    }
    state = ayBasis(g);
  }
  // Eight clocks in one shift/xor: feedback has not reached tap 3 again yet.
  while (clocks >= 8u) {
    state = (state >> 8) | (((state ^ (state >> 3)) & 0xffu) << 9);
    clocks -= 8;
  }
  while (clocks--) state = ayClock(state);
  return state;
}

MT_HOT float noteHz(float note) { return 440.f * fastExp2((note - 69.f) * (1.f / 12.f)); }

}  // namespace mt
