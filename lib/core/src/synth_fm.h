#pragma once
#include <stdint.h>
#include "synth_filter.h"

namespace mt {

constexpr int kFmOps = 8;

// A sparse, acyclic PM graph: a higher-index operator may modulate a lower one.
// Times are milliseconds to -60 dB; zero means no decay. Non-output levels are radians.
struct FmOp {
  float ratio = 1, level = 0, sustain = 0, decayMs = 0, fb = 0;
  float wave = 0;  // 0..3: sine/triangle/saw/square morph; 4..5: measured CHORD harmonic morph
  int8_t mod = -1;
  bool output = true;
};

// Measured-reference model parameters. These are candidate implementations, not Elektron internals.
struct FmParams {
  FmOp op[kFmOps];
  float hz = 0, pitchEnv = 0, pitchMs = 0;
  float ampMs = 0, ampSustain = 0, attackMs = 0.36f;
  float noise = 0, noiseMs = 0, noiseSustain = 0;
  uint8_t bursts = 0;
  float burstMs = 0, tail = 1;
  Svf::Mode filterMode = Svf::Mode::Lp;
  float filterHz = 0, filterQ = 0.707f;
  bool filterAll = false, oneShot = true;
  float noise2 = 0, noise2Ms = 0, noise2Sustain = 0;
  float noise2FilterHz = 0, noise2FilterQ = 0.707f;
  Svf::Mode noise2FilterMode = Svf::Mode::Lp;
  float sat = 0, outputGain = 1, postGain = 1, overComp = 0;  // postGain is after OVER, preserving its operating point
  bool resetPhase = true;
};

// Mapper entry points are separate so tonal and percussion calibration can be reviewed independently.
bool fmTonalMachine(uint8_t machine, const float raw[8], float note, FmParams& out);
bool fmPercussionMachine(uint8_t machine, const float raw[8], float note, FmParams& out);

// Render/control contract retained by Synth. No allocation, I/O or per-sample exp/pow.
class FmVoice {
 public:
  void trigger(bool keepPhase);
  void control(const FmParams& p, int span);
  // out must not overlap this voice state (all Synth callers use separate mix buffers).
  void render(float* __restrict out, int n, float gain);
  bool done() const { return oneShot_ && t_ > 32 && amp_ < 1e-4f && ampTarget_ < 1e-4f; }

 private:
  struct OpState {
    uint32_t phase = 0;
    float inc = 0, dInc = 0, level = 0, dLevel = 0, fb = 0, dFb = 0, wave = 0, dWave = 0, last = 0;
    int8_t mod = -1;
    bool output = true, active = false;
  };
  void additive(float* __restrict tonal, int n, int ramp);
  void pair(float* __restrict tonal, int n, int ramp);
  OpState op_[kFmOps];
  Svf filter_, noise2Filter_;
  uint32_t t_ = 0, rng_ = 0x12345678;
  int left_ = 0;
  float amp_ = 0, dAmp_ = 0, ampTarget_ = 0;
  float noise_ = 0, dNoise_ = 0, noise2_ = 0, dNoise2_ = 0;
  float sat_ = 0, dSat_ = 0, outputGain_ = 1, dOutputGain_ = 0;
  float attackSamples_ = 16, postGain_ = 1, dPostGain_ = 0;
  float lastOut_ = 0, fadeFrom_ = 0;
  uint8_t fadeLeft_ = 0;
  uint8_t bursts_ = 0;
  float burstSamples_ = 0, tail_ = 1;
  bool oneShot_ = true, filterOn_ = false, filterAll_ = false, noise2FilterOn_ = false;
  bool fresh_ = true, keepPhase_ = false, additive_ = true, paired_ = false;
};

}  // namespace mt
