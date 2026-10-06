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
  params_[kTracks] = {"Tracks", [this](char* o, int n) { snprintf(o, n, "%s", soloed_ ? "SOLOED" : "ALL"); },
                      [this](int d) { soloed_ = d > 0 && app_.project().anySolo(); },
                      [this] { return !app_.project().anySolo(); }};
  params_[kRender] = {"RENDER", [](char* o, int n) { snprintf(o, n, "LONG PRESS: STOP"); }, nullptr};
  params_[kCancel] = {"Cancel", nullptr, nullptr};
  list_.setParams(params_, kRows);
  list_.setVisibleRows((kAreaH - kHeaderH) / ParamList::kRowH);
}

bool RenderDialog::songOk() const { return app_.project().chainLen > 0; }

void RenderDialog::open() {
  source_ = app_.editPattern();
  soloed_ = false;
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
  storage::renderPath(app_.project(), spec, path, sizeof(path));
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
  if (soloed_ && p.anySolo()) {
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
