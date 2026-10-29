#pragma once
#include <stdint.h>
#include "ble_midi_parse.h"
#include "model.h"

namespace mt {

// Assigned functions take priority over ordinary notes. Enum order resolves overlaps.
// Note is a reserved legacy slot in the saved device blob; it is ignored.
enum class PadMode : uint8_t { Note, Pat, Perf, Mute, Count };
constexpr int kPads = 16;
// NOTE pad on a KIT track: pads 1-16 are its lanes 1-16; -1 = no lane.
inline int kitPadLane(int pad) { return pad >= 0 && pad < kKitLanes && pad < kPads ? pad : -1; }
const char* padModeName(PadMode m);  // "NOTE", "PAT", "PERF", "MUTE"; "?" for Count

// Device setting (NVS): the first note and channel each pad bank of the controller sends; a pad
// is (ch, base .. base + 15), or 8 notes for PERF. base 0xFF = unassigned.
// The legacy Note slot remains in the blob for compatibility and is ignored.
struct PadLearn {
  uint8_t ch[static_cast<int>(PadMode::Count)];
  uint8_t base[static_cast<int>(PadMode::Count)];
  PadLearn() { clear(); }
  void clear();
};

enum class ActKind : uint8_t {
  None,
  NoteOn,
  NoteOff,      // a: original MIDI note 0..127, b: velocity (NoteOff: 0)
  Pat,          // a: pattern 0..15 (press only)
  Mute,         // a: track 0..15 (press only)
  PerfOn,
  PerfOff,      // a: pad 0..7
  Knob,         // a: index into Project::knobs, b: (int8) delta, never 0 (no move = None)
  LearnPad,     // learn armed: a: the mode learned (ch / base stored already)
  KnobUnbound   // a CC with no binding: a = cc, b = channel (the UI may learn it)
};
struct Action {
  ActKind kind;
  uint8_t a;
  int8_t b;
};

// Pure: what a message from the controller does. route() and armLearn() run on the UI task only
// (the App drains the BLE queue there); `p` is the live project, read without the project lock:
// knobs[] is written by the UI task only, too.
class MidiRouter {
 public:
  MidiRouter() { reset(); }
  // The next note-on teaches PAT / PERF / MUTE; legacy Note and Count disarm.
  void armLearn(PadMode m) { learn_ = m > PadMode::Note && m < PadMode::Count ? static_cast<int8_t>(m) : -1; }
  void disarm() { learn_ = -1; }
  int learning() const { return learn_; }  // the armed PadMode, -1 = none
  Action route(const MidiMsg& m, PadLearn& pads, const Project& p);
  // On every connect: disarms learn and forgets every absolute knob reference (so the first value
  // of each knob only sets it). PadLearn and Project are not touched.
  void reset();
  // A bind newly learned into slot i: its first absolute value only sets the reference (a freed slot
  // may still hold an old reference of the same ch / cc).
  void forget(int i) {
    if (i >= 0 && i < kKnobBinds) last_[i] = 0xFF;
  }

 private:
  int8_t learn_ = -1;
  // Last absolute CC value per bind, 0xFF = none yet, and the (ch, cc) it was taken from: a bind
  // changed since (rebind, project load) counts as none.
  uint8_t last_[kKnobBinds];
  uint8_t lastCh_[kKnobBinds], lastCc_[kKnobBinds];
};

}  // namespace mt
