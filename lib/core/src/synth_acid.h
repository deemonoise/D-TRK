#pragma once
#include <stdint.h>

namespace mt {

// 303-style accent on the common SYNTH voice. The tracker owns pitch and legato;
// the accent changes the filter envelope rate and lifts cutoff and loudness.
class AcidAccent {
 public:
  bool armed() const { return armed_; }
  void reset() { *this = AcidAccent{}; }
  void note(uint8_t velocity, bool legato);
  void advance(uint32_t samples, uint8_t attack, uint8_t decay);
  float envelope() const { return envelope_; }
  float cutoffOctaves() const { return accent_ * 1.25f; }
  float gain() const { return 1.f + accent_ * .28f; }

 private:
  float envelope_ = 0, accent_ = 0;
  float attackAge_ = 0, attackSamples_ = 0, invAttack_ = 0, invTau_ = 0;
  uint8_t attack_ = 0, decay_ = 0;
  bool armed_ = false, accented_ = false, decaying_ = false;
};

}  // namespace mt
