#pragma once
#include "param_list.h"
namespace ui {
class App;
// Small operation form. Values are local until Apply; Cancel/B/long press never edit the pattern.
class ActionDialog {
 public:
  explicit ActionDialog(App& app) : app_(app) {}
  void open(const char* title, const Param* params, int count, std::function<void()> apply);
  bool isOpen() const { return open_; }
  void cancel();
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y0);
 private:
  bool action(int row);
  App& app_;
  Param params_[5];
  ParamList list_{kAreaY+28};
  std::function<void()> apply_;
  char title_[48]{};
  int count_=0;
  bool open_=false;
};
}
