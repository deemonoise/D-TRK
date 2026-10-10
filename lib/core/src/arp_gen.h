#pragma once
#include <stdint.h>
#include "edit_ops.h"
#include "model.h"
#include "scale.h"

namespace mt {

// Arp generator of FILL: a rhythm pattern (factory or user) run over held notes, written as notes
// and fx on one track. Text form of a pattern, one token per step: 'x' note, 'X' accent, 'o' ghost,
// '.' rest, '-' tie (the previous note holds); after a note: 's' short, 'l' long, '~' slide,
// 'r' repeat the previous note, 'p' the lowest held note, '^' +12, 'v' -12.
constexpr int kArpPatMax = 32;

enum class ArpKind : uint8_t { Rest, Note, Tie };
enum class ArpAcc : uint8_t { Ghost, Norm, Accent };
enum class ArpLen : uint8_t { Short, Norm, Long };
enum class ArpPitch : uint8_t { Next, Repeat, Root };

struct ArpStep {
  ArpKind kind = ArpKind::Rest;
  ArpAcc acc = ArpAcc::Norm;
  ArpLen len = ArpLen::Norm;
  ArpPitch pitch = ArpPitch::Next;
  int8_t oct = 0;  // -1, 0, +1
  bool slide = false;
};

struct ArpPattern {
  uint8_t len = 0;
  ArpStep steps[kArpPatMax];
};

// False when the text has no step. Steps past kArpPatMax are ignored.
bool parseArpPattern(const char* text, ArpPattern& out);
// Canonical text (tokens joined by ' '). False when cap is too small.
bool formatArpPattern(const ArpPattern& p, char* out, int cap);

// Built-in patterns, grouped by style (BASIC, TR, PSY, TE, HO, DNB, ACID, EL, BR, SW, DUB, CHIP).
int arpFactoryCount();
const char* arpFactoryName(int i);  // up to 10 chars; nullptr out of range
const char* arpFactoryText(int i);  // nullptr out of range

enum class ArpSource : uint8_t { Chord, Selection, Count };
enum class ArpMode : uint8_t { Up, Down, UpDown, DownUp, Played, Random, Converge, Diverge, Pedal, Chord, Count };
const char* arpModeName(ArpMode m);  // "UP", "UP/DN", ...

struct ArpSpec {
  ArpSource source = ArpSource::Chord;
  uint8_t root = 60;                // CHORD: the chord's note
  uint8_t chord = kChordTriad;      // CHORD: CHD value, built in the project's scale
  uint8_t dest = 0;                 // track written
  ArpMode mode = ArpMode::Up;
  uint8_t octaves = 1;              // 1..4
  uint8_t pattern = 0;              // UI: factory index, then user patterns
  uint8_t rate = 1;                 // 1..4 track steps per arp step
  uint8_t rotate = 0;               // pattern start offset
  uint8_t gate = 50;                // NORM gate %; SHORT half, LONG 100 (x rate)
  uint8_t swing = 0;                // 0..100: NDG swing / 2 on the odd arp steps
  uint8_t velLo = 60, velHi = 120;  // ghost, accent; normal = the middle
  uint8_t slide = 16;               // SLD value of slide steps
  uint8_t roll = 0;                 // % of notes with RAT 2..4
  uint8_t ghostPrb = 100;           // PRB of ghost notes, 100 = none written
  uint8_t mutate = 0;               // % of pattern changes (seeded)
  uint32_t seed = 1;
};

// Writes the arp into track spec.dest of dst over steps sel.s0..s1 (clamped to the pattern length):
// note, velocity and the arp's fx (GAT TIE SLD NDG RAT PRB CHD ARS ARP) of the range are replaced,
// other fx stay. Held notes: the chord (CHORD) or, per step, the notes of src tracks sel.t0..t1 with
// their CHD (SELECTION; a step with notes replaces the held chord, OFF alone releases it). src may
// be dst's pre-fill copy; SELECTION reads src from step 0, so chords set before s0 hold at s0.
// drumTracks[t]: never a source; a drum dest writes nothing (false). Callers must pass it whenever
// the selection may hold drum tracks (a drum step's note field is not a pitch).
bool applyArp(const Pattern& src, Pattern& dst, const Sel& sel, const ArpSpec& spec, const ArpPattern& pat,
              uint8_t root, ScaleType scale, const bool* drumTracks = nullptr);

// GAT value of a gate percentage (see gatePercent): 1..100 exact, above in 7 % units, max 800 %.
uint8_t gateValue(int pct);

// A track's steps s0..s1 (at most kArpPatMax) as a pattern: notes (velocity in thirds of the
// range's spread, NORM when it is under 6 or the velocity is the track's), GAT under 35 % short,
// 90 % and up long, SLD slide; empty steps after a TIE note tie, everything else rests. The same
// pitch as before repeats, the range's lowest note is the root. False: no note in the range.
bool captureArp(const Pattern& p, int track, int s0, int s1, ArpPattern& out);

}  // namespace mt
