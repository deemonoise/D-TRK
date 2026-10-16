#include "render_dialog.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "audio/audio.h"
#include "hw/sdcard.h"
#include "storage/render_io.h"

namespace ui {

namespace {
constexpr uint8_t kLoopCounts[] = {1, 2, 4, 8, 16};
constexpr int kLoopN = sizeof kLoopCounts;
const char* const kTailNames[] = {"AUTO", "OFF", "2 s", "4 s", "8 s", "WRAP"};
const char* const kStemNames[] = {"OFF", "TRACKS", "TRACKS+FX"};

int step(int v, int d, int lo, int hi) {
  v += d > 0 ? 1 : -1;
  return v < lo ? lo : (v > hi ? hi : v);
}

// "EXPORT 2/4" over every pass.
struct Prog {
  App* app;
};
bool progress(int pass, int passes, uint32_t done, uint32_t total, void* ctx) {
  char label[24];
  if (passes > 1) snprintf(label, sizeof label, "EXPORT %d/%d", pass + 1, passes);
  else snprintf(label, sizeof label, "EXPORT");
  return App::labeledProgress(label, done, total, static_cast<Prog*>(ctx)->app);
}
}  // namespace

RenderDialog::RenderDialog(App& app) : app_(app) {
  params_[kSource] = {"Source",
                      [this](char* o, int n) {
                        if (isSong()) snprintf(o, n, "SONG");
                        else snprintf(o, n, "PATTERN %02d", source_ + 1);
                      },
                      [this](int d) {
                        const int last = songOk() ? mt::kPatterns : mt::kPatterns - 1;
                        source_ = source_ + d < 0 ? 0 : (source_ + d > last ? last : source_ + d);
                        if (isSong() && tail_ == mt::ExportTail::Wrap) tail_ = mt::ExportTail::Auto;
                      }};
  params_[kLoops] = {"Loops", [this](char* o, int n) { snprintf(o, n, "%u", isSong() ? 1u : kLoopCounts[loops_]); },
                     [this](int d) {
                       if (!isSong()) loops_ = step(loops_, d, 0, kLoopN - 1);
                     },
                     [this] { return isSong(); }};
  params_[kTail] = {"Tail", [this](char* o, int n) { snprintf(o, n, "%s", kTailNames[static_cast<int>(tail_)]); },
                    [this](int d) {
                      const int last = isSong() ? static_cast<int>(mt::ExportTail::S8) : static_cast<int>(mt::ExportTail::Wrap);
                      tail_ = static_cast<mt::ExportTail>(step(static_cast<int>(tail_), d, 0, last));
                    }};
  params_[kTracks] = {"Tracks", [this](char* o, int n) { snprintf(o, n, "%s", soloed_ ? "SOLOED" : "ALL"); },
                      [this](int d) { soloed_ = d > 0 && app_.project().anySolo(); }};
  params_[kStems] = {"Stems", [this](char* o, int n) { snprintf(o, n, "%s", kStemNames[static_cast<int>(stems_)]); },
                     [this](int d) {
                       stems_ = static_cast<mt::ExportStems>(step(static_cast<int>(stems_), d, 0,
                                                                  static_cast<int>(mt::ExportStems::TracksFx)));
                     }};
  params_[kFormat] = {"Format", [this](char* o, int n) { snprintf(o, n, "%s", bits16_ ? "16 BIT" : "24 BIT"); },
                      [this](int d) { bits16_ = d > 0; }};
  params_[kExport] = {"EXPORT", [](char* o, int n) { snprintf(o, n, "LONG PRESS: STOP"); }, nullptr};
  params_[kCancel] = {"Cancel", nullptr, nullptr};
  list_.setParams(params_, kRows);
  list_.setVisibleRows((kAreaH - kHeaderH) / ParamList::kRowH);
}

bool RenderDialog::songOk() const { return app_.project().songLen > 0; }

void RenderDialog::open() {
  source_ = app_.editPattern();
  soloed_ = false;
  list_.setEdit(false);
  list_.setSel(kSource);
  open_ = true;
}

mt::ExportSpec RenderDialog::spec() const {
  const mt::Project& p = app_.project();
  mt::ExportSpec s;
  if (isSong()) s.mode = mt::RenderSpec::Mode::Song;
  else s.pattern = static_cast<uint8_t>(source_);
  s.loops = isSong() ? 1 : kLoopCounts[loops_];
  s.tail = isSong() && tail_ == mt::ExportTail::Wrap ? mt::ExportTail::Auto : tail_;
  s.stems = stems_;
  s.bits = bits16_ ? 16 : 24;
  if (soloed_ && p.anySolo()) {
    s.tracksMask = 0;
    for (int t = 0; t < mt::kTracks; ++t)
      if (p.tracks[t].solo) s.tracksMask |= static_cast<uint16_t>(1u << t);
  }
  return s;
}

// "~63MB x20 4:12 2 PASSES  3 MIDI SKIPPED"
void RenderDialog::estimate(char* out, int n) const {
  const mt::Project& p = app_.project();
  const mt::ExportSpec s = spec();
  static mt::ExportOut outs[mt::kExportMaxOut];
  const int files = mt::exportOutputs(p, s, outs);
  const uint32_t frames = mt::exportEstFrames(p, s);
  const uint32_t mb = static_cast<uint32_t>(mt::exportBytes(1, frames, s.bits) / (1024u * 1024u)) + 1;
  const uint32_t sec = frames / mt::kSynthRate;
  const int outMax = audio::streamInfo().psram == 1 ? mt::kExportMaxOut : 6;
  const int passes = mt::exportPasses(files, outMax);
  const int midi = mt::exportMidiTracks(p, s);
  int k = snprintf(out, n, "~%luMB x%d  %lu:%02lu", static_cast<unsigned long>(mb), files,
                   static_cast<unsigned long>(sec / 60), static_cast<unsigned long>(sec % 60));
  if (passes > 1 && k < n) k += snprintf(out + k, n - k, "  %d PASSES", passes);
  if (midi && k < n) snprintf(out + k, n - k, "  %d MIDI SKIPPED", midi);
}

bool RenderDialog::action(int row) {
  if (row == kExport) confirm();
  else if (row == kCancel) close();
  else return false;
  app_.invalidate();
  return true;
}

void RenderDialog::confirm() {
  const engine::Status& st = app_.status();
  if (st.playing || st.paused) {
    app_.toast("STOP FIRST");
    return;
  }
  if (!hw::sdReady()) {
    app_.toast(storage::resultText(storage::Result::NoSd));
    return;
  }
  if (isSong() && !songOk()) source_ = app_.editPattern();
  const mt::ExportSpec s = spec();
  if (s.stems != mt::ExportStems::Off && !mt::exportStemTracks(app_.project(), s)) {
    app_.toast("NO INT TRACK WITH NOTES");
    return;
  }
  char dir[128];
  mt::exportDir(app_.project(), s, dir, sizeof dir);
  if (hw::sdFs().exists(dir)) {
    char src[12], title[40];
    mt::exportSrcTag(s, src, sizeof src);
    snprintf(title, sizeof(title), "OVERWRITE %s?", src);
    const MenuItem items[] = {{"Cancel", kMenuCancel}, {"Overwrite", kMenuOverwrite}};
    app_.menu().open(title, items, 2, [this](int id) {
      if (id == kMenuOverwrite) run();
    });
    return;
  }
  run();
}

void RenderDialog::run() {
  mt::Project& p = app_.project();
  const mt::ExportSpec s = spec();
  storage::ExportStats st;
  Prog prog{&app_};
  const storage::Result r = storage::exportSet(p, s, st, progress, &prog);
  app_.endProgress();
  if (r != storage::Result::Ok) {
    app_.toast(storage::resultText(r));
    return;
  }
  char msg[48];
  const float db = st.peak > 0 ? 20.f * log10f(st.peak / 32767.f) : -96.f;
  const uint32_t sec = st.frames / mt::kSynthRate;
  snprintf(msg, sizeof(msg), "%lu:%02lu PEAK %.1fdB %d FILES%s", static_cast<unsigned long>(sec / 60),
           static_cast<unsigned long>(sec % 60), db, st.files, st.clips ? " CLIP" : "");
  app_.toast(msg);
  close();
}

void RenderDialog::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  if (!open_) return;
  if (ev.type == InputType::EncLong) {
    close();
    return;
  }
  if (ev.type == InputType::EncClick && !list_.editing() && action(list_.sel())) return;
  list_.onInput(ev);
}

void RenderDialog::onTouch(const TouchEvent& ev) {
  if (!open_) return;
  if (ev.type == TouchType::Tap) {
    const int r = list_.rowAt(ev.y);
    if (r == kExport || r == kCancel) {
      list_.setSel(r);
      action(r);
      return;
    }
  }
  list_.onTouch(ev);
}

void RenderDialog::draw(LGFX_Sprite& s, int y0) {
  if (!open_) return;
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  char dir[128], line[64];
  mt::exportDir(app_.project(), spec(), dir, sizeof dir);
  snprintf(line, sizeof line, "EXPORT  %s", dir);
  s.setTextColor(kText);
  s.drawString(line, ParamList::kLabelX, y0 + 2);
  estimate(line, sizeof line);
  s.setTextColor(kDim);
  s.drawString(line, ParamList::kLabelX, y0 + 2 + kCharH + 2);
  list_.draw(s, y0 + kHeaderH);
}

}  // namespace ui
