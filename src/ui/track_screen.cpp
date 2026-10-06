#include "track_screen.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "name_edit.h"

namespace ui {
namespace {

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void onOff(bool v, char* out, int n) { snprintf(out, n, "%s", v ? "ON" : "OFF"); }

}  // namespace

TrackScreen::TrackScreen(App& app) : app_(app) {
  params_[kName] = {"Name", [this](char* o, int n) { snprintf(o, n, "%s", cfg().name); },
                    [this](int d) { editName(d); }};
  params_[kOut] = {"Out", [this](char* o, int n) { snprintf(o, n, "%s", internal() ? "INT" : "MIDI"); },
                   [this](int d) {
                     const int v = clampi(static_cast<int>(cfg().out) + d, 0, static_cast<int>(mt::TrackOut::Count) - 1);
                     if (v == static_cast<int>(cfg().out)) return;
                     cfg().out = static_cast<mt::TrackOut>(v);
                     // Unheard steps are replanned for the new output, sounding notes end.
                     engine::post(engine::Cmd::ReleaseTies);
                     engine::post(engine::Cmd::TrackOut, static_cast<uint16_t>(app_.curTrack()));
                   }};
  params_[kInstr] = {"Instr",
                     [this](char* o, int n) {
                       const int i = cfg().instr % mt::kInstruments;
                       snprintf(o, n, "%d %s", i + 1, app_.project().instruments[i].name);
                     },
                     [this](int d) {
                       const int v = clampi(cfg().instr + d, 0, mt::kInstruments - 1);
                       if (v == cfg().instr) return;
                       cfg().instr = static_cast<uint8_t>(v);
                       if (internal()) engine::post(engine::Cmd::SendProgram, static_cast<uint16_t>(app_.curTrack()));
                     },
                     [this] { return !internal(); }};
  params_[kVol] = {"Volume", [this](char* o, int n) { snprintf(o, n, "%u", cfg().vol); },
                   [this](int d) { cfg().vol = static_cast<uint8_t>(clampi(cfg().vol + d, 0, 127)); },
                   [this] { return !internal(); }};
  auto midiOnly = [this] { return internal(); };
  params_[kChannel] = {"Channel", [this](char* o, int n) { snprintf(o, n, "%u", cfg().channel + 1); },
                       [this](int d) { cfg().channel = static_cast<uint8_t>(clampi(cfg().channel + d, 0, 15)); },
                       midiOnly};
  params_[kVel] = {"Def vel", [this](char* o, int n) { snprintf(o, n, "%u", cfg().defVel); },
                   [this](int d) { cfg().defVel = static_cast<uint8_t>(clampi(cfg().defVel + d, 1, 127)); }};
  params_[kGate] = {"Def gate", [this](char* o, int n) { snprintf(o, n, "%u%%", mt::gatePercent(cfg().defGate)); },
                    [this](int d) { cfg().defGate = static_cast<uint8_t>(clampi(cfg().defGate + d, 1, 200)); }};
  // The track's own length in the edited pattern (polymeter): OFF = the pattern length.
  params_[kPatLen] = {"Pat len",
                      [this](char* o, int n) {
                        const mt::Pattern& pt = app_.project().patterns[app_.editPattern()];
                        const uint8_t v = pt.trackLen[app_.curTrack()];
                        if (v && v < pt.length) snprintf(o, n, "%u / %u", v, pt.length);
                        else snprintf(o, n, "OFF");
                      },
                      [this](int d) {
                        mt::Pattern& pt = app_.project().patterns[app_.editPattern()];
                        uint8_t& v = pt.trackLen[app_.curTrack()];
                        // OFF below 1; the pattern length itself is OFF too.
                        const int cur = v && v < pt.length ? v : (d > 0 ? 0 : pt.length);
                        int nv = clampi(cur + d, 0, pt.length);
                        if (nv >= pt.length) nv = 0;
                        if (nv == v) return;
                        if (app_.editSeq() != patLenSeq_) app_.pushUndo();  // whole-pattern snapshot
                        v = static_cast<uint8_t>(nv);
                        patLenSeq_ = app_.editSeq() + 1;  // onEdit marks dirty next
                      }};
  params_[kCcA] = {"CC A", [this](char* o, int n) { snprintf(o, n, "%u", cfg().ccA); },
                   [this](int d) { cfg().ccA = static_cast<uint8_t>(clampi(cfg().ccA + d, 0, 127)); }, midiOnly};
  params_[kCcB] = {"CC B", [this](char* o, int n) { snprintf(o, n, "%u", cfg().ccB); },
                   [this](int d) { cfg().ccB = static_cast<uint8_t>(clampi(cfg().ccB + d, 0, 127)); }, midiOnly};
  params_[kProgram] = {"Program",
                       [this](char* o, int n) {
                         if (cfg().program == mt::kNoProgram) snprintf(o, n, "---");
                         else snprintf(o, n, "%u", cfg().program);
                       },
                       [this](int d) {
                         const int v = cfg().program == mt::kNoProgram ? -1 : cfg().program;
                         const int nv = clampi(v + d, -1, 127);
                         cfg().program = nv < 0 ? mt::kNoProgram : static_cast<uint8_t>(nv);
                         if (!internal()) engine::post(engine::Cmd::SendProgram, static_cast<uint16_t>(app_.curTrack()));
                       },
                       midiOnly};
  params_[kMute] = {"Mute", [this](char* o, int n) { onOff(cfg().mute, o, n); },
                    [this](int d) { cfg().mute = d > 0; }};
  params_[kSolo] = {"Solo", [this](char* o, int n) { onOff(cfg().solo, o, n); },
                    [this](int d) { cfg().solo = d > 0; }};
  list_.setParams(params_, kRows);
  list_.setVisibleRows(kVisibleRows);
  list_.setOnEdit([this] { app_.markDirty(); });
}

mt::TrackCfg& TrackScreen::cfg() { return app_.project().tracks[app_.curTrack()]; }

void TrackScreen::fixNames() {
  mt::Project& p = app_.project();
  for (int t = 0; t < mt::kTracks; ++t) {
    if (p.tracks[t].name[0]) continue;
    engine::lockProject();
    snprintf(p.tracks[t].name, sizeof(p.tracks[t].name), "TRK%d", t + 1);
    engine::unlockProject();
  }
}

void TrackScreen::leaveEdit() {
  list_.setEdit(false);
  namePos_ = 0;
  fixNames();
}

void TrackScreen::onEnter() { leaveEdit(); }

void TrackScreen::changeTrack(int d) {
  leaveEdit();
  app_.setCurTrack(app_.curTrack() + d);
}

void TrackScreen::editName(int delta) { editNameChar(cfg().name, kNameLen, namePos_, delta); }

void TrackScreen::onInput(const hw::InputEvent& ev) {
  const bool wasName = nameEdit();
  if (ev.type == hw::InputType::EncTurn && ev.shift) {
    if (wasName) {
      namePos_ = clampi(namePos_ + ev.delta, 0, kNameLen - 1);
      return;
    }
    if (!list_.editing()) {
      changeTrack(ev.delta);
      return;
    }
  }
  if (wasName && ev.type == hw::InputType::EncTurn) {
    list_.edit(ev.delta);  // no x10 for characters
    return;
  }
  list_.onInput(ev);
  if (wasName && !nameEdit()) leaveEdit();
}

void TrackScreen::onTouch(const TouchEvent& ev) {
  if (ev.type == TouchType::Tap && ev.y < y0_ + kHeaderH) {
    if (ev.x < kArrowW) changeTrack(-1);
    else if (ev.x >= kScreenW - kArrowW) changeTrack(1);
    return;
  }
  // Tap on a character of the name being edited moves the name cursor.
  if (ev.type == TouchType::Tap && nameEdit() && list_.rowAt(ev.y) == kName && ev.x >= ParamList::kValueX &&
      ev.x < ParamList::kValueX + kNameLen * kCharW) {
    namePos_ = (ev.x - ParamList::kValueX) / kCharW;
    return;
  }
  const bool wasName = nameEdit();
  list_.onTouch(ev);
  if (wasName && !nameEdit()) leaveEdit();
}

void TrackScreen::draw(LGFX_Sprite& s, int y0, int) {
  y0_ = y0;
  char buf[24];
  const int t = app_.curTrack();
  const int cy = y0 + kHeaderH / 2;
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  s.fillTriangle(24, cy - 2, 36, cy - 9, 36, cy + 5, kCursor);
  s.fillTriangle(kScreenW - 24, cy - 2, kScreenW - 36, cy - 9, kScreenW - 36, cy + 5, kCursor);
  snprintf(buf, sizeof(buf), "TRACK %d", t + 1);
  s.setTextColor(kText);
  s.drawString(buf, (kScreenW - static_cast<int>(strlen(buf)) * kCharW) / 2, y0 + (kHeaderH - 4 - kCharH) / 2);

  list_.draw(s, y0 + kHeaderH);
  if (nameEdit()) {
    const int uy = list_.rowY(kName) + (ParamList::kRowH + kCharH) / 2;
    s.fillRect(ParamList::kValueX + namePos_ * kCharW, uy, kCharW, 2, kEditCursor);
  }
}

}  // namespace ui
