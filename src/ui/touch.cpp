#include "touch.h"
#include <Arduino.h>
#include <stdlib.h>
#include "theme.h"

namespace ui {

bool TouchTracker::poll(LGFX& lcd, TouchEvent& ev, bool hdrag) {
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
    ev = {TouchType::Tap, x0_, y0_, 0, 0, x0_, y0_, id_};
    return true;
  }

  if (!down_) {
    down_ = true;
    longSent_ = dragging_ = hdragging_ = swiped_ = false;
    ++id_;
    x0_ = lastX_ = static_cast<int16_t>(x);
    y0_ = lastY_ = static_cast<int16_t>(y);
    t0_ = now;
    return false;
  }

  const int adx = abs(static_cast<int>(x) - x0_), ady = abs(static_cast<int>(y) - y0_);
  if (!hdrag) {
    // No sideways gestures wanted: any vertical move past kDragStart is a Drag.
    if (!dragging_ && !hdragging_ && !longSent_ && ady > kDragStart) dragging_ = true;
  } else if (!dragging_ && !hdragging_ && !longSent_) {
    // Axis lock: the first move past kDragStart decides vertical (Drag) or sideways (HDrag).
    if (ady > kDragStart && ady >= adx) {
      dragging_ = true;
    } else if (adx > kDragStart) {
      hdragging_ = true;
      lastX_ = x0_;
    }
  }
  if (adx > kDragStart) swiped_ = true;

  if (hdragging_) {
    const int d = static_cast<int>(x) - lastX_;
    if (abs(d) < kHDragStep) return false;
    lastX_ = static_cast<int16_t>(x);
    ev = {TouchType::HDrag, static_cast<int16_t>(x), static_cast<int16_t>(y), 0, static_cast<int16_t>(d), x0_, y0_, id_};
    return true;
  }

  if (dragging_) {
    const int d = static_cast<int>(y) - lastY_;
    if (abs(d) < kDragStep) return false;
    const int dy = (d / kDragStep) * kDragStep;  // keep the remainder for the next event
    lastY_ = static_cast<int16_t>(lastY_ + dy);
    ev = {TouchType::Drag, static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(dy), 0, x0_, y0_, id_};
    return true;
  }

  if (!longSent_ && !swiped_ && now - t0_ >= kLongMs) {
    longSent_ = true;
    ev = {TouchType::LongPress, x0_, y0_, 0, 0, x0_, y0_, id_};
    return true;
  }
  return false;
}

}  // namespace ui
