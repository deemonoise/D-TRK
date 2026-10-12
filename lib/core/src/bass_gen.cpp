#include "bass_gen.h"
#include <stddef.h>
#include "gen_util.h"
#include "rng.h"

namespace mt {
namespace {

struct Factory {
  const char* name;
  const char* text;
};
constexpr Factory kFactory[] = {
    {"HOUSE OFF", ". . x . . . x . . . x . . . x ."},
    {"DISCO OCT", "x . x^ . x . x^ . x . x^ . x . x^ ."},
    {"TECHNO RL", ". x x x . x x x . x x x . x x x"},
    {"TECHNO 16", "X x x x X x x x X x x x X x x x"},
    {"ACID 1", "X x~ x5 . X x~ X3 x x . X~ x . x5 X~ x"},
    {"ACID 2", "x^ x X~ x5 . x x^~ x X . x~ x7 x X x^ ."},
    {"ACID 3", "Xr x~ x . X~ xv x . Xr x x~ x . X x~ x5"},
    {"DNB", "Xl - . . . . x . . . Xl - . . x ."},
    {"REESE", "xl - - - - - - - x5l - - - - - - -"},
    {"DUB", "Xl - - . . . x5 . Xl - - . . . x ."},
    {"REGGAETN", "X . . x . . x . X . . x . . x ."},
    {"SYNTHW 8", "x . x . x . x . x . x . x . x xa"},
    {"ELECTRO", "X . . x . . X . . x . x X . x ."},
    {"FUNK", "X . x^ . . x . x X . . x^ . x7 x ."},
    {"WALKING", "x . . . x3 . . . x5 . . . xa . . ."},
    {"ROCK 8", "x x x x x x x x x x x x x x x x"},
    {"TRNC OFF", ". . X . . . X . . . X . . . X ."},
    {"PSY", ". X x x . X x x . X x x . X x x"},
    {"UKG", "X . . x . . . x . . X . . . x ."},
    {"BOOGIE", "x . x3 . x5 . x7 . x^ . x7 . x5 . x3 ."},
    {"OCT JUMP", "x x^ x x^ x x^ x x^ x x^ x x^ x x^ x x^"},
    {"ROOT 5TH", "x . . . x5 . . . x . . . x5 . . ."},
    {"APPROACH", "xl - - - - - - - - - - - - - xa ."},
    {"TECHNO OFF", ". . x . . x . . . . x . . x . ."},
    {"DEEP HOUSE", "x . . x . . x . . . x . . x5 . ."},
    {"GARAGE", ". . x . . x . x . . x . . x . x"},
    {"MINIMAL", "x . . . . . x . . . . . x . . ."},
    {"ROLLING", "x x^ x x x x^ x x x x^ x x x x^ x x"},
    {"GALLOP", "x . x x x . x x x . x x x . x x"},
    {"PSY TRIP", ". x x . x x . x x . x x . x x ."},
    {"EBM", "X x x5 x X x x5 x X x x5 x X x x5 x"},
    {"ITALO", "x . x^ x x . x^ x x . x^ x x . x^ x"},
    {"NU DISCO", "x . x^ . . x . x^ x . x^ . . x x^ ."},
    {"ACID 4", "X~ x x^ x~ . x X5 x~ x x^ . X~ xv x x~ x5"},
    {"BREAKBEAT", "X . . x . . . x . x . . . . x ."},
    {"SYNCOPA", "x . x . . x . x . . x . x . . x"},
    {"SUB HOLD", "xl - - - - - - - - - - - - - - -"},
    {"OCT HOLD", "xl - - - x^l - - - xl - - - x^l - - -"},
    {"DUBSTEP", "Xl - - - - - . . . . x . xv . . ."},
    {"TRAP 808", "Xl - - - - - x . . . Xl - - x . ."},
    {"ONE DROP", "Xl - . . . . x5 x . . . . x . . ."},
    {"MOTOWN", "x . . x x5 . x . x . . x x7 . xa ."},
    {"SLAP", "X . x^ x . x X . . x^ . X x . x^ ."},
    {"WALK STEP", "x . . . xw . . . xw . . . xa . . ."},
    {"ARPEGGIO", "x x3 x5 x7 x^ x7 x5 x3 x x3 x5 x7 x^ x7 x5 x3"},
    {"COUNTRY", "x . . . x5v . . . x . . . x5v . . ."},
    {"BOSSA", "x . . x5 x5 . . x x . . x5 x5 . . x"},
    {"TUMBAO", ". . . x . . x . . . . x5 . . x ."},
};
constexpr int kFactoryN = sizeof(kFactory) / sizeof(kFactory[0]);

const char* const kSourceNames[] = {"ROOT", "SELECTION", "PROGRESSION"};
static_assert(sizeof(kSourceNames) / sizeof(kSourceNames[0]) == static_cast<size_t>(BassSource::Count),
              "source names");

// Mods of a token, distinct chars: room for every ASCII char, so no mod is ever dropped.
constexpr int kBassModsCap = 129;
struct BassChord {
  bool on;
  uint8_t root;  // folded into the octave
  uint8_t chd;
};

// The chord of every step s0..s1 and the root of the next chord change after it (none: the first
// chord of the range).
struct Harmony {
  BassChord cur[kMaxSteps];
  uint8_t next[kMaxSteps];
};

uint8_t foldRoot(int note, int octave) { return static_cast<uint8_t>(12 * (octave + 1) + note % 12); }

// segBase: the step segs are relative to. SELECTION: src tracks t0..t1 from step 0 to src's end (the
// next change may lie past s1); a change is a different root / CHD or a chord after an OFF.
void buildHarmony(BassSource source, const BassSpec& spec, const ChordSeg* segs, int segN, const Pattern* src,
                  int t0, int t1, const bool* drumTracks, int s0, int s1, int segBase, Harmony& h) {
  const int octave = genClamp(spec.octave, 1, 3);
  bool event[kMaxSteps] = {};
  int end = s1;
  if (source == BassSource::Selection) {
    end = genClamp(src->length, 1, kMaxSteps) - 1;
    if (end < s1) end = s1;
    BassChord c{false, 0, kChordTriad};
    for (int s = 0; s <= end; ++s) {
      int low = 128, chd = kChordTriad;
      bool off = false;
      for (int t = t0; t <= t1 && t < kTracks; ++t) {
        if (drumTracks && drumTracks[t]) continue;
        const Step& ss = src->steps[t][s];
        if (ss.hasNote()) {
          if (ss.note < low) {
            low = ss.note;
            const FxSlot* f = ss.find(Fx::CHD);
            chd = f ? f->val : kChordTriad;
          }
        } else if (ss.note == kNoteOff) {
          off = true;
        }
      }
      if (low < 128) {
        const BassChord n{true, foldRoot(low, octave), static_cast<uint8_t>(chd)};
        event[s] = !c.on || n.root != c.root || n.chd != c.chd;
        c = n;
      } else if (off) {
        c.on = false;
      }
      h.cur[s] = c;
    }
  } else {
    for (int s = s0; s <= s1; ++s) {
      if (source == BassSource::Root) {
        h.cur[s] = BassChord{true, foldRoot(spec.root, octave), kChordTriad};
        continue;
      }
      const int rel = s - segBase;
      h.cur[s] = BassChord{false, 0, kChordTriad};
      for (int i = 0; i < segN; ++i)
        if (rel >= segs[i].start && rel < segs[i].start + segs[i].steps) {
          h.cur[s] = BassChord{true, foldRoot(segs[i].root, octave), segs[i].chd};
          event[s] = rel == segs[i].start;
          break;
        }
    }
  }
  int first = -1;
  for (int s = s0; s <= s1 && first < 0; ++s)
    if (h.cur[s].on) first = h.cur[s].root;
  uint8_t nxt = static_cast<uint8_t>(first < 0 ? 0 : first);
  for (int s = end; s >= s0; --s) {
    if (s <= s1) h.next[s] = nxt;
    if (event[s] && h.cur[s].on) nxt = h.cur[s].root;
  }
}

// Chord tone idx (0 root .. 3 seventh) of the root-position chord; missing: 2 x idx degrees up.
int chordTone(const BassChord& c, int idx, uint8_t root, ScaleType scale) {
  if (idx == 0) return c.root;
  uint8_t n[4];
  const int k = chordNotes(c.root, chordValue(chordType(c.chd), 0), root, scale, n);
  return idx < k ? n[idx] : moveDegrees(c.root, 2 * idx, root, scale);
}

// Pitch of a role before its octave modifier; prev = the previous note's, -1 none.
int rolePitch(BassRole r, const BassChord& c, int nextRoot, int prev, uint8_t root, ScaleType scale) {
  switch (r) {
    case BassRole::Third: return chordTone(c, 1, root, scale);
    case BassRole::Fifth: return chordTone(c, 2, root, scale);
    case BassRole::Seventh: return chordTone(c, 3, root, scale);
    case BassRole::Approach: return nextRoot - 1;
    case BassRole::Walk: {
      const int from = prev >= 0 ? prev : c.root;
      if (from == nextRoot) return c.root;
      // A scale step never passes the target (an off-scale next root is reached exactly).
      if (from < nextRoot) {
        const int to = moveDegrees(from, 1, root, scale);
        return to > nextRoot ? nextRoot : to;
      }
      const int to = moveDegrees(from, -1, root, scale);
      return to < nextRoot ? nextRoot : to;
    }
    case BassRole::Repeat: return prev >= 0 ? prev : c.root;
    default: return c.root;
  }
}

}  // namespace

bool parseBassPattern(const char* text, BassPattern& out) {
  out = BassPattern{};
  const char* p = text;
  while (*p && out.len < kArpPatMax) {
    while (*p && isBlankChar(*p)) ++p;
    if (!*p) break;
    ArpStep a;
    char mods[kBassModsCap];
    if (!parseGenToken(p, a, mods, kBassModsCap)) continue;
    BassRole role = BassRole::Root;
    int8_t oct = 0;
    for (const char* m = mods; *m; ++m) {
      switch (*m) {
        case '1': role = BassRole::Root; break;
        case '3': role = BassRole::Third; break;
        case '5': role = BassRole::Fifth; break;
        case '7': role = BassRole::Seventh; break;
        case 'a': role = BassRole::Approach; break;
        case 'w': role = BassRole::Walk; break;
        case 'r': role = BassRole::Repeat; break;
        case '^': oct = 1; break;
        case 'v': oct = -1; break;
        default: break;
      }
    }
    out.steps[out.len] = a;
    out.role[out.len] = role;
    out.oct[out.len] = oct;
    ++out.len;
  }
  return out.len > 0;
}

bool formatBassPattern(const BassPattern& p, char* out, int cap) {
  static const char kRoleChar[] = {'\0', '3', '5', '7', 'a', 'w', 'r'};
  int n = 0;
  for (int i = 0; i < p.len && i < kArpPatMax; ++i) {
    if (i > 0) {
      if (n + 1 >= cap) return false;
      out[n++] = ' ';
    }
    char mods[3];
    int m = 0;
    const int r = static_cast<int>(p.role[i]);
    if (r > 0 && r < static_cast<int>(sizeof(kRoleChar))) mods[m++] = kRoleChar[r];
    if (p.oct[i] > 0) mods[m++] = '^';
    if (p.oct[i] < 0) mods[m++] = 'v';
    mods[m] = '\0';
    if (!formatGenToken(p.steps[i], mods, out, cap, n)) return false;
  }
  if (cap < 1) return false;
  out[n] = '\0';
  return true;
}

int bassFactoryCount() { return kFactoryN; }
const char* bassFactoryName(int i) { return i >= 0 && i < kFactoryN ? kFactory[i].name : nullptr; }
const char* bassFactoryText(int i) { return i >= 0 && i < kFactoryN ? kFactory[i].text : nullptr; }

const char* bassSourceName(BassSource s) {
  const int i = static_cast<int>(s);
  return i >= 0 && i < static_cast<int>(BassSource::Count) ? kSourceNames[i] : "";
}

bool applyBass(const Pattern& src, Pattern& dst, const Sel& sel, const BassSpec& spec, const BassPattern& pat,
               uint8_t root, ScaleType scale, const ChordSeg* segs, int segN, const bool* drumTracks) {
  if (pat.len == 0 || spec.dest >= kTracks) return false;
  if (drumTracks && drumTracks[spec.dest]) return false;
  if (spec.source == BassSource::Progression && (!segs || segN <= 0)) return false;
  const int plen = genClamp(dst.length, 1, kMaxSteps);
  const int s0 = genClamp(sel.s0, 0, plen - 1), s1 = genClamp(sel.s1, s0, plen - 1);
  // Read src before dst is cleared: src may be dst itself.
  Harmony h;
  buildHarmony(spec.source, spec, segs, segN, &src, sel.t0, sel.t1, drumTracks, s0, s1, s0, h);
  Step* out = dst.steps[spec.dest];
  clearGenRange(out, s0, s1);

  const int rate = genClamp(spec.rate, 1, 4);
  const int velLo = genClamp(spec.velLo, 1, 127), velHi = genClamp(spec.velHi, 1, 127);
  const int gate = genClamp(spec.gate, 5, 100);
  const int mutate = genClamp(spec.mutate, 0, 100), swing = genClamp(spec.swing, 0, 100);
  Rng mut(spec.seed * 2654435761u + 1u);  // mutate, a fixed count of draws per bass step
  int prevNote = -1;  // pitch of the last note written (survives rests: slides glide from it)
  int prevBase = -1;  // the same before its octave modifier (what REPEAT / WALK start from)
  int prevStep = -1;  // its step, -1 after a rest
  bool tied = false;  // prevStep carries TIE
  for (int j = 0;; ++j) {
    const int s = s0 + j * rate;
    if (s > s1) break;
    const int i = (j + spec.rotate) % pat.len;
    ArpStep a = pat.steps[i];
    BassRole role = pat.role[i];
    int oct = pat.oct[i];
    if (mutate) {
      int draw = -1;
      mutateStep(a, mutate, mut, &draw);
      if (draw >= 0) {
        // A redrawn pitch: root, 5th, 3rd or root + 12.
        static const BassRole kDrawRole[] = {BassRole::Root, BassRole::Fifth, BassRole::Third, BassRole::Root};
        role = kDrawRole[draw & 3];
        oct = draw == 3 ? 1 : 0;
      }
    }
    const BassChord& c = h.cur[s];
    Step& st = out[s];
    // A rest, or the chord released (SELECTION OFF): silence, ending a running tie.
    if (!c.on || a.kind == ArpKind::Rest) {
      if (tied) st.note = kNoteOff;
      tied = false;
      prevStep = -1;
      continue;
    }
    if (a.kind == ArpKind::Tie) {
      if (prevStep >= 0 && !tied) tied = putFx(out[prevStep], Fx::TIE, 0);
      continue;
    }
    const int base = genClamp(rolePitch(role, c, h.next[s], prevBase, root, scale), 0, 127);
    const int note = genClamp(base + 12 * oct, 0, 127);
    st.note = static_cast<uint8_t>(note);
    st.vel = genVelocity(a, velLo, velHi);
    tied = false;
    putFx(st, Fx::GAT, gateValue(genGatePct(a, gate, rate)));
    if (a.slide && prevNote >= 0) {
      if (prevStep >= 0) putFx(out[prevStep], Fx::GAT, gateValue(100 * rate + 10));
      putFx(st, Fx::SLD, spec.slide ? spec.slide : 1);
    }
    const int ndg = a.nudge * rate + (swing && (j & 1) ? swing / 2 : 0);
    if (ndg) putFx(st, Fx::NDG, static_cast<uint8_t>(static_cast<int8_t>(genClamp(ndg, -50, 50))));
    prevNote = note;
    prevBase = base;
    prevStep = s;
  }
  if (tied) {
    // A tie running to the range's end: no OFF can follow, so a gate over the rest of the range.
    for (FxSlot& sl : out[prevStep].fx)
      if (sl.cmd == Fx::TIE) sl = FxSlot{};
    const int span = (s1 + 1 - prevStep) * 100;
    putFx(out[prevStep], Fx::GAT, gateValue(span < 800 ? span : 800));
  }
  return true;
}

bool captureBass(const Pattern& p, int track, int s0, int s1, const BassSpec& spec, uint8_t root, ScaleType scale,
                 const ChordSeg* segs, int segN, const Pattern* src, const Sel* sel, BassPattern& out,
                 const bool* drumTracks) {
  if (track < 0 || track >= kTracks) return false;
  const int plen = genClamp(p.length, 1, kMaxSteps);
  s0 = genClamp(s0, 0, plen - 1);
  s1 = genClamp(s1, s0, plen - 1);
  const int n = s1 - s0 + 1 < kArpPatMax ? s1 - s0 + 1 : kArpPatMax;
  const Step* tr = p.steps[track];
  int vMin = 128, vMax = 0;
  bool any = false;
  for (int i = 0; i < n; ++i) {
    const Step& st = tr[s0 + i];
    if (!st.hasNote()) continue;
    any = true;
    if (st.vel != kVelDefault) {
      if (st.vel < vMin) vMin = st.vel;
      if (st.vel > vMax) vMax = st.vel;
    }
  }
  if (!any) return false;
  BassSource source = spec.source;
  if (source == BassSource::Selection && (!src || !sel)) source = BassSource::Root;
  if (source == BassSource::Progression && (!segs || segN <= 0)) source = BassSource::Root;
  Harmony h;
  buildHarmony(source, spec, segs, segN, src, sel ? sel->t0 : 0, sel ? sel->t1 : 0, drumTracks, s0, s0 + n - 1, s0,
               h);
  const BassChord fallback{true, foldRoot(spec.root, genClamp(spec.octave, 1, 3)), kChordTriad};

  out = BassPattern{};
  out.len = static_cast<uint8_t>(n);
  const int spread = vMax - vMin;
  int prevBase = -1;  // the previous note before its octave modifier, as applyBass keeps it
  bool tie = false;
  for (int i = 0; i < n; ++i) {
    const Step& st = tr[s0 + i];
    ArpStep a;
    BassRole role = BassRole::Root;
    int oct = 0;
    if (st.hasNote()) {
      a.kind = ArpKind::Note;
      if (st.vel != kVelDefault && spread >= 6) {
        const int rel = (st.vel - vMin) * 3;
        a.acc = rel < spread ? ArpAcc::Ghost : (rel >= 2 * spread ? ArpAcc::Accent : ArpAcc::Norm);
      }
      if (const FxSlot* g = st.find(Fx::GAT)) {
        const int pct = gatePercent(g->val);
        a.len = pct < 35 ? ArpLen::Short : (pct >= 90 ? ArpLen::Long : ArpLen::Norm);
      }
      a.slide = st.find(Fx::SLD) != nullptr;
      const BassChord& c = h.cur[s0 + i].on ? h.cur[s0 + i] : fallback;
      const int note = st.note;
      bool found = false;
      for (int k = 0; k < 4 && !found; ++k) {
        const int d = note - chordTone(c, k, root, scale);
        if (d % 12 == 0 && d >= -12 && d <= 12) {
          role = static_cast<BassRole>(k);
          oct = genClamp(d / 12, -1, 1);
          found = true;
        }
      }
      if (!found && note == h.next[s0 + i] - 1) {
        role = BassRole::Approach;
      } else if (!found && prevBase >= 0 && (note - prevBase) % 12 == 0 && note - prevBase >= -12 &&
                 note - prevBase <= 12) {
        role = BassRole::Repeat;
        oct = (note - prevBase) / 12;
      } else if (!found) {
        // The nearest of root / 3rd / 5th within an octave either way (ties: earlier role, no octave).
        static const int kOcts[] = {0, -1, 1};
        int best = 1000;
        for (int k = 0; k < 3; ++k)
          for (int o : kOcts) {
            const int dist = note - (chordTone(c, k, root, scale) + 12 * o);
            const int ad = dist < 0 ? -dist : dist;
            if (ad < best) {
              best = ad;
              role = static_cast<BassRole>(k);
              oct = o;
            }
          }
      }
      prevBase = note - 12 * oct;
      tie = st.find(Fx::TIE) != nullptr;
    } else if (tie && st.note == kNoteEmpty) {
      a.kind = ArpKind::Tie;
    } else {
      tie = false;
    }
    out.steps[i] = a;
    out.role[i] = role;
    out.oct[i] = static_cast<int8_t>(oct);
  }
  return true;
}

}  // namespace mt
