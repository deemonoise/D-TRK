#pragma once
#include "hw/sdcard.h"
#include "model.h"
#include "scala.h"
#include "storage.h"

namespace storage {

// User tunings: /presets/TUN/NAME.scl (Scala), copied onto the card with a computer; read only. Names:
// validName (up to 16 characters A-Z a-z 0-9 _ -). UI task only.
constexpr const char* kTunDir = "/presets/TUN";
// Names (no extension), sorted; 0 without a card or folder.
int listTunings(char (*names)[hw::kNameMax], int max);
// Reads and parses NAME.scl into t (name = NAME, root kept). Result::Ok, or BadFile with err / errLine
// set (mt::ParseErr::TooBig for a file over mt::kSclFileMax), or the card's error (err Ok).
Result loadTuning(const char* name, mt::TuningTable& t, mt::ParseErr& err, int& errLine);

}  // namespace storage
