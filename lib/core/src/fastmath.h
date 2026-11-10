#pragma once
#include <stdint.h>
#include <string.h>
#include "hot.h"

namespace mt {

// exp2 / log2 inline for the audio path: newlib's sit in flash on the ESP32, and a flash cache miss
// costs ~1 us (the cache is shared with PSRAM and the UI keeps evicting it). Accurate to float
// precision (truncation error < 1e-8 relative).

// Float <-> bits. On the S3 a memcpy pun goes through the stack (store, load: a pipeline stall);
// RFR / WFR move between the register files directly.
MT_INLINE uint32_t fbits(float f) {
#if defined(__XTENSA__) && !defined(__XTENSA_SOFT_FLOAT__)
  uint32_t u;
  __asm__("rfr %0, %1" : "=a"(u) : "f"(f));
  return u;
#else
  uint32_t u;
  __builtin_memcpy(&u, &f, sizeof(u));
  return u;
#endif
}
MT_INLINE float ffrom(uint32_t u) {
#if defined(__XTENSA__) && !defined(__XTENSA_SOFT_FLOAT__)
  float f;
  __asm__("wfr %0, %1" : "=f"(f) : "a"(u));
  return f;
#else
  float f;
  __builtin_memcpy(&f, &u, sizeof(f));
  return f;
#endif
}

// min / max. On the Teensy (FPv5) single VMINNM / VMAXNM: the ternary is a compare + VMRS (waits for
// the compare) + select. A NaN operand gives the other one there. Elsewhere a ternary, not newlib's
// fminf / fmaxf (flash calls on the ESP32: a windowed call into the flash cache per sample, and the
// caller's float registers spilled around it); no NaN handling.
#if defined(__IMXRT1062__)
MT_INLINE float minf(float a, float b) { return __builtin_fminf(a, b); }
MT_INLINE float maxf(float a, float b) { return __builtin_fmaxf(a, b); }
#else
MT_INLINE float minf(float a, float b) { return a < b ? a : b; }
MT_INLINE float maxf(float a, float b) { return a > b ? a : b; }
#endif
// x clamped to lo .. hi (lo <= hi). On the Teensy (FPv5) fminf / fmaxf are single VMINNM / VMAXNM
// instructions: a compare + VMRS + select (a pipeline stall) or a branch per bound otherwise.
// NaN -> hi on every target: VMINNM returns the non-NaN operand (hi); the ternary below fails
// 'x < hi' for a NaN. So a clamped value is always safe to cast to int (foldTri).
MT_INLINE float fclamp(float x, float lo, float hi) {
#if defined(__IMXRT1062__)
  return __builtin_fmaxf(__builtin_fminf(x, hi), lo);
#else
  return !(x < hi) ? hi : (x < lo ? lo : x);
#endif
}

// 1 / d for 1e-30 <= d < 2^126 (positive, normal; callers guard d > 0: d = 0 is NaN here, inf on the
// Teensy): a bit-trick seed (|err| < 12 %) and three Newton steps,
// relative error < 1e-7. No division: the S3 has no divide instruction, a float '/' is a call to
// __divsf3 (ROM) per sample. On the Teensy a VDIV (14 cycles, exact): the seed and the Newton steps are
// ~12 instructions in one dependent chain, longer than the divide.
MT_INLINE float fastRcp(float d) {
#if defined(__IMXRT1062__)
  return 1.f / d;
#else
  float r = ffrom(0x7EF311C3u - fbits(d));
  r = r * (2.f - d * r);
  r = r * (2.f - d * r);
  r = r * (2.f - d * r);
  return r;
#endif
}

// 2^x, x in about -126 .. 127.
MT_INLINE float fastExp2(float x) {
  x = x < -126.f ? -126.f : (x > 127.f ? 127.f : x);
  const int i = static_cast<int>(x < 0 ? x - 0.5f : x + 0.5f);  // round: f in -0.5 .. 0.5
  const float t = (x - static_cast<float>(i)) * 0.69314718f;   // e^t = 2^f
  // Taylor to t^7: the next term is < 6e-9 for |t| <= 0.347.
  const float p =
      1.f + t * (1.f + t * (0.5f + t * (1.f / 6 + t * (1.f / 24 + t * (1.f / 120 + t * (1.f / 720 + t * (1.f / 5040)))))));
  const uint32_t bits = static_cast<uint32_t>(i + 127) << 23;
  return p * ffrom(bits);
}

// log2(x), x > 0 and normal.
MT_INLINE float fastLog2(float x) {
  uint32_t bits = fbits(x);
  int e = static_cast<int>((bits >> 23) & 0xFF) - 127;
  bits = (bits & 0x007FFFFFu) | 0x3F800000u;  // mantissa m in 1 .. 2
  float m = ffrom(bits);
  if (m > 1.41421356f) {  // m in 0.707 .. 1.414: |s| <= 0.1716 below
    m *= 0.5f;
    ++e;
  }
  // ln m = 2 atanh(s), s = (m - 1) / (m + 1): the series to s^9, the next term < 1e-9.
  const float s = (m - 1.f) * fastRcp(m + 1.f), s2 = s * s;  // no __divsf3 (m + 1 in 1.7 .. 2.5)
  const float ln = 2.f * s * (1.f + s2 * (1.f / 3 + s2 * (1.f / 5 + s2 * (1.f / 7 + s2 * (1.f / 9)))));
  return static_cast<float>(e) + ln * 1.44269504f;
}

// tan(w), w in 0 .. 1.45 (the Svf's pi hz / rate up to 0.45 x the rate): sin / cos by their Taylor
// series to w^11 / w^12, truncation < 2e-8.
MT_INLINE float fastTan(float w) {
  const float w2 = w * w;
  const float sn =
      w * (1.f - w2 * (1.f / 6 - w2 * (1.f / 120 - w2 * (1.f / 5040 - w2 * (1.f / 362880 - w2 * (1.f / 39916800))))));
  const float cs = 1.f - w2 * (0.5f - w2 * (1.f / 24 - w2 * (1.f / 720 - w2 * (1.f / 40320 -
                                                                              w2 * (1.f / 3628800 - w2 * (1.f / 479001600))))));
  return sn * fastRcp(cs);  // cs >= cos 1.45 > 0.12
}

// e^x and x^y (x > 0) through the two above; tanh(x) for any x.
MT_INLINE float fastExp(float x) { return fastExp2(x * 1.44269504f); }
MT_INLINE float fastPow(float x, float y) { return fastExp2(y * fastLog2(x)); }
// tanh: 1 in float above x = 9 (keeps fastRcp's argument far below its 2^126 limit); for x < 0 the
// argument is in 1 .. 2 (fastExp2 clamps at 2^-126): -1 below about -9.
MT_INLINE float fastTanh(float x) { return x > 9.f ? 1.f : 1.f - 2.f * fastRcp(fastExp(2.f * x) + 1.f); }
// tanh(x) to 1e-4 (an audio saturator): the (7, 6) continued-fraction approximant, x clamped to +-4.97
// (where it reaches 0.999999; odd, monotonic). ~12 instructions against fastTanh's ~45 (exp2 polynomial,
// rounding, the reciprocal).
MT_INLINE float tanhSoft(float x) {
  x = fclamp(x, -4.97f, 4.97f);
  const float x2 = x * x;
  return x * (135135.f + x2 * (17325.f + x2 * (378.f + x2))) *
         fastRcp(135135.f + x2 * (62370.f + x2 * (3150.f + x2 * 28.f)));
}

}  // namespace mt
