#pragma once
#include <stdint.h>
#include "arp_gen.h"
#include "bass_gen.h"
#include "chord_gen.h"
#include "model.h"
#include "scale.h"

namespace mt {

// Dice (PROJ): a random starter for the project, by layers (bits, any mix):
//   TEMPO   a genre: its BPM range and one of its scales, a random tonic;
//   SOUNDS  factory presets on the role tracks (a KIT on DRUMS, BASS / PAD+KEYS / LEAD+PLUCK presets);
//   PATTERN the role tracks of one pattern from the generators (Euclid lanes, CHORDS, BASS, ARP).
enum : uint8_t { kDiceTempo = 1, kDiceSounds = 2, kDicePattern = 4, kDiceAll = 7 };

enum class DiceRole : uint8_t { Drums, Bass, Chords, Lead, Count };
constexpr int kDiceRoles = static_cast<int>(DiceRole::Count);
// Track of each role (tracks 1..4); the others are never touched.
constexpr uint8_t kDiceTrack[kDiceRoles] = {0, 1, 2, 3};
const char* diceRoleName(DiceRole r);  // "DRUMS", "BASS", "CHORDS", "LEAD" (track names)

constexpr int kDiceScales = 3;
constexpr int kDiceKits = 2;
constexpr int kDiceKitPct = 70;  // SOUNDS: % of rolls that take one of the genre's kits
struct DiceGenre {
  const char* name;  // up to 10 chars
  uint16_t bpmLo, bpmHi;
  ScaleType scales[kDiceScales];  // 7-note scales only (diatonic chords)
  const char* kits[kDiceKits];    // factory KIT names that suit the genre
};
int diceGenreCount();
const DiceGenre& diceGenre(int i);  // out of range = 0
// The genre SOUNDS picks a kit for when TEMPO is not rolled with it: the first whose BPM range holds
// bpm, -1 = none.
int diceGenreOfBpm(uint16_t bpm);

// Instrument slot of each role: the role track's own instrument; DRUMS' drums go to
// factoryKitBlock(out[Drums]); a melodic slot taken by an earlier role, inside that block or played by
// another track (an INT track's instrument, a KIT's lane instruments) becomes the first free one that
// is no role track's own. kNoInstr = the role is skipped: no free slot, or (DRUMS) another track plays
// the kit or an instrument of its block.
void diceSlots(const Project& p, uint8_t out[kDiceRoles]);

// Scratch of a draft (kept out of the UI task's stack).
struct DiceWork {
  ArpPattern rhythm, arp;
  BassPattern bass;
  ChordSeg segs[kProgMax];
  Instrument tmp;
};

// Everything a roll may write: the master tempo / scale, the track settings, the instruments, GEN and
// one pattern.
struct DiceParts {
  uint16_t bpm = 120;
  uint8_t scaleRoot = 0, scaleType = 0;
  TrackCfg tracks[kTracks];
  Instrument instruments[kInstruments];
  GenCfg gen[kTracks];
  Pattern pat;
};

// A roll in two phases, so the engine never waits for the generators: diceDraft copies the parts from
// the project and rolls them (UI task, no lock: the UI is the only writer of these), diceApply swaps
// the written ones with the project's (under the project lock). After the swap `parts` holds what the
// roll replaced, bit for bit: the one-step undo. Track mute / solo are never swapped: both the apply
// and the undo keep the live ones. The caller owns it (PSRAM on the device, ~43 KB).
struct DiceSnap {
  uint8_t layers = 0;   // layers of the applied roll still to undo, 0 = nothing
  uint8_t drafted = 0;  // layers of a draft waiting for diceApply
  uint8_t pattern = 0;
  uint16_t cfgMask = 0;    // SOUNDS: tracks whose TrackCfg the roll wrote
  uint32_t instrMask = 0;  // SOUNDS: instruments it wrote
  uint16_t patMask = 0;    // PATTERN: tracks of `pattern` (steps, trackLen, trackSpeed) and their GEN
  DiceParts parts;
  DiceWork work;  // not state
};

struct DiceResult {
  int8_t genre = -1;     // TEMPO: the genre drawn, else -1
  uint16_t written = 0;  // tracks whose sound or steps changed (cfgMask | patMask)
};

// Rolls the layers into s.parts (pattern: 0..kPatterns-1, else 0), seeded; p is only read. Drops the
// previous roll's undo.
DiceResult diceDraft(const Project& p, uint8_t layers, int pattern, uint32_t seed, DiceSnap& s);
// Swaps the draft into p (what it wrote only). Returns its layers, 0 = no draft.
uint8_t diceApply(Project& p, DiceSnap& s);
// diceDraft + diceApply.
DiceResult diceRoll(Project& p, uint8_t layers, int pattern, uint32_t seed, DiceSnap& s);
// Puts back what the last applied roll wrote and empties the undo. Returns its layers, 0 = nothing.
// Edits made since elsewhere stay; edits to the rolled parts are lost.
uint8_t diceUndo(Project& p, DiceSnap& s);

}  // namespace mt
