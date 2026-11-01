#pragma once
#include "synth_syn.h"

namespace mt {

// A local C4 model, never a capture of the playing tracks. The four traces use the
// production oscillator path and a shared trigger seed, sampled every four samples.
constexpr int kSynPreviewPoints = 128;
constexpr int kSynPreviewStride = 4;
struct SynPreviewFrame {
  float osc[2][kSynPreviewPoints] = {};
  float mix[kSynPreviewPoints] = {}, mod[kSynPreviewPoints] = {};
  float mixLo[kSynPreviewPoints] = {}, mixHi[kSynPreviewPoints] = {};
  float modLo[kSynPreviewPoints] = {}, modHi[kSynPreviewPoints] = {};
  float mixScale = 1, modScale = 1;
  bool missing[2] = {};
};
struct SynPreviewPhase {
  uint8_t mode = 0, phase[2] = {0, 0};
  uint32_t seed = 0x51a7c3u;
};

class SynPreviewClock {
 public:
  bool update(uint32_t nowMs, bool live, bool changed);
  uint32_t ageSamples() const;
  void reset();
 private:
  uint32_t lastSeen_ = 0, lastFrame_ = 0, ageMs_ = 0, frameAgeMs_ = 0;
  bool initialized_ = false, live_ = false;
};

class SynPreviewModel {
 public:
  void reset() { initialized_ = false; active_ = 0; }
  void render(const SynParams& p, const SynPreviewPhase& phase, bool advance, SynPreviewFrame& out);
 private:
  SynVoice voices_[4];
  bool initialized_ = false;
  uint8_t active_ = 0;
};

// Build a display-only mip adapter from one cached eight-bit WT frame. All frames
// are the same snapshot; averaged mips approximate the display, not the PCM bank.
void synPreviewTable(const int8_t frame[kWtFrameLen], int16_t out[kWtTableSamples]);

}  // namespace mt
