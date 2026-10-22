#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

// Mutate (INST): moves an instrument's sound by up to strength % of half of each field's range
// (strength 0..100), seeded. A field already outside its range stays. Ranges:
//   amp envelope (CHIP, SAMPLE, SYNTH, FM TONE / CHORD): attack 0..100, decay 1..127, sustain 0..127,
//     release 0..110
//   filter (fltMode not OFF): cutoff 10..127, reso 0..110, fenv -64..63, fAtk 0..100, fDec 1..127
//   macros (FM, DRUM, SYNTH): macro[0..4] 0..127
//   SYNTH: synSub 0..127, synNoise 0..80, synEAtk 0..100, synEDec 1..127
//   CHIP with a PULSE wave: duty 5..95, pwmDepth 0..49, pwmRate 0..127 (only when pwmDepth was > 0)
// Everything else (name, type, machine, vol, tuning, wave, sample, slices, LFOs, sends, KIT lanes) stays.
constexpr int kMutateMax = 100;

// Not a KIT and strength 1..kMutateMax.
bool mutateApplies(const Instrument& m, int strength);
// False (m unchanged) when !mutateApplies.
bool mutateInstrument(Instrument& m, int strength, uint32_t seed);

// One-step undo of a Mutate: the instrument as it was (bit for bit), as the Mutate left it, and its
// slot, -1 = none.
struct MutateUndo {
  int8_t slot = -1;
  Instrument m;
  Instrument after;
};
// Mutates p.instruments[slot]; on success first copies it into undo. False (undo untouched): bad slot,
// or !mutateApplies.
bool mutateSlot(Project& p, int slot, int strength, uint32_t seed, MutateUndo& undo);
// Undo holds a Mutate and the slot is still bit for bit what it left (no preset load, type change,
// slice or knob edit since).
bool mutateCanRestore(const Project& p, const MutateUndo& undo);
// Puts undo's instrument back and empties undo. False when !mutateCanRestore (an edited slot stays as
// it is; undo is emptied all the same).
bool mutateRestore(Project& p, MutateUndo& undo);

}  // namespace mt
