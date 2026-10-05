#include "fx_info.h"
#include <stdio.h>
#include "scale.h"

namespace mt {
namespace {

struct Info {
  const char* name;
  int16_t min, max, def;
  bool isSigned;
};

constexpr Info kInfo[] = {
    {"...", 0, 0, 0, false},     // None
    {"CHN", 1, 16, 1, false},    // CHN
    {"RAT", 2, 8, 2, false},     // RAT
    {"PRB", 0, 100, 50, false},  // PRB
    {"GAT", 1, 200, 100, false}, // GAT (see gatePercent)
    {"TIE", 0, 0, 0, false},     // TIE
    {"NDG", -50, 50, 0, true},   // NDG
    {"CHD", 0, kChordCount - 1, 0, false},
    {"STR", 1, 50, 10, false},   // STR
    {"CND", 0, 0, 0x12, false},  // CND: own ordering
    {"VRN", 0, 64, 16, false},   // VRN
    {"NRN", 1, 7, 1, false},     // NRN
    {"CCA", 0, 127, 64, false},  // CCA
    {"CCB", 0, 127, 64, false},  // CCB
    {"PBN", -64, 63, 0, true},   // PBN
    {"PGM", 0, 127, 0, false},   // PGM
    {"SLD", 1, 255, 16, false},  // SLD: slide time x4 ms
    {"VIB", 0, 255, 0x44, false},  // VIB: xy = speed, depth (hex)
    {"ARP", 0, 255, 0x37, false},  // ARP: xy = +x, +y semitones (hex)
    {"VSL", -64, 63, -8, true},  // VSL: volume change per step
    {"OFS", 0, 255, 0, false},   // OFS: sample start, /256
    {"CUT", 1, 96, 6, false},    // CUT: ticks
    {"DEC", 0, 127, 64, false},  // DCY..CON: FM macro locks
    {"COL", 0, 127, 64, false},
    {"SHP", 0, 127, 64, false},
    {"SWP", 0, 127, 64, false},
    {"CON", 0, 127, 64, false},
};
static_assert(sizeof(kInfo) / sizeof(kInfo[0]) == static_cast<int>(Fx::Count), "kInfo must cover Fx");

const Info& info(Fx f) {
  const auto i = static_cast<uint8_t>(f);
  return kInfo[i < static_cast<uint8_t>(Fx::Count) ? i : 0];
}

// CND values in encoder order: FST, then A:B for B = 2..8, A = 1..B.
int cndIndex(uint8_t v) {
  if (v == 0) return 0;
  const int a = v >> 4, b = v & 15;
  if (b < 2 || b > 8 || a < 1 || a > b) return 0;
  int idx = 1;
  for (int bb = 2; bb < b; ++bb) idx += bb;
  return idx + a - 1;
}

uint8_t cndValue(int idx) {
  if (idx <= 0) return 0;
  int rest = idx - 1;
  for (int b = 2; b <= 8; ++b) {
    if (rest < b) return static_cast<uint8_t>(((rest + 1) << 4) | b);
    rest -= b;
  }
  return 0x88;
}

constexpr int kCndCount = 36;

}  // namespace

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
    case Fx::ARP: snprintf(out, 5, " %02X", v); return;
    case Fx::CHD: snprintf(out, 5, "%s", chordName(v)); return;
    case Fx::CND:
      if (v == 0) snprintf(out, 5, "FST");
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
  const Info& in = info(f);
  int cur = in.isSigned ? fxSigned(v) : v;
  cur += delta;
  cur = cur < in.min ? in.min : (cur > in.max ? in.max : cur);
  return static_cast<uint8_t>(cur);
}

bool fxSynthOnly(Fx f) { return f >= Fx::SLD && f < Fx::Count; }

Fx fxNextCmd(Fx f, int delta) {
  const int n = static_cast<int>(Fx::Count);
  const int i = ((static_cast<int>(f) + delta) % n + n) % n;
  return static_cast<Fx>(i);
}

}  // namespace mt
