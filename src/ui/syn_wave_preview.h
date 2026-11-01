#pragma once
#include "hw/lgfx_config.h"
#include "synth_preview.h"
#include "inst_codec.h"

namespace ui {

// ESP-side model only: no persistent scope subscription or audio-thread work.
class SynWavePreview {
 public:
  SynWavePreview() = default;
  ~SynWavePreview() { reset(); }
  SynWavePreview(const SynWavePreview&) = delete;
  SynWavePreview& operator=(const SynWavePreview&) = delete;
  void draw(LGFX_Sprite& s, const mt::Instrument& m, int x, int y, int w, int h,
            bool modPage, uint32_t nowMs);
  void toggleLive() { live_ = !live_; }
  bool live() const { return live_; }
  void reset();  // release WT scratch; keep the selected HOLD/LIVE mode

 private:
  struct Table {
    int16_t* data = nullptr;
    char name[mt::kSampleNameMax + 1] = {};
    int frame = -1;
    uint32_t hash = 0;
  } tables_[2];
  const int16_t* table(int k, const mt::Instrument& m, float shape);
  mt::SynPreviewClock clock_;
  mt::SynPreviewModel model_;
  mt::SynPreviewFrame frame_;
  uint8_t key_[mt::kSynRecSize + mt::kFmMacros + 2] = {};
  bool live_ = false, keyValid_ = false, noMemory_ = false;
  uint32_t lastMissingTry_ = 0;
  uint32_t bankStamp_ = 0, parkStamp_ = 0;
};

}  // namespace ui
