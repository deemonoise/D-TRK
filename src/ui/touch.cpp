#include "touch.h"
#include <Arduino.h>
#include <stdlib.h>
#include "theme.h"

namespace ui {

bool TouchTracker::poll(LGFX& lcd, TouchEvent& ev) {
  int32_t x, y;
  const bool t = lcd.getTouch(&x, &y);
  const uint32_t now = millis();
  if (t) {
    x = x < 0 ? 0 : (x >= kScreenW ? kScreenW - 1 : x);
    y = y < 0 ? 0 : (y >= kScreenH ? kScreenH - 1 : y);
  }

  if (!t) {
    if (!down_) return false;
    down_ = false;
    if (dragging_ || longSent_ || swiped_) return false;
    ev = {TouchType::Tap, x0_, y0_, 0};
    return true;
  }

  if (!down_) {
    down_ = true;
    longSent_ = dragging_ = swiped_ = false;
    x0_ = static_cast<int16_t>(x);
    y0_ = lastY_ = static_cast<int16_t>(y);
    t0_ = now;
    return false;
  }

  if (!dragging_ && !longSent_ && abs(static_cast<int>(y) - y0_) > kDragStart) dragging_ = true;
  if (abs(static_cast<int>(x) - x0_) > kDragStart) swiped_ = true;

  if (dragging_) {
    const int d = static_cast<int>(y) - lastY_;
    if (abs(d) < kDragStep) return false;
    const int dy = (d / kDragStep) * kDragStep;  // keep the remainder for the next event
    lastY_ = static_cast<int16_t>(lastY_ + dy);
    ev = {TouchType::Drag, static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(dy)};
    return true;
  }

  if (!longSent_ && !swiped_ && now - t0_ >= kLongMs) {
    longSent_ = true;
    ev = {TouchType::LongPress, x0_, y0_, 0};
    return true;
  }
  return false;
}

}  // namespace ui
