#include "synth_drum.h"
#include <math.h>
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

// Per-sample multiplier reaching -60 dB in ms; 0 ms = no decay.
float decayK(float ms) { return ms > 0 ? expf(-6.9077553f / (ms * kSamplesPerMs)) : 1.f; }
const float kChokeK = decayK(2);  // choke tail: 2 ms to -60 dB
const float kAccK = decayK(kAccentMs);

uint32_t gSeed = 0;  // noise seeds of successive triggers (a race only repeats a seed)

// Soft clip: slope 1 at 0, flat at +-1 from |x| = 1.5. No division: the ESP32 does float division
// in software (__divsf3), far too slow per sample.
MT_INLINE float softSat(float x) {
  float u = x * (2.f / 3.f);
  u = u > 1.f ? 1.f : (u < -1.f ? -1.f : u);
  return u * (1.5f - 0.5f * u * u);
}

float snap(float e) { return e < kZero && e > -kZero ? 0 : e; }

}  // namespace

void drumResetNoise() { gSeed = 0; }

float DrumVoice::K::get(float t) {
  if (t != ms) {
    ms = t;
    k = decayK(t);
  }
  return k;
}

MT_HOT void DrumVoice::Flt::set(Svf::Mode m, float h, float qq) {
  if (m == mode && h == hz && qq == q) return;
  mode = m;
  hz = h;
  q = qq;
  svf.set(m, h, qq);
}

void DrumVoice::trigger(bool keepTail) {
  tailV_ = keepTail ? last_ : 0;
  tonePh_[0] = tonePh_[1] = 0;
  toneE_ = pitchE_ = clickE_ = metalE_ = accE_ = noiseE_ = 1;
  impulse_ = true;
  burstsLeft_ = 0;
  fresh_ = true;
  // Voices triggered together play different noise.
  gSeed += 0x9E3779B9u;
  rng_ ^= gSeed;
  if (!rng_) rng_ = 0x9E3779B9u;
  bp1_.svf.reset();
  bp2_.svf.reset();
  hp_.svf.reset();
  nf_.svf.reset();
}

MT_HOT void DrumVoice::control(const DrumParams& p, int span) {
  if (span < 1) span = 1;
  if (fresh_) {
    // Bursts are latched at the trigger: their count and length don't follow later updates.
    fresh_ = false;
    burstLen_ = p.bursts ? static_cast<int32_t>(p.burstMs * kSamplesPerMs) : 0;
    burstsLeft_ = burstLen_ > 0 ? p.bursts : 0;
    burstT_ = burstLen_;
    burstK_ = burstLen_ > 0 ? expf(-4.f / burstLen_) : 1.f;  // each hit fades ~35 dB
  }
  toneE_ = snap(toneE_);
  pitchE_ = snap(pitchE_);
  clickE_ = snap(clickE_);
  metalE_ = snap(metalE_);
  accE_ = snap(accE_);
  noiseE_ = snap(noiseE_);
  tailV_ = snap(tailV_);

  // Tones: pitch now and span samples later (the pitch envelope decays in between).
  const float pk = pitchK_.get(p.pitchMs);
  toneK_.get(p.toneMs);
  float m0 = 1, m1 = 1;
  if (p.pitchEnv != 0 && pitchE_ > 0) {
    m0 = exp2f(p.pitchEnv * pitchE_ * (1.f / 12.f));
    m1 = exp2f(p.pitchEnv * pitchE_ * powf(pk, static_cast<float>(span)) * (1.f / 12.f));
  }
  for (int k = 0; k < kDrumTones; ++k) {
    const float hz = p.toneHz[k];
    toneLvl_[k] = hz > 0 ? p.toneLvl[k] : 0;
    float h0 = hz * m0, h1 = hz * m1;
    h0 = h0 < kMaxHz ? h0 : kMaxHz;
    h1 = h1 < kMaxHz ? h1 : kMaxHz;
    const float i0 = h0 * kIncPerHz, di = (h1 - h0) * kIncPerHz / span;
    toneInc_[k] = static_cast<uint32_t>(static_cast<int32_t>(i0));  // < 2^31
    toneDInc_[k] = static_cast<int32_t>(di < 0 ? di - 0.5f : di + 0.5f);
  }
  rampLeft_ = span;

  click_ = p.click;
  clickK_.get(p.clickMs);

  // Metal: normalised by the nonzero weights (fixed per machine), filters set only when it sounds.
  int count = 0;
  for (int k = 0; k < kDrumMetal; ++k) count += p.metalW[k] != 0;
  const float norm = count ? 1.f / count : 0;
  bool any = false;
  for (int k = 0; k < kDrumMetal; ++k) {
    const float hz = p.metalHz[k] < kMaxHz ? p.metalHz[k] : kMaxHz;
    metalInc_[k] = hz > 0 ? static_cast<uint32_t>(static_cast<int32_t>(hz * kIncPerHz)) : 0;
    metalW_[k] = hz > 0 ? p.metalW[k] * norm : 0;
    any |= metalW_[k] != 0;
  }
  metalLvl_ = p.metalLvl;
  metalAcc_ = p.metalAccent;
  metalOn_ = any && (metalLvl_ > 0 || metalAcc_ > 0);
  metalK_.get(p.metalMs);
  if (metalOn_) {
    bpOn_ = p.metalBp1 > 0;
    bp2On_ = bpOn_ && p.metalBp2 > 0;
    if (bpOn_) bp1_.set(Svf::Mode::Bp, p.metalBp1, p.metalQ);
    if (bp2On_) bp2_.set(Svf::Mode::Bp, p.metalBp2, p.metalQ);
    bpGain_ = 1.f / (p.metalQ > 0.5f ? p.metalQ : 0.5f);  // Svf band-pass peak = q
    hpOn_ = p.metalHp > 0;
    if (hpOn_) hp_.set(Svf::Mode::Hp, p.metalHp, 0.707f);
  }

  // Noise: burst decay while hits are left, then the tail's.
  noiseLvl_ = p.noiseLvl;
  tail_ = p.tail;
  const float nk = noiseK_.get(p.noiseMs);
  noiseKNow_ = burstsLeft_ ? burstK_ : nk;
  noiseFlt_ = p.noiseHz > 0;
  noiseGain_ = 1;
  if (noiseFlt_ && noiseLvl_ > 0) {
    nf_.set(p.noiseMode, p.noiseHz, p.noiseQ);
    if (p.noiseMode == Svf::Mode::Bp) {
      // Svf band-pass: peak q, bandwidth hz / q, so white noise keeps pi hz q / rate of its power.
      const float hz = p.noiseHz < 20.f ? 20.f : (p.noiseHz > kMaxHz ? kMaxHz : p.noiseHz);
      const float q = p.noiseQ < 0.5f ? 0.5f : p.noiseQ;
      noiseGain_ = kBpNoise * sqrtf(kSynthRate / (3.14159265f * hz * q));
    }
  }

  drive_ = p.drive < 0 ? 0 : (p.drive > 1 ? 1 : p.drive);
  driveGain_ = 1.f + 7.f * drive_;
}

MT_HOT void DrumVoice::render(float* __restrict out, int n, float amp) {
  // Hot state in locals; parts whose envelope has snapped to 0 are skipped.
  uint32_t ph0 = tonePh_[0], ph1 = tonePh_[1], inc0 = toneInc_[0], inc1 = toneInc_[1];
  const int32_t d0 = toneDInc_[0], d1 = toneDInc_[1];
  int32_t ramp = rampLeft_;
  const float l0 = toneLvl_[0], l1 = toneLvl_[1];
  float toneE = toneE_, pitchE = pitchE_, clickE = clickE_, metalE = metalE_, accE = accE_;
  float noiseE = noiseE_, noiseK = noiseKNow_, tailV = tailV_, last = last_;
  const float toneK = toneK_.k, pitchK = pitchK_.k, clickK = clickK_.k, metalK = metalK_.k;
  const float click = click_, noiseLvl = noiseLvl_ * noiseGain_, drive = drive_;
  const float driveGain = driveGain_, metalLvl = metalLvl_, metalAcc = metalAcc_;
  const float bpGain = bpGain_, tail = tail_;
  const bool tones = (l0 != 0 || l1 != 0) && toneE != 0;
  const bool clickOn = click > 0 && clickE != 0;
  const bool metalOn = metalOn_ && (metalE != 0 || accE != 0);
  const bool bpOn = bpOn_, bp2On = bp2On_, hpOn = hpOn_, noiseFlt = noiseFlt_;
  const bool noiseOn = noiseLvl > 0 && (noiseE != 0 || burstsLeft_);
  // In place: local copies of the arrays and filters cost a memcpy each way per block.
  uint32_t* const mph = metalPh_;
  const uint32_t* const minc = metalInc_;
  const float* const mw = metalW_;
  Svf &bp1 = bp1_.svf, &bp2 = bp2_.svf, &hp = hp_.svf, &nf = nf_.svf;
  uint8_t burstsLeft = burstsLeft_;
  int32_t burstT = burstT_;
  const int32_t burstLen = burstLen_;
  uint32_t rng = rng_;
  bool impulse = impulse_;
  for (int i = 0; i < n; ++i) {
    float s = 0;
    if (tones) {
      s = (l0 * tableSine(ph0) + l1 * tableSine(ph1)) * toneE;
      ph0 += inc0;
      ph1 += inc1;
      if (ramp > 0) {  // stops at the target if more than span samples are rendered
        inc0 += static_cast<uint32_t>(d0);
        inc1 += static_cast<uint32_t>(d1);
        --ramp;
      }
    }
    // White noise, shared by the click and the noise part.
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    const float wn = static_cast<int32_t>(rng) * (1.f / 2147483648.f);
    if (clickOn) s += click * clickE * (impulse ? 1.f : wn);
    impulse = false;
    // Metal: squares -> band-passes -> high-pass.
    if (metalOn) {
      float m = 0;
      for (int k = 0; k < kDrumMetal; ++k) {
        m += (mph[k] & 0x80000000u) ? mw[k] : -mw[k];
        mph[k] += minc[k];
      }
      if (bpOn) m = (bp2On ? bp1.process(m) + bp2.process(m) : bp1.process(m)) * bpGain;
      if (hpOn) m = hp.process(m);
      s += m * (metalLvl * metalE + metalAcc * accE);
    }
    if (noiseOn) s += (noiseFlt ? nf.process(wn) : wn) * noiseLvl * noiseE;
    // Every envelope runs from the trigger, sounding or not: a level raised later (LFO) can't
    // bring a part in at its trigger level.
    toneE *= toneK;
    pitchE *= pitchK;
    clickE *= clickK;
    metalE *= metalK;
    accE *= kAccK;
    noiseE *= noiseK;
    // Clap: each hit restarts the noise at 1; after the last one it drops to the tail level and
    // decays with noiseMs. Counted even with the noise off, so the voice still ends.
    if (burstsLeft && --burstT <= 0) {
      burstT = burstLen;
      noiseE = --burstsLeft ? 1.f : tail;
      if (!burstsLeft) noiseK = noiseK_.k;
    }
    if (drive > 0) s = softSat(s * driveGain);
    s = s > 1.f ? 1.f : (s < -1.f ? -1.f : s);
    last = s * amp + tailV;
    out[i] += last;
    tailV *= kChokeK;
  }
  tonePh_[0] = ph0, tonePh_[1] = ph1, toneInc_[0] = inc0, toneInc_[1] = inc1;
  rampLeft_ = ramp;
  toneE_ = toneE, pitchE_ = pitchE, clickE_ = clickE, metalE_ = metalE, accE_ = accE;
  noiseE_ = noiseE, noiseKNow_ = noiseK, tailV_ = tailV, last_ = last;
  burstsLeft_ = burstsLeft;
  burstT_ = burstT;
  rng_ = rng;
  impulse_ = impulse;
}

bool DrumVoice::done() const {
  if (fresh_ || burstsLeft_) return false;
  // Drive raises quiet parts by up to driveGain_ (tanh is linear near 0).
  const float off = kOff / driveGain_;
  const float t = toneE_ * (fabsf(toneLvl_[0]) + fabsf(toneLvl_[1]));
  const float m = metalOn_ ? metalE_ * metalLvl_ + accE_ * metalAcc_ : 0;
  const float nz = noiseE_ * noiseLvl_ * noiseGain_;
  const float c = clickE_ * click_;
  return t < off && m < off && nz < off && c < off && fabsf(tailV_) < kOff;
}

}  // namespace mt
