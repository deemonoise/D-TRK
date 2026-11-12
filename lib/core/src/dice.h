#pragma once
#include <stdint.h>
#include "dice_recipes.h"
#include "model.h"
#include "scale.h"
#include "dice_hybrid_work.h"

namespace mt {

// Guide percussion in one KIT, native pitched voices and complementary parts on tracks 1..6.
enum : uint8_t { kDiceTempo = 1, kDiceSounds = 2, kDicePattern = 4, kDiceAll = 7, kDiceSong = 8 };

// Track of each role (tracks 1..6).
constexpr uint8_t kDiceTrack[kDiceRoles] = {0, 1, 2, 3, 4, 5};

constexpr int kDicePats = 4;  // legacy Classic/Develop pattern count stays stable
constexpr int kDiceMaxPats = 6;
enum : uint8_t { kDiceMain, kDiceFill, kDiceBreak, kDiceDrop };
enum : uint8_t { kArrangeIntro, kArrangeMain, kArrangeBreak, kArrangeBuild, kArrangeDrop, kArrangeOutro };
constexpr int kDiceSongLen = 7;
constexpr int kDiceVarietyDefault = 35;

// Dice always starts at P01. The edit/count arguments remain for source compatibility.
int diceFirstPattern(int editPattern, int count = kDicePats);
// Patterns of the range (bit i = pattern first + i) holding any step: a PATTERN roll overwrites their role
// tracks (UI: "OVERWRITE Pxx-Pyy?").
uint16_t diceOverwrites(const Project& p, int editPattern, int count = kDicePats);

enum class DiceForm : uint8_t { Classic, Develop, Arrange };
enum class DiceChange : uint8_t { Subtle, Normal, Wild };
enum class DiceDensity : uint8_t { Sparse, Normal, Busy };

struct DiceOpts {
  DiceForm form = DiceForm::Develop;
  DiceChange change = DiceChange::Wild;
  DiceDensity density = DiceDensity::Normal;  // WILD pattern rolls only; independent of Variety
  uint8_t targetMask = 0x3f;
  uint8_t groove = 35;
  uint8_t lockMask = 0;  // locked roles keep sound, settings, notes, GEN/MUT and mute
  bool clean = true;
  uint32_t seed = 0;  // UI: 0 = new seed, otherwise replay this seed
  uint8_t variant = 0;  // 0 = CLASSIC; kDiceVariantAuto selects from the seed
  uint8_t avoidVariant = 255;  // NEW/AUTO: avoid the previous selection; FIXED ignores this
  uint8_t genre = 0, style = 0;  // diceRecipe(genre, style); out of range = 0, 0
  uint8_t variety = kDiceVarietyDefault;  // fresh melodic development; 0 retains the source contour in the chosen key
  uint8_t layers = kDiceAll;     // kDice* bits
  bool keepKey = false;         // full roll: keep project tonic/mode instead of choosing a fresh key
  int8_t root = -1;              // TEMPO: explicit tonic 0..11, -1 = seeded AUTO
  uint8_t len = 0;               // pattern steps 16/32/64/128; 0 = AUTO (from the style)
};

// Six sections only for a full WILD ARRANGE pattern roll; all other requests use four.
int dicePatternCount(const DiceOpts& opts);

// The role tracks of one pattern, and its header.
struct DicePat {
  char name[kPatternNameMax + 1];
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
  DicePat pats[kDiceMaxPats];
  DiceSong song;
};

// A roll in two phases, so the engine never waits for the generators: diceDraft copies the parts from
// the project and rolls them (UI task, no lock: the UI is the only writer of these), diceApply swaps
// the written ones with the project's (under the project lock). After the swap `parts` holds what the
// roll replaced, bit for bit: the one-step undo. Solo is never swapped; mute only of the tracks PATTERN
// muted / unmuted. The caller owns it (PSRAM on the device).
struct DiceSnap {
  bool preserveHeaders = false;  // a role lock also protects its pattern timing
  bool namePatterns = false;  // full WILD pattern roll: exchange section labels on apply/undo
  uint8_t layers = 0;   // layers of the applied roll still to undo, 0 = nothing
  uint8_t drafted = 0;  // layers of a draft waiting for diceApply
  uint8_t first = 0;    // first of the active pattern range
  uint8_t patCount = kDicePats;  // actual draft/undo range; inactive capacity is never read or swapped
  uint8_t cfgMask = 0;     // SOUNDS: role tracks whose TrackCfg the roll wrote
  uint8_t muteMask = 0;    // PATTERN: role tracks whose mute it wrote
  uint8_t patMask = 0;     // PATTERN: role tracks whose active-pattern steps, GEN and MUT it wrote
  uint32_t instrMask = 0;  // SOUNDS: instruments it wrote
  DiceParts parts;
  DicePat voices;  // one source-pattern scratch, kept off the UI stack while packing the KIT
  DiceHybridWork hybrid;
};

struct DiceResult {
  uint32_t seed = 0;
  uint8_t missingMask = 0, variant = 0;
  int8_t genre = -1, style = -1;  // the recipe rolled
  uint8_t first = 0;              // first of the active pattern range
  uint16_t written = 0;           // tracks whose sound, steps or mute changed
};

// bpm (all sections): the project BPM when a song slot's BPM holds the live tempo p.bpm (0 = p.bpm). The roll
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
