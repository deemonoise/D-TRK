#pragma once
#include <stdint.h>
#include "hw/lgfx_config.h"

namespace ui {

enum class TouchType : uint8_t { Tap, LongPress, Drag };

// Tap and LongPress carry the start point; Drag carries the current point and dy (multiple of kDragStep).
struct TouchEvent {
  TouchType type;
  int16_t x, y;
  int16_t dy;
};

class TouchTracker {
 public:
  static constexpr uint32_t kLongMs = 500;
  static constexpr int kDragStart = 12;
  static constexpr int kDragStep = 16;

  // Call every loop; returns true and fills ev when a gesture event is ready.
  bool poll(LGFX& lcd, TouchEvent& ev);
  // Start point of the current (or last) gesture.
  int16_t startY() const { return y0_; }

 private:
  bool down_ = false, longSent_ = false, dragging_ = false;
  bool swiped_ = false;  // moved sideways: no Tap / LongPress
  int16_t x0_ = 0, y0_ = 0, lastY_ = 0;
  uint32_t t0_ = 0;
};

}  // namespace ui
