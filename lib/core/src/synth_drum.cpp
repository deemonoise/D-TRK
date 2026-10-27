#include "synth_drum.h"
#include <math.h>
#include "fastmath.h"
#include "hot.h"
#include "synth_osc.h"

namespace mt {
namespace {

constexpr float kSamplesPerMs = kSynthRate / 1000.f;
constexpr float kIncPerHz = 4294967296.f / kSynthRate;
constexpr float kMaxHz = kSynthRate * 0.45f;
constexpr float kOff = 1e-4f;   // -80 dB: done
constexpr float kZero = 1e-6f;  // envelopes below snap to 0 (no denormals, silent parts skipped)
// Band-passed noise rms as a fraction of the unfiltered noise's: peaks stay below 1.
constexpr float kBpNoise = 0.4f;
constexpr float kSlowMax = 8;  // a slowing noise decay (noiseAccMs < 0) ends at 1 / 8 of its start rate

// Per-sample multiplier reaching -60 dB in ms; 0 ms = no decay.
MT_HOT float decayK(float ms) { return ms > 0 ? fastExp(-6.9077553f * fastRcp(ms * kSamplesPerMs)) : 1.f; }
const float kChokeK = decayK(2);  // choke tail: 2 ms to -60 dB

uint32_t gSeed = 0;  // noise seeds of successive triggers (a race only repeats a seed)

// Soft clip: slope 1 at 0, flat at +-1 from |x| = 1.5. No division: the ESP32 does float division
// in software (__divsf3), far too slow per sample.
MT_INLINE float softSat(float x) {
  float u = x * (2.f / 3.f);
  u = u > 1.f ? 1.f : (u < -1.f ? -1.f : u);
  return u * (1.5f - 0.5f * u * u);
}

MT_INLINE float snap(float e) { return e < kZero && e > -kZero ? 0 : e; }

// Whole samples of a time in ms.
inline uint32_t samples(float ms) { return ms > 0 ? static_cast<uint32_t>(ms * kSamplesPerMs) : 0; }

}  // namespace

void drumResetNoise() { gSeed = 0; }

MT_HOT void DrumVoice::trigger(bool keepTail) {  // note-on: IRAM
  tailV_ = keepTail ? last_ : 0;
  for (int k = 0; k < kDrumTones; ++k) {
    tonePh_[k] = 0;
    toneDE_[k] = 1;
  }
  toneE_ = fastE_ = pitchE_ = sweepAE_ = sweepBE_ = clickE_ = metalE_ = accE_ = metal2E_ = noiseE_ = 1;
  impulse_ = true;
  burstsLeft_ = 0;
  age_ = 0;
  fresh_ = true;
  // Voices triggered together play different noise.
  gSeed += 0x9E3779B9u;
  rng_ ^= gSeed;
  if (!rng_) rng_ = 0x9E3779B9u;
  bp1_.reset();
  bp2_.reset();
  hp_.reset();
  if (tailFlt_) {  // back to the hits' q (set() needn't follow a trigger with the same params)
    tailFlt_ = false;
    noiseFilter();
  }
  nf_.reset();
  noiseHpZ_ = 0;
}

MT_HOT void DrumVoice::noiseFilter() {
  noiseGain_ = 1;
  if (!noiseFlt_ || noiseLvl_ <= 0) return;
  const float q = tailFlt_ && tailQ_ > 0 ? tailQ_ : noiseQ_;
  nf_.set(noiseMode_, noiseHz_, q);
  if (noiseMode_ == Svf::Mode::Bp) {
    // Svf band-pass: peak q, bandwidth hz / q, so white noise keeps pi hz q / rate of its power.
    const float hz = noiseHz_ < 20.f ? 20.f : (noiseHz_ > kMaxHz ? kMaxHz : noiseHz_);
    const float qq = q < 0.5f ? 0.5f : q;
    noiseGain_ = kBpNoise * fastPow(kSynthRate * fastRcp(3.14159265f * hz * qq), 0.5f);
  }
}

// Control rate, only on a drumMachine() cache miss (new machine/pitch/macros or a stolen voice). IRAM: a
// hit may steal several voices in one block, and from flash set() costs ~140 us each.
MT_HOT void DrumVoice::set(const DrumParams& p) {
  // Tones.
  for (int k = 0; k < kDrumTones; ++k) {
    const float hz = p.toneHz[k];
    toneHz_[k] = hz > 0 ? (hz < kMaxHz ? hz : kMaxHz) : 0;
    toneLvl_[k] = hz > 0 ? p.toneLvl[k] : 0;
    toneDKSet_[k] = decayK(p.toneDecMs[k]);
  }
  tone2_ = toneLvl_[2] != 0;
  toneK_ = decayK(p.toneMs);
  fastK_ = decayK(p.fastMs);
  slowLvl_ = p.slowLvl;
  fastLvl_ = p.fastLvl;
  holdN_ = samples(p.holdMs);
  fastHoldN_ = samples(p.fastHoldMs);
  delay_ = samples(p.toneDelayMs);
  h3_ = p.h3;
  h5_ = p.h5;
  pitchEnv_ = p.pitchEnv;
  pitchK_ = decayK(p.pitchMs);
  pitchKL_ = fastLog2(pitchK_);
  sweepA_ = p.sweepA;
  sweepB_ = p.sweepB;
  sweepAKL_ = fastLog2(decayK(p.sweepAMs));
  sweepBKL_ = fastLog2(decayK(p.sweepBMs));

  click_ = p.click;
  clickK_ = decayK(p.clickMs);
  clickStep_ = p.clickStep;
  stepN_ = samples(p.clickStepMs);

  // Metal: normalised by the nonzero weights (fixed per machine), filters set only when it sounds.
  int count = 0;
  for (int k = 0; k < kDrumMetal; ++k) count += p.metalW[k] != 0;
  static_assert(kDrumMetal == 6, "kInvCount");
  static constexpr float kInvCount[kDrumMetal + 1] = {0, 1.f, 1.f / 2, 1.f / 3, 1.f / 4, 1.f / 5, 1.f / 6};
  const float norm = kInvCount[count];  // = 1 / count, rounded alike
  // A zero weight only adds +-0: those squares are left out of the sum (their phases still run).
  int nm = 0, silent = kDrumMetal;
  for (int k = 0; k < kDrumMetal; ++k) {
    const float hz = p.metalHz[k] < kMaxHz ? p.metalHz[k] : kMaxHz;
    metalInc_[k] = hz > 0 ? static_cast<uint32_t>(static_cast<int32_t>(hz * kIncPerHz)) : 0;
    const float w = hz > 0 ? p.metalW[k] * norm : 0;
    if (w != 0) {
      sqW_[nm] = w;
      sqOsc_[nm++] = static_cast<uint8_t>(k);
    } else sqOsc_[--silent] = static_cast<uint8_t>(k);
  }
  nMetal_ = static_cast<uint8_t>(nm);
  const bool any = nm > 0;
  metalLvl_ = p.metalLvl;
  metalAcc_ = p.metalAccent;
  bpOn_ = p.metalBp1 > 0;
  bp2On_ = bpOn_ && p.metalBp2 > 0;
  metal2On_ = bp2On_ && p.metal2Lvl > 0;
  metal2Lvl_ = metal2On_ ? p.metal2Lvl : 0;
  metalOn_ = any && (metalLvl_ > 0 || metalAcc_ > 0 || metal2Lvl_ > 0);
  metalK_ = decayK(p.metalMs);
  accK_ = decayK(p.metalAccMs);
  metal2K_ = decayK(p.metal2Ms);
  metalKL_ = fastLog2(metalK_);
  metal2KL_ = fastLog2(metal2K_);
  metalHoldN_ = samples(p.metalHoldMs);
  metalAccInv_ = p.metalAccelMs > 0 ? fastRcp(p.metalAccelMs * kSamplesPerMs) : 0;
  metal2AccInv_ = p.metal2AccelMs > 0 ? fastRcp(p.metal2AccelMs * kSamplesPerMs) : 0;
  cutN_ = p.cutMs > 0 ? (samples(p.cutMs) > 0 ? samples(p.cutMs) : 1) : 0;
  cutK_ = p.cutMs > 0 ? decayK(p.cutFallMs) : 1.f;
  if (metalOn_) {
    const float q2 = p.metalQ2 > 0 ? p.metalQ2 : p.metalQ;
    if (bpOn_) bp1_.set(Svf::Mode::Bp, p.metalBp1, p.metalQ);
    if (bp2On_) bp2_.set(Svf::Mode::Bp, p.metalBp2, q2);
    bpGain_ = fastRcp(p.metalQ > 0.5f ? p.metalQ : 0.5f);  // Svf band-pass peak = q
    bp2Gain_ = fastRcp(q2 > 0.5f ? q2 : 0.5f);
    hpOn_ = p.metalHp > 0;
    if (hpOn_) hp_.set(Svf::Mode::Hp, p.metalHp, 0.707f);
  }

  // Noise.
  noiseLvl_ = p.noiseLvl;
  tail_ = p.tail;
  noiseK_ = decayK(p.noiseMs);
  noiseKL_ = fastLog2(noiseK_);
  noiseHoldN_ = samples(p.noiseHoldMs);
  // Signed: negative = the rate falls (see DrumParams::noiseAccMs).
  noiseAccInv_ = p.noiseAccMs > 0 ? fastRcp(p.noiseAccMs * kSamplesPerMs)
                                  : (p.noiseAccMs < 0 ? -fastRcp(-p.noiseAccMs * kSamplesPerMs) : 0);
  bursts_ = p.bursts < kDrumBursts ? p.bursts : kDrumBursts;
  burstMs_ = p.burstMs;
  for (int k = 0; k < kDrumBursts; ++k) burstHitMs_[k] = p.burstLenMs[k] > 0 ? p.burstLenMs[k] : p.burstMs;
  burstTauMs_ = p.burstTauMs;
  noiseWide_ = p.noiseWide;
  noiseFlt_ = p.noiseHz > 0;
  noiseMode_ = p.noiseMode;
  noiseHz_ = p.noiseHz;
  noiseQ_ = p.noiseQ;
  tailQ_ = p.tailQ;
  noiseFilter();

  // One-pole high-pass: 1 - e^(-2 pi hz / rate), hz below the Nyquist margin.
  const float hpHz = p.noiseHpHz < kMaxHz ? p.noiseHpHz : kMaxHz;
  noiseRise_ = p.noiseRiseMs > 0 ? p.noiseRise : 0;
  noiseRiseInv_ = noiseRise_ != 0 ? fastRcp(p.noiseRiseMs * kSamplesPerMs) : 0;
  noiseHpC_ = hpHz > 0 ? 1.f - fastExp(-6.2831853f * hpHz * (1.f / kSynthRate)) : 0;

  const float drive = p.drive < 0 ? 0 : (p.drive > 1 ? 1 : p.drive);
  driveGain_ = 1.f + 7.f * drive;
  driveOff_ = kOff * fastRcp(driveGain_);  // driveGain_ 1..8
}

MT_HOT void DrumVoice::control(int span) {
  if (span < 1) span = 1;
  if (fresh_) {
    // Bursts are latched at the trigger: their count and length don't follow later updates.
    fresh_ = false;
    const int32_t len = bursts_ ? static_cast<int32_t>(burstMs_ * kSamplesPerMs) : 0;
    burstsLeft_ = len > 0 ? bursts_ : 0;
    // burstNext_[j]: the hit that starts with j hits left (hit bursts_ - j).
    for (int j = 0; j < kDrumBursts; ++j) {
      const int h = bursts_ - j;
      burstNext_[j] = j > 0 && h > 0 ? static_cast<int32_t>(burstHitMs_[h] * kSamplesPerMs) : 0;
    }
    burstT_ = static_cast<int32_t>(burstHitMs_[0] * kSamplesPerMs);
    // Each hit fades by its tau, by default a quarter of burstMs (~35 dB over it).
    const float tau = burstTauMs_ > 0 ? burstTauMs_ * kSamplesPerMs : 0.25f * static_cast<float>(len);
    burstK_ = len > 0 ? fastExp(-fastRcp(tau)) : 1.f;
  }
  // After the hits the noise filter takes the tail's q (once a hit, back at the trigger).
  if (!burstsLeft_ && bursts_ && tailQ_ > 0 && !tailFlt_) {
    tailFlt_ = true;
    noiseFilter();
  }
  toneE_ = snap(toneE_);
  fastE_ = snap(fastE_);
  pitchE_ = snap(pitchE_);
  sweepAE_ = snap(sweepAE_);
  sweepBE_ = snap(sweepBE_);
  for (float& e : toneDE_) e = snap(e);
  clickE_ = snap(clickE_);
  metalE_ = snap(metalE_);
  accE_ = snap(accE_);
  metal2E_ = snap(metal2E_);
  noiseE_ = snap(noiseE_);
  tailV_ = snap(tailV_);

  // Tones: silent and frozen until the delay is over, then held / decaying from their own start.
  const bool wait = age_ < delay_;
  const uint32_t t = wait ? 0 : age_ - delay_;
  toneKNow_ = wait || t < holdN_ ? 1.f : toneK_;
  fastKNow_ = wait || t < fastHoldN_ ? 1.f : fastK_;
  pitchKNow_ = wait ? 1.f : pitchK_;
  for (int k = 0; k < kDrumTones; ++k) toneDK_[k] = wait ? 1.f : toneDKSet_[k];

  // Pitch now and span samples later (the envelopes decay in between).
  const float fspan = static_cast<float>(span);
  float m0 = 1, m1 = 1;
  if (pitchEnv_ != 0 && pitchE_ > 0) {
    m0 = fastExp2(pitchEnv_ * pitchE_ * (1.f / 12.f));
    m1 = fastExp2(pitchEnv_ * pitchE_ * fastExp2(fspan * (wait ? 0.f : pitchKL_)) * (1.f / 12.f));  // log2 1 = 0
  }
  if (!wait) {
    const float a0 = sweepAE_, b0 = sweepBE_;
    const float a1 = sweepA_ != 0 && a0 > 0 ? a0 * fastExp2(fspan * sweepAKL_) : a0;
    const float b1 = sweepB_ != 0 && b0 > 0 ? b0 * fastExp2(fspan * sweepBKL_) : b0;
    m0 *= 1.f + sweepA_ * a0 + sweepB_ * b0;
    m1 *= 1.f + sweepA_ * a1 + sweepB_ * b1;
    sweepAE_ = a1;
    sweepBE_ = b1;
  }
  // No '/' per tick (a __divsf3 call each on the S3): kControl spans divide exactly by a multiply.
  const float invSpan = span == 32 ? 1.f / 32 : 1.f / fspan;
  for (int k = 0; k < kDrumTones; ++k) {
    const float hz = wait ? 0 : toneHz_[k];
    float h0 = hz * m0, h1 = hz * m1;
    h0 = h0 < kMaxHz ? h0 : kMaxHz;
    h1 = h1 < kMaxHz ? h1 : kMaxHz;
    const float i0 = h0 * kIncPerHz, di = (h1 - h0) * kIncPerHz * invSpan;
    toneInc_[k] = static_cast<uint32_t>(static_cast<int32_t>(i0));  // < 2^31
    toneDInc_[k] = static_cast<int32_t>(di < 0 ? di - 0.5f : di + 0.5f);
  }
  rampLeft_ = span;
  clickDc_ = age_ < stepN_ ? clickStep_ : 0;

  // Noise: burst decay while hits are left, then the tail's: flat on the plateau, then speeding up
  // with noiseAccMs (the rate this block: k^(1 + time / acc)).
  if (age_ < noiseHoldN_) noiseKTail_ = 1;
  else if (noiseAccInv_ != 0 && noiseK_ < 1) {
    const float t = static_cast<float>(age_ - noiseHoldN_);
    noiseKTail_ = noiseAccInv_ > 0 ? fastExp2((1.f + t * noiseAccInv_) * noiseKL_)
                                   : fastExp2(fastRcp(minf(1.f - t * noiseAccInv_, kSlowMax)) * noiseKL_);
  } else noiseKTail_ = noiseK_;

  // Metal: held, then decaying (speeding up with metal(2)AccInv_: k^(1 + time / acc)).
  if (age_ < metalHoldN_) metalKNow_ = metal2KNow_ = 1;
  else {
    const float mt = static_cast<float>(age_ - metalHoldN_);
    metalKNow_ = metalAccInv_ > 0 && metalK_ < 1 ? fastExp2((1.f + mt * metalAccInv_) * metalKL_) : metalK_;
    metal2KNow_ = metal2AccInv_ > 0 && metal2K_ < 1 ? fastExp2((1.f + mt * metal2AccInv_) * metal2KL_) : metal2K_;
  }
  accKNow_ = accK_;
  // The cut: whichever falls faster.
  if (cutN_ && age_ >= cutN_) {
    metalKNow_ = minf(metalKNow_, cutK_);
    metal2KNow_ = minf(metal2KNow_, cutK_);
    accKNow_ = minf(accKNow_, cutK_);
    noiseKTail_ = minf(noiseKTail_, cutK_);
  }
  noiseKNow_ = burstsLeft_ ? burstK_ : noiseKTail_;
  // The swell at the block's middle.
  noiseRiseNow_ = noiseRiseInv_ > 0 ? 1.f - noiseRise_ * fastExp(-(static_cast<float>(age_) + 0.5f * fspan) * noiseRiseInv_)
                                    : 1.f;
}

// Metal: squares -> band-passes (bp2 with its own envelope or bp1's), bp1 (or the squares) high-passed.
// Each square and each filter in a pass of its own over the block, its state in registers: the same
// sums in the same order as sample by sample, bit for bit. Stages pass rounded values, as across the
// old per-sample branches (GCC fuses madd.s within a basic block only): keep products unsplit.
MT_HOT void DrumVoice::metal(float* __restrict m, int n) {
  float sq[kSeg];
  for (int k = 0; k < nMetal_; ++k) {  // nMetal_ >= 1 (metalOn_)
    const int o = sqOsc_[k];
    uint32_t ph = metalPh_[o];
    const uint32_t inc = metalInc_[o];
    const float w = sqW_[k], nw = -w;
    if (k == 0) {  // 0 + w = w: the sum starts at the first term
      for (int i = 0; i < n; ++i) {
        sq[i] = (ph & 0x80000000u) ? w : nw;
        ph += inc;
      }
    } else {
      for (int i = 0; i < n; ++i) {
        sq[i] += (ph & 0x80000000u) ? w : nw;
        ph += inc;
      }
    }
    metalPh_[o] = ph;
  }
  for (int k = nMetal_; k < kDrumMetal; ++k) metalPh_[sqOsc_[k]] += metalInc_[sqOsc_[k]] * static_cast<uint32_t>(n);
  // g = metalLvl * metalE + metalAcc * accE, recomputed by each pass that needs it.
  const float lvl = metalLvl_, acc = metalAcc_, mk = metalKNow_, ak = accKNow_, m2k = metal2KNow_;
  const float e0 = metalE_, a0 = accE_;
  float e = e0, a = a0, e2 = metal2E_;
  if (bpOn_) {
    Svf::Run f = bp1_.run();
    const float gain = bpGain_;
    for (int i = 0; i < n; ++i) {
      m[i] = f.bp(sq[i]) * gain * (lvl * e + acc * a);
      e *= mk;
      a *= ak;
    }
    bp1_.store(f);
  } else {
    for (int i = 0; i < n; ++i) {
      m[i] = sq[i] * (lvl * e + acc * a);
      e *= mk;
      a *= ak;
    }
  }
  if (hpOn_) {
    Svf::Run f = hp_.run();
    for (int i = 0; i < n; ++i) m[i] = f.hp(m[i]);
    hp_.store(f);
  }
  if (bp2On_) {
    Svf::Run f = bp2_.run();
    const float gain = bp2Gain_;
    if (metal2On_) {
      const float lvl2 = metal2Lvl_;
      for (int i = 0; i < n; ++i) {
        m[i] = m[i] + f.bp(sq[i]) * gain * (lvl2 * e2);
        e2 *= m2k;
      }
    } else {
      float e1 = e0, a1 = a0;
      for (int i = 0; i < n; ++i) {
        m[i] = m[i] + f.bp(sq[i]) * gain * (lvl * e1 + acc * a1);
        e1 *= mk;
        a1 *= ak;
      }
    }
    bp2_.store(f);
  }
  if (!bp2On_ || !metal2On_)
    for (int i = 0; i < n; ++i) e2 *= m2k;
  metalE_ = e, accE_ = a, metal2E_ = e2;
}

// Tones for n <= kSeg samples: s = dc + the tones x the body envelope (dc alone when off), their
// envelopes and pitch ramp advanced. One statement per sum as before: GCC fuses alike.
MT_HOT void DrumVoice::tones(float* __restrict s, int n, bool on) {
  const float dc = clickDc_;
  float toneE = toneE_, fastE = fastE_, pitchE = pitchE_;
  float e0 = toneDE_[0], e1 = toneDE_[1], e2 = toneDE_[2];
  const float toneK = toneKNow_, fastK = fastKNow_, pitchK = pitchKNow_;
  const float k0 = toneDK_[0], k1 = toneDK_[1], k2 = toneDK_[2];
  if (on) {
    // The body envelope first: fewer values live in the tones' loop.
    float g[kSeg];
    const float slowL = slowLvl_, fastL = fastLvl_;
    for (int i = 0; i < n; ++i) {
      g[i] = slowL * toneE + fastL * fastE;
      toneE *= toneK;
      fastE *= fastK;
      pitchE *= pitchK;
    }
    uint32_t ph0 = tonePh_[0], ph1 = tonePh_[1], ph2 = tonePh_[2];
    uint32_t inc0 = toneInc_[0], inc1 = toneInc_[1], inc2 = toneInc_[2];
    const int32_t d0 = toneDInc_[0], d1 = toneDInc_[1], d2 = toneDInc_[2];
    const float l0 = toneLvl_[0], l1 = toneLvl_[1], l2 = toneLvl_[2];
    const float h3 = h3_, h5 = h5_;
    const bool t2 = tone2_, shape = h3 != 0 || h5 != 0;
    // The pitch ramp over its first r samples, then flat (stops at the target if more than span
    // samples are rendered): one loop body, no per-sample count.
    const int r = rampLeft_ < n ? (rampLeft_ > 0 ? rampLeft_ : 0) : n;
    rampLeft_ -= r;
    for (int part = 0, i = 0; part < 2; ++part) {
      const int end = part ? n : r;
      const uint32_t a0 = part ? 0 : static_cast<uint32_t>(d0), a1 = part ? 0 : static_cast<uint32_t>(d1),
                     a2 = part ? 0 : static_cast<uint32_t>(d2);
      for (; i < end; ++i) {
        float x = l0 * e0 * tableSine(ph0);
        if (shape) {  // tone 0 only: + h3 T3 + h5 T5, clamped (T3(sin) = -sin 3w, T5(sin) = sin 5w)
          const float c = x > 1.f ? 1.f : (x < -1.f ? -1.f : x), c2 = c * c;
          x += c * (h3 * (4.f * c2 - 3.f) + h5 * ((16.f * c2 - 20.f) * c2 + 5.f));
        }
        x += l1 * e1 * tableSine(ph1);
        ph0 += inc0;
        ph1 += inc1;
        if (t2) {
          x += l2 * e2 * tableSine(ph2);
          ph2 += inc2;
        }
        inc0 += a0;
        inc1 += a1;
        inc2 += a2;
        s[i] = dc + x * g[i];
        e0 *= k0;
        e1 *= k1;
        e2 *= k2;
      }
    }
    tonePh_[0] = ph0, tonePh_[1] = ph1, tonePh_[2] = ph2;
    toneInc_[0] = inc0, toneInc_[1] = inc1, toneInc_[2] = inc2;
  } else {
    for (int i = 0; i < n; ++i) {
      s[i] = dc;
      toneE *= toneK;
      fastE *= fastK;
      e0 *= k0;
      e1 *= k1;
      e2 *= k2;
      pitchE *= pitchK;
    }
  }
  toneDE_[0] = e0, toneDE_[1] = e1, toneDE_[2] = e2;
  toneE_ = toneE, fastE_ = fastE, pitchE_ = pitchE;
}

// s += click, metal m, noise for n <= kSeg samples; their envelopes and the hits advanced.
MT_HOT void DrumVoice::noise(float* __restrict s, const float* __restrict m, int n, bool clickOn,
                             bool metalOn, bool noiseOn) {
  float clickE = clickE_, noiseE = noiseE_, noiseK = noiseKNow_;
  const float clickK = clickK_, click = click_, tail = tail_;
  const float noiseLvl = noiseLvl_ * noiseGain_ * noiseRiseNow_;
  const float wide = noiseWide_ * noiseLvl_ * noiseRiseNow_;  // broadband alongside the filtered noise
  const bool noiseFlt = noiseFlt_;
  const float nhpC = noiseHpC_;
  float nhpZ = noiseHpZ_;
  // In place: a local copy (Svf::Run) spills more here.
  Svf& nf = nf_;
  uint8_t burstsLeft = burstsLeft_;
  int32_t burstT = burstT_;
  const int32_t* const burstNext = burstNext_;
  uint32_t rng = rng_;
  bool impulse = impulse_;
  for (int i = 0; i < n; ++i) {
    float v = s[i];
    // White noise, shared by the click and the noise part.
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    const float wn = static_cast<int32_t>(rng) * (1.f / 2147483648.f);
    if (clickOn) v += click * clickE * (impulse ? 1.f : wn);
    impulse = false;
    if (metalOn) v += m[i];
    if (noiseOn) {
      float nz = noiseFlt ? nf.process(wn) : wn;
      if (nhpC > 0) {
        nhpZ += nhpC * (nz - nhpZ);
        nz -= nhpZ;
      }
      v += (nz * noiseLvl + wn * wide) * noiseE;
    }
    s[i] = v;
    clickE *= clickK;
    noiseE *= noiseK;
    // Clap: each hit restarts the noise at 1; after the last one it drops to the tail level and
    // decays with noiseMs. Counted even with the noise off, so the voice still ends.
    if (burstsLeft && --burstT <= 0) {
      noiseE = --burstsLeft ? 1.f : tail;
      burstT = burstNext[burstsLeft];
      if (!burstsLeft) noiseK = noiseKTail_;
    }
  }
  clickE_ = clickE;
  noiseHpZ_ = nhpZ;
  noiseE_ = noiseE, noiseKNow_ = noiseK;
  burstsLeft_ = burstsLeft;
  burstT_ = burstT;
  rng_ = rng;
  impulse_ = impulse;
}

MT_HOT void DrumVoice::render(float* __restrict out, int n, float amp) {
  // Each part in a pass of its own over kSeg samples (state in registers), summed in the old order;
  // parts whose envelope has snapped to 0 are skipped (on / off fixed for the whole call).
  const bool tonesOn = (toneLvl_[0] != 0 || toneLvl_[1] != 0 || toneLvl_[2] != 0) && (toneE_ != 0 || fastE_ != 0);
  const bool clickOn = click_ > 0 && clickE_ != 0;
  const bool metalOn = metalOn_ && (metalE_ != 0 || accE_ != 0 || metal2E_ != 0);
  const bool noiseOn = noiseLvl_ * noiseGain_ * noiseRiseNow_ > 0 && (noiseE_ != 0 || burstsLeft_);
  const float driveGain = driveGain_, chokeK = kChokeK;
  float tailV = tailV_, last = last_;
  float sb[kSeg], mb[kSeg];
  for (int i0 = 0; i0 < n; i0 += kSeg) {
    const int seg = n - i0 < kSeg ? n - i0 : kSeg;
    float* __restrict o = out + i0;
    // Every envelope runs from the trigger, sounding or not: a level raised later (LFO) can't
    // bring a part in at its trigger level.
    if (metalOn) metal(mb, seg);
    else {
      float me = metalE_, ae = accE_, m2e = metal2E_;
      const float mk = metalKNow_, ak = accKNow_, m2k = metal2KNow_;
      for (int i = 0; i < seg; ++i) {
        me *= mk;
        ae *= ak;
        m2e *= m2k;
      }
      metalE_ = me, accE_ = ae, metal2E_ = m2e;
    }
    tones(sb, seg, tonesOn);
    noise(sb, mb, seg, clickOn, metalOn, noiseOn);
    for (int i = 0; i < seg; ++i) {
      // Output soft clip, drive into it (slope 1 at 0, flat at +-1).
      const float s = softSat(sb[i] * driveGain);
      last = s * amp + tailV;
      o[i] += last;
      tailV *= chokeK;
    }
  }
  tailV_ = tailV, last_ = last;
  age_ = age_ < 0x40000000u ? age_ + static_cast<uint32_t>(n) : age_;
}

MT_HOT bool DrumVoice::done() const {
  if (fresh_ || burstsLeft_ || clickDc_ != 0) return false;
  // Drive raises quiet parts by up to driveGain_ (the clip is linear near 0).
  const float off = driveOff_;
  float tl = 0;
  for (int k = 0; k < kDrumTones; ++k) tl += fabsf(toneLvl_[k]) * toneDE_[k];
  const float t = tl * (fabsf(slowLvl_) * toneE_ + fabsf(fastLvl_) * fastE_) * (1.f + fabsf(h3_) + fabsf(h5_));
  const float m = metalOn_ ? metalE_ * metalLvl_ + accE_ * metalAcc_ + metal2E_ * metal2Lvl_ : 0;
  const float nz = noiseE_ * noiseLvl_ * (noiseGain_ + noiseWide_);
  const float c = clickE_ * click_;
  return t < off && m < off && nz < off && c < off && fabsf(tailV_) < kOff;
}

}  // namespace mt
