#pragma once
#include <functional>
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "param_list.h"
#include "touch.h"

namespace ui {

class App;

// Transpose amount over GRID, fills the work area. Scale mode: +-2 octaves in scale degrees,
// Chromatic: +-24 semitones; Shift+turn = one octave. OK calls back with (amount, degrees);
// Cancel or a long encoder press closes without changes. Amount and mode survive between opens.
class TransposeDialog {
 public:
  explicit TransposeDialog(App& app);
  // title: what gets transposed (e.g. "TRACK 3" / "SEL T1-2 1-16").
  void open(const char* title, std::function<void(int amount, bool degrees)> onOk);
  bool isOpen() const { return open_; }
  void cancel();
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y0);

 private:
  enum Row : int { kAmount, kMode, kOk, kCancel, kRows };
  static constexpr int kHeaderH = 28;
  static constexpr int kBtnY = 2, kBtnH = 20;
  static constexpr int kCancelX = 320, kCancelW = 72;
  static constexpr int kOkX = 400, kOkW = 64;

  int octave() const;  // amount step of one octave in the current mode
  void clampAmount();
  void ok();
  bool action(int row);  // OK / Cancel row: runs it, true when handled
  void drawButton(LGFX_Sprite& s, int x, int y, int w, const char* label, bool sel);

  App& app_;
  Param params_[kRows];
  ParamList list_{kAreaY + kHeaderH};
  std::function<void(int, bool)> onOk_;
  char title_[28] = {0};
  int amount_ = 0;
  bool degrees_ = true;
  int y0_ = kAreaY;
  bool open_ = false;
};

}  // namespace ui
