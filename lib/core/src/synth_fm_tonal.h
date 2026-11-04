#pragma once
#include <stdint.h>
namespace mt {
// Read the same calibrated/candidate tables as the tonal mapper, without duplicating them in UI.
float fmToneRatio(float raw);
// Base TYPE signature in semitones; BAL applies its voicing changes separately. out may be null.
int fmChordSignature(uint8_t rawType, float out[4]);
}
