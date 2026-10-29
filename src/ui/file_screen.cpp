#include "file_screen.h"
#include "file_rules.h"
#include "engine/engine.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "app.h"
#include "audio/audio.h"
#include "link/bank_client.h"
#include "esp_heap_caps.h"
#include "hw/battery.h"
#include "hw/ble_midi.h"
#include "link/link.h"
#include "link/synth_fw.h"
#include "sample_set.h"
#include "stream_info.h"
#include "synth.h"
#include "demos.h"
#include "templates.h"
#include "storage/crashlog.h"
#include "storage/remote_fs.h"
#include "storage/storage.h"

namespace ui {
namespace {

constexpr const char* kLabels[] = {"Save",           "Save As...",        "Load...", "New",
                                   "Import MIDI...", "Export...", "Retry"};
// The last row: Retry without a card, Restore autosave with one (they never need the row together).
constexpr const char* kRestoreLabel = "Restore autosave";

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// I/O errors may mean the card was pulled: remount so the screen shows the real state.
void reprobe(storage::Result r) {
  if (r == storage::Result::WriteFail || r == storage::Result::ReadFail) hw::sdRecover();
}

}  // namespace

void FileScreen::onEnter() {
  kb_.close();
  import_.close();
  render_.close();
  wifi_.close();
  midiDlg_.close();
  closeList();
  autoAvail_ = storage::autosaveExists(app_.project().name);
  if (!enabled(sel_)) moveSel(1);
  audio::bankViewStale();
  midiDlg_.projectReplaced();  // a KNOBS edit of the old project's bind
  if (section_ == kSecSamples) sampleMove(0);  // the bank may have changed
  sysList_.setEdit(false);
}

void FileScreen::onProjectReplaced() {
  audio::bankViewStale();
  if (section_ == kSecSamples) sampleMove(0);  // another sample list
}

void FileScreen::onLeave() {
  kb_.close();
  import_.close();  // frees the file / note buffers
  render_.close();
  wifi_.close();    // leaving the tab ends Wi-Fi mode
  midiDlg_.close();
  closeList();
}

bool FileScreen::enabled(int a) const {
  const bool sd = hw::sdReady();
  switch (a) {
    case kSectionSel: return true;
    case kMidi:
    case kWifi: return true;  // connections do not need an SD card
    case kRetry: return !sd || autoAvail_;
    default: return sd;
  }
}

void FileScreen::moveSel(int delta) {
  const int dir = delta >= 0 ? 1 : -1;
  int n = delta == 0 ? 1 : (delta > 0 ? delta : -delta);
  int i = sel_;
  constexpr int kPos = kActions + 1;  // the actions and the section switch
  for (int guard = 0; guard < kPos * 2 && n > 0; ++guard) {
    i = (i + dir + kPos) % kPos;
    if (enabled(i)) {
      sel_ = i;
      --n;
    }
  }
}

void FileScreen::run(int a) {
  if (!enabled(a)) return;
  if (a >= 0 && a < kActions) sel_ = a;
  switch (a) {
    case kSectionSel: return;  // switched by A + turn / a tap on the page bar
    case kSave: save(); break;
    case kSaveAs: {
      const char* cur = app_.project().name;
      saveAs(strcmp(cur, "untitled") == 0 ? "" : cur);
      break;
    }
    case kLoad: openList(false); break;
    case kImport: openList(true); break;
    case kRender: render_.open(); break;
    case kMidi: midiDlg_.open(); break;
    case kNew: openNewMenu(); break;
      break;
    case kWifi:
      if (app_.projectDirty()) {
        // A firmware update reboots the device: offer to save first.
        const bool named = strcmp(app_.project().name, "untitled") != 0;
        const MenuItem items[] = {
            {"Cancel", kCancel}, {"Save & continue", kSaveWifi, named}, {"Continue w/o saving", kDiscardWifi}};
        app_.menu().open("UNSAVED CHANGES", items, 3, [this](int id) { onMenu(id); });
      } else {
        startWifi();
      }
      break;
    case kRetry:
      if (hw::sdReady()) {  // Restore autosave
        const MenuItem items[] = {{"Cancel", kCancel}, {"Restore", kRestoreAuto}};
        app_.menu().open(app_.projectDirty() ? "RESTORE? CHANGES LOST" : "RESTORE AUTOSAVE?", items, 2,
                         [this](int id) { onMenu(id); });
        break;
      }
      app_.toast(hw::sdBegin() ? "SD OK" : storage::resultText(storage::Result::NoSd));
      autoAvail_ = storage::autosaveExists(app_.project().name);
      if (!enabled(sel_)) moveSel(1);
      break;
    default: break;
  }
}

void FileScreen::onMenu(int id) {
  switch (id) {
    case kDiscardLoad: doLoad(false); break;
    case kLoadBak: doLoad(true); break;
    case kDiscardNew: doNew(); break;
    case kRestoreAuto: restoreAutosave(); break;
    case kOverwrite: doSave(pending_); break;
    case kSaveWifi:
      doSave(app_.project().name);
      if (!app_.projectDirty()) startWifi();
      break;
    case kDiscardWifi: startWifi(); break;
    case kOverwriteSample: doImport(pending_); break;
    case kRenameSample: {
      // Its file is found by name: renamed, it would be lost from the folder at the next save.
      const mt::Project& p = app_.project();
      const audio::BankView& v = audio::bankView();
      if (v.mounted && !v.sampleCached(mt::projSampleFind(p, pending_))) {
        app_.toast("SAMPLE MISSING");
        break;
      }
      renameSample(pending_);
      break;
    }
    case kDeleteSample: {
      const int ins = usedBy(pending_);
      if (ins < 0) {
        doDelete(pending_);
        break;
      }
      const MenuItem items[] = {{"Cancel", kCancel}, {"Delete", kDeleteUsed}};
      char title[28];
      snprintf(title, sizeof(title), "USED BY INS%d. DELETE?", ins + 1);
      app_.menu().openChild(title, items, 2, [this](int id) { onMenu(id); });
      break;
    }
    case kDeleteUsed: doDelete(pending_); break;
    case kClearCache:
      if (!playbackBusy()) doClear();
      break;
    default: break;
  }
  app_.invalidate();
}

void FileScreen::startWifi() {
  midiDlg_.close();  // the synth version screen can open Wi-Fi over it
  wifi_.open();
}

void FileScreen::save() {
  const char* name = app_.project().name;
  if (!name[0] || strcmp(name, "untitled") == 0) saveAs("");
  else doSave(name);
}

void FileScreen::saveAs(const char* initial) {
  kb_.open("NAME:", initial, [this](const char* text) {
    char nm[17];
    if (!storage::sanitize(text, nm)) {
      app_.toast("BAD NAME");
      saveAs(text);  // keep editing what was typed (text is a copy owned by the keyboard)
      return;
    }
    // FAT names are case-insensitive: "Song" and "SONG" are the same file.
    if (strcasecmp(nm, app_.project().name) != 0 && storage::exists(nm)) {
      strlcpy(pending_, nm, sizeof(pending_));
      const MenuItem items[] = {{"Cancel", kCancel}, {"Overwrite", kOverwrite}};
      char title[28];
      snprintf(title, sizeof(title), "%s EXISTS", nm);
      app_.menu().openChild(title, items, 2, [this](int id) { onMenu(id); });
      return;
    }
    doSave(nm);
  });
}

void FileScreen::doSave(const char* name) {
  struct Refresh {
    FileScreen& f;
    ~Refresh() { f.autoAvail_ = storage::autosaveExists(f.app_.project().name); }
  } refresh{*this};
  char nm[17];
  strlcpy(nm, name, sizeof(nm));  // name may point into the project
  app_.showBusy("SAVING...");
  const storage::Result r = storage::save(app_.project(), nm, App::syncProgress, &app_);
  if (r == storage::Result::SamplesNotSaved) {
    // The project file is saved, the folder is not complete: stays dirty, so the next save retries.
    reprobe(storage::Result::WriteFail);
    app_.toast(storage::resultText(r));
    return;
  }
  if (r != storage::Result::Ok) {
    reprobe(r);
    app_.toast(storage::resultText(r));
    return;
  }
  app_.markSaved();
  char msg[32];
  snprintf(msg, sizeof(msg), "SAVED %s", nm);
  app_.toast(msg);
}

void FileScreen::openList(bool midi) {
  if (!list_.alloc()) return;
  midiList_ = midi;
  wavList_ = false;
  // The folder may have been removed (Wi-Fi, card swap): start over at /midi.
  if (midi && !atRoot() && !(hw::sdReady() && hw::sdFs().exists(midiDir_))) strlcpy(midiDir_, "/midi", sizeof(midiDir_));
  listDir(curCursor());
}

bool FileScreen::atRoot() const { return strcmp(curDir(), rootDir()) == 0; }

void FileScreen::listDir(const char* cursor) {
  // Projects: only names that load back unchanged; others would be renamed by sanitize().
  // MIDI / WAV: subfolders first, then .mid / .midi / .wav with the extension (any case).
  static constexpr const char* kMidiExts[] = {".mid", ".midi"};
  static constexpr const char* kWavExts[] = {".wav"};
  list_.clear();
  if (wavList_) list_.addFolder(wavDir_, kWavExts, 1);
  else if (midiList_) list_.addFolder(midiDir_, kMidiExts, 2);
  else list_.addFiles("/projects", ".mtp", storage::validName);
  const bool top = !browsing() || atRoot();
  list_.setBack(top);
  list_.setPreviewDir(wavList_ ? wavDir_ : nullptr);
  list_.finish(cursor);
  // A toast at the top only; a subfolder shows EMPTY FOLDER in the list.
  if (list_.count() == 0 && top && hw::sdReady())
    app_.toast(wavList_ ? "NO /samples/*.wav" : (midiList_ ? "NO /midi FILES" : "NO PROJECTS"));
}

bool FileScreen::listUp() {
  char left[hw::kNameMax];
  if (!browsing() || !mt::browseUp(curDir(), rootDir(), left, sizeof(left))) return false;
  listDir(left);  // the cursor on the folder we came from
  return true;
}

void FileScreen::closeList() {
  if (list_.ready()) list_.cursorName(curCursor(), hw::kNameMax);
  list_.free();
  wavList_ = false;
  midiList_ = false;
}

void FileScreen::listAct(FileList::Act a) {
  if (a == FileList::Act::Back) {
    list_.previewStop();
    if (!listUp()) closeList();  // a subfolder: one level up; the top: the list closes
  } else if (a == FileList::Act::Open) {
    listOpen();
  }
}

void FileScreen::listOpen() {
  list_.previewStop();
  const int idx = list_.entry();
  if (idx < 0 || idx >= list_.count()) {
    listAct(FileList::Act::Back);
    return;
  }
  const char* nm = list_.name(idx);
  if (list_.kind(idx) == FileList::Kind::Dir) {
    switch (mt::browseInto(curDir(), curDirCap(), rootDir(), nm, FileList::kDepthMax)) {
      case mt::BrowseResult::TooDeep: app_.toast("TOO DEEP"); return;
      case mt::BrowseResult::TooLong: app_.toast("PATH TOO LONG"); return;
      default: listDir(nullptr); return;
    }
  }
  if (wavList_) {
    strlcpy(wavFile_, nm, sizeof(wavFile_));
    closeList();
    // Default sample name: the file name without the extension, cut to the bank's rules.
    char base[hw::kNameMax];
    strlcpy(base, wavFile_, sizeof(base));
    if (char* dot = strrchr(base, '.')) *dot = 0;
    char sn[17];
    storage::sanitize(base, sn);
    importAs(sn);
    return;
  }
  if (midiList_) {
    // Keep the list on failure so another file can be picked.
    if (import_.open(midiDir_, nm)) closeList();
    else if (!hw::sdReady()) closeList();
    return;
  }
  strlcpy(pending_, nm, sizeof(pending_));
  if (app_.projectDirty()) {
    const MenuItem items[] = {{"Cancel", kCancel}, {"Discard & load", kDiscardLoad}};
    app_.menu().openChild("DISCARD CHANGES?", items, 2, [this](int id) { onMenu(id); });
  } else {
    doLoad(false);
  }
}

void FileScreen::restoreAutosave() {
  app_.showBusy("LOADING...");
  int missing = 0;
  const storage::Result r = storage::loadAutosave(app_.project(), &missing, App::syncProgress, &app_);
  autoAvail_ = storage::autosaveExists(app_.project().name);
  if (r != storage::Result::Ok && r != storage::Result::SamplesNotSaved) {
    reprobe(r);
    app_.toast(storage::resultText(r));
    return;
  }
  app_.projectReplaced();
  app_.markDirty();  // the autosave is not the saved project: Save keeps it
  app_.loadedToast("AUTOSAVE RESTORED", missing, r == storage::Result::SamplesNotSaved);
}

// The clipboard is kept across Load / New on purpose: it copies patterns between projects.
void FileScreen::doLoad(bool bak) {
  struct Refresh {
    FileScreen& f;
    ~Refresh() { f.autoAvail_ = storage::autosaveExists(f.app_.project().name); }
  } refresh{*this};
  app_.showBusy("LOADING...");
  int missing = 0;
  const storage::Result r = storage::load(app_.project(), pending_, bak, &missing, App::syncProgress, &app_);
  if (r == storage::Result::Ok || r == storage::Result::SamplesNotSaved) {
    closeList();
    app_.projectReplaced();
    char msg[32];
    snprintf(msg, sizeof(msg), bak ? "LOADED %s.bak" : "LOADED %s", app_.project().name);
    app_.loadedToast(msg, missing, r == storage::Result::SamplesNotSaved);
    return;
  }
  reprobe(r);
  using storage::Result;
  const bool fileProblem = r != Result::BadVersion && r != Result::NoSd && r != Result::NoMemory &&
                           r != Result::EngineBusy;
  if (fileProblem && !bak && storage::exists(pending_, true)) {
    const MenuItem items[] = {{"Cancel", kCancel}, {"Load backup", kLoadBak}};
    app_.menu().openChild(storage::resultText(r), items, 2, [this](int id) { onMenu(id); });
    return;
  }
  if (!hw::sdReady()) closeList();
  app_.toast(storage::resultText(r));
}

// New: the built-in templates, the user ones (/templates), the demo songs and Save as template.
void FileScreen::openNewMenu() {
  MenuItem items[Menu::kMaxItems];
  int n = 0;
  for (int i = 0; i < mt::templateCount() && n < Menu::kMaxItems - 1; ++i) items[n++] = {mt::templateName(i), i};
  userTpl_ = 0;
  if (hw::sdReady()) {
    char names[kUserTpl][hw::kNameMax];
    const int found = hw::sdList(storage::kTemplateDir, ".mtp", names, kUserTpl, storage::validName);
    for (int k = 0; k < found && n < Menu::kMaxItems - 1; ++k) {
      strlcpy(userTplNames_[userTpl_], names[k], sizeof(userTplNames_[0]));
      snprintf(userTplLabels_[userTpl_], sizeof(userTplLabels_[0]), "> %s", names[k]);
      items[n++] = {userTplLabels_[userTpl_], kTplUser + userTpl_};
      ++userTpl_;
    }
  }
  items[n++] = {"Demo songs...", kTplDemos};
  items[n++] = {"Save as template...", kTplSave, hw::sdReady()};
  app_.menu().open("NEW PROJECT", items, n, [this](int id) { onNewChoice(id); });
}

void FileScreen::onNewChoice(int id) {
  if (id == kTplSave) {
    saveTemplateAs();
    return;
  }
  if (id == kTplDemos) {
    MenuItem demos[Menu::kMaxItems];
    int n = 0;
    for (int i = 0; i < mt::demoCount() && n < Menu::kMaxItems; ++i) demos[n++] = {mt::demoName(i), kTplDemo + i};
    app_.menu().openChild("DEMO SONGS", demos, n, [this](int d) { onNewChoice(d); });
    return;
  }
  newChoice_ = id;
  if (app_.projectDirty()) {
    const MenuItem confirm[] = {{"Cancel", kCancel}, {"Discard & new", kDiscardNew}};
    app_.menu().openChild("DISCARD CHANGES?", confirm, 2, [this](int c) { onMenu(c); });
  } else {
    doNew();
  }
}

void FileScreen::saveTemplateAs() {
  kb_.open("TEMPLATE NAME:", "", [this](const char* text) {
    char nm[17];
    if (!storage::sanitize(text, nm)) {
      app_.toast("BAD NAME");
      return;
    }
    app_.showBusy("SAVING...");
    const storage::Result r = storage::saveTemplate(app_.project(), nm);
    char msg[40];
    snprintf(msg, sizeof(msg), r == storage::Result::Ok ? "TEMPLATE %s SAVED" : "%s", r == storage::Result::Ok ? nm : storage::resultText(r));
    app_.toast(msg);
  });
}

void FileScreen::doNew() {
  struct Refresh {
    FileScreen& f;
    ~Refresh() { f.autoAvail_ = storage::autosaveExists(f.app_.project().name); }
  } refresh{*this};
  int missing = 0;
  const bool user = newChoice_ >= kTplUser && newChoice_ - kTplUser < userTpl_;
  const bool demo = newChoice_ >= kTplDemo && newChoice_ - kTplDemo < mt::demoCount();
  if (user) app_.showBusy("LOADING...");
  const storage::Result r = user   ? storage::newFromTemplate(app_.project(), userTplNames_[newChoice_ - kTplUser], &missing)
                            : demo ? storage::newDemo(app_.project(), newChoice_ - kTplDemo)
                                   : storage::newProject(app_.project(), newChoice_ < kTplUser ? newChoice_ : 0);
  if (r != storage::Result::Ok) {
    app_.toast(storage::resultText(r));
    return;
  }
  app_.projectReplaced();
  char msg[40];
  snprintf(msg, sizeof(msg), "NEW: %s",
           user   ? userTplNames_[newChoice_ - kTplUser]
           : demo ? mt::demoName(newChoice_ - kTplDemo)
                  : mt::templateName(newChoice_));
  app_.loadedToast(msg, missing, false);
}

void FileScreen::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  if (kb_.isOpen()) {
    kb_.onInput(ev);
    return;
  }
  if (import_.isOpen()) {
    import_.onInput(ev);
    return;
  }
  if (render_.isOpen()) {
    render_.onInput(ev);
    return;
  }
  if (wifi_.isOpen()) {
    wifi_.onInput(ev);
    return;
  }
  if (midiDlg_.isOpen()) {
    midiDlg_.onInput(ev);
    return;
  }
  if (list_.ready()) {
    listAct(list_.onInput(ev));
    return;
  }
  if (section_ == kSecSamples) {
    samplesInput(ev);
    return;
  }
  if (section_ == kSecSystem) {
    systemInput(ev);
    return;
  }
  switch (ev.type) {
    case InputType::EncTurn: moveSel(ev.delta); break;
    case InputType::EncClick:
      if (sel_ != kSectionSel) run(sel_);  // the section switch: A + turn
      break;
    case InputType::EditTurn:
      if (sel_ == kSectionSel && ev.delta) setSection(section_ + ev.delta);
      break;
    default: break;
  }
}

// B: the open dialog / keyboard gets its long press (close / cancel), else the file list goes up a
// folder (WAV / MIDI import below /samples, /midi) or closes;
// SYSTEM: leaves the edit with the value kept.
void FileScreen::onBack() {
  const hw::InputEvent lp{hw::InputType::EncLong, 0, false};
  if (kb_.isOpen()) kb_.onInput(lp);
  else if (import_.isOpen()) import_.onInput(lp);
  else if (render_.isOpen()) render_.onInput(lp);
  else if (wifi_.isOpen()) wifi_.onInput(lp);
  else if (midiDlg_.isOpen()) midiDlg_.onBack();  // learning: skips the step, else closes
  else if (list_.ready()) listAct(FileList::Act::Back);  // a subfolder: one level up; the top: closes
  else if (section_ == kSecSystem) {
    if (diagnostics_) showSystem(false); else sysList_.setEdit(false);
  }
}

// B + Shift + turn: the previous / next section.
void FileScreen::onPage(int d) {
  if (midiDlg_.isOpen()) { midiDlg_.onPage(d); return; }
  if (kb_.isOpen() || import_.isOpen() || render_.isOpen() || wifi_.isOpen() || list_.ready())
    return;
  setSection(section_ + d);
}

void FileScreen::onTouch(const TouchEvent& ev) {
  if (kb_.isOpen()) {
    kb_.onTouch(ev, app_.shift());
    return;
  }
  if (import_.isOpen()) {
    import_.onTouch(ev);
    return;
  }
  if (render_.isOpen()) {
    render_.onTouch(ev);
    return;
  }
  if (wifi_.isOpen()) {
    wifi_.onTouch(ev);
    return;
  }
  if (midiDlg_.isOpen()) {
    midiDlg_.onTouch(ev);
    return;
  }
  const int top = y0_ + kHeaderH + (list_.ready() ? 0 : PageBar::kH);
  if (list_.ready()) {
    listAct(list_.onTouch(ev, top));
    return;
  }
  if (ev.y >= y0_ + kHeaderH && ev.y < top) {
    if (ev.type == TouchType::Tap) {
      if (section_ == kSecSystem && diagnostics_) showSystem(false);
      else setSection(PageBar::at(ev.x, kSections));
    }
    return;
  }
  if (section_ == kSecSamples) {
    samplesTouch(ev);
    return;
  }
  if (section_ == kSecSystem) {
    systemTouch(ev);
    return;
  }
  if (ev.type != TouchType::Tap || ev.y < top) return;
  const int a = (ev.y - top) / kActionH;
  if (a < kActions) run(a);
}

void FileScreen::draw(LGFX_Sprite& s, int y0, int) {
  y0_ = y0;
  if (kb_.isOpen()) {
    kb_.draw(s, y0);
    return;
  }
  if (import_.isOpen()) {
    import_.draw(s, y0);
    return;
  }
  if (render_.isOpen()) {
    render_.draw(s, y0);
    return;
  }
  if (wifi_.isOpen()) {
    wifi_.draw(s, y0);
    return;
  }
  if (midiDlg_.isOpen()) {
    midiDlg_.draw(s, y0);
    return;
  }
  const bool sd = hw::sdReady();
  drawHeader(s, y0);
  const int top = y0 + kHeaderH + (list_.ready() ? 0 : PageBar::kH);
  if (list_.ready()) {
    list_.draw(s, top);
    return;
  }
  if (section_ == kSecSamples) {
    drawSamples(s, top);
    return;
  }
  if (section_ == kSecSystem) {
    batSeqDrawn_ = app_.batterySeq();
    profSecDrawn_ = profSecs();  // the CPU profile row shows this value
    sysList_.draw(s, top);
    return;
  }
  for (int a = 0; a < kActions; ++a) {
    if (a == kRetry && sd && !autoAvail_) break;
    const int ry = top + a * kActionH;
    const bool sel = a == sel_;
    if (sel) s.fillRect(0, ry, kScreenW, kActionH - 2, kSelBg);
    s.setTextColor(!enabled(a) ? kDim : (sel ? kCursor : kText));
    s.drawString(a == kRetry && sd ? kRestoreLabel : kLabels[a], 16, ry + (kActionH - 2 - kCharH) / 2);
  }
}

void FileScreen::drawHeader(LGFX_Sprite& s, int y0) {
  const bool sd = hw::sdReady();
  char buf[64];
  const int ty = y0 + (kHeaderH - 4 - kCharH) / 2;
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  s.setTextColor(kText);
  if (list_.ready()) {  // the title, the path's tail (so the current folder stays visible)
    FileList::drawHeader(s, y0, wavList_ ? "IMPORT WAV" : (midiList_ ? "IMPORT" : "LOAD"),
                         browsing() ? curDir() : "/projects");
    return;
  }
  snprintf(buf, sizeof(buf), "FILE  %s%s", app_.project().name, app_.projectDirty() ? "*" : "");
  s.drawString(buf, 16, ty);
  {
    // Sections: a page bar; with the encoder focus on it, an outline (A + turn = previous / next section).
    static const char* const kNames[kSections] = {"PROJECTS", "SAMPLES", "SYSTEM"};
    const int by = y0 + kHeaderH;
    if (section_ == kSecSystem && diagnostics_) {
      s.setTextColor(kCursor); s.drawString("< SYSTEM / DIAGNOSTICS",16,by+2);
      return;
    }
    PageBar::draw(s, by, kNames, kSections, section_);
    const bool focus = section_ == kSecSamples  ? ssel_ == kSwitchRow
                       : section_ == kSecSystem ? sysList_.barSelected()
                                                : sel_ == kSectionSel;
    if (focus) PageBar::drawFocus(s, by);
  }
  s.setTextColor(sd ? kDim : kEditCursor);
  const char* sdText = sd ? "SD OK" : "NO SD CARD";
  s.drawString(sdText, kScreenW - 16 - static_cast<int>(strlen(sdText)) * kCharW, ty);
}

void FileScreen::setSection(int section) {
  section_ = (section % kSections + kSections) % kSections;
  // Focus stays on the switch, so turning the encoder continues in the new section.
  switch (section_) {
    case kSecSamples:
      ssel_ = kSwitchRow;
      stop_ = 0;
      break;
    case kSecSystem:
      showSystem(false);
      sysList_.setEdit(false);
      sysList_.selectBar();
      break;
    default: sel_ = kSectionSel; break;
  }
  app_.invalidate();
}

// ---- SAMPLES ----

int FileScreen::sampleRowCount() const { return kFirstSample + app_.project().sampleCount; }

bool FileScreen::sampleEnabled(int row) const {
  const audio::BankView& v = audio::bankView();
  if (!v.mounted) return row == kSwitchRow;
  switch (row) {
    case kSwitchRow: return true;
    case kImportRow: return hw::sdReady();
    case kCompactRow:
    case kClearRow: return v.count > 0;
    default: return row < sampleRowCount();
  }
}

void FileScreen::sampleMove(int delta) {
  const int n = sampleRowCount() - kSwitchRow;  // rows kSwitchRow..sampleRowCount()-1
  if (ssel_ >= sampleRowCount()) ssel_ = sampleRowCount() - 1;  // the list got shorter: stay at its end
  ssel_ = ((ssel_ - kSwitchRow + delta) % n + n) % n + kSwitchRow;
  if (ssel_ >= 0 && ssel_ < stop_) stop_ = ssel_;
  if (ssel_ >= stop_ + kSampleRows) stop_ = ssel_ - kSampleRows + 1;
  if (ssel_ == kSwitchRow) stop_ = 0;
}

bool FileScreen::playbackBusy() {
  const engine::Status& st = app_.status();
  if (!st.playing && !st.paused) return false;
  app_.toast(audio::bankResultText(audio::BankResult::Busy));
  return true;
}

int FileScreen::usedBy(const char* sample) const { return mt::projSampleUser(app_.project(), sample); }

void FileScreen::sampleRun(int row) {
  if (!sampleEnabled(row)) return;
  ssel_ = row;
  switch (row) {
    case kSwitchRow: return;  // switched by A + turn / a tap on the page bar
    case kImportRow:
      if (!playbackBusy()) openWavList();
      return;
    case kCompactRow:
      if (!playbackBusy()) doCompact();
      return;
    case kClearRow: {
      const MenuItem items[] = {{"Cancel", kCancel}, {"Clear", kClearCache}};
      app_.menu().open("CLEAR UNUSED SAMPLES?", items, 2, [this](int id) { onMenu(id); });
      return;
    }
    default: break;
  }
  const int i = row - kFirstSample;
  if (i < 0 || i >= app_.project().sampleCount) return;
  strlcpy(pending_, app_.project().samples[i].name, sizeof(pending_));
  const MenuItem items[] = {{"Cancel", kCancel}, {"Rename", kRenameSample}, {"Delete", kDeleteSample}};
  app_.menu().open(pending_, items, 3, [this](int id) { onMenu(id); });
}

void FileScreen::openWavList() {
  if (!list_.alloc()) return;
  midiList_ = false;
  wavList_ = true;
  // The folder may have been removed (Wi-Fi, card swap): start over at /samples.
  if (!atRoot() && !(hw::sdReady() && hw::sdFs().exists(wavDir_))) strlcpy(wavDir_, "/samples", sizeof(wavDir_));
  listDir(curCursor());
}

// WAV import: Play previews the selected file (again: stops); Load / MIDI: the transport.
bool FileScreen::onPlay() {
  if (kb_.isOpen() || import_.isOpen() || wifi_.isOpen() || midiDlg_.isOpen() || !list_.ready()) return false;
  return list_.onPlay();
}

void FileScreen::importAs(const char* initial) {
  kb_.open("SAMPLE NAME:", initial, [this](const char* text) {
    char nm[17];
    if (!storage::sanitize(text, nm)) {
      app_.toast("BAD NAME");
      importAs(text);
      return;
    }
    if (mt::projSampleFind(app_.project(), nm) >= 0) {
      strlcpy(pending_, nm, sizeof(pending_));
      const MenuItem items[] = {{"Cancel", kCancel}, {"Overwrite", kOverwriteSample}};
      char title[28];
      snprintf(title, sizeof(title), "%s EXISTS", nm);
      app_.menu().openChild(title, items, 2, [this](int id) { onMenu(id); });
      return;
    }
    doImport(nm);
  });
}

void FileScreen::progress(uint32_t done, uint32_t total, void* ctx) {
  FileScreen& f = *static_cast<FileScreen*>(ctx);
  f.app_.showProgress(f.busyLabel_, done, total);
}

void FileScreen::doImport(const char* name) {
  if (playbackBusy()) return;
  char nm[17];
  strlcpy(nm, name, sizeof(nm));  // name may point into pending_
  mt::Project& p = app_.project();
  if (mt::projSampleFind(p, nm) < 0 && p.sampleCount >= mt::kProjSamples) {
    app_.toast("SAMPLE LIST FULL");
    return;
  }
  char path[sizeof(wavDir_) + hw::kNameMax];
  const int n = snprintf(path, sizeof(path), "%s/%s", wavDir_, wavFile_);
  if (n < 0 || n >= static_cast<int>(sizeof(path))) {
    app_.toast("PATH TOO LONG");
    return;
  }
  busyLabel_ = "IMPORT";
  app_.showBusy("IMPORT...");
  // The old data of an overwritten name stays cached until evicted: only the list entry changes.
  audio::ImportOut out{};
  const audio::BankResult r = audio::importToCache(path, out, nullptr, progress, this);
  if (r != audio::BankResult::Ok) {
    if (r == audio::BankResult::OpenFail || r == audio::BankResult::ReadFail) hw::sdRecover();
    app_.toast(audio::bankResultText(r));
    return;
  }
  engine::lockProject();  // the list and the instruments naming it are shared with the engine
  const int i = mt::projSampleSet(p, nm, out.crc, out.frames);
  engine::unlockProject();
  if (i < 0) {
    app_.toast("BAD NAME");
    return;
  }
  app_.markDirty();
  sampleMove(kFirstSample + i - ssel_);
  char msg[32];
  snprintf(msg, sizeof(msg), "IMPORTED %s", nm);
  app_.toast(msg);
}

void FileScreen::renameSample(const char* initial) {
  kb_.open("RENAME TO:", initial, [this](const char* text) {
    if (playbackBusy()) return;
    mt::Project& p = app_.project();
    const int i = mt::projSampleFind(p, pending_);
    if (i < 0) return;
    char nm[17];
    if (!storage::sanitize(text, nm)) {
      app_.toast("BAD NAME");
      renameSample(text);
      return;
    }
    if (strcmp(nm, p.samples[i].name) == 0) return;
    engine::lockProject();
    const bool renamed = mt::projSampleRename(p, i, nm);  // also renames it in the instruments
    engine::unlockProject();
    if (!renamed) {
      app_.toast("NAME TAKEN");
      renameSample(text);
      return;
    }
    app_.markDirty();
    strlcpy(pending_, nm, sizeof(pending_));
    sampleMove(kFirstSample + mt::projSampleFind(p, nm) - ssel_);
    char msg[32];
    snprintf(msg, sizeof(msg), "RENAMED %s", nm);
    app_.toast(msg);
  });
}

void FileScreen::doDelete(const char* name) {
  if (playbackBusy()) return;
  char nm[17];
  strlcpy(nm, name, sizeof(nm));
  mt::Project& p = app_.project();
  const int i = mt::projSampleFind(p, nm);
  if (i < 0) return;
  // Only the list entry: the data stays cached, the file goes with the next save.
  engine::lockProject();
  mt::projSampleRemove(p, i);
  engine::unlockProject();
  app_.markDirty();
  sampleMove(0);  // clamp to the shorter list
  char msg[32];
  snprintf(msg, sizeof(msg), "DELETED %s", nm);
  app_.toast(msg);
}

void FileScreen::doCompact() {
  busyLabel_ = "COMPACT";
  app_.showBusy("COMPACT...");
  const audio::BankResult r = audio::compactBank(progress, this);
  app_.toast(r == audio::BankResult::Ok ? "BANK COMPACTED" : audio::bankResultText(r));
}

void FileScreen::doClear() {
  app_.showBusy("CLEARING...");
  int n = 0;
  const audio::BankResult r = audio::clearCache(&n);
  if (r != audio::BankResult::Ok) {
    app_.toast(audio::bankResultText(r));
    return;
  }
  char msg[32];
  snprintf(msg, sizeof(msg), "CLEARED %d", n);
  app_.toast(msg);
}

uint32_t FileScreen::cacheBytes() { return audio::bankView().unused; }

void FileScreen::samplesInput(const hw::InputEvent& ev) {
  using hw::InputType;
  switch (ev.type) {
    case InputType::EncTurn: sampleMove(ev.delta); break;
    case InputType::EncClick:
      if (ssel_ != kSwitchRow) sampleRun(ssel_);  // the section switch: A + turn
      break;
    case InputType::EditTurn:
      if (ssel_ == kSwitchRow && ev.delta) setSection(section_ + ev.delta);
      break;
    default: break;
  }
}

void FileScreen::samplesTouch(const TouchEvent& ev) {
  const int rows = y0_ + kHeaderH + PageBar::kH + kInfoH;
  if (ev.type == TouchType::Drag) {
    dragAcc_ += ev.dy;
    const int n = dragAcc_ / kRowH;
    dragAcc_ -= n * kRowH;
    const int total = sampleRowCount();
    stop_ = clampi(stop_ - n, 0, total > kSampleRows ? total - kSampleRows : 0);
    return;
  }
  if (ev.type != TouchType::Tap || ev.y < rows) return;
  const int row = stop_ + (ev.y - rows) / kRowH;
  if (row < sampleRowCount()) sampleRun(row);
}

void FileScreen::drawSamples(LGFX_Sprite& s, int top) {
  const int ty = (kInfoH - kCharH) / 2;
  const audio::BankView& b = audio::bankView();
  if (!b.mounted) {
    s.setTextColor(kEditCursor);
    s.drawString(b.ok ? "NO SAMPLE BANK (synth flash)" : "SYNTH NOT ANSWERING", 16, top + ty);
    return;
  }
  const mt::Project& p = app_.project();
  char buf[48];
  const uint32_t cap = b.capacity, freeB = b.free;
  s.setTextColor(kDim);
  snprintf(buf, sizeof(buf), "FREE %u / %u KB  CACHE %u KB", static_cast<unsigned>(freeB / 1024),
           static_cast<unsigned>(cap / 1024), static_cast<unsigned>(cacheBytes() / 1024));
  s.drawString(buf, 16, top + ty);
  constexpr int kBarX = 352, kBarW = kScreenW - 16 - kBarX, kBarH = 10;
  const int barY = top + (kInfoH - kBarH) / 2;
  const int used = cap ? static_cast<int>(static_cast<uint64_t>(cap - freeB) * (kBarW - 2) / cap) : 0;
  s.drawRect(kBarX, barY, kBarW, kBarH, kDim);
  s.fillRect(kBarX + 1, barY + 1, used, kBarH - 2, kText);

  const int rowsTop = top + kInfoH;
  const int total = sampleRowCount();
  for (int r = 0; r < kSampleRows; ++r) {
    const int row = stop_ + r;
    if (row >= total) break;
    const int ry = rowsTop + r * kRowH;
    const int yy = ry + (kRowH - kCharH) / 2;
    const bool sel = row == ssel_;
    if (sel) s.fillRect(0, ry, kScreenW, kRowH, kSelBg);
    s.setTextColor(!sampleEnabled(row) ? kDim : (sel ? kCursor : kText));
    if (row == kImportRow) {
      s.drawString("Import WAV...", 16, yy);
      continue;
    }
    if (row == kCompactRow) {
      s.drawString("Compact", 16, yy);
      continue;
    }
    if (row == kClearRow) {
      s.drawString("Clear cache", 16, yy);
      continue;
    }
    const int i = row - kFirstSample;
    if (i >= p.sampleCount) break;
    const mt::ProjSample& ps = p.samples[i];
    s.drawString(ps.name, 16, yy);
    snprintf(buf, sizeof(buf), "%u KB", static_cast<unsigned>((ps.frames * 2 + 1023) / 1024));
    s.drawString(buf, kScreenW - 16 - static_cast<int>(strlen(buf)) * kCharW, yy);
    if (!b.sampleCached(i)) {
      s.setTextColor(kRed);
      s.drawString("MISSING", 16 + 18 * kCharW, yy);
      continue;
    }
    const uint32_t rate = b.rate[i];
    const uint32_t ds = rate ? (static_cast<uint64_t>(ps.frames) * 10 + rate / 2) / rate : 0;  // 0.1 s
    snprintf(buf, sizeof(buf), "%u.%us", static_cast<unsigned>(ds / 10), static_cast<unsigned>(ds % 10));
    s.drawString(buf, 16 + 18 * kCharW, yy);
  }
  s.setTextColor(kDim);
  if (stop_ > 0) s.drawString("^", kScreenW - 120, rowsTop + 4);
  if (stop_ + kSampleRows < total) s.drawString("v", kScreenW - 120, rowsTop + (kSampleRows - 1) * kRowH + 4);
}

// ---- SYSTEM ----

// Device settings (App, NVS) and read-only diagnostics: no row is project data, none marks it dirty.
void FileScreen::initSystem() {
  // Live output level; the project's master is MAIN on TRACK / MIX.
  sysParams_[kPhones] = {"Phones", [this](char* o, int n) { snprintf(o, n, "%d%%", app_.phones()); },
                         [this](int d) { app_.setPhones(app_.phones() + d); }};
  sysParams_[kPreview] = {"Preview", [this](char* o, int n) { snprintf(o, n, "%s", app_.preview() ? "ON" : "OFF"); },
                          [this](int d) { app_.setPreview(d > 0); }};
  sysParams_[kPerfVis] = {"Perf visuals",
                          [this](char* o, int n) { snprintf(o, n, "%s", app_.perfVisuals() ? "ON" : "OFF"); },
                          [this](int d) { app_.setPerfVisuals(d > 0); }};
  sysParams_[kTheme] = {"Theme", [this](char* o, int n) { snprintf(o, n, "%s", themeAt(app_.theme()).name); },
                        [this](int d) { app_.setTheme(app_.theme() + d); }};
  sysParams_[kAutosave] = {"Autosave",
                           [this](char* o, int n) {
                             if (app_.autosaveMin()) snprintf(o, n, "%d MIN", app_.autosaveMin());
                             else snprintf(o, n, "OFF");
                           },
                           [this](int d) {
                             static const int kSteps[] = {0, 1, 2, 5, 10};
                             int i = 0;
                             while (i < 4 && kSteps[i] < app_.autosaveMin()) ++i;
                             i = clampi(i + (d > 0 ? 1 : -1), 0, 4);
                             app_.setAutosaveMin(kSteps[i]);
                           }};
  // Read only: the firmware version and why the device last restarted (crash log: /diag/crashlog.txt).
  sysParams_[kFirmware] = {"Firmware", [](char* o, int n) { snprintf(o, n, "%s", storage::firmwareRev()); },
                           nullptr};
  // The synth board: its firmware ("NO SYNTH" while it does not answer; "!" = another protocol).
  sysParams_[kSynthFw] = {"Synth fw",
                          [](char* o, int n) {
                            if (!audio::synthUp()) snprintf(o, n, "NO SYNTH");
                            else snprintf(o, n, "%s P%u%s", audio::synthFw(), audio::synthProtocol(),
                                          audio::versionOk() ? "" : " !");
                          },
                          nullptr};
  // Click: flash the synth board with /firmware/teensy.hex from its card (Wi-Fi page puts it there).
  sysParams_[kUpdSynth] = {"Update synth",
                           [](char* o, int n) { snprintf(o, n, "%s", audio::synthUp() ? "CLICK" : "NO SYNTH"); },
                           nullptr};
  // The card in the synth board: size and free space.
  sysParams_[kCard] = {"Card",
                       [](char* o, int n) {
                         uint32_t total = 0, free = 0;
                         if (!audio::synthUp()) snprintf(o, n, "NO SYNTH");
                         else if (!storage::remoteSpace(total, free)) snprintf(o, n, "NO CARD");
                         else snprintf(o, n, "%lu.%lu / %lu.%lu GB FREE", static_cast<unsigned long>(free / 1024),
                                       static_cast<unsigned long>(free % 1024 * 10 / 1024),
                                       static_cast<unsigned long>(total / 1024),
                                       static_cast<unsigned long>(total % 1024 * 10 / 1024));
                       },
                       nullptr};
  // Samples: where they play from (the synth board's SD, plus PSRAM copies) and the voices that ran out
  // of data since it came up (an underrun: that voice faded out for a moment).
  sysParams_[kSamples] = {"Samples",
                          [](char* o, int n) {
                            if (!audio::synthUp()) {
                              snprintf(o, n, "NO SYNTH");
                              return;
                            }
                            const audio::StreamInfo s = audio::streamInfo();
                            mt::streamLine(o, n, s.psram, s.psramFill, s.underruns);
                          },
                          nullptr};
  // The link counters since boot: events lost / late on the synth, bad frames here.
  sysParams_[kLink] = {"Link",
                       [](char* o, int n) {
                         const audio::LinkCounters c = audio::linkCounters();
                         snprintf(o, n, "LOST %lu LATE %lu CRC %lu", static_cast<unsigned long>(c.lost + c.dropped),
                                  static_cast<unsigned long>(c.late), static_cast<unsigned long>(c.crcErrors));
                       },
                       nullptr};
  sysParams_[kLastReset] = {"Last reset", [](char* o, int n) { snprintf(o, n, "%s", storage::lastResetText()); },
                            nullptr};
  // Where the sequencer lives (PSRAM = it costs more) and the free internal RAM.
  sysParams_[kAudioRam] = {"Audio RAM",
                           [](char* o, int n) {
                             const unsigned kb =
                                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
                             snprintf(o, n, "SEQ %s, %uK FREE", engine::seqInternal() ? "INTERNAL" : "PSRAM", kb);
                           },
                           nullptr};
  // The lowest free internal RAM since boot and now (BLE, Wi-Fi, import, render, REC all take from it).
  // BLE up: "MIN/NOW BLE a/b", the untouched stack of its connect task / OUT writer in bytes (0: no
  // writer); short, the value column holds 35 characters and ParamList's buffer 31.
  sysParams_[kRamMin] = {"Internal RAM min",
                         [](char* o, int n) {
                           const unsigned mn =
                               static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024);
                           const unsigned now = static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
                           if (hw::ble::running())
                             snprintf(o, n, "%uK/%uK BLE %lu/%lu", mn, now, static_cast<unsigned long>(hw::ble::stackFree()),
                                      static_cast<unsigned long>(hw::ble::outStackFree()));
                           else
                             snprintf(o, n, "%uK, NOW %uK FREE", mn, now);
                         },
                         nullptr};
  // Sensor voltage, current (CHG while charging) and the gauge's percent; redrawn per sample.
  sysParams_[kBattery] = {"Battery",
                          [this](char* o, int n) {
                            hw::BatterySample b;
                            if (!hw::batteryPresent()) {
                              snprintf(o, n, "NOT FOUND");
                              return;
                            }
                            if (!app_.batterySample(b)) {
                              snprintf(o, n, "---");
                              return;
                            }
                            const mt::BatteryGauge& g = app_.battery();
                            const int ma = static_cast<int>(fabsf(b.amps) * 1000.f + 0.5f);
                            char pct[8];
                            if (g.percent() >= 0) snprintf(pct, sizeof(pct), "%d%%", g.percent());
                            else snprintf(pct, sizeof(pct), "--%%");
                            snprintf(o, n, "%.2fV %s%dmA %s", b.volts, g.charging() ? "CHG " : "", ma, pct);
                          },
                          nullptr};
  // Click: start; play a while (on the screen to measure), click again: the time per stage of the
  // audio render goes to /diag/cpuprof.txt.
  sysParams_[kCpuProf] = {"CPU profile",
                          [this](char* o, int n) {
                            if (audio::profileRunning())
                              snprintf(o, n, "RUNNING %lus, CLICK", static_cast<unsigned long>(profSecDrawn_));
                            else
                              snprintf(o, n, "CLICK TO START");
                          },
                          nullptr};
  sysParams_[kWifiRow] = {"Wi-Fi firmware...",nullptr,nullptr};
  sysParams_[kMidiRow] = {"MIDI devices...",nullptr,nullptr};
  sysParams_[kDiagnostics] = {"Diagnostics...",nullptr,nullptr};
  sysParams_[kDiagBack] = {"Back to SYSTEM",nullptr,nullptr};
  sysList_.setOnCancel([this] { app_.toast("CANCEL"); });
  sysList_.setPageBar(true);
  sysList_.setParams(sysParams_, kFirmware);
  sysList_.setVisibleRows(kSysListRows);
  sysList_.selectBar();
}

uint32_t FileScreen::profSecs() const { return (millis() - profStartMs_) / 1000; }

// A + turn on the page bar: the previous / next section; a click on CPU profile starts / stops it,
// on Update synth asks to flash the synth board.
void FileScreen::showSystem(bool diagnostics) {
  if (diagnostics && !diagnostics_) systemSel_=sysList_.sel();
  diagnostics_=diagnostics;
  sysList_.setEdit(false);
  sysList_.setPageBar(!diagnostics);
  sysList_.setParams(sysParams_+(diagnostics?kFirmware:0),diagnostics?kSysRows-kFirmware:kFirmware);
  sysList_.setVisibleRows(kSysListRows);
  sysList_.setSel(diagnostics?0:systemSel_);
  app_.invalidate();
}

void FileScreen::systemInput(const hw::InputEvent& ev) {
  if (ev.type==hw::InputType::EncClick && !sysList_.editing()) {
    const int row=systemRow();
    if(row==kCpuProf || row==kUpdSynth || row==kWifiRow || row==kMidiRow || row==kDiagnostics || row==kDiagBack) {
      sysAction(row);return;
    }
  }
  if(const int ov=sysList_.onInput(ev)) setSection(section_+ov);
}

void FileScreen::systemTouch(const TouchEvent& ev) {
  const int r=sysList_.rowAt(ev.y);
  const int row=(diagnostics_?kFirmware:0)+r;
  if(ev.type==TouchType::Tap && r>=0 && (row==kCpuProf || row==kUpdSynth || row==kWifiRow || row==kMidiRow || row==kDiagnostics || row==kDiagBack)) {
    sysList_.setEdit(false);sysList_.setSel(r);sysAction(row);return;
  }
  sysList_.onTouch(ev);
}

void FileScreen::sysAction(int row) {
  if(row==kUpdSynth) askUpdateSynth();
  else if(row==kCpuProf) toggleProfile();
  else if(row==kWifiRow) run(kWifi);
  else if(row==kMidiRow) run(kMidi);
  else if(row==kDiagnostics) showSystem(true);
  else if(row==kDiagBack) showSystem(false);
}

void FileScreen::askUpdateSynth() {
  app_.invalidate();
  if (!audio::synthUp()) {
    app_.toast("NO SYNTH");
    return;
  }
  fs::File f = hw::sdFs().open(synthfw::kHexPath);
  if (!f || f.isDirectory()) {
    app_.toast(storage::remoteLastError() == mt::link::kErrNoCard ? "NO CARD IN THE SYNTH"
                                                                   : "NO /FIRMWARE/TEENSY.HEX");
    return;
  }
  const unsigned long kb = static_cast<unsigned long>((f.size() + 1023) / 1024);
  f.close();
  char title[48];
  snprintf(title, sizeof(title), "UPDATE SYNTH (%lu KB)?", kb);
  enum : int { kCancel, kUpdate };
  const MenuItem items[] = {{"Cancel", kCancel}, {"Update", kUpdate}};
  app_.menu().openChild(title, items, 2, [this](int id) {
    if (id == kUpdate) updateSynth();
  });
}

void FileScreen::updateSynth() {
  const uint32_t oldBoot = slink::bootId();
  uint8_t res = 0;
  uint32_t bytes = 0;
  const synthfw::Result r = synthfw::update(
      synthfw::kHexPath, res, bytes,
      [](uint32_t done, uint32_t total, void* a) { static_cast<App*>(a)->showProgress("SYNTH FW", done, total); },
      &app_);
  if (r != synthfw::Result::Ok) {
    app_.endProgress();
    char line[48];
    snprintf(line, sizeof(line), "SYNTH FW: %s", r == synthfw::Result::NoSynth ? "NO ANSWER" : synthfw::rejectText(res));
    app_.toast(line);
    return;
  }
  app_.showBusy("SYNTH FW OK, RESTARTING");
  const uint32_t t0 = millis();
  bool back = false;
  while (millis() - t0 < synthfw::kRebootWaitMs) {
    if (synthfw::rebooted(oldBoot)) {
      back = true;
      break;
    }
    delay(100);
  }
  app_.endProgress();
  char line[48];
  if (back) snprintf(line, sizeof(line), "SYNTH FW %s", slink::synthFw());
  else snprintf(line, sizeof(line), "SYNTH FW: NO ANSWER AFTER THE UPDATE");
  app_.toast(line);
}

void FileScreen::toggleProfile() {
  app_.invalidate();
  if (!audio::profileRunning()) {
    audio::profileStart();
    if (!audio::profileRunning()) {
      app_.toast("PROFILE: NO SYNTH");
      return;
    }
    profStartMs_ = millis();
    app_.toast("PROFILE: PLAY, THEN CLICK AGAIN");
    return;
  }
  audio::Profile pr;
  const uint32_t secs = profSecs();
  if (!audio::profileStop(pr)) {
    app_.toast("PROFILE: NOTHING RENDERED");
    return;
  }
  float total = 0;
  int top = 0;
  for (int i = 0; i < audio::kProfStages; ++i) {
    total += pr.us[i];
    if (pr.us[i] > pr.us[top]) top = i;
  }
  constexpr float kBlockUs = audio::kBlock * 1e6f / audio::kRate;
  char line[96];
  snprintf(line, sizeof(line), "firmware %s, %lu s, %lu blocks, render %.0f us = %.1f %%\n", storage::firmwareRev(),
           static_cast<unsigned long>(secs), static_cast<unsigned long>(pr.blocks), total, total * 100 / kBlockUs);
  const bool saved = storage::appendCpuProfile(line, pr);
  snprintf(line, sizeof(line), "%s %.0f%%, TOP %s %.0f%%", saved ? "SAVED" : "NO SD", total * 100 / kBlockUs,
           mt::Synth::profName(top), pr.us[top] * 100 / kBlockUs);
  app_.toast(line);
}

// SYSTEM: a new battery sample, and the running CPU profile's seconds.
bool FileScreen::wantsRedraw(const engine::Status&) {
  if (wifi_.wantsRedraw() || midiDlg_.wantsRedraw()) return true;
  if (section_ != kSecSystem || list_.ready() || kb_.isOpen() || import_.isOpen() || render_.isOpen() || wifi_.isOpen() ||
      midiDlg_.isOpen())
    return false;
  return app_.batterySeq() != batSeqDrawn_ || (audio::profileRunning() && profSecs() != profSecDrawn_);
}

}  // namespace ui
