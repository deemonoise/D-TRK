#pragma once
#include <stdint.h>
#include "arp_gen.h"
#include "chord_gen.h"
#include "edit_ops.h"
#include "model.h"
#include "scale.h"

namespace mt {

// Bass generator of FILL: an ARP-style rhythm whose notes are roles against the chord of each step
// (a fixed root, the selection's chords or the CHORDS progression), written on one track.
// Text form: ARP tokens; after a note the role '1' root (default), '3' '5' '7' chord tones, 'a' a
// semitone under the next chord's root, 'w' a scale step from the previous note toward it, 'r' the
// previous note; '^' / 'v' an octave up / down. The last role char of a token wins (same for ^ / v).
enum class BassRole : uint8_t { Root, Third, Fifth, Seventh, Approach, Walk, Repeat };
struct BassPattern {
  uint8_t len = 0;
  ArpStep steps[kArpPatMax];  // kind / acc / len / slide / exact fields; pitch fields unused
  BassRole role[kArpPatMax];
  int8_t oct[kArpPatMax];  // -1, 0, +1
};
// False when the text has no step. ARP pitch mods ('p') are ignored; steps past kArpPatMax dropped.
bool parseBassPattern(const char* text, BassPattern& out);
// Canonical text (tokens joined by ' ', root role left out). False when cap is too small.
bool formatBassPattern(const BassPattern& p, char* out, int cap);
constexpr int kBassTextMax = kArpTextMax;

int bassFactoryCount();
const char* bassFactoryName(int i);  // up to 10 chars; nullptr out of range
const char* bassFactoryText(int i);  // nullptr out of range

enum class BassSource : uint8_t { Root, Selection, Progression, Count };
const char* bassSourceName(BassSource s);  // "ROOT", "SELECTION", "PROGRESSION"

struct BassSpec {
  BassSource source = BassSource::Root;
  uint8_t root = 36;    // ROOT: the tonic note (Root row)
  uint8_t octave = 2;   // chord roots folded into C<octave> .. B<octave>, 1..3
  uint8_t dest = 0;     // track written
  uint8_t pattern = 0;  // UI: factory index, then user patterns
  uint8_t rate = 1, rotate = 0, gate = 50, swing = 0;
  uint8_t velLo = 70, velHi = 120, slide = 16, mutate = 0;
  uint32_t seed = 1;
};

// Writes the bass into track spec.dest of dst over sel.s0..s1 like applyArp (rate, rotate, swing,
// nudge, slide, tie / OFF, a tie to the range end as a long GAT, mutate; no CHD). The chord of a
// step: ROOT spec.root with the scale's triad over the whole range; SELECTION the lowest note of
// src tracks sel.t0..t1 with its CHD (triad without one), read from step 0, an OFF alone releases
// (silence); PROGRESSION the segment covering the step (segs relative to sel.s0, from layoutChords).
// Chord roots are folded into octave spec.octave. Roles: 3 / 5 / 7 are the root-position chord's
// tones 1..3 (missing: 2 / 4 / 6 scale degrees up); 'a' / 'w' aim at the next chord's root: the next
// segment (PROGRESSION) or the next chord change in src (SELECTION: a different root / CHD, or a chord
// after an OFF; scanned up to src's pattern end, past s1), none: the range's first chord. 'w' never
// steps past that root. 'r' / 'w' start from the previous note before its octave modifier (else the
// root). A mutated pitch redraws the role from root / 5th / 3rd / root + 12.
// False: no pattern, drum dest, PROGRESSION without segments.
bool applyBass(const Pattern& src, Pattern& dst, const Sel& sel, const BassSpec& spec, const BassPattern& pat,
               uint8_t root, ScaleType scale, const ChordSeg* segs, int segN, const bool* drumTracks = nullptr);

// A track's steps s0..s1 (at most kArpPatMax) as a pattern, rhythm as captureArp; the role of each
// note against the step's chord (spec.source; segs relative to s0; SELECTION needs src and sel,
// else falls back to ROOT; drumTracks as applyBass): root / 3rd / 5th / 7th by pitch class within an
// octave either way (oct = distance / 12), else a semitone under the next root 'a', else the previous
// note's base (before its octave) within an octave 'r', else the nearest of root / 3rd / 5th.
// False: no note in the range.
bool captureBass(const Pattern& p, int track, int s0, int s1, const BassSpec& spec, uint8_t root, ScaleType scale,
                 const ChordSeg* segs, int segN, const Pattern* src, const Sel* sel, BassPattern& out,
                 const bool* drumTracks = nullptr);

}  // namespace mt
