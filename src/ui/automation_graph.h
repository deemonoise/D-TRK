#pragma once
#include "fx_info.h"
namespace ui {
// Shared mapping for touch edits and bars. Matrix depth is a signed byte on the wire.
struct AutomationGraph {
  explicit AutomationGraph(mt::Fx f) : fx(f), signedValue(f >= mt::Fx::M01 && f <= mt::Fx::M08),
      lo(signedValue ? -64 : 0), hi(signedValue ? 63 : mt::fxStep(f, 0, 1000)) {}
  // Height is the distance upwards from the graph's base; fingers may leave the graph.
  uint8_t valueAt(int height, int graphHeight) const {
    if (graphHeight <= 0) return mt::fxDefault(fx);
    if (height < 0) height = 0;
    if (height > graphHeight) height = graphHeight;
    return static_cast<uint8_t>(lo + height * (hi - lo) / graphHeight);
  }
  int height(uint8_t wireValue, int graphHeight) const {
    if (graphHeight <= 0 || hi <= lo) return 0;
    int value = signedValue ? mt::fxSigned(wireValue) : wireValue;
    if (value < lo) value = lo;
    if (value > hi) value = hi;
    return (value - lo) * graphHeight / (hi - lo);
  }
  mt::Fx fx;
  bool signedValue;
  int lo, hi;
};
}  // namespace ui
