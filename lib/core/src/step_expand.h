#pragma once
#include <stdint.h>
#include "model.h"
#include "rng.h"
#include "scale.h"

namespace mt {

enum class EvKind : uint8_t { NoteOn, NoteOff, Cc, PitchBend, Program, SynthFx };

struct StepEvent {
  int32_t offsetUs;  // relative to the step start, may be negative (NDG)
  EvKind kind;
  uint8_t ch;
  uint8_t note;  // Cc: controller; PitchBend: LSB; Program: program; SynthFx: Fx
  uint8_t vel;   // Cc: value; PitchBend: MSB; SynthFx: value
};

// 8 ratchets x 4 chord notes x 2, plus controls. A step has two fx slots, so
// CHD + RAT leave no room for controls; the margin is spare.
constexpr int kMaxStepEvents = 72;
constexpr uint32_t kMinGateUs = 1000;

struct ExpandOut {
  int count = 0;
  bool tie = false;  // last NoteOn has no NoteOff: the engine releases it later
  StepEvent ev[kMaxStepEvents];
};

struct ExpandCtx {
  uint32_t stepUs;
  uint32_t loop;  // pass counter of the pattern, for CND
  uint8_t scaleRoot;
  ScaleType scale;
};

// Expands one step of one track into events. Control events (CC, pitch bend,
// program; synth fx on INT tracks only) come first, even on a step without a note or on OFF. Then, per
// ratchet hit and per chord note, each NoteOn is directly followed by its
// NoteOff (except a tied last note). Returns false if nothing plays.
bool expandStep(const Step& s, const TrackCfg& t, const ExpandCtx& c, Rng& rng, ExpandOut& out);

}  // namespace mt
