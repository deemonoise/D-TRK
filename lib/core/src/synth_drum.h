#pragma once
#include <stdint.h>
#include "synth_filter.h"

namespace mt {

constexpr int kDrumTones = 2;
constexpr int kDrumMetal = 6;

// One DRUM voice's sound, made by a machine (synth_drum_machines) at control rate. Times are to
// -60 dB, ms; 0 = no decay. Every part has its own exponential envelope from the trigger.
struct DrumParams {
  float toneHz[kDrumTones] = {0, 0};  // sines, 0 = off
  float toneLvl[kDrumTones] = {0, 0};
  float toneMs = 0;
  float pitchEnv = 0, pitchMs = 0;    // semitones at the trigger, decaying to 0 (tones only)
  float click = 0, clickMs = 2;       // impulse + noise burst at the trigger
  float metalHz[kDrumMetal] = {0, 0, 0, 0, 0, 0};  // squares, 0 = off
  // Per-square weight; the sum is divided by the count of nonzero weights, so a square that drops
  // out (hz 0 above the Nyquist limit) doesn't change the others' level. Unused squares: weight 0.
  float metalW[kDrumMetal] = {1, 1, 1, 1, 1, 1};
  float metalLvl = 0, metalMs = 0;
  float metalAccent = 0;              // extra level decaying in kAccentMs (cowbell attack)
  float metalBp1 = 0, metalBp2 = 0;   // band-pass centres, unity peak, summed; bp1 0 = skip both,
                                      // bp2 0 = bp1 only
  float metalQ = 1.5f;
  float metalHp = 0;                  // high-pass after the band-passes, 0 = skip
  float noiseLvl = 0, noiseMs = 0;
  Svf::Mode noiseMode = Svf::Mode::Hp;
  float noiseHz = 0, noiseQ = 0.707f; // 0 Hz = unfiltered; Bp: same power at any hz, q
  uint8_t bursts = 0;                 // short noise hits before the tail (clap), latched at the trigger
  float burstMs = 0;                  // length of one hit
  float tail = 1;                     // noise level after the bursts
  float drive = 0;                    // 0..1, tanh saturation of the sum
};
constexpr float kAccentMs = 15;

// Runtime of one DRUM voice. control() takes the params at the current time and ramps the tone
// pitch linearly to its value span samples later; render() adds amp x sound to out.
class DrumVoice {
 public:
  // keepTail: choke — the voice's last output fades out over ~2 ms under the new hit.
  void trigger(bool keepTail);
  void control(const DrumParams& p, int span);
  void render(float* __restrict out, int n, float amp);
  // Every part below -80 dB (after drive) and no bursts left (also before the first trigger).
  bool done() const;

 private:
  // Per-sample decay multiplier of a time; expf only when the time changes.
  struct K {
    float ms = -1, k = 1;
    float get(float t);
  };
  // Svf coefficients; tanf only when the settings change.
  struct Flt {
    Svf svf;
    Svf::Mode mode = Svf::Mode::Lp;
    float hz = -1, q = 0;
    void set(Svf::Mode m, float hz, float q);
  };

  uint32_t tonePh_[kDrumTones] = {0, 0};
  uint32_t toneInc_[kDrumTones] = {0, 0};  // phase units (2^32 = cycle) per sample
  int32_t toneDInc_[kDrumTones] = {0, 0};
  int32_t rampLeft_ = 0;                   // samples until the pitch ramp reaches its target
  float toneLvl_[kDrumTones] = {0, 0};
  float toneE_ = 0, pitchE_ = 0;           // pitch envelope, 0..1
  float clickE_ = 0, click_ = 0;
  bool impulse_ = false;
  uint32_t metalPh_[kDrumMetal] = {0, 0, 0, 0, 0, 0};
  uint32_t metalInc_[kDrumMetal] = {0, 0, 0, 0, 0, 0};
  float metalW_[kDrumMetal] = {0, 0, 0, 0, 0, 0};  // incl. the 1 / count normalisation
  float metalE_ = 0, accE_ = 0, metalLvl_ = 0, metalAcc_ = 0, bpGain_ = 0;
  bool metalOn_ = false, bpOn_ = false, bp2On_ = false, hpOn_ = false;
  Flt bp1_, bp2_, hp_, nf_;
  float noiseE_ = 0, noiseLvl_ = 0, noiseGain_ = 1, tail_ = 1;
  bool noiseFlt_ = false;
  uint32_t rng_ = 0x9E3779B9u;
  uint8_t burstsLeft_ = 0;
  int32_t burstT_ = 0, burstLen_ = 0;      // samples
  float drive_ = 0, driveGain_ = 1;
  K toneK_, pitchK_, clickK_, metalK_, noiseK_;
  float burstK_ = 1;                       // decay within one burst
  float noiseKNow_ = 1;                    // burstK_ or the tail's, whichever phase the noise is in
  float last_ = 0, tailV_ = 0;             // last output (amp included); choke tail
  bool fresh_ = false;                     // triggered, first control() not yet run
};

// The noise seeds of the next triggers back to the boot ones (Synth::reset: a render repeats exactly).
void drumResetNoise();

}  // namespace mt
