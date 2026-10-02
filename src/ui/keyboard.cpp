#include "keyboard.h"
#include <string.h>

namespace ui {
namespace {

constexpr char kDel = 1, kOk = 2, kCancel = 3;

struct Key {
  char ch;
  uint8_t col, row, w;
  const char* label;
};

// Columns in key units of a 10-wide grid.
constexpr Key kKeys[] = {
    {'1', 0, 0, 1, "1"}, {'2', 1, 0, 1, "2"}, {'3', 2, 0, 1, "3"}, {'4', 3, 0, 1, "4"}, {'5', 4, 0, 1, "5"},
    {'6', 5, 0, 1, "6"}, {'7', 6, 0, 1, "7"}, {'8', 7, 0, 1, "8"}, {'9', 8, 0, 1, "9"}, {'0', 9, 0, 1, "0"},
    {'A', 0, 1, 1, "A"}, {'B', 1, 1, 1, "B"}, {'C', 2, 1, 1, "C"}, {'D', 3, 1, 1, "D"}, {'E', 4, 1, 1, "E"},
    {'F', 5, 1, 1, "F"}, {'G', 6, 1, 1, "G"}, {'H', 7, 1, 1, "H"}, {'I', 8, 1, 1, "I"}, {'J', 9, 1, 1, "J"},
    {'K', 0, 2, 1, "K"}, {'L', 1, 2, 1, "L"}, {'M', 2, 2, 1, "M"}, {'N', 3, 2, 1, "N"}, {'O', 4, 2, 1, "O"},
    {'P', 5, 2, 1, "P"}, {'Q', 6, 2, 1, "Q"}, {'R', 7, 2, 1, "R"}, {'S', 8, 2, 1, "S"}, {'T', 9, 2, 1, "T"},
    {'U', 0, 3, 1, "U"}, {'V', 1, 3, 1, "V"}, {'W', 2, 3, 1, "W"}, {'X', 3, 3, 1, "X"}, {'Y', 4, 3, 1, "Y"},
    {'Z', 5, 3, 1, "Z"}, {'-', 6, 3, 1, "-"}, {'_', 7, 3, 1, "_"}, {kDel, 8, 3, 2, "DEL"},
    {kCancel, 0, 4, 5, "CANCEL"}, {kOk, 5, 4, 5, "OK"},
};
constexpr int kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);

}  // namespace

void Keyboard::open(const char* title, const char* initial, std::function<void(const char*)> onOk) {
  strlcpy(title_, title ? title : "", sizeof(title_));
  strlcpy(text_, initial ? initial : "", sizeof(text_));
  len_ = static_cast<int>(strlen(text_));
  sel_ = kKeyCount - 1;  // OK
  onOk_ = std::move(onOk);
  open_ = true;
}

void Keyboard::close() {
  open_ = false;
  onOk_ = nullptr;
}

void Keyboard::press(int key, bool shift) {
  if (key < 0 || key >= kKeyCount) return;
  const char c = kKeys[key].ch;
  if (c == kCancel) {
    close();
  } else if (c == kOk) {
    char t[kMaxLen + 1];
    memcpy(t, text_, sizeof(t));
    std::function<void(const char*)> cb = std::move(onOk_);
    close();
    if (cb) cb(t);  // may reopen the keyboard
  } else if (c == kDel) {
    if (len_ > 0) text_[--len_] = 0;
  } else if (len_ < kMaxLen) {
    text_[len_++] = shift && c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    text_[len_] = 0;
  }
}

void Keyboard::onInput(const hw::InputEvent& ev) {
  if (!open_) return;
  switch (ev.type) {
    case hw::InputType::EncTurn: sel_ = ((sel_ + ev.delta) % kKeyCount + kKeyCount) % kKeyCount; break;
    case hw::InputType::EncClick: press(sel_, ev.shift); break;
    case hw::InputType::EncLong: close(); break;
    default: break;
  }
}

int Keyboard::keyAt(int x, int y) const {
  const int top = y0_ + kFieldH;
  if (y < top || x < kLeft) return -1;
  const int row = (y - top) / kKeyH, col = (x - kLeft) / kKeyW;
  for (int i = 0; i < kKeyCount; ++i)
    if (kKeys[i].row == row && col >= kKeys[i].col && col < kKeys[i].col + kKeys[i].w) return i;
  return -1;
}

void Keyboard::onTouch(const TouchEvent& ev, bool shift) {
  if (!open_ || ev.type != TouchType::Tap) return;
  const int k = keyAt(ev.x, ev.y);
  if (k < 0) return;
  sel_ = k;
  press(k, shift);
}

void Keyboard::draw(LGFX_Sprite& s, int y0) {
  if (!open_) return;
  y0_ = y0;
  s.fillRect(0, y0, kScreenW, kFieldH - 4, kBeatBg);
  s.setTextColor(kDim);
  s.drawString(title_, kLeft, y0 + (kFieldH - 4 - kCharH) / 2);
  const int tx = kLeft + (static_cast<int>(strlen(title_)) + 1) * kCharW;
  s.setTextColor(kText);
  s.drawString(text_, tx, y0 + (kFieldH - 4 - kCharH) / 2);
  s.fillRect(tx + len_ * kCharW, y0 + (kFieldH - 4 + kCharH) / 2, kCharW, 2, kEditCursor);

  for (int i = 0; i < kKeyCount; ++i) {
    const Key& k = kKeys[i];
    const int x = kLeft + k.col * kKeyW, y = y0 + kFieldH + k.row * kKeyH;
    const int w = k.w * kKeyW - kGap, h = kKeyH - kGap;
    const bool sel = i == sel_;
    s.fillRect(x, y, w, h, sel ? kPlayBg : kSelBg);
    if (sel) s.drawRect(x, y, w, h, kCursor);
    s.setTextColor(sel ? kCursor : kText);
    const int lw = static_cast<int>(strlen(k.label)) * kCharW;
    s.drawString(k.label, x + (w - lw) / 2, y + (h - kCharH) / 2);
  }
}

}  // namespace ui
