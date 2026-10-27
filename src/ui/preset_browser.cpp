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

// I/O errors may mean the card was pulled: remount so the next listing shows the real state.
void reprobe(storage::Result r) {
  if (r == storage::Result::WriteFail || r == storage::Result::ReadFail) hw::sdRecover();
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
  if (!list_.alloc()) return;
  if (mode == Mode::Load) {
    blockBackup_ = static_cast<mt::Instrument*>(heap_caps_malloc(mt::kKitLanes * sizeof(mt::Instrument), MALLOC_CAP_SPIRAM));
    if (!blockBackup_) {
      list_.free();
      app_.toast(storage::resultText(storage::Result::NoMemory));
      return;
    }
  }
  open_ = true;
  mode_ = mode;
  instr_ = instr;
  backup_ = inst();
  track_ = app_.curTrack();
  trackBackup_ = app_.project().tracks[track_];
  block_ = mt::factoryKitBlock(instr_);
  blockChanged_ = false;
  if (blockBackup_) memcpy(blockBackup_, &app_.project().instruments[block_], mt::kKitLanes * sizeof(mt::Instrument));
  seqBefore_ = seqAfter_ = app_.editSeq();
  changed_ = false;
  restore();
}

void PresetBrowser::close(bool keep) {
  if (!open_) return;
  kb_.close();
  remember();
  if (mode_ == Mode::Load && !keep && changed_) {
    engine::lockProject();
    inst() = backup_;
    mt::TrackCfg& c = app_.project().tracks[track_];
    c.drive = trackBackup_.drive;
    c.crushBits = trackBackup_.crushBits;
    c.crushRate = trackBackup_.crushRate;
    c.driveModel = trackBackup_.driveModel;
    if (blockChanged_)
      memcpy(&app_.project().instruments[block_], blockBackup_, mt::kKitLanes * sizeof(mt::Instrument));
    engine::unlockProject();
    // Nothing else edited since the audition: the project is as clean as before.
    if (app_.editSeq() == seqAfter_) app_.rewindEditSeq(seqBefore_);
    else app_.markDirty();
  }
  list_.free();
  heap_caps_free(blockBackup_);
  blockBackup_ = nullptr;
  open_ = false;
  app_.invalidate();
  if (onClose_) onClose_();
}

PresetBrowser::Place* PresetBrowser::place() {
  // Load and Save, per instrument type; allocated once, kept while powered.
  static constexpr int kTypes = static_cast<int>(mt::InstrType::Count);
  static Place* places = nullptr;
  if (!places) places = static_cast<Place*>(heap_caps_calloc(2 * kTypes, sizeof(Place), MALLOC_CAP_SPIRAM));
  const int t = static_cast<int>(backup_.type);
  if (!places || t < 0 || t >= kTypes) return nullptr;
  return &places[(mode_ == Mode::Save ? kTypes : 0) + t];
}

void PresetBrowser::remember() {
  Place* pl = place();
  if (!pl) return;
  pl->valid = true;
  pl->inFactory = inFactory_;
  pl->type = type_;
  pl->facType = facType_;
  strlcpy(pl->category, category_, sizeof(pl->category));
  strlcpy(pl->dir, dir_, sizeof(pl->dir));
  list_.cursorName(pl->cursor, sizeof(pl->cursor));
}

void PresetBrowser::restore() {
  // Default: Load at the top (type list), Save in the instrument's type root.
  const mt::InstrType saveType = mt::presetTypeHas(backup_.type) ? backup_.type : mt::InstrType::Chip;
  type_ = mode_ == Mode::Load ? mt::InstrType::Count : saveType;
  strlcpy(dir_, type_ == mt::InstrType::Count ? "" : mt::presetRoot(type_), sizeof(dir_));
  inFactory_ = false;
  facType_ = mt::InstrType::Count;
  category_[0] = 0;
  const Place* pl = place();
  if (!pl || !pl->valid || (mode_ == Mode::Save && pl->type != saveType)) {
    list(nullptr);
    return;
  }
  inFactory_ = pl->inFactory;
  type_ = pl->type;
  facType_ = pl->facType;
  strlcpy(category_, pl->category, sizeof(category_));
  strlcpy(dir_, pl->dir, sizeof(dir_));
  // A user folder gone (Wi-Fi, card swap): the type's root.
  if (!inFactory_ && type_ != mt::InstrType::Count && mt::presetDepth(dir_) > 0 &&
      !(hw::sdReady() && hw::sdFs().exists(dir_)))
    strlcpy(dir_, mt::presetRoot(type_), sizeof(dir_));
  list(pl->cursor);
}

void PresetBrowser::add(Kind k, int index, const char* name) {
  const bool preset = k == Kind::File || k == Kind::FactoryFile;
  list_.add(preset ? FileList::Kind::File : FileList::Kind::Dir, name, static_cast<uint8_t>(k), static_cast<int16_t>(index));
}

bool PresetBrowser::atTop() const {
  if (mode_ == Mode::Save) return mt::presetDepth(dir_) <= 0;
  return !inFactory_ && type_ == mt::InstrType::Count;
}

void PresetBrowser::list(const char* cursor) {
  list_.clear();
  picked_ = -1;
  if (inFactory_) {
    if (facType_ == mt::InstrType::Count) {
      for (int k = 0; k < mt::kInstrTypesUi; ++k) {
        const mt::InstrType t = mt::instrTypeAt(k);
        if (mt::factoryCount(t) > 0) add(Kind::FactoryType, static_cast<int>(t), mt::presetTypeName(t));
      }
    } else if (!category_[0]) {
      const int n = mt::factoryCategoryCount(facType_);
      for (int k = 0; k < n; ++k) add(Kind::Category, k, mt::factoryCategory(facType_, k));
    } else {
      for (int i = 0; i < mt::factoryCount(facType_); ++i) {
        const mt::FactoryPreset& f = mt::factoryPreset(facType_, i);
        if (strcmp(f.category, category_) == 0) add(Kind::FactoryFile, i, f.name);
      }
    }
  } else if (type_ == mt::InstrType::Count) {  // Load top: [FACTORY], the types' user folders
    add(Kind::Factory, 0, "[FACTORY]");
    for (int k = 0; k < mt::kInstrTypesUi; ++k) {
      const mt::InstrType t = mt::instrTypeAt(k);
      if (mt::presetTypeHas(t)) add(Kind::UserType, static_cast<int>(t), mt::presetTypeName(t));
    }
  } else if (hw::sdReady()) {
    // Folders, then the .mti files (only names that load back under themselves).
    list_.addFolder(dir_, nullptr, 0, true, static_cast<uint8_t>(Kind::Folder));
    list_.addFiles(dir_, ".mti", storage::validName, static_cast<uint8_t>(Kind::File));
  }
  list_.setBack(atTop());
  list_.color = [this](int i) { return kindOf(i) == Kind::Factory ? kYellow : kText; };
  list_.mark = [this](int i) { return i == picked_; };  // applied now
  list_.finish(cursor);
  app_.invalidate();
}

void PresetBrowser::enter(int i) {
  if (i < 0 || i >= list_.count()) return;
  char left[hw::kNameMax] = {0};  // row to select (the browsed type in [FACTORY])
  switch (kindOf(i)) {
    case Kind::UserType:
      type_ = static_cast<mt::InstrType>(list_.index(i));
      strlcpy(dir_, mt::presetRoot(type_), sizeof(dir_));
      break;
    case Kind::Factory:  // the type list, the browsed type selected
      inFactory_ = true;
      facType_ = mt::InstrType::Count;
      strlcpy(left, mt::presetTypeName(backup_.type), sizeof(left));
      break;
    case Kind::FactoryType: facType_ = static_cast<mt::InstrType>(list_.index(i)); break;
    case Kind::Category: strlcpy(category_, list_.name(i), sizeof(category_)); break;
    case Kind::Folder:
      switch (mt::browseInto(dir_, sizeof(dir_), mt::presetRoot(type_), list_.name(i), mt::kPresetDepthMax)) {
        case mt::BrowseResult::TooDeep: app_.toast("TOO DEEP"); return;
        case mt::BrowseResult::TooLong: app_.toast("PATH TOO LONG"); return;
        default: break;
      }
      break;
    default: return;
  }
  list(left);
}

void PresetBrowser::back() {
  char left[hw::kNameMax] = {0};  // the cursor goes on the folder left
  if (inFactory_ && category_[0]) {
    strlcpy(left, category_, sizeof(left));
    category_[0] = 0;
  } else if (inFactory_ && facType_ != mt::InstrType::Count) {
    strlcpy(left, mt::presetTypeName(facType_), sizeof(left));
    facType_ = mt::InstrType::Count;
  } else if (inFactory_) {
    strlcpy(left, "[FACTORY]", sizeof(left));
    inFactory_ = false;
  } else if (mode_ == Mode::Load && type_ != mt::InstrType::Count && mt::presetDepth(dir_) <= 0) {
    strlcpy(left, mt::presetTypeName(type_), sizeof(left));
    type_ = mt::InstrType::Count;
    dir_[0] = 0;
  } else if (type_ == mt::InstrType::Count || !mt::presetUp(dir_, left, sizeof(left))) {
    close(false);  // the top: Load puts the instrument back
    return;
  }
  list(left);
}

void PresetBrowser::act(FileList::Act a) {
  switch (a) {
    case FileList::Act::Back: back(); break;
    case FileList::Act::Open: choose(); break;
    case FileList::Act::Moved:
      // Live audition: a preset row is applied as soon as it is selected.
      if (mode_ == Mode::Load && isPreset(list_.entry())) pick(list_.entry());
      break;
    default: break;
  }
  app_.invalidate();
}

void PresetBrowser::apply(const mt::Instrument& src) {
  const bool found = mt::projSampleFind(app_.project(), src.sample) >= 0;
  engine::lockProject();
  if (blockChanged_) {  // a kit picked before: its drums go
    memcpy(&app_.project().instruments[block_], blockBackup_, mt::kKitLanes * sizeof(mt::Instrument));
    blockChanged_ = false;
  }
  mt::applyPreset(inst(), src, found);
  mt::takePresetInsert(app_.project(), instr_, track_);  // the track playing it takes its insert
  engine::unlockProject();
  if (inst().type == mt::InstrType::Synth) resolveTables();
  app_.markDirty();
  seqAfter_ = app_.editSeq();
  changed_ = true;
  audio::preview(static_cast<uint8_t>(instr_), kPreviewNote, previewInsert());
}

uint8_t PresetBrowser::previewInsert() const {
  const int t = track_;
  return app_.project().tracks[t].instr == instr_ ? static_cast<uint8_t>(t) : mt::kTracks;
}

void PresetBrowser::applyKit(int index) {
  engine::lockProject();
  // A kit picked before: its drums go, the block is back as on open.
  if (blockChanged_)
    memcpy(&app_.project().instruments[block_], blockBackup_, mt::kKitLanes * sizeof(mt::Instrument));
  mt::factoryBuildKit(index, app_.project(), instr_);
  mt::takePresetInsert(app_.project(), instr_, track_);  // the KIT track takes the kit's insert
  engine::unlockProject();
  blockChanged_ = true;
  app_.markDirty();
  seqAfter_ = app_.editSeq();
  changed_ = true;
  audio::preview(static_cast<uint8_t>(instr_), kPreviewNote, previewInsert());
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
  if (mode_ != Mode::Load || i < 0 || i >= list_.count()) return;
  mt::Instrument m;
  if (kindOf(i) == Kind::FactoryFile && facType_ == mt::InstrType::Kit) {
    applyKit(list_.index(i));
    picked_ = i;
    return;
  }
  if (kindOf(i) == Kind::FactoryFile) {
    mt::factoryBuild(facType_, list_.index(i), m);
  } else if (kindOf(i) == Kind::File) {
    // 84 bytes: no busy overlay, it would flash on every encoder step.
    const storage::Result r = storage::loadPreset(dir_, list_.name(i), m);
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

void PresetBrowser::choose() {
  const int i = list_.entry();
  if (i < 0 || i >= list_.count()) {
    back();
    return;
  }
  if (!isPreset(i)) {
    enter(i);
    return;
  }
  if (mode_ == Mode::Save) {
    saveAs(list_.name(i));  // over this file, after the confirmation
    return;
  }
  if (picked_ != i) pick(i);
  if (picked_ == i) close(true);
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
    list(nm);  // the cursor on the new folder
  });
}

void PresetBrowser::askDelete() {
  const int i = list_.entry();
  if (i < 0 || i >= list_.count() || kindOf(i) != Kind::File) return;
  strlcpy(pending_, list_.name(i), sizeof(pending_));
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
      mt::presetInsertForSave(app_.project(), instr_, track_, m);  // the track's insert, recommended
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
      const int keep = list_.sel();
      list(nullptr);
      list_.select(keep);
      break;
    }
    default: break;
  }
  app_.invalidate();
}

void PresetBrowser::button(int b) {
  if (mode_ == Mode::Load) {
    switch (b) {
      case 0:
        // OK on a preset row not auditioned yet takes it first.
        if (isPreset(list_.entry()) && picked_ != list_.entry()) choose();
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
  if (kb_.isOpen()) {
    kb_.onInput(ev);
    return;
  }
  act(list_.onInput(ev));
}

void PresetBrowser::onTouch(const TouchEvent& ev) {
  if (kb_.isOpen()) {
    kb_.onTouch(ev, app_.shift());
    return;
  }
  const int top = y0_ + kHeaderH;
  if (ev.type == TouchType::Tap && ev.y < top) {
    for (int b = 0; b < 3; ++b)
      if (ev.x >= kBtnX[b] && ev.x < kBtnX1[b]) button(b);
    return;
  }
  // A preset's first tap auditions it (Moved), a tap on it again keeps it; folders open at once.
  act(list_.onTouch(ev, top));
}

void PresetBrowser::drawHeader(LGFX_Sprite& s, int y0) {
  // Load: the whole path from the top ("/", "/SYNTH/MINE", "[FACTORY]/KIT/DRUM"). Save: the type,
  // then the path below its root.
  char title[16];
  char path[mt::kPresetDirMax + 16];
  if (mode_ == Mode::Save) {
    snprintf(title, sizeof(title), "SAVE %s", mt::presetTypeName(type_));
    snprintf(path, sizeof(path), "%s", mt::presetSubPath(dir_)[0] ? mt::presetSubPath(dir_) : "/");
  } else {
    strlcpy(title, "LOAD", sizeof(title));
    if (inFactory_) {
      snprintf(path, sizeof(path), "[FACTORY]%s%s%s%s", facType_ != mt::InstrType::Count ? "/" : "",
               facType_ != mt::InstrType::Count ? mt::presetTypeName(facType_) : "", category_[0] ? "/" : "",
               category_);
    } else if (type_ == mt::InstrType::Count) {
      snprintf(path, sizeof(path), "/");
    } else {
      snprintf(path, sizeof(path), "/%s%s", mt::presetTypeName(type_), mt::presetSubPath(dir_));
    }
  }
  FileList::drawHeader(s, y0, title, path, kBtnX[0] - 8);  // SD status before the buttons
  const int ty = y0 + (kHeaderH - 4 - kCharH) / 2;
  const char* const* labels = mode_ == Mode::Load ? kLoadBtns : kSaveBtns;
  const int i = list_.entry();
  const bool delOk = i >= 0 && i < list_.count() && kindOf(i) == Kind::File;
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
  list_.draw(s, y0 + kHeaderH);
}

}  // namespace ui
