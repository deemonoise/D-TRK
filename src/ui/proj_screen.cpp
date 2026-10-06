#include "proj_screen.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "names.h"
#include "scale.h"

namespace ui {
namespace {

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
int wrapi(int v, int n) { return ((v % n) + n) % n; }

}  // namespace

ProjScreen::ProjScreen(App& app) : app_(app) {
  params_[kBpm] = {"BPM", [this](char* o, int n) { snprintf(o, n, "%d", bpm()); }, [this](int d) { editBpm(d); }};
  params_[kRoot] = {"Scale root", [this](char* o, int n) { snprintf(o, n, "%s", kRootNames[app_.project().scaleRoot % 12]); },
                    [this](int d) { app_.project().scaleRoot = static_cast<uint8_t>(wrapi(app_.project().scaleRoot + d, 12)); }};
  params_[kScale] = {"Scale",
                     [this](char* o, int n) {
                       snprintf(o, n, "%s", mt::scaleName(static_cast<mt::ScaleType>(app_.project().scaleType)));
                     },
                     [this](int d) {
                       constexpr int count = static_cast<int>(mt::ScaleType::Count);
                       app_.project().scaleType = static_cast<uint8_t>(wrapi(app_.project().scaleType + d, count));
                     }};
  params_[kLength] = {"Length", [this](char* o, int n) { snprintf(o, n, "%u", pat().length); },
                      [this](int d) {
                        pat().length = static_cast<uint8_t>(clampi(pat().length + d, mt::kMinSteps, mt::kMaxSteps));
                        pat().fitTrackLen();
                      }};
  params_[kRes] = {"Resolution",
                   [this](char* o, int n) { snprintf(o, n, "%s", resName(pat().res)); },
                   [this](int d) {
                     const int r = clampi(static_cast<int>(pat().res) + d, 0, static_cast<int>(mt::Resolution::Count) - 1);
                     pat().res = static_cast<mt::Resolution>(r);
                   }};
  params_[kSwing] = {"Swing", [this](char* o, int n) { snprintf(o, n, "%u%%", pat().swing); },
                     [this](int d) { pat().swing = static_cast<uint8_t>(clampi(pat().swing + d, 50, 75)); }};
  params_[kVolume] = {"Volume", [this](char* o, int n) { snprintf(o, n, "%u%%", app_.project().masterVol); },
                      [this](int d) {
                        app_.project().masterVol = static_cast<uint8_t>(clampi(app_.project().masterVol + d, 0, mt::kMasterVolMax));
                      }};
  auto u7 = [this](uint8_t mt::Project::*f) {
    return [this, f](int d) { app_.project().*f = static_cast<uint8_t>(clampi(app_.project().*f + d, 0, 127)); };
  };
  auto num = [this](uint8_t mt::Project::*f) {
    return [this, f](char* o, int n) { snprintf(o, n, "%u", app_.project().*f); };
  };
  auto noDelay = [this] { return app_.project().dlyLevel == 0; };
  params_[kDlyTime] = {"Delay", [this](char* o, int n) { snprintf(o, n, "%u/16", app_.project().dlyTime); },
                       [this](int d) {
                         app_.project().dlyTime = static_cast<uint8_t>(clampi(app_.project().dlyTime + d, 1, mt::kDlyTimeMax));
                       },
                       noDelay};
  params_[kDlyFb] = {"Feedback", num(&mt::Project::dlyFb), u7(&mt::Project::dlyFb), noDelay};
  params_[kDlyTone] = {"Tone", num(&mt::Project::dlyTone), u7(&mt::Project::dlyTone), noDelay};
  params_[kDlyLevel] = {"Dly level", num(&mt::Project::dlyLevel), u7(&mt::Project::dlyLevel)};
  params_[kPreview] = {"Preview", [this](char* o, int n) { snprintf(o, n, "%s", app_.project().preview ? "ON" : "OFF"); },
                       [this](int d) { app_.project().preview = d > 0; }};
  list_.setParams(params_, kRows);
  list_.setVisibleRows(kListRows);
  list_.setOnEdit([this] { app_.markDirty(); });
}

mt::Pattern& ProjScreen::pat() { return app_.project().patterns[app_.editPattern()]; }

int ProjScreen::bpm() {
  const int cur = app_.project().bpm;
  if (bpmTarget_ != cur && millis() - bpmPostMs_ >= kBpmSettleMs) bpmTarget_ = cur;  // changed elsewhere
  return bpmTarget_;
}

void ProjScreen::editBpm(int delta) {
  bpmTarget_ = clampi(bpm() + delta, 20, 300);
  bpmPostMs_ = millis();
  engine::post(engine::Cmd::SetBpm, static_cast<uint16_t>(bpmTarget_));
}

void ProjScreen::onEnter() {
  list_.setEdit(false);
  bpmTarget_ = app_.project().bpm;
}

void ProjScreen::onInput(const hw::InputEvent& ev) { list_.onInput(ev); }

void ProjScreen::onTouch(const TouchEvent& ev) { list_.onTouch(ev); }

void ProjScreen::draw(LGFX_Sprite& s, int y0, int) {
  char buf[24];
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  snprintf(buf, sizeof(buf), "PROJECT  (P%02d)", app_.editPattern() + 1);
  s.setTextColor(kText);
  s.drawString(buf, ParamList::kLabelX, y0 + (kHeaderH - 4 - kCharH) / 2);
  list_.draw(s, y0 + kHeaderH);
}

}  // namespace ui
