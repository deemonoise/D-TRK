#pragma once
#include <stddef.h>
#include <functional>
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "theme.h"
#include "touch.h"

namespace ui {

// UI task only. Called outside the project mutex, immediately before/after a row edit.
void setParamEditObserver(std::function<void(bool before)> observer);
void setParamHintObserver(std::function<void(const char*)> observer);

struct Param {
  const char* label;
  std::function<void(char* out, int n)> format;
  std::function<void(int delta)> edit;  // called under engine::lockProject()
  std::function<bool()> dim;            // true: drawn grey (does not apply now); empty = never
  std::function<bool()> warn;           // true: value drawn red (e.g. missing sample); empty = never
  std::function<const char*()> hint;    // explanation while dim (display only; editing semantics unchanged)
};

// MIDI knob: turns the row of rows[0, count) whose label matches `label` in its first maxLen chars by
// delta under engine::lockProject() (as ParamList::edit), then its value text into out. False when there
// is no such row or it has no edit.
bool turnRow(const Param* rows, int count, const char* label, size_t maxLen, int delta, char* out, int n);

// Rows of "label  value". Turn = select, click = edit (value red), turn while editing = edit(delta),
// Shift = x10, Shift+click while editing = cancel (the value as it was when the edit began: the
// setEditScope() bytes copied back, else edit(+-1) until the value text matches again). Tap = select, tap on the selected row = edit. Drag while editing = edit(-dy / 16).
// Button A: EditTurn enters the edit if needed (hold edit) and edits (Shift = x10), EditEnd (A released)
// leaves a hold edit with the value kept, EditCancel (B while A held) = cancelEdit(). Rows without edit
// ignore EditTurn. A click / tap edit is never left by EditEnd.
// With setVisibleRows() smaller than the row count the list scrolls (Drag while not editing).
// setPageBar(true): the screen's page bar is a position above the first row (sel() == -1, drawn by
// the screen, see barSelected()); turning cycles through it and the rows. A click on it does nothing;
// A + turn on it returns the delta (pages to move); the screen switches and keeps the bar selected.
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
  // The selected row, nullptr on the page bar or with no rows.
  const Param* selected() const { return sel_ >= 0 && sel_ < count_ ? &params_[sel_] : nullptr; }
  // MIDI learn: the selected row if a knob can find it again by label (turnRow): it has an edit, its label
  // is not "-" and no other row of the list matches it in the first maxLen chars; else nullptr.
  const Param* knobRow(size_t maxLen) const;
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
    hold_ = false;
    top_ = 0;
  }
  bool barSelected() const { return bar_ && sel_ < 0; }
  bool editing() const { return edit_; }
  bool holding() const { return hold_; }  // the edit is an A + turn one: A released ends it
  // hold: restores a hold edit (rows rebuilt while A is held), so EditEnd still ends it.
  void setEdit(bool on, bool hold = false) {
    edit_ = on;
    hold_ = on && hold;
    dragAcc_ = 0;
  }
  // A + turn: enters the edit of the selected row if needed (hold edit). False = row not editable.
  bool holdEdit();
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
  // 0, or the pages to move: A + turn on the page bar.
  int onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void edit(int delta);
  // Row under y, or -1.
  int rowAt(int y) const;
  int rowY(int row) const { return y_ + (row - top_) * kRowH; }
  void draw(LGFX_Sprite& s, int y);
  void setFocusVisible(bool visible) { focusVisible_ = visible; }
  void selectRow(int row) { setSel(row); notifyHint(); }

 private:
  int shown() const { return visible_ > 0 && visible_ < count_ ? visible_ : count_; }
  void ensureVisible();
  void beginEdit();  // remembers the value for cancelEdit()
  void notifyHint(bool attempted = false) const;

  const Param* params_ = nullptr;
  int count_ = 0;
  int sel_ = 0;
  int top_ = 0;      // first drawn row
  int visible_ = 0;  // 0 = all
  int dragAcc_ = 0;  // drag px not yet turned into rows
  bool edit_ = false;
  bool hold_ = false;  // edit_ entered by holdEdit(): EditEnd leaves it
  bool focusVisible_ = true;
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
