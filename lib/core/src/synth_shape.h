#pragma once
#include <math.h>
#include "fastmath.h"
#include "hot.h"

namespace mt {

// Waveshaping curves: the track insert's drive (ModelDrive FOLD: the curve, its antiderivative and
// the first-order ADAA step) and the SYNTH wavefolder (foldTri: the same curve, naive).

// w mod 4 in [0, 4), no floorf (a flash call per sample on the ESP32); |u| is clamped so the int
// cast stays in range.
MT_INLINE float wrap4(float w) {
  int q = static_cast<int>(w * 0.25f);
  if (static_cast<float>(q) * 4.f > w) --q;
  return w - static_cast<float>(q) * 4.f;
}
MT_INLINE float foldClamp(float u) { return u < -1024.f ? -1024.f : (u > 1024.f ? 1024.f : u); }
// Triangle fold: 0 at 0, 1 at 1, 0 at 2, -1 at 3 (period 4, odd).
MT_INLINE float foldCurve(float u) { return fabsf(wrap4(foldClamp(u) - 1.f) - 2.f) - 1.f; }
// Its antiderivative: periodic (zero mean), so the wrap does not break the differences.
MT_INLINE float foldAnti(float u) {
  const float w = wrap4(foldClamp(u) - 1.f);
  return w <= 2.f ? w - 0.5f * w * w : 0.5f * w * w - 3.f * w + 4.f;
}

// foldCurve for |u| <= 60 without a branch (the SYNTH fold, naive: no ADAA): u is clamped to +-60
// and offset by a multiple of 4 into 3 .. 123, where the int cast is a floor. Beyond +-60 it holds
// at foldCurve(+-60) = 0 (a NaN too: fclamp sends it to 60); SYNTH's |u| <= ~12 never gets there.
MT_INLINE float foldTri(float u) {
  const float w = fclamp(u, -60.f, 60.f) + 63.f;  // u - 1 + 64
  const float r = w - static_cast<float>(static_cast<int>(w * 0.25f)) * 4.f;
  return fabsf(r - 2.f) - 1.f;
}

// Below this |u - u1| the ADAA quotient loses precision: f at the midpoint instead (off by at most
// ~|u - u1| / 4 next to a kink).
constexpr float kAdaaEps = 1e-3f;

// One first-order ADAA sample: (F(u) - F(u1)) / (u - u1), Fu = F(u), Fp = F(u1).
template <class Fn>
MT_INLINE float adaaStep(float u, float u1, float Fu, float Fp, Fn f) {
  const float d = u - u1;
  const float ad = fabsf(d);
#if defined(__IMXRT1062__)
  return ad > kAdaaEps ? (Fu - Fp) / d : f(0.5f * (u + u1));  // a VDIV: no sign branch
#else
  // No '/': a __divsf3 call per sample on the S3 (fastRcp, rel. error < 1e-7).
  return ad > kAdaaEps ? (Fu - Fp) * (d < 0 ? -fastRcp(ad) : fastRcp(ad)) : f(0.5f * (u + u1));
#endif
}

}  // namespace mt
