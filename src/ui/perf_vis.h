#pragma once
#include <stdint.h>
#include "hw/lgfx_config.h"
#include "perf_fx.h"
#include "perf_axis.h"

namespace ui {

// PERF visuals: the frame goes to the panel through the held effects' look (mt::perfLook) row by
// row instead of pushSprite, plus the panel's own invert / 8-colour modes. Two static line buffers
// in internal RAM (~2 KB), no heap: Wi-Fi's internal RAM margin stays.
class PerfVis {
 public:
  // The sprite through `stack`'s look; steps = play position (sixteenths), stepUs = a step's length.
  // trail (XY pad): discs painted over each row after the look, in trailColor (RGB565).
  void push(LGFX& lcd, LGFX_Sprite& spr, const mt::PerfStack& stack, float steps, uint32_t stepUs,
            const mt::PerfTrail* trail = nullptr, uint16_t trailColor = 0);
  // Back to plain (the panel modes off) before a plain pushSprite; nothing when not engaged.
  void reset(LGFX& lcd);
  bool engaged() const { return engaged_; }

 private:
  void panel(LGFX& lcd, bool invert, bool idle);

  bool engaged_ = false;
  bool pushed_ = false;  // a frame went out since engaged (Freeze keeps it)
  bool invert_ = false, idle_ = false;
  // The held effects and when each was pressed (millis), for TapeStop.
  mt::PerfFx fx_[mt::kPerfButtons];
  uint32_t at_[mt::kPerfButtons];
  int n_ = 0;
};

}  // namespace ui
