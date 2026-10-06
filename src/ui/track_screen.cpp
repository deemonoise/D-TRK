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

void TrackScreen::onEnter() {
  leaveEdit();
  if (mixSel_ != kMaster) mixSel_ = app_.curTrack() % kStrips;
}

void TrackScreen::changeTrack(int d) {
  leaveEdit();
  app_.setCurTrack(app_.curTrack() + d);
}

void TrackScreen::editName(int delta) { editNameChar(cfg().name, kNameLen, namePos_, delta); }

void TrackScreen::setMixer(bool on) {
  if (on == mixer_) return;
  leaveEdit();
  mixer_ = on;
  mixSel_ = app_.curTrack() % kStrips;
  app_.invalidate();
}

void TrackScreen::onInput(const hw::InputEvent& ev) {
  if (mixer_) {
    mixerInput(ev);
    return;
  }
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
  if (mixer_) {
    mixerTouch(ev);
    return;
  }
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
  if (mixer_) {
    drawMixer(s, y0);
    return;
  }
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

// ---- MIXER ----

int TrackScreen::firstTrack() const { return app_.curTrack() / kStrips * kStrips; }

// Mutes, solos, the half's volumes, the selection and the master: the mixer redraws when they change
// (from here, the track buttons or another screen), not with the play position.
uint32_t TrackScreen::mixSignature() const {
  const mt::Project& p = app_.project();
  uint32_t h = 2166136261u;
  auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
  mix(static_cast<uint32_t>(app_.curTrack()));
  mix(static_cast<uint32_t>(mixSel_));
  mix(p.masterVol);
  for (int t = 0; t < mt::kTracks; ++t) mix(p.tracks[t].mute | p.tracks[t].solo << 1 | static_cast<uint32_t>(p.tracks[t].out) << 2);
  for (int k = 0; k < kStrips; ++k) {
    const mt::TrackCfg& t = p.tracks[firstTrack() + k];
    const mt::Instrument& in = p.instruments[t.instr % mt::kInstruments];
    mix(t.vol | in.send << 8 | in.rsend << 16);
  }
  return h;
}

bool TrackScreen::wantsRedraw(const engine::Status&) {
  if (!mixer_) return false;
  if (mixSel_ != kMaster && mixSel_ != app_.curTrack() % kStrips) mixSel_ = app_.curTrack() % kStrips;
  const uint32_t sig = mixSignature();
  if (sig == mixSig_) return false;
  mixSig_ = sig;
  return true;
}

void TrackScreen::setVol(int track, int v) {
  mt::TrackCfg& t = app_.project().tracks[track];
  if (t.out != mt::TrackOut::Int) return;  // MIDI: no volume here
  v = clampi(v, 0, 127);
  if (v == t.vol) return;
  engine::lockProject();
  t.vol = static_cast<uint8_t>(v);
  engine::unlockProject();
  app_.markDirty();
}

void TrackScreen::setMasterVol(int v) {
  v = clampi(v, 0, mt::kMasterVolMax);
  if (v == app_.project().masterVol) return;
  engine::lockProject();
  app_.project().masterVol = static_cast<uint8_t>(v);
  engine::unlockProject();
  app_.invalidate();
}

void TrackScreen::toggleMuteSolo(int track, bool solo) {
  mt::TrackCfg& t = app_.project().tracks[track];
  engine::lockProject();
  if (solo) t.solo = !t.solo;
  else t.mute = !t.mute;
  engine::unlockProject();
  app_.markDirty();
}

void TrackScreen::mixerInput(const hw::InputEvent& ev) {
  using hw::InputType;
  switch (ev.type) {
    case InputType::EncTurn:
      if (ev.shift) {
        // One line: strips 1-8, MAIN, strips 9-16; the cursor follows the strips (and the half).
        const int step = ev.delta > 0 ? 1 : -1;
        for (int n = ev.delta > 0 ? ev.delta : -ev.delta; n > 0; --n) {
          const int cur = app_.curTrack();
          if (mixSel_ == kMaster) {  // MAIN sits between tracks 8 and 9
            const int t = step > 0 ? kStrips : kStrips - 1;
            app_.setCurTrack(t);
            mixSel_ = t % kStrips;
          } else if ((step > 0 && cur == kStrips - 1) || (step < 0 && cur == kStrips)) {
            app_.setCurTrack(kStrips - 1);  // shown with strips 1-8
            mixSel_ = kMaster;
          } else {
            const int t = cur + step;
            if (t < 0 || t >= mt::kTracks) break;
            app_.setCurTrack(t);
            mixSel_ = t % kStrips;
          }
        }
      } else if (mixSel_ == kMaster) {
        setMasterVol(app_.project().masterVol + ev.delta);
      } else {
        const int t = app_.curTrack();
        setVol(t, app_.project().tracks[t].vol + ev.delta * (ev.shift ? 10 : 1));
      }
      break;
    case InputType::EncClick:
      if (mixSel_ != kMaster) toggleMuteSolo(app_.curTrack(), false);
      break;
    default: break;
  }
}

bool TrackScreen::hitStrip(int x, int y, int& strip, Part& part) const {
  const int ly = y - y0_;
  if (x >= kMasterX && x < kMasterX + kMasterW) {
    strip = kMaster;
    if (ly < kFaderY) part = Part::Name;
    else if (ly < kFaderY + kFaderH + 8) part = Part::Fader;
    else return false;
    return true;
  }
  if (x < kStripX0 || x >= kStripX0 + kStrips * kStripW) return false;
  strip = (x - kStripX0) / kStripW;
  const int lx = (x - kStripX0) % kStripW;
  if (ly < kFaderY) part = Part::Name;
  else if (ly < kFaderY + kFaderH + 8) part = Part::Fader;
  else if (ly >= kBtnY - 4 && ly < kBtnY + kBtnH + 4) part = lx < 3 + kBtnW + 1 ? Part::Mute : Part::Solo;
  else return false;
  return true;
}

void TrackScreen::mixerTouch(const TouchEvent& ev) {
  if (ev.type == TouchType::HDrag) return;
  int strip;
  Part part;
  // A drag keeps to the fader it started on.
  const int sx = ev.type == TouchType::Drag ? ev.x0 : ev.x, sy = ev.type == TouchType::Drag ? ev.y0 : ev.y;
  if (!hitStrip(sx, sy, strip, part)) return;
  if (ev.type == TouchType::Drag && part != Part::Fader) return;
  const int fy = y0_ + kFaderY;
  const int pos = clampi(ev.y - fy, 0, kFaderH);  // 0 = top
  if (strip == kMaster) {
    mixSel_ = kMaster;
    if (part == Part::Fader) setMasterVol(mt::kMasterVolMax - pos * mt::kMasterVolMax / kFaderH);
    app_.invalidate();
    return;
  }
  const int tr = firstTrack() + strip;
  if (ev.type == TouchType::Tap && (part == Part::Mute || part == Part::Solo)) {
    toggleMuteSolo(tr, part == Part::Solo);
    return;
  }
  app_.setCurTrack(tr);
  mixSel_ = strip;
  if (part == Part::Fader) setVol(tr, 127 - pos * 127 / kFaderH);
  app_.invalidate();
}

void TrackScreen::drawFader(LGFX_Sprite& s, int x, int y, int value, int max, uint16_t fill) {
  s.drawRect(x, y, kFaderW, kFaderH, kDim);
  const int h = clampi(value, 0, max) * (kFaderH - 2) / max;
  if (h > 0) s.fillRect(x + 1, y + kFaderH - 1 - h, kFaderW - 2, h, fill);
}

void TrackScreen::drawMixer(LGFX_Sprite& s, int y0) {
  const mt::Project& p = app_.project();
  char buf[12];
  for (int k = 0; k < kStrips; ++k) {
    const int tr = firstTrack() + k;
    const mt::TrackCfg& t = p.tracks[tr];
    const bool internal = t.out == mt::TrackOut::Int;
    const bool audible = p.trackAudible(tr);
    const int x = kStripX0 + k * kStripW;
    if (mixSel_ == k) s.drawRect(x, y0 + 1, kStripW - 2, kAreaH - 2, kCursor);
    snprintf(buf, sizeof(buf), "%.6s", t.name);  // 6 x 8 px fit the strip
    s.setTextColor(t.solo ? kCursor : (audible ? kText : kDim));
    s.drawString(buf, x + 2, y0 + kNameY);
    const int fx = x + (kStripW - 2 - kFaderW) / 2, fy = y0 + kFaderY;
    if (internal) drawFader(s, fx, fy, t.vol, 127, audible ? kText : kDim);
    else s.drawRect(fx, fy, kFaderW, kFaderH, kDim);
    if (internal) snprintf(buf, sizeof(buf), "%3u", t.vol);
    else snprintf(buf, sizeof(buf), "MIDI");
    s.setTextColor(internal ? kText : kDim);
    s.drawString(buf, x + 6, y0 + kValY);
    const mt::Instrument& in = p.instruments[t.instr % mt::kInstruments];
    s.setTextColor(kDim);
    if (internal) snprintf(buf, sizeof(buf), "S%3u", in.send);
    else snprintf(buf, sizeof(buf), "S --");
    s.drawString(buf, x + 6, y0 + kSendY);
    if (internal) snprintf(buf, sizeof(buf), "R%3u", in.rsend);
    else snprintf(buf, sizeof(buf), "R --");
    s.drawString(buf, x + 6, y0 + kSendY + 18);
    const int by = y0 + kBtnY;
    s.fillRect(x + 3, by, kBtnW, kBtnH, t.mute ? kRed : kBeatBg);
    s.fillRect(x + 3 + kBtnW + 2, by, kBtnW, kBtnH, t.solo ? kCursor : kBeatBg);
    s.setTextColor(t.mute ? kText : kDim);
    s.drawString("M", x + 3 + (kBtnW - kCharW) / 2, by + 1);
    s.setTextColor(t.solo ? kBg : kDim);
    s.drawString("S", x + 3 + kBtnW + 2 + (kBtnW - kCharW) / 2, by + 1);
  }
  // Master: masterVol 0..200 %; above 100 % the fill turns yellow (the soft clip works harder).
  s.drawFastVLine(kMasterX - 4, y0, kAreaH, kDim);
  if (mixSel_ == kMaster) s.drawRect(kMasterX, y0 + 1, kMasterW, kAreaH - 2, kCursor);
  s.setTextColor(kText);
  s.drawString("MAIN", kMasterX + (kMasterW - 4 * kCharW) / 2, y0 + kNameY);
  const int mv = p.masterVol;
  drawFader(s, kMasterX + (kMasterW - kFaderW) / 2, y0 + kFaderY, mv, mt::kMasterVolMax, mv > 100 ? kCursor : kText);
  snprintf(buf, sizeof(buf), "%u%%", mv);
  s.drawString(buf, kMasterX + (kMasterW - static_cast<int>(strlen(buf)) * kCharW) / 2, y0 + kValY);
  s.setTextColor(kDim);
  snprintf(buf, sizeof(buf), "%d-%d", firstTrack() + 1, firstTrack() + kStrips);
  s.drawString(buf, kMasterX + (kMasterW - static_cast<int>(strlen(buf)) * kCharW) / 2, y0 + kBtnY);
}

}  // namespace ui
