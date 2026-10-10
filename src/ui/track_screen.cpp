#include "track_screen.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "audio/audio.h"
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
                        // Whole-pattern snapshot, once per run of Pat len edits on one pattern.
                        if (app_.editSeq() != patLenSeq_ || app_.editPattern() != patLenPat_) app_.pushUndo();
                        v = static_cast<uint8_t>(nv);
                        patLenSeq_ = app_.editSeq() + 1;  // onEdit marks dirty next
                        patLenPat_ = app_.editPattern();
                      }};
  // The track's speed in the edited pattern: 1/4 1/2 x1 x2 x4 (left to right).
  params_[kSpeed] = {"Speed",
                     [this](char* o, int n) {
                       const mt::Pattern& pt = app_.project().patterns[app_.editPattern()];
                       snprintf(o, n, "%s", mt::speedName(mt::toSpeed(pt.trackSpeed[app_.curTrack()])));
                     },
                     [this](int d) {
                       static constexpr mt::TrackSpeed kOrder[] = {mt::TrackSpeed::Quarter, mt::TrackSpeed::Half,
                                                                   mt::TrackSpeed::X1, mt::TrackSpeed::X2,
                                                                   mt::TrackSpeed::X4};
                       constexpr int kN = sizeof(kOrder) / sizeof(kOrder[0]);
                       mt::Pattern& pt = app_.project().patterns[app_.editPattern()];
                       uint8_t& v = pt.trackSpeed[app_.curTrack()];
                       int i = 0;
                       while (i < kN && kOrder[i] != mt::toSpeed(v)) ++i;
                       if (i == kN) i = 2;  // x1
                       const int ni = clampi(i + d, 0, kN - 1);
                       if (kOrder[ni] == mt::toSpeed(v)) return;
                       // Whole-pattern snapshot, once per run of Speed edits on one pattern (as Pat len).
                       if (app_.editSeq() != speedSeq_ || app_.editPattern() != speedPat_) app_.pushUndo();
                       v = static_cast<uint8_t>(kOrder[ni]);
                       speedSeq_ = app_.editSeq() + 1;  // onEdit marks dirty next
                       speedPat_ = app_.editPattern();
                     }};
  params_[kHumanize] = {"Humanize",
                        [this](char* o, int n) {
                          if (cfg().humanize) snprintf(o, n, "%u", cfg().humanize);
                          else snprintf(o, n, "OFF");
                        },
                        [this](int d) { cfg().humanize = static_cast<uint8_t>(clampi(cfg().humanize + d, 0, 100)); }};
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
  list_.setOnEdit([this] { app_.markDirty(); });
  // Cancel: the name is copied back (several characters may have changed); the other rows step
  // back through their edit(), which also sends what they send (program, output).
  list_.setEditScope([this]() -> void* { return nameEdit() ? cfg().name : nullptr; }, sizeof(mt::TrackCfg::name));
  list_.setOnCancel([this] {
    namePos_ = 0;
    fixNames();
    app_.toast("CANCEL");
  });
  setPageRows(kPgMain, false);  // no leaveEdit(): the App is a global, built before the project exists
}

void TrackScreen::setPageRows(int page, bool bar) {
  page_ = (page % kPages + kPages) % kPages;
  list_.setPageBar(true);
  list_.setParams(params_ + kPageFirst[page_], kPageFirst[page_ + 1] - kPageFirst[page_]);
  list_.setVisibleRows(kVisibleRows);
  if (bar) list_.selectBar();
  else list_.setSel(0);
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

void TrackScreen::onLeave() {
  leaveEdit();
  audio::setMeters(false);  // the level meters cost render time: only while MIX is shown
}

void TrackScreen::showPage(int page, bool bar) {
  leaveEdit();
  setPageRows(page, bar);
}

// The track changed under an open edit (a track button, GRID): the edit belonged to the old one.
void TrackScreen::followTrack() {
  if (app_.curTrack() == editTrack_) return;
  if (list_.editing()) leaveEdit();
  editTrack_ = app_.curTrack();
}

void TrackScreen::onEnter() {
  leaveEdit();
  editTrack_ = app_.curTrack();
  audio::setMeters(mixer_);
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
  audio::setMeters(on);
  app_.invalidate();
}

void TrackScreen::onInput(const hw::InputEvent& ev) {
  followTrack();
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
  // A + turn on the name row: a character, Shift = the position.
  if (ev.type == hw::InputType::EditTurn && page_ == kPgMain && list_.sel() == kName && list_.holdEdit()) {
    if (ev.shift) namePos_ = clampi(namePos_ + ev.delta, 0, kNameLen - 1);
    else list_.edit(ev.delta);
    return;
  }
  if (const int ov = list_.onInput(ev)) {  // click (Shift+click) on the page bar: the next (previous) page
    showPage(page_ + ov, true);
    return;
  }
  if (wasName && !nameEdit()) leaveEdit();
}

// B + Shift + turn: the previous / next page; the cursor stays on the page bar if it was there.
void TrackScreen::onPage(int d) {
  if (mixer_) return;
  showPage(page_ + d, list_.barSelected());
}

// B: leaves the edit with the value kept (as the second click), a name edit gets fixNames().
void TrackScreen::onBack() {
  if (!mixer_ && list_.editing()) leaveEdit();
}

void TrackScreen::onTouch(const TouchEvent& ev) {
  followTrack();
  if (mixer_) {
    mixerTouch(ev);
    return;
  }
  if (ev.type == TouchType::Tap && ev.y < y0_ + kHeaderH) {
    if (ev.x < kArrowW) changeTrack(-1);
    else if (ev.x >= kScreenW - kArrowW) changeTrack(1);
    return;
  }
  if (ev.y >= y0_ + kHeaderH && ev.y < y0_ + kHeaderH + PageBar::kH) {
    if (ev.type == TouchType::Tap) showPage(PageBar::at(ev.x, kPages), true);
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
  followTrack();
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

  static const char* const kNames[kPages] = {"MAIN", "NOTE", "MIDI"};
  PageBar::draw(s, y0 + kHeaderH, kNames, kPages, page_);
  if (list_.barSelected()) PageBar::drawFocus(s, y0 + kHeaderH);
  list_.draw(s, y0 + kHeaderH + PageBar::kH);
  if (nameEdit()) {
    const int uy = list_.rowY(kName) + (ParamList::kRowH + kCharH) / 2;
    s.fillRect(ParamList::kValueX + namePos_ * kCharW, uy, kCharW, 2, kEditCursor);
  }
}

// ---- MIXER ----

int TrackScreen::firstTrack() const { return app_.curTrack() / kStrips * kStrips; }

// Mutes, solos, the half's volumes and the master: the mixer redraws when they change (from here, the
// track buttons or another screen); the scope adds its own frames.
uint32_t TrackScreen::mixSignature() const {
  const mt::Project& p = app_.project();
  uint32_t h = 2166136261u;
  auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
  mix(static_cast<uint32_t>(app_.curTrack()));
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
  const uint32_t sig = mixSignature();
  const bool scope = millis() - scopeMs_ >= kScopeMs;
  if (sig == mixSig_ && !scope) return false;
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
  app_.markDirty();
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
  if (ev.type == hw::InputType::EditTurn) {  // A + turn: master volume, Shift = x10
    setMasterVol(app_.project().masterVol + ev.delta * (ev.shift ? 10 : 1));
    return;
  }
  if (ev.type != hw::InputType::EncTurn) return;
  if (ev.shift) {  // A / B: the same strip in the other half
    app_.setCurTrack((app_.curTrack() + kStrips) % mt::kTracks);
    app_.invalidate();
    return;
  }
  setMasterVol(app_.project().masterVol + ev.delta);
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
    if (part == Part::Fader) setMasterVol(mt::kMasterVolMax - pos * mt::kMasterVolMax / kFaderH);
    app_.invalidate();
    return;
  }
  const int tr = firstTrack() + strip;
  if (ev.type == TouchType::Tap && (part == Part::Mute || part == Part::Solo)) {
    toggleMuteSolo(tr, part == Part::Solo);
    return;
  }
  if (part == Part::Fader) setVol(tr, 127 - pos * 127 / kFaderH);
  app_.invalidate();
}

void TrackScreen::drawFader(LGFX_Sprite& s, int x, int y, int value, int max, uint16_t fill) {
  s.drawRect(x, y, kFaderW, kFaderH, kDim);
  const int h = clampi(value, 0, max) * (kFaderH - 2) / max;
  if (h > 0) s.fillRect(x + 1, y + kFaderH - 1 - h, kFaderW - 2, h, fill);
}

// Output scope under the strips: the last samples to the speaker, scaled to the recent peak (quiet
// material still fills the box; the meter shows the real level), the peak meter and CLIP (held 1 s).
void TrackScreen::drawScope(LGFX_Sprite& s, int y0) {
  scopeMs_ = millis();
  constexpr int kX = kStripX0, kW = kScreenW - 2 * kStripX0, kH = kScopeH;
  constexpr int kN = kW - kMeterW - 4;  // one sample per pixel
  const int y = y0 + kScopeY, mid = y + kH / 2;
  s.fillRect(kX, y, kW, kH, kBeatBg);
  s.drawFastHLine(kX, mid, kN, kDim);
  int16_t buf[kN];
  audio::scopeRead(buf, kN);
  int pk = 0;
  for (int i = 0; i < kN; ++i) pk = buf[i] < 0 ? (-buf[i] > pk ? -buf[i] : pk) : (buf[i] > pk ? buf[i] : pk);
  // Auto gain: the frame's peak fills ~90 % of the half height; fast down (louder), slow up, at most
  // x32 (-30 dB), so silence stays a line instead of magnified noise.
  const float want = pk > 0 ? 0.9f * 32768.f / pk : 32.f;
  const float target = want > 32.f ? 32.f : (want < 1.f ? 1.f : want);
  scopeGain_ = target < scopeGain_ ? target : scopeGain_ + (target - scopeGain_) * 0.1f;
  const float k = scopeGain_ * (kH / 2 - 1) / 32768.f;
  int prev = mid;
  for (int i = 0; i < kN; ++i) {
    int yy = mid - static_cast<int>(buf[i] * k);
    yy = clampi(yy, y, y + kH - 1);
    if (i) s.drawLine(kX + i - 1, prev, kX + i, yy, kGreen);
    prev = yy;
  }
  const int16_t peak = audio::scopePeak();
  if (peak >= 32000) clipMs_ = scopeMs_ | 1;
  meter_ = peak > meter_ ? peak : meter_ * 7 / 8;  // fast up, slow down
  const int mx = kX + kW - kMeterW;
  const int mh = meter_ * kH / 32768;
  s.fillRect(mx, y + kH - mh, kMeterW, mh, meter_ > 29000 ? kYellow : kGreen);
  s.setTextColor(kDim);
  char gain[8];
  snprintf(gain, sizeof(gain), "x%d", static_cast<int>(scopeGain_ + 0.5f));
  s.drawString(gain, kX + 4, y + 2);
  if (clipMs_ && scopeMs_ - clipMs_ < 1000) {
    s.setTextColor(kRed);
    s.drawString("CLIP", mx - 4 * kCharW - 6, y + 2);
  }
}

void TrackScreen::updateMeters() {
  float pk[mt::kTracks];
  audio::trackPeaks(pk);
  const uint32_t now = millis();
  for (int t = 0; t < mt::kTracks; ++t) {
    const float db = pk[t] > 1e-6f ? 20.f * log10f(pk[t]) : -120.f;
    float v = (db + 48.f) * (1.f / 48.f);
    v = v < 0 ? 0 : (v > 1 ? 1 : v);
    level_[t] = v > level_[t] ? v : (level_[t] - 0.06f > v ? level_[t] - 0.06f : v);
    if (pk[t] >= 0.99f) clipAt_[t] = now | 1;
  }
}

void TrackScreen::drawMixer(LGFX_Sprite& s, int y0) {
  const mt::Project& p = app_.project();
  char buf[12];
  updateMeters();
  const uint32_t now = millis();
  for (int k = 0; k < kStrips; ++k) {
    const int tr = firstTrack() + k;
    const mt::TrackCfg& t = p.tracks[tr];
    const bool internal = t.out == mt::TrackOut::Int;
    const bool audible = p.trackAudible(tr);
    const int x = kStripX0 + k * kStripW;
    snprintf(buf, sizeof(buf), "%.6s", t.name);  // 6 x 8 px fit the strip
    s.setTextColor(t.solo ? kCursor : (audible ? kText : kDim));
    s.drawString(buf, x + 2, y0 + kNameY);
    const int fx = x + (kStripW - 2 - kFaderW) / 2, fy = y0 + kFaderY;
    if (internal) drawFader(s, fx, fy, t.vol, 127, audible ? kText : kDim);
    else s.drawRect(fx, fy, kFaderW, kFaderH, kDim);
    if (internal) {  // level meter: green, yellow above -6 dB, red for a second after full scale
      const int mx = fx + kFaderW + 3;
      s.fillRect(mx, fy, kMeterBarW, kFaderH, kBeatBg);
      const int mh = static_cast<int>(level_[tr] * kFaderH + 0.5f);
      const int yel = static_cast<int>(kFaderH * 42.f / 48.f);  // -6 dB
      const bool clip = clipAt_[tr] && now - clipAt_[tr] < 1000;
      if (mh > 0) s.fillRect(mx, fy + kFaderH - (mh < yel ? mh : yel), kMeterBarW, mh < yel ? mh : yel, kGreen);
      if (mh > yel) s.fillRect(mx, fy + kFaderH - mh, kMeterBarW, mh - yel, kYellow);
      if (clip) s.fillRect(mx, fy, kMeterBarW, 3, kRed);
    }
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
    s.drawString(buf, x + 6, y0 + kSendY + kSendDy);
    const int by = y0 + kBtnY;
    s.fillRect(x + 3, by, kBtnW, kBtnH, t.mute ? kRed : kBeatBg);
    s.fillRect(x + 3 + kBtnW + 2, by, kBtnW, kBtnH, t.solo ? kCursor : kBeatBg);
    s.setTextColor(t.mute ? kText : kDim);
    s.drawString("M", x + 3 + (kBtnW - kCharW) / 2, by + (kBtnH - kCharH) / 2);
    s.setTextColor(t.solo ? kBg : kDim);
    s.drawString("S", x + 3 + kBtnW + 2 + (kBtnW - kCharW) / 2, by + (kBtnH - kCharH) / 2);
  }
  // Master: masterVol 0..200 %; above 100 % the fill turns yellow (the soft clip works harder).
  s.drawFastVLine(kMasterX - 4, y0, kScopeY - 4, kDim);
  s.setTextColor(kText);
  s.drawString("MAIN", kMasterX + (kMasterW - 4 * kCharW) / 2, y0 + kNameY);
  const int mv = p.masterVol;
  drawFader(s, kMasterX + (kMasterW - kFaderW) / 2, y0 + kFaderY, mv, mt::kMasterVolMax, mv > 100 ? kCursor : kText);
  snprintf(buf, sizeof(buf), "%u%%", mv);
  s.drawString(buf, kMasterX + (kMasterW - static_cast<int>(strlen(buf)) * kCharW) / 2, y0 + kValY);
  // The half shown: A = tracks 1-8, B = 9-16 (Shift + turn).
  s.setTextColor(kCursor);
  const char* half = firstTrack() ? "B" : "A";
  s.drawString(half, kMasterX + (kMasterW - kCharW) / 2, y0 + kSendY);
  s.setTextColor(kDim);
  snprintf(buf, sizeof(buf), "%d-%d", firstTrack() + 1, firstTrack() + kStrips);
  s.drawString(buf, kMasterX + (kMasterW - static_cast<int>(strlen(buf)) * kCharW) / 2, y0 + kSendY + kSendDy);
  drawScope(s, y0);
}

}  // namespace ui
