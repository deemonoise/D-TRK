#pragma once
#include <stdint.h>
#include "hw/lgfx_config.h"

namespace ui {

enum class TouchType : uint8_t { Tap, LongPress, Drag, HDrag };

// Tap and LongPress carry the start point; Drag carries the current point and dy (multiple of kDragStep);
// HDrag (axis-locked sideways move) the current point and dx (|dx| >= kHDragStep). x0 / y0 = start
// point of the gesture, id = gesture counter (new on every touch-down).
struct TouchEvent {
  TouchType type;
  int16_t x, y;
  int16_t dy;
  int16_t dx;
  int16_t x0, y0;
  uint16_t id;
};

class TouchTracker {
 public:
  static constexpr uint32_t kLongMs = 500;
  static constexpr int kDragStart = 12;
  static constexpr int kDragStep = 16;
  static constexpr int kHDragStep = 2;

  // Call every loop; returns true and fills ev when a gesture event is ready. hdrag: sideways
  // gestures are wanted now (axis lock into HDrag); false = vertical Drag only, as before HDrag.
  bool poll(LGFX& lcd, TouchEvent& ev, bool hdrag);
  // Start point of the current (or last) gesture.
  int16_t startY() const { return y0_; }

 private:
  bool down_ = false, longSent_ = false, dragging_ = false, hdragging_ = false;
  bool swiped_ = false;  // moved sideways: no Tap / LongPress
  int16_t x0_ = 0, y0_ = 0, lastY_ = 0, lastX_ = 0;
  uint16_t id_ = 0;
  uint32_t t0_ = 0;
};

}  // namespace ui
