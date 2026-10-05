#pragma once
#include <stdint.h>
#include "synth_filter.h"

namespace mt {

constexpr int kFmOps = 4;

// Operator routing, op1 = index 0; ">" = modulates.
enum class FmAlg : uint8_t {
  Stack,       // 4>3>2>1, out 1
  TwoToOne,    // 4>3>1, 2>1, out 1
  TwoPairs,    // 4>3, 2>1, out 1 + 3
  OneToThree,  // 4>1, 4>2, 4>3, out 1 + 2 + 3
  Additive,    // out 1 + 2 + 3 + 4
  Count
};
bool fmCarrier(FmAlg a, int op);
// Largest modulation index (radians) of a modulator at hz: keeps the sidebands below ~12 kHz.
float fmMaxIndex(float hz);

// Times are to -60 dB, ms; 0 = no decay.
struct FmOp {
  float ratio = 1;    // x FmParams::hz
  float level = 0;    // carrier: gain; modulator: index, radians
  float sustain = 0;  // fraction of level the decay ends at, 0..1
  float decayMs = 0;
  float fb = 0;       // self-modulation, radians
};

// One FM voice's sound, made by a machine (synth_fm_machines) at control rate.
struct FmParams {
  FmAlg alg = FmAlg::Additive;
  FmOp op[kFmOps];
  float hz = 0;          // base frequency
  float pitchEnv = 0;    // semitones at the trigger, decaying to 0
  float pitchMs = 0;
  float fbEnv = 0;       // feedback x (1 + fbEnv) at the trigger, decaying
  float fbMs = 0;
  float noise = 0;       // white noise level
  float noiseMs = 0;     // 0 = flat
  uint8_t bursts = 0;    // noise retriggers before the decay (clap)
  float burstMs = 0;
  float tail = 1;        // noise level after the bursts
  Svf::Mode filterMode = Svf::Mode::Lp;
  float filterHz = 0;    // 0 = no filter
  float filterQ = 0.707f;
  bool filterAll = false;  // filter the operators too, else the noise only
  bool oneShot = true;     // own amplitude envelope; false = held at 1 (the caller's ADSR shapes it)
  float ampMs = 0;
};

// Runtime of one FM voice. control() takes the values at the current time and ramps them
// linearly to their values span samples later; render() advances per sample.
class FmVoice {
 public:
  // keepPhase: retrigger of a sounding voice (choke): oscillators run on and the amplitude
  // ramps from where it is, so there is no click.
  void trigger(bool keepPhase);
  void control(const FmParams& p, int span);
  // Adds n samples x gain to out.
  void render(float* out, int n, float gain);
  // One-shot sound has decayed to silence.
  bool done() const { return oneShot_ && t_ > kAttack && amp_ < 1e-4f; }

 private:
  static constexpr uint32_t kAttack = 16;  // one-shot attack, samples (0.5 ms)

  // Exponential decay to -60 dB over ms, starting at off (samples after the trigger), advanced
  // by one multiplication per control tick; expf only when ms, off, span or the time jumps.
  struct Decay {
    float ms = 0, off = 0, c = 1, e = 1;
    int span = -1;
    uint32_t t = 0xffffffffu;  // time e belongs to
    // Values at t0 and t0 + span; both >= off.
    void at(float ms, float off, uint32_t t0, int span, float& e0, float& e1);
  };

  template <FmAlg A>
  void run(float* __restrict out, int n, float gain);
  void noisePair(const FmParams& p, uint32_t t0, int span, float& n0, float& n1);
  void ampPair(const FmParams& p, uint32_t t0, int span, float& a0, float& a1);

  uint32_t ph_[kFmOps] = {0};
  uint32_t inc_[kFmOps] = {0};  // phase units (2^32 = cycle) per sample
  int32_t dInc_[kFmOps] = {0};
  float lvl_[kFmOps] = {0}, dLvl_[kFmOps] = {0};  // modulators: in cycles
  float fb_[kFmOps] = {0}, dFb_[kFmOps] = {0};    // cycles
  float last_[kFmOps] = {0}, fbIn_[kFmOps] = {0};  // feedback: mean of the last two outputs
  float noise_ = 0, dNoise_ = 0;
  float amp_ = 0, dAmp_ = 0, ampStart_ = 0;
  uint32_t rng_ = 0x12345678;
  uint32_t t_ = 0;  // samples since the trigger
  FmAlg alg_ = FmAlg::Additive;
  bool oneShot_ = false, filterOn_ = false, filterAll_ = false;
  bool choke_ = false;  // next control() ramps levels from where they are
  Decay opDec_[kFmOps], pitchDec_, fbDec_, noiseDec_, ampDec_;
  float bendEnv_ = 0, bendD_ = -1, bend_ = 1;
  float filterHz_ = -1, filterQ_ = 0;  // svf_ coefficients set for these (skips tanf)
  Svf::Mode filterMode_ = Svf::Mode::Lp;  // last pitch bend: exp2 of bendEnv_ x bendD_ / 12
  Svf svf_;
};

}  // namespace mt
