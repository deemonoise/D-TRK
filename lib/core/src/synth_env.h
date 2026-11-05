#pragma once
#include <stdint.h>
#include "hot.h"
#include "synth_osc.h"

namespace mt {

// ADSR advanced once per sample. Linear by default; SYNTH uses exponential decay/release.
// Attack runs at a fixed rate (full scale in aMs), so retriggering stays continuous.
class Env {
 public:
  enum class Stage : uint8_t { Idle, Attack, Decay, Sustain, Release };

  // Exponential mode uses dMs / rMs as time constants; attack remains linear.
  void set(uint16_t aMs, uint16_t dMs, float sustain, uint16_t rMs, bool exponential = false) {
    expDecay_ = expRelease_ = exponential;
    sus_ = sustain < 0.f ? 0.f : (sustain > 1.f ? 1.f : sustain);
    aStep_ = aMs ? 1.f / samples(aMs) : 1.f;
    dStep_ = dMs ? (exponential ? coefficient(dMs) : (1.f - sus_) / samples(dMs)) : 1.f;
    rLen_ = rMs ? (exponential ? coefficient(rMs) : samples(rMs)) : 0.f;
  }
  // on: from the current level to Attack (no click); off: Release.
  void gate(bool on) {
    if (on) {
      stage_ = Stage::Attack;
    } else if (stage_ != Stage::Idle) {
      if (rLen_ == 0 || level_ <= 0.f) {
        kill();
      } else {
        stage_ = Stage::Release;
        rStep_ = expRelease_ ? rLen_ : level_ / rLen_;
      }
    }
  }
  // Release over ms, whatever the release time (choke).
  void fade(uint16_t ms) {
    expRelease_ = false;  // stop/choke/voice stealing must keep its short linear fade
    rLen_ = samples(ms);
    gate(false);
  }
  void kill() {
    stage_ = Stage::Idle;
    level_ = 0;
  }
  MT_INLINE float next() {
    switch (stage_) {
      case Stage::Attack:
        level_ += aStep_;
        if (level_ >= 1.f) {
          level_ = 1.f;
          stage_ = Stage::Decay;
        }
        break;
      case Stage::Decay: {
        const float before = level_;
        level_ -= expDecay_ ? (level_ - sus_) * dStep_ : dStep_;
        if (level_ <= sus_ || (expDecay_ && (level_ - sus_ <= 1e-5f || level_ == before))) {
          level_ = sus_;
          stage_ = Stage::Sustain;
          if (sus_ <= 0.f) kill();  // nothing left to hold: free the voice
        }
        break;
      }
      case Stage::Release:
        level_ -= expRelease_ ? level_ * rStep_ : rStep_;
        if (level_ <= (expRelease_ ? 1e-5f : 0.f)) kill();
        break;
      default:
        break;
    }
    return level_;
  }
  // A flat envelope at l (Sustain: next() returns l, never ends). CHIP STEP's held chunk level.
  void hold(float l) {
    stage_ = Stage::Sustain;
    level_ = l;
  }
  bool idle() const { return stage_ == Stage::Idle; }
  Stage stage() const { return stage_; }
  float level() const { return level_; }

 private:
  static float samples(uint16_t ms) { return ms * (kSynthRate / 1000.f); }
  static float coefficient(uint16_t ms) {
    const float x = 1.f / samples(ms);  // callers guarantee >= 1 ms
    return x * (1.f - .5f * x);  // 1-exp(-x), relative error < .009% at >=1 ms
  }

  Stage stage_ = Stage::Idle;
  bool expDecay_ = false, expRelease_ = false;  // occupy existing alignment padding
  float level_ = 0;
  float sus_ = 1;
  float aStep_ = 1, dStep_ = 1, rStep_ = 1;
  float rLen_ = 0;
};
static_assert(sizeof(Env) == 28, "SYNTH envelope curves must not grow the voice pool");

}  // namespace mt
