#pragma once
#include <stdint.h>
#include "perf_fx.h"

namespace mt {

// XY pad (PERF): a PerfFx on each axis of a touch pad, the finger's position sets its depth. pos 0
// is the axis' low end (left / bottom), len pixels long; pos is clamped.
// The range is spread evenly, values growing towards the high end (a list-valued effect: equal
// zones, one per value). A bipolar range (FILTER: LP <-> HP) is 0 (OFF) in a dead zone of len / 8
// round the middle, lo .. -1 below it and 1 .. hi above. None or len <= 0: lo.
int perfAxisDepth(PerfFx f, int pos, int len);
// List-valued effects (not coarse): first pixel of visible choice i (retired IDs skipped).
bool perfAxisZoned(PerfFx f);
int perfAxisEdge(PerfFx f, int i, int len);
// The bipolar dead zone [from, to) in pixels; false for every other effect.
bool perfAxisDead(PerfFx f, int len, int& from, int& to);
// The effect axis a (0 = X, 1 = Y) of xy (Project::perfXy) plays: None when unset or out of range;
// Y also when on X's effect (X drives it) or when both are buffer effects (one read head: each move
// would re-press Y over X's).
PerfFx perfXyAxis(const uint8_t xy[2], int a);

// The finger's trail over the PERF visuals (ui::PerfVis paints it into each row as the row goes
// out, after the look, so the look never moves it): discs in screen pixels.
struct PerfTrail {
  static constexpr int kMax = 16;
  int16_t x[kMax], y[kMax];
  uint8_t r[kMax];
  int n = 0;
  void add(int px, int py, int rad) {  // radius <= 0 or full: ignored
    if (rad <= 0 || n >= kMax) return;
    x[n] = static_cast<int16_t>(px);
    y[n] = static_cast<int16_t>(py);
    r[n] = static_cast<uint8_t>(rad > 255 ? 255 : rad);
    ++n;
  }
};
// Row y of the trail's discs into dst (w pixels) in `color`, in dst's own pixel format.
void perfTrailRow(const PerfTrail& t, int y, uint16_t* dst, int w, uint16_t color);

}  // namespace mt
