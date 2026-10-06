#pragma once
#include "model.h"

namespace mt {

// Built-in presets, read-only. Order is the browser's: by category, then as listed.
struct FactoryPreset {
  InstrType type;
  const char* category;         // folder in the browser, e.g. "BASS"
  const char* name;             // <= 8 chars, also the instrument name
  void (*fill)(Instrument& m);  // on top of Instrument{} with type and name set
};

int factoryCount(InstrType t);
const FactoryPreset& factoryPreset(InstrType t, int i);  // 0 <= i < factoryCount(t)
// Instrument{} + type + name + fill.
void factoryBuild(InstrType t, int i, Instrument& out);

}  // namespace mt
