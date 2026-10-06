#include "render_dialog.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "hw/sdcard.h"
#include "storage/render_io.h"

namespace ui {

RenderDialog::RenderDialog(App& app) : app_(app) {
  params_[kSource] = {"Source",
                      [this](char* o, int n) {
                        if (source_ >= mt::kPatterns) snprintf(o, n, "SONG");
                        else snprintf(o, n, "PATTERN %02d", source_ + 1);
                      },
                      [this](int d) {
                        const int last = songOk() ? mt::kPatterns : mt::kPatterns - 1;
                        source_ = source_ + d < 0 ? 0 : (source_ + d > last ? last : source_ + d);
                      }};
  params_[kTracks] = {"Tracks",
                      [this](char* o, int n) {
                        static const char* const kNames[] = {"ALL", "SOLOED", "STEMS"};
                        snprintf(o, n, "%s", kNames[tracks_]);
                      },
                      [this](int d) {
                        int v = tracks_ + (d > 0 ? 1 : -1);
                        v = v < kAll ? kAll : (v > kStems ? kStems : v);
                        if (v == kSoloed && !app_.project().anySolo()) v += d > 0 ? 1 : -1;  // no solo: skipped
                        tracks_ = v;
                      }};
  params_[kRender] = {"RENDER", [](char* o, int n) { snprintf(o, n, "LONG PRESS: STOP"); }, nullptr};
  params_[kCancel] = {"Cancel", nullptr, nullptr};
  list_.setParams(params_, kRows);
  list_.setVisibleRows((kAreaH - kHeaderH) / ParamList::kRowH);
}

bool RenderDialog::songOk() const { return app_.project().chainLen > 0; }

void RenderDialog::open() {
  source_ = app_.editPattern();
  tracks_ = kAll;
  list_.setEdit(false);
  list_.setSel(kSource);
  open_ = true;
}

bool RenderDialog::action(int row) {
  if (row == kRender) confirm();
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
  if (source_ >= mt::kPatterns && !songOk()) source_ = app_.editPattern();
  mt::RenderSpec spec;
  if (source_ >= mt::kPatterns) spec.mode = mt::RenderSpec::Mode::Song;
  else spec.pattern = static_cast<uint8_t>(source_);
  char path[96];
  int first = -1;  // STEMS: the first stem's file stands for all of them
  if (tracks_ == kStems) {
    const uint16_t m = stemTracks(spec);
    if (!m) {
      app_.toast("NO INT TRACK WITH NOTES");
      return;
    }
    while (!(m & (1u << ++first))) {}
  }
  storage::renderPath(app_.project(), spec, path, sizeof(path), first);
  if (hw::sdFs().exists(path)) {
    char title[48];
    const char* file = strrchr(path, '/');
    snprintf(title, sizeof(title), "OVERWRITE %s?", file ? file + 1 : path);
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
  mt::RenderSpec spec;
  if (source_ >= mt::kPatterns) spec.mode = mt::RenderSpec::Mode::Song;
  else spec.pattern = static_cast<uint8_t>(source_);
  if (tracks_ == kStems) {
    runStems(spec);
    return;
  }
  if (tracks_ == kSoloed && p.anySolo()) {
    spec.tracksMask = 0;
    for (int t = 0; t < mt::kTracks; ++t)
      if (p.tracks[t].solo) spec.tracksMask |= static_cast<uint16_t>(1u << t);
  }
  spec.tailBlocks = kTailBlocks;
  char path[96];
  storage::renderPath(p, spec, path, sizeof(path));
  storage::RenderStats st;
  const storage::Result r = storage::renderWav(p, spec, path, st, App::renderProgress, &app_);
  app_.endProgress();
  if (r != storage::Result::Ok) {
    app_.toast(storage::resultText(r));
    return;
  }
  char msg[48];
  const float db = st.peak > 0 ? 20.f * log10f(st.peak / 32767.f) : -96.f;
  snprintf(msg, sizeof(msg), "%lu.%lus  PEAK %.1fdB%s", static_cast<unsigned long>(st.frames / 32000),
           static_cast<unsigned long>(st.frames % 32000 / 3200), db, st.clips ? "  CLIP" : "");
  app_.toast(msg);
  close();
}

uint16_t RenderDialog::stemTracks(const mt::RenderSpec& spec) const {
  const mt::Project& p = app_.project();
  bool used[mt::kPatterns] = {};
  if (spec.mode == mt::RenderSpec::Mode::Song) {
    for (int i = 0; i < p.chainLen && i < mt::kChainMax; ++i) used[p.chain[i] < mt::kPatterns ? p.chain[i] : 0] = true;
  } else {
    used[spec.pattern] = true;
  }
  uint16_t m = 0;
  for (int t = 0; t < mt::kTracks; ++t) {
    if (!p.trackInternal(t) || !p.trackAudible(t)) continue;
    for (int pi = 0; pi < mt::kPatterns && !(m & (1u << t)); ++pi) {
      if (!used[pi]) continue;
      const mt::Pattern& pt = p.patterns[pi];
      for (int s = 0; s < pt.length; ++s)
        if (pt.steps[t][s].hasNote()) {
          m |= static_cast<uint16_t>(1u << t);
          break;
        }
    }
  }
  return m;
}

namespace {
struct StemProgress {
  App* app;
  int index, count;
};
// One bar over all the stems.
bool stemProgress(uint32_t done, uint32_t total, void* ctx) {
  const StemProgress& s = *static_cast<StemProgress*>(ctx);
  return App::renderProgress(static_cast<uint32_t>(s.index) * total + done, static_cast<uint32_t>(s.count) * total,
                             s.app);
}
}  // namespace

void RenderDialog::runStems(const mt::RenderSpec& base) {
  mt::Project& p = app_.project();
  const uint16_t m = stemTracks(base);
  int count = 0;
  for (int t = 0; t < mt::kTracks; ++t) count += (m >> t) & 1;
  StemProgress prog{&app_, 0, count};
  storage::Result r = storage::Result::Ok;
  for (int t = 0; t < mt::kTracks && r == storage::Result::Ok; ++t) {
    if (!(m & (1u << t))) continue;
    mt::RenderSpec spec = base;
    spec.tracksMask = static_cast<uint16_t>(1u << t);
    spec.tailBlocks = kTailBlocks;
    char path[96];
    storage::renderPath(p, spec, path, sizeof(path), t);
    storage::RenderStats st;
    r = storage::renderWav(p, spec, path, st, stemProgress, &prog);
    ++prog.index;
  }
  app_.endProgress();
  if (r != storage::Result::Ok) {
    app_.toast(storage::resultText(r));
    return;
  }
  char msg[40];
  snprintf(msg, sizeof(msg), "%d STEMS RENDERED", count);
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
    if (r == kRender || r == kCancel) {
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
  s.setTextColor(kText);
  s.drawString("RENDER WAV  /samples/render", ParamList::kLabelX, y0 + (kHeaderH - 4 - kCharH) / 2);
  list_.draw(s, y0 + kHeaderH);
}

}  // namespace ui
