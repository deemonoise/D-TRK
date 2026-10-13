#pragma once
#include "chord_gen.h"
#include "hw/sdcard.h"
#include "storage.h"

namespace storage {

// User chord progressions: /presets/PRG/NAME.prg, the progression's text form (chord_gen.h). UI task only.
constexpr const char* kProgDir = "/presets/PRG";
// Names (no extension), sorted; 0 without a card or folder.
int listProgs(char (*names)[hw::kNameMax], int max);
Result loadProg(const char* name, mt::Progression& out);
// name: validName. Creates the folder; writes NAME.tmp, then replaces NAME.prg.
Result saveProg(const char* name, const mt::Progression& p);

}  // namespace storage
