#pragma once
#include <stdint.h>
#include "model.h"
#include "synth_fm.h"

namespace mt {

// Sound of a machine (FmMachine, out of range = Kick). mac: DECAY..CONTOUR, 0..127, fractional
// after the LFO. pitch: MIDI note incl. transpose, fine, bend, ARP, VIB; C4 plays the machine's base
// pitch. Pure: called at control rate.
void fmMachine(uint8_t machine, const float mac[kFmMacros], float pitch, FmParams& out);
// CHORD type of a SHAPE value, e.g. "MAJ".
const char* fmChordName(uint8_t shape);
constexpr int kFmChords = 12;

}  // namespace mt
