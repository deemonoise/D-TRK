#pragma once
#include <stdint.h>
#include "synth_fxutil.h"

namespace mt {

// Delay modes (SynthModel::dlyMode).
enum class DelayMode : uint8_t { Digital, Tape, PingPong, Count };

// Mono send delay. The int16 line is owned by the caller (firmware: PSRAM); without one the delay is
// silent. A one-pole LP (TONE) sits on the line's output, so the first echo and every repeat get
// darker; the repeats are fed back through it. Level, feedback, tone and width ramp over 5 ms.
// Feedback writes truncate toward zero, so lossy tails eventually reach silence.
//   DIGITAL   the same return on both sides; time changes crossfade two taps over 5 ms.
//   TAPE      fractional read delayed by wow (0.6 Hz, +-3 ms) + flutter (7 Hz, +-0.3 ms), x Wear;
//             time changes glide (one-pole ~200 ms, slew <= 0.5 sample / sample: a pitch bend);
//             feedback through a HP (60 -> 250 Hz with Wear) and a soft saturation. Wear is smoothed
//             (~100 ms). Wear 0 with the glide settled runs the DIGITAL loop (bit-exact). Into TAPE
//             Wear fades in from 0; back to DIGITAL the TAPE loop runs on with Wear 0 until settled.
//   PING-PONG the line split into two halves (L, R; max time len / 2 - 1): send -> L, L out -> R,
//             R out x fb -> L. First echo left, second right. Width (chr) 0 = both centred, 127 =
//             hard L / R.
// Switching into or out of PING-PONG (the line layout changes): this block's return fades out, then
// the line is wiped kWipe samples per block with the delay muted (4 s line: ~0.25 s), then the new
// mode starts from silence. DIGITAL <-> TAPE switches seamlessly.
class Delay {
 public:
  static constexpr float kScale = 8192.f;  // line units per 1.0: +-4.0 before it saturates
  static constexpr uint32_t kWipe = 4096;  // samples wiped per process call after a layout change
  static constexpr float kWowMs = 3.f, kFlutterMs = 0.3f, kWowHz = 0.6f, kFlutterHz = 7.f;
  static constexpr float kGlideMs = 200.f, kMaxSlew = 0.5f, kWearMs = 100.f;

  // buf: len samples, nullptr = off. Clears it.
  void setBuffer(int16_t* buf, uint32_t len);
  void clear();
  // DIGITAL: mixL[i] and mixR[i] += the return (the same on both sides); the line takes
  // send[i] + return x feedback. delay: samples, clamped to 1..len-1. fb, tone, level: 0..127
  // (Project::dly*). tone 127 = no filter.
  void process(const float* send, float* mixL, float* mixR, int n, uint32_t delay, uint8_t fb, uint8_t tone,
               uint8_t level);
  // With a mode (DelayMode) and its character chr 0..127 (Project::dlyChar: TAPE wear, PING-PONG
  // width). mode DIGITAL = the call above.
  void process(const float* send, float* mixL, float* mixR, int n, uint32_t delay, uint8_t fb, uint8_t tone,
               uint8_t level, uint8_t mode, uint8_t chr);
  bool wiping() const { return wipe_ < len_; }

 private:
  void tapeSegment(float target, float lim, float wearT, int32_t& offQ, int32_t& dQ, float& hpA, float& satK);
  void transition(const float* send, float* mixL, float* mixR, int n, uint32_t delay, uint8_t fb, uint8_t level,
                  uint8_t mode, uint8_t chr);
  void digital(const float* send, float* mixL, float* mixR, int n, uint32_t delay, uint8_t fb, uint8_t level);
  void tape(const float* send, float* mixL, float* mixR, int n, uint32_t delay, uint8_t fb, uint8_t level,
            uint8_t chr);
  void pingPong(const float* send, float* mixL, float* mixR, int n, uint32_t delay, uint8_t fb, uint8_t level,
                uint8_t chr);
  void run(uint8_t mode, const float* send, float* mixL, float* mixR, int n, uint32_t delay, uint8_t fb,
           uint8_t level, uint8_t chr);
  void resetModeState();
  void setParams(uint8_t fb, uint8_t tone, uint8_t level, uint8_t width);
  MT_INLINE void nextParams() {
    if (paramsLeft_ == 0) return;
    coef_ += coefStep_; feedback_ += feedbackStep_; level_ += levelStep_; width_ += widthStep_;
    if (--paramsLeft_ == 0) {
      coef_ = coefTarget_; feedback_ = feedbackTarget_; level_ = levelTarget_; width_ = widthTarget_;
    }
  }
  MT_INLINE void setTime(uint32_t delay) {
    timeWant_ = delay;
    if (timeFrom_ == 0) timeFrom_ = timeTo_ = delay;
  }
  MT_INLINE void syncTime(uint32_t delay) {
    timeFrom_ = timeTo_ = timeWant_ = delay;
    timeLeft_ = 0;
  }
  MT_INLINE float nextTime() {
    if (timeLeft_ == 0 && timeTo_ != timeWant_) {
      timeFrom_ = timeTo_; timeTo_ = timeWant_; timeLeft_ = kSmoothSamples;
    }
    if (timeLeft_ == 0) return 1.f;
    const float blend = (kSmoothSamples - timeLeft_ + 1) * (1.f / kSmoothSamples);
    if (--timeLeft_ == 0) timeFrom_ = timeTo_;
    return blend;
  }
  MT_INLINE float tap(const int16_t* line, uint32_t len, uint32_t w, float blend) const {
    const uint32_t a = w >= timeFrom_ ? w - timeFrom_ : w + len - timeFrom_;
    const float x = line[a];
    if (timeFrom_ == timeTo_) return x;
    const uint32_t b = w >= timeTo_ ? w - timeTo_ : w + len - timeTo_;
    return x + (static_cast<float>(line[b]) - x) * blend;
  }
  static constexpr int kSmoothSamples = (kSynthRate * 5 + 999) / 1000;
  bool paramsCold_ = true;
  int paramsLeft_ = 0;
  float feedback_ = 0, level_ = 0, width_ = 0;
  float coefTarget_ = 1, feedbackTarget_ = 0, levelTarget_ = 0, widthTarget_ = 0;
  float coefStep_ = 0, feedbackStep_ = 0, levelStep_ = 0, widthStep_ = 0;
  uint32_t timeFrom_ = 0, timeTo_ = 0, timeWant_ = 0;
  int timeLeft_ = 0;
  bool tapeSettled(float target) const;

  int16_t* buf_ = nullptr;
  uint32_t len_ = 0;
  uint32_t w_ = 0;  // write position
  float lp_ = 0;
  uint8_t tone_ = 0xFF;  // the TONE coef_ was made for (powf / expf: once per change)
  float coef_ = 1;
  // Modes.
  uint8_t mode_ = 0;
  uint32_t wipe_ = 0;  // < len_: wiping the line (muted) up to here
  float lpR_ = 0;      // PING-PONG: R line's TONE state
  float dCur_ = -1;    // TAPE: glided delay (samples), < 0 = take the target
  float off_ = 0;      // TAPE: read offset at the current segment's end (delay + wow / flutter)
  int seg_ = 0;        // TAPE: samples left in the current segment
  // TAPE: the read offset per sample in fixed point (samples x 2^kOffFrac): the index and the
  // fraction by a shift and a mask, no float -> int per sample; it ramps by dQ_ to endQ_ (= off_).
  static constexpr int kOffFrac = 13;
  int32_t offQ_ = 0, dQ_ = 0, endQ_ = 0;
  float hp_ = 0;       // TAPE: feedback HP state
  FxLfo wow_, flutter_;
  float hpA_ = 0, satK_ = 0, wear_ = 0;  // TAPE: smoothed Wear (0..1) and what follows from it
};

}  // namespace mt
