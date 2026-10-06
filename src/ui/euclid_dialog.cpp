#include "euclid_dialog.h"
#include <esp_random.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "note_name.h"
#include "scale.h"

namespace ui {
namespace {

constexpr int kMaxAccentEvery = 64;
const char* const kFillNames[] = {"ROOT", "UP", "DOWN", "UP/DOWN", "RANDOM"};
static_assert(sizeof(kFillNames) / sizeof(kFillNames[0]) == static_cast<size_t>(mt::EuclidFill::Count), "fill names");

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
uint8_t clampu8(int v, int lo, int hi) { return static_cast<uint8_t>(clampi(v, lo, hi)); }

}  // namespace

EuclidDialog::EuclidDialog(App& app) : app_(app) {
  params_[kHits] = {"Hits", [this](char* o, int n) { snprintf(o, n, "%u", e_->hits); },
                    [this](int d) { e_->hits = clampu8(e_->hits + d, 0, e_->length); }};
  params_[kLength] = {"Length", [this](char* o, int n) { snprintf(o, n, "%u", e_->length); },
                      [this](int d) {
                        e_->length = clampu8(e_->length + d, 1, patLen());
                        clampParams();
                      }};
  params_[kRotation] = {"Rotation", [this](char* o, int n) { snprintf(o, n, "%d", e_->rotation); },
                        [this](int d) {
                          const int m = e_->length - 1;
                          e_->rotation = static_cast<int8_t>(clampi(e_->rotation + d, -m, m));
                        }};
  params_[kLane] = {"Lane", [this](char* o, int n) { snprintf(o, n, "%d", e_->lane + 1); },
                    [this](int d) { e_->lane = static_cast<int8_t>(clampi(e_->lane + d, 0, mt::kKitLanes - 1)); }};
  params_[kFill] = {"Fill", [this](char* o, int n) { snprintf(o, n, "%s", kFillNames[static_cast<int>(e_->fill)]); },
                    [this](int d) {
                      const int f = clampi(static_cast<int>(e_->fill) + d, 0, static_cast<int>(mt::EuclidFill::Count) - 1);
                      e_->fill = static_cast<mt::EuclidFill>(f);
                    }};
  params_[kBase] = {"Base note",
                    [this](char* o, int n) {
                      char nn[4];
                      mt::noteName(e_->baseNote, nn);
                      snprintf(o, n, "%s", nn);
                    },
                    [this](int d) { e_->baseNote = clampu8(e_->baseNote + d, 0, 127); }};
  params_[kRange] = {"Range", [this](char* o, int n) { snprintf(o, n, "%u OCT", e_->range); },
                     [this](int d) { e_->range = clampu8(e_->range + d, 1, 4); }};
  params_[kVel] = {"Velocity", [this](char* o, int n) { snprintf(o, n, "%u", e_->vel); },
                   [this](int d) { e_->vel = clampu8(e_->vel + d, 1, 127); }};
  params_[kAccEvery] = {"Accent every",
                        [this](char* o, int n) {
                          if (e_->accentEvery) snprintf(o, n, "%u", e_->accentEvery);
                          else snprintf(o, n, "OFF");
                        },
                        [this](int d) { e_->accentEvery = clampu8(e_->accentEvery + d, 0, kMaxAccentEvery); }};
  params_[kAccVel] = {"Accent vel", [this](char* o, int n) { snprintf(o, n, "%u", e_->accentVel); },
                      [this](int d) { e_->accentVel = clampu8(e_->accentVel + d, 1, 127); }};
  params_[kSeed] = {"Seed",
                    [this](char* o, int n) {
                      snprintf(o, n, "%lu", static_cast<unsigned long>(e_->seed));
                    },
                    [this](int d) { e_->seed += static_cast<uint32_t>(d); }};
  params_[kReseed] = {"Reseed", [](char* o, int n) { snprintf(o, n, "CLICK / SHIFT+CLICK"); }, nullptr};
  params_[kMode] = {"Mode", [this](char* o, int n) { snprintf(o, n, "%s", e_->merge ? "MERGE" : "REPLACE"); },
                    [this](int d) { e_->merge = d > 0; }};
  params_[kOk] = {"OK", nullptr, nullptr};
  params_[kCancel] = {"Cancel", [](char* o, int n) { snprintf(o, n, "LONG PRESS"); }, nullptr};
  buildRows();
  list_.setVisibleRows((kAreaH - kHeaderH) / ParamList::kRowH);
  list_.setOnEdit([this] { preview(); });
}

void EuclidDialog::buildRows() {
  const bool lane = e_ && e_->lane >= 0;
  shownCount_ = 0;
  for (int id = 0; id < kRows; ++id) {
    const bool melodicOnly = id == kFill || id == kBase || id == kRange || id == kVel || id == kSeed || id == kReseed;
    if (id == kLane ? !lane : (melodicOnly && lane)) continue;
    shownIds_[shownCount_] = id;
    shown_[shownCount_++] = params_[id];
  }
  list_.setParams(shown_, shownCount_);
}

int EuclidDialog::listRow(int id) const {
  for (int r = 0; r < shownCount_; ++r)
    if (shownIds_[r] == id) return r;
  return 0;
}

int EuclidDialog::patLen() const {
  return clampi(app_.project().patterns[pattern_].length, mt::kMinSteps, mt::kMaxSteps);
}

void EuclidDialog::clampParams() {
  e_->length = clampu8(e_->length, 1, patLen());
  e_->hits = clampu8(e_->hits, 0, e_->length);
  const int m = e_->length - 1;
  e_->rotation = static_cast<int8_t>(clampi(e_->rotation, -m, m));
  e_->range = clampu8(e_->range, 1, 4);
  e_->vel = clampu8(e_->vel, 1, 127);
  e_->accentVel = clampu8(e_->accentVel, 1, 127);
  e_->baseNote = clampu8(e_->baseNote, 0, 127);
  if (e_->lane >= mt::kKitLanes) e_->lane = mt::kKitLanes - 1;
}

void EuclidDialog::open(int pattern, int track, mt::EuclidParams* e) {
  pattern_ = clampi(pattern, 0, mt::kPatterns - 1);
  track_ = clampi(track, 0, mt::kTracks - 1);
  e_ = e;
  clampParams();
  app_.pushUndo();
  // No lock: the engine never writes pattern data.
  memcpy(saved_, app_.project().patterns[pattern_].steps[track_], sizeof(saved_));
  seqAtOpen_ = app_.editSeq();
  previews_ = 0;
  buildRows();
  list_.setEdit(false);
  list_.setSel(listRow(kHits));
  open_ = true;
  preview();
}

void EuclidDialog::preview() {
  mt::Project& p = app_.project();
  const mt::ScaleType scale = static_cast<mt::ScaleType>(p.scaleType);
  engine::lockProject();
  mt::Pattern& pt = p.patterns[pattern_];
  memcpy(pt.steps[track_], saved_, sizeof(saved_));
  mt::applyEuclid(pt, track_, *e_, p.scaleRoot, scale);
  engine::unlockProject();
  app_.markDirty();
  ++previews_;
  engine::post(engine::Cmd::ReleaseTies);  // a held TIE may have lost its step
}

void EuclidDialog::ok() {
  if (!open_) return;
  open_ = false;
  list_.setEdit(false);
  app_.toast("EUCLID");
}

void EuclidDialog::cancel() {
  if (!open_) return;
  open_ = false;
  list_.setEdit(false);
  engine::lockProject();
  memcpy(app_.project().patterns[pattern_].steps[track_], saved_, sizeof(saved_));
  engine::unlockProject();
  // Back to the dirty state of open() unless something else was edited meanwhile (e.g. BPM).
  if (app_.editSeq() == seqAtOpen_ + previews_) app_.rewindEditSeq(seqAtOpen_);
  else app_.markDirty();
  engine::post(engine::Cmd::ReleaseTies);
  app_.dropUndo();
}

void EuclidDialog::reseed() {
  e_->seed = esp_random();
  preview();
}

bool EuclidDialog::action(int row) {
  if (row == kOk) ok();
  else if (row == kCancel) cancel();
  else if (row == kReseed) reseed();
  else return false;
  app_.invalidate();
  return true;
}

void EuclidDialog::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  if (!open_) return;
  switch (ev.type) {
    case InputType::EncLong: cancel(); return;
    case InputType::EncClick:
      if (ev.shift) {
        if (e_->lane < 0) reseed();
        return;
      }
      if (!list_.editing() && action(rowId(list_.sel()))) return;
      break;
    default: break;
  }
  list_.onInput(ev);
}

void EuclidDialog::onTouch(const TouchEvent& ev) {
  if (!open_) return;
  if (ev.type == TouchType::Tap && ev.y < y0_ + kHeaderH) {
    const int by = y0_ + kBtnY;
    if (ev.y >= by && ev.y < by + kBtnH) {
      if (ev.x >= kCancelX && ev.x < kCancelX + kCancelW) action(kCancel);
      else if (ev.x >= kOkX && ev.x < kOkX + kOkW) action(kOk);
    }
    return;
  }
  if (ev.type == TouchType::Tap) {
    const int r = list_.rowAt(ev.y);
    const int id = rowId(r);
    if (id == kOk || id == kCancel || id == kReseed) {
      list_.setSel(r);
      action(id);
      return;
    }
  }
  list_.onTouch(ev);
}

void EuclidDialog::drawButton(LGFX_Sprite& s, int x, int y, int w, const char* label, bool sel) {
  s.fillRect(x, y, w, kBtnH, sel ? kPlayBg : kMenuBg);
  s.drawRect(x, y, w, kBtnH, sel ? kCursor : kDim);
  s.setTextColor(sel ? kCursor : kText);
  s.drawString(label, x + (w - static_cast<int>(strlen(label)) * kCharW) / 2, y + (kBtnH - kCharH) / 2);
}

void EuclidDialog::draw(LGFX_Sprite& s, int y0) {
  if (!open_) return;
  y0_ = y0;
  char buf[48];
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  snprintf(buf, sizeof(buf), "EUCLID T%d %s P%02d", track_ + 1, app_.project().tracks[track_].name, pattern_ + 1);
  s.setTextColor(kText);
  s.drawString(buf, ParamList::kLabelX, y0 + (kHeaderH - 4 - kCharH) / 2);
  drawButton(s, kCancelX, y0 + kBtnY, kCancelW, "CANCEL", rowId(list_.sel()) == kCancel);
  drawButton(s, kOkX, y0 + kBtnY, kOkW, "OK", rowId(list_.sel()) == kOk);
  list_.draw(s, y0 + kHeaderH);
}

}  // namespace ui
