#include "param_list.h"
#include <string.h>
#include "engine/engine.h"
#include "esp_heap_caps.h"
#include "theme.h"

namespace ui {
namespace {
std::function<void(bool)> editObserver;
}  // namespace
void setParamEditObserver(std::function<void(bool)> observer) { editObserver = std::move(observer); }

bool turnRow(const Param* rows, int count, const char* label, size_t maxLen, int delta, char* out, int n) {
  for (int i = 0; i < count; ++i) {
    const Param& r = rows[i];
    if (!r.label || strncmp(r.label, label, maxLen) != 0) continue;
    if (!r.edit) return false;
    if (editObserver) editObserver(true);
    engine::lockProject();
    r.edit(delta);
    engine::unlockProject();
    if (editObserver) editObserver(false);
    if (n > 0) out[0] = 0;
    if (r.format && n > 0) r.format(out, n);
    return true;
  }
  return false;
}

void ParamList::edit(int delta) {
  if (sel_ < 0 || sel_ >= count_ || !params_[sel_].edit || delta == 0) return;
  if (editObserver) editObserver(true);
  engine::lockProject();
  params_[sel_].edit(delta);
  engine::unlockProject();
  if (editObserver) editObserver(false);
  net_ += delta;
  if (onEdit_) onEdit_();
}

const Param* ParamList::knobRow(size_t maxLen) const {
  const Param* r = selected();
  if (!r || !r->edit || !r->label || strcmp(r->label, "-") == 0) return nullptr;
  for (int i = 0; i < count_; ++i)
    if (i != sel_ && params_[i].label && strncmp(params_[i].label, r->label, maxLen) == 0) return nullptr;
  return r;
}

void ParamList::beginEdit() {
  origRow_ = sel_;
  net_ = 0;
  orig_[0] = 0;
  if (sel_ >= 0 && sel_ < count_ && params_[sel_].format) params_[sel_].format(orig_, sizeof(orig_));
  snapOk_ = false;
  if (!scope_ || scopeSize_ == 0) return;
  if (!snap_) {
    snap_ = static_cast<uint8_t*>(heap_caps_malloc(scopeSize_, MALLOC_CAP_SPIRAM));
    if (!snap_) snap_ = static_cast<uint8_t*>(malloc(scopeSize_));
  }
  const void* src = scope_();
  if (snap_ && src) {
    memcpy(snap_, src, scopeSize_);  // the UI is the only writer: no lock needed to read
    snapOk_ = true;
  }
}

bool ParamList::cancelEdit() {
  if (!edit_) return false;
  if (editObserver) editObserver(true);
  hold_ = false;
  void* dst = snapOk_ ? scope_() : nullptr;  // asked while still editing, as in beginEdit()
  edit_ = false;
  dragAcc_ = 0;
  if (snapOk_) {
    if (dst) {
      engine::lockProject();
      memcpy(dst, snap_, scopeSize_);
      engine::unlockProject();
    }
  } else if (origRow_ == sel_ && sel_ >= 0 && sel_ < count_ && params_[sel_].format && net_ != 0) {
    // Step back until the value reads as before (clamped edits do not sum up exactly).
    const int dir = net_ > 0 ? -1 : 1;
    char cur[32];
    for (int i = 0; i < 1024; ++i) {
      cur[0] = 0;
      params_[sel_].format(cur, sizeof(cur));
      if (strcmp(cur, orig_) == 0) break;
      engine::lockProject();
      params_[sel_].edit(dir);
      engine::unlockProject();
    }
  }
  if (editObserver) editObserver(false);
  snapOk_ = false;
  net_ = 0;
  if (onEdit_) onEdit_();
  if (onCancel_) onCancel_();
  return true;
}

void ParamList::ensureVisible() {
  if (sel_ < 0) {  // the page bar: the list from its top
    top_ = 0;
    return;
  }
  const int n = shown();
  if (sel_ < top_) top_ = sel_;
  if (sel_ >= top_ + n) top_ = sel_ - n + 1;
  if (top_ > count_ - n) top_ = count_ - n;
  if (top_ < 0) top_ = 0;
}

bool ParamList::holdEdit() {
  if (sel_ < 0 || sel_ >= count_ || !params_[sel_].edit) return false;
  if (!edit_) {
    ensureVisible();  // never edit a row scrolled out of sight
    edit_ = true;
    hold_ = true;
    dragAcc_ = 0;
    beginEdit();
  }
  return true;
}

int ParamList::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  switch (ev.type) {
    case InputType::EncTurn: {
      if (edit_) {
        edit(ev.delta * (ev.shift ? 10 : 1));
        break;
      }
      // Round and round: the rows, and the page bar before the first one.
      const int first = bar_ ? -1 : 0;
      const int n = count_ - first;
      if (n <= 0) break;
      sel_ = ((sel_ - first + ev.delta) % n + n) % n + first;
      ensureVisible();
      break;
    }
    case InputType::EncClick:
      if (bar_ && sel_ < 0) break;
      if (count_ == 0) break;
      if (edit_ && ev.shift) {
        cancelEdit();
        break;
      }
      ensureVisible();  // never edit a row scrolled out of sight
      edit_ = !edit_;
      hold_ = false;
      if (edit_) beginEdit();
      break;
    case InputType::EditTurn:
      if (bar_ && sel_ < 0) return ev.delta;
      if (holdEdit()) edit(ev.delta * (ev.shift ? 10 : 1));
      break;
    case InputType::EditEnd:  // A released: a hold edit keeps its value
      if (hold_) edit_ = false;
      hold_ = false;
      break;
    case InputType::EditCancel: cancelEdit(); break;
    default: break;
  }
  return 0;
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
  hold_ = false;
  if (r == sel_) {
    edit_ = !edit_;
    if (edit_) beginEdit();
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
    const bool dim = params_[i].dim && params_[i].dim();
    if (sel) s.fillRect(0, ry, kScreenW, kRowH, kSelBg);
    s.setTextColor(sel ? kCursor : (dim ? kDim : kText));
    s.drawString(params_[i].label, kLabelX, ry + (kRowH - kCharH) / 2);
    buf[0] = 0;
    if (params_[i].format) params_[i].format(buf, sizeof(buf));
    const bool warn = params_[i].warn && params_[i].warn();
    s.setTextColor(sel && edit_ ? kEditCursor : (warn ? kRed : (dim ? kDim : kText)));
    s.drawString(buf, kValueX, ry + (kRowH - kCharH) / 2);
  }
  if (n < count_) {  // scroll marks
    s.setTextColor(kDim);
    if (top_ > 0) s.drawString("^", kScreenW - 16, y + (kRowH - kCharH) / 2);
    if (top_ + n < count_) s.drawString("v", kScreenW - 16, y + (n - 1) * kRowH + (kRowH - kCharH) / 2);
  }
}

}  // namespace ui
