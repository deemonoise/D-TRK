#pragma once
#include "synth_syn.h"
#include "fastmath.h"

namespace mt {
// Shared by the audio engine and the local C4 waveform model. Pitch is added before
// noteHz/cachedHz so the audio engine can retain its frequency cache.
MT_INLINE float synEnvPitch2(uint8_t dest, float amount) {
  return dest == static_cast<uint8_t>(SynEnvDest::Pitch2) ? 24.f * amount : 0.f;
}
MT_INLINE void synApplyEnv(SynParams& p, uint8_t dest, float amount) {
  if (amount == 0.f) return;
  switch (static_cast<SynEnvDest>(dest)) {
    case SynEnvDest::Fold: p.fold = fclamp(p.fold + amount, 0.f, 1.f); break;
    case SynEnvDest::Ring: p.ring = fclamp(p.ring + amount, 0.f, 1.f); break;
    case SynEnvDest::Mix: p.mix = fclamp(p.mix + amount, 0.f, 1.f); break;
    case SynEnvDest::Pitch2: break;
    default:
      p.shape[0] = fclamp(p.shape[0] + amount, 0.f, 1.f);
      p.shape[1] = fclamp(p.shape[1] + amount, 0.f, 1.f);
      break;
  }
}
}  // namespace mt
