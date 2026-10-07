#include "synth_syn.h"
#include <math.h>

namespace mt {

float wtLevelPos(float hz) {
  const float x = log2f(hz * kWtHarm / 8000.f);
  return x < 0 ? 0 : (x > kWtLevels - 1 ? kWtLevels - 1 : x);
}

void WtOsc::setLevel(float hz) {
  // floor(wtLevelPos(hz)) without log2f (control rate, every SYNTH voice): halvings are exact.
  float x = hz * kWtHarm / 8000.f;
  int k = 0;
  while (k < kWtLevels - 1 && x >= 2.f) {
    x *= 0.5f;
    ++k;
  }
  off = wtLevelOff(k);
  lb = 0;
  while ((1 << lb) < wtLevelLen(k)) ++lb;
}

void SynVoice::trigger() {
  for (auto& o : bl_) o.reset();
  for (auto& o : wt_) o.reset();
  sub_.reset();
  snap_ = true;
}

void SynVoice::control(const SynParams& p, int n) {
  p_ = p;
  for (int k = 0; k < 2; ++k)
    if (p.mode[k] == static_cast<uint8_t>(SynOsc::Wt)) wt_[k].setLevel(p.hz[k]);
  if (snap_) {
    snap_ = false;
    shape_[0] = p.shape[0], shape_[1] = p.shape[1], mix_ = p.mix;
    shapeStep_[0] = shapeStep_[1] = mixStep_ = 0;
    return;
  }
  const float inv = n > 0 ? 1.f / n : 1.f;
  for (int k = 0; k < 2; ++k) shapeStep_[k] = (p.shape[k] - shape_[k]) * inv;
  mixStep_ = (p.mix - mix_) * inv;
}

constexpr float kMaxDt = 0.4999f;  // phase step per sample, below Nyquist

MT_HOT void SynVoice::oscBlock(int k, float* dst, const float* sync, float* wrap, int n) {
  // Below Nyquist: the BL oscillators wrap their phase once per sample (WtOsc::step clamps alike).
  const float dt = fminf(p_.hz[k] * (1.f / kSynthRate), kMaxDt);
  float s = shape_[k];
  const float ds = shapeStep_[k];
  switch (static_cast<SynOsc>(p_.mode[k])) {
    case SynOsc::Square: {
      BlOsc& o = bl_[k];
      for (int i = 0; i < n; ++i, s += ds) {
        const BlOut r = o.square(dt, 0.5f + 0.45f * s, sync ? sync[i] : -1.f);
        dst[i] = r.v;
        if (wrap) wrap[i] = r.wrap;
      }
      break;
    }
    case SynOsc::Tri: {
      BlOsc& o = bl_[k];
      for (int i = 0; i < n; ++i) {
        const BlOut r = o.tri(dt);
        dst[i] = r.v;
        if (wrap) wrap[i] = r.wrap;
      }
      break;
    }
    case SynOsc::Wt: {
      WtOsc& o = wt_[k];
      const int16_t* tab = p_.wt[k];
      const float fs = kWtFrames - 1;
      if (sync) {  // slave: no wraps of its own needed
        for (int i = 0; i < n; ++i, s += ds) dst[i] = o.next(tab, dt, s * fs, sync[i]);
        break;
      }
      const uint32_t dph = WtOsc::step(dt);
      if (!wrap) {
        for (int i = 0; i < n; ++i, s += ds) dst[i] = o.next(tab, dph, s * fs);
        break;
      }
      // Master of a sync: where the phase wrapped in this step.
      const float invDt = dt > 0 ? 1.f / dt : 0.f;
      for (int i = 0; i < n; ++i, s += ds) {
        const uint32_t before = o.ph;
        dst[i] = o.next(tab, dph, s * fs);
        wrap[i] = o.ph < before ? o.phase() * invDt : -1.f;
      }
      break;
    }
    default: {
      BlOsc& o = bl_[k];
      for (int i = 0; i < n; ++i) {
        const BlOut r = o.saw(dt, sync ? sync[i] : -1.f);
        dst[i] = r.v;
        if (wrap) wrap[i] = r.wrap;
      }
      break;
    }
  }
}

MT_HOT void SynVoice::render(float* out, int n, float amp) {
  constexpr int kChunk = 32;
  const float g = amp * kSynGain;
  const float subDt = fminf(p_.hz[0] * (1.f / kSynthRate) / (p_.subOct >= 2 ? 4.f : 2.f), kMaxDt);
  // An oscillator out of the mix for the whole block is skipped (osc 1 still runs as a sync master).
  const float mixEnd = mix_ + mixStep_ * n;
  const bool need1 = mix_ < 1.f || mixEnd < 1.f || p_.sync;
  const bool need2 = mix_ > 0.f || mixEnd > 0.f;
  float a[kChunk], b[kChunk], w[kChunk];
  for (int pos = 0; pos < n; pos += kChunk) {
    const int m = n - pos < kChunk ? n - pos : kChunk;
    if (need1) oscBlock(0, a, nullptr, p_.sync ? w : nullptr, m);
    else for (int i = 0; i < m; ++i) a[i] = 0;
    if (need2) oscBlock(1, b, p_.sync ? w : nullptr, nullptr, m);
    else for (int i = 0; i < m; ++i) b[i] = 0;
    float mx = mix_;
    const float dm = mixStep_;
    for (int i = 0; i < m; ++i, mx += dm) a[i] += (b[i] - a[i]) * mx;
    if (p_.sub > 0) {
      const float sg = p_.sub;
      for (int i = 0; i < m; ++i) a[i] += sub_.square(subDt, 0.5f, -1).v * sg;
    }
    if (p_.noise > 0) {
      const float ng = p_.noise * (1.f / 2147483648.f);
      uint32_t x = noise_;
      for (int i = 0; i < m; ++i) {
        x ^= x << 13, x ^= x >> 17, x ^= x << 5;
        a[i] += static_cast<int32_t>(x) * ng;
      }
      noise_ = x;
    }
    float* o = out + pos;
    for (int i = 0; i < m; ++i) o[i] += a[i] * g;
    shape_[0] += shapeStep_[0] * m, shape_[1] += shapeStep_[1] * m, mix_ = mx;
  }
}

}  // namespace mt
