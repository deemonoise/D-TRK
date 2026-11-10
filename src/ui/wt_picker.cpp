#include "wt_picker.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "app.h"
#include "file_rules.h"
#include "audio/audio.h"
#include "audio/bank.h"
#include "engine/engine.h"
#include "esp_heap_caps.h"
#include "sample_set.h"
#include "storage/storage.h"
#include "storage/wt_source_file.h"
#include "wt_builtin.h"
#include "wt_make.h"
#include "project_io.h"

namespace ui {
namespace {

constexpr const char* kRoot = "/wavetables";

void importProgress(uint32_t done, uint32_t total, void* ctx) {
  static_cast<App*>(ctx)->showProgress("IMPORT", done, total);
}

// out = base, or base cut + "-N" when base names another table; false when none fits (list full).
bool uniqueName(const mt::Project& p, const char* base, uint32_t crc, char out[mt::kSampleNameMax + 1]) {
  for (int n = 1; n < 100; ++n) {
    if (n == 1) {
      strlcpy(out, base, mt::kSampleNameMax + 1);
    } else {
      char suf[4];
      snprintf(suf, sizeof(suf), "-%d", n);
      const int keep = mt::kSampleNameMax - static_cast<int>(strlen(suf));
      snprintf(out, mt::kSampleNameMax + 1, "%.*s%s", keep, base, suf);
    }
    const int i = mt::projWtFind(p, out);
    if (i < 0) return p.wavetableCount < mt::kProjWavetables;
    if (p.wavetables[i].crc == crc) return true;
  }
  return false;
}

}  // namespace

void drawWtFrame(LGFX_Sprite& s, const int16_t* table, int f, int x0, int y0, int x1, int y1, uint16_t c) {
  if (!table) return;
  const int16_t* fr = table + (f < 0 ? 0 : f >= mt::kWtFrames ? mt::kWtFrames - 1 : f) * mt::kWtFramePts;  // level 0
  const int w = x1 - x0, mid = (y0 + y1) / 2, half = (y1 - y0) / 2;
  auto yAt = [&](int i) { return mid - fr[i] * half / 32768; };
  int px = x0, py = yAt(0);
  for (int x = 1; x < w; ++x) {
    const int ny = yAt(x * mt::kWtFrameLen / w);
    s.drawLine(px, py, x0 + x, ny, c);
    px = x0 + x;
    py = ny;
  }
}

bool wtImportFile(App& app, const char* path, const char* base, char out[mt::kSampleNameMax + 1], bool replaceShared) {
  const engine::Status& st = app.status();
  if (st.playing || st.paused) {
    app.toast(audio::bankResultText(audio::BankResult::Busy));
    return false;
  }
  if (!hw::sdReady()) {
    app.toast(audio::bankResultText(audio::BankResult::NoSd));
    return false;
  }
  char nm[mt::kSampleNameMax + 1];
  if (!storage::sanitize(base, nm)) strlcpy(nm, "WT", sizeof(nm));
  app.showBusy("IMPORT...");
  mt::Project& p = app.project();
  uint32_t crc = 0;
  const audio::BankResult r = audio::importWtToCache(path, p, crc, nullptr, importProgress, &app);
  if (r != audio::BankResult::Ok) {
    if (r == audio::BankResult::OpenFail || r == audio::BankResult::ReadFail) hw::sdRecover();
    app.toast(audio::bankResultText(r));
    return false;
  }
  if (replaceShared) strlcpy(out, nm, mt::kSampleNameMax + 1);
  if (!replaceShared && !uniqueName(p, nm, crc, out)) {
    app.toast("TABLE LIST FULL");
    return false;
  }
  engine::lockProject();
  const int i = mt::projWtSet(p, out, crc);
  engine::unlockProject();
  if (i < 0) {
    app.toast("TABLE LIST FULL");
    return false;
  }
  app.markDirty();
  return true;
}

bool wtSaveSourceFile(App& app, const char* path, const char* name, const int16_t* source,
                      char out[mt::kSampleNameMax + 1], bool replaceShared) {
  const auto st=engine::status();
  if (st.playing || st.paused) { app.toast("STOP PLAYBACK FIRST"); return false; }
  if (!hw::sdReady()) { app.toast("NO SD CARD"); return false; }
  if (mt::projWtFind(app.project(),name)<0 && app.project().wavetableCount>=mt::kProjWavetables) {
    app.toast("TABLE LIST FULL"); return false;
  }
  struct ImportContext {
    App& app;
    const char* name;
    char* out;
    bool replaceShared;
  } context{app,name,out,replaceShared};
  const auto import = [](const char* file, void* value) {
    auto& context = *static_cast<ImportContext*>(value);
    return wtImportFile(context.app,file,context.name,context.out,context.replaceShared);
  };
  const auto result = storage::saveWtSourceFile(path,source,import,&context);
  if (result == storage::WtSourceSaveResult::Ok) return true;
  if (result == storage::WtSourceSaveResult::RecoveryNeeded) app.toast("FILE RECOVERY: .BAK KEPT");
  else if (result == storage::WtSourceSaveResult::ReadbackFail) app.toast("WT VERIFY FAILED");
  else app.toast("WT WRITE / IMPORT FAILED");
  return false;
}

mt::Instrument& WtPicker::inst() { return app_.project().instruments[instr_]; }

void WtPicker::open(int instr, int osc) {
  if (open_) close();
  if (!list_.alloc()) return;
  open_ = true;
  instr_ = instr;
  osc_ = osc & 1;
  listTables(inst().synWt[osc_]);
}

void WtPicker::close() {
  draw_.close();
  if (!open_) return;
  if (files_) list_.cursorName(cursor_, sizeof(cursor_));
  list_.free();
  open_ = false;
  app_.invalidate();
  if (onClose_) onClose_();
}

void WtPicker::listTables(const char* select) {
  files_ = false;
  list_.clear();
  list_.setBack(true);
  list_.setPreviewDir(nullptr);
  list_.setNameRight(kPvX0 - 8);
  for (int i = 0; i < mt::kWtBuiltins; ++i) list_.add(FileList::Kind::File, mt::wtBuiltinName(i), uint8_t(Kind::Table));
  const mt::Project& p = app_.project();
  for (int i = 0; i < p.wavetableCount; ++i) list_.add(FileList::Kind::File, p.wavetables[i].name, uint8_t(Kind::Table));
  list_.add(FileList::Kind::Action, "DRAW...", uint8_t(Kind::Draw));
  list_.add(FileList::Kind::Action, "IMPORT...", uint8_t(Kind::Import));
  list_.color = [this](int i) {
    const Kind k = static_cast<Kind>(list_.tag(i));
    if (k == Kind::Import || k == Kind::Draw) return kCyan;
    return (audio::wavetableSource() && audio::wavetableSource()->findWt(list_.name(i))) ? kText : kRed;  // red: not in the bank
  };
  list_.mark = [this](int i) {  // set now
    return static_cast<Kind>(list_.tag(i)) == Kind::Table && strcasecmp(inst().synWt[osc_], list_.name(i)) == 0;
  };
  list_.finish(select);
  app_.invalidate();
}

void WtPicker::listFiles(const char* cursor) {
  files_ = true;
  // The folder may have been removed (Wi-Fi, card swap): start over at the root.
  if (strcmp(dir_, kRoot) != 0 && !(hw::sdReady() && hw::sdFs().exists(dir_))) strlcpy(dir_, kRoot, sizeof(dir_));
  list_.clear();
  list_.color = nullptr;
  list_.mark = nullptr;
  list_.setNameRight(FileList::kMarkX - 8);
  // Subfolders first, then .wav (any case), both sorted.
  static constexpr const char* kWavExts[] = {".wav"};
  list_.addFolder(dir_, kWavExts, 1);
  list_.setBack(strcmp(dir_, kRoot) == 0);  // "< Back": to the tables
  list_.setPreviewDir(dir_);
  list_.finish(cursor);
  if (list_.count() == 0 && strcmp(dir_, kRoot) == 0 && hw::sdReady()) app_.toast("NO /wavetables/*.wav");
  app_.invalidate();
}

void WtPicker::setTable(const char* name) {
  if (strcasecmp(inst().synWt[osc_], name) == 0) return;
  engine::lockProject();
  strlcpy(inst().synWt[osc_], name, sizeof(inst().synWt[osc_]));
  engine::unlockProject();
  app_.markDirty();
}

void WtPicker::back() {
  if (!files_) {
    close();
    return;
  }
  char left[hw::kNameMax];
  if (!mt::browseUp(dir_, kRoot, left, sizeof(left))) {  // to the tables, the cursor on IMPORT...
    list_.cursorName(cursor_, sizeof(cursor_));
    listTables(nullptr);
    list_.select(list_.rows() - 1);
    return;
  }
  listFiles(left);  // the cursor on the folder we came from
}

void WtPicker::importFile(int i) {
  char path[sizeof(dir_) + hw::kNameMax];
  const int n = snprintf(path, sizeof(path), "%s/%s", dir_, list_.name(i));
  if (n < 0 || n >= static_cast<int>(sizeof(path))) {
    app_.toast("PATH TOO LONG");
    return;
  }
  char base[hw::kNameMax];
  strlcpy(base, list_.name(i), sizeof(base));
  if (char* dot = strrchr(base, '.')) *dot = 0;
  char nm[mt::kSampleNameMax + 1];
  if (!wtImportFile(app_, path, base, nm)) return;
  setTable(nm);
  char msg[32];
  snprintf(msg, sizeof(msg), "IMPORTED %s", nm);
  app_.toast(msg);
  close();
}

void WtPicker::choose() {
  list_.previewStop();
  const int i = list_.entry();
  if (i < 0 || i >= list_.count()) {
    back();
    return;
  }
  if (files_) {
    if (list_.kind(i) == FileList::Kind::File) {
      importFile(i);
      return;
    }
    switch (mt::browseInto(dir_, sizeof(dir_), kRoot, list_.name(i), FileList::kDepthMax)) {
      case mt::BrowseResult::TooDeep: app_.toast("TOO DEEP"); return;
      case mt::BrowseResult::TooLong: app_.toast("PATH TOO LONG"); return;
      default: listFiles(nullptr); return;
    }
  }
  switch (static_cast<Kind>(list_.tag(i))) {
    case Kind::Import:
      if (!hw::sdReady()) {
        app_.toast(storage::resultText(storage::Result::NoSd));
        return;
      }
      listFiles(cursor_);
      return;
    case Kind::Draw: {
      const MenuItem items[]={{"Edit current WT",0},{"New wave",1},{"New morph",2}};
      app_.menu().open("DRAW WT",items,3,[this](int choice) {
        openDraw(choice==0?inst().synWt[osc_]:"",choice==2);
      });
      return;
    }
    default:
      setTable(list_.name(i));
      close();
      return;
  }
}

void WtPicker::openDraw(const char* source,bool morph) {
  list_.free();
  if (!draw_.open(source,[this](const char* nm) {
    setTable(nm);
    char msg[32]; snprintf(msg,sizeof(msg),"WT %s",nm);
    app_.toast(msg); close();
  },morph)) {
    if (list_.alloc()) listTables("DRAW..."); else close();
  }
}

void WtPicker::act(FileList::Act a) {
  if (a == FileList::Act::Back) {
    list_.previewStop();
    back();
  } else if (a == FileList::Act::Open) {
    choose();
  }
}

void WtPicker::onInput(const hw::InputEvent& ev) {
  if (draw_.isOpen()) {
    draw_.onInput(ev);
    if (open_ && !draw_.isOpen()) { if (list_.alloc()) listTables("DRAW..."); else close(); }
    return;
  }
  if (!list_.ready()) { if (!list_.alloc()) { close(); return; } listTables("DRAW..."); }
  act(list_.onInput(ev));
}

bool WtPicker::onPlay() {
  if (!open_) return false;
  if (draw_.isOpen()) return draw_.onPlay();
  return list_.onPlay();  // the tables: no preview, Play starts the transport
}

void WtPicker::onTouch(const TouchEvent& ev) {
  if (draw_.isOpen()) {
    draw_.onTouch(ev);
    if (open_ && !draw_.isOpen()) { if (list_.alloc()) listTables("DRAW..."); else close(); }
    return;
  }
  if (!list_.ready()) { if (!list_.alloc()) { close(); return; } listTables("DRAW..."); }
  act(list_.onTouch(ev, y0_ + kHeaderH));
}

void WtPicker::draw(LGFX_Sprite& s, int y0) {
  y0_ = y0;
  if (draw_.isOpen()) {
    draw_.draw(s, y0);
    return;
  }
  if (!list_.ready()) {
    if (!list_.alloc()) { close(); return; }
    listTables("DRAW...");
  }
  if (files_) {
    FileList::drawHeader(s, y0, "IMPORT WT", dir_);
  } else {
    char title[16];
    snprintf(title, sizeof(title), "OSC%d TABLE", osc_ + 1);
    FileList::drawHeader(s, y0, title, nullptr);
  }
  const int top = y0 + kHeaderH;
  list_.draw(s, top);
  // Frame 0 of the selected table.
  const int i = list_.entry();
  if (!files_ && i >= 0 && static_cast<Kind>(list_.tag(i)) == Kind::Table) {
    const mt::WtSource* source = audio::wavetableSource();
    const int16_t* table = source ? source->findWt(list_.name(i)) : nullptr;
    if (table) {
      s.drawRect(kPvX0 - 4, top + kPvY0 - 4, kPvX1 - kPvX0 + 8, kPvY1 - kPvY0 + 8, kDim);
      drawWtFrame(s, table, 0, kPvX0, top + kPvY0, kPvX1, top + kPvY1, kGreen);
    }
  }
}

}  // namespace ui
