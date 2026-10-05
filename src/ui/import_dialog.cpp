#include "import_dialog.h"
#include <new>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "esp_heap_caps.h"
#include "hw/sdcard.h"
#include "names.h"
#include "note_name.h"
#include "storage/storage.h"

namespace ui {
namespace {

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
void onOff(bool v, char* out, int n) { snprintf(out, n, "%s", v ? "ON" : "OFF"); }

constexpr const char* kMonoNames[] = {"Highest", "Lowest", "First"};

// Same rounding and clamp as mt::importSmf (usPerQ > 0).
unsigned fileBpm(uint32_t usPerQ) {
  const uint32_t bpm = (60000000u + usPerQ / 2) / usPerQ;
  return bpm < 20 ? 20 : (bpm > 300 ? 300 : bpm);
}

template <class T>
T* allocPsram(size_t n = 1) {
  return static_cast<T*>(heap_caps_malloc(sizeof(T) * n, MALLOC_CAP_SPIRAM));
}

// Temporary Project in PSRAM for the import (live is only touched by commit()).
mt::Project* allocProject() {
  void* m = allocPsram<mt::Project>();
  return m ? new (m) mt::Project() : nullptr;
}

void freeProject(mt::Project* p) {
  if (!p) return;
  p->~Project();
  heap_caps_free(p);
}

}  // namespace

// Kept off the stack and out of internal RAM (SmfInfo alone is ~760 bytes).
struct ImportDialog::State {
  mt::SmfInfo info;
  mt::ImportMap map;
  Param params[kMaxRows];
  char labels[mt::kSmfMaxSources][24];
  char file[hw::kNameMax];
  bool cut = false;  // file truncated / too many notes
  // Dry run shown in the confirmation and committed as is; freed by close() / the next dry run.
  mt::Project* tmp = nullptr;
  mt::ImportResult result{};
};

bool ImportDialog::open(const char* dir, const char* name) {
  close();
  void* m = allocPsram<State>();
  notes_ = allocPsram<mt::SmfNote>(kNoteCap);
  if (!m || !notes_) {
    heap_caps_free(m);
    close();
    app_.toast(storage::resultText(storage::Result::NoMemory));
    return false;
  }
  state_ = new (m) State();
  strlcpy(state_->file, name, sizeof(state_->file));
  if (char* dot = strrchr(state_->file, '.')) *dot = 0;  // title without the extension
  if (!readAndParse(dir, name)) {
    close();
    return false;
  }
  buildRows();
  return true;
}

void ImportDialog::close() {
  list_.setParams(nullptr, 0);
  list_.setEdit(false);
  if (state_) {
    freeProject(state_->tmp);
    state_->~State();
    heap_caps_free(state_);
    state_ = nullptr;
  }
  heap_caps_free(notes_);
  notes_ = nullptr;
  noteCount_ = 0;
  importRow_ = cancelRow_ = -1;
}

bool ImportDialog::readAndParse(const char* dir, const char* name) {
  if (!hw::sdReady()) {
    app_.toast(storage::resultText(storage::Result::NoSd));
    return false;
  }
  char path[260];
  snprintf(path, sizeof(path), "%s/%s", dir, name);
  app_.showBusy("READING...");
  fs::File f = hw::sdFs().open(path, FILE_READ);
  if (!f) {
    hw::sdBegin();  // the card may have been pulled
    app_.toast(storage::resultText(storage::Result::ReadFail));
    return false;
  }
  const size_t size = f.size();
  if (size > kMaxFileSize) {
    f.close();
    app_.toast("FILE TOO BIG (>512K)");
    return false;
  }
  uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(size > 0 ? size : 1, MALLOC_CAP_SPIRAM));
  if (!buf) {
    f.close();
    app_.toast(storage::resultText(storage::Result::NoMemory));
    return false;
  }
  size_t got = 0;
  while (got < size) {
    const size_t r = f.read(buf + got, size - got);
    if (r == 0 || r > size - got) break;
    got += r;
  }
  f.close();
  if (got != size) {
    heap_caps_free(buf);
    hw::sdBegin();
    app_.toast(storage::resultText(storage::Result::ReadFail));
    return false;
  }
  const mt::SmfErr e = mt::parseSmf(buf, size, state_->info, notes_, kNoteCap);
  heap_caps_free(buf);  // notes and info are all the import needs
  noteCount_ = state_->info.noteCount < kNoteCap ? state_->info.noteCount : kNoteCap;
  switch (e) {
    case mt::SmfErr::Ok: break;
    case mt::SmfErr::Truncated:
    case mt::SmfErr::TooManyNotes: state_->cut = true; break;
    case mt::SmfErr::Smpte: app_.toast("SMPTE TIME NOT SUPPORTED"); return false;
    case mt::SmfErr::Unsupported: app_.toast("UNSUPPORTED MIDI FORMAT"); return false;
    default: app_.toast("NOT A MIDI FILE"); return false;
  }
  bool any = false;
  for (int s = 0; s < state_->info.sourceCount && s < mt::kSmfMaxSources; ++s) any |= state_->info.src[s].count > 0;
  if (!any || noteCount_ == 0) {
    app_.toast("NO NOTES IN FILE");
    return false;
  }
  if (e == mt::SmfErr::Truncated) app_.toast("FILE TRUNCATED, PARTIAL");
  else if (e == mt::SmfErr::TooManyNotes) app_.toast("TOO MANY NOTES, CUT");
  return true;
}

void ImportDialog::buildRows() {
  State& st = *state_;
  mt::ImportMap& map = st.map;
  Param* p = st.params;
  int r = 0;
  int track = 0;
  for (int s = 0; s < st.info.sourceCount && s < mt::kSmfMaxSources; ++s) {
    const mt::SmfSource& src = st.info.src[s];
    if (src.count == 0) continue;
    if (track < mt::kTracks) map.target[s] = static_cast<int8_t>(track++);  // default: in order
    // map.useSourceChannel stays OFF by default on purpose: tracks keep their own channel.
    if (src.name[0]) snprintf(st.labels[s], sizeof(st.labels[s]), "%.12s ch%u", src.name, src.channel + 1);
    else snprintf(st.labels[s], sizeof(st.labels[s]), "Trk%u ch%u", src.track + 1, src.channel + 1);
    p[r++] = {st.labels[s],
              [this, s](char* o, int n) {
                const mt::SmfSource& sc = state_->info.src[s];
                const int t = state_->map.target[s];
                char tr[6], lo[4], hi[4];
                if (t < 0) snprintf(tr, sizeof(tr), "skip");
                else snprintf(tr, sizeof(tr), "T%d", t + 1);
                mt::noteName(sc.lo, lo);
                mt::noteName(sc.hi, hi);
                snprintf(o, n, "%-4s %5u %s..%s", tr, sc.count, lo, hi);
              },
              [this, s](int d) {
                state_->map.target[s] = static_cast<int8_t>(clampi(state_->map.target[s] + d, -1, mt::kTracks - 1));
              }};
    p[r++] = {"  transpose", [this, s](char* o, int n) { snprintf(o, n, "%+d", state_->map.transpose[s]); },
              [this, s](int d) {
                state_->map.transpose[s] = static_cast<int8_t>(clampi(state_->map.transpose[s] + d, -24, 24));
              }};
  }
  p[r++] = {"Offset bars", [this](char* o, int n) { snprintf(o, n, "%u", state_->map.offsetBars); },
            [this](int d) { state_->map.offsetBars = static_cast<uint16_t>(clampi(state_->map.offsetBars + d, 0, 999)); }};
  p[r++] = {"Quantize", [this](char* o, int n) { snprintf(o, n, "%s", resName(state_->map.quant)); },
            [this](int d) {
              const int q = clampi(static_cast<int>(state_->map.quant) + d, 0, static_cast<int>(mt::Resolution::Count) - 1);
              state_->map.quant = static_cast<mt::Resolution>(q);
            }};
  p[r++] = {"Pattern length", [this](char* o, int n) { snprintf(o, n, "%u", state_->map.patternLen); },
            [this](int d) {
              state_->map.patternLen = static_cast<uint8_t>(clampi(state_->map.patternLen + d, mt::kMinSteps, mt::kMaxSteps));
            }};
  p[r++] = {"First pattern", [this](char* o, int n) { snprintf(o, n, "P%02d", state_->map.firstPattern + 1); },
            [this](int d) {
              state_->map.firstPattern = static_cast<uint8_t>(clampi(state_->map.firstPattern + d, 0, mt::kPatterns - 1));
            }};
  p[r++] = {"Mono", [this](char* o, int n) { snprintf(o, n, "%s", kMonoNames[static_cast<int>(state_->map.mono)]); },
            [this](int d) { state_->map.mono = static_cast<mt::MonoMode>(clampi(static_cast<int>(state_->map.mono) + d, 0, 2)); }};
  p[r++] = {"Source channel", [this](char* o, int n) { onOff(state_->map.useSourceChannel, o, n); },
            [this](int d) { state_->map.useSourceChannel = d > 0; }};
  p[r++] = {"Use tempo",
            [this](char* o, int n) {
              if (!state_->map.useTempo) snprintf(o, n, "OFF");
              else if (state_->info.firstTempoUsPerQ == 0) snprintf(o, n, "ON  (keep)");
              else snprintf(o, n, "ON  (%u BPM)", fileBpm(state_->info.firstTempoUsPerQ));
            },
            [this](int d) { state_->map.useTempo = d > 0; }};
  p[r++] = {"Microtiming", [this](char* o, int n) { onOff(state_->map.keepMicrotiming, o, n); },
            [this](int d) { state_->map.keepMicrotiming = d > 0; }};
  importRow_ = r;
  p[r++] = {"IMPORT", nullptr, nullptr};
  cancelRow_ = r;
  p[r++] = {"CANCEL", nullptr, nullptr};
  list_.setParams(p, r);
  list_.setVisibleRows((kAreaH - kHeaderH) / ParamList::kRowH);
  list_.setSel(0);
  list_.setEdit(false);
}

void ImportDialog::action(int row) {
  list_.setEdit(false);
  if (row == importRow_) confirm();
  else if (row == cancelRow_) close();
}

// Dry run into a copy of live; the confirmation names the exact patterns that will be
// overwritten and commit() copies them from this result. No lock for the copy: the engine only
// writes bpm.
void ImportDialog::confirm() {
  freeProject(state_->tmp);
  state_->tmp = allocProject();
  if (!state_->tmp) {
    app_.toast(storage::resultText(storage::Result::NoMemory));
    return;
  }
  *state_->tmp = app_.project();
  state_->result = mt::importSmf(state_->info, notes_, noteCount_, state_->map, *state_->tmp);
  const int n = state_->result.patternsWritten;
  if (n == 0) {
    freeProject(state_->tmp);
    state_->tmp = nullptr;
    app_.toast("NOTHING TO IMPORT");
    return;
  }
  const int first = state_->map.firstPattern + 1;
  const int last = first + n - 1;
  char title[28];
  if (first == last) snprintf(title, sizeof(title), "OVERWRITE P%02d?", first);
  else snprintf(title, sizeof(title), "OVERWRITE P%02d-P%02d?", first, last);
  const MenuItem items[] = {{"Cancel", kCancel}, {"Import", kDoImport}};
  app_.menu().open(title, items, 2, [this](int id) {
    if (id == kDoImport) commit();
    app_.invalidate();
  });
}

void ImportDialog::commit() {
  if (!state_ || !state_->tmp) return;
  app_.showBusy("IMPORTING...");
  if (!storage::stopEngine()) {
    app_.toast(storage::resultText(storage::Result::EngineBusy));
    return;
  }
  const mt::ImportResult r = state_->result;
  const bool setBpm = state_->map.useTempo && state_->info.firstTempoUsPerQ > 0;
  // importSmf only writes patterns first..first+n-1 and bpm. The engine is stopped, so one
  // lock around the whole copy is fine.
  mt::Project& live = app_.project();
  const int first = state_->map.firstPattern;
  engine::lockProject();
  for (int i = first; i < first + r.patternsWritten && i < mt::kPatterns; ++i) live.patterns[i] = state_->tmp->patterns[i];
  if (setBpm) live.bpm = r.bpm;
  engine::unlockProject();
  engine::post(engine::Cmd::ReleaseTies);  // ties may still hold notes of the old data
  if (setBpm) engine::post(engine::Cmd::SetBpm, r.bpm);  // engine-side tempo state
  close();
  app_.projectReplaced();  // clears undo, refreshes screens (and marks saved: undone below)
  app_.markDirty();
  char msg[48];
  const char* pl = r.patternsWritten == 1 ? "" : "S";
  if (r.notesDropped > 0)
    snprintf(msg, sizeof(msg), "IMPORTED %u PATTERN%s (%lu DROPPED)", r.patternsWritten, pl,
             static_cast<unsigned long>(r.notesDropped));
  else snprintf(msg, sizeof(msg), "IMPORTED %u PATTERN%s", r.patternsWritten, pl);
  app_.toast(msg);
}

void ImportDialog::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  if (ev.type == InputType::EncLong) {
    if (list_.editing()) list_.setEdit(false);
    else close();
    return;
  }
  if (ev.type == InputType::EncClick && isAction(list_.sel())) {
    action(list_.sel());
    return;
  }
  list_.onInput(ev);
}

void ImportDialog::onTouch(const TouchEvent& ev) {
  if (ev.type == TouchType::Tap) {
    const int r = list_.rowAt(ev.y);
    if (isAction(r)) {
      list_.setSel(r);
      action(r);
      return;
    }
  }
  list_.onTouch(ev);
}

void ImportDialog::draw(LGFX_Sprite& s, int y0) {
  if (!state_ || !isOpen()) return;
  char buf[48];
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  s.setTextColor(kText);
  snprintf(buf, sizeof(buf), "IMPORT %.20s", state_->file);
  s.drawString(buf, ParamList::kLabelX, y0 + (kHeaderH - 4 - kCharH) / 2);
  s.setTextColor(state_->cut ? kEditCursor : kDim);
  snprintf(buf, sizeof(buf), "%lu notes%s", static_cast<unsigned long>(noteCount_), state_->cut ? " CUT" : "");
  s.drawString(buf, kScreenW - 16 - static_cast<int>(strlen(buf)) * kCharW, y0 + (kHeaderH - 4 - kCharH) / 2);
  list_.draw(s, y0 + kHeaderH);
}

}  // namespace ui
