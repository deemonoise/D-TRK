#include "file_screen.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "app.h"
#include "esp_heap_caps.h"
#include "storage/storage.h"

namespace ui {
namespace {

constexpr const char* kLabels[] = {"Save", "Save As...", "Load...", "New", "Import MIDI...", "Retry"};

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// I/O errors may mean the card was pulled: remount so the screen shows the real state.
void reprobe(storage::Result r) {
  if (r == storage::Result::WriteFail || r == storage::Result::ReadFail) hw::sdBegin();
}

}  // namespace

void FileScreen::onEnter() {
  kb_.close();
  import_.close();
  closeList();
  if (!enabled(sel_)) moveSel(1);
}

void FileScreen::onLeave() {
  kb_.close();
  import_.close();  // frees the file / note buffers
  closeList();
}

bool FileScreen::enabled(int a) const {
  const bool sd = hw::sdReady();
  switch (a) {
    case kRetry: return !sd;
    default: return sd;
  }
}

void FileScreen::moveSel(int delta) {
  const int dir = delta >= 0 ? 1 : -1;
  int n = delta == 0 ? 1 : (delta > 0 ? delta : -delta);
  int i = sel_;
  for (int guard = 0; guard < kActions * 2 && n > 0; ++guard) {
    i = (i + dir + kActions) % kActions;
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
    default: break;
  }
  app_.invalidate();
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
  // Projects: only names that load back unchanged; others would be renamed by sanitize().
  // MIDI: .mid / .MID (the extension match ignores case).
  count_ = midi ? hw::sdList("/midi", ".mid", names_, kMaxFiles)
                : hw::sdList("/projects", ".mtp", names_, kMaxFiles, storage::validName);
  listSel_ = count_ > 0 ? 1 : 0;
  listTop_ = 0;
  dragAcc_ = 0;
  if (count_ == 0) app_.toast(midi ? "NO /midi FILES" : "NO PROJECTS");
}

void FileScreen::closeList() {
  heap_caps_free(names_);
  names_ = nullptr;
  count_ = 0;
}

void FileScreen::chooseFile(int idx) {
  if (idx <= 0 || idx > count_) {
    closeList();
    return;
  }
  if (midiList_) {
    // Keep the list on failure so another file can be picked.
    if (import_.open(names_[idx - 1])) closeList();
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
  if (names_) {
    switch (ev.type) {
      case InputType::EncTurn:
        listSel_ = clampi(listSel_ + ev.delta, 0, count_);
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
  const int top = y0_ + kHeaderH;
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
  const mt::Project& p = app_.project();
  const bool sd = hw::sdReady();
  char buf[40];
  const int ty = y0 + (kHeaderH - 4 - kCharH) / 2;
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  s.setTextColor(kText);
  snprintf(buf, sizeof(buf), "FILE  %s%s", p.name, app_.projectDirty() ? "*" : "");
  s.drawString(buf, 16, ty);
  s.setTextColor(sd ? kDim : kEditCursor);
  const char* sdText = sd ? "SD OK" : "NO SD CARD";
  s.drawString(sdText, kScreenW - 16 - static_cast<int>(strlen(sdText)) * kCharW, ty);

  const int top = y0 + kHeaderH;
  if (names_) {
    for (int r = 0; r < kListRows; ++r) {
      const int i = listTop_ + r;
      if (i > count_) break;
      const int ry = top + r * kRowH;
      const bool sel = i == listSel_;
      if (sel) s.fillRect(0, ry, kScreenW, kRowH, kSelBg);
      s.setTextColor(sel ? kCursor : (i == 0 ? kDim : kText));
      s.drawString(i == 0 ? "< Back" : names_[i - 1], 16, ry + (kRowH - kCharH) / 2);
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

}  // namespace ui
