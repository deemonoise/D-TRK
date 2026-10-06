#include "synth_fm.h"
#include <math.h>
#include "hot.h"

namespace mt {
namespace {

constexpr float kInvTwoPi = 1.f / 6.2831853f;
constexpr float kIncPerHz = 4294967296.f / kSynthRate;
constexpr float kMaxInc = 0.45f * 4294967296.f;  // 0.45 x sample rate, < 2^31
constexpr float kSamplesPerMs = kSynthRate / 1000.f;

// Phase offset in cycles -> phase units, |c| < 2048.
MT_INLINE uint32_t cycles(float c) {
  return static_cast<uint32_t>(static_cast<int32_t>(c * 1048576.f)) << 12;
}

// Exponential decay to -60 dB in ms; t in samples.
float decayAt(float ms, float t) {
  if (ms <= 0) return 1;
  return expf(-6.9078f * t / (ms * kSamplesPerMs));
}

// One operator's per-sample state, in locals for the duration of a block.
// mode: 0 = silent all block (output 0, phase only), 1 = sounding without feedback, 2 = with feedback.
struct OpRun {
  uint32_t ph, inc;
  int32_t dInc;
  float lvl, dLvl, fb, dFb, last, prev, fbIn;
  int mode;
};

MT_INLINE float opStep(OpRun& s, float mod) {
  if (s.mode == 0) return 0;
  float r;
  if (s.mode == 2) {
    r = tableSine(s.ph + cycles(mod + s.fb * s.fbIn));
    s.fbIn = 0.5f * (r + s.last);
    s.last = r;
    s.fb += s.dFb;
  } else {
    r = tableSine(s.ph + cycles(mod));
    s.prev = s.last;
    s.last = r;
  }
  const float y = r * s.lvl;
  s.lvl += s.dLvl;
  return y;
}

}  // namespace

bool fmCarrier(FmAlg a, int op) {
  switch (a) {
    case FmAlg::Stack:
    case FmAlg::TwoToOne: return op == 0;
    case FmAlg::TwoPairs: return op == 0 || op == 2;
    case FmAlg::OneToThree: return op < 3;
    default: return true;
  }
}

float fmMaxIndex(float hz) {
  if (hz <= 0) return 8;
  const float m = 12000.f / hz - 1.f;
  return m < 0.2f ? 0.2f : (m > 8.f ? 8.f : m);
}

void FmVoice::trigger(bool keepPhase) {
  ampStart_ = keepPhase ? amp_ : 0;
  t_ = 0;
  choke_ = keepPhase;
  if (keepPhase) return;
  for (int i = 0; i < kFmOps; ++i) ph_[i] = 0, last_[i] = fbIn_[i] = 0;
  amp_ = 0;
  svf_.reset();
}

void FmVoice::Decay::at(float newMs, float newOff, uint32_t t0, int newSpan, float& e0, float& e1) {
  if (newMs <= 0) {
    e0 = e1 = 1;
    return;
  }
  const bool same = newMs == ms && newOff == off;
  if (!same || t0 != t || newSpan != span) {
    const float k = -6.9078f / (newMs * kSamplesPerMs);
    if (!same || t0 != t) e = expf(k * (static_cast<float>(t0) - newOff));
    if (!same || newSpan != span) c = expf(k * newSpan);
  }
  ms = newMs;
  off = newOff;
  span = newSpan;
  e0 = e;
  e = e * c;
  if (e < 1e-30f) e = 0;
  e1 = e;
  t = t0 + static_cast<uint32_t>(newSpan);
}

void FmVoice::noisePair(const FmParams& p, uint32_t t0, int span, float& n0, float& n1) {
  const float burst = p.burstMs * kSamplesPerMs;
  if (p.bursts && burst > 0) {
    const float bursts = p.bursts * burst;
    const float ta = static_cast<float>(t0), tb = ta + span;
    if (ta >= bursts) {
      noiseDec_.at(p.noiseMs, bursts, t0, span, n0, n1);
      n0 *= p.tail;
      n1 *= p.tail;
      return;
    }
    // During the bursts (the clap's first few tens of ms): direct.
    auto at = [&](float t) {
      if (t < bursts) {
        const float in = t - burst * static_cast<int>(t / burst);
        return expf(-in / (burst * 0.25f));
      }
      return p.tail * decayAt(p.noiseMs, t - bursts);
    };
    n0 = at(ta);
    n1 = at(tb);
    return;
  }
  noiseDec_.at(p.noiseMs, 0, t0, span, n0, n1);
}

void FmVoice::ampPair(const FmParams& p, uint32_t t0, int span, float& a0, float& a1) {
  if (!p.oneShot) {
    a0 = a1 = 1;
    return;
  }
  // Bursts (clap) play at full level: the decay starts after them, with the noise tail.
  const float burst = p.burstMs * kSamplesPerMs;
  const float bursts = p.bursts && burst > 0 ? p.bursts * burst : 0;
  const float start = bursts > kAttack ? bursts : static_cast<float>(kAttack);
  const float ta = static_cast<float>(t0), tb = ta + span;
  if (ta >= start) {
    ampDec_.at(p.ampMs, start, t0, span, a0, a1);
    return;
  }
  auto at = [&](float t) {
    if (t < kAttack) return ampStart_ + (1.f - ampStart_) * t / kAttack;
    if (t < start) return 1.f;
    return decayAt(p.ampMs, t - start);
  };
  a0 = at(ta);
  a1 = at(tb);
}

void FmVoice::control(const FmParams& p, int span) {
  alg_ = p.alg;
  oneShot_ = p.oneShot;
  const float inv = span > 0 ? 1.f / span : 0;
  // Pitch sweep: the bend at t0 is the previous tick's bend at t1 (cached), one exp2f per tick.
  float bend0 = 1, bend1 = 1;
  if (p.pitchEnv != 0) {
    float d0, d1;
    pitchDec_.at(p.pitchMs, 0, t_, span, d0, d1);
    auto bend = [&](float d) {
      if (p.pitchEnv == bendEnv_ && d == bendD_) return bend_;
      const float semis = p.pitchEnv * d;
      const float b = fabsf(semis) < 1e-4f ? 1.f : exp2f(semis * (1.f / 12.f));
      bendEnv_ = p.pitchEnv, bendD_ = d, bend_ = b;
      return b;
    };
    bend0 = bend(d0);
    bend1 = bend(d1);
  }
  float fbScale0 = 1, fbScale1 = 1;
  if (p.fbEnv != 0) {
    float d0, d1;
    fbDec_.at(p.fbMs, 0, t_, span, d0, d1);
    fbScale0 = 1.f + p.fbEnv * d0;
    fbScale1 = 1.f + p.fbEnv * d1;
  }
  for (int i = 0; i < kFmOps; ++i) {
    const FmOp& o = p.op[i];
    const float hz = p.hz * o.ratio;
    const float i0 = hz * bend0 * kIncPerHz, i1 = hz * bend1 * kIncPerHz;
    float l0 = 0, l1 = 0;
    if (o.level != 0) {
      float d0, d1;
      opDec_[i].at(o.decayMs, 0, t_, span, d0, d1);
      const float s = o.sustain < 0 ? 0 : (o.sustain > 1 ? 1 : o.sustain);
      l0 = o.level * (s + (1.f - s) * d0);
      l1 = o.level * (s + (1.f - s) * d1);
      if (!fmCarrier(p.alg, i)) {
        const float m = fmMaxIndex(hz);
        l0 = (l0 < m ? l0 : m) * kInvTwoPi;
        l1 = (l1 < m ? l1 : m) * kInvTwoPi;
      }
    }
    if (hz <= 0 || i0 >= kMaxInc || i1 >= kMaxInc) l0 = l1 = 0;
    else if (choke_) l0 = lvl_[i];  // ramp from the sounding level, no snap
    const float inc0 = i0 < kMaxInc ? i0 : 0;
    inc_[i] = static_cast<uint32_t>(static_cast<int32_t>(inc0));
    const float di = (i1 - inc0) * inv;
    dInc_[i] = i1 < kMaxInc ? static_cast<int32_t>(di < 0 ? di - 0.5f : di + 0.5f) : 0;
    lvl_[i] = l0;
    dLvl_[i] = (l1 - l0) * inv;
    const float f0 = choke_ ? fb_[i] : o.fb * fbScale0 * kInvTwoPi;
    fb_[i] = f0;
    dFb_[i] = (o.fb * fbScale1 * kInvTwoPi - f0) * inv;
  }
  float n0 = 0, n1 = 0;
  if (p.noise != 0) {
    noisePair(p, t_, span, n0, n1);
    n0 *= p.noise;
    n1 *= p.noise;
  }
  if (choke_) n0 = noise_;
  choke_ = false;
  noise_ = n0;
  dNoise_ = (n1 - n0) * inv;
  float a0, a1;
  ampPair(p, t_, span, a0, a1);
  amp_ = a0;
  dAmp_ = (a1 - a0) * inv;
  filterOn_ = p.filterHz > 0;
  filterAll_ = p.filterAll;
  if (filterOn_ && (p.filterHz != filterHz_ || p.filterQ != filterQ_ || p.filterMode != filterMode_)) {
    svf_.set(p.filterMode, p.filterHz, p.filterQ);
    filterHz_ = p.filterHz, filterQ_ = p.filterQ, filterMode_ = p.filterMode;
  }
}

template <FmAlg A>
MT_INLINE void FmVoice::run(float* __restrict out, int n, float gain) {
  // State in locals: out cannot alias it, so it stays in registers across the loop.
  OpRun o[kFmOps];
  for (int i = 0; i < kFmOps; ++i) {
    OpRun& s = o[i];
    s.ph = ph_[i], s.inc = inc_[i], s.dInc = dInc_[i];
    s.lvl = lvl_[i], s.dLvl = dLvl_[i], s.fb = fb_[i], s.dFb = dFb_[i];
    s.last = s.prev = last_[i], s.fbIn = fbIn_[i];
    s.mode = s.lvl == 0 && s.dLvl == 0 ? 0 : (s.fb == 0 && s.dFb == 0 ? 1 : 2);
  }
  float noise = noise_, amp = amp_;
  const float dNoise = dNoise_, dAmp = dAmp_;
  uint32_t rng = rng_;
  // Noise and its filter run only while the noise is audible (or the filter takes the operators):
  // a silent noise-only filter keeps its state, which has rung out by then.
  const bool noisy = noise > 1e-5f || noise + dNoise * n > 1e-5f;
  const bool filterOn = filterOn_, filterAll = filterAll_;
  const bool noiseOn = noisy || (filterOn && filterAll);
  Svf svf = svf_;
  for (int s = 0; s < n; ++s) {
    float y;
    if (A == FmAlg::Stack) {
      y = opStep(o[0], opStep(o[1], opStep(o[2], opStep(o[3], 0))));
    } else if (A == FmAlg::TwoToOne) {
      const float a = opStep(o[2], opStep(o[3], 0));
      y = opStep(o[0], a + opStep(o[1], 0));
    } else if (A == FmAlg::TwoPairs) {
      const float hi = opStep(o[2], opStep(o[3], 0));
      y = opStep(o[0], opStep(o[1], 0)) + hi;
    } else if (A == FmAlg::OneToThree) {
      const float m = opStep(o[3], 0);
      y = opStep(o[0], m) + opStep(o[1], m) + opStep(o[2], m);
    } else {
      y = opStep(o[0], 0) + opStep(o[1], 0) + opStep(o[2], 0) + opStep(o[3], 0);
    }
    if (noiseOn) {
      rng ^= rng << 13;
      rng ^= rng >> 17;
      rng ^= rng << 5;
      const float nz = static_cast<int32_t>(rng) * (1.f / 2147483648.f) * noise;
      if (!filterOn) y += nz;
      else if (filterAll) y = svf.process(y + nz);
      else y += svf.process(nz);
    }
    out[s] += y * amp * gain;
    for (int i = 0; i < kFmOps; ++i) {
      o[i].ph += o[i].inc;
      o[i].inc += static_cast<uint32_t>(o[i].dInc);
    }
    noise += dNoise;
    amp += dAmp;
  }
  for (int i = 0; i < kFmOps; ++i) {
    const OpRun& s = o[i];
    ph_[i] = s.ph, inc_[i] = s.inc, lvl_[i] = s.lvl, fb_[i] = s.fb;
    if (s.mode == 2) last_[i] = s.last, fbIn_[i] = s.fbIn;
    else if (s.mode == 1 && n > 0) last_[i] = s.last, fbIn_[i] = 0.5f * (s.last + s.prev);
  }
  noise_ = noise;
  amp_ = amp;
  rng_ = rng;
  svf_ = svf;
  t_ += static_cast<uint32_t>(n);
}

// The algorithms inline here: one IRAM function (template instances in IRAM sections break the
// Xtensa literal placement).
MT_HOT void FmVoice::render(float* out, int n, float gain) {
  switch (alg_) {
    case FmAlg::Stack: run<FmAlg::Stack>(out, n, gain); break;
    case FmAlg::TwoToOne: run<FmAlg::TwoToOne>(out, n, gain); break;
    case FmAlg::TwoPairs: run<FmAlg::TwoPairs>(out, n, gain); break;
    case FmAlg::OneToThree: run<FmAlg::OneToThree>(out, n, gain); break;
    default: run<FmAlg::Additive>(out, n, gain); break;
  }
}

}  // namespace mt
