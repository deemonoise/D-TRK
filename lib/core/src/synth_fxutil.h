#pragma once
#include <math.h>
#include <stdint.h>
#include "fastmath.h"
#include "hot.h"

namespace mt {

// Shared helpers for the bus effects (MOD bus, tape delay, shimmer, EQ). Hardware-free.
constexpr float kFxRate = 32000.f;
constexpr int kFxSeg = 32;  // LFO / coefficient segment: values computed once, ramped in between

// sin(2 pi p), p in 0 .. 1 (wraps): parabola + one correction step, error < 0.1 %. LFO use only.
MT_INLINE float fxSin(float p) {
  p -= static_cast<float>(static_cast<int>(p));
  if (p < 0) p += 1.f;
  const float x = 2.f * p - 1.f;                      // -1 .. 1, sin(2 pi p) = -sin(pi x)
  const float y = 4.f * x * (1.f - (x < 0 ? -x : x));  // ~sin(pi x)
  return -(0.225f * (y * (y < 0 ? -y : y) - y) + y);
}

// Rate 0..127 -> 0.05 .. 8 Hz, exponential.
inline float fxRateHz(uint8_t rate) {
  return 0.05f * powf(160.f, (rate > 127 ? 127 : rate) * (1.f / 127.f));
}

// Phase accumulator, phase in 0 .. 1. advance() once per segment.
struct FxLfo {
  float ph = 0;
  MT_INLINE void advance(float hz, int samples) {
    ph += hz * samples * (1.f / kFxRate);
    if (ph >= 1.f) ph -= static_cast<float>(static_cast<int>(ph));
  }
  MT_INLINE float at(float offset) const { return fxSin(ph + offset); }
};

// Linear interpolated read of an int16 ring, `off` samples behind write position w (off >= 1,
// off <= len - 2). Returns line units (int16 scale).
MT_INLINE float fracRead(const int16_t* buf, uint32_t len, uint32_t w, float off) {
  const int io = static_cast<int>(off);
  const float f = off - static_cast<float>(io);
  int32_t i0 = static_cast<int32_t>(w) - io;  // newer sample (offset io)
  if (i0 < 0) i0 += static_cast<int32_t>(len);
  int32_t i1 = i0 - 1;  // older sample (offset io + 1)
  if (i1 < 0) i1 += static_cast<int32_t>(len);
  const float a = buf[i0];
  return a + (static_cast<float>(buf[i1]) - a) * f;
}

// fracRead for a power-of-two ring (mask = len - 1): no wrap branches.
MT_INLINE float fracReadMask(const int16_t* buf, uint32_t mask, uint32_t w, float off) {
  const int io = static_cast<int>(off);
  const float f = off - static_cast<float>(io);
  const uint32_t i0 = (w - static_cast<uint32_t>(io)) & mask;  // newer sample (offset io)
  const uint32_t i1 = (i0 - 1u) & mask;                        // older sample (offset io + 1)
  const float a = buf[i0];
  return a + (static_cast<float>(buf[i1]) - a) * f;
}

// Float (already in line units) -> int16 with saturation, rounded (no lrintf: a flash call).
// The S3: ROUND.S (to nearest; saturates to int32 out of range) + CLAMPS to int16, no branches.
MT_INLINE int16_t toLine(float x) {
#if defined(__XTENSA__) && !defined(__XTENSA_SOFT_FLOAT__)
  int32_t r;
  __asm__("round.s %0, %1, 0" : "=a"(r) : "f"(x));
  __asm__("clamps %0, %0, 15" : "+a"(r));
  return static_cast<int16_t>(r);
#else
  x = x > 32767.f ? 32767.f : (x < -32768.f ? -32768.f : x);
  return static_cast<int16_t>(x < 0 ? x - 0.5f : x + 0.5f);
#endif
}

// Soft saturation x / (1 + k |x|), unity slope at 0, |y| < 1 / k.
// No division: two Newton steps (rel. error < 1e-5).
MT_INLINE float softSat(float x, float k) {
  const float d = 1.f + k * (x < 0 ? -x : x);
  float r = ffrom(0x7EF311C3u - fbits(d));
  r = r * (2.f - d * r);
  r = r * (2.f - d * r);
  return x * r;
}

// One-pole TPT (bilinear) low-pass: exact 0 at Nyquist, 1 at DC. g = tan(pi fc / fs).
struct OnePoleTpt {
  float s = 0;
  MT_INLINE float lp(float x, float G) {  // G = g / (1 + g)
    const float v = (x - s) * G;
    const float y = v + s;
    s = y + v;
    return y;
  }
  static float coef(float hz) {
    const float g = tanf(3.14159265f * hz / kFxRate);
    return g / (1.f + g);
  }
  void flush() {
    if (fabsf(s) < 1e-15f) s = 0;
  }
};

}  // namespace mt
