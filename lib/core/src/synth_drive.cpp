#include "synth_drive.h"
#include "fastmath.h"

namespace mt {

float Drive::tab_[Drive::kTab];
float Drive::slope_[Drive::kTab];
bool Drive::ready_ = false;

void Drive::build() {
  for (int i = 0; i < kTab; ++i) tab_[i] = tanhf(-8.f + i * (1.f / 16.f));
  for (int i = 0; i < kTab - 1; ++i) slope_[i] = tab_[i + 1] - tab_[i];
  slope_[kTab - 1] = 0;
  ready_ = true;
}
namespace {

// Below this |u - u1| the ADAA quotient loses precision: f at the midpoint instead (off by at most
// ~|u - u1| / 4 next to a kink).
constexpr float kAdaaEps = 1e-3f;
// DC blocker pole: 1 - 2 pi 10 Hz / 32 kHz.
constexpr float kDcR = 1.f - 2.f * 3.14159265f * 10.f / 32000.f;

MT_INLINE float clip(float u) { return u < -1.f ? -1.f : (u > 1.f ? 1.f : u); }
MT_INLINE float clipF(float u) {
  const float a = fabsf(u);
  return a <= 1.f ? 0.5f * u * u : a - 0.5f;
}

// w mod 4 in [0, 4), no floorf (a flash call per sample on the ESP32); |u| is clamped so the int
// cast stays in range.
MT_INLINE float wrap4(float w) {
  int q = static_cast<int>(w * 0.25f);
  if (static_cast<float>(q) * 4.f > w) --q;
  return w - static_cast<float>(q) * 4.f;
}
MT_INLINE float foldClamp(float u) { return u < -1024.f ? -1024.f : (u > 1024.f ? 1024.f : u); }
// Triangle fold: 0 at 0, 1 at 1, 0 at 2, -1 at 3 (period 4, odd).
MT_INLINE float fold(float u) { return fabsf(wrap4(foldClamp(u) - 1.f) - 2.f) - 1.f; }
// Its antiderivative: periodic (zero mean), so the wrap does not break the differences.
MT_INLINE float foldF(float u) {
  const float w = wrap4(foldClamp(u) - 1.f);
  return w <= 2.f ? w - 0.5f * w * w : 0.5f * w * w - 3.f * w + 4.f;
}

MT_INLINE float rect(float u) {
  const float a = fabsf(u);
  return a < 1.f ? a : 1.f;
}
MT_INLINE float rectF(float u) {
  const float a = fabsf(u);
  return a <= 1.f ? 0.5f * u * a : (u < 0 ? 0.5f - a : a - 0.5f);
}

// First-order ADAA over a block: x in, k f(g x) (antialiased) out.
template <class Fn, class Fa>
MT_INLINE void adaa(float* __restrict x, int n, float g, float k, float& x1, float& F1, Fn f, Fa fa) {
  float u1 = x1 * g, Fp = F1;
  const float last = x[n - 1];
  for (int i = 0; i < n; ++i) {
    const float u = x[i] * g;
    const float Fu = fa(u);
    const float d = u - u1;
    // No '/': a __divsf3 call per sample on the S3 (fastRcp, rel. error < 1e-7).
    const float ad = fabsf(d);
    const float y = ad > kAdaaEps ? (Fu - Fp) * (d < 0 ? -fastRcp(ad) : fastRcp(ad)) : f(0.5f * (u + u1));
    x[i] = y * k;
    u1 = u;
    Fp = Fu;
  }
  x1 = last;
  F1 = Fp;
}

MT_INLINE void dcBlock(float* __restrict x, int n, float& x1, float& y1) {
  float a = x1, b = y1;
  for (int i = 0; i < n; ++i) {
    const float v = x[i];
    b = v - a + kDcR * b;
    a = v;
    x[i] = b;
  }
  x1 = a;
  y1 = b;
}

}  // namespace

float ModelDrive::curve(uint8_t model, float u) {
  switch (model) {
    case kClip: return clip(u);
    case kFold: return fold(u);
    case kRect: return rect(u);
    case kAsym: return Drive::tanhLut(u + kBias) - Drive::tanhLut(kBias);
    default: return Drive::tanhLut(u);
  }
}

void ModelDrive::change(uint8_t drive, uint8_t model) {
  const bool wasOn = tanh_.on();
  tanh_.set(drive);
  drive_ = drive;
  if (!tanh_.on()) return;
  if (!wasOn) reset();
  const bool moved = model != model_;
  model_ = model;
  retune();
  // A model move hands the DC blocker over without a step: its input history becomes the new
  // curve at the last input and its output the last sample out (TANH / CLIP / FOLD leave y1_).
  if (moved && (model_ == kRect || model_ == kAsym)) {
    dx1_ = raw(x1_);
    dy1_ = y1_;
  }
}

void ModelDrive::reset() {
  x1_ = y1_ = 0;
  F1_ = model_ == kClip || model_ == kFold || model_ == kRect ? anti(model_, 0.f) : 0.f;
  dx1_ = dy1_ = 0;
}

float ModelDrive::anti(uint8_t model, float u) {
  switch (model) {
    case kClip: return clipF(u);
    case kFold: return foldF(u);
    case kRect: return rectF(u);
    default: return 0;
  }
}

MT_HOT void ModelDrive::processBlock(float* __restrict x, int n) {
  if (n <= 0 || !on()) return;
  const float g = tanh_.g();
  const float last = x[n - 1];  // every model: a later move to an ADAA model starts from here
  switch (model_) {
    case kClip: adaa(x, n, g, k_, x1_, F1_, clip, clipF); break;
    case kFold: adaa(x, n, g, k_, x1_, F1_, fold, foldF); break;
    case kRect:
      adaa(x, n, g, k_, x1_, F1_, rect, rectF);
      dcBlock(x, n, dx1_, dy1_);
      break;
    case kAsym: {
      const float b = kBias, tb = tb_, an = an_;
      const float *t, *s;
      Drive::tables(t, s);
      for (int i = 0; i < n; ++i) x[i] = (Drive::tanhLut(x[i] * g + b, t, s) - tb) * an;
      dcBlock(x, n, dx1_, dy1_);
      break;
    }
    default: tanh_.processBlock(x, n); break;
  }
  x1_ = last;
  y1_ = x[n - 1];
}

}  // namespace mt
