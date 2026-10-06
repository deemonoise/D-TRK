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

// 8 ratchets x 8 KIT lanes x (on, off) (a melodic step needs 8 x 4 chord notes x 2), plus controls
// from the other fx slots.
constexpr int kMaxStepEvents = 8 * kKitLanes * 2 + kFxSlots;
constexpr uint32_t kMinGateUs = 1000;

struct ExpandOut {
  int count = 0;
  bool tie = false;  // last NoteOn has no NoteOff: the engine releases it later
  int32_t offUs = -1;  // OFF: when the track goes silent, relative to the step start; -1 = no OFF
  StepEvent ev[kMaxStepEvents];
};

struct ExpandCtx {
  uint32_t stepUs;
  uint32_t loop;  // pass counter of the pattern, for CND
  uint8_t scaleRoot;
  ScaleType scale;
  uint16_t tps = 24;  // ticks per step (OFF)
  const Instrument* kit = nullptr;  // the track's KIT: steps are lane masks (drum track)
  bool fill = false;  // fill held: CND FIL steps play, NFL steps do not
};

// Expands one step of one track into events. Control events (CC, pitch bend,
// program; synth fx on INT tracks only) come first, even on a step without a note or on OFF. Then, per
// ratchet hit and per chord note, each NoteOn is directly followed by its
// NoteOff (except a tied last note). OFF sets offUs (not before kMinGateUs on a step with a note),
// cuts the step's notes there and cancels TIE. Returns false if nothing plays and there is no OFF.
// On a drum track (c.kit set) Step::vel is the lane mask and Step::note the step velocity (0 =
// defVel): one NoteOn per set lane per ratchet with the lane's note, 60 % velocity for lanes outside
// ACC; TIE, CHD, NRN and STR are ignored. An empty mask is still a note step (returns true).
bool expandStep(const Step& s, const TrackCfg& t, const ExpandCtx& c, Rng& rng, ExpandOut& out);

}  // namespace mt
