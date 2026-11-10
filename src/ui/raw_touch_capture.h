#pragma once
namespace ui {
enum class RawTouchPhase { Down, Move, Up };
enum class RawTouchRoute { None, Screen, Xy, CancelXy };

// A continuous gesture belongs to the pad/document that received Down. Closing it or opening
// a menu cancels the capture; later Move/Up must not enter its replacement.
class RawTouchCapture {
 public:
  void reset() { screen_ = nullptr; xy_ = false; }
  RawTouchRoute event(RawTouchPhase phase, const void* screen, bool xy, bool raw, bool blocked) {
    if (phase == RawTouchPhase::Down) {
      reset();
      if (!blocked) {
        xy_ = xy;
        if (!xy && raw) screen_ = screen;
      }
    }
    RawTouchRoute route = RawTouchRoute::None;
    if (xy_) {
      if (!blocked && xy) route = RawTouchRoute::Xy;
      else { reset(); route = RawTouchRoute::CancelXy; }
    } else if (screen_) {
      if (!blocked && screen_ == screen && raw) route = RawTouchRoute::Screen;
      else reset();
    }
    if (phase == RawTouchPhase::Up) reset();
    return route;
  }
 private:
  const void* screen_ = nullptr;
  bool xy_ = false;
};
}
