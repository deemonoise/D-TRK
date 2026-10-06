#pragma once
#include "model.h"
#include "preset_paths.h"
#include "storage.h"

namespace storage {

// User presets: /presets/<TYPE>/[folders up to kPresetDepthMax]/NAME.mti. UI task only.
// name: validName (the UI sanitizes it first).
constexpr int kPresetDepthMax = mt::kPresetDepthMax;
// "/presets/CHIP" etc.
inline const char* presetRoot(mt::InstrType t) { return mt::presetRoot(t); }
// Folders below the type root of dir (0 = the root itself).
inline int presetDepth(const char* dir) { return mt::presetDepth(dir); }
bool presetExists(const char* dir, const char* name);
// Writes NAME.tmp, reads it back (loadPreset), replaces NAME.mti.
Result savePreset(const char* dir, const char* name, const mt::Instrument& m);
Result loadPreset(const char* dir, const char* name, mt::Instrument& out);
// Only the .mti; empty folders go through Wi-Fi.
Result removePreset(const char* dir, const char* name);
// dir/name; Ok when it already exists.
Result makePresetDir(const char* dir, const char* name);

}  // namespace storage
