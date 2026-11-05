#pragma once
#include <stdint.h>
#include "hot.h"

namespace mt {
// Selected Virus TI Snow Init measurements, not a complete Virus emulation.
// LP cutoff/Q were identified from dry saw spectra; ADSR from dry sine recordings.
// Cutoff validated through104, resonance through127; time values above96 extrapolate.
// One original HyperSaw group fitted from isolated live oscillator recordings.
MT_INLINE float synHyperDensity(float normalized) {
  const float x = normalized < 0.f ? 0.f : (normalized > 1.f ? 1.f : normalized);
  return x <= .5f ? 1.f + 4.f * x : x <= .75f ? 8.f * x - 1.f : 16.f * x - 7.f;
}
MT_CONTROL float synHyperSpread(float normalized);  // maximum nine-saw spread, cents either side
MT_CONTROL uint16_t synModAttackMs(uint8_t raw);
MT_CONTROL float synModEnv(uint32_t samples, uint8_t attack, uint8_t decay);
MT_CONTROL float synFenvOctaves(float amount);  // bipolar tracker depth; measured positive Virus scale
MT_CONTROL float synAnalogOctaves(float raw, int poles);
MT_CONTROL float synLpOctaves(float raw);  // octaves above 20 Hz
MT_CONTROL float synLpQ(float raw);
MT_CONTROL float synLpGain(float raw);     // measured resonance level compensation
MT_CONTROL uint16_t synAttackMs(uint8_t raw);
MT_CONTROL uint16_t synDecayTauMs(uint8_t raw);  // decay / release exponential time constant
// Musical time shown in ENV: exponential transition from 90% to 10% of its range.
inline uint16_t synDecaySpanMs(uint8_t raw) {
  return static_cast<uint16_t>(synDecayTauMs(raw) * 2.19722458f + .5f);
}
}
