#pragma once
#include <Arduino.h>
#include <AudioStream.h>
#include "ev_queue.h"
#include "practice.h"

namespace mt {
class Synth;
class StreamRing;
class RecCapture;
}

// The synth as an Audio Library source: 2 inputs (the codec's ADC, AudioInputI2S: recording and MON),
// 2 outputs (L, R). Runs in the audio interrupt.
class SynthStream : public AudioStream {
 public:
  static constexpr float kBlockUs = 1e6f * AUDIO_BLOCK_SAMPLES / AUDIO_SAMPLE_RATE_EXACT;

  // CPU cycles in one block's time.
  static uint32_t blockCycles() { return static_cast<uint32_t>(kBlockUs * (F_CPU_ACTUAL / 1000000)); }

  SynthStream() : AudioStream(2, inQ_) {}
  void begin(mt::Synth* s) { synth_ = s; }
  void update() override;

  EvQueue<256>& queue() { return q_; }

  // Counters since the previous take (loop()).
  struct Stats {
    uint32_t cycles, blocks, peakCycles;  // render time
    uint16_t stalls, late, lost;          // block over budget, events played late, events dropped
    uint16_t outPeak;
  };
  Stats take();
  void addLost() { lost_++; }
  // The slowest block's cycles since the previous call (CPU profile), cleared.
  uint32_t takeProfPeak() {
    const uint32_t c = profPeak_;
    profPeak_ = 0;
    return c;
  }

  // Parked (bank changes): silence, due events are dropped, the synth is not run, so it never reads
  // the bank. Takes effect at once for the next block (loop() never runs inside update()). Unparking
  // resets the synth (its voices may point at moved bank data) in the next block.
  void park(bool on);
  bool parked() const { return parked_; }
  // Parks since boot (mod 256): each one ends with a synth reset, which releases PERF (the ESP's
  // PERF pad polls it through Status).
  uint8_t parks() const { return parks_; }

  // Phones level 0..100 % (gain (pct/100)^2) on the output, after the scope.
  void setPhones(uint8_t pct);
  // Scope (MIX): kept only while on; scopeRead copies the newest n samples, every 2nd, as int8
  // (>> 8), oldest first, and returns the count (0 when off).
  void setScope(bool on) { scopeOn_ = on; }
  int scopeRead(int8_t* out, int n) const;

  // The file preview (preview_stream.h), added to the output before the meters at gain.
  void setPreview(mt::StreamRing* r) { preview_ = r; }
  void setPreviewGain(float g) { previewGain_ = g; }

  // Recording (rec_server.h): while set, every block goes to c->feed (its meter runs in any state):
  // the input blocks for MIC / LINE (silence when none came), the synth's output (before the preview)
  // for RESAMPLE. MON adds the input (MIC: the left one on both sides) to the output, saturated,
  // before the output meter and the phones level.
  void setRec(mt::RecCapture* c) { rec_ = c; }
  void setMonitor(bool on) { monitor_ = on; }

  // Called in the audio interrupt before each block's events and render (the pool bench), with the
  // previous block's render cycles.
  void setBlockHook(void (*f)(uint32_t lastCycles)) { hook_ = f; }

 private:
  mt::MetronomeVoice click_;
  mt::Synth* synth_ = nullptr;
  mt::StreamRing* preview_ = nullptr;
  audio_block_t* inQ_[2];
  mt::RecCapture* volatile rec_ = nullptr;
  volatile bool monitor_ = false;
  volatile float previewGain_ = 0.4f;
  void (*volatile hook_)(uint32_t) = nullptr;
  uint32_t lastCycles_ = 0;
  EvQueue<256> q_;
  volatile uint32_t cycles_ = 0, blocks_ = 0, peakCycles_ = 0, profPeak_ = 0;
  volatile uint16_t stalls_ = 0, late_ = 0, lost_ = 0, outPeak_ = 0;
  volatile int32_t phonesQ15_ = 32768;
  static constexpr int kScopeLen = 512;
  int16_t scope_[kScopeLen] = {};
  volatile int scopeAt_ = 0;
  volatile bool scopeOn_ = false;
  volatile bool parked_ = false, resetPending_ = false;
  volatile uint8_t parks_ = 0;
};
