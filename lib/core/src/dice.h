#pragma once
#include <stdint.h>
#include "arp_gen.h"
#include "bass_gen.h"
#include "chord_gen.h"
#include "dice_recipes.h"
#include "model.h"
#include "scale.h"

namespace mt {

// Dice (PROJ): a starter in a genre / style (dice_recipes.h), by layers (bits, any mix):
//   TEMPO   the style's BPM and one of its scales, a tonic;
//   SOUNDS  the style's KIT on DRUMS, its presets on the other role tracks;
//   PATTERN four patterns MAIN / FILL / BREAK / DROP from the start pattern (diceFirstPattern): the role
//           tracks' steps, the patterns' length / resolution / swing / groove; roles the style lacks are
//           emptied and muted, the others unmuted;
//   SONG    (only with PATTERN) the song becomes MAIN x2, FILL, BREAK, DROP x2, FILL.
// Tracks 7..16 and the other patterns are never touched.
enum : uint8_t { kDiceTempo = 1, kDiceSounds = 2, kDicePattern = 4, kDiceAll = 7, kDiceSong = 8 };

// Track of each role (tracks 1..6).
constexpr uint8_t kDiceTrack[kDiceRoles] = {0, 1, 2, 3, 4, 5};
const char* diceRoleName(DiceRole r);  // "DRUMS", "BASS", "CHORDS", "LEAD", "ARP", "FX" (track names)

constexpr int kDicePats = 4;  // MAIN, FILL, BREAK, DROP
enum : uint8_t { kDiceMain, kDiceFill, kDiceBreak, kDiceDrop };
constexpr int kDiceSongLen = 7;
constexpr int kDiceVarietyDefault = 30;

// The first of the four patterns for the edited one: it, or earlier so that all four fit (P13..P16).
int diceFirstPattern(int editPattern);
// Patterns of the four (bit i = pattern first + i) holding any step: a PATTERN roll overwrites their role
// tracks (UI: "OVERWRITE Pxx-Pyy?").
uint16_t diceOverwrites(const Project& p, int editPattern);

// Instrument slot of each role: the role track's own instrument; DRUMS' drums go to
// factoryKitBlock(out[Drums]); a melodic slot taken by an earlier role, inside that block or played by
// another track (an INT track's instrument, a KIT's lane instruments) becomes the first free one that
// is no role track's own. kNoInstr = the role is skipped: no free slot, or (DRUMS) another track plays
// the kit or an instrument of its block.
void diceSlots(const Project& p, uint8_t out[kDiceRoles]);

struct DiceOpts {
  uint8_t genre = 0, style = 0;  // diceRecipe(genre, style); out of range = 0, 0
  uint8_t variety = kDiceVarietyDefault;  // 0..100: 0 = canon (the heaviest option, no mutations)
  uint8_t layers = kDiceAll;     // kDice* bits
  int8_t root = -1;              // TEMPO: tonic 0..11, -1 = random
  uint8_t len = 0;               // pattern steps 16/32/64/128; 0 = AUTO (from the style)
};

// The role tracks of one pattern, and its header.
struct DicePat {
  uint8_t length;
  Resolution res;
  uint8_t swing, groove;
  uint8_t trackLen[kDiceRoles], trackSpeed[kDiceRoles];
  Step steps[kDiceRoles][kMaxSteps];
};

struct DiceSong {
  uint8_t song[kSongMax];
  uint16_t songMute[kSongMax];
  uint8_t songLen;
  SongFollow songFol[kSongMax];
  int8_t songTrn[kSongMax];
  uint16_t songBpm[kSongMax];
};

// Everything a roll may write.
struct DiceParts {
  uint16_t bpm = 120;
  uint8_t scaleRoot = 0, scaleType = 0;
  TrackCfg tracks[kDiceRoles];
  Instrument instruments[kInstruments];
  GenCfg gen[kDiceRoles];
  VarCfg var[kDiceRoles];
  DicePat pats[kDicePats];
  DiceSong song;
};

// Scratch of a draft (kept out of the UI task's stack).
struct DiceWork {
  ArpPattern rhythm, arp;
  BassPattern bass;
  Progression prog;
  ChordSeg segs[kProgMax];
  Instrument tmp;
  Pattern full;  // MAIN with every role (DROP)
};

// A roll in two phases, so the engine never waits for the generators: diceDraft copies the parts from
// the project and rolls them (UI task, no lock: the UI is the only writer of these), diceApply swaps
// the written ones with the project's (under the project lock). After the swap `parts` holds what the
// roll replaced, bit for bit: the one-step undo. Solo is never swapped; mute only of the tracks PATTERN
// muted / unmuted. The caller owns it (PSRAM on the device, sizeof(DiceSnap) ~ 86 KB).
struct DiceSnap {
  uint8_t layers = 0;   // layers of the applied roll still to undo, 0 = nothing
  uint8_t drafted = 0;  // layers of a draft waiting for diceApply
  uint8_t first = 0;    // first of the four patterns
  uint8_t cfgMask = 0;     // SOUNDS: role tracks whose TrackCfg the roll wrote
  uint8_t muteMask = 0;    // PATTERN: role tracks whose mute it wrote
  uint8_t patMask = 0;     // PATTERN: role tracks whose steps (four patterns), GEN and MUT it wrote
  uint32_t instrMask = 0;  // SOUNDS: instruments it wrote
  DiceParts parts;
  DiceWork work;  // not state
};

struct DiceResult {
  int8_t genre = -1, style = -1;  // the recipe rolled
  uint8_t first = 0;              // first of the four patterns
  uint16_t written = 0;           // tracks whose sound, steps or mute changed
};

// bpm (all four): the project BPM when a song slot's BPM holds the live tempo p.bpm (0 = p.bpm). The roll
// reads it, and the undo keeps it, instead of p.bpm.
// Rolls o.layers into s.parts, seeded (same project + opts + seed = same roll); p is only read. Drops the
// previous roll's undo.
DiceResult diceDraft(const Project& p, const DiceOpts& o, int editPattern, uint32_t seed, DiceSnap& s,
                     uint16_t bpm = 0);
// Swaps the draft into p (what it wrote only). Returns its layers, 0 = no draft.
uint8_t diceApply(Project& p, DiceSnap& s, uint16_t bpm = 0);
// diceDraft + diceApply.
DiceResult diceRoll(Project& p, const DiceOpts& o, int editPattern, uint32_t seed, DiceSnap& s, uint16_t bpm = 0);
// Puts back what the last applied roll wrote and empties the undo. Returns its layers, 0 = nothing.
// Edits made since elsewhere stay; edits to the rolled parts are lost.
uint8_t diceUndo(Project& p, DiceSnap& s, uint16_t bpm = 0);

}  // namespace mt
