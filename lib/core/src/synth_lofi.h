#pragma once
#include <stdint.h>
#include "bytebeat_vm.h"
#include "model.h"

namespace mt {

// Control-rate inputs of a LOFI voice (Synth::controlLofi).
struct LofiParams {
  uint8_t machine = 0;              // LofiMachine
  int16_t formula = -1;             // BYTE: library index code was taken from (Synth's cache), -1 none
  const ByteCode* code = nullptr;   // BYTE formula, nullptr = silent
  float hz = 0;                     // note frequency (BYTE: C-4 = t at 8 kHz)
  float mac[kFmMacros] = {0, 0, 0, 0, 0};  // 0..1. BYTE: A B C TONE LOOP; APC: WIDTH TONE DRIFT SMOOTH -
};

// Bytebeat t rate per Hz of the note: middle C (noteHz(60)) runs t at 8 kHz.
constexpr float kByteRatePerHz = 8000.f / 261.625565f;

// LOOP macro v (0..127): 0 = off; 1..127 = t & (2^k - 1), k = 10 .. 20.
constexpr int lofiLoopBits(int v) { return v > 0 ? 10 + ((v - 1) * 10 + 63) / 126 : 0; }
// LOFI BYTE SYNC (1024 t per 1/16 step): the loop length in steps, 1 .. 1024; 0 = off.
constexpr int lofiLoopSteps(int v) { return v > 0 ? 1 << (lofiLoopBits(v) - 10) : 0; }

// One LOFI voice. BYTE: a bytebeat formula over a counter t; APC: the Atari Punk Console (an
// oscillator triggering a one-shot pulse). Both end in TONE (one-pole LP) and a DC blocker.
// control() takes the params for the next n samples; render() adds amp x sound to out.
class LofiVoice {
 public:
  // Note-on: t and the APC oscillator from zero, the drift walk from seed. BYTE: the first
  // formula value primes TONE and the DC blocker, so the note starts without a step: from silence,
  // or (sounding: a retrigger of a playing voice) from the current output. APC filters keep their
  // state.
  void trigger(uint32_t seed, bool sounding = false);
  void control(const LofiParams& p, int n);
  void render(float* out, int n, float amp);
  uint32_t tick() const { return t_; }        // BYTE: the counter t
  uint32_t lastTick() const { return last_; } // BYTE: the last evaluated t (after LOOP)
  uint32_t loopMask() const { return mask_; }
  uint32_t evals() const { return evals_; }   // BYTE: formula evaluations since the trigger

 private:
  void renderByte(float* out, int n, float amp);
  void renderApc(float* out, int n, float amp);
  // Step h at distance x (0..1 samples) before the current sample: PolyBLEP corrections for the
  // previous (bPrev_) and the current sample (bCorr_), as BlOsc.
  void blep(float h, float x) {
    bPrev_ += 0.5f * h * x * x;
    bCorr_ += 0.5f * h * (2.f * x - x * x - 1.f);
  }

  uint8_t machine_ = 0;
  float g_ = 1, lp_ = 0;        // TONE: one-pole LP coefficient (1 = open) and state
  float dcx_ = 0, dcy_ = 0;     // DC blocker state
  bool prime_ = false;          // BYTE: the next formula value primes TONE / the DC blocker
  // BYTE.
  const ByteCode* code_ = nullptr;
  uint32_t t_ = 0, frac_ = 0;   // 32.32 counter
  uint32_t incI_ = 0, incF_ = 0;
  uint32_t mask_ = 0xFFFFFFFFu; // LOOP
  uint32_t last_ = 0xFFFFFFFFu; // evaluated t, ~0 = none yet
  uint32_t evals_ = 0;
  int a_ = 0, b_ = 0, c_ = 0;
  float prev_ = 0, cur_ = 0;    // the last two formula values
  bool interp_ = false;         // TONE > 0: linear interpolation between them
  // APC.
  float ph_ = 1, inc_ = 0;      // oscillator phase 0..1, per sample (1 = an edge on the next sample)
  float q_ = 0, qInc_ = 0;      // one-shot progress 0..1, per sample
  bool on_ = false;             // the pulse runs
  float smooth_ = 0;            // PolyBLEP amount
  float bPrev_ = 0, bCorr_ = 0; // one sample late output and the correction due for the next
  // DRIFT: a smoothed random walk, -1..1, x 50 cents x the macro.
  uint32_t rng_ = 1;
  float dTarget_ = 0, dVal_ = 0;
  int32_t dLeft_ = 0;           // samples until the next target
};

}  // namespace mt
