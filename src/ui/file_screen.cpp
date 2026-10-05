#include "file_screen.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "app.h"
#include "audio/audio.h"
#include "audio/bank.h"
#include "esp_heap_caps.h"
#include "storage/storage.h"

namespace ui {
namespace {

constexpr const char* kLabels[] = {"Save", "Save As...", "Load...", "New", "Import MIDI...", "Wi-Fi transfer...", "Retry"};

constexpr uint32_t kPreviewMs = 8000;  // WAV preview length (32 kHz mono: 512 KB)

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// I/O errors may mean the card was pulled: remount so the screen shows the real state.
void reprobe(storage::Result r) {
  if (r == storage::Result::WriteFail || r == storage::Result::ReadFail) hw::sdBegin();
}

}  // namespace

void FileScreen::onEnter() {
  kb_.close();
  import_.close();
  wifi_.close();
  closeList();
  if (!enabled(sel_)) moveSel(1);
  if (samples_) sampleMove(0);  // the bank may have changed
}

void FileScreen::onLeave() {
  kb_.close();
  import_.close();  // frees the file / note buffers
  wifi_.close();    // leaving the tab ends Wi-Fi mode
  closeList();
}

bool FileScreen::enabled(int a) const {
  const bool sd = hw::sdReady();
  switch (a) {
    case kSectionSel: return true;
    case kRetry: return !sd;
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
  sel_ = a;
  switch (a) {
    case kSectionSel: setSection(true); break;
    case kSave: save(); break;
    case kSaveAs: {
      const char* cur = app_.project().name;
      saveAs(strcmp(cur, "untitled") == 0 ? "" : cur);
      break;
    }
    case kLoad: openList(false); break;
    case kImport: openList(true); break;
    case kNew:
      if (app_.projectDirty()) {
        const MenuItem items[] = {{"Cancel", kCancel}, {"Discard & new", kDiscardNew}};
        app_.menu().open("DISCARD CHANGES?", items, 2, [this](int id) { onMenu(id); });
      } else {
        doNew();
      }
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
      app_.toast(hw::sdBegin() ? "SD OK" : storage::resultText(storage::Result::NoSd));
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
    case kOverwrite: doSave(pending_); break;
    case kSaveWifi:
      doSave(app_.project().name);
      if (!app_.projectDirty()) startWifi();
      break;
    case kDiscardWifi: startWifi(); break;
    case kOverwriteSample: {
      if (playbackBusy()) break;
      // The old sample stays until the new one is imported and checked.
      doImport(pending_, true);
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
      app_.menu().open(title, items, 2, [this](int id) { onMenu(id); });
      break;
    }
    case kDeleteUsed: doDelete(pending_); break;
    default: break;
  }
  app_.invalidate();
}

void FileScreen::startWifi() {
  if (!hw::sdReady()) {
    app_.toast(storage::resultText(storage::Result::NoSd));
    return;
  }
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
      app_.menu().open(title, items, 2, [this](int id) { onMenu(id); });
      return;
    }
    doSave(nm);
  });
}

void FileScreen::doSave(const char* name) {
  char nm[17];
  strlcpy(nm, name, sizeof(nm));  // name may point into the project
  app_.showBusy("SAVING...");
  const storage::Result r = storage::save(app_.project(), nm);
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
  if (!names_) {
    names_ = static_cast<char(*)[hw::kNameMax]>(heap_caps_malloc(kMaxFiles * hw::kNameMax, MALLOC_CAP_SPIRAM));
    if (!names_) {
      app_.toast(storage::resultText(storage::Result::NoMemory));
      return;
    }
  }
  midiList_ = midi;
  wavList_ = false;
  // Projects: only names that load back unchanged; others would be renamed by sanitize().
  // MIDI: subfolders first, then .mid / .midi with the extension (any case).
  dirCount_ = midi ? hw::sdListDirs(midiDir_, names_, kMaxFiles) : 0;
  static constexpr const char* kMidiExts[] = {".mid", ".midi"};
  count_ = dirCount_ + (midi ? hw::sdListFiles(midiDir_, kMidiExts, 2, names_ + dirCount_, kMaxFiles - dirCount_)
                             : hw::sdList("/projects", ".mtp", names_, kMaxFiles, storage::validName));
  listSel_ = count_ > 0 ? 1 : 0;
  listTop_ = 0;
  dragAcc_ = 0;
  if (count_ == 0) app_.toast(!midi ? "NO PROJECTS" : (atRoot() ? "NO /midi FILES" : "EMPTY FOLDER"));
}

bool FileScreen::atRoot() const { return strcmp(curDir(), rootDir()) == 0; }

void FileScreen::reopenList() {
  if (wavList_) openWavList();
  else openList(true);
}

bool FileScreen::listUp() {
  if (!browsing() || atRoot()) return false;
  char* dir = curDir();
  char* slash = strrchr(dir, '/');
  char left[hw::kNameMax];
  strlcpy(left, slash + 1, sizeof(left));
  if (slash == dir) strlcpy(dir, rootDir(), curDirCap());  // should not happen
  else *slash = 0;
  reopenList();
  // Put the cursor on the folder we came from.
  for (int i = 0; i < dirCount_; ++i)
    if (strcasecmp(names_[i], left) == 0) {
      listSel_ = i + 1;
      if (listSel_ >= listTop_ + kListRows) listTop_ = listSel_ - kListRows + 1;
      break;
    }
  return true;
}

void FileScreen::closeList() {
  stopPreview();
  heap_caps_free(names_);
  names_ = nullptr;
  count_ = 0;
  dirCount_ = 0;
  wavList_ = false;
}

void FileScreen::chooseFile(int idx) {
  stopPreview();
  if (idx <= 0 || idx > count_) {
    if (!(idx == 0 && listUp())) closeList();
    return;
  }
  if (browsing() && idx - 1 < dirCount_) {
    const char* sub = names_[idx - 1];
    char* dir = curDir();
    if (wavList_) {
      int depth = 0;
      for (const char* p = dir + strlen("/samples"); *p; ++p) depth += *p == '/';
      if (depth >= kWavDepthMax) {
        app_.toast("TOO DEEP");
        return;
      }
    }
    if (strlen(dir) + 1 + strlen(sub) >= curDirCap()) {
      app_.toast("PATH TOO LONG");
      return;
    }
    strlcat(dir, "/", curDirCap());
    strlcat(dir, sub, curDirCap());
    reopenList();
    return;
  }
  if (wavList_) {
    strlcpy(wavFile_, names_[idx - 1], sizeof(wavFile_));
    closeList();
    // Default sample name: the file name without the extension, cut to the bank's rules.
    char base[hw::kNameMax];
    strlcpy(base, wavFile_, sizeof(base));
    if (char* dot = strrchr(base, '.')) *dot = 0;
    char nm[17];
    storage::sanitize(base, nm);
    importAs(nm);
    return;
  }
  if (midiList_) {
    // Keep the list on failure so another file can be picked.
    if (import_.open(midiDir_, names_[idx - 1])) closeList();
    else if (!hw::sdReady()) closeList();
    return;
  }
  strlcpy(pending_, names_[idx - 1], sizeof(pending_));
  if (app_.projectDirty()) {
    const MenuItem items[] = {{"Cancel", kCancel}, {"Discard & load", kDiscardLoad}};
    app_.menu().open("DISCARD CHANGES?", items, 2, [this](int id) { onMenu(id); });
  } else {
    doLoad(false);
  }
}

// The clipboard is kept across Load / New on purpose: it copies patterns between projects.
void FileScreen::doLoad(bool bak) {
  app_.showBusy("LOADING...");
  const storage::Result r = storage::load(app_.project(), pending_, bak);
  if (r == storage::Result::Ok) {
    closeList();
    app_.projectReplaced();
    char msg[32];
    snprintf(msg, sizeof(msg), bak ? "LOADED %s.bak" : "LOADED %s", app_.project().name);
    app_.toast(msg);
    return;
  }
  reprobe(r);
  using storage::Result;
  const bool fileProblem = r != Result::BadVersion && r != Result::NoSd && r != Result::NoMemory &&
                           r != Result::EngineBusy;
  if (fileProblem && !bak && storage::exists(pending_, true)) {
    const MenuItem items[] = {{"Cancel", kCancel}, {"Load backup", kLoadBak}};
    app_.menu().open(storage::resultText(r), items, 2, [this](int id) { onMenu(id); });
    return;
  }
  if (!hw::sdReady()) closeList();
  app_.toast(storage::resultText(r));
}

void FileScreen::doNew() {
  const storage::Result r = storage::newProject(app_.project());
  if (r != storage::Result::Ok) {
    app_.toast(storage::resultText(r));
    return;
  }
  app_.projectReplaced();
  app_.toast("NEW PROJECT");
}

void FileScreen::listScroll(int rows) {
  const int total = count_ + 1;
  listTop_ = clampi(listTop_ + rows, 0, total > kListRows ? total - kListRows : 0);
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
  if (wifi_.isOpen()) {
    wifi_.onInput(ev);
    return;
  }
  if (samples_ && !names_) {
    samplesInput(ev);
    return;
  }
  if (names_) {
    switch (ev.type) {
      case InputType::EncTurn:
        listSel_ = ((listSel_ + ev.delta) % (count_ + 1) + count_ + 1) % (count_ + 1);
        if (listSel_ != pvSel_) stopPreview();
        if (listSel_ < listTop_) listTop_ = listSel_;
        if (listSel_ >= listTop_ + kListRows) listTop_ = listSel_ - kListRows + 1;
        break;
      case InputType::EncClick: chooseFile(listSel_); break;
      case InputType::EncLong: closeList(); break;
      default: break;
    }
    return;
  }
  switch (ev.type) {
    case InputType::EncTurn: moveSel(ev.delta); break;
    case InputType::EncClick: run(sel_); break;
    default: break;
  }
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
  if (wifi_.isOpen()) {
    wifi_.onTouch(ev);
    return;
  }
  const int top = y0_ + kHeaderH;
  if (!names_ && ev.type == TouchType::Tap && ev.y < top && ev.x >= kSwitchX - 8 && ev.x < kSwitchX + kSwitchW + 8) {
    setSection(ev.x >= kSwitchX + 11 * kCharW);  // "PROJECTS | " is 11 chars
    return;
  }
  if (samples_ && !names_) {
    samplesTouch(ev);
    return;
  }
  if (names_) {
    if (ev.type == TouchType::Drag) {
      dragAcc_ += ev.dy;
      const int rows = dragAcc_ / kRowH;
      dragAcc_ -= rows * kRowH;
      listScroll(-rows);
      return;
    }
    if (ev.type != TouchType::Tap || ev.y < top) return;
    const int idx = listTop_ + (ev.y - top) / kRowH;
    if (idx > count_) return;
    listSel_ = idx;
    chooseFile(idx);
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
  if (wifi_.isOpen()) {
    wifi_.draw(s, y0);
    return;
  }
  const bool sd = hw::sdReady();
  drawHeader(s, y0);
  const int top = y0 + kHeaderH;
  if (samples_ && !names_) {
    drawSamples(s, top);
    return;
  }
  if (names_) {
    for (int r = 0; r < kListRows; ++r) {
      const int i = listTop_ + r;
      if (i > count_) break;
      const int ry = top + r * kRowH;
      const bool sel = i == listSel_;
      if (sel) s.fillRect(0, ry, kScreenW, kRowH, kSelBg);
      s.setTextColor(sel ? kCursor : (i == 0 ? kDim : kText));
      const bool dir = browsing() && i > 0 && i - 1 < dirCount_;
      const char* label = i == 0 ? (browsing() && !atRoot() ? "< Up (..)" : "< Back") : names_[i - 1];
      s.drawString(label, 16, ry + (kRowH - kCharH) / 2);
      if (dir) s.drawString("/", 16 + static_cast<int>(strlen(label)) * kCharW, ry + (kRowH - kCharH) / 2);
    }
    s.setTextColor(kDim);
    if (listTop_ > 0) s.drawString("^", kScreenW - 24, top + 4);
    if (listTop_ + kListRows < count_ + 1) s.drawString("v", kScreenW - 24, top + (kListRows - 1) * kRowH + 4);
    return;
  }
  for (int a = 0; a < kActions; ++a) {
    if (a == kRetry && sd) break;
    const int ry = top + a * kActionH;
    const bool sel = a == sel_;
    if (sel) s.fillRect(0, ry, kScreenW, kActionH - 2, kSelBg);
    s.setTextColor(!enabled(a) ? kDim : (sel ? kCursor : kText));
    s.drawString(kLabels[a], 16, ry + (kActionH - 2 - kCharH) / 2);
  }
}

void FileScreen::drawHeader(LGFX_Sprite& s, int y0) {
  const bool sd = hw::sdReady();
  char buf[64];
  const int ty = y0 + (kHeaderH - 4 - kCharH) / 2;
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  s.setTextColor(kText);
  if (names_ && browsing()) {
    // Path tail, so the current folder stays visible.
    const char* prefix = wavList_ ? "IMPORT WAV " : "IMPORT ";
    const int fit = (kScreenW - 16 - 16 - 12 * kCharW) / kCharW - static_cast<int>(strlen(prefix));
    const char* dir = curDir();
    const int len = static_cast<int>(strlen(dir));
    snprintf(buf, sizeof(buf), "%s%s%s", prefix, len > fit ? "..." : "", dir + (len > fit ? len - fit + 3 : 0));
    s.drawString(buf, 16, ty);
  } else {
    snprintf(buf, sizeof(buf), "FILE  %s%s", app_.project().name, app_.projectDirty() ? "*" : "");
    s.drawString(buf, 16, ty);
  }
  if (!names_) {
    // Section switch.
    const bool focus = samples_ ? ssel_ == kSwitchRow : sel_ == kSectionSel;
    if (focus) s.fillRect(kSwitchX - 4, y0 + 2, kSwitchW + 8, kHeaderH - 8, kSelBg);
    s.setTextColor(samples_ ? kDim : (focus ? kCursor : kText));
    s.drawString("PROJECTS", kSwitchX, ty);
    s.setTextColor(kDim);
    s.drawString("|", kSwitchX + 9 * kCharW, ty);
    s.setTextColor(!samples_ ? kDim : (focus ? kCursor : kText));
    s.drawString("SAMPLES", kSwitchX + 11 * kCharW, ty);
  }
  s.setTextColor(sd ? kDim : kEditCursor);
  const char* sdText = sd ? "SD OK" : "NO SD CARD";
  s.drawString(sdText, kScreenW - 16 - static_cast<int>(strlen(sdText)) * kCharW, ty);
}

void FileScreen::setSection(bool samples) {
  samples_ = samples;
  // Focus stays on the switch, so turning the encoder continues in the new section.
  if (samples) {
    ssel_ = kSwitchRow;
    stop_ = 0;
  } else {
    sel_ = kSectionSel;
  }
  app_.invalidate();
}

// ---- SAMPLES ----

int FileScreen::sampleRowCount() const { return kFirstSample + (audio::bankMounted() ? audio::bank().count() : 0); }

bool FileScreen::sampleEnabled(int row) const {
  if (!audio::bankMounted()) return row == kSwitchRow;
  switch (row) {
    case kSwitchRow: return true;
    case kImportRow: return hw::sdReady();
    case kCompactRow: return audio::bank().count() > 0;
    default: return row < sampleRowCount();
  }
}

void FileScreen::sampleMove(int delta) {
  const int n = sampleRowCount() - kSwitchRow;  // rows kSwitchRow..sampleRowCount()-1
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

int FileScreen::usedBy(const char* sample) const {
  const mt::Project& p = app_.project();
  for (int i = 0; i < mt::kInstruments; ++i) {
    const mt::Instrument& in = p.instruments[i];
    if (in.type == mt::InstrType::Sample && strcasecmp(in.sample, sample) == 0) return i;
  }
  return -1;
}

void FileScreen::sampleRun(int row) {
  if (!sampleEnabled(row)) return;
  ssel_ = row;
  switch (row) {
    case kSwitchRow: setSection(false); return;
    case kImportRow:
      if (!playbackBusy()) openWavList();
      return;
    case kCompactRow:
      if (!playbackBusy()) doCompact();
      return;
    default: break;
  }
  const mt::BankEntry* e = audio::bank().entry(row - kFirstSample);
  if (!e) return;
  strlcpy(pending_, e->name, sizeof(pending_));
  const MenuItem items[] = {{"Cancel", kCancel}, {"Delete", kDeleteSample}};
  app_.menu().open(e->name, items, 2, [this](int id) { onMenu(id); });
}

void FileScreen::openWavList() {
  if (!names_) {
    names_ = static_cast<char(*)[hw::kNameMax]>(heap_caps_malloc(kMaxFiles * hw::kNameMax, MALLOC_CAP_SPIRAM));
    if (!names_) {
      app_.toast(storage::resultText(storage::Result::NoMemory));
      return;
    }
  }
  midiList_ = false;
  wavList_ = true;
  // The folder may have been removed (Wi-Fi, card swap): start over at /samples.
  if (!atRoot() && !hw::sdFs().exists(wavDir_)) strlcpy(wavDir_, "/samples", sizeof(wavDir_));
  // Subfolders first, then .wav (any case), both sorted; hidden entries (macOS ._*) are skipped.
  dirCount_ = hw::sdListDirs(wavDir_, names_, kMaxFiles);
  static constexpr const char* kWavExts[] = {".wav"};
  count_ = dirCount_ + hw::sdListFiles(wavDir_, kWavExts, 1, names_ + dirCount_, kMaxFiles - dirCount_);
  listSel_ = count_ > 0 ? 1 : 0;
  listTop_ = 0;
  dragAcc_ = 0;
  if (count_ == 0) app_.toast(atRoot() ? "NO /samples/*.wav" : "EMPTY FOLDER");
}

bool FileScreen::onPlay() {
  if (kb_.isOpen() || import_.isOpen() || wifi_.isOpen() || !names_ || !wavList_) return false;
  // Folders and Back: nothing, but Play does not start the transport in this list either.
  if (listSel_ > dirCount_ && listSel_ <= count_) togglePreview();
  return true;
}

void FileScreen::togglePreview() {
  const bool again = pvBuf_ && pvSel_ == listSel_ && audio::previewPlaying();
  stopPreview();
  if (again) return;
  char path[sizeof(wavDir_) + hw::kNameMax];
  const int n = snprintf(path, sizeof(path), "%s/%s", wavDir_, names_[listSel_ - 1]);
  if (n < 0 || n >= static_cast<int>(sizeof(path))) {
    app_.toast("PATH TOO LONG");
    return;
  }
  app_.showBusy("LOADING...");
  uint32_t frames = 0, rate = 0;
  const audio::BankResult r = audio::loadWavPreview(path, kPreviewMs, &pvBuf_, &frames, &rate);
  if (r != audio::BankResult::Ok) {
    if (r == audio::BankResult::OpenFail || r == audio::BankResult::ReadFail) hw::sdBegin();
    app_.toast(audio::bankResultText(r));
    return;
  }
  pvSel_ = listSel_;
  audio::previewBuffer(pvBuf_, frames, rate);
}

void FileScreen::stopPreview() {
  if (!pvBuf_) return;
  // Not acknowledged: the audio task may still read the buffer, so it is left allocated.
  if (audio::previewStop()) heap_caps_free(pvBuf_);
  pvBuf_ = nullptr;
  pvSel_ = -1;
}

void FileScreen::importAs(const char* initial) {
  kb_.open("SAMPLE NAME:", initial, [this](const char* text) {
    char nm[17];
    if (!storage::sanitize(text, nm)) {
      app_.toast("BAD NAME");
      importAs(text);
      return;
    }
    if (audio::bank().find(nm) >= 0) {
      strlcpy(pending_, nm, sizeof(pending_));
      const MenuItem items[] = {{"Cancel", kCancel}, {"Overwrite", kOverwriteSample}};
      char title[28];
      snprintf(title, sizeof(title), "%s EXISTS", nm);
      app_.menu().open(title, items, 2, [this](int id) { onMenu(id); });
      return;
    }
    doImport(nm);
  });
}

void FileScreen::progress(uint32_t done, uint32_t total, void* ctx) {
  FileScreen& f = *static_cast<FileScreen*>(ctx);
  const int pct = total ? static_cast<int>(static_cast<uint64_t>(done) * 100 / total) : 100;
  const uint32_t now = millis();
  if (pct == f.lastPct_ || (now - f.lastBusyMs_ < 200 && pct < 100)) return;
  f.lastPct_ = pct;
  f.lastBusyMs_ = now;
  snprintf(f.busyMsg_, sizeof(f.busyMsg_), "%s %d%%", f.busyLabel_, pct);
  f.app_.showBusy(f.busyMsg_);
}

void FileScreen::doImport(const char* name, bool replace) {
  if (playbackBusy()) return;
  char nm[17];
  strlcpy(nm, name, sizeof(nm));  // name may point into pending_
  char path[sizeof(wavDir_) + hw::kNameMax];
  const int n = snprintf(path, sizeof(path), "%s/%s", wavDir_, wavFile_);
  if (n < 0 || n >= static_cast<int>(sizeof(path))) {
    app_.toast("PATH TOO LONG");
    return;
  }
  busyLabel_ = "IMPORT";
  lastPct_ = -1;
  lastBusyMs_ = 0;
  app_.showBusy("IMPORT...");
  const audio::BankResult r = audio::importWav(path, nm, replace, progress, this);
  if (r != audio::BankResult::Ok) {
    if (r == audio::BankResult::OpenFail || r == audio::BankResult::ReadFail) hw::sdBegin();
    app_.toast(audio::bankResultText(r));
    return;
  }
  const int i = audio::bank().find(nm);
  if (i >= 0) sampleMove(kFirstSample + i - ssel_);
  char msg[32];
  snprintf(msg, sizeof(msg), "IMPORTED %s", nm);
  app_.toast(msg);
}

void FileScreen::doDelete(const char* name) {
  if (playbackBusy()) return;
  char nm[17];
  strlcpy(nm, name, sizeof(nm));
  app_.showBusy("DELETING...");
  const audio::BankResult r = audio::removeSample(audio::bank().find(nm));
  sampleMove(0);  // clamp to the shorter list
  if (r != audio::BankResult::Ok) {
    app_.toast(audio::bankResultText(r));
    return;
  }
  char msg[32];
  snprintf(msg, sizeof(msg), "DELETED %s", nm);
  app_.toast(msg);
}

void FileScreen::doCompact() {
  busyLabel_ = "COMPACT";
  lastPct_ = -1;
  lastBusyMs_ = 0;
  app_.showBusy("COMPACT...");
  const audio::BankResult r = audio::compactBank(progress, this);
  app_.toast(r == audio::BankResult::Ok ? "BANK COMPACTED" : audio::bankResultText(r));
}

void FileScreen::samplesInput(const hw::InputEvent& ev) {
  using hw::InputType;
  switch (ev.type) {
    case InputType::EncTurn: sampleMove(ev.delta); break;
    case InputType::EncClick: sampleRun(ssel_); break;
    default: break;
  }
}

void FileScreen::samplesTouch(const TouchEvent& ev) {
  const int rows = y0_ + kHeaderH + kInfoH;
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
  if (!audio::bankMounted()) {
    s.setTextColor(kEditCursor);
    s.drawString("NO SAMPLE BANK (flash partition)", 16, top + ty);
    return;
  }
  const mt::SampleBank& b = audio::bank();
  char buf[48];
  const uint32_t cap = b.capacity(), freeB = b.freeBytes();
  s.setTextColor(kDim);
  snprintf(buf, sizeof(buf), "FREE %u / %u KB", static_cast<unsigned>(freeB / 1024), static_cast<unsigned>(cap / 1024));
  s.drawString(buf, 16, top + ty);
  constexpr int kBarX = 272, kBarW = kScreenW - 16 - kBarX, kBarH = 10;
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
    const mt::BankEntry* e = b.entry(row - kFirstSample);
    if (!e) break;
    s.drawString(e->name, 16, yy);
    const uint32_t sec = e->rate ? (e->frames + e->rate / 2) / e->rate : 0;
    snprintf(buf, sizeof(buf), "%u:%02u", static_cast<unsigned>(sec / 60), static_cast<unsigned>(sec % 60));
    s.drawString(buf, 16 + 18 * kCharW, yy);
    snprintf(buf, sizeof(buf), "%u KB", static_cast<unsigned>((e->frames * 2 + 1023) / 1024));
    s.drawString(buf, kScreenW - 16 - static_cast<int>(strlen(buf)) * kCharW, yy);
  }
  s.setTextColor(kDim);
  if (stop_ > 0) s.drawString("^", kScreenW - 120, rowsTop + 4);
  if (stop_ + kSampleRows < total) s.drawString("v", kScreenW - 120, rowsTop + (kSampleRows - 1) * kRowH + 4);
}

}  // namespace ui
