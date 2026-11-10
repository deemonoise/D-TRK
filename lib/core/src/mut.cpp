#include "mut.h"
#include "edit_ops.h"

namespace mt {

namespace {
uint8_t u(VarMode m) { return static_cast<uint8_t>(m); }
// An old file's EVO on: it made new changes.
bool evoLive(const EvoCfg& e) { return e.amount > 0 && (e.mask & kEvoMaskAll); }
}  // namespace

VarMode mutModeOf(const VarCfg& c, bool kit) {
  if (c.mode >= u(VarMode::Count)) return VarMode::Off;
  const VarMode m = static_cast<VarMode>(c.mode);
  return m == VarMode::Ca2d && !kit ? VarMode::Ca1d : m;
}

VarMode mutMode(const Project& p, int tr) {
  if (tr < 0 || tr >= kTracks) return VarMode::Off;
  if (p.var[tr].mode == u(VarMode::Off)) return VarMode::Off;  // no instrument lookup
  return mutModeOf(p.var[tr], p.trackIsDrum(tr));
}

bool mutOn(const Project& p, int tr) { return mutMode(p, tr) != VarMode::Off; }

int mutModes(bool kit, VarMode* out) {
  int n = 0;
  out[n++] = VarMode::Off;
  out[n++] = VarMode::Evo;
  out[n++] = VarMode::Ca1d;
  if (kit) out[n++] = VarMode::Ca2d;
  out[n++] = VarMode::Markov;
  return n;
}

VarMode mutCycle(const Project& p, int tr, int d) {
  VarMode ms[kMutModesMax];
  const int c = mutModes(p.trackIsDrum(tr), ms);
  const VarMode cur = mutMode(p, tr);
  int i = 0;
  while (i < c && ms[i] != cur) ++i;
  if (i == c) i = 0;
  const int step = d > 0 ? 1 : (d < 0 ? -1 : 0);
  return ms[(i + step + c) % c];
}

void mutSetMode(Project& p, int tr, VarMode m) {
  if (tr < 0 || tr >= kTracks) return;
  if (m >= VarMode::Count) m = VarMode::Off;
  if (m == VarMode::Ca2d && !p.trackIsDrum(tr)) m = VarMode::Ca1d;
  p.var[tr].mode = u(m);
  if (m != VarMode::Evo) return;
  EvoCfg& e = p.evo[tr];
  if (e.amount == 0) e.amount = kEvoAmountOn;
  if (!(e.mask & kEvoMaskAll)) e.mask = EvoCfg().mask;
}

void mutFix(Project& p, uint16_t parked) {
  for (int t = 0; t < kTracks; ++t) {
    VarCfg& v = p.var[t];
    if (v.mode >= u(VarMode::Count)) v.mode = u(VarMode::Off);
    if (v.mode == u(VarMode::Ca2d) && !p.trackIsDrum(t)) v.mode = u(VarMode::Ca1d);
    if ((parked >> t) & 1) continue;  // a MUT file's EvoCfg outside EVO: settings, not EVO on
    if (!evoLive(p.evo[t]) || v.mode == u(VarMode::Evo)) continue;
    if (v.mode == u(VarMode::Off)) v.mode = u(VarMode::Evo);
    else p.evo[t].mask = 0;  // VAR wins; EVO stacked on it is gone
  }
}

const Step& mutStep(const Project& p, const Pattern& pt, int tr, int idx, const EvoTrack& e, const VarTrack& v,
                    Step& tmp) {
  const uint8_t m = p.var[tr].mode;
  if (m == u(VarMode::Off) || m >= u(VarMode::Count)) return pt.steps[tr][idx];  // no lookups
  if (m == u(VarMode::Evo))
    return (p.evo[tr].mask & kEvoMaskAll) ? evoStep(pt, tr, idx, e, evoApplyOf(p, tr), tmp) : pt.steps[tr][idx];
  return varStep(pt, tr, idx, v, varApplyOf(p, tr), tmp);
}

void captureLive(Project& p, Pattern& pt, int tr, const EvoTrack& e, const VarTrack& v, const GenState& g,
                 int fromStep, Step* scratch) {
  if (tr < 0 || tr >= kTracks) return;
  if (mutOn(p, tr)) {
    const int len = evoLen(pt, tr);
    for (int i = 0; i < len; ++i) {  // built whole first: a step may read others (SHIFT, ghosts, CA, MARKOV)
      Step tmp;
      scratch[i] = mutStep(p, pt, tr, i, e, v, tmp);
    }
    for (int i = 0; i < len; ++i) pt.steps[tr][i] = scratch[i];
  }
  if (p.genOn(tr)) {
    captureGen(pt, tr, p.gen[tr], g, fromStep, p.scaleRoot, static_cast<ScaleType>(p.scaleType));
    p.gen[tr].mode = static_cast<uint8_t>(GenMode::Off);  // else it would replace the notes just written
  }
}

}  // namespace mt
