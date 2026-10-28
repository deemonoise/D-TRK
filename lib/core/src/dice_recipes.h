#pragma once
#include <stdint.h>
#include "chord_gen.h"
#include "model.h"
#include "scale.h"

namespace mt {

// Dice roles, on tracks 1..6 (kDiceTrack in dice.h).
enum class DiceRole : uint8_t { Drums, Bass, Chords, Lead, Arp, Fx, Count };
constexpr int kDiceRoles = static_cast<int>(DiceRole::Count);
constexpr uint8_t diceRoleBit(DiceRole r) { return static_cast<uint8_t>(1u << static_cast<int>(r)); }

// Recipe lists: an option and its weight (> 0); every list ends with an entry of weight 0.
struct DiceName {  // a factory item by name, or a drum hits / motif string
  const char* name;
  uint8_t w;
};
struct DiceVal {  // a value (ScaleType, ArpMode, DiceLead)
  uint8_t v;
  uint8_t w;
};
struct DiceSound {  // a factory preset: type + name
  InstrType type;
  const char* name;
  uint8_t w;
};

// Drum lanes of a recipe; dice.cpp finds each in the kit by its drums' names.
enum DiceLane : uint8_t { kDiceKick, kDiceSnare, kDiceClap, kDiceHat, kDiceOpen, kDicePerc, kDiceLanes };

// Kit lane of each DiceLane in a kit dice builds (the factory kits' layout: kick, snare, closed / open hat,
// clap, perc).
constexpr int8_t kDiceKitLane[kDiceLanes] = {0, 1, 4, 2, 3, 5};

// A drum of a dice kit: a factory DRUM / FM / CHIP preset, its level (the lane instrument's vol and the
// lane's, 0..127, linear: 64 = -6 dB from 127) and offsets to its first two macros (DRUM / FM: DECAY and
// TONE / ATTACK / COLOR..., the length and click / brightness of the hit; other types ignore them).
struct DiceDrum {
  InstrType type;
  const char* name;
  uint8_t vol;
  int8_t dec, tone;
  uint8_t w;
};
// The drums of a style's kit, per DiceLane: every lane has a list (one drum each is rolled).
struct DiceKit {
  const DiceDrum* lane[kDiceLanes];
};
// Drum dynamics: step velocities by position in the beat (i % 4 of the pattern: on the beat, 'e', '&',
// 'a'), 'X' accent and 'o' ghost velocities, the drum track's humanize at Variety 100 (scaled down to 0
// at Variety 0). A step's lanes share its velocity: the loudest hit wins.
struct DiceFeel {
  uint8_t vel[4];
  uint8_t accent, ghost, human;
};
// A role's mix and tone on top of its preset. vol / pan: the track's. Caps (kDiceKeep = leave): cut, res,
// fenv: the filter's cutoff / resonance / env amount at most (cut on a preset without a filter adds a
// low-pass there); drv: track drive and SYNTH fold / ring, FM SAT at most; crush: bit / rate reduction at
// most. atk / rel: amp envelope at least (envTimeMs, 0 = leave). dly / rvb: the delay / reverb sends (on
// DRUMS: the lanes other than the kick; the delay only on perc), kDiceKeep = the preset's.
constexpr uint8_t kDiceKeep = 255;
struct DiceTone {
  uint8_t vol, pan, cut, res, fenv, atk, rel, drv, crush, dly, rvb;
};
struct DiceMix {
  DiceTone role[kDiceRoles];
};

// LEAD: an arp over the chords (the recipe's arps), a motif by scale degrees, or nothing (track muted).
enum class DiceLead : uint8_t { None, Arp, Motif, Count };

// A style (subgenre). Drum hits strings: one char per step, 16 or 32 chars, repeated over
// the pattern: 'X' accent, 'x' hit, 'o' ghost, '2'..'4' a hit rolled (RAT) when it is the step's only
// lane, '.' none. Motif strings: space-separated tokens per step: '1'..'9' scale degree from the tonic
// (8 = octave), suffix '^' / 'v' an octave up / down, '-' holds the previous note, '.' rest.
struct DiceRecipe {
  const char* style;            // up to 11 chars
  uint16_t bpmLo, bpm, bpmHi;   // bpm: canon (Variety 0)
  uint8_t len;                  // AUTO length: 64 melodic, 32 groove (DiceOpts::len overrides)
  Resolution res;
  const char* groove;           // groove name (groove.h), nullptr = OFF: swing applies
  uint8_t swing;                // 50..75
  const DiceVal* scales;        // ScaleType, 7-note scales only
  uint8_t roles;                // diceRoleBit mask
  const DiceKit* kit;           // DRUMS: built lane by lane
  const DiceName* drums[kDiceLanes];  // hits strings per lane, nullptr = lane silent
  const DiceSound* sounds[kDiceRoles];  // per role ([Drums] unused: kits)
  const DiceName* progs;        // chord_gen factory progressions
  const DiceName* rhythms;      // chord_gen factory rhythms
  ChordFlavor flavor;
  uint8_t chordOct;             // 3..4
  const DiceName* basses;       // bass_gen factory patterns
  const DiceName* arps;         // arp_gen factory patterns (ARP, and LEAD in Arp mode)
  const DiceVal* arpModes;      // ArpMode
  const DiceVal* leads;         // DiceLead
  const DiceName* motifs;       // motif names (diceMotif*)
  const DiceName* fxHits;       // FX in percussion mode (a non-FX preset): hits strings, nullptr = default
  const DiceFeel* feel;         // drum dynamics
  const DiceMix* mix;           // role levels and tone
};

// Genres and styles, by index; order and names only ever grow at the end (indices are stored in NVS).
int diceGenreCount();
const char* diceGenreName(int g);                // nullptr out of range
int diceStyleCount(int g);                       // 0 out of range
const char* diceStyleName(int g, int s);         // nullptr out of range
const DiceRecipe* diceRecipe(int g, int s);      // nullptr out of range

// LEAD motifs (by name in recipes).
int diceMotifCount();
const char* diceMotifName(int i);  // nullptr out of range
const char* diceMotifText(int i);  // nullptr out of range
int diceMotifSteps(const char* text);  // token count, -1 = a bad token

// Drum fills of FILL's last steps (8 per lane; a 16-step pattern takes the last 4): kick, snare, perc.
struct DiceFill {
  const char* kick;
  const char* snare;
  const char* perc;
  uint8_t w;
};
const DiceFill* diceFills();  // ends with w = 0

}  // namespace mt
