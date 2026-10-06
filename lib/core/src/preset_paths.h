#pragma once
#include <stddef.h>
#include "model.h"

namespace mt {

// Folders of user presets on the card: /presets/<TYPE>/[folders]/NAME.mti. Paths are absolute,
// segments joined by '/', no trailing '/'.
constexpr int kPresetDepthMax = 4;  // folders below the type's root
constexpr int kPresetDirMax = 160;  // a browser's folder buffer, incl. terminator

const char* presetTypeName(InstrType t);  // "CHIP", "SAMPLE", "FM", "DRUM", "SYNTH"
const char* presetRoot(InstrType t);      // "/presets/CHIP" etc.
// Folders below the type's root of dir: 0 for the root itself, -1 when dir is not inside
// /presets/<TYPE> of a known type.
int presetDepth(const char* dir);
// Part of dir below its type's root: "" at the root, else "/A/B".
const char* presetSubPath(const char* dir);
// out = dir/name + ext (ext may be ""). False when it does not fit.
bool presetJoin(char* out, size_t cap, const char* dir, const char* name, const char* ext = "");
// Cuts the last folder of dir, never above its type's root. False at the root (dir unchanged).
// left (optional, kNameMax-like buffer of leftCap) gets the folder that was cut.
bool presetUp(char* dir, char* left = nullptr, size_t leftCap = 0);

// Categories of the built-in presets of t, unique, in table order.
int factoryCategoryCount(InstrType t);
const char* factoryCategory(InstrType t, int k);  // 0 <= k < factoryCategoryCount(t)

}  // namespace mt
