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
  void reset();
  void render(const SynParams& p, bool advance, SynPreviewFrame& out);
 private:
  SynVoice voices_[4];
  bool initialized_ = false;
  uint8_t active_ = 0;
};

// C4 input settings supported by the ESP production SYNTH; excludes filter and ADSR.
void synPreviewParams(const Instrument& m, uint32_t ageSamples, SynParams& out);

}  // namespace mt
