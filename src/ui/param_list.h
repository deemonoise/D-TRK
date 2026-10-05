#pragma once
#include <functional>
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "theme.h"
#include "touch.h"

namespace ui {

struct Param {
  const char* label;
  std::function<void(char* out, int n)> format;
  std::function<void(int delta)> edit;  // called under engine::lockProject()
  std::function<bool()> dim;            // true: drawn grey (does not apply now); empty = never
  std::function<bool()> warn;           // true: value drawn red (e.g. missing sample); empty = never
};

// Rows of "label  value". Turn = select, click = edit (value red), turn while editing = edit(delta),
// Shift = x10. Tap = select, tap on the selected row = edit. Drag while editing = edit(-dy / 16).
// With setVisibleRows() smaller than the row count the list scrolls (Drag while not editing).
class ParamList {
 public:
  static constexpr int kRowH = 24;
  static constexpr int kLabelX = 16;
  static constexpr int kValueX = 200;

  explicit ParamList(int y = kAreaY) : y_(y) {}
  // params must outlive the list.
  void setParams(const Param* params, int count) {
    params_ = params;
    count_ = count;
    if (sel_ >= count_) sel_ = 0;
    top_ = 0;
    dragAcc_ = 0;
    ensureVisible();
  }
  // Rows drawn at once; 0 = all.
  void setVisibleRows(int n) {
    visible_ = n;
    ensureVisible();
  }
  int sel() const { return sel_; }
  void setSel(int row) {
    sel_ = row < 0 ? 0 : (row >= count_ ? count_ - 1 : row);
    if (sel_ < 0) sel_ = 0;
    ensureVisible();
  }
  bool editing() const { return edit_; }
  void setEdit(bool on) {
    edit_ = on;
    dragAcc_ = 0;
  }
  // Called after every edit (dirty flag).
  void setOnEdit(std::function<void()> f) { onEdit_ = std::move(f); }
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void edit(int delta);
  // Row under y, or -1.
  int rowAt(int y) const;
  int rowY(int row) const { return y_ + (row - top_) * kRowH; }
  void draw(LGFX_Sprite& s, int y);

 private:
  int shown() const { return visible_ > 0 && visible_ < count_ ? visible_ : count_; }
  void ensureVisible();

  const Param* params_ = nullptr;
  int count_ = 0;
  int sel_ = 0;
  int top_ = 0;      // first drawn row
  int visible_ = 0;  // 0 = all
  int dragAcc_ = 0;  // drag px not yet turned into rows
  bool edit_ = false;
  int y_;  // top of the first row, updated by draw()
  std::function<void()> onEdit_;
};

}  // namespace ui
