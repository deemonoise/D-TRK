#include "automation.h"
#include "fx_info.h"
#include <utility>
namespace mt {
namespace {
constexpr Fx params[] = {Fx::FLT, Fx::RES, Fx::DCY, Fx::COL, Fx::SHP, Fx::SWP,
                         Fx::CON, Fx::DLY, Fx::RVB, Fx::PAN, Fx::DRV, Fx::BIT,
                         Fx::SRR, Fx::DTY, Fx::GTE, Fx::MOD, Fx::SAT};
bool valid(Fx fx) {
  for (Fx f : params)
    if (fx == f)
      return true;
  return false;
}
bool room(const Step &s, Fx f) {
  for (const auto &x : s.fx)
    if (x.cmd == f || x.cmd == Fx::None)
      return true;
  return false;
}
bool bounds(int len) { return len > 0 && len <= kMaxSteps; }
} // namespace
Fx automationFx(int index) {
  return params[(index % kAutomationParams + kAutomationParams) %
                kAutomationParams];
}
int automationValue(const TrackCfg &t, const Instrument &i, Fx f) {
  if (f >= Fx::DCY && f <= Fx::CON) {
    if (i.type != InstrType::Fm && i.type != InstrType::Drum &&
        i.type != InstrType::Synth && i.type != InstrType::String &&
        i.type != InstrType::Lofi)
      return -1;
    return i.macro[static_cast<int>(f) - static_cast<int>(Fx::DCY)];
  }
  switch (f) {
  case Fx::FLT:
    return i.cutoff;
  case Fx::RES:
    return i.reso;
  case Fx::DLY:
    return i.send;
  case Fx::RVB:
    return i.rsend;
  case Fx::SAT:
    return i.type == InstrType::Fm ? i.fmSat : -1;
  case Fx::PAN:
    return t.pan;
  case Fx::DRV:
    return t.drive;
  case Fx::BIT:
    return t.crushBits;
  case Fx::SRR:
    return t.crushRate;
  case Fx::DTY:
    return t.driveModel;
  case Fx::GTE:
    return t.gateDepth;
  case Fx::MOD:
    return t.modSend;
  default:
    return -1;
  }
}
int automationGet(const Step &s, Fx f) {
  int v = -1;
  for (const auto &x : s.fx)
    if (x.cmd == f)
      v = x.val;
  return v;
}
bool automationSet(Step &s, Fx f, uint8_t v) {
  if (!valid(f) || !room(s, f))
    return false;
  int slot = -1;
  for (int n = 0; n < kFxSlots; ++n)
    if (s.fx[n].cmd == f)
      slot = n;
  if (slot < 0)
    for (int n = 0; n < kFxSlots; ++n)
      if (s.fx[n].cmd == Fx::None) {
        slot = n;
        break;
      }
  automationClear(s, f);
  s.fx[slot] = {f, fxStep(f, v, 0)};
  return true;
}
void automationClear(Step &s, Fx f) {
  for (auto &x : s.fx)
    if (x.cmd == f)
      x = FxSlot{};
}
bool automationRamp(Step *s, int len, Fx f, int a, int b, uint8_t va,
                    uint8_t vb) {
  if (!s || !bounds(len) || !valid(f) || a < 0 || b < 0 || a >= len || b >= len)
    return false;
  if (a > b) {
    std::swap(a, b);
    std::swap(va, vb);
  }
  for (int n = a; n <= b; ++n)
    if (!room(s[n], f))
      return false;
  for (int n = a; n <= b; ++n) {
    const int v = a == b ? va
                         : (static_cast<int>(va) * (b - n) +
                            static_cast<int>(vb) * (n - a)) /
                               (b - a);
    automationSet(s[n], f, static_cast<uint8_t>(v));
  }
  return true;
}
void automationCopy(const Step *s, int len, Fx f, AutomationClip &c) {
  c.len = s && bounds(len) && valid(f) ? len : 0;
  for (int n = 0; n < c.len; ++n)
    c.value[n] = automationGet(s[n], f);
}
bool automationPaste(Step *s, int len, Fx f, const AutomationClip &c) {
  if (!s || !bounds(len) || !bounds(c.len) || !valid(f))
    return false;
  const int count = len < c.len ? len : c.len;
  for (int n = 0; n < count; ++n)
    if (c.value[n] >= 0 && !room(s[n], f))
      return false;
  for (int n = 0; n < count; ++n) {
    if (c.value[n] < 0)
      automationClear(s[n], f);
    else
      automationSet(s[n], f, static_cast<uint8_t>(c.value[n]));
  }
  return true;
}
void MotionLatch::clearValues() {
  for (auto &v : value) v = -1;
}
void MotionLatch::clear() {
  clearValues();
  hasPass_ = false;
}
void MotionLatch::beginPass(uint32_t pass) {
  if (hasPass_ && pass_ != pass) {
    clearValues();
  }
  hasPass_ = true;
  pass_ = pass;
}
void MotionLatch::touch(const int16_t *before, const int16_t *after) {
  for (int n = 0; n < kAutomationParams; ++n)
    if (after[n] >= 0 && before[n] != after[n])
      value[n] = after[n];
}
bool MotionLatch::write(Step &s) const {
  Step out = s;
  for (int n = 0; n < kAutomationParams; ++n)
    if (value[n] >= 0 &&
        !automationSet(out, automationFx(n), static_cast<uint8_t>(value[n])))
      return false;
  s = out;
  return true;
}
int automationStep(const Pattern &p, int tr, int pos, uint32_t loop,
                   uint8_t phase) {
  if (tr < 0 || tr >= kTracks || !bounds(p.length))
    return 0;
  const auto speed = toSpeed(p.trackSpeed[tr]);
  const int div = speedDiv(speed);
  if (div > 1) {
    const uint64_t c = static_cast<uint64_t>(loop) * p.length + pos;
    const int len =
        p.trackLen[tr] && p.trackLen[tr] < p.length ? p.trackLen[tr] : p.length;
    return static_cast<int>((c / div) % len);
  }
  return trackStepAt(p, tr, pos, loop, phase * speedMul(speed) / 256);
}
} // namespace mt
