#pragma once
#include <stdint.h>
#include "model.h"
#include "synth_fm.h"
namespace mt {
// Independent raw A-H controls of the ten measured-reference machines. The mapper is pure.
// Non-finite inputs become neutral; unknown machine ids use SY TONE.
void fmMachine(uint8_t machine, const float raw[kFmParams], float pitch, FmParams& out);
// Mixer trim is separate from reference-render calibration and the OVER operating point.
float fmMixGain(uint8_t machine, float spacingRaw = 64.f);
}
