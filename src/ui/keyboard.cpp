#include "keyboard.h"
#include <string.h>

namespace ui {
namespace {

constexpr char kDel = 1, kOk = 2, kCancel = 3, kPage = 4;

// Rows of single-width keys, 10 per row (columns in key units of a 10-wide grid).
#ifdef DTRK_BRICK
constexpr const char* kLetterRows[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM_"};
#else
constexpr const char* kLetterRows[] = {"1234567890", "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ-_"};
#endif
constexpr const char* kSymbolRows[] = {"!@#$%^&*()", "-_=+[]{}\\|", ";:'\",.<>/?", "`~"};

}  // namespace

void Keyboard::layout() {
  keyCount_ = 0;
#ifdef DTRK_BRICK
  preferredCol_ = -1;
#endif
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
  len_ = cursor_ = static_cast<int>(strlen(text_));
#ifdef DTRK_BRICK
  fieldFocused_ = false;
#endif
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
    if (cursor_ > 0) {
      memmove(text_ + cursor_ - 1, text_ + cursor_, len_ - cursor_ + 1);
      --len_;
      --cursor_;
    }
  } else if (len_ < maxLen_) {
    const bool lower = c >= 'A' && c <= 'Z' && (textMode_ ? !shift : shift);
    memmove(text_ + cursor_ + 1, text_ + cursor_, len_ - cursor_ + 1);
    text_[cursor_++] = lower ? static_cast<char>(c - 'A' + 'a') : c;
    ++len_;
  }
}

#ifdef DTRK_BRICK
void Keyboard::cycleChar(int step) {
  constexpr char nameChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_abcdefghijklmnopqrstuvwxyz";
  char& ch = text_[cursor_];
  if (cursor_ == len_) {
    if (len_ == maxLen_) return;
    ch = textMode_ ? (step > 0 ? 'a' : '~') : (step > 0 ? 'A' : 'z');
    text_[++len_] = 0;
  } else if (textMode_) {
    constexpr int count = '~' - ' ' + 1;
    ch = ch >= ' ' && ch <= '~' ? static_cast<char>(' ' + (ch - ' ' + step + count) % count) : 'a';
  } else {
    constexpr int count = sizeof(nameChars) - 1;
    const char* pos = strchr(nameChars, ch);
    ch = pos ? nameChars[(pos - nameChars + step + count) % count] : 'A';
  }
}

void Keyboard::moveKey(hw::InputAxis axis, int direction) {
  if (!direction) return;
  const Key& current = keys_[sel_];
  const int step = direction < 0 ? -1 : 1;
  if (fieldFocused_) {
    if (axis == hw::InputAxis::Vertical) cycleChar(-step);  // Up advances one character.
    else if (cursor_ + step >= 0 && cursor_ + step <= len_) cursor_ += step;
    return;
  }
  if (axis == hw::InputAxis::Vertical && step < 0 && !current.row) {
    fieldFocused_ = true;
    return;
  }
  if (axis == hw::InputAxis::Horizontal) {
    const int next = sel_ + step;
    if (next >= 0 && next < keyCount_ && keys_[next].row == current.row) {
      sel_ = next;
      preferredCol_ = -1;
    }
    return;
  }
  // Keep the column when crossing wide keys such as SPACE, CANCEL and OK.
  if (preferredCol_ < 0) preferredCol_ = current.col + (current.w - 1) / 2;
  for (int i = 0; i < keyCount_; ++i) {
    const Key& next = keys_[i];
    if (next.row == current.row + step && preferredCol_ >= next.col && preferredCol_ < next.col + next.w) {
      sel_ = i;
      return;
    }
  }
}
#endif

void Keyboard::onInput(const hw::InputEvent& ev) {
  if (!open_) return;
  switch (ev.type) {
    case hw::InputType::EncTurn:
#ifdef DTRK_BRICK
      if (ev.axis != hw::InputAxis::Encoder) {
        moveKey(ev.axis, ev.delta);
        break;
      }
      fieldFocused_ = false;
      preferredCol_ = -1;
#endif
      sel_ = ((sel_ + ev.delta) % keyCount_ + keyCount_) % keyCount_;
      break;
    case hw::InputType::EncClick:
#ifdef DTRK_BRICK
      if (fieldFocused_) {
        fieldFocused_ = false;
        break;
      }
#endif
      press(sel_, ev.shift);
      break;
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
#ifdef DTRK_BRICK
  fieldFocused_ = false;
  preferredCol_ = -1;
#endif
  press(k, shift);
}

void Keyboard::draw(LGFX_Sprite& s, int y0) {
  if (!open_) return;
  y0_ = y0;
  s.fillRect(0, y0, kScreenW, kFieldH - 4, kBeatBg);
  const int tx = kLeft + (static_cast<int>(strlen(title_)) + 1) * kCharW;
  const int fit = (kScreenW - kLeft - tx) / kCharW - 1;
  const int skip = cursor_ > fit ? cursor_ - fit : 0;
  int textY = y0 + (kFieldH - 4 - kCharH) / 2;
#ifdef DTRK_BRICK
  textY = y0 + 1;
  if (fieldFocused_) s.drawRect(0, y0, kScreenW, kFieldH - 4, kCursor);
  s.setTextColor(fieldFocused_ ? kCursor : kDim);
  s.drawString(fieldFocused_ ? "LEFT/RIGHT MOVE   UP/DOWN CHANGE   A KEYS" : "PRESS UP FROM THE TOP ROW TO EDIT TEXT",
               kLeft, textY + kCharH + 2);
#endif
  s.setTextColor(kDim);
  s.drawString(title_, kLeft, textY);
  s.setTextColor(kText);
  s.drawString(text_ + skip, tx, textY);
  s.fillRect(tx + (cursor_ - skip) * kCharW, textY + kCharH, kCharW, 2, kEditCursor);

  for (int i = 0; i < keyCount_; ++i) {
    const Key& k = keys_[i];
    const int x = kLeft + k.col * kKeyW, y = y0 + kFieldH + k.row * kKeyH;
    const int w = k.w * kKeyW - kGap, h = kKeyH - kGap;
    bool sel = i == sel_;
#ifdef DTRK_BRICK
    if (fieldFocused_) sel = false;
#endif
    s.fillRect(x, y, w, h, sel ? kPlayBg : kSelBg);
    if (sel) s.drawRect(x, y, w, h, kCursor);
    s.setTextColor(sel ? kCursor : kText);
    const int lw = static_cast<int>(strlen(k.label)) * kCharW;
    s.drawString(k.label, x + (w - lw) / 2, y + (h - kCharH) / 2);
  }
}

}  // namespace ui
