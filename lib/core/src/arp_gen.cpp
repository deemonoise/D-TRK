#include "arp_gen.h"
#include <stddef.h>
#include <string.h>
#include "rng.h"

namespace mt {
namespace {

bool blank(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ','; }

struct Factory {
  const char* name;
  const char* text;
};
constexpr Factory kFactory[] = {
    {"16THS", "x x x x x x x x x x x x x x x x"},
    {"8THS", "x . x . x . x . x . x . x . x ."},
    {"TRIPLET", "X x x X x x X x x X x x"},
    {"DOTTED", "x . . x . . x . . x . . x . x ."},
    {"QUARTERS", "xl . . . xl . . . xl . . . xl . . ."},
    {"OFFBEAT", ". . x . . . x . . . x . . . x ."},
    {"ACCENT 4", "X x x x X x x x X x x x X x x x"},
    {"TR GATE", "X xs xs x X xs xs x X xs xs x X xs x xs"},
    {"TR OFFBT", ". . Xl . . . Xl . . . Xl . . . Xl ."},
    {"TR ROLL", "X o x o X o x o X o x o X o x o"},
    {"TR PEDAL", "Xp x xp x Xp x xp x Xp x xp x Xp x xp x"},
    {"TR UPLIFT", "x x x x x^ x^ x^ x^ X X X X X^ X^ X^ X^"},
    {"TR 332", "X - x X - x X - X - x X - x X -"},
    {"TR CHUG", "X o x o x o X o x o x o X o x x"},
    {"PSY GALOP", ". Xp x xp . Xp xp x . Xp x xp . Xp xp x"},
    {"PSY TRIPL", ". Xp xp . Xp x . Xp xp . Xp x"},
    {"TE STAB", "Xs . . Xs . . Xs . . . Xs . . Xs . ."},
    {"TE HYPNO3", "X x . x x . X x . x x ."},
    {"TE MINIMAL", "x . . o . . x . . . o . x . . ."},
    {"TE RUMBLE", ". o o o . o o o . o o o . o o o"},
    {"TE ROLLER", "xp . xr xr xp . xr . xp . xr xr xp . x ."},
    {"HO OFFBT", ". . Xs . . . Xs . . . Xs . . . Xs ."},
    {"HO ORGAN", "Xl . . xl . . Xl . . xl . . Xl . xl ."},
    {"HO PIANO", "X . x . . x . x X . x . . x . ."},
    {"HO SHUFFLE", "x . o x . o x . o x . o x . o x"},
    {"DNB ROLL", "X o x o o x o x X o x o o x o x"},
    {"DNB STAB", "Xs . . . . . Xs . . . Xs . . . . ."},
    {"DNB AMEN", "X . x . . x . x . x X . . x . ."},
    {"DNB REESE", "Xl - - - . . xl - Xl - - . . . x ."},
    {"DNB 2STEP", "X . . x . . . . . . X . . x . ."},
    {"ACID 1", "X x~ x . X x~ X x x . X~ x . x X~ x"},
    {"ACID 2", "x^ x X~ x . x x^~ x X . x~ x x X x^ ."},
    {"ACID 3", "Xr x~ xr . X~ xv x . Xr x xr~ x . X x~ x"},
    {"EL FUNK", "X . . x . . X . . x . x X . x ."},
    {"EL ROBOT", "Xs . xs . Xs xs . xs Xs . xs . Xs xs xs ."},
    {"BR BREAK", "X . x . . x X . . x . x X . . x"},
    {"SW 8THS", "X . x^ . x . x^ . X . x^ . x . x^ ."},
    {"SW DRIVE", "X x x x X x x x X x x x X x x^ x^"},
    {"DUB STAB", "Xs . . . . . o . . . . . Xs . o ."},
    {"DUB ECHO", ". . Xs . . o . o . . Xs . . o . o"},
    {"CHIP OCT", "x xr^ x xr^ x xr^ x xr^ x xr^ x xr^ x xr^ x xr^"},
    {"CHIP RUN", "xs xs xs xs xs xs xs xs xs xs xs xs xs xs xs xs"},
};
constexpr int kFactoryN = sizeof(kFactory) / sizeof(kFactory[0]);

const char* const kModeNames[] = {"UP",     "DOWN",     "UP/DN",   "DN/UP", "PLAYED",
                                  "RANDOM", "CONVERGE", "DIVERGE", "PEDAL", "CHORD"};
static_assert(sizeof(kModeNames) / sizeof(kModeNames[0]) == static_cast<size_t>(ArpMode::Count), "mode names");

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

constexpr int kHeldMax = kTracks * 4;  // every track of a selection with a 4-note CHD
constexpr int kSeqMax = kHeldMax * 4;   // x 4 octaves

struct Held {
  uint8_t n = 0;
  uint8_t notes[kHeldMax];  // played order, no duplicates
};

void addNote(Held& h, uint8_t note) {
  for (int i = 0; i < h.n; ++i)
    if (h.notes[i] == note) return;
  if (h.n < kHeldMax) h.notes[h.n++] = note;
}

// One octave after another; ascending within an octave, PLAYED keeps the held order.
int buildSeq(const Held& h, ArpMode mode, int octaves, uint8_t* seq) {
  uint8_t base[kHeldMax];
  memcpy(base, h.notes, h.n);
  if (mode != ArpMode::Played)
    for (int i = 1; i < h.n; ++i)
      for (int j = i; j > 0 && base[j - 1] > base[j]; --j) {
        const uint8_t t = base[j];
        base[j] = base[j - 1];
        base[j - 1] = t;
      }
  int n = 0;
  for (int o = 0; o < octaves; ++o)
    for (int i = 0; i < h.n; ++i)
      if (base[i] + 12 * o <= 127) seq[n++] = static_cast<uint8_t>(base[i] + 12 * o);
  return n;
}

// Index into seq of n notes at cycle position k (Random is drawn by the caller).
int orderAt(ArpMode m, int n, int k) {
  if (n <= 1) return 0;
  switch (m) {
    case ArpMode::Down:
      return n - 1 - k % n;
    case ArpMode::UpDown: {
      const int len = 2 * n - 2, i = k % len;
      return i < n ? i : len - i;
    }
    case ArpMode::DownUp: {
      const int len = 2 * n - 2, i = k % len;
      return i < n ? n - 1 - i : i - (n - 1);
    }
    case ArpMode::Converge: {
      const int i = k % n;
      return i % 2 == 0 ? i / 2 : n - 1 - i / 2;
    }
    case ArpMode::Diverge: {
      const int i = n - 1 - k % n;
      return i % 2 == 0 ? i / 2 : n - 1 - i / 2;
    }
    case ArpMode::Pedal: {
      const int len = 2 * (n - 1), i = k % len;
      return i % 2 == 0 ? 0 : 1 + i / 2;
    }
    default:
      return k % n;
  }
}

int lowest(const uint8_t* seq, int n) {
  int v = seq[0];
  for (int i = 1; i < n; ++i)
    if (seq[i] < v) v = seq[i];
  return v;
}

// Seeded variation of one arp step: hits toggled, accents and pitches redrawn. Always draws the
// same count of numbers so a step's change does not shift the next ones.
void mutateStep(ArpStep& a, int amount, Rng& r) {
  const int hit = static_cast<int>(r.below(200)), acc = static_cast<int>(r.below(300));
  const int pitch = static_cast<int>(r.below(400));
  const uint32_t accV = r.below(3), pitchV = r.below(4);
  if (a.kind != ArpKind::Tie && hit < amount) a.kind = a.kind == ArpKind::Note ? ArpKind::Rest : ArpKind::Note;
  if (acc < amount) a.acc = static_cast<ArpAcc>(accV);
  if (pitch < amount) {
    a.pitch = pitchV == 3 ? ArpPitch::Next : static_cast<ArpPitch>(pitchV);
    a.oct = pitchV == 3 ? 1 : 0;
  }
}

bool arpFx(Fx f) {
  switch (f) {
    case Fx::GAT:
    case Fx::TIE:
    case Fx::SLD:
    case Fx::NDG:
    case Fx::RAT:
    case Fx::PRB:
    case Fx::CHD:
    case Fx::ARS:
    case Fx::ARP:
      return true;
    default:
      return false;
  }
}

// The slot already holding f, else the first free one. False: no slot (fx skipped).
bool putFx(Step& s, Fx f, uint8_t v) {
  for (FxSlot& sl : s.fx)
    if (sl.cmd == f) {
      sl.val = v;
      return true;
    }
  for (FxSlot& sl : s.fx)
    if (sl.cmd == Fx::None) {
      sl.cmd = f;
      sl.val = v;
      return true;
    }
  return false;
}

}  // namespace

bool parseArpPattern(const char* text, ArpPattern& out) {
  out = ArpPattern{};
  const char* p = text;
  while (*p && out.len < kArpPatMax) {
    while (*p && blank(*p)) ++p;
    if (!*p) break;
    ArpStep a;
    const char c = *p++;
    switch (c) {
      case 'x': a.kind = ArpKind::Note; break;
      case 'X': a.kind = ArpKind::Note; a.acc = ArpAcc::Accent; break;
      case 'o': a.kind = ArpKind::Note; a.acc = ArpAcc::Ghost; break;
      case '.': break;
      case '-': a.kind = ArpKind::Tie; break;
      default:
        while (*p && !blank(*p)) ++p;
        continue;
    }
    for (; *p && !blank(*p); ++p) {
      switch (*p) {
        case 's': a.len = ArpLen::Short; break;
        case 'l': a.len = ArpLen::Long; break;
        case '~': a.slide = true; break;
        case 'r': a.pitch = ArpPitch::Repeat; break;
        case 'p': a.pitch = ArpPitch::Root; break;
        case '^': a.oct = 1; break;
        case 'v': a.oct = -1; break;
        default: break;
      }
    }
    out.steps[out.len++] = a;
  }
  return out.len > 0;
}

bool formatArpPattern(const ArpPattern& p, char* out, int cap) {
  int n = 0;
  auto put = [&](char c) {
    if (n + 1 >= cap) return false;
    out[n++] = c;
    return true;
  };
  for (int i = 0; i < p.len && i < kArpPatMax; ++i) {
    const ArpStep& a = p.steps[i];
    if (i > 0 && !put(' ')) return false;
    if (a.kind == ArpKind::Rest) {
      if (!put('.')) return false;
      continue;
    }
    if (a.kind == ArpKind::Tie) {
      if (!put('-')) return false;
      continue;
    }
    if (!put(a.acc == ArpAcc::Accent ? 'X' : (a.acc == ArpAcc::Ghost ? 'o' : 'x'))) return false;
    if (a.len == ArpLen::Short && !put('s')) return false;
    if (a.len == ArpLen::Long && !put('l')) return false;
    if (a.slide && !put('~')) return false;
    if (a.pitch == ArpPitch::Repeat && !put('r')) return false;
    if (a.pitch == ArpPitch::Root && !put('p')) return false;
    if (a.oct > 0 && !put('^')) return false;
    if (a.oct < 0 && !put('v')) return false;
  }
  if (cap < 1) return false;
  out[n] = '\0';
  return true;
}

int arpFactoryCount() { return kFactoryN; }
const char* arpFactoryName(int i) { return i >= 0 && i < kFactoryN ? kFactory[i].name : nullptr; }
const char* arpFactoryText(int i) { return i >= 0 && i < kFactoryN ? kFactory[i].text : nullptr; }

const char* arpModeName(ArpMode m) {
  const int i = static_cast<int>(m);
  return i >= 0 && i < static_cast<int>(ArpMode::Count) ? kModeNames[i] : "";
}

uint8_t gateValue(int pct) {
  if (pct < 1) pct = 1;
  if (pct <= 100) return static_cast<uint8_t>(pct);
  const int v = 100 + (pct - 100 + 6) / 7;
  return static_cast<uint8_t>(v > 200 ? 200 : v);
}

bool applyArp(const Pattern& src, Pattern& dst, const Sel& sel, const ArpSpec& spec, const ArpPattern& pat,
              uint8_t root, ScaleType scale, const bool* drumTracks) {
  if (pat.len == 0 || spec.dest >= kTracks) return false;
  if (drumTracks && drumTracks[spec.dest]) return false;
  const int plen = clampInt(dst.length, 1, kMaxSteps);
  const int s0 = clampInt(sel.s0, 0, plen - 1), s1 = clampInt(sel.s1, s0, plen - 1);
  Step* out = dst.steps[spec.dest];
  for (int s = s0; s <= s1; ++s) {
    Step& st = out[s];
    st.note = kNoteEmpty;
    st.vel = kVelDefault;
    for (FxSlot& sl : st.fx)
      if (arpFx(sl.cmd)) sl = FxSlot{};
  }

  const int octaves = clampInt(spec.octaves, 1, 4), rate = clampInt(spec.rate, 1, 4);
  Held held;
  if (spec.source == ArpSource::Chord) {
    uint8_t c[4];
    const int n = chordNotes(spec.root, spec.chord, root, scale, c);
    for (int i = 0; i < n; ++i) addNote(held, c[i]);
  }
  uint8_t seq[kSeqMax];
  int seqN = buildSeq(held, spec.mode, octaves, seq);
  const uint8_t chd = spec.source == ArpSource::Chord ? spec.chord : kChordTriad;
  int nextSrc = 0;  // SELECTION: the first source step not read yet (chords set before s0 count)
  const int velLo = clampInt(spec.velLo, 1, 127), velHi = clampInt(spec.velHi, 1, 127);
  const int gate = clampInt(spec.gate, 5, 100);

  const int mutate = clampInt(spec.mutate, 0, 100), swing = clampInt(spec.swing, 0, 100);
  const int roll = clampInt(spec.roll, 0, 100);
  // Separate streams, so changing Roll or Mutate does not redraw the RANDOM notes (and back).
  Rng rng(spec.seed ^ 0xA5A5F00Du);       // RANDOM-mode notes
  Rng mut(spec.seed * 2654435761u + 1u);  // mutate, a fixed count of draws per arp step
  Rng rol(spec.seed * 40503u + 7u);       // rolls, a fixed count of draws per note
  int k = 0;          // cycle position
  int prevNote = -1;  // pitch of the last note written (survives rests: slides glide from it)
  int prevBase = -1;  // the same before its octave modifier (what REPEAT repeats)
  int prevStep = -1;  // its step, -1 after a rest
  bool tied = false;  // prevStep carries TIE
  for (int j = 0;; ++j) {
    const int s = s0 + j * rate;
    if (s > s1) break;
    if (spec.source == ArpSource::Selection) {
      // Chord changes up to s: the latest step with notes wins, an OFF alone releases.
      for (; nextSrc <= s; ++nextSrc) {
        Held h;  // built in place, copied into held only on a chord change
        bool off = false;
        for (int t = sel.t0; t <= sel.t1 && t < kTracks; ++t) {
          if (drumTracks && drumTracks[t]) continue;
          const Step& ss = src.steps[t][nextSrc];
          if (ss.hasNote()) {
            uint8_t cn[4] = {ss.note};
            const FxSlot* c = ss.find(Fx::CHD);
            const int n = c ? chordNotes(ss.note, c->val, root, scale, cn) : 1;
            for (int i = 0; i < n; ++i) addNote(h, cn[i]);
          } else if (ss.note == kNoteOff) {
            off = true;
          }
        }
        if (h.n) {
          held = h;
          seqN = buildSeq(held, spec.mode, octaves, seq);
        } else if (off) {
          held.n = 0;
          seqN = 0;
        }
      }
    }
    ArpStep a = pat.steps[(j + spec.rotate) % pat.len];
    if (mutate) mutateStep(a, mutate, mut);
    Step& st = out[s];
    // A rest, or held notes released (SELECTION OFF): silence, ending a running tie.
    if (seqN == 0 || a.kind == ArpKind::Rest) {
      if (tied) st.note = kNoteOff;
      tied = false;
      prevStep = -1;
      continue;
    }
    if (a.kind == ArpKind::Tie) {
      if (prevStep >= 0 && !tied) tied = putFx(out[prevStep], Fx::TIE, 0);
      continue;
    }
    int note;
    if (spec.mode == ArpMode::Chord || a.pitch == ArpPitch::Root) {
      note = lowest(seq, seqN);  // the cycle does not advance
    } else if (a.pitch == ArpPitch::Repeat && prevBase >= 0) {
      note = prevBase;
    } else {
      const int idx = spec.mode == ArpMode::Random ? static_cast<int>(rng.below(static_cast<uint32_t>(seqN)))
                                                   : orderAt(spec.mode, seqN, k);
      ++k;
      note = seq[idx];
    }
    const int base = note;
    note = clampInt(note + 12 * a.oct, 0, 127);
    st.note = static_cast<uint8_t>(note);
    st.vel = static_cast<uint8_t>(a.acc == ArpAcc::Ghost    ? velLo
                                  : a.acc == ArpAcc::Accent ? velHi
                                                            : (velLo + velHi) / 2);
    tied = false;
    const int pct =
        (a.len == ArpLen::Short ? (gate / 2 < 5 ? 5 : gate / 2) : (a.len == ArpLen::Long ? 100 : gate)) * rate;
    putFx(st, Fx::GAT, gateValue(pct));
    if (a.slide && prevNote >= 0) {
      // The previous note is held into this one only when adjacent; after a rest the synth still
      // glides from the last pitch.
      if (prevStep >= 0) putFx(out[prevStep], Fx::GAT, gateValue(100 * rate + 10));
      putFx(st, Fx::SLD, spec.slide ? spec.slide : 1);
    }
    if (spec.mode == ArpMode::Chord) putFx(st, Fx::CHD, chd);
    if (swing && (j & 1)) putFx(st, Fx::NDG, static_cast<uint8_t>(swing / 2));
    if (a.acc == ArpAcc::Ghost && spec.ghostPrb < 100) putFx(st, Fx::PRB, spec.ghostPrb);
    const int rollP = static_cast<int>(rol.below(100)), rollV = static_cast<int>(rol.below(3));
    if (!a.slide && rollP < roll) putFx(st, Fx::RAT, static_cast<uint8_t>(2 + rollV));
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

bool captureArp(const Pattern& p, int track, int s0, int s1, ArpPattern& out) {
  if (track < 0 || track >= kTracks) return false;
  const int plen = clampInt(p.length, 1, kMaxSteps);
  s0 = clampInt(s0, 0, plen - 1);
  s1 = clampInt(s1, s0, plen - 1);
  const int n = s1 - s0 + 1 < kArpPatMax ? s1 - s0 + 1 : kArpPatMax;
  const Step* tr = p.steps[track];
  int lo = 128, vMin = 128, vMax = 0;
  for (int i = 0; i < n; ++i) {
    const Step& st = tr[s0 + i];
    if (!st.hasNote()) continue;
    if (st.note < lo) lo = st.note;
    if (st.vel != kVelDefault) {
      if (st.vel < vMin) vMin = st.vel;
      if (st.vel > vMax) vMax = st.vel;
    }
  }
  if (lo == 128) return false;
  out = ArpPattern{};
  out.len = static_cast<uint8_t>(n);
  const int spread = vMax - vMin;
  int prev = -1;
  bool tie = false;
  for (int i = 0; i < n; ++i) {
    const Step& st = tr[s0 + i];
    ArpStep a;
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
      if (prev >= 0 && st.note == prev)
        a.pitch = ArpPitch::Repeat;
      else if (prev >= 0 && st.note == lo)
        a.pitch = ArpPitch::Root;
      prev = st.note;
      tie = st.find(Fx::TIE) != nullptr;
    } else if (tie && st.note == kNoteEmpty) {
      a.kind = ArpKind::Tie;
    } else {
      tie = false;
    }
    out.steps[i] = a;
  }
  return true;
}

}  // namespace mt
