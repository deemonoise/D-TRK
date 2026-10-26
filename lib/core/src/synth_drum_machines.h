#pragma once
#include <stdint.h>
#include "model.h"
#include "synth_drum.h"

namespace mt {

// Sound of a DRUM machine (DrumMachine, out of range = BD8). mac: DECAY..CONTOUR 0..127, fractional
// after the LFO. pitch: MIDI note incl. transpose, fine, bend, ARP, VIB; C4 plays the machine's base
// pitch (tones and metal; noise filters stay put). Pure: called at control rate.
void drumMachine(uint8_t machine, const float mac[kFmMacros], float pitch, DrumParams& out);
const char* drumMachineName(uint8_t machine);  // "BD8"
// Macro label of the machine, e.g. "SNAPPY"; "" = the slot does nothing on it.
const char* drumMacroName(uint8_t machine, int macro);

// INST picker: machines in groups (KICK, SNARE, ...), each of a few models (808, 909, ...). Machine
// numbers are unchanged; Unused22 and unknown machines map as BDA.
int drumGroupCount();
const char* drumGroupName(int g);         // "KICK"; out of range -> group 0
int drumModelCount(int g);
const char* drumModelName(int g, int k);  // "909"
uint8_t drumMachineAt(int g, int k);      // out of range -> model 0 of group 0
int drumGroupOf(uint8_t machine);
int drumModelOf(uint8_t machine);

}  // namespace mt
