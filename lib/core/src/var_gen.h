#pragma once
#include <stdint.h>
#include "evolve.h"
#include "model.h"

namespace mt {

// VAR (VarCfg): every `every` pattern passes the track's state takes a new generation (varNext) and the
// sequencer plays each track step through it (varStep); the pattern stays as it is. CA: cells are bits
// per track step, 1 row on a melodic track, 8 rows (KIT lanes) on a drum track; generation 0 is the
// pattern (steps with a note, KIT: lane bits). MARKOV: a new sequence from the pattern's own transitions.
// Every random choice is varRand(run seed, track, generation n, step, kind): no shared generator. See
// docs/plans/2026-10-23-generative-2-design.md.
constexpr int kVarRows = 8;                  // CA rows: KIT lanes (melodic: row 0 only)
constexpr int kVarRowBytes = kMaxSteps / 8;  // bit i of a row = step i
constexpr int kVarTokMax = 16;               // MARKOV: distinct tokens (+ REST)
constexpr uint8_t kVarRest = 0xFF;           // MARKOV src: the step plays no note
constexpr uint8_t kVarRootOct = 60;          // a cell on a track without notes: root % 12 + this (C-4)

struct VarCells {
  uint8_t r[kVarRows][kVarRowBytes];
};

struct VarTrack {
  uint32_t n = 0;          // generations since the reset
  uint8_t sinceReset = 0;  // generations since generation 0; 0 = plays the pattern
  uint8_t mode = 0;        // the VarMode that built the state (varModeOf)
  union {
    VarCells cells;           // CA
    uint8_t src[kMaxSteps];   // MARKOV: per step the pattern step it plays (itself = as is), or kVarRest
  };
  VarTrack() : cells() {}
};

inline bool varBit(const uint8_t* row, int i) { return (row[i >> 3] >> (i & 7)) & 1; }
inline void varSetBit(uint8_t* row, int i, bool v) {
  if (v) row[i >> 3] = static_cast<uint8_t>(row[i >> 3] | (1u << (i & 7)));
  else row[i >> 3] = static_cast<uint8_t>(row[i >> 3] & ~(1u << (i & 7)));
}

uint32_t varRand(uint32_t seed, int track, uint32_t n, int i, uint32_t salt);
// The mode that plays: CA2D on a melodic track plays as CA1D; Count and above -> Off.
VarMode varModeOf(const VarCfg& c, bool kit);
void varReset(VarTrack& s);  // n = 0, generation 0
// Values into range (files, UI): mode, every 1..kVarEveryMax, rule2, reset, noise, amount, order 1..2,
// rhythm 0 / 1.
void varClamp(VarCfg& c);

// One CA1D generation of a row (Wolfram rule, neighbours i - 1, i + 1 circular over len), in place.
void varCa1d(uint8_t* row, int len, uint8_t rule);
// One Life-like generation (VarRule2) on a torus of kVarRows rows x len (Moore neighbourhood), in place.
void varCa2d(VarCells& c, int len, uint8_t rule2);
// Generation 0 of track tr: melodic: row 0 = steps with a note; KIT: row k = lane k of note steps.
void varGen0(const Pattern& pt, int tr, bool kit, VarCells& out);
// One CA generation (n -> n + 1) from the state (generation 0 = gen0): the rule (KIT CA1D: each row on
// its own), then with chance noise % one random cell flips. Back to generation 0 (sinceReset = 0) after
// `reset` generations (0 = never), when all cells die or when nothing changed. c is clamped.
void varCaNext(VarTrack& s, const VarCfg& c, const VarCells& gen0, int len, bool kit, uint32_t seed, int track);

// The step output step idx of track tr plays from cells c: alive + a note: the step; alive + no note: a
// copy of the nearest step with a note to the left (circular), whole; no note on the track: root % 12 +
// kVarRootOct at the default velocity (KIT: velocity 0 = the track's); dead: the step without its note
// (fx kept). KIT: the lane mask is the step's alive bits. Returns the pattern's step itself when nothing
// changes, else tmp.
const Step& varCellStep(const Pattern& pt, int tr, int idx, const VarCells& c, bool kit, uint8_t root,
                        Step& tmp);

// MARKOV tokens of track tr: the note steps (and with rhythm the others, as REST = ntok) in step order.
// Token values: distinct notes (KIT: lane masks) in order of appearance, up to kVarTokMax; more map to
// the nearest one (pitch; KIT: fewest lanes apart). first[t]: the first step holding token t.
struct VarSeq {
  uint8_t m = 0;     // sequence length
  uint8_t ntok = 0;  // distinct tokens (REST not counted)
  uint8_t val[kVarTokMax] = {};    // token -> note / lane mask
  uint8_t first[kVarTokMax] = {};  // token -> step
  uint8_t pos[kMaxSteps] = {};     // sequence -> step
  uint8_t tok[kMaxSteps] = {};     // sequence -> token (ntok = REST)
};
void varSeq(const Pattern& pt, int tr, bool kit, bool rhythm, VarSeq& q);
// A Markov sequence of track tr (generation n): src[i] for each step 0..len-1 (see VarTrack::src).
// Starts at the pattern's first token; each next token, with chance amount %, follows the chain (order 1
// or 2, transitions counted circularly over the sequence, picked by their count), else is the pattern's.
// A state without successors keeps the pattern's. A token equal to the pattern's keeps the step. c is
// clamped.
void varMarkov(const Pattern& pt, int tr, const VarCfg& c, bool kit, uint32_t seed, uint32_t n, uint8_t* src);

// One generation of track tr's state as c says (varCaNext from the pattern's generation 0, or a new
// varMarkov with sinceReset = 1). A changed mode starts from generation 0. Off: nothing.
void varNext(VarTrack& s, const VarCfg& c, const Pattern& pt, int tr, bool kit, uint32_t seed);

// How a track plays its state: the mode in effect (varModeOf), a drum track, MARKOV rhythm, scale root.
struct VarApply {
  uint8_t mode = 0;
  bool kit = false;
  bool rhythm = false;
  uint8_t root = 0;
};
VarApply varApply(const VarCfg& c, bool kit, uint8_t root);
// VarApply of track tr of p now (the sequencer, Capture): a KIT track is a drum track.
VarApply varApplyOf(const Project& p, int tr);
// Output step idx (0 <= idx < kMaxSteps) of track tr as the state plays it: the pattern's step at
// generation 0 or when the state was built by another mode; CA: varCellStep; MARKOV: src (rhythm: the
// source step whole, REST drops the note; without rhythm only a note step's note / lane mask changes).
// Returns the pattern's step itself when nothing changes, else tmp.
const Step& varStep(const Pattern& pt, int tr, int idx, const VarTrack& s, const VarApply& a, Step& tmp);

// Track tr as VAR plays it, the source EVO works on (order pattern -> VAR -> EVO -> GEN). The view must
// outlive the EvoSrc.
struct VarView {
  const Pattern* pt = nullptr;
  int tr = 0;
  const VarTrack* s = nullptr;
  VarApply a;
};
EvoSrc varSrc(const VarView& v);
// Capture: writes steps 0..evoLen-1 of track tr as VAR, then EVO play them (state v, e) into pt; scratch:
// kMaxSteps steps of the caller's. The caller then clears both states (Sequencer::varClear, and evoClear
// when e is not idle: its changes are in the pattern now).
void varCapture(Pattern& pt, int tr, const VarTrack& v, const VarApply& va, const EvoTrack& e, const EvoApply& ea,
                Step* scratch);

}  // namespace mt
