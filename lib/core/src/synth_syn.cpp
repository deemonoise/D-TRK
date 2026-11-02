#include "synth_syn.h"
#include "fastmath.h"
#include "hot.h"
#include "synth_shape.h"
#include <math.h>

namespace mt {

MT_HOT __attribute__((noinline)) float WtOsc::syncRead(const int16_t* table, float pos, uint32_t phase) const {
  return read(table, pos, phase);
}

float wtLevelPos(float hz) {
  const float x = log2f(hz * kWtHarm / 8000.f);
  return x < 0 ? 0 : (x > kWtLevels - 1 ? kWtLevels - 1 : x);
}

MT_HOT void WtOsc::setLevel(float hz) {
  // Finish a running 32-sample fade before accepting another level. A mid-block bend may
  // reverse direction: retaining both endpoints avoids a jump or a third table read.
  if (fade) return;
  // floor(wtLevelPos(hz)) without log2f (control rate, every SYNTH voice): halvings are exact.
  float x = hz * kWtHarm * (1.f / 8000.f);  // no __divsf3
  int k = 0;
  while (k < kWtLevels - 1 && x >= 2.f) {
    x *= 0.5f;
    ++k;
  }
  if (level != 0xff) {
    // 2% hysteresis, well inside the original Nyquist margin, prevents vibrato from
    // repeatedly starting transitions around an octave boundary.
    if (k == level + 1 && hz < 125.f * (1u << level) * 1.02f) k = level;
    else if (k + 1 == level && hz > 62.5f * (1u << level) * 0.98f) k = level;
    if (k == level) return;
    oldOff = off;
    oldLb = lb;
    fade = 32;
  }
  level = static_cast<uint8_t>(k);
  off = wtLevelOff(k);
  lb = 0;
  while ((1 << lb) < wtLevelLen(k)) ++lb;
}

void SynVoice::trigger(uint8_t phaseMode, uint8_t phase1, uint8_t phase2, uint32_t seed, bool keep) {
  snap_ = true;
  if (phaseMode == static_cast<uint8_t>(SynPhaseMode::Free) && keep) return;
  for (auto& o : bl_) o.reset();
  for (auto& o : wt_) o.reset();
  sub_.reset();
  const bool random = phaseMode == static_cast<uint8_t>(SynPhaseMode::Random);
  uint32_t rng = seed ? seed : 0x9e3779b9u;
  auto nextPhase = [&rng]() {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
  };
  for (int k = 0; k < 2; ++k) {
    uint32_t ph = static_cast<uint32_t>((k ? phase2 : phase1) & 127u) << 25;
    if (random) ph += nextPhase();
    bl_[k].t = (ph >> 8) * (1.f / 16777216.f);
    wt_[k].ph = ph;
  }
  if (random) sub_.t = (nextPhase() >> 8) * (1.f / 16777216.f);
}

MT_CONTROL void SynVoice::control(const SynParams& p, int n) {
  p_ = p;
  for (int k = 0; k < 2; ++k)
    if (p.mode[k] == static_cast<uint8_t>(SynOsc::Wt)) wt_[k].setLevel(p.hz[k]);
  // Smooth the effective folder amount, including its pitch protection. Changing MIX or
  // enabling even a tiny RING introduces another source; applying its limit in render()
  // would step the existing folder before that source has faded in.
  float foldTarget = 0.f;
  if (p.fold > 0.f) {
    const float mixStart = snap_ ? p.mix : mix_;
    const bool ring = p.ring > 0.f || (!snap_ && ring_ > 0.f);
    const bool source2 = mixStart > 0.f || p.mix > 0.f || ring;
    const bool source1 = mixStart < 1.f || p.mix < 1.f || ring || (p.sync && source2);
    float foldHz = source1 ? p.hz[0] : 0.f;
    if (source2 && p.hz[1] > foldHz) foldHz = p.hz[1];
    if (ring) foldHz = p.hz[0] + p.hz[1];  // the product creates sum frequencies
    const float lh = (5000.f - foldHz) * (1.f / 4000.f);
    foldTarget = p.fold * (lh < 0.f ? 0.f : (lh > 1.f ? 1.f : lh));
  }
  if (snap_) {
    snap_ = false;
    shape_[0] = p.shape[0], shape_[1] = p.shape[1], mix_ = p.mix, fold_ = foldTarget, ring_ = p.ring;
    subGain_ = p.sub; noiseGain_ = p.noise;
    shapeStep_[0] = shapeStep_[1] = mixStep_ = foldStep_ = ringStep_ = subStep_ = noiseStep_ = 0;
    return;
  }
  const float inv = n == 32 ? 1.f / 32 : (n > 0 ? 1.f / n : 1.f);  // kControl: no __divsf3
  for (int k = 0; k < 2; ++k) shapeStep_[k] = (p.shape[k] - shape_[k]) * inv;
  mixStep_ = (p.mix - mix_) * inv;
  foldStep_ = (foldTarget - fold_) * inv;
  ringStep_ = (p.ring - ring_) * inv;
  subStep_ = (p.sub - subGain_) * inv;
  noiseStep_ = (p.noise - noiseGain_) * inv;
}

constexpr float kMaxDt = 0.4999f;  // phase step per sample, below Nyquist

MT_HOT void SynVoice::oscBlock(int k, float* dst, const float* sync, float* wrap, int n) {
  // Below Nyquist: the BL oscillators wrap their phase once per sample (WtOsc::step clamps alike).
  const float hzDt = p_.hz[k] * (1.f / kSynthRate);
  const float dt = hzDt < kMaxDt ? hzDt : kMaxDt;  // no fminf: a flash call
  const float inv = dt > 1e-30f ? fastRcp(dt) : 0.f;  // the edge distances: no '/' per wrap
  float s = shape_[k];
  const float ds = shapeStep_[k];
  switch (static_cast<SynOsc>(p_.mode[k])) {
    case SynOsc::Sine: {
      BlOsc o = bl_[k];
      for (int i = 0; i < n; ++i) {
        const BlOut r = o.sine(dt, inv, sync ? sync[i] : -1.f);
        dst[i] = r.v;
        if (wrap) wrap[i] = r.wrap;
      }
      bl_[k] = o;
      break;
    }
    case SynOsc::Square: {
      // Local state cannot alias dst: keep oscillator registers across sample stores.
      BlOsc o = bl_[k];
      const float dpw = 0.45f * ds;
      const float relative = dt - dpw;
      const float edgeInv = relative > 1e-30f ? fastRcp(relative)
                            : (relative < -1e-30f ? -fastRcp(-relative) : 0.f);
      if (sync) {
        for (int i = 0; i < n; ++i, s += ds)
          dst[i] = o.squarePwm(dt, inv, 0.5f + 0.45f * s, dpw, edgeInv, sync[i]).v;
      } else {
        // The first interval may have the preceding control block's width slope.
        const BlOut first = o.squarePwm(dt, inv, 0.5f + 0.45f * s, dpw, edgeInv, -1.f);
        dst[0] = first.v;
        if (wrap) wrap[0] = first.wrap;
        s += ds;
        if (ds == 0.f) {
          const float pw = 0.5f + 0.45f * s, dc = 2.f * pw - 1.f;
          for (int i = 1; i < n; ++i) {
            const BlOut r = o.square(dt, inv, pw, -1.f);
            dst[i] = r.v - dc;
            if (wrap) wrap[i] = r.wrap;
          }
        } else if (relative >= 0.f) {
          for (int i = 1; i < n; ++i, s += ds) {
            const BlOut r = o.squarePwmRun<false>(dt, inv, 0.5f + 0.45f * s, edgeInv);
            dst[i] = r.v;
            if (wrap) wrap[i] = r.wrap;
          }
        } else {
          for (int i = 1; i < n; ++i, s += ds) {
            const BlOut r = o.squarePwmRun<true>(dt, inv, 0.5f + 0.45f * s, edgeInv);
            dst[i] = r.v;
            if (wrap) wrap[i] = r.wrap;
          }
        }
      }
      bl_[k] = o;
      break;
    }
    case SynOsc::Tri: {
      BlOsc o = bl_[k];
      for (int i = 0; i < n; ++i) {
        const BlOut r = o.tri(dt, inv, sync ? sync[i] : -1.f);
        dst[i] = r.v;
        if (wrap) wrap[i] = r.wrap;
      }
      bl_[k] = o;
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
      for (int i = 0; i < n; ++i, s += ds) {
        const uint32_t before = o.ph;
        dst[i] = o.next(tab, dph, s * fs);
        wrap[i] = o.ph < before ? o.phase() * inv : -1.f;
      }
      break;
    }
    default: {
      BlOsc& o = bl_[k];
      for (int i = 0; i < n; ++i) {
        const BlOut r = o.saw(dt, inv, sync ? sync[i] : -1.f);
        dst[i] = r.v;
        if (wrap) wrap[i] = r.wrap;
      }
      break;
    }
  }
}

// Fold: gain 1..8 (up to ~4 folds) with a make-up to the plain mix's level at full fold. Ring:
// osc 1 x osc 2 scaled to the plain mix's level. Both: a 220 Hz saw + square or tri within +-3 dB RMS.
constexpr float kFoldGain = 1.0f, kRingGain = 1.3f;
constexpr float kFoldFade = 16.f;  // fully wet from fold ~8/127

MT_HOT void SynVoice::foldBlock(float* a, int m) {
  // Naive (no ADAA: its slope quotient cost ~1 %/voice on the Teensy); the pitch limit bounds the
  // aliasing. No branch, no division per sample.
  float fd = fold_;
  const float df = foldStep_;
  // Two samples per pass: the M7 issues in order, two independent chains fill each other's stalls.
#pragma GCC unroll 2
  for (int i = 0; i < m; ++i, fd += df) {
    const float e = fd;
    const float x = a[i];
    float y = foldTri(x * (1.f + 7.f * e));
    if (kFoldGain != 1.f) y *= 1.f + (kFoldGain - 1.f) * e;
    // Dry / wet fade-in: a mix above 1 (ring, BLEP overshoot) would otherwise fold all at once as
    // the fold leaves 0 (an LFO crossing it, the pitch limit). w = 0 is the skip path.
    const float w = fclamp(e * kFoldFade, 0.f, 1.f);  // e >= 0; no compare + VMRS
    a[i] = x + (y - x) * w;
  }
}

MT_HOT void SynVoice::render(float* out, int n, float amp) {
  constexpr int kChunk = 32;
  const float g = amp * kSynGain;
  const float subHz = p_.hz[0] * (1.f / kSynthRate) * (p_.subOct >= 2 ? 0.25f : 0.5f);  // exact
  const float subDt = subHz < kMaxDt ? subHz : kMaxDt;  // no fminf: a flash call
  const float subInv = subDt > 1e-30f ? fastRcp(subDt) : 0.f;
  // An oscillator out of the mix for the whole block is skipped (osc 1 still runs as a sync master).
  const float mixEnd = mix_ + mixStep_ * n;
  const bool ring = ring_ > 0.f || ring_ + ringStep_ * n > 0.f;  // needs both oscillators
  const bool need1 = mix_ < 1.f || mixEnd < 1.f || p_.sync || ring;
  const bool need2 = mix_ > 0.f || mixEnd > 0.f || ring;
  const bool fold = fold_ > 0.f || fold_ + foldStep_ * n > 0.f;
  float a[kChunk], b[kChunk], w[kChunk];
  for (int pos = 0; pos < n; pos += kChunk) {
    const int m = n - pos < kChunk ? n - pos : kChunk;
    if (need1) oscBlock(0, a, nullptr, p_.sync ? w : nullptr, m);
    else for (int i = 0; i < m; ++i) a[i] = 0;
    if (need2) oscBlock(1, b, p_.sync ? w : nullptr, nullptr, m);
    else for (int i = 0; i < m; ++i) b[i] = 0;
    float mx = mix_;
    const float dm = mixStep_;
    if (ring) {
      float rg = ring_;
      const float dr = ringStep_;
      for (int i = 0; i < m; ++i, mx += dm, rg += dr) {
        const float mi = a[i] + (b[i] - a[i]) * mx;
        a[i] = mi + (a[i] * b[i] * kRingGain - mi) * rg;
      }
      ring_ = rg;
    } else {
      for (int i = 0; i < m; ++i, mx += dm) a[i] += (b[i] - a[i]) * mx;
      ring_ += ringStep_ * m;
    }
    if (fold) foldBlock(a, m);
    fold_ += foldStep_ * m;
    if (subGain_ > 0.f || subGain_ + subStep_ * m > 0.f) {
      float sg = subGain_;
      const float ds = subStep_;
      BlOsc sub = sub_;
      switch (static_cast<SynSubWave>(p_.subWave)) {
        case SynSubWave::Sine:
          for (int i = 0; i < m; ++i, sg += ds) a[i] += sub.sine(subDt, subInv, -1.f).v * sg;
          break;
        case SynSubWave::Tri:
          for (int i = 0; i < m; ++i, sg += ds) a[i] += sub.tri(subDt, subInv, -1.f).v * sg;
          break;
        default:
          for (int i = 0; i < m; ++i, sg += ds) a[i] += sub.square(subDt, subInv, 0.5f, -1.f).v * sg;
          break;
      }
      sub_ = sub;
    }
    if (noiseGain_ > 0.f || noiseGain_ + noiseStep_ * m > 0.f) {
      float ng = noiseGain_ * (1.f / 2147483648.f);
      const float dn = noiseStep_ * (1.f / 2147483648.f);
      uint32_t x = noise_;
      for (int i = 0; i < m; ++i, ng += dn) {
        x ^= x << 13, x ^= x >> 17, x ^= x << 5;
        a[i] += static_cast<int32_t>(x) * ng;
      }
      noise_ = x;
    }
    subGain_ += subStep_ * m;
    noiseGain_ += noiseStep_ * m;
    float* o = out + pos;
    for (int i = 0; i < m; ++i) o[i] += a[i] * g;
    shape_[0] += shapeStep_[0] * m, shape_[1] += shapeStep_[1] * m, mix_ = mx;
  }
}

}  // namespace mt
