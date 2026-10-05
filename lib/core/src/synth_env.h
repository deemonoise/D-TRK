#pragma once
#include <stdint.h>
#include "synth_osc.h"

namespace mt {

// Linear ADSR, advanced once per sample. Attack runs at a fixed rate (full scale in aMs),
// so a retrigger from a non-zero level is shorter and has no click.
class Env {
 public:
  enum class Stage : uint8_t { Idle, Attack, Decay, Sustain, Release };

  void set(uint16_t aMs, uint16_t dMs, float sustain, uint16_t rMs) {
    sus_ = sustain < 0.f ? 0.f : (sustain > 1.f ? 1.f : sustain);
    aStep_ = aMs ? 1.f / samples(aMs) : 1.f;
    dStep_ = dMs ? (1.f - sus_) / samples(dMs) : 1.f;
    rLen_ = samples(rMs);
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
        rStep_ = level_ / rLen_;
      }
    }
  }
  void kill() {
    stage_ = Stage::Idle;
    level_ = 0;
  }
  float next() {
    switch (stage_) {
      case Stage::Attack:
        level_ += aStep_;
        if (level_ >= 1.f) {
          level_ = 1.f;
          stage_ = Stage::Decay;
        }
        break;
      case Stage::Decay:
        level_ -= dStep_;
        if (level_ <= sus_) {
          level_ = sus_;
          stage_ = Stage::Sustain;
          if (sus_ <= 0.f) kill();  // nothing left to hold: free the voice
        }
        break;
      case Stage::Release:
        level_ -= rStep_;
        if (level_ <= 0.f) kill();
        break;
      default:
        break;
    }
    return level_;
  }
  bool idle() const { return stage_ == Stage::Idle; }
  Stage stage() const { return stage_; }
  float level() const { return level_; }

 private:
  static float samples(uint16_t ms) { return ms * (kSynthRate / 1000.f); }

  Stage stage_ = Stage::Idle;
  float level_ = 0;
  float sus_ = 1;
  float aStep_ = 1, dStep_ = 1, rStep_ = 1;
  float rLen_ = 0;
};

}  // namespace mt
