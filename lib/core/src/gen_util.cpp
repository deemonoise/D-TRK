#include "gen_util.h"
#include <stdio.h>

namespace mt {
namespace {

// Signed decimal at p (p moves past it); 0 when none. Capped so it cannot overflow.
int readInt(const char*& p) {
  const bool neg = *p == '-';
  if (*p == '-' || *p == '+') ++p;
  int v = 0;
  for (; *p >= '0' && *p <= '9'; ++p)
    if (v < 100000) v = v * 10 + (*p - '0');
  return neg ? -v : v;
}

}  // namespace

int genClamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool isBlankChar(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ','; }

bool genFx(Fx f) {
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

void clearGenRange(Step* track, int s0, int s1) {
  for (int s = s0; s <= s1; ++s) {
    Step& st = track[s];
    clearNote(st);
    for (FxSlot& sl : st.fx)
      if (genFx(sl.cmd)) sl = FxSlot{};
  }
}

bool parseGenToken(const char*& p, ArpStep& a, char* mods, int cap) {
  a = ArpStep{};
  int m = 0;
  if (mods && cap > 0) mods[0] = '\0';
  if (!*p) return false;
  const char c = *p++;
  switch (c) {
    case 'x': a.kind = ArpKind::Note; break;
    case 'X': a.kind = ArpKind::Note; a.acc = ArpAcc::Accent; break;
    case 'o': a.kind = ArpKind::Note; a.acc = ArpAcc::Ghost; break;
    case '.': break;
    case '-': a.kind = ArpKind::Tie; break;
    default:
      while (*p && !isBlankChar(*p)) ++p;
      return false;
  }
  for (; *p && !isBlankChar(*p) && *p != ':'; ++p) {
    switch (*p) {
      case 's': a.len = ArpLen::Short; break;
      case 'l': a.len = ArpLen::Long; break;
      case '~': a.slide = true; break;
      default: {
        if (!mods || cap < 2) break;
        // Each distinct char once, at its last position (later mods override earlier ones).
        int w = 0;
        for (int i = 0; i < m; ++i)
          if (mods[i] != *p) mods[w++] = mods[i];
        m = w;
        if (m + 1 < cap) mods[m++] = *p;
        mods[m] = '\0';
        break;
      }
    }
  }
  if (*p == ':') {
    ++p;
    a.velPct = static_cast<uint8_t>(genClamp(readInt(p), 0, 200));
    if (*p == ':') {
      ++p;
      a.gatePct = static_cast<uint16_t>(genClamp(readInt(p), 0, 800));
    }
    if (*p == ':') {
      ++p;
      a.nudge = static_cast<int8_t>(genClamp(readInt(p), -50, 50));
    }
    while (*p && !isBlankChar(*p)) ++p;
  }
  return true;
}

bool formatGenToken(const ArpStep& a, const char* mods, char* out, int cap, int& n) {
  auto put = [&](char c) {
    if (n + 1 >= cap) return false;
    out[n++] = c;
    return true;
  };
  if (a.kind == ArpKind::Rest) return put('.');
  if (a.kind == ArpKind::Tie) return put('-');
  if (!put(a.acc == ArpAcc::Accent ? 'X' : (a.acc == ArpAcc::Ghost ? 'o' : 'x'))) return false;
  if (a.len == ArpLen::Short && !put('s')) return false;
  if (a.len == ArpLen::Long && !put('l')) return false;
  if (a.slide && !put('~')) return false;
  for (const char* m = mods; m && *m; ++m)
    if (!put(*m)) return false;
  if (a.velPct || a.gatePct || a.nudge) {
    char num[24];
    int k = snprintf(num, sizeof(num), ":%d:%d", a.velPct, a.gatePct);
    if (a.nudge) k += snprintf(num + k, sizeof(num) - k, ":%d", a.nudge);
    for (int c = 0; c < k; ++c)
      if (!put(num[c])) return false;
  }
  return true;
}

uint8_t genVelocity(const ArpStep& a, int velLo, int velHi) {
  return static_cast<uint8_t>(a.velPct                  ? genClamp(velHi * a.velPct / 128, 1, 127)
                              : a.acc == ArpAcc::Ghost  ? velLo
                              : a.acc == ArpAcc::Accent ? velHi
                                                        : (velLo + velHi) / 2);
}

int genGatePct(const ArpStep& a, int gate, int rate) {
  // An exact gate ignores Short / Long; Gate 50 = the pattern as written.
  return a.gatePct ? a.gatePct * gate / 50 * rate
                   : (a.len == ArpLen::Short ? (gate / 2 < 5 ? 5 : gate / 2)
                                             : (a.len == ArpLen::Long ? 100 : gate)) * rate;
}

void mutateStep(ArpStep& a, int amount, Rng& r, int* pitchDraw) {
  const int hit = static_cast<int>(r.below(200)), acc = static_cast<int>(r.below(300));
  const int pitch = static_cast<int>(r.below(400));
  const uint32_t accV = r.below(3), pitchV = r.below(4);
  if (a.kind != ArpKind::Tie && hit < amount) a.kind = a.kind == ArpKind::Note ? ArpKind::Rest : ArpKind::Note;
  if (acc < amount) {
    a.acc = static_cast<ArpAcc>(accV);
    a.velPct = 0;  // a redrawn accent drops the exact velocity
  }
  if (pitchDraw) *pitchDraw = pitch < amount ? static_cast<int>(pitchV) : -1;
  if (pitch < amount) {
    a.pitch = pitchV == 3 ? ArpPitch::Next : static_cast<ArpPitch>(pitchV);
    a.oct = pitchV == 3 ? 1 : 0;
  }
}

}  // namespace mt
