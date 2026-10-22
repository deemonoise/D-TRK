#pragma once
#include <stdint.h>
#include "model.h"
#include "scale.h"

namespace mt {

// Evolve (Project::evo, EvoCfg): every `every` pattern passes the track's state mutates (evoMutate); the
// sequencer plays each track step through it (evoStep), the pattern stays as it is. The state holds
// per-step deltas, indexed by the track's own step (trackStepAt), and the track's rotation. Every
// random choice is evoRand(run seed, track, mutation number, step, kind): no shared generator. The run
// seed is the follow one (followRunSeed: Project::seed, a fresh salt per live Play, salt 0 in Render),
// so Render always plays the same drift. See docs/plans/2026-10-21-evolve.md.
constexpr uint8_t kEvoDrop = 0x01;       // RHYTHM: the step's note is not played
constexpr uint8_t kEvoGhost = 0x02;      // RHYTHM: an empty step plays a neighbour's note, quietly
constexpr uint8_t kEvoGhostNext = 0x04;  // the ghost's neighbour: set = the next step, clear = the previous
constexpr uint8_t kEvoRtrgShift = 3;     // RTRG: bits 3..4, 0 = none, 1 = x2, 2 = x3
constexpr uint8_t kEvoRtrgBits = 0x18;
constexpr uint8_t kEvoSwapNext = 0x20;  // SHIFT: this step and the next play each other's steps
constexpr int kEvoDegMax = 24, kEvoVolMax = 48, kEvoVolStep = 8, kEvoRotMax = 8, kEvoGhostPct = 40;
// States per track the sequencer keeps for rewind(): a rewind goes back over the planned but unheard
// steps (the lookahead, Sequencer::lookUs, at most Sequencer::kHist steps) and a track mutates at most
// once per pass of >= kMinSteps steps, so the window rarely holds more than a pass end or two; 4 covers
// it. An older state than the ring holds restarts from no changes (rewind, deterministic). Keep it in
// step with the lookahead if that grows.
constexpr int kEvoRing = 4;
constexpr uint32_t kEvoNone = 0xFFFFFFFFu;  // a ring slot without a state

struct EvoStep {
  int8_t deg = 0;     // scale degrees, +-kEvoDegMax
  int8_t vol = 0;     // velocity, +-kEvoVolMax
  uint8_t flags = 0;  // kEvoDrop .. kEvoSwapNext
};
static_assert(sizeof(EvoStep) == 3, "EvoStep: 3 bytes");

struct EvoTrack {
  uint32_t n = 0;  // mutations since the reset
  int8_t rot = 0;  // the track rotated: output step i plays step i - rot (SHIFT)
  EvoStep step[kMaxSteps];
};

// The track's length in pattern pt: trackLen when shorter, else the pattern's (0 for a bad track).
int evoLen(const Pattern& pt, int tr);
void evoReset(EvoTrack& s);        // no changes, n = 0
void evoClearDeltas(EvoTrack& s);  // no changes, n kept (Capture)
bool evoIdle(const EvoTrack& s);   // no change anywhere
int evoChanged(const EvoTrack& s, int len);  // steps 0..len-1 with a change (UI)
uint32_t evoRand(uint32_t seed, int track, uint32_t n, int i, uint32_t salt);

// The pattern step output step i (0..len-1) plays: a SHIFT swap first, then the rotation. Without
// kEvoShift in mask: i.
int evoSource(const EvoTrack& s, int i, int len, uint8_t mask);

// What a mutation looks at: per output step, what plays there now (after SHIFT).
enum : uint8_t { kEvoKEmpty, kEvoKNote, kEvoKOther };  // no note / a note / OFF
struct EvoView {
  uint8_t len = 0;
  uint8_t degs = 12;   // scale degrees per octave (an octave jump)
  bool noteOk = true;  // NOTE may change this track (not a drum or GEN track)
  uint8_t kind[kMaxSteps] = {};
};
void evoMakeView(const Pattern& pt, int tr, const EvoTrack& s, uint8_t mask, bool noteOk, ScaleType t,
                 EvoView& v);

// How a track plays its state: the mask in effect (RHYTHM off on a GEN GATE track: the generator makes
// the rhythm), a drum track (note = velocity, vel = lane mask), NOTE allowed (not drum, not GEN), the
// track's default velocity, the project scale.
struct EvoApply {
  uint8_t mask = 0;
  bool drum = false;
  bool noteOk = true;
  uint8_t defVel = 100;
  uint8_t root = 0;
  ScaleType scale = ScaleType::Chromatic;
};
// RHYTHM may change track tr (not a GEN GATE track).
bool evoRhythmOk(const Project& p, int tr);
// EvoApply of track tr of p now (the sequencer, Capture).
EvoApply evoApplyOf(const Project& p, int tr);
// Output step idx (0 <= idx < kMaxSteps) of track tr as the state plays it: SHIFT (the source step),
// RHYTHM (drop: no note, fx kept; ghost on an empty step: the neighbour's note / lane mask at
// kEvoGhostPct % of defVel, without its fx), NOTE (moveDegrees), RTRG+VOL (velocity, a RAT x2 / x3
// in a free slot unless the step has one). Only the kinds in a.mask. Returns the pattern's step itself
// when nothing changes, else tmp.
const Step& evoStep(const Pattern& pt, int tr, int idx, const EvoTrack& s, const EvoApply& a, Step& tmp);

// One mutation of track `track`'s state (n -> n + 1, steps 0..v.len-1): Home first (each change one unit
// back to the pattern with chance home %: deg 1, vol kEvoVolStep, flags cleared, rot 1), then new
// changes with strength amount (NOTE, RHYTHM, RTRG+VOL per step, SHIFT once; see the plan). c is clamped.
void evoMutate(EvoTrack& s, const EvoCfg& c, const EvoView& v, uint32_t seed, int track);

// Capture: writes steps 0..evoLen-1 of track tr as the state plays them (evoStep with a) into pt;
// scratch: kMaxSteps steps of the caller's (the result is built before any step is overwritten).
void evoCapture(Pattern& pt, int tr, const EvoTrack& s, const EvoApply& a, Step* scratch);

// Values into range (files, UI): amount, home <= 100, every 1..kEvoEveryMax, mask kEvoMaskAll bits.
void evoClamp(EvoCfg& c);

}  // namespace mt
