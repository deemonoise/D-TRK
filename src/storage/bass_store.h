#pragma once
#include "bass_gen.h"
#include "hw/sdcard.h"
#include "storage.h"

namespace storage {

// User bass patterns: /presets/BAS/NAME.bas, the pattern's text form (bass_gen.h). UI task only.
constexpr const char* kBassDir = "/presets/BAS";
// Names (no extension), sorted; 0 without a card or folder.
int listBass(char (*names)[hw::kNameMax], int max);
Result loadBass(const char* name, mt::BassPattern& out);
// name: validName. Creates the folder; writes NAME.tmp, then replaces NAME.bas.
Result saveBass(const char* name, const mt::BassPattern& p);

}  // namespace storage
