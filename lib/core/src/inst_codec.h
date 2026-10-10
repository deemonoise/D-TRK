#pragma once
#include <stddef.h>
#include <stdint.h>
#include "model.h"

namespace mt {

// Instrument records shared by .mtp (INST, FMIN, FLTR, SLCE chunks) and .mti presets. Little-endian.
// Unpack clamps every value to its valid range.
constexpr size_t kInstRecSize = 48;  // 47 bytes of fields + 1 reserved
constexpr size_t kFmRecSize = 16;    // drive, rsend, velCut, velMac at 10..13, crushBits, crushRate at 14, 15
constexpr size_t kFltRecSize = 8;    // filter, delay send
constexpr size_t kSliceRecSize = 8 + 2 * kMaxSlices;  // modes, count, 3 reserved, positions
constexpr size_t kSynRecSize = 48;  // SYNTH: 41 bytes of fields + reserved
constexpr size_t kLfoRecSize = 16;  // LFO 1 sync, then LFO 2..4 as wave, rate, depth, dest, sync (bits)

void packInst(const Instrument& m, uint8_t* b);
void unpackInst(const uint8_t* b, Instrument& m);
void packFm(const Instrument& m, uint8_t* b);
void unpackFm(const uint8_t* b, Instrument& m);
void packFlt(const Instrument& m, uint8_t* b);
void unpackFlt(const uint8_t* b, Instrument& m);
// SYNTH: osc types, wavetable names (16 bytes each, no terminator), semi, sync, sub, sub octave,
// noise, env -> SHAPE attack / decay.
void packSyn(const Instrument& m, uint8_t* b);
void unpackSyn(const uint8_t* b, Instrument& m);
void packLfo(const Instrument& m, uint8_t* b);
void unpackLfo(const uint8_t* b, Instrument& m);
// Count is cut to the longest strictly ascending prefix of the positions.
void packSlices(const Instrument& m, uint8_t* b);
void unpackSlices(const uint8_t* b, Instrument& m);
// After all records of an instrument: a machine number the type does not have becomes 0.
void fixInstrument(Instrument& m);

}  // namespace mt
