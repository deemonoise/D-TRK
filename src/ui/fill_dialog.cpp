#include "fill_dialog.h"
#include <algorithm>
#include <esp_random.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "fx_info.h"
#include "note_name.h"

namespace ui {
namespace {

const char* const kWhereNames[] = {"EVERY N", "EUCLID", "RANDOM %"};
const char* const kValueNames[] = {"CONST", "RAMP", "RANDOM"};
const char* const kModeNames[] = {"OVERWRITE", "EMPTY ONLY", "NOTES ONLY"};
static_assert(sizeof(kWhereNames) / sizeof(kWhereNames[0]) == static_cast<size_t>(mt::FillWhere::Count), "where names");
static_assert(sizeof(kValueNames) / sizeof(kValueNames[0]) == static_cast<size_t>(mt::FillValue::Count), "value names");
static_assert(sizeof(kModeNames) / sizeof(kModeNames[0]) == static_cast<size_t>(mt::FillMode::Count), "mode names");
constexpr int kTargets = 2 + mt::kFxSlots;  // NOTE, VEL, FX1..

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
uint8_t clampu8(int v, int lo, int hi) { return static_cast<uint8_t>(clampi(v, lo, hi)); }

template <typename E>
E stepEnum(E e, int d) {
  return static_cast<E>(clampi(static_cast<int>(e) + d, 0, static_cast<int>(E::Count) - 1));
}

}  // namespace

FillDialog::FillDialog(App& app) : app_(app) {
  params_[kWhere] = {"Steps", [this](char* o, int n) { snprintf(o, n, "%s", kWhereNames[static_cast<int>(f_->where)]); },
                     [this](int d) { f_->where = stepEnum(f_->where, d); }};
  params_[kEvery] = {"Every", [this](char* o, int n) { snprintf(o, n, "%u", f_->every); },
                     [this](int d) {
                       f_->every = clampu8(f_->every + d, 1, rangeLen());
                       clampParams();
                     }};
  params_[kOffset] = {"Offset", [this](char* o, int n) { snprintf(o, n, "%u", f_->offset); },
                      [this](int d) { f_->offset = clampu8(f_->offset + d, 0, f_->every - 1); }};
  params_[kHits] = {"Hits", [this](char* o, int n) { snprintf(o, n, "%u", f_->hits); },
                    [this](int d) { f_->hits = clampu8(f_->hits + d, 0, f_->length); }};
  params_[kLength] = {"Length", [this](char* o, int n) { snprintf(o, n, "%u", f_->length); },
                      [this](int d) {
                        f_->length = clampu8(f_->length + d, 1, rangeLen());
                        clampParams();
                      }};
  params_[kRotation] = {"Rotation", [this](char* o, int n) { snprintf(o, n, "%d", f_->rotation); },
                        [this](int d) {
                          const int m = f_->length - 1;
                          f_->rotation = static_cast<int8_t>(clampi(f_->rotation + d, -m, m));
                        }};
  params_[kDensity] = {"Density", [this](char* o, int n) { snprintf(o, n, "%u %%", f_->density); },
                       [this](int d) { f_->density = clampu8(f_->density + d, 0, 100); }};
  params_[kTarget] = {"Fill",
                      [this](char* o, int n) {
                        const int t = targetIndex();
                        if (t == 0) snprintf(o, n, "%s", drum_ ? "HITS (LANE)" : "NOTE");
                        else if (t == 1) snprintf(o, n, "VELOCITY");
                        else snprintf(o, n, "FX %d", t - 1);
                      },
                      [this](int d) { setTarget(clampi(targetIndex() + d, 0, kTargets - 1)); }};
  params_[kCmd] = {"Command",
                   [this](char* o, int n) {
                     if (f_->cmd == mt::Fx::None) snprintf(o, n, "... CLEAR SLOT");
                     else snprintf(o, n, "%s %s", mt::fxName(f_->cmd), mt::fxLongName(f_->cmd));
                   },
                   [this](int d) {
                     f_->cmd = mt::fxNextCmd(f_->cmd, d > 0 ? 1 : -1);
                     valueDefaults();
                   }};
  params_[kLane] = {"Lane", [this](char* o, int n) { snprintf(o, n, "%u", f_->lane + 1); },
                    [this](int d) { f_->lane = clampu8(f_->lane + d, 0, mt::kKitLanes - 1); }};
  params_[kValue] = {"Value", [this](char* o, int n) { snprintf(o, n, "%s", kValueNames[static_cast<int>(f_->value)]); },
                     [this](int d) { f_->value = stepEnum(f_->value, d); }};
  params_[kFrom] = {"From", [this](char* o, int n) { formatValue(f_->from, o, n); },
                    [this](int d) { f_->from = stepValue(f_->from, d); }};
  params_[kTo] = {"To", [this](char* o, int n) { formatValue(f_->to, o, n); },
                  [this](int d) { f_->to = stepValue(f_->to, d); }};
  params_[kMode] = {"Mode", [this](char* o, int n) { snprintf(o, n, "%s", kModeNames[static_cast<int>(f_->mode)]); },
                    [this](int d) { f_->mode = stepEnum(f_->mode, d); }};
  params_[kSeed] = {"Seed", [this](char* o, int n) { snprintf(o, n, "%lu", static_cast<unsigned long>(f_->seed)); },
                    [this](int d) { f_->seed += static_cast<uint32_t>(d); }};
  params_[kReseed] = {"Reseed", [](char* o, int n) { snprintf(o, n, "CLICK / SHIFT+CLICK"); }, nullptr};
  params_[kOk] = {"OK", nullptr, nullptr};
  params_[kCancel] = {"Cancel", [](char* o, int n) { snprintf(o, n, "LONG PRESS"); }, nullptr};
  list_.setVisibleRows((kAreaH - kHeaderH) / ParamList::kRowH);
  list_.setOnEdit([this] {
    buildRows();
    preview();
  });
}

int FillDialog::targetIndex() const {
  if (f_->target == mt::FillTarget::Note) return 0;
  if (f_->target == mt::FillTarget::Vel) return 1;
  return 2 + f_->slot;
}

void FillDialog::setTarget(int i) {
  if (i == targetIndex()) return;
  if (i == 0) f_->target = mt::FillTarget::Note;
  else if (i == 1) f_->target = mt::FillTarget::Vel;
  else {
    const bool wasFx = f_->target == mt::FillTarget::Fx;
    f_->target = mt::FillTarget::Fx;
    f_->slot = static_cast<uint8_t>(i - 2);
    if (wasFx) return;  // another slot: same command and values
  }
  valueDefaults();
}

void FillDialog::valueDefaults() {
  switch (f_->target) {
    case mt::FillTarget::Note:
      f_->from = 60;
      f_->to = 72;
      break;
    case mt::FillTarget::Vel:
      f_->from = 100;
      f_->to = 127;
      break;
    default: {
      const uint8_t v = f_->cmd == mt::Fx::None ? 0 : mt::fxDefault(f_->cmd);
      f_->from = f_->to = v;
      break;
    }
  }
}

void FillDialog::formatValue(uint8_t v, char* out, int n) const {
  switch (f_->target) {
    case mt::FillTarget::Note: {
      char nn[4];
      mt::noteName(v, nn);
      snprintf(out, n, "%s", nn);
      break;
    }
    case mt::FillTarget::Vel: snprintf(out, n, "%u", v); break;
    default: {
      if (f_->cmd == mt::Fx::None) {
        snprintf(out, n, "---");
        break;
      }
      char s[5];
      mt::fxFormat(f_->cmd, v, s);
      const char* p = s;
      while (*p == ' ') ++p;
      snprintf(out, n, "%s", p);
      break;
    }
  }
}

uint8_t FillDialog::stepValue(uint8_t v, int d) const {
  switch (f_->target) {
    case mt::FillTarget::Note: return clampu8(v + d, 0, 127);
    case mt::FillTarget::Vel: return clampu8(v + d, 1, 127);
    default: return f_->cmd == mt::Fx::None ? 0 : mt::fxStep(f_->cmd, v, d);
  }
}

void FillDialog::buildRows() {
  const bool lane = drum_ && f_->target == mt::FillTarget::Note;
  const bool fx = f_->target == mt::FillTarget::Fx;
  const bool values = !lane && !(fx && f_->cmd == mt::Fx::None);
  const bool seeded = f_->where == mt::FillWhere::Random || (values && f_->value == mt::FillValue::Random);
  params_[kFrom].label = f_->value == mt::FillValue::Const ? "Value" : "From";
  shownCount_ = 0;
  for (int id = 0; id < kRows; ++id) {
    bool show = true;
    switch (id) {
      case kEvery: case kOffset: show = f_->where == mt::FillWhere::Every; break;
      case kHits: case kLength: case kRotation: show = f_->where == mt::FillWhere::Euclid; break;
      case kDensity: show = f_->where == mt::FillWhere::Random; break;
      case kCmd: show = fx; break;
      case kLane: show = lane; break;
      case kValue: case kFrom: show = values; break;
      case kTo: show = values && f_->value != mt::FillValue::Const; break;
      case kSeed: case kReseed: show = seeded; break;
      default: break;
    }
    if (!show) continue;
    shownIds_[shownCount_] = id;
    shown_[shownCount_++] = params_[id];
  }
  list_.replaceParams(shown_, shownCount_);
}

int FillDialog::listRow(int id) const {
  for (int r = 0; r < shownCount_; ++r)
    if (shownIds_[r] == id) return r;
  return 0;
}

void FillDialog::clampParams() {
  const int n = rangeLen();
  f_->every = clampu8(f_->every, 1, n);
  f_->offset = clampu8(f_->offset, 0, f_->every - 1);
  f_->length = clampu8(f_->length, 1, n);
  f_->hits = clampu8(f_->hits, 0, f_->length);
  const int m = f_->length - 1;
  f_->rotation = static_cast<int8_t>(clampi(f_->rotation, -m, m));
  f_->density = clampu8(f_->density, 0, 100);
  if (f_->slot >= mt::kFxSlots) f_->slot = 0;
  if (f_->lane >= mt::kKitLanes) f_->lane = 0;
  if (f_->target == mt::FillTarget::Vel) {
    f_->from = clampu8(f_->from, 1, 127);
    f_->to = clampu8(f_->to, 1, 127);
  } else if (f_->target == mt::FillTarget::Note) {
    f_->from = clampu8(f_->from, 0, 127);
    f_->to = clampu8(f_->to, 0, 127);
  }
}

bool FillDialog::open(int pattern, const mt::Sel& sel, mt::FillSpec* f, bool drum) {
  saved_ = app_.undoScratch();
  if (!saved_) return false;
  pattern_ = clampi(pattern, 0, mt::kPatterns - 1);
  const int plen = clampi(app_.project().patterns[pattern_].length, mt::kMinSteps, mt::kMaxSteps);
  sel_ = sel;
  if (sel_.s1 >= plen) sel_.s1 = static_cast<uint8_t>(plen - 1);
  if (sel_.s0 > sel_.s1) sel_.s0 = sel_.s1;
  f_ = f;
  drum_ = drum;
  clampParams();
  // No lock: the engine never writes pattern data.
  *saved_ = app_.project().patterns[pattern_];
  seqAtOpen_ = app_.editSeq();
  previews_ = 0;
  buildRows();
  list_.setEdit(false);
  list_.setSel(listRow(kWhere));
  open_ = true;
  preview();
  return true;
}

void FillDialog::restore() {
  mt::Pattern& pt = app_.project().patterns[pattern_];
  for (int t = sel_.t0; t <= sel_.t1; ++t) memcpy(pt.steps[t], saved_->steps[t], sizeof(pt.steps[t]));
}

void FillDialog::preview() {
  mt::Project& p = app_.project();
  bool drumTr[mt::kTracks];
  for (int t = 0; t < mt::kTracks; ++t) drumTr[t] = p.trackIsDrum(t);
  engine::lockProject();
  restore();
  mt::applyFill(p.patterns[pattern_], sel_, *f_, p.scaleRoot, static_cast<mt::ScaleType>(p.scaleType), drumTr);
  engine::unlockProject();
  app_.markDirty();
  ++previews_;
  engine::post(engine::Cmd::ReleaseTies);  // a held TIE may have lost its step
}

void FillDialog::ok() {
  if (!open_) return;
  open_ = false;
  list_.setEdit(false);
  // The undo snapshot is taken only now (a cancel must not cost the oldest entry of a full ring):
  // the pattern as it was at open, with the range's old tracks swapped in for the copy.
  engine::lockProject();
  mt::Pattern& pt = app_.project().patterns[pattern_];
  for (int t = sel_.t0; t <= sel_.t1; ++t) std::swap_ranges(pt.steps[t], pt.steps[t] + mt::kMaxSteps, saved_->steps[t]);
  app_.pushUndo(static_cast<uint8_t>(pattern_));
  for (int t = sel_.t0; t <= sel_.t1; ++t) std::swap_ranges(pt.steps[t], pt.steps[t] + mt::kMaxSteps, saved_->steps[t]);
  engine::unlockProject();
  app_.toast("FILL");
}

void FillDialog::cancel() {
  if (!open_) return;
  open_ = false;
  list_.setEdit(false);
  engine::lockProject();
  restore();
  engine::unlockProject();
  // Back to the dirty state of open() unless something else was edited meanwhile (e.g. BPM).
  if (app_.editSeq() == seqAtOpen_ + previews_) app_.rewindEditSeq(seqAtOpen_);
  else app_.markDirty();
  engine::post(engine::Cmd::ReleaseTies);
}

void FillDialog::reseed() {
  f_->seed = esp_random();
  preview();
}

bool FillDialog::action(int row) {
  if (row == kOk) ok();
  else if (row == kCancel) cancel();
  else if (row == kReseed) reseed();
  else return false;
  app_.invalidate();
  return true;
}

void FillDialog::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  if (!open_) return;
  switch (ev.type) {
    case InputType::EncLong: cancel(); return;
    case InputType::EncClick:
      if (ev.shift) {
        reseed();
        return;
      }
      if (!list_.editing() && action(rowId(list_.sel()))) return;
      break;
    default: break;
  }
  list_.onInput(ev);
}

void FillDialog::onTouch(const TouchEvent& ev) {
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

void FillDialog::drawButton(LGFX_Sprite& s, int x, int y, int w, const char* label, bool sel) {
  s.fillRect(x, y, w, kBtnH, sel ? kPlayBg : kMenuBg);
  s.drawRect(x, y, w, kBtnH, sel ? kCursor : kDim);
  s.setTextColor(sel ? kCursor : kText);
  s.drawString(label, x + (w - static_cast<int>(strlen(label)) * kCharW) / 2, y + (kBtnH - kCharH) / 2);
}

void FillDialog::draw(LGFX_Sprite& s, int y0) {
  if (!open_) return;
  y0_ = y0;
  char buf[48];
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  if (sel_.t0 == sel_.t1)
    snprintf(buf, sizeof(buf), "FILL T%d %s %d-%d", sel_.t0 + 1, app_.project().tracks[sel_.t0].name, sel_.s0 + 1,
             sel_.s1 + 1);
  else
    snprintf(buf, sizeof(buf), "FILL T%d-%d %d-%d", sel_.t0 + 1, sel_.t1 + 1, sel_.s0 + 1, sel_.s1 + 1);
  s.setTextColor(kText);
  s.drawString(buf, ParamList::kLabelX, y0 + (kHeaderH - 4 - kCharH) / 2);
  drawButton(s, kCancelX, y0 + kBtnY, kCancelW, "CANCEL", rowId(list_.sel()) == kCancel);
  drawButton(s, kOkX, y0 + kBtnY, kOkW, "OK", rowId(list_.sel()) == kOk);
  list_.draw(s, y0 + kHeaderH);
}

}  // namespace ui
