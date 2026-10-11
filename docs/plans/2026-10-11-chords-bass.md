# FILL CHORDS / BASS Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task.

**Goal:** FILL gets two more types: CHORDS (a chord progression written as root + CHD on one track)
and BASS (a bass line following the harmony), plus 27 new CHD chord types and CHD inversions.

**Architecture:** Pure core modules `chord_gen` and `bass_gen` in `lib/core/src` with native Unity
tests, sharing the arp token grammar and write helpers extracted from `arp_gen`. FillDialog gains a
`FillType` (FILL / ARP / CHORDS / BASS); the CHORDS / BASS rows live in a second translation unit.
User files: `/presets/PRG/*.prg`, `/presets/BAS/*.bas` through a generic text store.

**Tech Stack:** C++17, PlatformIO (`native` env for tests, `wt32` for the device), Unity.

Design: `docs/plans/2026-10-11-chords-bass-design.md` (read it first: all formats and semantics).

**Rules for every task:**
- Match the surrounding style: 2-space indent, 120 cols, short English comments only where the code
  is not obvious, `namespace mt` in core, `namespace ui` / `storage` elsewhere.
- No commits (project rule: commit everything only after the device check, Task 9).
- Test command: `pio test -e native -f <suite>`; full: `pio test -e native`. Build: `pio run -e wt32`.

---

### Task 1: CHD — new chord types and inversions

**Files:**
- Modify: `lib/core/src/scale.h`, `lib/core/src/scale.cpp`
- Modify: `lib/core/src/fx_info.cpp:23` (CHD max), `:164` (CHD format)
- Modify: `src/ui/fill_dialog.cpp:102-103` (ARP Chord row: format + range)
- Test: `test/test_scale/test_main.cpp`, `test/test_fx_info/*`

**API (scale.h):**
```cpp
// CHD value: inv * kChordTypes + type, inv 0..3 (inv k: the k lowest notes up an octave).
enum : uint8_t {
  kChordTriad, kChordSeventh, kChordSus2, kChordSus4, kChordSixth, kChordAdd9,
  kChordPower, kChordOctave,
  kChordNinth, kChordSeventhSus4, kChordAdd11,                       // diatonic
  kChordMaj, kChordMin, kChordDim, kChordAug, kChordSus2Fix, kChordSus4Fix,
  kChordMaj7, kChordMin7, kChordDom7, kChordHalfDim, kChordDim7, kChordMinMaj7,
  kChordMaj6, kChordMin6, kChordMaj9, kChordMin9, kChordDom9, kChordDom7Sus4,
  kChordMajAdd9, kChordMinAdd9, kChordDom7Sharp9, kChordDom7Flat9, kChordDom13,
  kChordMin11, kChordQuartal,
  kChordTypes
};
constexpr int kChordInversions = 4;
constexpr int kChordValues = kChordTypes * kChordInversions;  // CHD range 0 .. kChordValues - 1
inline uint8_t chordType(uint8_t v) { return v % kChordTypes; }
inline uint8_t chordInv(uint8_t v) { return v / kChordTypes; }
inline uint8_t chordValue(uint8_t type, uint8_t inv) { return inv * kChordTypes + type; }
const char* chordName(uint8_t chord);           // the type's 3 chars ("tri", "MA7"); "???" out of range
void chordLabel(uint8_t chord, char out[5]);    // name + inversion digit: "MA7 ", "tri2"
// Name -> type (exact, case-sensitive: "tri" vs "MAJ"); -1 when unknown.
int chordTypeByName(const char* s, int len);
```
`kChordCount` is removed; replace its uses (`fx_info.cpp`, `fill_dialog.cpp`) with
`kChordValues`. Keep `"???"` for values >= `kChordValues`.

Names (index order): `tri 7th su2 su4 6th ad9 pwr oct 9th 7s4 a11 MAJ MIN DIM AUG SU2 SU4 MA7 MI7
DOM HDM DM7 MMA MA6 MI6 MA9 MI9 DO9 7S4 AD9 MD9 D7# D7b D13 M11 QRT`.

Notes:
- Diatonic degrees (existing table format, scale degrees, -1 end): `9th {0,2,6,8}`,
  `7s4 {0,3,4,6}`, `a11 {0,2,4,10}`.
- Fixed semitone tables: MAJ 0 4 7, MIN 0 3 7, DIM 0 3 6, AUG 0 4 8, SU2 0 2 7, SU4 0 5 7,
  MA7 0 4 7 11, MI7 0 3 7 10, DOM 0 4 7 10, HDM 0 3 6 10, DM7 0 3 6 9, MMA 0 3 7 11, MA6 0 4 7 9,
  MI6 0 3 7 9, MA9 0 4 11 14, MI9 0 3 10 14, DO9 0 4 10 14, 7S4 0 5 7 10, AD9 0 4 7 14,
  MD9 0 3 7 14, D7# 0 4 10 15, D7b 0 4 10 13, D13 0 4 10 21, M11 0 3 10 17, QRT 0 5 10 15.
  Fixed chords ignore root / scale.
- `chordNotes(note, chord, ...)`: build the type's notes (as now; notes > 127 dropped), then apply
  the inversion: repeat `inv` times: take the lowest note, +12 (dropped if > 127), keep sorted
  ascending. Output sorted ascending. Update the comment ("root first" no longer true with inv).
- `fx_info.cpp:164`: `case Fx::CHD: chordLabel(v, out); return;` (out has 5 bytes; check signature).

**Tests (test_scale), write first, see them fail:**
- old values 0..7 give exactly the old notes (existing tests stay).
- `chordNotes(60, kChordNinth, 0, Major)` = 60 64 71 74; `kChordAdd11` = 60 64 67 77.
- each fixed type at note 60 equals its semitone table (+60); in Minor and Chromatic too.
- inversion: `chordValue(kChordMaj, 1)` at 60 = 64 67 72; inv 2 = 67 72 76; inv 3 of a triad = 72 76 79;
  `chordValue(kChordMaj7, 3)` at 60 = 71 72 76 79.
- note near 127 drops notes, never overflows; `chordLabel(chordValue(kChordMaj7,0))` = "MA7 ",
  inv 2 = "MA72"; `chordLabel(200)` = "??? " or "???" (pick one, test it).
- `chordTypeByName("MA7",3) == kChordMaj7`, `("tri",3) == kChordTriad`, `("xyz",3) == -1`.
- fx_info test: CHD max == kChordValues - 1 (if such a test exists, adjust).

Run `pio test -e native -f test_scale -f test_fx_info -f test_expand -f test_arp -f test_synth`: PASS.

---

### Task 2: Shared generator helpers (refactor of arp_gen, no behavior change)

**Files:**
- Create: `lib/core/src/gen_util.h`, `lib/core/src/gen_util.cpp`
- Modify: `lib/core/src/arp_gen.cpp`, `lib/core/src/arp_gen.h` (only if a declaration moves)
- Test: `test/test_arp/test_arp.cpp` must pass unchanged.

**API (gen_util.h, namespace mt):**
```cpp
int genClamp(int v, int lo, int hi);
bool genFx(Fx f);                         // the arp's fx: GAT TIE SLD NDG RAT PRB CHD ARS ARP
bool putFx(Step& s, Fx f, uint8_t v);     // the slot holding f, else the first free one
void clearGenRange(Step* track, int s0, int s1);  // note empty, vel default, genFx slots cleared
// One step token at p (p moves past it and its trailing blanks are NOT skipped). Reads the kind
// char (x X o . -), every following modifier char that is not ':' / blank into mods (cap incl. '\0',
// extra chars dropped), then ':vel:gate:nudge'. Applies s / l / ~ itself (and removes them from
// mods). False: unknown kind char (p is moved past the token, caller skips it).
bool parseGenToken(const char*& p, ArpStep& a, char* mods, int cap);
// Appends one step: kind char, s / l / ~, then mods, then ':vel:gate[:nudge]' when set.
// Returns false when out of space. n: current length, updated.
bool formatGenToken(const ArpStep& a, const char* mods, char* out, int cap, int& n);
// Velocity / gate as applyArp computes them (ghost = lo, accent = hi, norm = mid, velPct exact;
// gatePct x gate / 50 x rate, else short / norm / long x rate).
uint8_t genVelocity(const ArpStep& a, int velLo, int velHi);
int genGatePct(const ArpStep& a, int gate, int rate);
void mutateStep(ArpStep& a, int amount, Rng& r);  // moved as is
bool isBlankChar(char c);
```
`parseArpPattern` / `formatArpPattern` / `applyArp` use these; arp keeps interpreting the mods
`r p ^ v`. Gate math in `applyArp` replaced by `genGatePct`, velocity by `genVelocity`.

**Steps:** move code, build `pio test -e native -f test_arp -f test_fill`: PASS unchanged. No new
test needed beyond a small `test_gen_util` (optional): parse / format round trip of
`"Xs~^:100:50:-3"` with mods `"^"`.

---

### Task 3: chord_gen core

**Files:**
- Create: `lib/core/src/chord_gen.h`, `lib/core/src/chord_gen.cpp`
- Test: `test/test_chords/test_main.cpp`

**API (chord_gen.h):**
```cpp
constexpr int kProgMax = 16;
struct ProgChord {
  int8_t degree = 0;      // 0..6
  int8_t shift = 0;       // -1 'b', +1 '#'
  int8_t type = -1;       // CHD type, -1 = from ChordSpec::type (diatonic)
  uint8_t len = 1;        // units of Length
};
struct Progression {
  uint8_t n = 0;
  bool steps = false;     // '@steps' first token: len counts steps (Length = 1)
  ProgChord c[kProgMax];
};
bool parseProgression(const char* text, Progression& out);  // false: no chord
bool formatProgression(const Progression& p, char* out, int cap);
constexpr int kProgTextMax = kProgMax * 10 + 8;

int progFactoryCount();  const char* progFactoryName(int i);  const char* progFactoryText(int i);
int chordRhythmCount();  const char* chordRhythmName(int i);  const char* chordRhythmText(int i);

enum class ChordFlavor : uint8_t { Triad, Seventh, Ninth, Sus, Count };   // tri 7th 9th su4
enum class ChordVoicing : uint8_t { Root, Close, Count };
const char* chordFlavorName(ChordFlavor f);
const char* chordVoicingName(ChordVoicing v);

struct ChordSpec {
  uint8_t prog = 0;          // UI: factory, user, then RANDOM (the UI passes the resolved Progression)
  bool random = false;
  uint8_t chords = 4;        // RANDOM: 2..8
  uint8_t octave = 4;        // roots around C<octave> (MIDI 12 * (octave + 1)); 2..5
  ChordFlavor flavor = ChordFlavor::Triad;
  ChordVoicing voicing = ChordVoicing::Close;
  uint8_t length = 0;        // steps per unit, 0 = AUTO
  uint8_t dest = 0;
  uint8_t rhythm = 0;        // factory rhythm index
  uint8_t gate = 50;
  uint8_t velLo = 60, velHi = 110;
  uint8_t mutate = 0;
  uint32_t seed = 1;
};

// A chord over steps [start, start + steps).
struct ChordSeg { uint8_t root; uint8_t chd; uint8_t start, steps; };
// The progression laid over n steps (n <= kMaxSteps): RANDOM draws spec.chords chords first
// (randomProgression), Mutate swaps relatives, Voicing picks inversions. Returns the segment count.
int layoutChords(const Progression& prog, const ChordSpec& spec, int n, uint8_t root, ScaleType scale,
                 ChordSeg* out /* kProgMax */);
void randomProgression(int count, uint32_t seed, ScaleType scale, Progression& out);
// Writes onto track spec.dest over sel.s0..s1. False: drum dest / no chord.
bool applyChords(Pattern& dst, const Sel& sel, const ChordSpec& spec, const Progression& prog,
                 const ArpPattern& rhythm, uint8_t root, ScaleType scale, const bool* drumTracks = nullptr);
// Dest steps s0..s1 as a progression ('@steps', lengths in steps). False: no note.
bool captureChords(const Pattern& p, int track, int s0, int s1, uint8_t root, ScaleType scale, Progression& out);
```

**Semantics (from the design, exact):**
- Token: `[b|#]<1-7>[<chord name>][*N]`; `@steps` allowed as the first token. Unknown tokens skipped.
- Root of a chord: shift 0 → `degreeNote(degree, root, scale, base)`; shift ±1 → Roman numeral:
  `base + root + kMajor[degree] + shift` (kMajor = 0 2 4 5 7 9 11, any scale), base = `12 * (octave + 1)`;
  then folded into `[base, base + 11]` octave window. Type: token type if set; else if `shift != 0`
  → `kChordMaj`; else flavor's diatonic type (`tri 7th 9th su4`).
- Unit length: `length` steps, AUTO (0): `n / sum(len)` (min 1). Last segment extends to the range
  end. Segments past the range are dropped; a too-short range cuts the progression.
- Voicing Close: first chord inv 0; each next chord: the inversion 0..(notes-1) whose sorted notes
  minimize `sum |a_i - b_i|` against the previous chord's notes (compare the first min(count) notes;
  ties → lower inv). Note: the root note on the step stays the chord root; only CHD's inv changes.
- RANDOM (`randomProgression`): major-ish scales (Major, Lydian, Mixolydian, PentMaj, Chromatic) use
  degrees I ii iii IV V vi; minor-ish use i iv v VI(b6 → degree 5) VII(degree 6) III(degree 2), all
  diatonic (no shift). Markov over T={I,vi,iii} S={IV,ii} D={V,vii}: first = I; next drawn with
  weights T→S 0.5, T→D 0.3, T→T 0.2; S→D 0.6, S→T 0.2, S→S 0.2; D→T 0.8, D→S 0.2; never the same
  degree twice in a row; the last chord is drawn from {V, IV} (minor: {v, iv, VII}). Rng from
  `rng.h`, seeded with `seed`.
- Mutate (seeded, separate Rng stream): per chord with probability mutate %: swap relative
  (I↔vi, ii↔IV, iii↔V by degree 0↔5, 1↔3, 2↔4) and, if type is -1, flavor drawn anew. Rhythm steps
  mutated by `mutateStep` (pitch changes ignored).
- Write: `clearGenRange` first. Rhythm restarts at each segment start (`rhythm.steps[j % len]`).
  Note step: note = root (+12 * oct mod from `^` / `v` mods), CHD = chd, vel = `genVelocity`,
  GAT = `gateValue(genGatePct(a, gate, 1))`. Tie `-`: TIE on the last note step (once). A rest
  after a tie, or a new segment start while tied → OFF on the step that ends it (a new chord note
  simply replaces: put OFF only if the step is a rest). Tie running to the range end → long GAT over
  the remaining range (max 800 %) and TIE removed (exactly like applyArp).
- Capture: walk Dest s0..s1; a step with a note starts a new chord when note or CHD differs from the
  current one; lengths in steps between chord starts (the first chord starts at the first note);
  degree: the scale degree of (note - root) if in scale (via `degreeNote` inverse: find degree d with
  `degreeNote(d,...) % 12 == note % 12`), shift 0, type = CHD type (no CHD → MAJ / MIN chosen by
  whether the scale's third above the note is 4 or 3 semitones, then stored as fixed type); not in
  scale: Roman numeral relative to major: the degree d with `kMajor[d] - 1 == pc` → degree d, shift -1
  (flats first), else `kMajor[d] + 1 == pc` → shift +1; type explicit (no CHD → MAJ). `steps = true`. Max kProgMax chords.

**Factory content (in chord_gen.cpp):**
- Progressions (name ≤ 10 chars, text): `POP 1564 "1 5 6 4"`, `POP 6415 "6 4 1 5"`,
  `POP 1645 "1 6 4 5"`, `POP 1465 "1 4 6 5"`, `JAZZ 251 "2MI7 5DOM 1MA7*2"`,
  `JAZZ 1625 "1MA7 6MI7 2MI7 5DOM"`, `JAZZ 3625 "3MI7 6MI7 2MI7 5DOM"`, `MIN 1636 "1 b6 b3 b7"`,
  `MIN 1767 "1 b7 b6 b7"`, `ANDALUS "1MIN b7 b6 5MAJ"`, `MIN 1451 "1 4 5 1"`,
  `DEEP 2 "1MI7 4MI7"`, `DEEP 1MI9 "1MI9 b7MA7"`, `DEEP 21 "2MI7 1MA7"`, `TRNC EPIC "1 b6 b3 b7"`,
  `TRNC 6415 "6 4 1 5"`, `TRNC 1671 "1 b6 b7 1"`, `SW 1671 "1MIN b6 b7 1MIN"`, `SW 6455 "6 4 5 5"`,
  `DUB 17 "1 b7"`, `BLUES 12 "1DOM*4 4DOM*2 1DOM*2 5DOM 4DOM 1DOM 5DOM"`, `ROCK 1741 "1 b7 4 1"`,
  `ROCK 1454 "1 4 5 4"`, `DORIAN 14 "1MIN 4MAJ"`, `PHRYG 12 "1MIN b2MAJ"`, `LYDIAN 12 "1MAJ 2MAJ"`,
  `GOSPEL "1 4 1 5 6 4 1 5"`, `CANON "1 5 6 3 4 1 4 5"`, `AXIS "1 5 6 4 1 5 4 4"`,
  `HOUSE 4 "1MI7 b6MA7 b7DOM 1MI7"`.
- Rhythms (16 steps, ARP tokens): `HOLD "xl - - - - - - - - - - - - - - -"`,
  `HALF "xl - - - - - - - xl - - - - - - -"`, `QUARTER "x . . . x . . . x . . . x . . ."`,
  `8THS "x . x . x . x . x . x . x . x ."`, `STAB "Xs . . . . . . . Xs . . . . . . ."`,
  `OFFBEAT ". . Xs . . . Xs . . . Xs . . . Xs ."`, `CHARLSTN "X . . x . . . . X . . x . . . ."`,
  `HOUSE PNO ". . X . . x . . . . X . . x . ."`, `DUB SKNK ". . . . Xs . . . . . . . Xs . . ."`,
  `REGGAE ". . Xs . . . Xs . . . Xs . . . Xs ."` (same as offbeat but short),
  `TRNC GATE "X xs xs x X xs xs x X xs xs x X xs x xs"`, `SYNCOPA "X . . x . . x . . . X . . x . ."`,
  `PUSH "xl - - - - - - - - - - - - - - xl"`, `FUNK "X . o x . o X . . x . o X . x ."`,
  `POP 8 "X . x . x . x x X . x . x . x x"`, `BALLAD "xl - - - - - x - xl - - - - - x -"`.
  Make REGGAE differ from OFFBEAT: `". . Xs o . . Xs o . . Xs o . . Xs o"`.

**Tests (test_chords), TDD — write, see fail, implement, pass:**
1. parse `"2MI7 5DOM 1MA7*2"`: n 3, degrees 1 4 0, types MI7 DOM MA7, len 1 1 2; `"b7 #4 1"`: shifts.
2. parse `"@steps 1*8 4*8"`: steps true; format round-trips both texts canonically.
3. every factory progression and rhythm parses (`n > 0`, rhythm len 16).
4. layout C major, octave 4, Length 4, `"1 5 6 4"` over 16 steps: roots 60 67 69 65 (folded into
   60..71), starts 0 4 8 12, chd types tri.
5. AUTO: 3 chords over 16 → unit 5, last extends to 16 (5, 5, 6).
6. `b7` in C major → root 70, type MAJ; in C minor `b6` → 68 (Ab, MAJ) and plain `6` → 68 with a
   diatonic type; `#4` in C major → 66.
7. Voicing Close: `"1 4"` triads in C major: second chord F major gets inv 2 (C F A = 60 65 69 vs
   60 64 67): assert inv 2; Root voicing: inv 0.
8. randomProgression deterministic per seed; first degree 0; no repeats; count respected; differs
   for two seeds (find two seeds that differ, assert).
9. applyChords HOLD rhythm over 16 with Length 8: step 0 note + CHD + TIE, step 8 note + CHD + TIE
   → the tie at the range end becomes GAT (TIE removed); step 7 has no OFF (new chord follows).
10. QUARTER rhythm: notes at 0 4 8 12, GAT = gateValue(50), velocities = (lo+hi)/2.
11. other fx (e.g. FLT on a step) survive; drum dest returns false and writes nothing.
12. captureChords round trip: apply `"1 4 5 1"` with Length 4 / HOLD, capture → `@steps`, 4
    chords, lengths 4, degrees 0 3 4 0.

---

### Task 4: bass_gen core

**Files:**
- Create: `lib/core/src/bass_gen.h`, `lib/core/src/bass_gen.cpp`
- Test: `test/test_bass/test_main.cpp`

**API (bass_gen.h):**
```cpp
enum class BassRole : uint8_t { Root, Third, Fifth, Seventh, Approach, Walk, Repeat };
struct BassPattern {
  uint8_t len = 0;
  ArpStep steps[kArpPatMax];   // kind / acc / len / slide / exact fields; pitch fields unused
  BassRole role[kArpPatMax];
  int8_t oct[kArpPatMax];      // -1, 0, +1
};
bool parseBassPattern(const char* text, BassPattern& out);   // mods: 1 3 5 7 a w r ^ v
bool formatBassPattern(const BassPattern& p, char* out, int cap);
constexpr int kBassTextMax = kArpTextMax;
int bassFactoryCount(); const char* bassFactoryName(int i); const char* bassFactoryText(int i);

enum class BassSource : uint8_t { Root, Selection, Progression, Count };
const char* bassSourceName(BassSource s);  // "ROOT", "SELECTION", "PROGRESSION"
struct BassSpec {
  BassSource source = BassSource::Root;
  uint8_t root = 36;        // ROOT: the tonic note (Root row)
  uint8_t octave = 2;       // chord roots folded into C<octave> .. B<octave>, 1..3
  uint8_t dest = 0;
  uint8_t pattern = 0;
  uint8_t rate = 1, rotate = 0, gate = 50, swing = 0;
  uint8_t velLo = 70, velHi = 120, slide = 16, mutate = 0;
  uint32_t seed = 1;
};
// Chords per step: PROGRESSION passes segs (layoutChords output, relative to sel.s0), else nullptr.
bool applyBass(const Pattern& src, Pattern& dst, const Sel& sel, const BassSpec& spec, const BassPattern& pat,
               uint8_t root, ScaleType scale, const ChordSeg* segs, int segN, const bool* drumTracks = nullptr);
bool captureBass(const Pattern& p, int track, int s0, int s1, const BassSpec& spec, uint8_t root, ScaleType scale,
                 const ChordSeg* segs, int segN, const Pattern* src, const Sel* sel, BassPattern& out);
```

**Semantics:**
- Current chord at a step (notes via `chordNotes`, sorted; its root = the chord's root note, not
  the lowest after inversion: for SELECTION the lowest note of the step's tracks + its CHD as in
  applyArp (OFF releases → silence), for PROGRESSION the segment covering the step (root + chd), for
  ROOT `spec.root` + `kChordTriad` the whole range). Root folded into `[12*(octave+1), +11]`.
- "Next chord root": the root of the next chord change after the current step (PROGRESSION: next
  segment; SELECTION: scan src forward to the next step with notes in sel; ROOT / none: the first
  chord's root, i.e. wraps to the start).
- Roles: Root = root; Third / Fifth / Seventh = chord tone index 1 / 2 / 3 of the root-position chord
  (`chordNotes` with inv 0), missing → `moveDegrees(root, 2/4/6, ...)`; Approach = next root - 1;
  Walk = `moveDegrees(prev, ±1)` toward the next root (prev = last written note, else root; equal →
  root); Repeat = previous written note (else root). Then `+12 * oct`, clamp 0..127.
- Rhythm / write: exactly applyArp's loop with `rate`, `rotate`, swing, nudge, slide, tie / OFF,
  tie-to-end, mutate (`mutateStep`; when a pitch mutation fires redraw the role from {Root, Fifth,
  Third, oct +1} with the same draw count). Use gen_util helpers. No CHD written.
- Capture: like captureArp (≤ 32 steps, velocity thirds, GAT short/long, SLD, TIE), role per note
  against the step's chord: root / third / fifth / seventh pitch-class match (with oct = (note -
  chordtone) / 12 clamped to ±1), else note == next root - 1 → Approach, else same as previous →
  Repeat, else nearest chord tone (Root / Third / Fifth).

**Factory (≥ 24, 16 steps unless noted):**
`HOUSE OFF ". . x . . . x . . . x . . . x ."`, `DISCO OCT "x . x^ . x . x^ . x . x^ . x . x^ ."`,
`TECHNO RL ". x x x . x x x . x x x . x x x"`, `TECHNO 16 "X x x x X x x x X x x x X x x x"`,
`ACID 1 "X x~ x5 . X x~ X3 x . X~ x . x5 X~ x"`, `ACID 2 "x^ x X~ x5 . x x^~ x X . x~ x7 x X x^ ."`,
`ACID 3 "Xr x~ x . X~ xv x . Xr x x~ x . X x~ x5"`, `DNB "Xl - . . . . x . . . Xl - . . x ."`,
`REESE "xl - - - - - - - x5l - - - - - - -"`, `DUB "Xl - - . . . x5 . Xl - - . . . x ."`,
`REGGAETN "X . . x . . x . X . . x . . x ."`, `SYNTHW 8 "x . x . x . x . x . x . x . x a"`,
`ELECTRO "X . . x . . X . . x . x X . x ."`, `FUNK "X . x^ . . x . x X . . x^ . x7 x ."`,
`WALKING "x . . . x3 . . . x5 . . . xa . . ."`, `ROCK 8 "x x x x x x x x x x x x x x x x"`
(all root, 8ths at Rate 2 is the user's choice — keep 16ths),
`TRNC OFF ". . X . . . X . . . X . . . X ."`, `PSY ". x x x . x x x . x x x . x x x"` with
accents `". X x x . X x x . X x x . X x x"`, `UKG "X . . x . . . x . . X . . . x ."`,
`BOOGIE "x . x3 . x5 . x6 . x^ . x6 . x5 . x3 ."` (`6` is not a role: use `x5^`… keep roles valid:
`"x . x3 . x5 . x7 . x^ . x7 . x5 . x3 ."`), `OCT JUMP "x x^ x x^ x x^ x x^ x x^ x x^ x x^ x x^"`,
`ROOT 5TH "x . . . x5 . . . x . . . x5 . . ."`, `APPROACH "xl - - - - - - - - - - - - - xa ."`,
`TECHNO OFF ". . x . . x . . . . x . . x . ."`.

**Tests (test_bass), TDD:**
1. parse / format round trip `"X5^ xa xw~ xr . - x7v:90:40"`; old ARP-only tokens parse.
2. all factory patterns parse, len > 0.
3. ROOT source, C major, root 36: pattern `"x x3 x5 x7"` → 36 40 43 47.
4. PROGRESSION segs C(0..7) F(8..15) octave 2: `"x . . . . . . xa"` repeated: step 7 = 40 (F 41 - 1),
   step 15 approaches the first chord → 35.
5. Walk: `"x . . . . . . xw"` C→F: step 7 = moveDegrees(36, +1) = 38.
6. SELECTION: src track holds C3+CHD tri at step 0 and A2 at step 8; bass follows; OFF at step 12
   silences (OFF written if tied, nothing otherwise).
7. Rate 2 / rotate / swing NDG, tie-to-end GAT, slide SLD: one combined case each like test_arp.
8. mutate deterministic by seed; 0 mutate = pattern unchanged.
9. capture round trip of an applied pattern (ROOT source) → same roles / kinds.

---

### Task 5: Storage for user progressions and bass patterns

**Files:**
- Create: `src/storage/text_store.h`, `src/storage/text_store.cpp`
- Modify: `src/storage/arp_store.{h,cpp}` (use text_store; API unchanged)
- Create: `src/storage/prog_store.{h,cpp}`, `src/storage/bass_store.{h,cpp}`

**API:**
```cpp
// text_store.h: one-text-file-per-name folders under /presets. UI task only.
int listTexts(const char* dir, const char* ext, char (*names)[hw::kNameMax], int max);
Result loadText(const char* dir, const char* name, const char* ext, char* out, int cap);  // '\0'-terminated
Result saveText(const char* dir, const char* name, const char* ext, const char* text);   // tmp / bak swap
```
Move arp_store's path / tmp / bak logic into saveText unchanged. prog_store: `kProgDir =
"/presets/PRG"`, `.prg`, `listProgs`, `loadProg(name, mt::Progression&)`, `saveProg`. bass_store:
`kBassDir = "/presets/BAS"`, `.bas`, `listBass`, `loadBass(name, mt::BassPattern&)`, `saveBass`.
Verification: `pio run -e wt32` compiles (no native tests: Arduino FS).

---

### Task 6: FillDialog — FillType, CHORDS and BASS rows

**Files:**
- Modify: `lib/core/src/fill.h` (`bool arp` → `FillType type`), `test/test_fill/*` if it touches `arp`
- Modify: `src/ui/fill_dialog.h`, `src/ui/fill_dialog.cpp`
- Create: `src/ui/fill_dialog_gen.cpp` (CHORDS / BASS param init, rows, resolve, capture)
- Modify: `src/ui/grid_screen.h:150-153`, `src/ui/grid_screen.cpp:739-780` (ChordSpec / BassSpec, defaults)

**Changes:**
- `fill.h`: `enum class FillType : uint8_t { Fill, Arp, Chords, Bass, Count };` and
  `FillType type = FillType::Fill;` replacing `arp`. Type row: `FILL / ARP / CHORDS / BASS` via
  `stepEnum`. Everywhere `f_->arp` → `f_->type == mt::FillType::Arp`.
- `open(...)` gains `mt::ChordSpec* c, mt::BassSpec* b`. GridScreen keeps `chordSpec_`, `bassSpec_`;
  on open: `chordSpec_.dest = bassSpec_.dest = sel.t0` (like arp), `bassSpec_.root` from
  `lastNote_[tr]` folded to octave 2 on first init.
- New Row ids (before kReseed): CHORDS `kCProg, kCChords, kCOctave, kCFlavor, kCVoicing, kCLength,
  kCDest, kCRhythm, kCGate, kCVelLo, kCVelHi, kCMutate, kCCapture`; BASS `kBSource, kBRoot,
  kBOctave, kBDest, kBPattern, kBRate, kBRotate, kBGate, kBSwing, kBVelLo, kBVelHi, kBSlide,
  kBMutate, kBCapture`. `buildRows` shows per type; Seed / Reseed when CHORDS RANDOM or mutate, BASS
  mutate. BASS Root only for ROOT source. kCChords only for RANDOM. Length shows `AUTO` for 0.
  Dest rows red on a drum track (as kADest).
- Progression list: factory → user (`listProgs`, up to kUserMax, read on first CHORDS show) →
  `RANDOM` last. Bass pattern list: factory → user. Resolve outside the project lock (as syncArp);
  separate `progUser_` / `bassUser_` name arrays (allocated on first open like `userNames_`) and
  resolved caches (`prog_`, `bassPat_`, `rhythmPat_`).
- `preview()`: CHORDS → `applyChords(p.patterns[pattern_], ...)` (touched = dest); BASS →
  `layoutChords` from the current chord spec + resolved progression when source PROGRESSION, then
  `applyBass(*saved_, ...)` (touched = dest).
- Capture rows: keyboards "PROG NAME:" / "BASS NAME:", `captureChords` / `captureBass` from
  `*saved_`, save, relist, select, preview (mirror `capture`). Toasts `NO NOTES`, `SAVED`, `LIST FULL`.
- Header line: `CHORDS T%d %d-%d` / `BASS T%d %d-%d` (+ `< T..` for SELECTION source).
- `ok()` toast: `"CHORDS"` / `"BASS"`. `seed()` returns the current type's seed.
- Touch handling: Capture rows act like kACapture.

Verification: `pio run -e wt32` builds; `pio test -e native` all PASS.

---

### Task 7: Docs

**Files:** `README.md`, `docs/manual.md`, `docs/manual_ru.md`
- CHD: the new types table (both groups), inversion digit, fixed vs diatonic (UPPER vs lower case).
- FILL: Type CHORDS (progression token syntax, rows, RANDOM, Voicing, rhythms, user files
  `/presets/PRG`, Capture) and BASS (sources, role tokens, rows, factory, `/presets/BAS`, Capture).
- Match the style and depth of the existing ARP sections.

---

### Task 8: Final verification

- `pio test -e native` → all suites PASS (paste summary).
- `pio run -e wt32` → SUCCESS.
- Code review of the whole diff (requesting-code-review).

### Task 9: Device check (user) and commit

Hardware checklist for the user:
1. CHD on a step: scroll through all values, labels `MA7 `, `MA71`…; chords sound right, inversions.
2. FILL → Type CHORDS: POP 1564 / HOLD over 16 / 64 steps; Voicing CLOSE vs ROOT audible; RANDOM +
   Reseed; Capture → appears in the list after factory; OK / Cancel / undo.
3. FILL → Type BASS: ROOT / SELECTION (over the chord track) / PROGRESSION; ACID 1 slides; Capture.
4. ARP unchanged (spot-check a factory and a VIRUS pattern).
After the user confirms: one commit of everything (design, plan, code, docs).
