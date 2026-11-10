#pragma once
#include "inst_codec.h"
#include "project_io.h"

namespace mt {

// v12 uses header E8 and appends SLX1 + MODM to the classic-FM v8 prefix.
// Versions 9..11 are a different Teensy FM family and are deliberately rejected.
// .mti: "MTI1" u8 version u8 type u16 reserved, INST + FMIN + FLTR (+ SLCE from v2, + SYNI from v3,
// + LFO from v5, + LOFI from v6, + LFOB from v7, + FMEX from v8) records (inst_codec), u32 crc32 of every
// byte before it. Little-endian. v1 files load without slices, v1 / v2 with SYNTH defaults, v5 and older
// with LOFI formula 0, v6 and older with LFO BYTE defaults, v7 and older with SAT 0.
constexpr uint8_t kPresetVersion = 12;  // 4: drive, rsend, velCut, velMac in the FM record (same size as 3);
                                       // 5: + the LFO record; 6: + the LOFI record; 7: + the LFOB record;
                                       // 8: + the FMEX record (FM SAT)
constexpr size_t kPresetSizeV1 = 8 + kInstRecSize + kFmRecSize + kFltRecSize + 4;
constexpr size_t kPresetSizeV2 = kPresetSizeV1 + kSliceRecSize;
constexpr size_t kPresetSizeV3 = kPresetSizeV2 + kSynRecSize;
constexpr size_t kPresetSizeV5 = kPresetSizeV3 + kLfoRecSize;
constexpr size_t kPresetSizeV6 = kPresetSizeV5 + kLofiRecSize;
constexpr size_t kPresetSizeV7 = kPresetSizeV6 + kLfoByteRecSize;
constexpr size_t kPresetSizeV8 = kPresetSizeV7 + kFmExRecSize;
constexpr size_t kPresetSize = kPresetSizeV8 + kSliceExtRecSize + kModRecSize;
static_assert(kPresetSizeV8 == 240 && kPresetSize == 544, "ESP classic-FM preset layout");

bool savePreset(const Instrument& m, ByteSink& out);
// out is written only on Ok. Values are clamped like a project's.
LoadErr loadPreset(ByteSource& in, Instrument& out);
// Copies src into dst. A SAMPLE preset whose sample is not in the project (sampleFound false)
// keeps dst's sample name, root and slice positions. SYNTH wavetable names are copied as they are
// (the firmware resolves them).
void applyPreset(Instrument& dst, const Instrument& src, bool sampleFound);
// After a preset was loaded into p.instruments[instr]: its recommended insert (drive, crushBits,
// crushRate, driveModel) overwrites track `track`'s insert when that track is INT and plays instr;
// then the instrument's recommendation is cleared (a project's instruments carry none). Another track
// (e.g. a KIT track while one of its lane instruments gets a preset), a MIDI track or track -1: the
// recommendation is dropped, no track changes.
void takePresetInsert(Project& p, int instr, int track);
// The same for a track c known to play m (INT): c takes m's recommendation, m's is cleared.
void takePresetInsert(Instrument& m, TrackCfg& c);
// Saving p.instruments[instr] as the preset m (a copy of it): its recommendation is track `track`'s
// insert when that track is INT and plays instr, else none.
void presetInsertForSave(const Project& p, int instr, int track, Instrument& m);

}  // namespace mt
