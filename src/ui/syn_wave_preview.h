#pragma once
#include "hw/lgfx_config.h"
#include "synth_preview.h"
#include "inst_codec.h"

namespace ui {
// UI-side C4 model; frame and oscillator state live in App's PSRAM allocation.
// Full bank tables are borrowed only while rendering, never copied or retained.
class SynWavePreview {
 public:
  void draw(LGFX_Sprite& s, const mt::Instrument& m, int x, int y, int w, int h,
            bool modPage, uint32_t nowMs);
  void toggleLive() { live_ = !live_; }
  bool live() const { return live_; }
  bool wantsRedraw(uint32_t now) const {
    return ((live_ || pending_) && now-lastFrameMs_ >= 100) ||
           ((frame_.missing[0] || frame_.missing[1]) && now-lastMissingTry_ >= 500);
  }
  void reset();
 private:
  mt::SynPreviewClock clock_;
  mt::SynPreviewModel model_;
  mt::SynPreviewFrame frame_;
  uint8_t key_[mt::kSynRecSize + mt::kFmMacros + 2] = {};
  bool live_ = false, keyValid_ = false, pending_ = false;
  uint32_t lastMissingTry_ = 0, lastFrameMs_ = 0, bankStamp_ = 0;
};
}  // namespace ui
