#include "preset_browser.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "app.h"
#include "audio/audio.h"
#include "esp_heap_caps.h"
#include "file_rules.h"
#include "preset_io.h"
#include "presets_factory.h"
#include "sample_set.h"
#include "storage/presets.h"
#include "wt_builtin.h"
#include "wt_picker.h"

namespace ui {
namespace {

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// I/O errors may mean the card was pulled: remount so the next listing shows the real state.
void reprobe(storage::Result r) {
  if (r == storage::Result::WriteFail || r == storage::Result::ReadFail) hw::sdBegin();
}

constexpr const char* kLoadBtns[3] = {"OK", "DEL", "CANCEL"};
constexpr const char* kSaveBtns[3] = {"SAVE", "+DIR", "CANCEL"};

}  // namespace

mt::Instrument& PresetBrowser::inst() { return app_.project().instruments[instr_]; }

void PresetBrowser::open(Mode mode, int instr) {
  if (open_) close(true);
  if (mode == Mode::Save && !hw::sdReady()) {
    app_.toast(storage::resultText(storage::Result::NoSd));
    return;
  }
  names_ = static_cast<char(*)[hw::kNameMax]>(heap_caps_malloc(kMaxEntries * hw::kNameMax, MALLOC_CAP_SPIRAM));
  if (!names_) {
    app_.toast(storage::resultText(storage::Result::NoMemory));
    return;
  }
  open_ = true;
  mode_ = mode;
  instr_ = instr;
  backup_ = inst();
  seqBefore_ = seqAfter_ = app_.editSeq();
  changed_ = false;
  type_ = mt::presetTypeHas(backup_.type) ? backup_.type : mt::InstrType::Chip;
  const char* last = lastDir_[static_cast<int>(type_)];
  strlcpy(dir_, last[0] && hw::sdReady() && hw::sdFs().exists(last) ? last : mt::presetRoot(type_), sizeof(dir_));
  inFactory_ = false;
  category_[0] = 0;
  list();
}

void PresetBrowser::close(bool keep) {
  if (!open_) return;
  kb_.close();
  if (mode_ == Mode::Load && !keep && changed_) {
    engine::lockProject();
    inst() = backup_;
    engine::unlockProject();
    // Nothing else edited since the audition: the project is as clean as before.
    if (app_.editSeq() == seqAfter_) app_.rewindEditSeq(seqBefore_);
    else app_.markDirty();
  }
  heap_caps_free(names_);
  names_ = nullptr;
  count_ = 0;
  open_ = false;
  app_.invalidate();
  if (onClose_) onClose_();
}

void PresetBrowser::add(Kind k, int index, const char* name) {
  if (count_ >= kMaxEntries) return;
  entries_[count_] = {k, static_cast<int16_t>(index)};
  strlcpy(names_[count_], name, hw::kNameMax);
  ++count_;
}

void PresetBrowser::list() {
  count_ = 0;
  picked_ = -1;
  if (inFactory_) {
    add(Kind::Up, 0, "..");
    if (!category_[0]) {
      const int n = mt::factoryCategoryCount(type_);
      for (int k = 0; k < n; ++k) add(Kind::Category, k, mt::factoryCategory(type_, k));
    } else {
      for (int i = 0; i < mt::factoryCount(type_); ++i) {
        const mt::FactoryPreset& f = mt::factoryPreset(type_, i);
        if (strcmp(f.category, category_) == 0) add(Kind::FactoryFile, i, f.name);
      }
    }
  } else {
    const bool root = mt::presetDepth(dir_) <= 0;
    if (!root) add(Kind::Up, 0, "..");
    if (root && mode_ == Mode::Load && mt::factoryCount(type_) > 0) add(Kind::Factory, 0, "[FACTORY]");
    if (hw::sdReady()) {
      // Straight into names_, kinds after.
      int from = count_;
      count_ += hw::sdListDirs(dir_, names_ + count_, kMaxEntries - count_);
      for (int i = from; i < count_; ++i) entries_[i] = {Kind::Folder, 0};
      from = count_;
      count_ += hw::sdList(dir_, ".mti", names_ + count_, kMaxEntries - count_, storage::validName);
      for (int i = from; i < count_; ++i) entries_[i] = {Kind::File, 0};
    }
    strlcpy(lastDir_[static_cast<int>(type_)], dir_, sizeof(lastDir_[0]));
  }
  sel_ = count_ > 1 && entries_[0].kind == Kind::Up ? 1 : 0;
  top_ = 0;
  dragAcc_ = 0;
  app_.invalidate();
}

void PresetBrowser::enter(int i) {
  if (i < 0 || i >= count_) return;
  char left[hw::kNameMax] = {0};  // row to select after going up
  switch (entries_[i].kind) {
    case Kind::Up:
      if (inFactory_ && category_[0]) {
        strlcpy(left, category_, sizeof(left));
        category_[0] = 0;
      } else if (inFactory_) {
        strlcpy(left, "[FACTORY]", sizeof(left));
        inFactory_ = false;
      } else if (!mt::presetUp(dir_, left, sizeof(left))) {
        return;
      }
      break;
    case Kind::Factory: inFactory_ = true; break;
    case Kind::Category: strlcpy(category_, names_[i], sizeof(category_)); break;
    case Kind::Folder: {
      if (mt::presetDepth(dir_) >= mt::kPresetDepthMax) {
        app_.toast("TOO DEEP");
        return;
      }
      char sub[mt::kPresetDirMax];
      if (!mt::presetJoin(sub, sizeof(sub), dir_, names_[i])) {
        app_.toast("PATH TOO LONG");
        return;
      }
      strlcpy(dir_, sub, sizeof(dir_));
      break;
    }
    default: return;
  }
  list();
  if (!left[0]) return;
  for (int k = 0; k < count_; ++k)
    if (entries_[k].kind != Kind::Up && strcasecmp(names_[k], left) == 0) {
      sel_ = k;
      if (sel_ >= top_ + kRows) top_ = sel_ - kRows + 1;
      break;
    }
}

void PresetBrowser::apply(const mt::Instrument& src) {
  const bool found = mt::projSampleFind(app_.project(), src.sample) >= 0;
  engine::lockProject();
  mt::applyPreset(inst(), src, found);
  engine::unlockProject();
  if (inst().type == mt::InstrType::Synth) resolveTables();
  app_.markDirty();
  seqAfter_ = app_.editSeq();
  changed_ = true;
  audio::preview(static_cast<uint8_t>(instr_), kPreviewNote);
}

void PresetBrowser::resolveTables() {
  // A table neither built in nor in the project: /wavetables/<name>.wav, while stopped. Not found,
  // the name stays (INST shows it red, the oscillator is silent).
  const engine::Status& st = app_.status();
  if (st.playing || st.paused || !hw::sdReady()) return;
  for (int k = 0; k < 2; ++k) {
    const char* name = inst().synWt[k];
    if (!name[0] || mt::isWtBuiltin(name) || mt::projWtFind(app_.project(), name) >= 0) continue;
    if (!mt::projectBaseValid(name)) continue;  // a file name only, no path
    char path[32 + mt::kSampleNameMax];
    snprintf(path, sizeof(path), "/wavetables/%s.wav", name);
    if (!hw::sdFs().exists(path)) continue;
    char nm[mt::kSampleNameMax + 1];
    if (!wtImportFile(app_, path, name, nm)) continue;
    if (strcmp(nm, name) != 0) {
      engine::lockProject();
      strlcpy(inst().synWt[k], nm, sizeof(inst().synWt[k]));
      engine::unlockProject();
    }
  }
}

void PresetBrowser::pick(int i) {
  if (mode_ != Mode::Load || i < 0 || i >= count_) return;
  mt::Instrument m;
  if (entries_[i].kind == Kind::FactoryFile) {
    mt::factoryBuild(type_, entries_[i].index, m);
  } else if (entries_[i].kind == Kind::File) {
    // 84 bytes: no busy overlay, it would flash on every encoder step.
    const storage::Result r = storage::loadPreset(dir_, names_[i], m);
    if (r != storage::Result::Ok) {
      reprobe(r);
      app_.toast(storage::resultText(r));
      return;
    }
  } else {
    return;
  }
  apply(m);
  picked_ = i;
}

void PresetBrowser::choose(int i) {
  if (i < 0 || i >= count_) return;
  sel_ = i;
  const Kind k = entries_[i].kind;
  if (k != Kind::File && k != Kind::FactoryFile) {
    enter(i);
    return;
  }
  if (mode_ == Mode::Save) {
    saveAs(names_[i]);  // over this file, after the confirmation
    return;
  }
  if (picked_ != i) pick(i);
  if (picked_ == i) close(true);
}

void PresetBrowser::moveSel(int d) {
  if (count_ == 0) return;
  const int prev = sel_;
  sel_ = clampi(sel_ + d, 0, count_ - 1);
  if (sel_ < top_) top_ = sel_;
  if (sel_ >= top_ + kRows) top_ = sel_ - kRows + 1;
  // Live audition: a preset row is applied as soon as it is selected.
  const Kind k = entries_[sel_].kind;
  if (sel_ != prev && (k == Kind::File || k == Kind::FactoryFile)) pick(sel_);
}

void PresetBrowser::askName() {
  char initial[17];
  storage::sanitize(inst().name, initial);
  kb_.open("PRESET NAME:", initial, [this](const char* text) {
    char nm[17];
    if (!storage::sanitize(text, nm)) {
      app_.toast("BAD NAME");
      return;
    }
    saveAs(nm);
  });
}

void PresetBrowser::saveAs(const char* name) {
  strlcpy(pending_, name, sizeof(pending_));
  if (storage::presetExists(dir_, pending_)) {
    const MenuItem items[] = {{"Cancel", kCancel}, {"Overwrite", kOverwrite}};
    char title[28];
    snprintf(title, sizeof(title), "%s EXISTS", pending_);
    app_.menu().open(title, items, 2, [this](int id) { onMenu(id); });
    return;
  }
  onMenu(kOverwrite);
}

void PresetBrowser::askFolder() {
  if (mt::presetDepth(dir_) >= mt::kPresetDepthMax) {
    app_.toast("TOO DEEP");
    return;
  }
  kb_.open("FOLDER NAME:", "", [this](const char* text) {
    char nm[17];
    if (!storage::sanitize(text, nm)) {
      app_.toast("BAD NAME");
      return;
    }
    app_.showBusy("SAVING...");
    const storage::Result r = storage::makePresetDir(dir_, nm);
    if (r != storage::Result::Ok) {
      reprobe(r);
      app_.toast(storage::resultText(r));
      return;
    }
    list();
    for (int k = 0; k < count_; ++k)
      if (entries_[k].kind == Kind::Folder && strcasecmp(names_[k], nm) == 0) {
        sel_ = k;
        if (sel_ >= top_ + kRows) top_ = sel_ - kRows + 1;
        break;
      }
  });
}

void PresetBrowser::askDelete() {
  if (sel_ >= count_ || entries_[sel_].kind != Kind::File) return;
  strlcpy(pending_, names_[sel_], sizeof(pending_));
  const MenuItem items[] = {{"Cancel", kCancel}, {"Delete", kDelete}};
  char title[28];
  snprintf(title, sizeof(title), "DELETE %s?", pending_);
  app_.menu().open(title, items, 2, [this](int id) { onMenu(id); });
}

void PresetBrowser::onMenu(int id) {
  if (!open_) return;
  switch (id) {
    case kOverwrite: {
      // The file name may be longer than an instrument's: the preset keeps its first 8 chars.
      mt::Instrument m = inst();
      strlcpy(m.name, pending_, sizeof(m.name));
      app_.showBusy("SAVING...");
      const storage::Result r = storage::savePreset(dir_, pending_, m);
      if (r != storage::Result::Ok) {
        reprobe(r);
        app_.toast(storage::resultText(r));
        break;
      }
      if (strcmp(inst().name, m.name) != 0) {
        engine::lockProject();
        memcpy(inst().name, m.name, sizeof(m.name));
        engine::unlockProject();
        app_.markDirty();
      }
      char msg[32];
      snprintf(msg, sizeof(msg), "SAVED %s", pending_);
      app_.toast(msg);
      close(true);
      break;
    }
    case kDelete: {
      app_.showBusy("DELETING...");
      const storage::Result r = storage::removePreset(dir_, pending_);
      if (r != storage::Result::Ok) {
        reprobe(r);
        app_.toast(storage::resultText(r));
      }
      const int keep = sel_;
      list();
      sel_ = clampi(keep, 0, count_ > 0 ? count_ - 1 : 0);
      if (sel_ >= top_ + kRows) top_ = sel_ - kRows + 1;
      break;
    }
    default: break;
  }
  app_.invalidate();
}

void PresetBrowser::switchType(int d) {
  if (mode_ != Mode::Load) return;
  constexpr int kTypes = static_cast<int>(mt::InstrType::Count);
  int pos = mt::instrTypePos(type_);
  do {  // types without presets (KIT) are skipped
    pos = ((pos + d) % kTypes + kTypes) % kTypes;
  } while (!mt::presetTypeHas(mt::instrTypeAt(pos)));
  type_ = mt::instrTypeAt(pos);
  const char* last = lastDir_[static_cast<int>(type_)];
  strlcpy(dir_, last[0] && hw::sdReady() && hw::sdFs().exists(last) ? last : mt::presetRoot(type_), sizeof(dir_));
  inFactory_ = false;
  category_[0] = 0;
  list();
}

void PresetBrowser::button(int b) {
  if (mode_ == Mode::Load) {
    switch (b) {
      case 0:
        // OK on a preset row not auditioned yet takes it first.
        if (sel_ < count_ && picked_ != sel_ &&
            (entries_[sel_].kind == Kind::File || entries_[sel_].kind == Kind::FactoryFile))
          choose(sel_);
        else close(true);
        break;
      case 1: askDelete(); break;
      default: close(false); break;
    }
    return;
  }
  switch (b) {
    case 0: askName(); break;
    case 1: askFolder(); break;
    default: close(false); break;
  }
}

void PresetBrowser::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  if (kb_.isOpen()) {
    kb_.onInput(ev);
    return;
  }
  switch (ev.type) {
    case InputType::EncTurn:
      if (ev.shift) switchType(ev.delta);
      else moveSel(ev.delta);
      break;
    case InputType::EncClick: choose(sel_); break;
    case InputType::EncLong: close(false); break;
    default: break;
  }
}

void PresetBrowser::onTouch(const TouchEvent& ev) {
  if (kb_.isOpen()) {
    kb_.onTouch(ev, app_.shift());
    return;
  }
  const int top = y0_ + kHeaderH;
  if (ev.type == TouchType::Drag) {
    dragAcc_ += ev.dy;
    const int rows = dragAcc_ / kRowH;
    dragAcc_ -= rows * kRowH;
    top_ = clampi(top_ - rows, 0, count_ > kRows ? count_ - kRows : 0);
    return;
  }
  if (ev.type != TouchType::Tap) return;
  if (ev.y < top) {
    if (mode_ == Mode::Load && ev.x < kLeftX1) switchType(-1);
    else if (mode_ == Mode::Load && ev.x >= kRightX0 && ev.x < kRightX1) switchType(1);
    for (int b = 0; b < 3; ++b)
      if (ev.x >= kBtnX[b] && ev.x < kBtnX1[b]) button(b);
    return;
  }
  const int i = top_ + (ev.y - top) / kRowH;
  if (i >= count_) return;
  const Kind k = entries_[i].kind;
  // Load: a tap auditions a preset (OK keeps it); folders open.
  if (mode_ == Mode::Load && (k == Kind::File || k == Kind::FactoryFile)) {
    sel_ = i;
    pick(i);
    return;
  }
  choose(i);
}

void PresetBrowser::drawHeader(LGFX_Sprite& s, int y0) {
  const int cy = y0 + kHeaderH / 2;
  const int ty = y0 + (kHeaderH - 4 - kCharH) / 2;
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  const char* tn = mt::presetTypeName(type_);
  s.setTextColor(kText);
  if (mode_ == Mode::Load) {
    s.fillTriangle(14, cy - 2, 26, cy - 9, 26, cy + 5, kCursor);
    s.fillTriangle(kRightX0 + 26, cy - 2, kRightX0 + 14, cy - 9, kRightX0 + 14, cy + 5, kCursor);
    const int mid = (kLeftX1 + kRightX0) / 2;
    s.drawString(tn, mid - static_cast<int>(strlen(tn)) * kCharW / 2, ty);
  } else {
    char buf[16];
    snprintf(buf, sizeof(buf), "SAVE %s", tn);
    s.drawString(buf, 8, ty);
  }
  // Path below the type's root, its tail when long.
  char path[mt::kPresetDirMax + 16];
  if (inFactory_) snprintf(path, sizeof(path), "[FACTORY]%s%s", category_[0] ? "/" : "", category_);
  else snprintf(path, sizeof(path), "%s", mt::presetSubPath(dir_)[0] ? mt::presetSubPath(dir_) : "/");
  constexpr int kFit = (kBtnX[0] - 8 - kPathX) / kCharW;
  const int len = static_cast<int>(strlen(path));
  char shown[kFit + 1];
  snprintf(shown, sizeof(shown), "%s%s", len > kFit ? "..." : "", path + (len > kFit ? len - kFit + 3 : 0));
  s.setTextColor(kDim);
  s.drawString(shown, kPathX, ty);
  const char* const* labels = mode_ == Mode::Load ? kLoadBtns : kSaveBtns;
  const bool delOk = sel_ < count_ && entries_[sel_].kind == Kind::File;
  for (int b = 0; b < 3; ++b) {
    const int w = kBtnX1[b] - kBtnX[b];
    s.fillRect(kBtnX[b], y0 + 2, w, kHeaderH - 8, kPlayBg);
    s.setTextColor(mode_ == Mode::Load && b == 1 && !delOk ? kDim : kCursor);
    s.drawString(labels[b], kBtnX[b] + (w - static_cast<int>(strlen(labels[b])) * kCharW) / 2, ty);
  }
}

void PresetBrowser::draw(LGFX_Sprite& s, int y0) {
  y0_ = y0;
  if (kb_.isOpen()) {
    kb_.draw(s, y0);
    return;
  }
  drawHeader(s, y0);
  const int top = y0 + kHeaderH;
  for (int r = 0; r < kRows; ++r) {
    const int i = top_ + r;
    if (i >= count_) break;
    const int ry = top + r * kRowH;
    const bool sel = i == sel_;
    const Kind k = entries_[i].kind;
    if (sel) s.fillRect(0, ry, kScreenW, kRowH, kSelBg);
    uint16_t c = k == Kind::Up ? kDim : (k == Kind::Factory ? kYellow : kText);
    if (sel) c = kCursor;
    s.setTextColor(c);
    const int ty = ry + (kRowH - kCharH) / 2;
    if (i == picked_) s.drawString(">", 4, ty);  // applied now
    s.drawString(names_[i], 16, ty);
    if (k == Kind::Folder || k == Kind::Category)
      s.drawString("/", 16 + static_cast<int>(strlen(names_[i])) * kCharW, ty);
  }
  s.setTextColor(kDim);
  if (count_ == 0) s.drawString(hw::sdReady() ? "EMPTY" : "NO SD CARD", 16, top + (kRowH - kCharH) / 2);
  if (top_ > 0) s.drawString("^", kScreenW - 24, top + 4);
  if (top_ + kRows < count_) s.drawString("v", kScreenW - 24, top + (kRows - 1) * kRowH + 4);
}

}  // namespace ui
