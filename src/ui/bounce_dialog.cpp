#include "bounce_dialog.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "bounce.h"
#include "engine/engine.h"
#include "hw/sdcard.h"
#include "menu.h"
#include "note_name.h"
#include "oneshot_lib.h"
#include "sample_set.h"
#include "storage/render_io.h"
#include "storage/storage.h"

namespace ui {

namespace {

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

BounceDialog::BounceDialog(App& app) : app_(app) {
  params_[kNote] = {"Note",
                    [this](char* o, int n) {
                      char nm[4];
                      mt::noteName(note_, nm);
                      snprintf(o, n, "%s", nm);
                    },
                    [this](int d) { note_ = static_cast<uint8_t>(clampInt(note_ + d, 0, 127)); }};
  params_[kHold] = {"Hold",
                    [this](char* o, int n) {
                      snprintf(o, n, "%d.%02d S", hold_ * 5 / 100, hold_ * 5 % 100);
                    },
                    [this](int d) { hold_ = clampInt(hold_ + d, kHoldMin, kHoldMax); }};  // Shift: x10 = 0.5 s
  params_[kVel] = {"Vel", [this](char* o, int n) { snprintf(o, n, "%d", vel_); },
                   [this](int d) { vel_ = static_cast<uint8_t>(clampInt(vel_ + d, 1, 127)); }};
  params_[kName] = {"Name", [this](char* o, int n) { snprintf(o, n, "%s", name_); }, nullptr};
  params_[kSave] = {"SAVE", nullptr, nullptr};
  params_[kCancel] = {"Cancel", [](char* o, int n) { snprintf(o, n, "LONG PRESS"); }, nullptr};
  kitParams_[0] = params_[kHold];
  kitParams_[1] = params_[kVel];
  kitParams_[2] = params_[kSave];
  kitParams_[3] = params_[kCancel];
  list_.setParams(params_, kRows);
}

bool BounceDialog::progress(uint32_t done, uint32_t total, void* ctx) {
  BounceDialog& d = *static_cast<BounceDialog*>(ctx);
  return App::labeledProgress(d.progLabel_, done, total, &d.app_);
}

int BounceDialog::rowId(int listRow) const {
  static const int kKitIds[kKitRows] = {kHold, kVel, kSave, kCancel};
  if (!kit_) return listRow;
  return listRow >= 0 && listRow < kKitRows ? kKitIds[listRow] : -1;
}

void BounceDialog::open(int instr, uint8_t note, uint8_t insTrack) {
  instr_ = instr;
  note_ = note > 127 ? 60 : note;
  ins_ = insTrack;
  vel_ = 127;
  kit_ = false;
  onBaked_ = nullptr;
  mt::nextBounceName(app_.project(), app_.project().instruments[instr].name, name_);
  list_.setParams(params_, kRows);
  list_.setEdit(false);
  list_.setSel(kSave);
  open_ = true;
}

void BounceDialog::openKit(int kit, std::function<void()> onBaked) {
  instr_ = kit;
  ins_ = mt::kTracks;  // dry: the baked lane plays through the track's insert again
  vel_ = 127;
  kit_ = true;
  onBaked_ = std::move(onBaked);
  list_.setParams(kitParams_, kKitRows);
  list_.setEdit(false);
  list_.setSel(2);  // SAVE
  open_ = true;
}

void BounceDialog::cancel() {
  if (!open_) return;
  open_ = false;
  kb_.close();
  list_.setEdit(false);
}

void BounceDialog::save() {
  if (!hw::sdReady()) {
    app_.toast("NO SD");
    return;
  }
  if (app_.project().sampleCount >= mt::kProjSamples) {
    app_.toast("SAMPLE LIST FULL");
    return;
  }
  if (kit_) {
    runKit();
    return;
  }
  if (mt::projSampleFind(app_.project(), name_) >= 0) {
    enum : int { kNo, kYes };
    const MenuItem items[] = {{"Cancel", kNo}, {"Overwrite", kYes}};
    app_.menu().open("SAMPLE EXISTS", items, 2, [this](int i) {
      if (i == kYes) run();
    });
    return;
  }
  run();
}

bool BounceDialog::stopTransport() {
  const engine::Status st = engine::status();
  if (st.playing || st.paused) {  // the render resets the synth
    if (!storage::stopEngine()) {
      app_.toast("BUSY");
      return false;
    }
    app_.toast("STOPPED");
  }
  return true;
}

void BounceDialog::resultToast(int res) {
  const storage::Result r = static_cast<storage::Result>(res);
  switch (r) {
    case storage::Result::NoSd: app_.toast("NO SD"); break;
    case storage::Result::EngineBusy:
    case storage::Result::AudioBusy: app_.toast("BUSY"); break;
    case storage::Result::NoSynth: app_.toast("SYNTH DOWN"); break;
    case storage::Result::BankFull: app_.toast("SAMPLE LIST FULL"); break;
    case storage::Result::Silent: app_.toast("SILENT"); break;
    case storage::Result::Cancelled: break;
    default:
      if (r == storage::Result::WriteFail || r == storage::Result::ReadFail) hw::sdRecover();
      app_.toast(storage::resultText(r));
      break;
  }
}

void BounceDialog::run() {
  if (!stopTransport()) return;
  mt::BounceSpec spec;
  spec.instr = static_cast<uint8_t>(instr_);
  spec.note = note_;
  spec.vel = vel_;
  spec.insTrack = ins_;
  spec.holdBlocks = mt::bounceHoldBlocks(hold_ * 0.05f);
  storage::RenderStats rs;
  strlcpy(progLabel_, "SHOT", sizeof(progLabel_));
  app_.showBusy("SHOT...");
  const storage::Result r = storage::bounce(app_.project(), spec, name_, rs, progress, this);
  app_.endProgress();
  if (r == storage::Result::Ok) {
    app_.markDirty();
    char msg[40];
    if (rs.copied) snprintf(msg, sizeof(msg), "SAVED %s", name_);
    else snprintf(msg, sizeof(msg), "SAVED, NO COPY");
    app_.toast(msg);
    cancel();
  } else {
    resultToast(static_cast<int>(r));
  }
  app_.invalidate();
}

// Each lane rendered as the KIT at the lane's note (the KIT plays that lane there), into a sample named
// after the lane's instrument, kept in that instrument's folder; then the lane plays the sample.
void BounceDialog::runKit() {
  mt::Project& p = app_.project();
  uint8_t lanes[mt::kKitLanes];
  const int n = mt::kitBakeLanes(p, instr_, lanes);
  if (n == 0) {
    app_.toast("NO INSTR LANES");
    return;
  }
  if (!stopTransport()) return;
  int baked = 0;
  bool noCopy = false;
  storage::Result err = storage::Result::Ok;
  app_.showBusy("BAKE...");
  for (int i = 0; i < n; ++i) {
    const int l = lanes[i];
    const mt::KitLane& ln = p.instruments[instr_].kit[l];
    const mt::Instrument& src = p.instruments[ln.instr];  // its folder and name
    char name[mt::kSampleNameMax + 1];
    mt::nextBounceName(p, src.name, name);
    mt::BounceSpec spec;
    spec.instr = static_cast<uint8_t>(instr_);
    spec.note = ln.note;
    spec.vel = vel_;
    spec.insTrack = ins_;
    spec.holdBlocks = mt::bounceHoldBlocks(hold_ * 0.05f);
    spec.normalize = false;  // the lane at kLaneUnityVol sounds as the instrument did
    snprintf(progLabel_, sizeof(progLabel_), "LANE %d/%d", i + 1, n);
    storage::RenderStats rs;
    const storage::Result r = storage::bounce(p, spec, name, rs, progress, this, &src);
    if (r == storage::Result::Silent) continue;
    if (r != storage::Result::Ok) {
      err = r;
      break;
    }
    engine::lockProject();
    mt::kitLaneToSample(p.instruments[instr_], l, name);
    engine::unlockProject();
    app_.markDirty();
    ++baked;
    if (!rs.copied) noCopy = true;
  }
  app_.endProgress();
  if (baked > 0 && onBaked_) onBaked_();
  if (err != storage::Result::Ok && !(err == storage::Result::Cancelled && baked > 0)) {
    resultToast(static_cast<int>(err));
  } else if (baked == 0) {
    app_.toast("SILENT");  // every lane
  } else {
    char msg[40];
    snprintf(msg, sizeof(msg), "SAVED %d LANES%s", baked, noCopy ? ", NO COPY" : "");
    app_.toast(msg);
    cancel();
  }
  app_.invalidate();
}

bool BounceDialog::action(int row) {
  if (row < 0) return false;
  if (row == kSave) save();
  else if (row == kCancel) cancel();
  else if (row == kName)
    kb_.open("SAMPLE NAME:", name_, [this](const char* t) {
      char s[mt::kSampleNameMax + 1];
      if (storage::sanitize(t, s)) strlcpy(name_, s, sizeof(name_));
      app_.invalidate();
    });
  else return false;
  app_.invalidate();
  return true;
}

void BounceDialog::onInput(const hw::InputEvent& ev) {
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
      if (!list_.editing() && action(rowId(list_.sel()))) return;
      break;
    default: break;
  }
  list_.onInput(ev);
}

void BounceDialog::onTouch(const TouchEvent& ev) {
  if (!open_) return;
  if (kb_.isOpen()) {
    kb_.onTouch(ev, app_.shift());
    app_.invalidate();
    return;
  }
  if (ev.type == TouchType::Tap && ev.y < y0_ + kHeaderH) {
    const int by = y0_ + kBtnY;
    if (ev.y >= by && ev.y < by + kBtnH) {
      if (ev.x >= kCancelX && ev.x < kCancelX + kCancelW) action(kCancel);
      else if (ev.x >= kOkX && ev.x < kOkX + kOkW) action(kSave);
    }
    return;
  }
  if (ev.type == TouchType::Tap) {
    const int r = list_.rowAt(ev.y);
    const int id = rowId(r);
    if (id == kSave || id == kCancel || id == kName) {
      list_.setSel(r);
      action(id);
      return;
    }
  }
  list_.onTouch(ev);
}

void BounceDialog::drawButton(LGFX_Sprite& s, int x, int y, int w, const char* label, bool sel) {
  s.fillRect(x, y, w, kBtnH, sel ? kPlayBg : kMenuBg);
  s.drawRect(x, y, w, kBtnH, sel ? kCursor : kDim);
  s.setTextColor(sel ? kCursor : kText);
  s.drawString(label, x + (w - static_cast<int>(strlen(label)) * kCharW) / 2, y + (kBtnH - kCharH) / 2);
}

void BounceDialog::draw(LGFX_Sprite& s, int y0) {
  if (!open_) return;
  y0_ = y0;
  if (kb_.isOpen()) {
    kb_.draw(s, y0);
    return;
  }
  char buf[48];
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  snprintf(buf, sizeof(buf), "ONE-SHOT %s%s%s", kit_ ? "KIT " : "", app_.project().instruments[instr_].name,
           !kit_ && ins_ < mt::kTracks ? " +FX" : "");
  s.setTextColor(kText);
  s.drawString(buf, ParamList::kLabelX, y0 + (kHeaderH - 4 - kCharH) / 2);
  drawButton(s, kCancelX, y0 + kBtnY, kCancelW, "CANCEL", rowId(list_.sel()) == kCancel);
  drawButton(s, kOkX, y0 + kBtnY, kOkW, "SAVE", rowId(list_.sel()) == kSave);
  list_.draw(s, y0 + kHeaderH);
}

}  // namespace ui
