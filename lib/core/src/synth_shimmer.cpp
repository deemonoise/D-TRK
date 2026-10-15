#include "synth_shimmer.h"
#include <math.h>
#include <string.h>
#include "hot.h"

namespace mt {
namespace {
const float kLpCoef = 1.f - expf(-6.2831853f * 6000.f / kFxRate);  // ~6 kHz one-pole
const float kHpCoef = 1.f - expf(-6.2831853f * Shimmer::kHpHz / kFxRate);
}  // namespace

float PitchShift::ratio(uint8_t interval) {
  switch (interval) {
    case Fifth: return 1.4983071f;  // 2^(7/12)
    case TwoOct: return 4.f;
    default: return 2.f;
  }
}

void PitchShift::setBuffer(int16_t* buf, uint32_t len) {
  buf_ = buf && len >= kLen ? buf : nullptr;
  len_ = buf_ ? len : 0;
  clear();
}

void PitchShift::clear() {
  if (buf_) memset(buf_, 0, sizeof(int16_t) * len_);
  w_ = 0;
  dA_ = kLo + kW;
  dB_ = kLo + kW / 2;
  lp_ = 0;
}

void PitchShift::change(uint8_t interval) {
  interval_ = interval;
  step_ = static_cast<int32_t>((ratio(interval) - 1.f) * static_cast<float>(1 << kFrac) + 0.5f);
}

MT_HOT void PitchShift::process(const float* in, float* out, int n) {
  if (!buf_) {
    for (int i = 0; i < n; ++i) out[i] = 0;
    return;
  }
  // The ring runs over the first kLen samples (a power of two: masked, no wrap branches); the reads
  // reach back at most kWindow + 3 < kLen. Each tap's delay falls from window + 2 to 2 (the read
  // moves at `ratio`), then jumps back by the window; B runs half a window apart. Gains: triangular,
  // 0 at A's jump, 1 mid-window; B takes the rest (sum 1).
  static_assert((kLen & (kLen - 1)) == 0 && kWindow + 4.f < kLen, "shifter ring");
  static_assert(static_cast<int64_t>(kLo + kW) < (int64_t{1} << 31), "Q20 delay fits");
  constexpr uint32_t kMask = kLen - 1;
  constexpr int32_t kFMask = (1 << kFrac) - 1;
  constexpr float kQ = 1.f / static_cast<float>(1 << kFrac);
  constexpr float kG = 2.f / static_cast<float>(kW);  // (dA - kLo) -> 0 .. 2
  int16_t* const buf = buf_;
  uint32_t w = w_ & kMask;
  int32_t dA = dA_, dB = dB_;
  const int32_t step = step_;
  float lp = lp_;
  // One-pole LP on the line-unit signal, the 1 / kScale folded into its input gain.
  const float lpIn = kLpCoef * (1.f / kScale), lpKeep = 1.f - kLpCoef;
  for (int i = 0; i < n; ++i) {
    buf[w] = toLine(in[i] * kScale);
    const uint32_t a0 = (w - static_cast<uint32_t>(dA >> kFrac)) & kMask;  // newer sample of tap A
    const uint32_t b0 = (w - static_cast<uint32_t>(dB >> kFrac)) & kMask;
    const float fa = static_cast<float>(dA & kFMask) * kQ;
    const float fb = static_cast<float>(dB & kFMask) * kQ;
    const float xa = buf[a0], xb = buf[b0];
    const float a = xa + (static_cast<float>(buf[(a0 - 1u) & kMask]) - xa) * fa;
    const float b = xb + (static_cast<float>(buf[(b0 - 1u) & kMask]) - xb) * fb;
    const float t = static_cast<float>(dA - kLo) * kG - 1.f;  // 1 at A's jump .. -1 at its end
    const float ga = 1.f - fabsf(t);
    lp = lp * lpKeep + (b + (a - b) * ga) * lpIn;
    out[i] = lp;
    dA -= step;
    dB -= step;
    if (dA < kLo) dA += kW;
    if (dB < kLo) dB += kW;
    w = (w + 1) & kMask;
  }
  lp_ = fabsf(lp) < 1e-15f ? 0.f : lp;
  dA_ = dA;
  dB_ = dB;
  w_ = w;
}

// The loop gain for amount / size: once per change (flash, sqrtf).
void Shimmer::retune(uint8_t amount, uint8_t size) {
  amt_ = amount;
  size_ = size;
  const float fb = 0.70f + 0.28f * (size > 127 ? 127 : size) * (1.f / 127.f);  // as Reverb
  gT_ = (amount > 127 ? 127 : amount) * (kLoop / 127.f) * 2.f * sqrtf(1.f - fb * fb);
  if (gT_ > 0.5f) gT_ = 0.5f;
}

MT_HOT void Shimmer::process(const float* send, const float* prevWet, float* rvbIn, int n, uint8_t amount,
                             uint8_t size, bool freeze) {
  if (amount != amt_ || size != size_) retune(amount, size);
  const float gT = gT_;
  if (g_ == 0.f && gT == 0.f) {
    if (rvbIn != send) memcpy(rvbIn, send, sizeof(float) * n);
    return;
  }
  if (g_ == 0.f) {  // starting again: no stale line
    ps_.clear();
    hp_ = 0;
  }
  for (int i0 = 0; i0 < n; i0 += kMaxN) {
    const int k = n - i0 < kMaxN ? n - i0 : kMaxN;
    float sh[kMaxN];
    ps_.process(prevWet + i0, sh, k);
    const float dG = (gT - g_) * fastRcp(static_cast<float>(n - i0));
    float g = g_, hp = hp_;
    const float hpK = kHpCoef;
    if (freeze) {  // nothing added: the HP and the gain run on
      for (int i = 0; i < k; ++i) {
        g += dG;
        hp += (sh[i] - hp) * hpK;
        rvbIn[i0 + i] = send[i0 + i];
      }
    } else {
      for (int i = 0; i < k; ++i) {
        g += dG;
        hp += (sh[i] - hp) * hpK;  // HP: DC / sub would not climb, only build up in the combs
        rvbIn[i0 + i] = send[i0 + i] + softSat((sh[i] - hp) * g, 0.5f);
      }
    }
    g_ = g;
    hp_ = fabsf(hp) < 1e-15f ? 0.f : hp;
  }
  g_ = gT;
}

}  // namespace mt
