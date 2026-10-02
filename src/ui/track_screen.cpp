#include "track_screen.h"
#include <stdio.h>
#include <string.h>
#include "app.h"

namespace ui {
namespace {

constexpr char kNameChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_ ";
constexpr int kNameCharCount = sizeof(kNameChars) - 1;

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

int nameCharIndex(char c) {
  if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  for (int i = 0; i < kNameCharCount; ++i)
    if (kNameChars[i] == c) return i;
  return kNameCharCount - 1;  // unknown or end of string = space
}

void onOff(bool v, char* out, int n) { snprintf(out, n, "%s", v ? "ON" : "OFF"); }

}  // namespace

TrackScreen::TrackScreen(App& app) : app_(app) {
  params_[kName] = {"Name", [this](char* o, int n) { snprintf(o, n, "%s", cfg().name); },
                    [this](int d) { editName(d); }};
  params_[kChannel] = {"Channel", [this](char* o, int n) { snprintf(o, n, "%u", cfg().channel + 1); },
                       [this](int d) { cfg().channel = static_cast<uint8_t>(clampi(cfg().channel + d, 0, 15)); }};
  params_[kVel] = {"Def vel", [this](char* o, int n) { snprintf(o, n, "%u", cfg().defVel); },
                   [this](int d) { cfg().defVel = static_cast<uint8_t>(clampi(cfg().defVel + d, 1, 127)); }};
  params_[kGate] = {"Def gate", [this](char* o, int n) { snprintf(o, n, "%u%%", mt::gatePercent(cfg().defGate)); },
                    [this](int d) { cfg().defGate = static_cast<uint8_t>(clampi(cfg().defGate + d, 1, 200)); }};
  params_[kCcA] = {"CC A", [this](char* o, int n) { snprintf(o, n, "%u", cfg().ccA); },
                   [this](int d) { cfg().ccA = static_cast<uint8_t>(clampi(cfg().ccA + d, 0, 127)); }};
  params_[kCcB] = {"CC B", [this](char* o, int n) { snprintf(o, n, "%u", cfg().ccB); },
                   [this](int d) { cfg().ccB = static_cast<uint8_t>(clampi(cfg().ccB + d, 0, 127)); }};
  params_[kProgram] = {"Program",
                       [this](char* o, int n) {
                         if (cfg().program == mt::kNoProgram) snprintf(o, n, "---");
                         else snprintf(o, n, "%u", cfg().program);
                       },
                       [this](int d) {
                         const int v = cfg().program == mt::kNoProgram ? -1 : cfg().program;
                         const int nv = clampi(v + d, -1, 127);
                         cfg().program = nv < 0 ? mt::kNoProgram : static_cast<uint8_t>(nv);
                         engine::post(engine::Cmd::SendProgram, static_cast<uint16_t>(app_.curTrack()));
                       }};
  params_[kMute] = {"Mute", [this](char* o, int n) { onOff(cfg().mute, o, n); },
                    [this](int d) { cfg().mute = d > 0; }};
  params_[kSolo] = {"Solo", [this](char* o, int n) { onOff(cfg().solo, o, n); },
                    [this](int d) { cfg().solo = d > 0; }};
  list_.setParams(params_, kRows);
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

void TrackScreen::editName(int delta) {
  char buf[kNameLen + 1];
  mt::TrackCfg& t = cfg();
  memset(buf, ' ', kNameLen);
  buf[kNameLen] = 0;
  const size_t len = strnlen(t.name, kNameLen);
  memcpy(buf, t.name, len);
  const int i = ((nameCharIndex(buf[namePos_]) + delta) % kNameCharCount + kNameCharCount) % kNameCharCount;
  buf[namePos_] = kNameChars[i];
  int end = kNameLen;
  while (end > 0 && buf[end - 1] == ' ') --end;  // keep stored names trimmed
  buf[end] = 0;
  memcpy(t.name, buf, end + 1);
}

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
