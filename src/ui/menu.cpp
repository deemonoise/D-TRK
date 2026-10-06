#include "menu.h"
#include <string.h>
#include "theme.h"

namespace ui {

void Menu::open(const char* title, const MenuItem* items, int count, std::function<void(int id)> onChoose) {
  if (count > kMaxItems) count = kMaxItems;
  if (count < 0) count = 0;
  strlcpy(title_, title ? title : "", sizeof(title_));
  for (int i = 0; i < count; ++i) {
    strlcpy(items_[i].label, items[i].label ? items[i].label : "", sizeof(items_[i].label));
    items_[i].id = items[i].id;
    items_[i].enabled = items[i].enabled;
  }
  count_ = count;
  onChoose_ = std::move(onChoose);
  sel_ = 0;
  while (sel_ < count_ && !items_[sel_].enabled) ++sel_;
  if (sel_ == count_) sel_ = 0;
  top_ = 0;
  dragAcc_ = 0;
  ensureVisible();
  open_ = true;
}

void Menu::close() {
  open_ = false;
  onChoose_ = nullptr;
}

int Menu::left() const { return (kScreenW - kW) / 2; }
int Menu::topY() const { return (kScreenH - height()) / 2; }

void Menu::move(int delta) {
  if (count_ == 0) return;
  const int dir = delta > 0 ? 1 : -1;
  for (int n = delta > 0 ? delta : -delta; n > 0; --n) {
    int i = sel_;
    for (int k = 0; k < count_; ++k) {
      i = (i + dir + count_) % count_;
      if (items_[i].enabled) {
        sel_ = i;
        break;
      }
    }
  }
  ensureVisible();
}

void Menu::ensureVisible() {
  if (sel_ < top_) top_ = sel_;
  if (sel_ >= top_ + kVisible) top_ = sel_ - kVisible + 1;
}

void Menu::choose(int idx) {
  if (idx < 0 || idx >= count_ || !items_[idx].enabled) return;
  const int id = items_[idx].id;
  std::function<void(int)> cb = std::move(onChoose_);
  close();
  if (cb) cb(id);  // may open another menu
}

void Menu::onInput(const hw::InputEvent& ev) {
  if (!open_) return;
  switch (ev.type) {
    case hw::InputType::EncTurn: move(ev.delta); break;
    case hw::InputType::EncClick: choose(sel_); break;
    case hw::InputType::EncLong: close(); break;
    default: break;
  }
}

void Menu::onTouch(const TouchEvent& ev) {
  if (!open_) return;
  const int x0 = left(), y0 = topY();
  const bool inside = ev.x >= x0 && ev.x < x0 + kW && ev.y >= y0 && ev.y < y0 + height();
  if (ev.type == TouchType::Drag) {
    if (count_ > kVisible) {
      dragAcc_ += ev.dy;
      const int rows = dragAcc_ / kRowH;
      dragAcc_ -= rows * kRowH;
      top_ -= rows;
      if (top_ > count_ - kVisible) top_ = count_ - kVisible;
      if (top_ < 0) top_ = 0;
      // The selection stays on screen: a click must not run an item the user cannot see.
      if (sel_ < top_) sel_ = top_;
      if (sel_ >= top_ + kVisible) sel_ = top_ + kVisible - 1;
    }
    return;
  }
  if (ev.type != TouchType::Tap) return;
  if (!inside) {
    close();
    return;
  }
  const int row = (ev.y - (y0 + 2 + kRowH)) / kRowH;
  if (ev.y < y0 + 2 + kRowH || row >= rows()) return;  // title or bottom padding
  const int idx = top_ + row;
  if (!items_[idx].enabled) return;
  sel_ = idx;
  choose(sel_);
}

void Menu::draw(LGFX_Sprite& s) {
  if (!open_) return;
  const int x0 = left(), y0 = topY(), h = height();
  s.fillRect(x0, y0, kW, h, kMenuBg);
  s.drawRect(x0, y0, kW, h, kMenuBorder);
  s.setTextColor(kCursor);
  s.drawString(title_, x0 + 8, y0 + 4);
  s.drawFastHLine(x0, y0 + 2 + kRowH, kW, kMenuBorder);
  for (int r = 0; r < rows(); ++r) {
    const int i = top_ + r;
    const int y = y0 + 2 + (r + 1) * kRowH;
    if (i == sel_) s.fillRect(x0 + 2, y + 1, kW - 4, kRowH - 1, kPlayBg);
    s.setTextColor(!items_[i].enabled ? kDim : (i == sel_ ? kCursor : kText));
    s.drawString(items_[i].label, x0 + 12, y + 3);
  }
  s.setTextColor(kDim);
  if (top_ > 0) s.drawString("^", x0 + kW - 28, y0 + 4);
  if (top_ + kVisible < count_) s.drawString("v", x0 + kW - 16, y0 + 4);
}

}  // namespace ui
