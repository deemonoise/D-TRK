#pragma once
#include <stddef.h>
#include <stdint.h>
#include "model.h"

namespace mt {

// Instrument records shared by .mtp (INST, FMIN, FLTR, SLCE, FMEX chunks) and .mti presets. Little-endian.
// Unpack clamps every value to its valid range.
constexpr size_t kInstRecSize = 48;  // 47 bytes of fields + driveModel at 47 (once reserved)
constexpr size_t kFmRecSize = 16;    // drive, rsend, velCut, velMac at 10..13, crushBits, crushRate at 14, 15
// drive, crushBits, crushRate (FM record) and driveModel (INST record): a preset's recommended track
// insert; 0 in a project (see Instrument::drive).
constexpr size_t kFltRecSize = 8;    // filter, delay send
constexpr int kLegacySlices = 32;
constexpr size_t kSliceRecSize = 72;  // frozen legacy SLCE / presets V2..V9
constexpr size_t kSliceExtRecSize = 264;  // SLX1 / preset V10: 128 positions
constexpr size_t kSynRecSize = 48;  // SYNTH: legacy 0..42, phase mode/offsets 43..45, env target 46, sub wave 47
constexpr size_t kLfoRecSize = 16;  // LFO 1 sync, then LFO 2..4 as wave, rate, depth, dest, sync (bits)
constexpr size_t kLofiRecSize = 8;  // LOFI: formula, sync, 6 reserved (0)
constexpr size_t kLfoByteRecSize = 2 * kLfos;  // LFO BYTE: formula, A of LFO 1..4
constexpr size_t kFmParamRecSize = kFmParams;  // FMPA: independent A..H raw controls
constexpr size_t kFmExRecSize = 4;  // FM/SYNTH extension: input level, CHIP STEP, SYNTH sat type, reserved0

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
// LOFI: the formula index and sync (machine and macros are in the FM record). unpackLofi reads n bytes of a
// record (older / shorter: the rest keeps its value).
void packLofi(const Instrument& m, uint8_t* b);
void unpackLofi(const uint8_t* b, size_t n, Instrument& m);
// LFO BYTE (Instrument::lfoByte). unpackLfoByte reads n bytes of a record (shorter: the rest keeps its value).
void packLfoByte(const Instrument& m, uint8_t* b);
void unpackLfoByte(const uint8_t* b, size_t n, Instrument& m);
// Independent current-generation FM controls.
void packFmParams(const Instrument& m, uint8_t* b);
void unpackFmParams(const uint8_t* b, size_t n, Instrument& m);
// Shared extension (fmSat, chipStep, synSatType). Missing/invalid SYNTH type defaults to Off;
// older shorter records retain the other absent fields.
void packFmEx(const Instrument& m, uint8_t* b);
void unpackFmEx(const uint8_t* b, size_t n, Instrument& m);
// Count is cut to the longest strictly ascending prefix of the positions.
void packSlices(const Instrument& m, uint8_t* b);
void unpackSlices(const uint8_t* b, Instrument& m);
void packSlicesExt(const Instrument& m, uint8_t* b);
void unpackSlicesExt(const uint8_t* b, Instrument& m);
// After all records: an unknown FM machine becomes TONE; other unknown machines become 0, as does a
// LOFI or LFO BYTE formula past the library (byteFormulaCount); an LFO BYTE A over 127 becomes 127.
void fixInstrument(Instrument& m);
// Call only after identifying a pre-FMPA file (project without FMPA, preset version<=8).
// Approximate conversion of the old8-machine family; common controls and source macros stay intact.
// Returns true for a known old FM voice; unknown old ids fall back to neutral TONE.
bool migrateLegacyFm(Instrument& m);

}  // namespace mt
