#pragma once
#include <stdint.h>
#include "perf_fx.h"
#include "synth_drive.h"
#include "synth_filter.h"

namespace mt {

// Master punch-in effects (PERF): after the compressor and the key track, before MAIN. Writes the
// mix into a stereo ring buffer (int16, caller-owned, PSRAM on the device) for the buffer effects.
// Nothing held: the mix is untouched (bit-exact) and only the buffer is written.
// Chain: buffer stage (REPEAT .. TAPE STOP, one at a time) -> GATE -> FILTER -> CRUSH -> DOWNSAMPLE
// -> DRIVE. Throws and freeze only hand send levels to the synth.
class PerfBus {
 public:
  static constexpr int kXfade = 64;        // 2 ms crossfades: effect in / out, loop seams
  static constexpr float kScale = 8191.f;  // float mix +-4 <-> int16
  static constexpr uint32_t kTail = 128000;  // 4 s of send returns after a throw / freeze
  static constexpr float kGridWrap = 64.f;   // 4 bars: a multiple of every gate length

  // frames stereo frames (buf holds 2 x frames int16), nullptr = buffer effects silent. clear
  // zeroes it; pass false for a buffer already zeroed (the audio task: no 768 KB memset there).
  void setBuffer(int16_t* buf, uint32_t frames, bool clear = true);
  void press(PerfFx f) { stack_.press(f); }
  void release(PerfFx f) { stack_.release(f); }
  void turn(int delta) { stack_.turn(delta); }
  void releaseAll() { stack_.releaseAll(); }
  const PerfStack& stack() const { return stack_; }
  // Grid: 0xFA restarts it at step 0; it advances only while running (the synth passes "not
  // paused": it does not know stop, the UI releases everything on stop).
  void transportStart() { gridPos_ = 0; }
  // Step clock (0xF6 4): grid step step (mod kGridWrap) starts offset samples into the next
  // process() block. That block's start is re-anchored to it (no drift against the sequencer);
  // dropped if the block is not running.
  void stepAt(uint8_t step, int offset) {
    syncStep_ = step;
    syncOff_ = offset;
  }
  double gridPos() const { return gridPos_; }  // steps since transportStart, modulo kGridWrap
  // Throws and freeze, read by the synth for this block (before delay / reverb).
  float delaySend() const;   // 0..1, x the mono mix into the delay send
  float reverbSend() const;  // 0..1
  bool freeze() const { return stack_.held(PerfFx::Freeze); }
  float freezeDry() const;   // live mix gain under a frozen reverb, 0..1
  // The return levels stay up for a while after a throw / freeze ends (its tail): the delay's after
  // DELAY THROW, the reverb's after REVERB THROW or FREEZE. Updated by process().
  bool delayTail() const { return dTail_ > 0; }
  bool reverbTail() const { return rTail_ > 0; }
  bool sendTail() const { return dTail_ > 0 || rTail_ > 0; }
  // Back to rest at once (audio park): nothing held, no tails, heads, fades and effect state
  // cleared. The ring content and the grid stay.
  void reset();
  void process(float* l, float* r, int n, float samplesPerStep, bool running);

 private:
  // The tone effects in chain order; fade_[t] is each one's wet amount 0..1.
  enum Tone : uint8_t { kGate, kFilter, kCrush, kDown, kDrive, kTones };

  // A read head on the ring: plays frame base + off (linear interpolation), advancing one sample
  // at a time. Offsets stay relative to base so a float keeps sub-frame precision anywhere in the
  // ring. REPEAT / ROLL / REVERSE run at speed +-1 from an integer frame, so their reads are exact;
  // the interpolation matters for SCRATCH and TAPE STOP. Looping heads count left down by one per
  // sample and seam at left <= 0; the overshoot (-left, 0..1) shortens the next loop, so lengths
  // alternate between floor and ceil and keep their fractional average (1/16T at 1000 samples per
  // step: 667, 667, 666, ...). fx None = live input. No head reads at or past the writer.
  struct Head {
    PerfFx fx = PerfFx::None;
    uint32_t base = 0;   // ring frame of off 0: REPEAT / ROLL loop start, else the press point
    float off = 0;       // read position, frames from base (REVERSE / SCRATCH: <= 0)
    float speed = 1;     // frames per sample: REPEAT / ROLL 1, REVERSE -1, TAPE STOP 1 -> 0
    bool seams = true;   // TAPE STOP: none
    float left = 0;      // samples to the next seam (SCRATCH: to the period end)
    float steps = 0;     // REPEAT / ROLL loop length in steps (ROLL halves it)
    int loops = 0;       // ROLL: loops since the last halving
    int32_t prot = 0;    // lowest frame read, from base: the writer must not come round onto it
    uint32_t avail = 0;  // REVERSE / SCRATCH: frames recorded without a pause behind base at press
    // SCRATCH: off = amp x (s - 1), (c, s) = (cos, sin) of the phase rotated by (rc, rs)
    float amp = 0, period = 0, c = 1, s = 0, rc = 1, rs = 0;
    // TAPE STOP: speed = v0 - u x dec, set exactly once per block and stepped by dec inside it
    // (u samples since the last slope change: a turn keeps the current speed, new slope); gain
    // fades the output once stopped.
    float dec = 0, v0 = 1, u = 0, gain = 1;
  };

  // The head's current sample into l, r; advances it one sample.
  static void play(Head& h, const int16_t* b, uint32_t frames, float& l, float& r);
  void settle(Head& h) const;  // per block: SCRATCH renormalised, TAPE STOP base moved up
  void writeBuffer(const float* l, const float* r, int n);
  bool writeBlocked(uint32_t w, int n) const;  // the writer would come round onto the head
  void switchHead(PerfFx want, uint32_t w0, int n, double grid0, float samplesPerStep,
                  bool running);
  // REVERSE / SCRATCH need this many frames recorded behind the press point to start.
  bool canCapture(PerfFx f, int n, float samplesPerStep) const;
  float loopLen(float steps, float samplesPerStep) const;  // frames, clipped behind the writer
  uint32_t intact(const Head& h, uint32_t wEnd) const;     // recorded frames behind base
  void reverseWindow(Head& h, float over, float samplesPerStep, uint32_t wEnd) const;
  void scratchPeriod(Head& h, float over, float samplesPerStep, uint32_t wEnd) const;
  void seam(Head& h, Head& out, int& xf, float samplesPerStep) const;

  PerfStack stack_;
  int16_t* buf_ = nullptr;
  uint32_t frames_ = 0, w_ = 0;  // ring size, next write frame
  uint32_t fresh_ = 0;           // frames before w_ written without a pause (live, in order)
  double gridPos_ = 0;           // steps since transportStart, modulo kGridWrap (no float drift)
  int syncStep_ = -1, syncOff_ = 0;  // pending stepAt(), -1 none
  uint32_t dTail_ = 0, rTail_ = 0;  // samples left of the delay / reverb send tails
  float fade_[kTones] = {};
  // Buffer stage: the playing head and the one it crossfades from (xf_ samples left, linear).
  Head head_, out_;
  int xf_ = 0;
  // Tone effect state and settings. The settings follow the depth only while the effect is held
  // (a release fades out with what was heard); the state is reset when the fade reaches 0.
  float gateGain_ = 1, gateLen_ = 1;  // gain 0..1, length in steps
  Svf filtL_, filtR_;
  int8_t filtSign_ = 0;               // -1 LP, 1 HP, 0 reset (the state belongs to that mode)
  float crushStep_ = 0.125f;          // quantizer step on +-1 (6 bits)
  int downHold_ = 4, downLeft_ = 0;   // samples per held sample, samples until the next one
  float downL_ = 0, downR_ = 0;
  Drive drive_;
};

}  // namespace mt
