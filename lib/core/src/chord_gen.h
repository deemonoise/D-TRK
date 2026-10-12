#pragma once
#include <stdint.h>
#include "arp_gen.h"
#include "edit_ops.h"
#include "model.h"
#include "scale.h"

namespace mt {

// Chords generator of FILL: a progression laid over a range of one track as chord roots with CHD,
// played by a rhythm (ARP pattern tokens). Text form, one token per chord: '[b|#]<1-7>[name][*N]'.
// Plain degrees follow the project scale (degreeNote); 'b' / '#' / 'n' are Roman numerals: the major
// scale's degree from scale root, a semitone down / up / natural, in any scale. name: a CHD type
// ("MI7", "tri"), else the Type parameter's diatonic chord (Roman without name: MAJ). '*N': N units
// of Length.
// '@steps' as the first token: lengths count steps (Length = 1).
constexpr int kProgMax = 16;
struct ProgChord {
  int8_t degree = 0;  // 0..6
  int8_t shift = 0;   // -1 'b', +1 '#'
  bool roman = false;  // 'b' / '#' / 'n': degree of the major scale, not the project scale
  int8_t type = -1;   // CHD type, -1 = from ChordSpec::flavor (diatonic)
  uint8_t len = 1;    // units of Length
};
struct Progression {
  uint8_t n = 0;
  bool steps = false;  // '@steps' first token: len counts steps (Length = 1)
  ProgChord c[kProgMax];
};
// False: no chord. Unknown tokens are skipped, chords past kProgMax ignored.
bool parseProgression(const char* text, Progression& out);
// Canonical text (tokens joined by ' ', '*1' left out). False when cap is too small.
bool formatProgression(const Progression& p, char* out, int cap);
// Longest token "#7MA7*128" + blank, plus "@steps ".
constexpr int kProgTextMax = kProgMax * 10 + 8;

int progFactoryCount();
const char* progFactoryName(int i);  // up to 10 chars; nullptr out of range
const char* progFactoryText(int i);  // nullptr out of range
int chordRhythmCount();
const char* chordRhythmName(int i);  // up to 10 chars; nullptr out of range
const char* chordRhythmText(int i);  // ARP pattern tokens, 16 steps; nullptr out of range

enum class ChordFlavor : uint8_t { Triad, Seventh, Ninth, Sus, Count };  // tri 7th 9th su4
enum class ChordVoicing : uint8_t { Root, Close, Count };
const char* chordFlavorName(ChordFlavor f);    // "TRIAD", "7TH", "9TH", "SUS"
const char* chordVoicingName(ChordVoicing v);  // "ROOT", "CLOSE"

struct ChordSpec {
  uint8_t prog = 0;  // UI: factory, user, then RANDOM (the UI passes the resolved Progression)
  bool random = false;
  uint8_t chords = 4;  // RANDOM: 2..8
  uint8_t octave = 4;  // roots in C<octave>..B<octave> (MIDI 12 * (octave + 1)); 2..5
  ChordFlavor flavor = ChordFlavor::Triad;
  ChordVoicing voicing = ChordVoicing::Close;
  uint8_t length = 0;  // steps per unit, 0 = AUTO
  uint8_t dest = 0;
  uint8_t rhythm = 0;  // factory rhythm index
  uint8_t gate = 50;
  uint8_t velLo = 60, velHi = 110;
  uint8_t mutate = 0;
  uint32_t seed = 1;
};

// A chord over steps [start, start + steps) of the range.
struct ChordSeg {
  uint8_t root;
  uint8_t chd;
  uint8_t start, steps;
};
// The progression laid over n steps (n <= kMaxSteps): RANDOM draws spec.chords chords first
// (randomProgression), Mutate swaps relatives, Voicing picks inversions. Unit = Length steps, AUTO
// n / sum of lengths; the last chord runs to the range end, chords past it are dropped. CLOSE: each
// chord after the first takes, of its inversions at the root or an octave down (not below
// base - 7, base = 12 * (octave + 1)), the one whose notes move least from the previous chord's
// (sum of |a_i - b_i|; ties: the root's octave, then the lower inversion); the segment's root is
// that candidate's. Returns the segment count.
int layoutChords(const Progression& prog, const ChordSpec& spec, int n, uint8_t root, ScaleType scale,
                 ChordSeg* out /* kProgMax */);
// Markov over tonic / subdominant / dominant degrees: starts on I, never repeats a degree, ends on
// V / IV (minor-ish scales: v / iv / VII). Diatonic chords (type -1), deterministic per seed.
void randomProgression(int count, uint32_t seed, ScaleType scale, Progression& out);
// Writes onto track spec.dest over sel.s0..s1: the range's note, velocity and generator fx (see
// clearGenRange) are replaced, other fx stay. The rhythm restarts on every chord: a hit writes the
// root ('^' / 'v' an octave up / down) with CHD, velocity and GAT as ARP; '-' ties, a rest after a
// tie puts OFF, a tie to the range end becomes a long GAT. False: drum dest / no chord.
bool applyChords(Pattern& dst, const Sel& sel, const ChordSpec& spec, const Progression& prog,
                 const ArpPattern& rhythm, uint8_t root, ScaleType scale, const bool* drumTracks = nullptr);
// Track steps s0..s1 as a progression ('@steps', lengths in steps): a new chord where the note or
// CHD changes. In-scale notes are degrees, others Roman numerals (natural, else flat, else sharp); a note without CHD
// is MAJ / MIN by the scale's third (MAJ out of the scale). False: no note.
bool captureChords(const Pattern& p, int track, int s0, int s1, uint8_t root, ScaleType scale, Progression& out);

}  // namespace mt
