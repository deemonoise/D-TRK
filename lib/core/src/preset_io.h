#pragma once
#include "inst_codec.h"
#include "project_io.h"

namespace mt {

// .mti: "MTI1" u8 version u8 type u16 reserved, INST + FMIN + FLTR (+ SLCE from v2, + SYNI from v3)
// records (inst_codec), u32 crc32 of every byte before it. Little-endian. v1 files load without
// slices, v1 / v2 with SYNTH defaults.
constexpr uint8_t kPresetVersion = 5;  // 4: drive, rsend, velCut, velMac in the FM record (same size as 3);
                                       // 5: + the LFO record
constexpr size_t kPresetSizeV1 = 8 + kInstRecSize + kFmRecSize + kFltRecSize + 4;
constexpr size_t kPresetSizeV2 = kPresetSizeV1 + kSliceRecSize;
constexpr size_t kPresetSizeV3 = kPresetSizeV2 + kSynRecSize;
constexpr size_t kPresetSize = kPresetSizeV3 + kLfoRecSize;

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
