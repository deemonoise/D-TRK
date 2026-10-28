#pragma once
// Factory presets: read-only, built into the firmware (ESP flash, .rodata; not linked on the Teensy).
//
// HOW TO ADD ONE
// - One file per type: presets_factory_<type>.cpp, type = chip fm drum synth string lofi kit (each
//   defines kFactory<Type>, see presets_factory_fill.h). Add a row to its kTable:
//     {InstrType::Fm, "BASS", "SUB FM", [](I& m) { fm(m, FmMachine::Tone); mac(m, ...); env(m, ...); }},
//   fill: void(Instrument&), a captureless lambda, run on Instrument{} with type and name already set
//   (KIT: kitSetDefaults and the lane vols too). Helpers (presets_factory_fill.h, namespace pf):
//   fm dr mac sy st bb tl apc lp hp bp env lfo1 lfoN fx, W F D LW Wt for the enum fields.
//   KIT rows: KIT(category, name, drums) with a FactoryDrum[] {type, preset name, lane level}, lanes
//   kick, snare, closed hat, open hat, clap, extras (<= kKitLanes); the lane level becomes the drum's vol.
// - name: 1..8 chars (Instrument::name is char[9]), unique within its type, upper case like the rest.
//   Names are the key: DICE recipes (dice_recipes.cpp), demos.cpp, templates.cpp and the KIT tables
//   find presets by type + name, so never rename or remove one; the index is only the browser's.
// - category: 1..16 chars, the browser folder. A category is one run of rows (no interleaving);
//   put a new preset at the end of its category's run, a new category after the last one. In use:
//   CHIP LEAD BASS PAD KEYS PERC FX; FM DRUMS PERC KEYS BASS LEAD PAD; DRUM 808 909 LOFI HARD ANALOG;
//   SYNTH BASS LEAD PAD KEYS PLUCK FX; STRING PLUCK KEYS; LOFI BYTE APC; KIT DRUM FM CHIP MIX.
//   "FX" means a riser / effect to DICE.
// - drive / crushBits / crushRate / driveModel are the track insert the preset recommends; lower vol
//   with heavy drive or resonance (the insert is dropped when the preset plays behind a KIT lane).
// - Values: whatever survives the instrument record (test_valid_values packs and unpacks each preset);
//   SYNTH wavetables are built-in "*NAME" only. test/test_presets_factory checks all of this: run
//   `pio test -e native -f test_presets_factory`.
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
// Instrument{} + type + name + fill. drive / crushBits / crushRate / driveModel are the track insert
// the preset recommends (takePresetInsert copies them to the track it is loaded onto).
// KIT: the lanes' notes (60 + k) and volumes; no instruments (see factoryBuildKit).
void factoryBuild(InstrType t, int i, Instrument& out);

// First of the kKitLanes instrument slots a factory KIT in `kitSlot` fills: INS25..32, INS17..24 when
// the kit itself is in INS25..32.
int factoryKitBlock(int kitSlot);
// KIT preset i into p.instruments[kitSlot], its drums into the block from factoryKitBlock (lane k ->
// block + k, transposed down by k to sound at its own pitch on note 60 + k), without their insert
// recommendation (a lane has no insert of its own). Slots past the kit's drums are left as they are.
void factoryBuildKit(int i, Project& p, int kitSlot);
// The same on an instrument array of kInstruments slots (a copy of the project's).
void factoryBuildKit(int i, Instrument* instruments, int kitSlot);

}  // namespace mt
