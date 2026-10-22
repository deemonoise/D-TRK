#pragma once
#include <stdint.h>

namespace mt {

// Punch-in effects on the master (PERF mode); Project::perfMap says which one each track button
// holds. File format: PRF2 bytes, append only.
enum class PerfFx : uint8_t {
  None, Repeat, Roll, Reverse, Scratch, TapeStop, Gate, Filter, Crush, Downsample, Drive, DelayThrow,
  ReverbThrow, Freeze, ModThrow, TapeWobble, Count
};
constexpr int kPerfButtons = 8;
// A new project's track buttons 1..8.
constexpr uint8_t kPerfDefaultMap[kPerfButtons] = {
    static_cast<uint8_t>(PerfFx::Repeat),     static_cast<uint8_t>(PerfFx::Roll),
    static_cast<uint8_t>(PerfFx::Filter),     static_cast<uint8_t>(PerfFx::Gate),
    static_cast<uint8_t>(PerfFx::DelayThrow), static_cast<uint8_t>(PerfFx::ReverbThrow),
    static_cast<uint8_t>(PerfFx::Crush),      static_cast<uint8_t>(PerfFx::TapeStop)};
// A new project's XY pad axes (Project::perfXy): X, Y.
constexpr uint8_t kPerfXyDefault[2] = {static_cast<uint8_t>(PerfFx::Filter), static_cast<uint8_t>(PerfFx::Crush)};

// Buffer effects share the ring buffer's read head: one plays at a time.
constexpr bool perfBuffered(PerfFx f) { return f >= PerfFx::Repeat && f <= PerfFx::TapeStop; }

// Encoder range of an effect's depth; def: a PerfStack press, and a button's start Value (Project::perfDepth) when its effect is picked.
struct PerfRange {
  int8_t lo, hi, def;
  bool coarse;  // Shift turns x10
};
PerfRange perfRange(PerfFx f);
const char* perfName(PerfFx f);   // toast / PROJ text, "" for None and out of range
const char* perfShort(PerfFx f);  // 3 letters for the GRID / SONG header
void perfValue(PerfFx f, int depth, char* out, int n);  // toast value: "1/16", "LP 20", "6 BIT"
PerfFx perfFromLegacy(uint8_t old);  // PRFM byte (pre-2026-10-13 enum) -> the new effect

// A PERF message to the synth: 0xF6 op val (see Synth): op 0 press PerfFx val, 1 release PerfFx
// val, 2 turn the last pressed by static_cast<int8_t>(val), 3 release all.
struct PerfMsg {
  uint8_t op, val;
};

// Lengths behind the list-valued depths, in steps (sixteenths); depth is clamped into the range.
float perfRepeatSteps(int depth);   // Repeat: 1/2 1/4 1/8 1/8T 1/16 1/16T 1/32
float perfGateSteps(int depth);     // Gate:   1/4 1/8 1/8T 1/16 1/16T 1/32
float perfReverseSteps(int depth);  // Reverse: 1/4 1/2 1 2 bars
float perfScratchSteps(int depth);  // Scratch period: 1/4 1/8 1/16 1/32
float perfTapeSteps(int depth);     // Tape stop: 1/16 1/8 1/4 1/2 1 bar
int perfDownHz(int depth);          // 16000 8000 4000 2000 1000 500

// The held effects in press order (the last one gets the encoder) with their depths.
class PerfStack {
 public:
  void press(PerfFx f);           // to the top at its default depth (again: back to the default)
  void release(PerfFx f);
  void releaseAll() { n_ = 0; }
  void turn(int delta);           // the last pressed, clamped to its range
  // f on top at depth `to` (clamped into its range), for the XY pad: pressed when not held, pressed
  // again (to the top, at its default) when held under others, then turned. Nothing when f already
  // has that depth (the press order stays) or the stack is full. out: the same as synth messages,
  // in order (press, turn); returns how many (0..2).
  int setDepth(PerfFx f, int to, PerfMsg out[2]);
  bool held(PerfFx f) const;
  int depth(PerfFx f) const;      // default when not held
  PerfFx last() const { return n_ ? slot_[n_ - 1].fx : PerfFx::None; }
  PerfFx buffered() const;        // the newest held buffer effect, None if none
  int count() const { return n_; }
  PerfFx at(int i) const { return slot_[i].fx; }  // press order

 private:
  struct Slot {
    PerfFx fx;
    int8_t depth;
  };
  int find(PerfFx f) const;  // slot index, -1 when not held
  Slot slot_[kPerfButtons];
  int n_ = 0;
};

}  // namespace mt
