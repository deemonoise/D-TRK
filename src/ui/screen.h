#pragma once
#include "engine/engine.h"
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "theme.h"
#include "touch.h"

namespace ui {

class App;

class Screen {
 public:
  virtual ~Screen() = default;
  virtual void onEnter() {}
  virtual void onLeave() {}
  // Heard pattern changed (called on every screen, active or not).
  virtual void onPatternChange() {}
  // Project loaded / new (called on every screen, before onPatternChange): drop references to old data.
  virtual void onProjectReplaced() {}
  // EncTurn / EncClick / EncLong; PlayPress and Shift are handled by App.
  virtual void onInput(const hw::InputEvent& ev) = 0;
  // Absolute screen coordinates; only events inside the work area (or Drag) arrive here.
  virtual void onTouch(const TouchEvent& ev) = 0;
  virtual void draw(LGFX_Sprite& s, int y0, int h) = 0;
  // Extra redraw request on top of App's own (status changes already redraw).
  virtual bool wantsRedraw(const engine::Status&) { return false; }
};

// Stand-in for screens that are not implemented yet.
class TodoScreen : public Screen {
 public:
  explicit TodoScreen(const char* name) : name_(name) {}
  void onInput(const hw::InputEvent&) override {}
  void onTouch(const TouchEvent&) override {}
  void draw(LGFX_Sprite& s, int y0, int h) override {
    s.setTextColor(kDim);
    s.drawString(name_, 16, y0 + 16);
    s.drawString("TODO", (kScreenW - 4 * kCharW) / 2, y0 + (h - kCharH) / 2);
  }

 private:
  const char* name_;
};

}  // namespace ui
