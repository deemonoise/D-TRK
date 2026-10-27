#pragma once
#include <stdint.h>
#include "synth_filter.h"

namespace mt {

constexpr int kDrumTones = 3;
constexpr int kDrumMetal = 6;
constexpr int kDrumBursts = 4;
constexpr float kAccentMs = 15;

// One DRUM voice's sound, made by a machine (synth_drum_machines) at control rate. Times are to
// -60 dB, ms (6.9 x tau); 0 = no decay. Every part has its own exponential envelope from the
// trigger. Holds, delays and steps count whole control blocks (1 ms at 32 kHz). New parts default to off.
struct DrumParams {
  // Tones: sines, hz 0 = off. Each is level x its own decay (toneDecMs, 0 = none); tone 0 shaped
  // (h3, h5), all summed, then x the body envelope: slowLvl x [holdMs, then toneMs] + fastLvl x [fastHoldMs,
  // then fastMs]. The tones, their envelopes and holds start toneDelayMs after the trigger.
  float toneHz[kDrumTones] = {0, 0, 0};
  float toneLvl[kDrumTones] = {0, 0, 0};
  float toneDecMs[kDrumTones] = {0, 0, 0};
  float toneMs = 0;
  float slowLvl = 1, holdMs = 0;
  float fastLvl = 0, fastHoldMs = 0, fastMs = 0;
  float toneDelayMs = 0;
  // Odd waveshaper on tone 0 (the others pass clean: no intermodulation): 3rd / 5th harmonic of a unit sine at these levels (Chebyshev:
  // the fundamental stays; input clamped to +-1). Negative = inverted phase. 0 = off.
  float h3 = 0, h5 = 0;
  // Pitch of every tone: hz x 2^(pitchEnv x e / 12) x (1 + sweepA x eA + sweepB x eB). pitchEnv:
  // semitones at the trigger; sweepA / B: extra hz at the trigger as a fraction of the tone's hz
  // (fast and slow exponentials, 808 / 909 kicks), each decaying to 0 in its own time.
  float pitchEnv = 0, pitchMs = 0;
  float sweepA = 0, sweepAMs = 0, sweepB = 0, sweepBMs = 0;
  float click = 0, clickMs = 2;          // impulse + noise burst at the trigger
  float clickStep = 0, clickStepMs = 0;  // DC step from the trigger, this long (909 kick)
  float metalHz[kDrumMetal] = {0, 0, 0, 0, 0, 0};  // squares, 0 = off
  // Per-square weight; the sum is divided by the count of nonzero weights, so a square that drops
  // out (hz 0 above the Nyquist limit) doesn't change the others' level. Unused squares: weight 0.
  float metalW[kDrumMetal] = {1, 1, 1, 1, 1, 1};
  float metalLvl = 0, metalMs = 0;
  float metalAccent = 0;              // extra level decaying in metalAccMs (cowbell attack)
  float metalAccMs = kAccentMs;
  float metalBp1 = 0, metalBp2 = 0;   // band-pass centres, unity peak, summed; bp1 0 = skip both,
                                      // bp2 0 = bp1 only
  float metalQ = 1.5f;
  float metalQ2 = 0;                  // bp2's q, 0 = metalQ
  // bp2 as a band of its own: metal2Lvl x its own decay metal2Ms (cymbal) instead of bp1's
  // envelope. metal2Lvl 0 = off: bp2 follows metalLvl / metalMs / accent.
  float metal2Lvl = 0, metal2Ms = 0;
  // Metal plateau and speed-up (hats): bp1's envelope and bp2's own (not the accent) hold
  // metalHoldMs, then decay; metalAccelMs / metal2AccelMs: bp1's / bp2's own decay rate then grows
  // by its start rate every that many ms (as noiseAccMs > 0). 0 = off.
  float metalHoldMs = 0, metalAccelMs = 0, metal2AccelMs = 0;
  // Cut: from cutMs after the trigger the metal (both bands and the accent) and the noise all fall
  // to -60 dB in cutFallMs, unless their own decay is faster (the 808 hats' and cymbal's end). 0 = off.
  float cutMs = 0, cutFallMs = 0;
  float metalHp = 0;                  // high-pass after bp1 (bp2 unfiltered; the squares without bp1), 0 = skip
  float noiseLvl = 0, noiseMs = 0;
  // Noise plateau before its decay; noiseAccMs: the decay rate grows by its start rate every
  // noiseAccMs after the plateau (a fall that speeds up, 909 snare), 0 = plain exponential;
  // negative: the rate falls instead, start rate / (1 + time / |noiseAccMs|), down to
  // 1 / 8 of it (a fall that slows, clap tail).
  float noiseHoldMs = 0, noiseAccMs = 0;
  // Noise swell: its level x (1 - noiseRise x e^(-t / tau)), tau noiseRiseMs, stepped per control
  // block (909 crash). 0 = off.
  float noiseRise = 0, noiseRiseMs = 0;
  Svf::Mode noiseMode = Svf::Mode::Hp;
  float noiseHz = 0, noiseQ = 0.707f; // 0 Hz = unfiltered; Bp: same power at any hz, q
  float noiseHpHz = 0;                // one-pole high-pass after that (a gentle tilt), 0 = off
  float noiseWide = 0;                // unfiltered white noise x noiseLvl alongside, 0 = off (clap)
  uint8_t bursts = 0;                 // short noise hits before the tail (clap), up to kDrumBursts,
                                      // latched at the trigger
  float burstMs = 0;                  // length of one hit, 0 = no hits
  float burstLenMs[kDrumBursts] = {0, 0, 0, 0};  // per hit, 0 = burstMs (uneven spacing, 909 clap)
  float burstTauMs = 0;               // decay within a hit (tau), 0 = burstMs / 4
  float tailQ = 0;                    // the noise filter's q after the hits, 0 = noiseQ
  float tail = 1;                     // noise level after the bursts
  float drive = 0;                    // 0..1, gain 1..8 into the output soft clip (always on, flat at +-1)
};

// Runtime of one DRUM voice. set() takes a machine's params (after a trigger and whenever they
// change: the voice keeps only what it derives from them); control() advances the block state and
// ramps the tone pitch linearly to its value span samples later; render() adds amp x sound to out.
class DrumVoice {
 public:
  // keepTail: choke — the voice's last output fades out over ~2 ms under the new hit.
  void trigger(bool keepTail);
  void set(const DrumParams& p);
  void control(int span);
  void control(const DrumParams& p, int span) {
    set(p);
    control(span);
  }
  void render(float* __restrict out, int n, float amp);
  // Every part below -80 dB (after drive) and no bursts left (also before the first trigger).
  bool done() const;

 private:
  void noiseFilter();  // nf_ and noiseGain_ for the hits' q or the tail's
  static constexpr int kSeg = 32;  // render()'s passes, samples
  // The metal part for n <= kSeg samples (m x g), its envelopes advanced.
  void metal(float* __restrict m, int n);
  // s = dc + the tones (on), then s += click, metal m, noise: each for n <= kSeg samples.
  void tones(float* __restrict s, int n, bool on);
  void noise(float* __restrict s, const float* __restrict m, int n, bool clickOn, bool metalOn, bool noiseOn);

  // From set(): per-sample decay multipliers (1 = none), lengths in samples.
  float toneHz_[kDrumTones] = {0, 0, 0};      // base pitch, clamped; 0 = off
  float toneLvl_[kDrumTones] = {0, 0, 0};
  float toneDKSet_[kDrumTones] = {1, 1, 1};
  float toneK_ = 1, fastK_ = 1, pitchK_ = 1, sweepAKL_ = 0, sweepBKL_ = 0;  // sweeps: see pitchKL_
  float slowLvl_ = 1, fastLvl_ = 0, h3_ = 0, h5_ = 0;
  float pitchEnv_ = 0, sweepA_ = 0, sweepB_ = 0;
  uint32_t delay_ = 0, holdN_ = 0, fastHoldN_ = 0, stepN_ = 0, noiseHoldN_ = 0;
  float clickStep_ = 0, click_ = 0, clickK_ = 1;
  float metalK_ = 1, accK_ = 1, metal2K_ = 1, metalLvl_ = 0, metalAcc_ = 0, metal2Lvl_ = 0;
  float bpGain_ = 0, bp2Gain_ = 0;
  uint32_t metalHoldN_ = 0, cutN_ = 0;         // cutN_ 0 = no cut
  float metalAccInv_ = 0, metal2AccInv_ = 0;   // 1 / samples of metal(2)AccelMs, 0 = off
  float cutK_ = 1;
  bool metalOn_ = false, bpOn_ = false, bp2On_ = false, hpOn_ = false, metal2On_ = false;
  bool noiseFlt_ = false, tone2_ = false;     // tone2_: the third tone sounds
  uint8_t bursts_ = 0;                         // latched by the first control() after a trigger
  float burstMs_ = 0, burstTauMs_ = 0, noiseWide_ = 0;
  float burstHitMs_[kDrumBursts] = {0, 0, 0, 0};
  float noiseHz_ = 0, noiseQ_ = 0.707f, tailQ_ = 0;
  Svf::Mode noiseMode_ = Svf::Mode::Hp;
  bool tailFlt_ = false;                       // nf_ at the tail's q (after the hits)
  float noiseK_ = 1, noiseAccInv_ = 0;         // 1 / samples of noiseAccMs, 0 = off
  float noiseLvl_ = 0, noiseGain_ = 1, tail_ = 1;
  float noiseRise_ = 0, noiseRiseInv_ = 0;     // depth, 1 / samples of the swell's tau (0 = off)
  float noiseRiseNow_ = 1;                     // this block's level factor
  float noiseHpC_ = 0;                         // one-pole coefficient, 0 = off
  float driveGain_ = 1;
  uint32_t metalInc_[kDrumMetal] = {0, 0, 0, 0, 0, 0};
  // The squares by weight: sqOsc_[0..nMetal_) the nonzero ones in order, the rest silent (their
  // phases only run on). sqW_ incl. the 1 / count normalisation.
  float sqW_[kDrumMetal] = {0, 0, 0, 0, 0, 0};
  uint8_t sqOsc_[kDrumMetal] = {0, 1, 2, 3, 4, 5};
  uint8_t nMetal_ = 0;
  Svf bp1_, bp2_, hp_, nf_;

  // State.
  uint32_t tonePh_[kDrumTones] = {0, 0, 0};
  uint32_t toneInc_[kDrumTones] = {0, 0, 0};  // phase units (2^32 = cycle) per sample
  int32_t toneDInc_[kDrumTones] = {0, 0, 0};
  int32_t rampLeft_ = 0;                      // samples until the pitch ramp reaches its target
  float toneDE_[kDrumTones] = {0, 0, 0};      // per-tone decays
  float toneDK_[kDrumTones] = {1, 1, 1};      // their multipliers this block (1 while delayed)
  float toneE_ = 0, fastE_ = 0, pitchE_ = 0;  // body slow / fast, semitone pitch envelope, 0..1
  float toneKNow_ = 1, fastKNow_ = 1, pitchKNow_ = 1;  // this block's (1 while held / delayed)
  float sweepAE_ = 0, sweepBE_ = 0;           // advanced at control rate (span samples a block)
  float clickE_ = 0, clickDc_ = 0;
  bool impulse_ = false;
  bool fresh_ = false;                        // triggered, first control() not yet run
  uint8_t burstsLeft_ = 0;
  uint32_t metalPh_[kDrumMetal] = {0, 0, 0, 0, 0, 0};
  float metalE_ = 0, accE_ = 0, metal2E_ = 0;
  float metalKNow_ = 1, accKNow_ = 1, metal2KNow_ = 1;  // this block's (hold, speed-up, cut)
  float noiseE_ = 0;
  float noiseHpZ_ = 0;                        // the high-pass's low-pass state
  uint32_t rng_ = 0x9E3779B9u;
  int32_t burstT_ = 0;                        // samples left in this hit
  int32_t burstNext_[kDrumBursts] = {0, 0, 0, 0};  // length of the next hit by the hits left, samples
  uint32_t age_ = 0;                          // samples rendered since the trigger
  float burstK_ = 1;                          // decay within one burst
  float noiseKNow_ = 1;                       // burstK_ or the tail's, whichever phase the noise is in
  float noiseKTail_ = 1;                      // the tail's this block (1 on the plateau)
  float last_ = 0, tailV_ = 0;                // last output (amp included); choke tail

  // From set(), last (the hot fields' offsets stay short). fastLog2 of the decays control() raises
  // to a power: fastPow(k, y) = fastExp2(y x that), bit for bit.
  float pitchKL_ = 0, noiseKL_ = 0, metalKL_ = 0, metal2KL_ = 0;
  float driveOff_ = 1e-4f * fastRcp(1.f);     // done()'s threshold: -80 dB / driveGain_
};

// The noise seeds of the next triggers back to the boot ones (Synth::reset: a render repeats exactly).
void drumResetNoise();

}  // namespace mt
