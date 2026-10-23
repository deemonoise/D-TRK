#include "mutate.h"
#include <string.h>
#include "rng.h"

namespace mt {

bool mutateApplies(const Instrument& m, int strength) {
  return m.type != InstrType::Kit && m.type < InstrType::Count && strength > 0 && strength <= kMutateMax;
}

namespace {

// Moves fields by up to strength % of half their range: one draw per moved field, in a fixed order.
struct Mover {
  Rng& r;
  int strength;
  template <class T>
  void move(T& v, int lo, int hi) {
    const int x = v;
    const int span = (hi - lo) * strength / 200;
    const int d = static_cast<int>(r.below(2u * static_cast<uint32_t>(span) + 1u)) - span;
    if (x < lo || x > hi) return;  // outside its range (a preset's own value): stays
    const int y = x + d;
    v = static_cast<T>(y < lo ? lo : (y > hi ? hi : y));
  }
};

}  // namespace

bool mutateInstrument(Instrument& m, int strength, uint32_t seed) {
  if (!mutateApplies(m, strength)) return false;
  Rng r(seed);
  Mover mv{r, strength};
  const InstrType t = m.type;
  if (t == InstrType::Chip || t == InstrType::Sample || t == InstrType::Synth || t == InstrType::Lofi ||
      (t == InstrType::Fm && fmGated(m.machine))) {
    mv.move(m.attack, 0, 100);
    mv.move(m.decay, 1, 127);
    mv.move(m.sustain, 0, 127);
    mv.move(m.release, 0, 110);
  }
  if (m.fltMode != static_cast<uint8_t>(FltMode::Off)) {
    mv.move(m.cutoff, 10, 127);
    mv.move(m.reso, 0, 110);
    mv.move(m.fenv, -64, 63);
    mv.move(m.fAtk, 0, 100);
    mv.move(m.fDec, 1, 127);  // 0 = hold: outside, stays
  }
  if (t == InstrType::Fm || t == InstrType::Drum || t == InstrType::Synth)
    for (uint8_t& k : m.macro) mv.move(k, 0, 127);
  if (t == InstrType::Lofi)
    for (int k = 0; k < kFmMacros; ++k)
      if (*lofiMacroName(m.machine, k)) mv.move(m.macro[k], 0, 127);  // not a slot the machine leaves unused
  if (t == InstrType::Synth) {
    mv.move(m.synSub, 0, 127);
    mv.move(m.synNoise, 0, 80);
    mv.move(m.synEAtk, 0, 100);
    mv.move(m.synEDec, 1, 127);
  }
  if (t == InstrType::Chip && m.wave == static_cast<uint8_t>(Wave::Pulse)) {
    const bool pwm = m.pwmDepth > 0;  // a PWM-less pulse gets no sweep speed
    mv.move(m.duty, 5, 95);
    mv.move(m.pwmDepth, 0, 49);
    if (pwm) mv.move(m.pwmRate, 0, 127);
  }
  return true;
}

bool mutateSlot(Project& p, int slot, int strength, uint32_t seed, MutateUndo& undo) {
  if (slot < 0 || slot >= kInstruments) return false;
  Instrument& m = p.instruments[slot];
  if (!mutateApplies(m, strength)) return false;
  memcpy(&undo.m, &m, sizeof(Instrument));
  undo.slot = static_cast<int8_t>(slot);
  const bool ok = mutateInstrument(m, strength, seed);
  memcpy(&undo.after, &m, sizeof(Instrument));
  return ok;
}

bool mutateCanRestore(const Project& p, const MutateUndo& undo) {
  return undo.slot >= 0 && undo.slot < kInstruments &&
         memcmp(&p.instruments[undo.slot], &undo.after, sizeof(Instrument)) == 0;
}

bool mutateRestore(Project& p, MutateUndo& undo) {
  if (!mutateCanRestore(p, undo)) {
    undo.slot = -1;
    return false;
  }
  memcpy(&p.instruments[undo.slot], &undo.m, sizeof(Instrument));
  undo.slot = -1;
  return true;
}

}  // namespace mt
