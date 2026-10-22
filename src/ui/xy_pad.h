#pragma once
#include <stdint.h>
#include "hw/lgfx_config.h"
#include "perf_axis.h"

namespace ui {

class PerfPad;

// PERF XY pad (App, PERF on, A + B): draws the work area as the pad PerfPad plays: the zones of
// list-valued axis effects, FILTER's OFF band, each axis' effect and value, the crosshair and the
// finger's fading trail (or hands the trail to PerfVis, which paints it over the visuals).
class XyPad {
 public:
  static constexpr uint32_t kTrailMs = 400;  // a trail point fades out over this
  static constexpr int kTrailR = 12;         // radius of a fresh point / under the finger, px
  void touch(int x, int y, uint32_t now);    // finger at pad x, y: crosshair, maybe a trail point
  void lift() { down_ = false; }
  void clear() {
    n_ = head_ = 0;
    down_ = false;
  }
  bool animating(uint32_t now) const;  // a trail point is still fading: keep redrawing
  // Into the work area (top y0, h high, the full width); trail false: PerfVis paints the trail.
  void draw(LGFX_Sprite& s, int y0, int h, const PerfPad& perf, uint32_t now, bool trail) const;
  void trail(mt::PerfTrail& out, int y0, uint32_t now) const;  // screen px (pad y + y0)

 private:
  static constexpr int kPts = mt::PerfTrail::kMax - 1;  // + the disc under the finger
  struct Pt {
    int16_t x, y;
    uint32_t ms;
  };
  int radius(const Pt& p, uint32_t now) const;  // 0 = faded
  Pt pts_[kPts];
  int n_ = 0, head_ = 0;  // ring: head_ = the next slot
  bool down_ = false;
  int16_t x_ = 0, y_ = 0;
};

}  // namespace ui
