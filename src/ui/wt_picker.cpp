#include "wt_picker.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "app.h"
#include "audio/audio.h"
#include "audio/bank.h"
#include "engine/engine.h"
#include "esp_heap_caps.h"
#include "sample_set.h"
#include "storage/storage.h"
#include "wt_builtin.h"

namespace ui {
namespace {

constexpr const char* kRoot = "/wavetables";

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

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
  const int16_t* fr = table + clampi(f, 0, mt::kWtFrames - 1) * mt::kWtFramePts;  // level 0
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

bool wtImportFile(App& app, const char* path, const char* base, char out[mt::kSampleNameMax + 1]) {
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
    if (r == audio::BankResult::OpenFail || r == audio::BankResult::ReadFail) hw::sdBegin();
    app.toast(audio::bankResultText(r));
    return false;
  }
  if (!uniqueName(p, nm, crc, out)) {
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

mt::Instrument& WtPicker::inst() { return app_.project().instruments[instr_]; }

void WtPicker::open(int instr, int osc) {
  if (open_) close(true);
  names_ = static_cast<char(*)[hw::kNameMax]>(heap_caps_malloc(kMaxEntries * hw::kNameMax, MALLOC_CAP_SPIRAM));
  if (!names_) {
    app_.toast(storage::resultText(storage::Result::NoMemory));
    return;
  }
  open_ = true;
  instr_ = instr;
  osc_ = osc & 1;
  strlcpy(backup_, inst().synWt[osc_], sizeof(backup_));
  seqBefore_ = seqAfter_ = app_.editSeq();
  changed_ = imported_ = false;
  listTables(backup_);
}

void WtPicker::close(bool keep, bool restore) {
  if (!open_) return;
  if (!keep && restore && changed_) {
    engine::lockProject();
    memcpy(inst().synWt[osc_], backup_, sizeof(backup_));
    engine::unlockProject();
    // Nothing else edited since the audition: the project is as clean as before.
    if (app_.editSeq() == seqAfter_ && !imported_) app_.rewindEditSeq(seqBefore_);
    else app_.markDirty();
  }
  heap_caps_free(names_);
  names_ = nullptr;
  count_ = 0;
  open_ = false;
  app_.invalidate();
  if (onClose_) onClose_();
}

void WtPicker::add(Kind k, const char* name) {
  if (count_ >= kMaxEntries) return;
  kinds_[count_] = k;
  strlcpy(names_[count_], name, hw::kNameMax);
  ++count_;
}

void WtPicker::listTables(const char* select) {
  files_ = false;
  count_ = 0;
  for (int i = 0; i < mt::kWtBuiltins; ++i) add(Kind::Table, mt::wtBuiltinName(i));
  const mt::Project& p = app_.project();
  for (int i = 0; i < p.wavetableCount; ++i) add(Kind::Table, p.wavetables[i].name);
  add(Kind::Import, "IMPORT...");
  sel_ = 0;
  picked_ = -1;
  for (int i = 0; select && select[0] && i < count_; ++i)
    if (kinds_[i] == Kind::Table && strcasecmp(names_[i], select) == 0) {
      sel_ = i;
      if (strcasecmp(inst().synWt[osc_], names_[i]) == 0) picked_ = i;
      break;
    }
  top_ = sel_ >= kRows ? sel_ - kRows + 1 : 0;
  dragAcc_ = 0;
  app_.invalidate();
}

void WtPicker::listFiles() {
  files_ = true;
  picked_ = -1;
  count_ = 0;
  // The folder may have been removed (Wi-Fi, card swap): start over at the root.
  if (strcmp(dir_, kRoot) != 0 && !(hw::sdReady() && hw::sdFs().exists(dir_))) strlcpy(dir_, kRoot, sizeof(dir_));
  add(Kind::Up, "..");
  int from = count_;
  count_ += hw::sdListDirs(dir_, names_ + count_, kMaxEntries - count_);
  for (int i = from; i < count_; ++i) kinds_[i] = Kind::Folder;
  from = count_;
  static constexpr const char* kWavExts[] = {".wav"};
  count_ += hw::sdListFiles(dir_, kWavExts, 1, names_ + count_, kMaxEntries - count_);
  for (int i = from; i < count_; ++i) kinds_[i] = Kind::File;
  // Names the card cannot give back (non-ASCII, shown as "?") would not open: drop them.
  int n = 1;
  for (int i = 1; i < count_; ++i) {
    if (strchr(names_[i], '?')) continue;
    if (n != i) {
      kinds_[n] = kinds_[i];
      memcpy(names_[n], names_[i], hw::kNameMax);
    }
    ++n;
  }
  count_ = n;
  sel_ = count_ > 1 ? 1 : 0;
  top_ = 0;
  dragAcc_ = 0;
  app_.invalidate();
}

void WtPicker::setTable(const char* name) {
  engine::lockProject();
  strlcpy(inst().synWt[osc_], name, sizeof(inst().synWt[osc_]));
  engine::unlockProject();
  app_.markDirty();
  seqAfter_ = app_.editSeq();
  changed_ = true;
}

void WtPicker::pick(int i) {
  if (files_ || i < 0 || i >= count_ || kinds_[i] != Kind::Table) return;
  setTable(names_[i]);
  picked_ = i;
  audio::preview(static_cast<uint8_t>(instr_), kPreviewNote);
}

void WtPicker::backToTables() {
  listTables(inst().synWt[osc_]);
  sel_ = count_ - 1;  // IMPORT...
  top_ = sel_ >= kRows ? sel_ - kRows + 1 : 0;
}

void WtPicker::up() {
  if (strcmp(dir_, kRoot) == 0) {
    backToTables();
    return;
  }
  char* slash = strrchr(dir_, '/');
  char left[hw::kNameMax];
  strlcpy(left, slash + 1, sizeof(left));
  *slash = 0;
  listFiles();
  for (int i = 0; i < count_; ++i)
    if (kinds_[i] == Kind::Folder && strcasecmp(names_[i], left) == 0) {
      sel_ = i;
      if (sel_ >= top_ + kRows) top_ = sel_ - kRows + 1;
      break;
    }
}

void WtPicker::importFile(int i) {
  char path[sizeof(dir_) + hw::kNameMax];
  const int n = snprintf(path, sizeof(path), "%s/%s", dir_, names_[i]);
  if (n < 0 || n >= static_cast<int>(sizeof(path))) {
    app_.toast("PATH TOO LONG");
    return;
  }
  char base[hw::kNameMax];
  strlcpy(base, names_[i], sizeof(base));
  if (char* dot = strrchr(base, '.')) *dot = 0;
  char nm[mt::kSampleNameMax + 1];
  if (!wtImportFile(app_, path, base, nm)) return;
  imported_ = true;
  listTables(nm);
  pick(sel_);
  char msg[32];
  snprintf(msg, sizeof(msg), "IMPORTED %s", nm);
  app_.toast(msg);
}

void WtPicker::choose(int i) {
  if (i < 0 || i >= count_) return;
  sel_ = i;
  switch (kinds_[i]) {
    case Kind::Table:
      if (picked_ != i) pick(i);
      close(true);
      return;
    case Kind::Import:
      if (!hw::sdReady()) {
        app_.toast(storage::resultText(storage::Result::NoSd));
        return;
      }
      listFiles();
      return;
    case Kind::Up: up(); return;
    case Kind::Folder: {
      int depth = 0;
      for (const char* p = dir_ + strlen(kRoot); *p; ++p) depth += *p == '/';
      if (depth >= kDepthMax) {
        app_.toast("TOO DEEP");
        return;
      }
      if (strlen(dir_) + 1 + strlen(names_[i]) >= sizeof(dir_)) {
        app_.toast("PATH TOO LONG");
        return;
      }
      strlcat(dir_, "/", sizeof(dir_));
      strlcat(dir_, names_[i], sizeof(dir_));
      listFiles();
      return;
    }
    case Kind::File: importFile(i); return;
  }
}

void WtPicker::moveSel(int d) {
  if (count_ == 0) return;
  const int prev = sel_;
  sel_ = clampi(sel_ + d, 0, count_ - 1);
  if (sel_ < top_) top_ = sel_;
  if (sel_ >= top_ + kRows) top_ = sel_ - kRows + 1;
  // Live audition: a table row is applied as soon as it is selected.
  if (sel_ != prev && !files_ && kinds_[sel_] == Kind::Table) pick(sel_);
}

void WtPicker::button(int b) {
  if (b == 0) {
    if (sel_ < count_) choose(sel_);
    else if (!files_) close(true);
    return;
  }
  if (files_) {
    backToTables();  // the folder is kept for the next IMPORT...
  } else {
    close(false);
  }
}

void WtPicker::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  switch (ev.type) {
    case InputType::EncTurn: moveSel(ev.delta); break;
    case InputType::EncClick: choose(sel_); break;
    case InputType::EncLong: button(1); break;
    default: break;
  }
}

void WtPicker::onTouch(const TouchEvent& ev) {
  const int top = y0_ + kHeaderH;
  if (ev.type == TouchType::Drag) {
    dragAcc_ += ev.dy;
    const int rows = dragAcc_ / kRowH;
    dragAcc_ -= rows * kRowH;
    top_ = clampi(top_ - rows, 0, count_ > kRows ? count_ - kRows : 0);
    return;
  }
  dragAcc_ = 0;
  if (ev.type != TouchType::Tap) return;
  if (ev.y < top) {
    for (int b = 0; b < 2; ++b)
      if (ev.x >= kBtnX[b] && ev.x < kBtnX1[b]) button(b);
    return;
  }
  const int i = top_ + (ev.y - top) / kRowH;
  if (i >= count_) return;
  // A tap auditions a table (OK keeps it); the other rows open.
  if (kinds_[i] == Kind::Table) {
    sel_ = i;
    pick(i);
    return;
  }
  choose(i);
}

void WtPicker::draw(LGFX_Sprite& s, int y0) {
  y0_ = y0;
  const int ty = y0 + (kHeaderH - 4 - kCharH) / 2;
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  char title[48];
  if (files_) {
    // Path tail when long.
    constexpr int kFit = (kBtnX[0] - 16) / kCharW;
    const int len = static_cast<int>(strlen(dir_));
    snprintf(title, sizeof(title), "%s%s", len > kFit ? "..." : "", dir_ + (len > kFit ? len - kFit + 3 : 0));
  } else {
    snprintf(title, sizeof(title), "OSC%d TABLE", osc_ + 1);
  }
  s.setTextColor(kText);
  s.drawString(title, 8, ty);
  const char* const labels[2] = {files_ ? "OPEN" : "OK", files_ ? "BACK" : "CANCEL"};
  for (int b = 0; b < 2; ++b) {
    const int w = kBtnX1[b] - kBtnX[b];
    s.fillRect(kBtnX[b], y0 + 2, w, kHeaderH - 8, kPlayBg);
    s.setTextColor(kCursor);
    s.drawString(labels[b], kBtnX[b] + (w - static_cast<int>(strlen(labels[b])) * kCharW) / 2, ty);
  }
  const int top = y0 + kHeaderH;
  const mt::WtSource* src = audio::wavetableSource();
  for (int r = 0; r < kRows; ++r) {
    const int i = top_ + r;
    if (i >= count_) break;
    const int ry = top + r * kRowH;
    const bool sel = i == sel_;
    const Kind k = kinds_[i];
    if (sel) s.fillRect(0, ry, kScreenW, kRowH, kSelBg);
    uint16_t c = kText;
    if (k == Kind::Up) c = kDim;
    else if (k == Kind::Import) c = kCyan;
    else if (k == Kind::Table && !(src && src->findWt(names_[i]))) c = kRed;  // not in the bank
    if (sel) c = kCursor;
    s.setTextColor(c);
    const int tyy = ry + (kRowH - kCharH) / 2;
    if (i == picked_) s.drawString(">", 4, tyy);  // set now
    s.drawString(names_[i], 16, tyy);
    if (k == Kind::Folder) s.drawString("/", 16 + static_cast<int>(strlen(names_[i])) * kCharW, tyy);
  }
  s.setTextColor(kDim);
  if (files_ && count_ <= 1)
    s.drawString(hw::sdReady() ? "NO WAV FILES" : "NO SD CARD", 16, top + kRowH + (kRowH - kCharH) / 2);
  if (top_ > 0) s.drawString("^", kScreenW - 24, top + 4);
  if (top_ + kRows < count_) s.drawString("v", kScreenW - 24, top + (kRows - 1) * kRowH + 4);
  // Frame 0 of the selected table.
  if (!files_ && sel_ < count_ && kinds_[sel_] == Kind::Table && src) {
    const int16_t* t = src->findWt(names_[sel_]);
    if (t) {
      s.drawRect(kPvX0 - 4, top + kPvY0 - 4, kPvX1 - kPvX0 + 8, kPvY1 - kPvY0 + 8, kDim);
      drawWtFrame(s, t, 0, kPvX0, top + kPvY0, kPvX1, top + kPvY1, kGreen);
    }
  }
}

}  // namespace ui
