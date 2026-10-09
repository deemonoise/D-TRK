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
// Shift = x10, Shift+click while editing = cancel (the value as it was when the edit began: the
// setEditScope() bytes copied back, else edit(+-1) until the value text matches again). Tap = select, tap on the selected row = edit. Drag while editing = edit(-dy / 16).
// With setVisibleRows() smaller than the row count the list scrolls (Drag while not editing).
// setPageBar(true): the screen's page bar is a position above the first row (sel() == -1, drawn by
// the screen, see barSelected()); turning cycles through it and the rows. A click on it returns +1
// (next page), Shift+click -1 (previous page); the screen switches and keeps the bar selected.
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
    if (sel_ < 0 && !bar_) sel_ = 0;
    top_ = 0;
    dragAcc_ = 0;
    ensureVisible();
  }
  // Swaps in a rebuilt row set of the same screen (rows shown / hidden): selection and scroll stay,
  // clamped to the new count.
  void replaceParams(const Param* params, int count) {
    params_ = params;
    count_ = count;
    if (sel_ >= count_) sel_ = count_ > 0 ? count_ - 1 : (bar_ ? -1 : 0);
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
  void setPageBar(bool on) {
    bar_ = on;
    if (!on && sel_ < 0) sel_ = 0;
  }
  // The page bar position (setPageBar mode): no row selected, not editing.
  void selectBar() {
    if (!bar_) return;
    sel_ = -1;
    edit_ = false;
    top_ = 0;
  }
  bool barSelected() const { return bar_ && sel_ < 0; }
  bool editing() const { return edit_; }
  void setEdit(bool on) {
    edit_ = on;
    dragAcc_ = 0;
  }
  // Called after every edit (dirty flag), also after a cancel.
  void setOnEdit(std::function<void()> f) { onEdit_ = std::move(f); }
  // Bytes an edit of these rows may change (e.g. the instrument): copied when an edit begins and
  // back on cancel, under engine::lockProject(). region() is asked at both times.
  void setEditScope(std::function<void*()> region, size_t size) {
    scope_ = std::move(region);
    scopeSize_ = size;
  }
  // Called after a cancel (e.g. a toast).
  void setOnCancel(std::function<void()> f) { onCancel_ = std::move(f); }
  // Restores the value the edit began with and leaves edit; false when not editing.
  bool cancelEdit();
  // 0, or -1 / +1: a click (Shift+click) on the page bar, the previous / next page.
  int onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void edit(int delta);
  // Row under y, or -1.
  int rowAt(int y) const;
  int rowY(int row) const { return y_ + (row - top_) * kRowH; }
  void draw(LGFX_Sprite& s, int y);

 private:
  int shown() const { return visible_ > 0 && visible_ < count_ ? visible_ : count_; }
  void ensureVisible();
  void beginEdit();  // remembers the value for cancelEdit()

  const Param* params_ = nullptr;
  int count_ = 0;
  int sel_ = 0;
  int top_ = 0;      // first drawn row
  int visible_ = 0;  // 0 = all
  int dragAcc_ = 0;  // drag px not yet turned into rows
  bool edit_ = false;
  bool bar_ = false;  // setPageBar: position -1 is the screen's page bar
  int y_;  // top of the first row, updated by draw()
  std::function<void()> onEdit_;
  std::function<void()> onCancel_;
  std::function<void*()> scope_;
  size_t scopeSize_ = 0;
  uint8_t* snap_ = nullptr;  // scopeSize_ bytes, allocated on first use
  bool snapOk_ = false;      // snap_ holds the scope of the current edit
  int origRow_ = -1;
  char orig_[32] = {0};      // value text when the edit began
  int net_ = 0;              // sum of the edit deltas since
};

}  // namespace ui
