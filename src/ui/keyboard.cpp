#include "keyboard.h"
#include <string.h>

namespace ui {
namespace {

constexpr char kDel = 1, kOk = 2, kCancel = 3, kPage = 4;

// Rows of single-width keys, 10 per row (columns in key units of a 10-wide grid).
constexpr const char* kLetterRows[] = {"1234567890", "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ-_"};
constexpr const char* kSymbolRows[] = {"!@#$%^&*()", "-_=+[]{}\\|", ";:'\",.<>/?", "`~"};

}  // namespace

void Keyboard::layout() {
  keyCount_ = 0;
  auto add = [this](char ch, int col, int row, int w, const char* label) {
    Key& k = keys_[keyCount_++];
    k.ch = ch;
    k.col = static_cast<uint8_t>(col);
    k.row = static_cast<uint8_t>(row);
    k.w = static_cast<uint8_t>(w);
    if (label) {
      strlcpy(k.label, label, sizeof(k.label));
    } else {
      k.label[0] = ch;
      k.label[1] = 0;
    }
  };
  const char* const* rows = symbols_ ? kSymbolRows : kLetterRows;
  for (int r = 0; r < 4; ++r)
    for (int c = 0; rows[r][c]; ++c) add(rows[r][c], c, r, 1, nullptr);
  if (symbols_) add(' ', 2, 3, 6, "SPACE");
  add(kDel, 8, 3, 2, "DEL");
  if (textMode_) {
    add(kCancel, 0, 4, 4, "CANCEL");
    add(kPage, 4, 4, 2, symbols_ ? "ABC" : "#+=");
    add(kOk, 6, 4, 4, "OK");
  } else {
    add(kCancel, 0, 4, 5, "CANCEL");
    add(kOk, 5, 4, 5, "OK");
  }
  if (textMode_ && !symbols_)  // lower case is the default for passwords
    for (int i = 0; i < keyCount_; ++i)
      if (keys_[i].ch >= 'A' && keys_[i].ch <= 'Z') keys_[i].label[0] = static_cast<char>(keys_[i].ch - 'A' + 'a');
}

void Keyboard::open(const char* title, const char* initial, std::function<void(const char*)> onOk, bool text) {
  textMode_ = text;
  symbols_ = false;
  maxLen_ = text ? kMaxText : kMaxLen;
  strlcpy(title_, title ? title : "", sizeof(title_));
  strlcpy(text_, initial ? initial : "", static_cast<size_t>(maxLen_) + 1);
  len_ = static_cast<int>(strlen(text_));
  layout();
  sel_ = keyCount_ - 1;  // OK
  onOk_ = std::move(onOk);
  open_ = true;
}

void Keyboard::close() {
  open_ = false;
  onOk_ = nullptr;
}

void Keyboard::press(int key, bool shift) {
  if (key < 0 || key >= keyCount_) return;
  const char c = keys_[key].ch;
  if (c == kCancel) {
    close();
  } else if (c == kOk) {
    char t[kMaxText + 1];
    memcpy(t, text_, sizeof(t));
    std::function<void(const char*)> cb = std::move(onOk_);
    close();
    if (cb) cb(t);  // may reopen the keyboard
  } else if (c == kPage) {
    symbols_ = !symbols_;
    layout();
    sel_ = keyCount_ - 2;  // stay on the page key
  } else if (c == kDel) {
    if (len_ > 0) text_[--len_] = 0;
  } else if (len_ < maxLen_) {
    const bool lower = c >= 'A' && c <= 'Z' && (textMode_ ? !shift : shift);
    text_[len_++] = lower ? static_cast<char>(c - 'A' + 'a') : c;
    text_[len_] = 0;
  }
}

void Keyboard::onInput(const hw::InputEvent& ev) {
  if (!open_) return;
  switch (ev.type) {
    case hw::InputType::EncTurn: sel_ = ((sel_ + ev.delta) % keyCount_ + keyCount_) % keyCount_; break;
    case hw::InputType::EncClick: press(sel_, ev.shift); break;
    case hw::InputType::EncLong: close(); break;
    default: break;
  }
}

int Keyboard::keyAt(int x, int y) const {
  const int top = y0_ + kFieldH;
  if (y < top || x < kLeft) return -1;
  const int row = (y - top) / kKeyH, col = (x - kLeft) / kKeyW;
  for (int i = 0; i < keyCount_; ++i)
    if (keys_[i].row == row && col >= keys_[i].col && col < keys_[i].col + keys_[i].w) return i;
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
  // Long text: show the tail so the cursor stays visible.
  const int fit = (kScreenW - kLeft - tx) / kCharW - 1;
  const int skip = len_ > fit ? len_ - fit : 0;
  s.setTextColor(kText);
  s.drawString(text_ + skip, tx, y0 + (kFieldH - 4 - kCharH) / 2);
  s.fillRect(tx + (len_ - skip) * kCharW, y0 + (kFieldH - 4 + kCharH) / 2, kCharW, 2, kEditCursor);

  for (int i = 0; i < keyCount_; ++i) {
    const Key& k = keys_[i];
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
