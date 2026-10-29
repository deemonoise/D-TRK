#include "transpose_dialog.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "scale.h"

namespace ui {
namespace {

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

TransposeDialog::TransposeDialog(App& app) : app_(app) {
  params_[kAmount] = {"Amount",
                      [this](char* o, int n) { snprintf(o, n, "%+d %s", amount_, degrees_ ? "DEG" : "ST"); },
                      [this](int d) {
                        amount_ += d;
                        clampAmount();
                      }};
  params_[kMode] = {"Mode", [this](char* o, int n) { snprintf(o, n, "%s", degrees_ ? "SCALE" : "CHROMATIC"); },
                    [this](int d) {
                      degrees_ = d < 0;
                      clampAmount();
                    }};
  list_.setParams(params_, kOk);
  list_.setVisibleRows(actions_.listRows(kHeaderH));
}

int TransposeDialog::octave() const {
  return degrees_ ? mt::scaleDegrees(static_cast<mt::ScaleType>(app_.project().scaleType)) : 12;
}

void TransposeDialog::clampAmount() {
  const int m = 2 * octave();
  amount_ = clampi(amount_, -m, m);
}

void TransposeDialog::open(const char* title, std::function<void(int, bool)> onOk) {
  strlcpy(title_, title ? title : "", sizeof(title_));
  onOk_ = std::move(onOk);
  amount_ = 0;  // a shift of the notes, not a state: starting at the last amount hid that
  list_.setEdit(false);
  list_.setSel(kAmount);
  actions_.reset(list_);
  open_ = true;
}

void TransposeDialog::ok() {
  if (!open_) return;
  open_ = false;
  list_.setEdit(false);
  std::function<void(int, bool)> cb = std::move(onOk_);
  onOk_ = nullptr;
  if (cb && amount_ != 0) cb(amount_, degrees_);
}

void TransposeDialog::cancel() {
  if (!open_) return;
  open_ = false;
  list_.setEdit(false);
  onOk_ = nullptr;
}

bool TransposeDialog::action(int row) {
  if (row == kOk) ok();
  else if (row == kCancel) cancel();
  else return false;
  app_.invalidate();
  return true;
}

void TransposeDialog::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  if (!open_) return;
  switch (ev.type) {
    case InputType::EncLong:
      cancel();
      app_.invalidate();
      return;

    case InputType::EncTurn:
      if (list_.editing() && list_.sel() == kAmount && ev.shift) {  // octave steps, not ParamList's x10
        list_.edit(ev.delta * octave());
        return;
      }
      break;
    case InputType::EditTurn:  // A + turn: the same, entering the edit if needed
      if (!actions_.footerFocused() && list_.sel() == kAmount && ev.shift && list_.holdEdit()) {
        list_.edit(ev.delta * octave());
        return;
      }
      break;
    default: break;
  }
  const int result=actions_.input(list_,kOk,ev);
  if(result>=0)action(result==0?kOk:kCancel);
}

void TransposeDialog::onTouch(const TouchEvent& ev) {
  if (!open_) return;
  const int result=actions_.touch(list_,ev,y0_);
  if(result>=0)action(result==0?kOk:kCancel);
}


void TransposeDialog::draw(LGFX_Sprite& s, int y0) {
  if (!open_) return;
  y0_ = y0;
  char buf[48];
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  snprintf(buf, sizeof(buf), "TRANSPOSE %s", title_);
  s.setTextColor(kText);
  s.drawString(buf, ParamList::kLabelX, y0 + (kHeaderH - 4 - kCharH) / 2);
  list_.draw(s, y0 + kHeaderH);
  actions_.draw(s,y0,"APPLY");
}

}  // namespace ui
