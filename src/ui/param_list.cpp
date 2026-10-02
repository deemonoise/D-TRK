#include "param_list.h"
#include "engine/engine.h"
#include "theme.h"

namespace ui {

void ParamList::edit(int delta) {
  if (sel_ < 0 || sel_ >= count_ || !params_[sel_].edit || delta == 0) return;
  engine::lockProject();
  params_[sel_].edit(delta);
  engine::unlockProject();
  if (onEdit_) onEdit_();
}

void ParamList::ensureVisible() {
  const int n = shown();
  if (sel_ < top_) top_ = sel_;
  if (sel_ >= top_ + n) top_ = sel_ - n + 1;
  if (top_ > count_ - n) top_ = count_ - n;
  if (top_ < 0) top_ = 0;
}

void ParamList::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  if (count_ == 0) return;
  switch (ev.type) {
    case InputType::EncTurn:
      if (edit_) {
        edit(ev.delta * (ev.shift ? 10 : 1));
      } else {
        sel_ += ev.delta;
        if (sel_ < 0) sel_ = 0;
        if (sel_ >= count_) sel_ = count_ - 1;
        ensureVisible();
      }
      break;
    case InputType::EncClick:
      ensureVisible();  // never edit a row scrolled out of sight
      edit_ = !edit_;
      break;
    default: break;
  }
}

int ParamList::rowAt(int y) const {
  if (y < y_) return -1;
  const int r = (y - y_) / kRowH;
  return r < shown() ? top_ + r : -1;
}

void ParamList::onTouch(const TouchEvent& ev) {
  if (ev.type == TouchType::Drag) {
    if (edit_) {
      edit(-ev.dy / TouchTracker::kDragStep);
    } else if (shown() < count_) {
      dragAcc_ += ev.dy;
      const int rows = dragAcc_ / kRowH;
      dragAcc_ -= rows * kRowH;
      top_ -= rows;
      if (top_ > count_ - shown()) top_ = count_ - shown();
      if (top_ < 0) top_ = 0;
      // Keep the selection on screen.
      if (sel_ < top_) sel_ = top_;
      if (sel_ >= top_ + shown()) sel_ = top_ + shown() - 1;
    }
    return;
  }
  dragAcc_ = 0;  // a new gesture
  if (ev.type != TouchType::Tap) return;
  const int r = rowAt(ev.y);
  if (r < 0) return;
  if (r == sel_) {
    edit_ = !edit_;
  } else {
    sel_ = r;
    edit_ = false;
  }
}

void ParamList::draw(LGFX_Sprite& s, int y) {
  y_ = y;
  char buf[32];
  const int n = shown();
  for (int i = top_; i < top_ + n; ++i) {
    const int ry = y + (i - top_) * kRowH;
    const bool sel = i == sel_;
    if (sel) s.fillRect(0, ry, kScreenW, kRowH, kSelBg);
    s.setTextColor(sel ? kCursor : kText);
    s.drawString(params_[i].label, kLabelX, ry + (kRowH - kCharH) / 2);
    buf[0] = 0;
    if (params_[i].format) params_[i].format(buf, sizeof(buf));
    s.setTextColor(sel && edit_ ? kEditCursor : kText);
    s.drawString(buf, kValueX, ry + (kRowH - kCharH) / 2);
  }
  if (n < count_) {  // scroll marks
    s.setTextColor(kDim);
    if (top_ > 0) s.drawString("^", kScreenW - 16, y + (kRowH - kCharH) / 2);
    if (top_ + n < count_) s.drawString("v", kScreenW - 16, y + (n - 1) * kRowH + (kRowH - kCharH) / 2);
  }
}

}  // namespace ui
