#pragma once
#include <stdint.h>
#include "synth_fxutil.h"

namespace mt {

// MOD send bus: one selectable modulation effect, mono in (the bus) -> stereo return added to the
// mix. CHORUS, ENSEMBLE and FLANGER share one int16 line (kLineLen samples = 32 ms, 2 KB, a member:
// internal RAM inside Synth); PHASER is 6 first-order allpasses per side. LFOs and coefficients are
// computed once per kFxSeg samples and ramped in between.
//   CHORUS   2 taps (L, R), 12 ms +- depth x 6 ms, LFOs 90 deg apart.
//   ENSEMBLE 3 taps 120 deg apart (L, C, R), 10 ms +- depth x 5 ms slow (rate) + 0.75 ms at 6.3 Hz.
//   FLANGER  1 tap per side, 0.3 .. 0.3 + depth x 7.7 ms, R 90 deg off, feedback fb x 0.9.
//   PHASER   notch sweep 200 Hz .. 4 kHz (exp, depth = range around ~900 Hz), R 90 deg off,
//            feedback fb x 0.85. The dry track in the mix makes the notches.
// Bypass: type OFF, level 0 or idle (silent bus and tails below -100 dB) -> mix untouched,
// bit-exact. Level ramps over the block; depth and fb are smoothed per segment; a type change fades
// the old type out (64 samples), clears the line and states, and fades the new one in.
enum class ModFxType : uint8_t { Off, Chorus, Ensemble, Flanger, Phaser, Count };

class ModFx {
 public:
  static constexpr int kLineLen = 1024;
  static constexpr float kScale = 8192.f;   // line units per 1.0
  static constexpr int kFade = 64;          // type change fade, samples
  static constexpr int kQuietIdle = 2048;   // > the line: silent this long -> idle
  static constexpr int kStages = 6;

  ModFx() { reset(); }
  void reset();
  // The delay line (kLineLen int16, owned by the caller: the Synth's reverb block), nullptr = the
  // line effects (CHORUS, ENSEMBLE, FLANGER) silent; the PHASER needs none. Resets the effect.
  void setBuffer(int16_t* line) {
    line_ = line;
    reset();
  }
  // type: ModFxType; rate, depth, fb, level: 0..127 (Project::mod*). rate 0..127 -> 0.05..8 Hz.
  void process(const float* in, float* mixL, float* mixR, int n, uint8_t type, uint8_t rate, uint8_t depth,
               uint8_t fb, uint8_t level);
  bool idle() const { return idle_; }
  ModFxType type() const { return static_cast<ModFxType>(type_); }

 private:
  void clearState();
  // Returns the peak of the return before level / fade (idle test).
  float segment(const float* in, float* mixL, float* mixR, int k, float lv, float dLv);
  int targets(int k, float* t);

  int16_t* line_ = nullptr;
  uint32_t w_ = 0;
  FxLfo lfo_, fast_;
  float hz_ = 0;
  uint8_t rate_ = 0xFF;
  float off_[3] = {};     // tap offsets at the segment start (samples)
  float apA_[2] = {};     // phaser coefficients at the segment start
  float ap_[2][kStages] = {};
  float apY_[2] = {};     // phaser last outputs (feedback)
  float flY_ = 0;         // flanger last taps' mean (feedback)
  float depth_ = 0, fb_ = 0;  // smoothed 0..1
  float lv_ = 0;          // current level
  float xf_ = 1;          // type crossfade gain
  bool first_ = true;     // next segment: offsets start at their targets
  bool idle_ = true;
  int quiet_ = 0;
  uint8_t type_ = 0, want_ = 0;
};

}  // namespace mt
