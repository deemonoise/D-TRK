#pragma once
#include "model.h"

namespace mt {

// A KIT lane's drum: a factory preset of another type, by name, and the lane volume.
struct FactoryDrum {
  InstrType type;
  const char* name;
  uint8_t vol;
};

// Built-in presets, read-only. Order is the browser's: by category, then as listed.
struct FactoryPreset {
  InstrType type;
  const char* category;         // folder in the browser, e.g. "BASS"
  const char* name;             // <= 8 chars, also the instrument name
  void (*fill)(Instrument& m);  // on top of Instrument{} with type and name set
  const FactoryDrum* drums = nullptr;  // KIT: lane k plays drums[k]
  uint8_t drumCount = 0;               // <= kKitLanes
};

int factoryCount(InstrType t);
const FactoryPreset& factoryPreset(InstrType t, int i);  // 0 <= i < factoryCount(t)
// Instrument{} + type + name + fill.
// KIT: the lanes' notes (60 + k) and volumes; no instruments (see factoryBuildKit).
void factoryBuild(InstrType t, int i, Instrument& out);

// First of the kKitLanes instrument slots a factory KIT in `kitSlot` fills: INS25..32, INS17..24 when
// the kit itself is in INS25..32.
int factoryKitBlock(int kitSlot);
// KIT preset i into p.instruments[kitSlot], its drums into the block from factoryKitBlock (lane k ->
// block + k, transposed down by k to sound at its own pitch on note 60 + k). Slots past the kit's
// drums are left as they are.
void factoryBuildKit(int i, Project& p, int kitSlot);

}  // namespace mt
