#include "chord_gen.h"
#include <stddef.h>
#include <initializer_list>
#include <stdio.h>
#include <string.h>
#include "gen_util.h"
#include "rng.h"

namespace mt {
namespace {

struct Factory {
  const char* name;
  const char* text;
};
constexpr Factory kProgs[] = {
    {"POP 1564", "1 5 6 4"},
    {"POP 6415", "6 4 1 5"},
    {"POP 1645", "1 6 4 5"},
    {"POP 1465", "1 4 6 5"},
    {"POP 4156", "4 1 5 6"},
    {"POP 1364", "1 3 6 4"},
    {"OLDIES", "1 6 2 5"},
    {"AXIS", "1 5 6 4 1 5 4 4"},
    {"CANON", "1 5 6 3 4 1 4 5"},
    {"ROYAL RD", "4MA7 5DOM 3MI7 6MIN"},
    {"CITY POP", "4MA7 3DOM 6MI7 5MI7 1DOM"},
    {"JAZZ 251", "2MI7 5DOM 1MA7*2"},
    {"JAZZ 1625", "1MA7 6MI7 2MI7 5DOM"},
    {"JAZZ 3625", "3MI7 6MI7 2MI7 5DOM"},
    {"JAZZ TURN", "1MA7 6DOM 2MI7 5DOM"},
    {"JAZZ BACK", "4MA7 b7DOM 1MA7*2"},
    {"MIN 251", "2HDM 5D7b 1MI7*2"},
    {"NEO SOUL", "4MA7 3MI7 2MI7 1MA7"},
    {"SOUL 25", "2MI9 5DO9"},
    {"LOFI", "4MA7 3MI7 6MI7 1MA7"},
    {"GOSPEL", "1 4 1 5 6 4 1 5"},
    {"GOSPEL 2", "1MAJ 4MAJ #4DIM 5MAJ"},
    {"MIN 1636", "1 b6 b3 b7"},
    {"MIN 1767", "1 b7 b6 b7"},
    {"ANDALUS", "1MIN b7 b6 5MAJ"},
    {"MIN 1451", "1 4 5 1"},
    {"DEEP 2", "1MI7 4MI7"},
    {"DEEP 1MI9", "1MI9 b7MA7"},
    {"DEEP 21", "2MI7 1MA7"},
    {"HOUSE 4", "1MI7 b6MA7 b7DOM 1MI7"},
    {"HOUSE 2", "1MI9 b7MA7 b6MA7 b7MA7"},
    {"LIQUID", "1MI9 b6MA7 b3MA7 b7DOM"},
    {"TRNC EPIC", "1 b6 b7 b3"},
    {"TRNC 6471", "6 4 b7 1"},
    {"TRNC 1671", "1 b6 b7 1"},
    {"SW 1671", "1MIN b6 b7 1MIN"},
    {"SW 6455", "6 4 5 5"},
    {"DUB 17", "1 b7"},
    {"DUB 1b6", "1MI7 b6MA7"},
    {"FUNK 14", "1DO9 4DO9"},
    {"BLUES 12", "1DOM*4 4DOM*2 1DOM*2 5DOM 4DOM 1DOM 5DOM"},
    {"BLUES QC", "1DOM 4DOM 1DOM*2 4DOM*2 1DOM*2 5DOM 4DOM 1DOM 5DOM"},
    {"MIN BLUES", "1MI7*4 4MI7*2 1MI7*2 b6DOM 5DOM 1MI7*2"},
    {"ROCK 1741", "1 b7 4 1"},
    {"ROCK 1454", "1 4 5 4"},
    {"MIXO 1574", "1MAJ 5MAJ b7MAJ 4MAJ"},
    {"DORIAN 14", "1MIN 4MAJ"},
    {"DORIAN 12", "1MI7 2MI7"},
    {"PHRYG 12", "1MIN b2MAJ"},
    {"LYDIAN 12", "1MAJ 2MAJ"},
    {"CINEMA", "1MAJ b6MAJ b3MAJ 4MAJ"},
    {"HERO", "1MAJ 3MAJ 4MAJ 4MIN"},
    {"AMBIENT", "1ad9 4ad9 6ad9 5ad9"},
    {"SUS DRIFT", "1SU2 4SU2 5SU4 1SU2"},
    {"QUARTAL", "1QRT 2QRT 4QRT 5QRT"},
};
constexpr int kProgsN = sizeof(kProgs) / sizeof(kProgs[0]);

constexpr Factory kRhythms[] = {
    {"HOLD", "xl - - - - - - - - - - - - - - -"},
    {"HALF", "xl - - - - - - - xl - - - - - - -"},
    {"QUARTER", "x . . . x . . . x . . . x . . ."},
    {"8THS", "x . x . x . x . x . x . x . x ."},
    {"STAB", "Xs . . . . . . . Xs . . . . . . ."},
    {"OFFBEAT", ". . Xs . . . Xs . . . Xs . . . Xs ."},
    {"CHARLSTN", "X . . x . . . . X . . x . . . ."},
    {"HOUSE PNO", ". . X . . x . . . . X . . x . ."},
    {"DUB SKNK", ". . . . Xs . . . . . . . Xs . . ."},
    {"REGGAE", ". . Xs o . . Xs o . . Xs o . . Xs o"},
    {"TRNC GATE", "X xs xs x X xs xs x X xs xs x X xs x xs"},
    {"SYNCOPA", "X . . x . . x . . . X . . x . ."},
    {"PUSH", "xl - - - - - - - - - - - - - - xl"},
    {"FUNK", "X . o x . o X . . x . o X . x ."},
    {"POP 8", "X . x . x . x x X . x . x . x x"},
    {"BALLAD", "xl - - - - - x - xl - - - - - x -"},
    {"UPBEAT16", ". x . x . x . x . x . x . x . x"},
    {"GALLOP", "x . x x x . x x x . x x x . x x"},
    {"REV GALLOP", "x x . x x x . x x x . x x x . x"},
    {"THREE", "x x x . x x x . x x x . x x x ."},
    {"DOUBLE", ". x x . . x x . . x x . . x x ."},
    {"ANTICIP", ". . . x . . . x . . . x . . . x"},
    {"ROLL UP", "o o o X o o o X o o o X o o o X"},
    {"SWELL", "o x X - o x X - o x X - o x X -"},
    {"LONG SHRT", "xl - Xs . xl - Xs . xl - Xs . xl - Xs ."},
    {"SHRT LONG", "Xs . xl - Xs . xl - Xs . xl - Xs . xl -"},
    {"LATE HOLD", ". x - - . x - - . x - - . x - -"},
    {"STUTTER", "X xs . . X xs . . X xs . . X xs . ."},
    {"PNO COMP", "Xl - o . x . o . x . o . x . o ."},
    {"GHOST 3", "x o o x o o x o x o o x o o x o"},
    {"CHOP 3", "x - . x - . x - . x - . x - . x"},
    {"TRESILLO", "X . . x . . x . X . . x . . x ."},
};
constexpr int kRhythmsN = sizeof(kRhythms) / sizeof(kRhythms[0]);

const char* const kFlavorNames[] = {"TRIAD", "7TH", "9TH", "SUS"};
static_assert(sizeof(kFlavorNames) / sizeof(kFlavorNames[0]) == static_cast<size_t>(ChordFlavor::Count), "flavors");
constexpr uint8_t kFlavorTypes[] = {kChordTriad, kChordSeventh, kChordNinth, kChordSus4};
const char* const kVoicingNames[] = {"ROOT", "CLOSE"};
static_assert(sizeof(kVoicingNames) / sizeof(kVoicingNames[0]) == static_cast<size_t>(ChordVoicing::Count),
              "voicings");

constexpr int8_t kMajorSemis[7] = {0, 2, 4, 5, 7, 9, 11};  // Roman numerals: the major scale

// Harmonic functions of RANDOM: tonic, subdominant, dominant.
enum Fn { kT, kS, kD };
int fnOf(int degree) { return degree == 0 || degree == 2 || degree == 5 ? kT : (degree == 1 || degree == 3 ? kS : kD); }
constexpr int kFnWeight[3][3] = {{2, 5, 3}, {2, 2, 6}, {8, 2, 0}};  // [from][to], tenths

bool majorish(ScaleType s) {
  return s == ScaleType::Major || s == ScaleType::Lydian || s == ScaleType::Mixolydian ||
         s == ScaleType::PentatonicMajor || s == ScaleType::Chromatic;
}

// Root note of a chord: folded into the octave window [base, base + 11].
int chordRoot(const ProgChord& c, uint8_t root, ScaleType scale, int base) {
  const int d = genClamp(c.degree, 0, 6);
  const int n = c.roman ? base + root + kMajorSemis[d] + c.shift : degreeNote(d, root, scale, base);
  int r = base + ((n - base) % 12 + 12) % 12;
  while (r > 127) r -= 12;
  return r;
}

int distance(const uint8_t* a, int an, const uint8_t* b, int bn) {
  int sum = 0;
  for (int i = 0; i < an && i < bn; ++i) sum += a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
  return sum;
}

}  // namespace

bool parseProgression(const char* text, Progression& out) {
  out = Progression{};
  const char* p = text;
  bool first = true;
  while (*p) {
    while (*p && isBlankChar(*p)) ++p;
    if (!*p) break;
    const char* tok = p;
    while (*p && !isBlankChar(*p)) ++p;
    const int len = static_cast<int>(p - tok);
    const bool wasFirst = first;
    first = false;
    if (wasFirst && len == 6 && !strncmp(tok, "@steps", 6)) {
      out.steps = true;
      continue;
    }
    if (out.n >= kProgMax) continue;
    ProgChord c;
    int i = 0;
    if (tok[i] == 'b' || tok[i] == '#' || tok[i] == 'n') {
      c.roman = true;
      c.shift = static_cast<int8_t>(tok[i] == 'b' ? -1 : (tok[i] == '#' ? 1 : 0));
      ++i;
    }
    if (i >= len || tok[i] < '1' || tok[i] > '7') continue;
    c.degree = static_cast<int8_t>(tok[i++] - '1');
    const int nameAt = i;
    while (i < len && tok[i] != '*') ++i;
    if (i > nameAt) {
      const int type = chordTypeByName(tok + nameAt, i - nameAt);
      if (type < 0) continue;
      c.type = static_cast<int8_t>(type);
    }
    if (i < len) {  // '*N'
      int v = 0, digits = 0;
      for (++i; i < len && tok[i] >= '0' && tok[i] <= '9'; ++i, ++digits)
        if (v < 100000) v = v * 10 + (tok[i] - '0');
      if (!digits || i < len) continue;
      c.len = static_cast<uint8_t>(genClamp(v, 1, kMaxSteps));
    }
    out.c[out.n++] = c;
  }
  return out.n > 0;
}

bool formatProgression(const Progression& p, char* out, int cap) {
  int n = 0;
  auto put = [&](const char* s) {
    const int l = static_cast<int>(strlen(s));
    if (n + l >= cap) return false;
    memcpy(out + n, s, l);
    n += l;
    return true;
  };
  if (cap < 1) return false;
  if (p.steps && !put("@steps")) return false;
  for (int i = 0; i < p.n && i < kProgMax; ++i) {
    const ProgChord& c = p.c[i];
    char tok[16];
    int k = 0;
    if (c.roman) tok[k++] = c.shift < 0 ? 'b' : (c.shift > 0 ? '#' : 'n');
    tok[k++] = static_cast<char>('1' + genClamp(c.degree, 0, 6));
    tok[k] = '\0';
    if (c.type >= 0 && c.type < kChordTypes) {
      memcpy(tok + k, chordName(static_cast<uint8_t>(c.type)), 3);
      k += 3;
      tok[k] = '\0';
    }
    if (c.len > 1) snprintf(tok + k, sizeof(tok) - k, "*%d", c.len);
    if ((n > 0 && !put(" ")) || !put(tok)) return false;
  }
  out[n] = '\0';
  return true;
}

int progFactoryCount() { return kProgsN; }
const char* progFactoryName(int i) { return i >= 0 && i < kProgsN ? kProgs[i].name : nullptr; }
const char* progFactoryText(int i) { return i >= 0 && i < kProgsN ? kProgs[i].text : nullptr; }
int chordRhythmCount() { return kRhythmsN; }
const char* chordRhythmName(int i) { return i >= 0 && i < kRhythmsN ? kRhythms[i].name : nullptr; }
const char* chordRhythmText(int i) { return i >= 0 && i < kRhythmsN ? kRhythms[i].text : nullptr; }

const char* chordFlavorName(ChordFlavor f) {
  const int i = static_cast<int>(f);
  return i >= 0 && i < static_cast<int>(ChordFlavor::Count) ? kFlavorNames[i] : "";
}

const char* chordVoicingName(ChordVoicing v) {
  const int i = static_cast<int>(v);
  return i >= 0 && i < static_cast<int>(ChordVoicing::Count) ? kVoicingNames[i] : "";
}

void randomProgression(int count, uint32_t seed, ScaleType scale, Progression& out) {
  out = Progression{};
  count = genClamp(count, 1, kProgMax);
  const bool maj = majorish(scale);
  // Major: I ii iii IV V vi; minor: i iv v VI VII III (all diatonic degrees of the scale).
  static constexpr int8_t kMajPool[] = {0, 1, 2, 3, 4, 5}, kMinPool[] = {0, 3, 4, 5, 6, 2};
  static constexpr int8_t kMajLast[] = {4, 3}, kMinLast[] = {4, 3, 6};
  const int8_t* pool = maj ? kMajPool : kMinPool;
  const int8_t* lastSet = maj ? kMajLast : kMinLast;
  const int lastN = maj ? 2 : 3;
  int fnSize[3] = {};
  for (int i = 0; i < 6; ++i) ++fnSize[fnOf(pool[i])];
  Rng rng(seed);
  int prev = 0;
  out.c[out.n++].degree = 0;
  while (out.n < count) {
    int cand[6], w[6], m = 0, sum = 0;
    if (out.n == count - 1) {
      for (int i = 0; i < lastN; ++i)
        if (lastSet[i] != prev) cand[m] = lastSet[i], w[m++] = 1;
    } else {
      for (int i = 0; i < 6; ++i) {
        const int d = pool[i];
        if (d == prev) continue;
        const int f = fnOf(d);
        cand[m] = d;
        w[m++] = kFnWeight[fnOf(prev)][f] * 6 / fnSize[f];  // a function's weight shared by its degrees
      }
    }
    for (int i = 0; i < m; ++i) sum += w[i];
    int r = static_cast<int>(rng.below(static_cast<uint32_t>(sum)));
    int pick = 0;
    while (pick < m - 1 && r >= w[pick]) r -= w[pick++];
    prev = cand[pick];
    out.c[out.n++].degree = static_cast<int8_t>(prev);
  }
}

int layoutChords(const Progression& prog, const ChordSpec& spec, int n, uint8_t root, ScaleType scale,
                 ChordSeg* out) {
  if (n <= 0) return 0;
  n = n < kMaxSteps ? n : kMaxSteps;
  Progression p = prog;
  if (spec.random) randomProgression(spec.chords, spec.seed, scale, p);
  if (p.n == 0) return 0;
  if (p.n > kProgMax) p.n = kProgMax;

  const int mutate = genClamp(spec.mutate, 0, 100);
  if (mutate) {
    Rng mut(spec.seed * 2654435761u + 1u);  // two draws per chord
    static constexpr int8_t kRelMajor[7] = {5, 3, 4, 1, 2, 0, 6};  // I-vi, ii-IV, iii-V
    static constexpr int8_t kRelMinor[7] = {2, 1, 0, 5, 6, 3, 4};  // i-III, iv-VI, v-VII
    const int8_t* rel = majorish(scale) ? kRelMajor : kRelMinor;
    for (int i = 0; i < p.n; ++i) {
      ProgChord& c = p.c[i];
      const int hit = static_cast<int>(mut.below(100)), fl = static_cast<int>(mut.below(4));
      if (hit >= mutate) continue;
      if (c.roman) continue;
      c.degree = rel[genClamp(c.degree, 0, 6)];
      if (c.type < 0) c.type = static_cast<int8_t>(kFlavorTypes[fl]);
    }
  }

  int unit = 1;
  if (!p.steps) {
    if (spec.length) {
      unit = spec.length;
    } else {
      int sum = 0;
      for (int i = 0; i < p.n; ++i) sum += p.c[i].len;
      unit = sum > 0 && n / sum > 1 ? n / sum : 1;
    }
  }
  const int base = 12 * (genClamp(spec.octave, 0, 9) + 1);
  const int flavor = genClamp(static_cast<int>(spec.flavor), 0, static_cast<int>(ChordFlavor::Count) - 1);
  uint8_t prevNotes[4];
  int prevN = 0, count = 0, start = 0;
  for (int i = 0; i < p.n && start < n; ++i) {
    const ProgChord& c = p.c[i];
    const int len = c.len * unit;
    const bool last = i == p.n - 1 || start + len >= n;
    ChordSeg& s = out[count++];
    s.root = static_cast<uint8_t>(chordRoot(c, root, scale, base));
    const uint8_t type = c.type >= 0 && c.type < kChordTypes ? c.type : (c.roman ? kChordMaj : kFlavorTypes[flavor]);
    s.start = static_cast<uint8_t>(start);
    s.steps = static_cast<uint8_t>(last ? n - start : len);
    uint8_t inv = 0, notes[4];
    int notesN = chordNotes(s.root, chordValue(type, 0), root, scale, notes);
    if (spec.voicing == ChordVoicing::Close && prevN > 0) {
      // Candidates: every inversion at the root, then an octave down (not below base - 7). Strict '<'
      // keeps the original octave, then the lower inversion, on ties.
      const int root0 = s.root, inv0N = notesN;
      int best = distance(notes, notesN, prevNotes, prevN);
      for (int o = 0; o < 2; ++o) {
        const int r = root0 - 12 * o;
        if (o && r < base - 7) break;
        for (int k = o ? 0 : 1; k < inv0N && k < kChordInversions; ++k) {
          uint8_t cand[4];
          const int candN =
              chordNotes(static_cast<uint8_t>(r), chordValue(type, static_cast<uint8_t>(k)), root, scale, cand);
          const int d = distance(cand, candN, prevNotes, prevN);
          if (d < best) {
            best = d;
            inv = static_cast<uint8_t>(k);
            s.root = static_cast<uint8_t>(r);
            notesN = candN;
            memcpy(notes, cand, sizeof(cand));
          }
        }
      }
    }
    s.chd = chordValue(type, inv);
    memcpy(prevNotes, notes, sizeof(notes));
    prevN = notesN;
    if (last) break;
    start += len;
  }
  return count;
}

bool applyChords(Pattern& dst, const Sel& sel, const ChordSpec& spec, const Progression& prog,
                 const ArpPattern& rhythm, uint8_t root, ScaleType scale, const bool* drumTracks) {
  if (rhythm.len == 0 || spec.dest >= kTracks) return false;
  if (drumTracks && drumTracks[spec.dest]) return false;
  const int plen = genClamp(dst.length, 1, kMaxSteps);
  const int s0 = genClamp(sel.s0, 0, plen - 1), s1 = genClamp(sel.s1, s0, plen - 1);
  ChordSeg seg[kProgMax];
  const int segN = layoutChords(prog, spec, s1 - s0 + 1, root, scale, seg);
  if (segN == 0) return false;
  Step* out = dst.steps[spec.dest];
  clearGenRange(out, s0, s1);

  const int velLo = genClamp(spec.velLo, 1, 127), velHi = genClamp(spec.velHi, 1, 127);
  const int gate = genClamp(spec.gate, 5, 100), mutate = genClamp(spec.mutate, 0, 100);
  Rng mut(spec.seed * 40503u + 7u);  // rhythm mutation, apart from the chords' stream
  // A pure hold (one note on step 0, ties after it, like HOLD) holds the whole chord: no re-hit on wrap.
  bool pureHold = rhythm.len >= 2 && rhythm.steps[0].kind == ArpKind::Note;
  for (int i = 1; i < rhythm.len && pureHold; ++i) pureHold = rhythm.steps[i].kind == ArpKind::Tie;
  int prevStep = -1;  // the last note of the current chord, -1 after a rest / at a chord start
  int tieStep = -1;   // the step carrying a running TIE
  for (int g = 0; g < segN; ++g) {
    const ChordSeg& c = seg[g];
    prevStep = -1;  // a tie at a chord start does not hold the previous chord
    for (int j = 0; j < c.steps; ++j) {
      const int s = s0 + c.start + j;
      ArpStep a = rhythm.steps[pureHold && j >= rhythm.len ? rhythm.len - 1 : j % rhythm.len];
      if (mutate) {
        const int8_t oct = a.oct;
        mutateStep(a, mutate, mut);
        a.oct = oct;  // pitch changes do not apply to chords
      }
      Step& st = out[s];
      if (a.kind == ArpKind::Note) {
        // A new chord note simply replaces a running tie.
        st.note = static_cast<uint8_t>(genClamp(c.root + 12 * a.oct, 0, 127));
        st.vel = genVelocity(a, velLo, velHi);
        putFx(st, Fx::CHD, c.chd);  // first: CHD keeps a slot when slots are scarce
        putFx(st, Fx::GAT, gateValue(genGatePct(a, gate, 1)));
        tieStep = -1;
        prevStep = s;
        continue;
      }
      if (a.kind == ArpKind::Tie && prevStep >= 0) {
        if (tieStep < 0 && putFx(out[prevStep], Fx::TIE, 0)) tieStep = prevStep;
        continue;
      }
      // A rest (or a tie with nothing of this chord to hold) ends a running tie.
      if (tieStep >= 0) st.note = kNoteOff;
      tieStep = -1;
      prevStep = -1;
    }
  }
  if (tieStep >= 0) {
    // A tie running to the range's end: no OFF can follow, so a gate over the rest of the range.
    for (FxSlot& sl : out[tieStep].fx)
      if (sl.cmd == Fx::TIE) sl = FxSlot{};
    const int span = (s1 + 1 - tieStep) * 100;
    putFx(out[tieStep], Fx::GAT, gateValue(span < 800 ? span : 800));
  }
  return true;
}

bool captureChords(const Pattern& p, int track, int s0, int s1, uint8_t root, ScaleType scale, Progression& out) {
  if (track < 0 || track >= kTracks) return false;
  const int plen = genClamp(p.length, 1, kMaxSteps);
  s0 = genClamp(s0, 0, plen - 1);
  s1 = genClamp(s1, s0, plen - 1);
  const Step* tr = p.steps[track];
  Progression r;
  r.steps = true;
  int starts[kProgMax];
  int curNote = -1, curChd = -1, end = s1 + 1;
  for (int s = s0; s <= s1; ++s) {
    const Step& st = tr[s];
    if (!st.hasNote()) continue;
    const FxSlot* f = st.find(Fx::CHD);
    const int chd = f && f->val < kChordValues ? f->val : -1;
    if (st.note == curNote && chd == curChd) continue;
    if (r.n == kProgMax) {
      end = s;
      break;
    }
    curNote = st.note;
    curChd = chd;
    ProgChord& c = r.c[r.n];
    starts[r.n++] = s;
    const int pc = ((st.note - root) % 12 + 12) % 12;
    int d = 0;
    while (d < 7 && degreeNote(d, root, scale, 0) % 12 != st.note % 12) ++d;
    if (d < 7) {
      c.degree = static_cast<int8_t>(d);
      const int third = degreeNote(d + 2, root, scale, 0) - degreeNote(d, root, scale, 0);
      c.type = static_cast<int8_t>(chd >= 0 ? chordType(static_cast<uint8_t>(chd))
                                            : (third == 3 ? kChordMin : kChordMaj));
    } else {
      // Roman numeral of the major scale: natural, else flat, else sharp (every pitch class matches).
      c.roman = true;
      for (int sh : {0, -1, 1}) {
        int k = 0;
        while (k < 7 && (kMajorSemis[k] + sh + 12) % 12 != pc) ++k;
        if (k < 7) {
          c.degree = static_cast<int8_t>(k);
          c.shift = static_cast<int8_t>(sh);
          break;
        }
      }
      c.type = static_cast<int8_t>(chd >= 0 ? chordType(static_cast<uint8_t>(chd)) : kChordMaj);
    }
  }
  if (r.n == 0) return false;
  for (int i = 0; i < r.n; ++i)
    r.c[i].len = static_cast<uint8_t>((i + 1 < r.n ? starts[i + 1] : end) - starts[i]);
  out = r;
  return true;
}

}  // namespace mt
