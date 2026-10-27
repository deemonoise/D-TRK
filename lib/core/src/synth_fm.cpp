#include "synth_fm.h"
#include <math.h>
#include "fastmath.h"
#include "hot.h"
#include "synth_drive.h"

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
MT_HOT float decayAt(float ms, float t) {
  if (ms <= 0) return 1;
  return fastExp(-6.9078f * t / (ms * kSamplesPerMs));
}

// One operator's per-sample state, in locals for the duration of a block.
// mode: 0 = silent all block (output 0, phase advanced after the loop), 1 = sounding without feedback,
// 2 = with feedback.
struct OpRun {
  uint32_t ph, inc;
  int32_t dInc;
  float lvl, dLvl, fb, dFb, last, prev, fbIn;
  int mode;
};

// mode: s.mode, from a register (s stays in memory).
MT_INLINE float opStep(OpRun& s, int mode, float mod) {
  if (mode == 0) return 0;
  float r;
  if (mode == 2) {
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
  s.ph += s.inc;
  s.inc += static_cast<uint32_t>(s.dInc);
  return y;
}

}  // namespace

MT_HOT bool fmCarrier(FmAlg a, int op) {
  switch (a) {
    case FmAlg::Stack:
    case FmAlg::TwoToOne: return op == 0;
    case FmAlg::TwoPairs: return op == 0 || op == 2;
    case FmAlg::OneToThree: return op < 3;
    default: return true;
  }
}

MT_HOT float fmMaxIndex(float hz) {
  if (hz <= 0) return 12;
  const float m = 16000.f * fastRcp(hz < 1e-30f ? 1e-30f : hz) - 1.f;  // per modulator per tick: no __divsf3
  return m < 0.2f ? 0.2f : (m > 12.f ? 12.f : m);
}

MT_HOT void FmVoice::trigger(bool keepPhase) {  // note-on: IRAM
  ampStart_ = keepPhase ? amp_ : 0;
  t_ = 0;
  choke_ = keepPhase;
  if (keepPhase) return;
  satSnap_ = true;
  for (int i = 0; i < kFmOps; ++i) ph_[i] = 0, last_[i] = fbIn_[i] = 0;
  amp_ = 0;
  svf_.reset();
}

MT_HOT void FmVoice::Decay::at(float newMs, float newOff, uint32_t t0, int newSpan, float& e0, float& e1) {
  if (newMs <= 0) {
    e0 = e1 = 1;
    return;
  }
  const bool same = newMs == ms && newOff == off;
  if (!same || t0 != t || newSpan != span) {
    const float k = -6.9078f / (newMs * kSamplesPerMs);
    if (!same || t0 != t) e = fastExp(k * (static_cast<float>(t0) - newOff));
    if (!same || newSpan != span) c = fastExp(k * newSpan);
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

MT_HOT void FmVoice::noisePair(const FmParams& p, uint32_t t0, int span, float& n0, float& n1) {
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
    auto at = [&](float t) __attribute__((always_inline)) {  // inlined: in IRAM with the caller
      if (t < bursts) {
        const float in = t - burst * static_cast<int>(t / burst);
        return fastExp(-in / (burst * 0.25f));
      }
      return p.tail * decayAt(p.noiseMs, t - bursts);
    };
    n0 = at(ta);
    n1 = at(tb);
    return;
  }
  noiseDec_.at(p.noiseMs, 0, t0, span, n0, n1);
}

MT_HOT void FmVoice::ampPair(const FmParams& p, uint32_t t0, int span, float& a0, float& a1) {
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
  auto at = [&](float t) __attribute__((always_inline)) {  // inlined: in IRAM with the caller
    if (t < kAttack) return ampStart_ + (1.f - ampStart_) * t / kAttack;
    if (t < start) return 1.f;
    return decayAt(p.ampMs, t - start);
  };
  a0 = at(ta);
  a1 = at(tb);
}

MT_HOT void FmVoice::control(const FmParams& p, int span) {
  alg_ = p.alg;
  oneShot_ = p.oneShot;
  const float inv = span == 32 ? 1.f / 32 : (span > 0 ? 1.f / span : 0);  // kControl: no __divsf3
  // Pitch sweep: the bend at t0 is the previous tick's bend at t1 (cached), one exp2f per tick.
  float bend0 = 1, bend1 = 1;
  if (p.pitchEnv != 0) {
    float d0, d1;
    pitchDec_.at(p.pitchMs, 0, t_, span, d0, d1);
    auto bend = [&](float d) __attribute__((always_inline)) {  // inlined: in IRAM with the caller
      if (p.pitchEnv == bendEnv_ && d == bendD_) return bend_;
      const float semis = p.pitchEnv * d;
      const float b = fabsf(semis) < 1e-4f ? 1.f : fastExp2(semis * (1.f / 12.f));
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
        // The limit of the highest pitch this tick reaches: a sweep starts octaves up (KICK, PERC).
        const float m = fmMaxIndex(hz * (bend0 > bend1 ? bend0 : bend1));
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
  // SAT: this tick's targets (tanh only when SAT moves), ramped from the last tick's; a fresh trigger
  // starts at them.
  const float su = p.sat <= 0 ? 0.f : (p.sat >= 1 ? 1.f : p.sat);
  float sg0 = satGT_, sn0 = satNT_, sw0 = satWT_;
  if (su != satU_) {
    satU_ = su;
    satGT_ = 1.f + 7.f * su * su;
    satNT_ = fastRcp(fastTanh(satGT_));
    satWT_ = su * 8.f < 1.f ? su * 8.f : 1.f;
  }
  if (satSnap_) sg0 = satGT_, sn0 = satNT_, sw0 = satWT_, satSnap_ = false;
  satG_ = sg0, satN_ = sn0, satW_ = sw0;
  dSatG_ = (satGT_ - sg0) * inv;
  dSatN_ = (satNT_ - sn0) * inv;
  dSatW_ = (satWT_ - sw0) * inv;
}

template <FmAlg A>
MT_INLINE void FmVoice::run(float* __restrict out, int n, float gain) {
  // State in locals: out cannot alias it, so it stays in registers across the loop.
  OpRun ops[kFmOps];
  for (int i = 0; i < kFmOps; ++i) {
    OpRun& s = ops[i];
    s.ph = ph_[i], s.inc = inc_[i], s.dInc = dInc_[i];
    s.lvl = lvl_[i], s.dLvl = dLvl_[i], s.fb = fb_[i], s.dFb = dFb_[i];
    s.last = s.prev = last_[i], s.fbIn = fbIn_[i];
    s.mode = s.lvl == 0 && s.dLvl == 0 ? 0 : (s.fb == 0 && s.dFb == 0 ? 1 : 2);
  }
  // Operators through an opaque pointer: in memory, not scalarised (that spills more, render() +4 KB).
  OpRun* o = ops;
  asm("" : "+r"(o));
  const int m0 = ops[0].mode, m1 = ops[1].mode, m2 = ops[2].mode, m3 = ops[3].mode;
  float noise = noise_, amp = amp_;
  const float dNoise = dNoise_, dAmp = dAmp_;
  uint32_t rng = rng_;
  // Noise and its filter run only while the noise is audible (or the filter takes the operators):
  // a silent noise-only filter keeps its state, which has rung out by then.
  const bool noisy = noise > 1e-5f || noise + dNoise * n > 1e-5f;
  const bool filterOn = filterOn_, filterAll = filterAll_;
  const bool noiseOn = noisy || (filterOn && filterAll);
  Svf svf = svf_;
  // SAT: off (w 0 all block) skips the shaper: bit-exact without it.
  float sg = satG_, sn = satN_, sw = satW_;
  const float dsg = dSatG_, dsn = dSatN_, dsw = dSatW_;
  const bool satOn = sw != 0 || dsw != 0;
  const float *tt = nullptr, *ts = nullptr;
  if (satOn) Drive::tables(tt, ts);
  for (int s = 0; s < n; ++s) {
    float y;
    if (A == FmAlg::Stack) {
      y = opStep(o[0], m0, opStep(o[1], m1, opStep(o[2], m2, opStep(o[3], m3, 0))));
    } else if (A == FmAlg::TwoToOne) {
      const float a = opStep(o[2], m2, opStep(o[3], m3, 0));
      y = opStep(o[0], m0, a + opStep(o[1], m1, 0));
    } else if (A == FmAlg::TwoPairs) {
      const float hi = opStep(o[2], m2, opStep(o[3], m3, 0));
      y = opStep(o[0], m0, opStep(o[1], m1, 0)) + hi;
    } else if (A == FmAlg::OneToThree) {
      const float m = opStep(o[3], m3, 0);
      y = opStep(o[0], m0, m) + opStep(o[1], m1, m) + opStep(o[2], m2, m);
    } else {
      y = opStep(o[0], m0, 0) + opStep(o[1], m1, 0) + opStep(o[2], m2, 0) + opStep(o[3], m3, 0);
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
    if (satOn) {
      y += sw * (Drive::tanhLut(sg * y, tt, ts) * sn - y);
      sg += dsg;
      sn += dsn;
      sw += dsw;
    }
    out[s] += y * amp * gain;
    noise += dNoise;
    amp += dAmp;
  }
  // Silent operators: n samples of the phase ramp at once (exact, mod 2^32).
  const uint32_t un = static_cast<uint32_t>(n), tri = un * (un - 1) / 2;
  for (int i = 0; i < kFmOps; ++i) {
    OpRun& s = o[i];
    if (s.mode == 0) {
      const uint32_t d = static_cast<uint32_t>(s.dInc);
      s.ph += un * s.inc + d * tri;
      s.inc += un * d;
    }
    ph_[i] = s.ph, inc_[i] = s.inc, lvl_[i] = s.lvl, fb_[i] = s.fb;
    if (s.mode == 2) last_[i] = s.last, fbIn_[i] = s.fbIn;
    else if (s.mode == 1 && n > 0) last_[i] = s.last, fbIn_[i] = 0.5f * (s.last + s.prev);
  }
  noise_ = noise;
  amp_ = amp;
  satG_ = sg, satN_ = sn, satW_ = sw;
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
