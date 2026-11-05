#pragma once
#include <stdint.h>
#include "hot.h"

namespace mt {
// Selected Virus TI Snow Init measurements, not a complete Virus emulation.
// LP cutoff/Q were identified from dry saw spectra; ADSR from dry sine recordings.
// Values above the measured range (cutoff 92, resonance 96, time 96) extrapolate.
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
