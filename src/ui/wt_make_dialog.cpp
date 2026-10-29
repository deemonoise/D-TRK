#include "wt_make_dialog.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "hw/sdcard.h"
#include "link/bank_client.h"
#include "menu.h"
#include "storage/storage.h"
#include "wt_picker.h"

namespace ui {

namespace {

void progress(uint32_t done, uint32_t total, void* ctx) { static_cast<App*>(ctx)->showProgress("WT", done, total); }

}  // namespace

WtMakeDialog::WtMakeDialog(App& app) : app_(app) {
  params_[kMode] = {"Mode", [this](char* o, int n) { snprintf(o, n, "%s", p_.mode == mt::WtMode::Cycle ? "CYCLE" : "WINDOW"); },
                    [this](int d) { p_.mode = d < 0 ? mt::WtMode::Cycle : mt::WtMode::Window; }};
  params_[kNorm] = {"Norm", [this](char* o, int n) { snprintf(o, n, "%s", p_.norm == mt::WtNorm::Frame ? "FRAME" : "TABLE"); },
                    [this](int d) { p_.norm = d < 0 ? mt::WtNorm::Frame : mt::WtNorm::Table; }};
  params_[kSeam] = {"Seam", [this](char* o, int n) { snprintf(o, n, "%s", p_.seam ? "ON" : "OFF"); },
                    [this](int d) { p_.seam = d > 0; }};
  params_[kName] = {"Name", [this](char* o, int n) { snprintf(o, n, "%s", name_); }, nullptr};
  params_[kMake] = {"MAKE", nullptr, nullptr};
  params_[kCancel] = {"Cancel", [](char* o, int n) { snprintf(o, n, "LONG PRESS"); }, nullptr};
  list_.setParams(params_, kMake);
  list_.setVisibleRows(actions_.listRows(kHeaderH));
}

void WtMakeDialog::open(int index, uint32_t from, uint32_t to, uint32_t rate, const char* name) {
  index_ = index;
  from_ = from;
  to_ = to;
  rate_ = rate ? rate : 44100;
  if (!storage::sanitize(name ? name : "", name_)) strlcpy(name_, "WT", sizeof(name_));
  list_.setEdit(false);
  list_.setSel(kMode);
  actions_.reset(list_);
  open_ = true;
}

void WtMakeDialog::cancel() {
  if (!open_) return;
  open_ = false;
  kb_.close();
  list_.setEdit(false);
}

void WtMakeDialog::make() {
  const engine::Status& st = app_.status();
  if (st.playing || st.paused) {
    app_.toast(audio::bankResultText(audio::BankResult::Busy));
    return;
  }
  if (!hw::sdReady()) {
    app_.toast(audio::bankResultText(audio::BankResult::NoSd));
    return;
  }
  if (to_ - from_ < mt::kWtMakeMinLen) {
    app_.toast(mt::wtMakeErrText(mt::WtMakeErr::TooShort));
    return;
  }
  if (app_.project().wavetableCount >= mt::kProjWavetables) {
    app_.toast("TABLE LIST FULL");
    return;
  }
  snprintf(path_, sizeof(path_), "/wavetables/%s.wav", name_);
  if (hw::sdFs().exists(path_)) {
    enum : int { kNo, kYes };
    const MenuItem items[] = {{"Cancel", kNo}, {"Overwrite", kYes}};
    app_.menu().open("FILE EXISTS", items, 2, [this](int i) {
      if (i == kYes) run();
    });
    return;
  }
  run();
}

void WtMakeDialog::run() {
  mt::WtMakeErr err = mt::WtMakeErr::Ok;
  int pitched = 0;
  app_.showBusy("WT...");
  const audio::BankResult r = audio::wtRender(index_, from_, to_, p_, path_, err, pitched, progress, &app_);
  app_.endProgress();
  if (r != audio::BankResult::Ok) {
    if (r == audio::BankResult::WriteFail || r == audio::BankResult::ReadFail) hw::sdRecover();
    app_.toast(audio::bankResultText(r));
    return;
  }
  if (err != mt::WtMakeErr::Ok) {
    app_.toast(mt::wtMakeErrText(err));
    return;
  }
  char nm[mt::kSampleNameMax + 1];
  const bool ok = wtImportFile(app_, path_, name_, nm);
  app_.endProgress();
  if (!ok) return;  // it toasted
  char msg[40];
  if (p_.mode == mt::WtMode::Cycle) snprintf(msg, sizeof(msg), "WT %s %d/%d", nm, pitched, mt::kWtFrames);
  else snprintf(msg, sizeof(msg), "WT %s", nm);
  app_.toast(msg);
  cancel();
  app_.invalidate();
}

bool WtMakeDialog::action(int row) {
  if (row == kMake) make();
  else if (row == kCancel) cancel();
  else if (row == kName)
    kb_.open("TABLE NAME:", name_, [this](const char* t) {
      if (!storage::sanitize(t, name_)) strlcpy(name_, "WT", sizeof(name_));
      app_.invalidate();
    });
  else return false;
  app_.invalidate();
  return true;
}

void WtMakeDialog::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  if (!open_) return;
  if (kb_.isOpen()) {
    kb_.onInput(ev);
    app_.invalidate();
    return;
  }
  switch (ev.type) {
    case InputType::EncLong:
      cancel();
      app_.invalidate();
      return;
    case InputType::EncClick:
      if (!actions_.footerFocused() && !list_.editing() && action(list_.sel())) return;
      break;
    default: break;
  }
  const int result=actions_.input(list_,kMake,ev);
  if(result>=0)action(result==0?kMake:kCancel);
}

void WtMakeDialog::onTouch(const TouchEvent& ev) {
  if (!open_) return;
  if (kb_.isOpen()) {
    kb_.onTouch(ev, app_.shift());
    app_.invalidate();
    return;
  }
  if(ev.y>=y0_+kAreaH-DialogActions::kH){
    const int result=actions_.touch(list_,ev,y0_);if(result>=0)action(result==0?kMake:kCancel);return;
  }
  if (ev.type == TouchType::Tap) {
    const int r = list_.rowAt(ev.y);
    if (r == kName) {
      actions_.focusList(list_);
      list_.setSel(r);
      action(r);
      return;
    }
  }
  actions_.touch(list_,ev,y0_);
}

void WtMakeDialog::draw(LGFX_Sprite& s, int y0) {
  if (!open_) return;
  y0_ = y0;
  if (kb_.isOpen()) {
    kb_.draw(s, y0);
    return;
  }
  char buf[48];
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  const uint32_t ms = static_cast<uint32_t>((static_cast<uint64_t>(to_ - from_) * 1000) / rate_);
  snprintf(buf, sizeof(buf), "TO WAVETABLE %lu MS", static_cast<unsigned long>(ms));
  s.setTextColor(kText);
  s.drawString(buf, ParamList::kLabelX, y0 + (kHeaderH - 4 - kCharH) / 2);
  list_.draw(s, y0 + kHeaderH);
  actions_.draw(s,y0,"CREATE");
}

}  // namespace ui
