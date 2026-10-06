#include "fx_info.h"
#include <stdio.h>
#include "scale.h"

namespace mt {
namespace {

struct Info {
  const char* name;
  const char* longName;  // toast when the command is chosen, "" for None
  int16_t min, max, def;
  bool isSigned;
};

constexpr Info kInfo[] = {
    {"...", "", 0, 0, 0, false},     // None
    {"CHN", "MIDI CHANNEL", 1, 16, 1, false},    // CHN
    {"RAT", "RETRIGGER", 2, 8, 2, false},     // RAT
    {"PRB", "PROBABILITY", 0, 100, 50, false},  // PRB
    {"GAT", "GATE LENGTH", 1, 200, 100, false}, // GAT (see gatePercent)
    {"TIE", "TIE", 0, 0, 0, false},     // TIE
    {"NDG", "NUDGE", -50, 50, 0, true},   // NDG
    {"CHD", "CHORD", 0, kChordCount - 1, 0, false},
    {"STR", "STRUM", 1, 50, 10, false},   // STR
    {"CND", "CONDITION", 0, 0, 0x12, false},  // CND: own ordering
    {"VRN", "VELOCITY RANDOM", 0, 64, 16, false},   // VRN
    {"NRN", "NOTE RANDOM", 1, 7, 1, false},     // NRN
    {"CCA", "CC A", 0, 127, 64, false},  // CCA
    {"CCB", "CC B", 0, 127, 64, false},  // CCB
    {"PBN", "PITCH BEND", -64, 63, 0, true},   // PBN
    {"PGM", "PROGRAM CHANGE", 0, 127, 0, false},   // PGM
    {"SLD", "SLIDE", 1, 255, 16, false},  // SLD: slide time x4 ms
    {"VIB", "VIBRATO", 0, 255, 0x44, false},  // VIB: xy = speed, depth (hex)
    {"ARP", "ARPEGGIO", 0, 255, 0x37, false},  // ARP: xy = +x, +y semitones (hex)
    {"VSL", "VOLUME SLIDE", -64, 63, -8, true},  // VSL: volume change per step
    {"OFS", "SAMPLE OFFSET", 0, 255, 0, false},   // OFS: sample start, /256
    {"CUT", "NOTE CUT", 1, 96, 6, false},    // CUT: ticks
    // DCY..CON: FM / DRUM / SYNTH macro locks (SYNTH: SHP1, SHP2, MIX, DET, SENV).
    {"DEC", "MACRO DECAY / SHP1", 0, 127, 64, false},
    {"COL", "MACRO COLOR / SHP2", 0, 127, 64, false},
    {"SHP", "MACRO SHAPE / MIX", 0, 127, 64, false},
    {"SWP", "MACRO SWEEP / DET", 0, 127, 64, false},
    {"CON", "MACRO CONTOUR / SENV", 0, 127, 64, false},
    {"FLT", "FILTER CUTOFF", 0, 127, 64, false},  // FLT: filter cutoff lock
    {"RES", "FILTER RESONANCE", 0, 127, 0, false},   // RES: filter resonance lock
    {"SLC", "SAMPLE SLICE", 0, kMaxSlices - 1, 0, false},  // SLC: slice of the next SAMPLE note-on (FX slice mode)
    {"OFF", "NOTE OFF", 0, 96, 0, false},    // OFF: ticks after the step start
    {"DLY", "DELAY SEND", 0, 127, 64, false},  // DLY: delay send lock
    {"ACC", "ACCENT", 0, 255, 0xFF, false},    // ACC: lane mask (hex), drum tracks
    {"DRV", "DRIVE", 0, 127, 64, false},       // DRV: drive lock
    {"RVB", "REVERB SEND", 0, 127, 64, false}, // RVB: reverb send lock
    {"ARM", "ARP MODE", 0, 0, kArmDefault, false},  // ARM: own ordering (rate 1..8, then mode)
    {"ARS", "STEP ARP", 0, 0, kArsDefault, false},  // ARS: steps per note, octaves, then mode
};
static_assert(sizeof(kInfo) / sizeof(kInfo[0]) == static_cast<int>(Fx::Count), "kInfo must cover Fx");

const Info& info(Fx f) {
  const auto i = static_cast<uint8_t>(f);
  return kInfo[i < static_cast<uint8_t>(Fx::Count) ? i : 0];
}

// CND values in encoder order: FST, then A:B for B = 2..8, A = 1..B, then FIL, NFL.
constexpr int kCndFillIdx = 36;

int cndIndex(uint8_t v) {
  if (v == 0) return 0;
  if (v == kCndFill) return kCndFillIdx;
  if (v == kCndNoFill) return kCndFillIdx + 1;
  const int a = v >> 4, b = v & 15;
  if (b < 2 || b > 8 || a < 1 || a > b) return 0;
  int idx = 1;
  for (int bb = 2; bb < b; ++bb) idx += bb;
  return idx + a - 1;
}

uint8_t cndValue(int idx) {
  if (idx <= 0) return 0;
  if (idx == kCndFillIdx) return kCndFill;
  if (idx >= kCndFillIdx + 1) return kCndNoFill;
  int rest = idx - 1;
  for (int b = 2; b <= 8; ++b) {
    if (rest < b) return static_cast<uint8_t>(((rest + 1) << 4) | b);
    rest -= b;
  }
  return 0x88;
}

constexpr int kCndCount = kCndFillIdx + 2;

// The order the encoder runs through the commands: related ones next to each other (notes and arp,
// timing, chance, pitch and level, sound locks, sample, sends, MIDI only).
constexpr Fx kOrder[] = {
    Fx::None,
    Fx::CHD, Fx::STR, Fx::ARP, Fx::ARM, Fx::ARS,
    Fx::RAT, Fx::NDG, Fx::GAT, Fx::TIE, Fx::OFF, Fx::CUT,
    Fx::PRB, Fx::CND, Fx::VRN, Fx::NRN,
    Fx::SLD, Fx::VIB, Fx::PBN, Fx::VSL, Fx::ACC,
    Fx::FLT, Fx::RES, Fx::DRV, Fx::DCY, Fx::COL, Fx::SHP, Fx::SWP, Fx::CON,
    Fx::OFS, Fx::SLC,
    Fx::DLY, Fx::RVB,
    Fx::CHN, Fx::CCA, Fx::CCB, Fx::PGM,
};
constexpr int kOrderN = sizeof(kOrder) / sizeof(kOrder[0]);
static_assert(kOrderN == static_cast<int>(Fx::Count), "kOrder must list every Fx once");

constexpr bool orderComplete() {
  for (int f = 0; f < kOrderN; ++f) {
    int n = 0;
    for (int i = 0; i < kOrderN; ++i) n += static_cast<int>(kOrder[i]) == f;
    if (n != 1) return false;
  }
  return true;
}
static_assert(orderComplete(), "kOrder must list every Fx once");

// ARM / ARS: mode << 4 | rate 1..kArmRateMax.
int armRate(uint8_t v) { return (v & 15) < 1 ? 1 : ((v & 15) > kArmRateMax ? kArmRateMax : (v & 15)); }

}  // namespace

const char* fxLongName(Fx f) { return info(f).longName; }

const char* fxName(Fx f) { return info(f).name; }
uint8_t fxDefault(Fx f) { return static_cast<uint8_t>(info(f).def); }

void fxFormat(Fx f, uint8_t v, char out[5]) {
  switch (f) {
    case Fx::None: snprintf(out, 5, "   "); return;
    case Fx::TIE: snprintf(out, 5, " --"); return;
    case Fx::GAT: snprintf(out, 5, "%3u", gatePercent(v)); return;
    case Fx::NDG:
    case Fx::PBN:
    case Fx::VSL: snprintf(out, 5, "%+3d", fxSigned(v)); return;
    case Fx::VIB:
    case Fx::ARP:
    case Fx::ACC: snprintf(out, 5, " %02X", v); return;
    case Fx::CHD: snprintf(out, 5, "%s", chordName(v)); return;
    case Fx::ARM: snprintf(out, 5, " %c%d", "UDBR"[(v >> 4) & 3], armRate(v)); return;
    case Fx::ARS: {  // " U1"; with more than one octave the count follows: "U12"
      const int oct = ((v >> 6) & 3) + 1;
      if (oct == 1) snprintf(out, 5, " %c%d", "UDBR"[(v >> 4) & 3], armRate(v));
      else snprintf(out, 5, "%c%d%d", "UDBR"[(v >> 4) & 3], armRate(v), oct);
      return;
    }
    case Fx::CND:
      if (v == 0) snprintf(out, 5, "FST");
      else if (v == kCndFill) snprintf(out, 5, "FIL");
      else if (v == kCndNoFill) snprintf(out, 5, "NFL");
      else snprintf(out, 5, "%d:%d", v >> 4, v & 15);
      return;
    default: snprintf(out, 5, "%3u", v); return;
  }
}

uint8_t fxStep(Fx f, uint8_t v, int delta) {
  if (f == Fx::CND) {
    int i = cndIndex(v) + delta;
    i = i < 0 ? 0 : (i >= kCndCount ? kCndCount - 1 : i);
    return cndValue(i);
  }
  if (f == Fx::ARS) {  // the steps first, then the octaves, then the mode
    constexpr int kPerMode = kArmRateMax * kArsOctMax;
    int i = ((v >> 4) & 3) * kPerMode + ((v >> 6) & 3) * kArmRateMax + armRate(v) - 1 + delta;
    i = i < 0 ? 0 : (i >= kArmModes * kPerMode ? kArmModes * kPerMode - 1 : i);
    const int mode = i / kPerMode, oct = i % kPerMode / kArmRateMax, rate = i % kArmRateMax + 1;
    return static_cast<uint8_t>(oct << 6 | mode << 4 | rate);
  }
  if (f == Fx::ARM) {  // index mode * 8 + rate - 1: the rate first, then the mode
    int i = ((v >> 4) & 3) * kArmRateMax + armRate(v) - 1 + delta;
    i = i < 0 ? 0 : (i >= kArmModes * kArmRateMax ? kArmModes * kArmRateMax - 1 : i);
    return static_cast<uint8_t>(((i / kArmRateMax) << 4) | (i % kArmRateMax + 1));
  }
  const Info& in = info(f);
  int cur = in.isSigned ? fxSigned(v) : v;
  cur += delta;
  cur = cur < in.min ? in.min : (cur > in.max ? in.max : cur);
  return static_cast<uint8_t>(cur);
}

bool fxSynthOnly(Fx f) { return (f >= Fx::SLD && f <= Fx::SLC) || f == Fx::DLY || (f >= Fx::DRV && f <= Fx::ARM); }

bool fxDrumOnly(Fx f) { return f == Fx::ACC; }

Fx fxNextCmd(Fx f, int delta) {
  int at = 0;
  for (int i = 0; i < kOrderN; ++i)
    if (kOrder[i] == f) at = i;
  return kOrder[((at + delta) % kOrderN + kOrderN) % kOrderN];
}

}  // namespace mt
