#pragma once
#include <stdint.h>
#include <string.h>
#include "hot.h"

namespace mt {

// exp2 / log2 inline for the audio path: newlib's sit in flash on the ESP32, and a flash cache miss
// costs ~1 us (the cache is shared with PSRAM and the UI keeps evicting it). Accurate to float
// precision (truncation error < 1e-8 relative).

// 2^x, x in about -126 .. 127.
MT_INLINE float fastExp2(float x) {
  x = x < -126.f ? -126.f : (x > 127.f ? 127.f : x);
  const int i = static_cast<int>(x < 0 ? x - 0.5f : x + 0.5f);  // round: f in -0.5 .. 0.5
  const float t = (x - static_cast<float>(i)) * 0.69314718f;   // e^t = 2^f
  // Taylor to t^7: the next term is < 6e-9 for |t| <= 0.347.
  const float p =
      1.f + t * (1.f + t * (0.5f + t * (1.f / 6 + t * (1.f / 24 + t * (1.f / 120 + t * (1.f / 720 + t * (1.f / 5040)))))));
  const uint32_t bits = static_cast<uint32_t>(i + 127) << 23;
  float s;
  memcpy(&s, &bits, sizeof(s));
  return p * s;
}

// log2(x), x > 0 and normal.
MT_INLINE float fastLog2(float x) {
  uint32_t bits;
  memcpy(&bits, &x, sizeof(bits));
  int e = static_cast<int>((bits >> 23) & 0xFF) - 127;
  bits = (bits & 0x007FFFFFu) | 0x3F800000u;  // mantissa m in 1 .. 2
  float m;
  memcpy(&m, &bits, sizeof(m));
  if (m > 1.41421356f) {  // m in 0.707 .. 1.414: |s| <= 0.1716 below
    m *= 0.5f;
    ++e;
  }
  // ln m = 2 atanh(s), s = (m - 1) / (m + 1): the series to s^9, the next term < 1e-9.
  const float s = (m - 1.f) / (m + 1.f), s2 = s * s;
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
  return sn / cs;
}

// e^x and x^y (x > 0) through the two above; tanh(x), x >= 0.
MT_INLINE float fastExp(float x) { return fastExp2(x * 1.44269504f); }
MT_INLINE float fastPow(float x, float y) { return fastExp2(y * fastLog2(x)); }
MT_INLINE float fastTanh(float x) { return 1.f - 2.f / (fastExp(2.f * x) + 1.f); }

}  // namespace mt
