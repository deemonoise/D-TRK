#include "synth_acid.h"
#include "fastmath.h"
#include "synth_osc.h"
#include "synth_syn_calibration.h"

namespace mt {

void AcidAccent::note(uint8_t velocity, bool legato) {
  if (!armed_ || !legato) {
    envelope_ = accent_ = attackAge_ = 0;
    decaying_ = false;
  }
  armed_ = true;
  accented_ = velocity >= 110;  // 100 is a regular note at the track's default velocity
}

MT_CONTROL void AcidAccent::advance(uint32_t samples, uint8_t attack, uint8_t decay) {
  if (!armed_) return;
  if (attack != attack_) {
    attack_ = attack;
    attackSamples_ = synModAttackMs(attack) * (kSynthRate / 1000.f);
    invAttack_ = attackSamples_ > 0.f ? fastRcp(attackSamples_) : 0.f;
  }
  if (decay != decay_) {
    decay_ = decay;
    const float tau = synDecayTauMs(decay) * (kSynthRate / 1000.f);
    invTau_ = tau > 0.f ? fastRcp(tau) : 0.f;
  }
  float span = static_cast<float>(samples);
  if (!decaying_) {
    const float left = attackSamples_ > attackAge_ ? attackSamples_ - attackAge_ : 0.f;
    if (span < left) {
      attackAge_ += span;
      envelope_ = attackAge_ * invAttack_;
    } else {
      span -= left;
      envelope_ = 1.f;
      decaying_ = true;
    }
  }
  if (decaying_ && invTau_ > 0.f && envelope_ > 0.f) {
    const float decayRate = accented_ ? 2.f * invTau_ : invTau_;
    envelope_ *= fastExp(-span * decayRate);
    if (envelope_ < 1e-7f) envelope_ = 0.f;
  }
  if (samples && (accented_ || accent_ > 0.f)) {
    // An RC-like smoothed copy of the filter envelope swells on accented hits.
    // Legato preserves both levels when a new velocity changes the decay and input.
    constexpr float kAccentTau = 8.f * (kSynthRate / 1000.f);
    const float target = accented_ ? envelope_ : 0.f;
    accent_ += (target - accent_) * (1.f - fastExp(-(samples * (1.f / kAccentTau))));
    if (accent_ < 1e-7f) accent_ = 0.f;
  }
}

}  // namespace mt
