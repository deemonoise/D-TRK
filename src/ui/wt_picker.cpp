#include "wt_picker.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "app.h"
#include "audio/audio.h"
#include "link/bank_client.h"
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

void drawWtFrame(LGFX_Sprite& s, const int8_t* pts, int x0, int y0, int x1, int y1, uint16_t c) {
  if (!pts) return;
  const int w = x1 - x0, mid = (y0 + y1) / 2, half = (y1 - y0) / 2;
  auto yAt = [&](int i) { return mid - pts[i] * half / 128; };
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
  const audio::BankResult r = audio::importWtToCache(path, crc, nullptr, importProgress, &app);
  if (r != audio::BankResult::Ok) {
    if (r == audio::BankResult::OpenFail || r == audio::BankResult::ReadFail) hw::sdRecover();
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
  if (open_) close();
  names_ = static_cast<char(*)[hw::kNameMax]>(heap_caps_malloc(kMaxEntries * hw::kNameMax, MALLOC_CAP_SPIRAM));
  if (!names_) {
    app_.toast(storage::resultText(storage::Result::NoMemory));
    return;
  }
  open_ = true;
  instr_ = instr;
  osc_ = osc & 1;
  listTables(inst().synWt[osc_]);
}

void WtPicker::close() {
  if (!open_) return;
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

void WtPicker::setSel(int row) {
  sel_ = clampi(row, 0, rowCount() - 1);
  if (sel_ < top_) top_ = sel_;
  if (sel_ >= top_ + kRows) top_ = sel_ - kRows + 1;
}

void WtPicker::listTables(const char* select) {
  files_ = false;
  count_ = 0;
  for (int i = 0; i < mt::kWtBuiltins; ++i) add(Kind::Table, mt::wtBuiltinName(i));
  const mt::Project& p = app_.project();
  for (int i = 0; i < p.wavetableCount; ++i) add(Kind::Table, p.wavetables[i].name);
  add(Kind::Import, "IMPORT...");
  top_ = 0;
  dragAcc_ = 0;
  int row = 1;
  for (int i = 0; select && select[0] && i < count_; ++i)
    if (kinds_[i] == Kind::Table && strcasecmp(names_[i], select) == 0) {
      row = i + 1;
      break;
    }
  setSel(row);
  app_.invalidate();
}

void WtPicker::listFiles() {
  files_ = true;
  count_ = 0;
  // The folder may have been removed (Wi-Fi, card swap): start over at the root.
  if (strcmp(dir_, kRoot) != 0 && !(hw::sdReady() && hw::sdFs().exists(dir_))) strlcpy(dir_, kRoot, sizeof(dir_));
  // Subfolders first, then .wav (any case), both sorted.
  count_ = hw::sdListDirs(dir_, names_, kMaxEntries);
  for (int i = 0; i < count_; ++i) kinds_[i] = Kind::Folder;
  const int from = count_;
  static constexpr const char* kWavExts[] = {".wav"};
  count_ += hw::sdListFiles(dir_, kWavExts, 1, names_ + count_, kMaxEntries - count_);
  for (int i = from; i < count_; ++i) kinds_[i] = Kind::File;
  // Names the card cannot give back (non-ASCII, shown as "?") would not open: drop them.
  int n = 0;
  for (int i = 0; i < count_; ++i) {
    if (strchr(names_[i], '?')) continue;
    if (n != i) {
      kinds_[n] = kinds_[i];
      memcpy(names_[n], names_[i], hw::kNameMax);
    }
    ++n;
  }
  count_ = n;
  top_ = 0;
  dragAcc_ = 0;
  setSel(count_ > 0 ? 1 : 0);
  if (count_ == 0) app_.toast(strcmp(dir_, kRoot) == 0 ? "NO /wavetables/*.wav" : "EMPTY FOLDER");
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
  if (strcmp(dir_, kRoot) == 0) {  // to the tables, the cursor on IMPORT...
    listTables(nullptr);
    setSel(rowCount() - 1);
    return;
  }
  char* slash = strrchr(dir_, '/');
  char left[hw::kNameMax];
  strlcpy(left, slash + 1, sizeof(left));
  *slash = 0;
  listFiles();
  // The cursor on the folder we came from.
  for (int i = 0; i < count_; ++i)
    if (kinds_[i] == Kind::Folder && strcasecmp(names_[i], left) == 0) {
      setSel(i + 1);
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
  setTable(nm);
  char msg[32];
  snprintf(msg, sizeof(msg), "IMPORTED %s", nm);
  app_.toast(msg);
  close();
}

void WtPicker::choose(int row) {
  if (row <= 0 || row > count_) {
    back();
    return;
  }
  const int i = row - 1;
  switch (kinds_[i]) {
    case Kind::Table:
      setTable(names_[i]);
      close();
      return;
    case Kind::Import:
      if (!hw::sdReady()) {
        app_.toast(storage::resultText(storage::Result::NoSd));
        return;
      }
      listFiles();
      return;
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
  const int n = rowCount();
  setSel(((sel_ + d) % n + n) % n);
}

void WtPicker::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  switch (ev.type) {
    case InputType::EncTurn: moveSel(ev.delta); break;
    case InputType::EncClick: choose(sel_); break;
    case InputType::EncLong: back(); break;
    default: break;
  }
}

void WtPicker::onTouch(const TouchEvent& ev) {
  const int top = y0_ + kHeaderH;
  if (ev.type == TouchType::Drag) {
    dragAcc_ += ev.dy;
    const int rows = dragAcc_ / kRowH;
    dragAcc_ -= rows * kRowH;
    top_ = clampi(top_ - rows, 0, rowCount() > kRows ? rowCount() - kRows : 0);
    return;
  }
  dragAcc_ = 0;
  if (ev.type != TouchType::Tap || ev.y < top) return;
  const int row = top_ + (ev.y - top) / kRowH;
  if (row >= rowCount()) return;
  // Tables: the first tap selects (frame preview), a tap on the selected one sets it; the other
  // rows open at once, as in FILE.
  if (row > 0 && kinds_[row - 1] == Kind::Table && row != sel_) {
    setSel(row);
    return;
  }
  sel_ = row;
  choose(row);
}

void WtPicker::draw(LGFX_Sprite& s, int y0) {
  y0_ = y0;
  const int ty = y0 + (kHeaderH - 4 - kCharH) / 2;
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  s.setTextColor(kText);
  char title[64];
  if (files_) {
    // Path tail, so the current folder stays visible.
    static constexpr const char* kPrefix = "IMPORT WT ";
    const int fit = (kScreenW - 16 - 16 - 12 * kCharW) / kCharW - static_cast<int>(strlen(kPrefix));
    const int len = static_cast<int>(strlen(dir_));
    snprintf(title, sizeof(title), "%s%s%s", kPrefix, len > fit ? "..." : "", dir_ + (len > fit ? len - fit + 3 : 0));
  } else {
    snprintf(title, sizeof(title), "OSC%d TABLE", osc_ + 1);
  }
  s.drawString(title, 16, ty);
  const bool sd = hw::sdReady();
  s.setTextColor(sd ? kDim : kEditCursor);
  const char* sdText = sd ? "SD OK" : "NO SD CARD";
  s.drawString(sdText, kScreenW - 16 - static_cast<int>(strlen(sdText)) * kCharW, ty);

  const int top = y0 + kHeaderH;
  for (int r = 0; r < kRows; ++r) {
    const int row = top_ + r;
    if (row >= rowCount()) break;
    const int ry = top + r * kRowH;
    const int tyy = ry + (kRowH - kCharH) / 2;
    const bool sel = row == sel_;
    if (sel) s.fillRect(0, ry, kScreenW, kRowH, kSelBg);
    if (row == 0) {
      s.setTextColor(sel ? kCursor : kDim);
      s.drawString(files_ && strcmp(dir_, kRoot) != 0 ? "< Up (..)" : "< Back", 16, tyy);
      continue;
    }
    const int i = row - 1;
    const Kind k = kinds_[i];
    uint16_t c = kText;
    if (k == Kind::Import) c = kCyan;
    else if (k == Kind::Table && !audio::wtCached(names_[i])) c = kRed;  // not in the bank
    s.setTextColor(sel ? kCursor : c);
    if (k == Kind::Table && strcasecmp(inst().synWt[osc_], names_[i]) == 0) s.drawString(">", 4, tyy);  // set now
    s.drawString(names_[i], 16, tyy);
    if (k == Kind::Folder) s.drawString("/", 16 + static_cast<int>(strlen(names_[i])) * kCharW, tyy);
  }
  s.setTextColor(kDim);
  if (top_ > 0) s.drawString("^", kScreenW - 24, top + 4);
  if (top_ + kRows < rowCount()) s.drawString("v", kScreenW - 24, top + (kRows - 1) * kRowH + 4);
  // Frame 0 of the selected table.
  if (!files_ && sel_ > 0 && kinds_[sel_ - 1] == Kind::Table) {
    int8_t pts[mt::kWtFrameLen];
    if (audio::wtFrame(names_[sel_ - 1], 0, pts)) {
      s.drawRect(kPvX0 - 4, top + kPvY0 - 4, kPvX1 - kPvX0 + 8, kPvY1 - kPvY0 + 8, kDim);
      drawWtFrame(s, pts, kPvX0, top + kPvY0, kPvX1, top + kPvY1, kGreen);
    }
  }
}

}  // namespace ui
