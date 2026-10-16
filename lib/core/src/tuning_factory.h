#pragma once
#include "model.h"

namespace mt {

// Built-in tunings (PROJ Tuning list after 12-TET), kept as .scl texts and read with parseScl.
int factoryTuningCount();
const char* factoryTuningName(int i);           // "JUST 5-LIMIT"; "" out of range
bool factoryTuning(int i, TuningTable& out);    // fills count, mc, name; keeps root. False out of range
int factoryTuningFind(const char* name);        // index by exact name, -1 if none

}  // namespace mt
