#pragma once
#include "model.h"

namespace mt {

// Built-in demo songs for FILE -> New -> Demo songs (nine): whole projects (instruments from the factory
// presets, patterns with notes, a song chain, mixer and sends) built in code, no samples.
int demoCount();
const char* demoName(int i);        // the project name, e.g. "DEMO-TRANCE"
void demoBuild(int i, Project& p);  // p.reset(), then the demo; i out of range = the first one

}  // namespace mt
