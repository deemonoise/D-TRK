#pragma once
#include <stdint.h>
#include "evolve.h"
#include "model.h"
#include "shift_gen.h"
#include "var_gen.h"

namespace mt {

// MUT: one mutator per track, VarCfg::mode (Project::var) picks it: OFF, EVO (EvoCfg, evolve.h), CA1D,
// CA2D (KIT tracks only), MARKOV (var_gen.h). Only that mode plays and advances; a step plays
// pattern -> MUT (mutStep) -> GEN -> expand. See docs/plans/2026-10-27-mut-design.md.

// The mode that plays: CA2D on a melodic track plays as CA1D; Count and above -> Off.
VarMode mutModeOf(const VarCfg& c, bool kit);
VarMode mutMode(const Project& p, int tr);  // of track tr (Off out of range)
bool mutOn(const Project& p, int tr);       // mutMode != Off

// The modes the UI offers, in order: OFF, EVO, CA1D, (KIT: CA2D,) MARKOV. Returns the count.
constexpr int kMutModesMax = 5;
int mutModes(bool kit, VarMode* out);

// The Mut row (UI): the mode one step from the one shown (mutMode) in mutModes' order, with wrap, toward
// d's sign whatever its size (Shift x10 too); d = 0: the shown mode.
VarMode mutCycle(const Project& p, int tr, int d);

// EVO's amount when the track enters EVO with amount 0 (mutSetMode).
constexpr uint8_t kEvoAmountOn = 25;
// Sets track tr's mode (UI). CA2D on a melodic track -> CA1D. Entering EVO: amount 0 -> kEvoAmountOn,
// mask 0 -> EvoCfg()'s, so the mode does something. The other settings stay. The caller posts
// engine::Cmd::MutEdit after (the sequencer resets the track's state on a mode change).
void mutSetMode(Project& p, int tr, VarMode m);

// After loading a project: per track, CA2D on a melodic track -> CA1D (stored; live, mutModeOf plays and
// shows it as CA1D and the stored mode stays), then for old files (EVO and VAR were two stacked
// generators): VAR on and EVO on (amount > 0, mask) -> VAR, EVO mask 0; VAR off and EVO on -> EVO.
// parked: bit t = track t's TEVO record says its EvoCfg is not the active mode (kept settings, no rule).
void mutFix(Project& p, uint16_t parked = 0);

// Output step idx (0 <= idx < kMaxSteps) of track tr of pt as MUT plays it with state e (EVO) or v (VAR):
// the pattern's step itself when nothing changes, else tmp.
const Step& mutStep(const Project& p, const Pattern& pt, int tr, int idx, const EvoTrack& e, const VarTrack& v,
                    Step& tmp);

// Capture live, the pure part: writes steps 0..evoLen-1 of track tr of pt as the track plays them now: MUT
// (state e / v), then GEN as captureGen does (register g, from track step fromStep, flip 0), and turns
// GEN off. scratch: kMaxSteps steps of the caller's. The caller resets the MUT state after (the pattern
// holds it now): Sequencer::captureLive does it all.
void captureLive(Project& p, Pattern& pt, int tr, const EvoTrack& e, const VarTrack& v, const GenState& g,
                 int fromStep, Step* scratch);

// Sequencer::captureLive / engine::captureLive.
enum class LiveCapture : uint8_t {
  Ok,
  Off,           // neither GEN nor MUT plays on the track: nothing to capture
  OtherPattern,  // playing, and the pattern is not the heard one
};

}  // namespace mt
