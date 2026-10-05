#pragma once
#include <functional>
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "theme.h"
#include "touch.h"

namespace ui {

// On-screen keyboard. Name mode: 0-9, A-Z (Shift = lower case), '-', '_', DEL, OK, CANCEL, up to 16.
// Text mode (passwords): letters lower case (Shift = upper), a symbols page with space, up to 63.
// Encoder: turn = key, click = press, long = cancel. Fills the work area.
class Keyboard {
 public:
  static constexpr int kMaxLen = 16;
  static constexpr int kMaxText = 63;

  // onOk runs after the keyboard closed, with the typed text (may be empty).
  void open(const char* title, const char* initial, std::function<void(const char*)> onOk, bool text = false);
  void close();
  bool isOpen() const { return open_; }
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev, bool shift);
  void draw(LGFX_Sprite& s, int y0);

 private:
  static constexpr int kKeyW = 46, kKeyH = 40, kGap = 2;
  static constexpr int kLeft = (kScreenW - 10 * kKeyW) / 2;
  static constexpr int kFieldH = 40;

  static constexpr int kMaxKeys = 48;

  struct Key {
    char ch;
    uint8_t col, row, w;
    char label[8];
  };

  void layout();  // fills keys_ for text_ mode and page
  void press(int key, bool shift);
  int keyAt(int x, int y) const;

  char title_[24] = {0};
  char text_[kMaxText + 1] = {0};
  Key keys_[kMaxKeys];
  int keyCount_ = 0;
  int len_ = 0;
  int maxLen_ = kMaxLen;
  bool textMode_ = false;
  bool symbols_ = false;
  int sel_ = 0;
  int y0_ = kAreaY;
  bool open_ = false;
  std::function<void(const char*)> onOk_;
};

}  // namespace ui
