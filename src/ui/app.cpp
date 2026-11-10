#include "app.h"
#include <new>
#include <stdio.h>
#include <string.h>
#include "audio/audio.h"
#include "fx_info.h"
#include "scene.h"
#include "esp_heap_caps.h"
#include "hw/battery.h"
#include "hw/trackio.h"
#include "storage/settings.h"
#include "storage/storage.h"
#include "hw/sdcard.h"
#include "track_buttons.h"

namespace ui {
namespace {

// PSRAM first, internal RAM as fallback; elements are constructed in place.
template <class T>
T* allocArray(size_t n) {
  void* m = heap_caps_malloc(sizeof(T) * n, MALLOC_CAP_SPIRAM);
  if (!m) m = heap_caps_malloc(sizeof(T) * n, MALLOC_CAP_8BIT);
  if (!m) return nullptr;
  T* a = static_cast<T*>(m);
  for (size_t i = 0; i < n; ++i) new (&a[i]) T();
  return a;
}

}  // namespace

App::FrameBufferLoan::FrameBufferLoan(App& app)
    : lease_(app.frameBuffer_,app.spr_,App::waitFrameBuffer,App::createFrameBuffer,&app) {}

void App::waitFrameBuffer(void* context) {
  auto& app = *static_cast<App*>(context);
  if (app.lcd_) app.lcd_->waitDMA();
}

bool App::createFrameBuffer(LGFX_Sprite& sprite, void*) {
  if (!sprite.createSprite(kScreenW,kScreenH)) return false;
  sprite.setFont(font());
  return true;
}

void App::prepareProjectLoadMemory() { proj_.relocateDiceUndo(); }

void App::begin(LGFX* lcd, mt::Project* p) {
  lcd_ = lcd;
  p_ = p;
  setParamEditObserver([this](bool before) { motionObserve(before); });
  setParamHintObserver([this](const char* hint) { toast(hint); });

  undoBuf_ = allocArray<mt::Undo::Entry>(mt::Undo::kDepth + 1);
  if (undoBuf_) undo_ = mt::Undo(undoBuf_);
  else Serial.println("ui: no memory for undo, disabled");
  clip_ = allocArray<mt::Clipboard>(1);
  if (!clip_) Serial.println("ui: no memory for clipboard");

  spr_ = new LGFX_Sprite(lcd_);
  spr_->setPsram(true);
  spr_->setColorDepth(16);
  if (!spr_->createSprite(kScreenW, kScreenH)) {
    Serial.println("ui: sprite alloc failed (PSRAM?)");
    delete spr_;
    spr_ = nullptr;
  } else {
    spr_->setFont(font());
  }
  Serial.printf("psram free %u KB\n", static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));

  // Device settings in NVS: the colour theme, the autosave interval, the phones level, preview,
  // perf visuals.
  const int theme = storage::loadTheme(0);
  theme_ = savedTheme_ = theme < themeCount() ? theme : 0;
  applyTheme(theme_);
  const int asv = storage::loadSetting("autosave", 5);
  autosaveMin_ = savedAutosaveMin_ = asv <= 60 ? asv : 5;
  const int ph = storage::loadSetting("phones", 100);
  phones_ = savedPhones_ = ph <= 100 ? ph : 100;
  audio::setPhones(static_cast<uint8_t>(phones_));
  preview_ = savedPreview_ = storage::loadSetting("preview", 1) != 0;
  visuals_ = savedVisuals_ = storage::loadSetting("perfvis", 1) != 0;
  practicePrefs_ = mt::practicePrefs(storage::loadSetting("practice", 0), storage::loadSetting("clickvol", 64));
  savedPracticeBits_ = mt::practiceBits(practicePrefs_);
  savedClickLevel_ = practicePrefs_.clickLevel;
  engine::post(engine::Cmd::PracticeSettings, savedPracticeBits_, savedClickLevel_);

  proj_.loadDicePrefs();
  status_ = engine::status();
  lastBpm_ = p_->bpm;
  if (perfTab(tab_)) perf_.attach();
  screen()->onEnter();
  dirty_ = true;
}

void App::setTab(Tab t) {
  if (t == Tab::Count || !screens_[static_cast<int>(t)]) return;
  setBpmEdit(false);
  if (t == tab_) return;
  rawTouch_.reset();
  perf_.setXy(false);  // the pad belongs to the tab it was opened on
  xyPad_.clear();
  menu_.close();
  if (perfTab(tab_)) perf_.allOff();  // leaving releases the held effects
  screen()->onLeave();
  // GRID / SONG / MIX keep the PERF buffer between them; any other tab frees it (its PSRAM is needed there).
  if (perfTab(t)) perf_.attach();
  else perf_.detach();
  tab_ = t;
  if (t == Tab::Track || t == Tab::Mix) track_.setMixer(t == Tab::Mix);  // one screen, two views
  screen()->onEnter();
  dirty_ = true;
}

void App::recordShortcut(bool motionOnly) {
  if (transportLocked_) { toast("RECORDING UNAVAILABLE"); return; }
  if (menu_.isOpen() || bpmEdit_ || grid_.dialogOpen()) {
    toast("CLOSE DIALOG FIRST");
    return;
  }
  const auto mode = mt::recordModeToggle({grid_.recording(), motionRecording()}, motionOnly);
  setRecording(mode.notes, mode.motion);
  if (mode.motion && !status_.playing) {
    if (!status_.paused) postSong(tab_ == Tab::Song && p_->songLen > 0);
    engine::post(engine::Cmd::StartStop);
  }
  toast(!mode.motion ? "REC OFF" : (mode.notes ? "REC: NOTES + MOTION" : "REC: MOTION ONLY"));
}

void App::transport() {
  if (transportLocked_) {
    toast("WI-FI MODE");
    return;
  }
  if (shift_) {
    startSong();
    return;
  }
  // Stopped: the start plays the song from the SONG cursor on the SONG tab, else loops.
  if (!status_.playing && !status_.paused) postSong(tab_ == Tab::Song && p_->songLen > 0);
  engine::post(engine::Cmd::StartStop);
}

// Shift + Play: the song from the SONG cursor, on any tab; playing, it starts again from there.
void App::startSong() {
  if (p_->songLen == 0) {
    toast("SONG EMPTY");
    return;
  }
  engine::post(engine::Cmd::Stop);
  postSong(true);
  engine::post(engine::Cmd::StartStop);
}

void App::postSong(bool on) {
  engine::post(engine::Cmd::SetSong, static_cast<uint16_t>((on ? 0x8000 : 0) | (song_.cursor() & 0xff)));
}

void App::setBpmEdit(bool on) {
  // From the live tempo shown; Shift+click puts it back. While a song slot's BPM holds the live tempo, the
  // edit is live only (SetLiveBpm): the project BPM stays (PROJ edits it).
  if (on && !bpmEdit_) {
    bpmTarget_ = p_->bpm;
    bpmOrig_ = p_->bpm;
    bpmTurned_ = false;
  }
  bpmEdit_ = on;
}

void App::onInput(const hw::InputEvent& ev) {
  lastInputMs_ = millis();
  using hw::InputType;
  shift_ = ev.shift;
  dirty_ = true;
  switch (ev.type) {
    case InputType::ADown: abAction(ab_.aDown()); return;
    case InputType::AUp: abAction(ab_.aUp(ev.shift)); return;
    case InputType::BDown: abAction(ab_.bDown()); return;
    case InputType::BUp: abAction(ab_.bUp(ev.shift)); return;
    case InputType::PlayPress:
      // A drawing document keeps Play even while its action/confirmation menu is open.
      if (screen()->wantsRawTouch() && screen()->onPlay()) return;
      if (const auto o = ab_.play(); o.act != mt::AbAction::Pass) {
        recordShortcut(o.act == mt::AbAction::RecordMotion);
        return;
      }
      if (!menu_.isOpen() && !bpmEdit_ && screen()->onPlay()) return;
      if (ev.shift && status_.playing && !transportLocked_) {
        fillDown();
        return;
      }
      transport();
      return;
    case InputType::PlayRelease:
      fillUp();
      return;
    case InputType::TrackRelease:
      if (ev.delta == heldTrackBtn_) {
        heldTrackBtn_ = -1;
        holdMode_ = mt::HoldMode::Vol;
      }
      trackRelease(ev.delta);
      return;
    case InputType::EncClick:
      if (sceneKeys()) {  // SCENE view: a held track button + click = unset its lock, A + click = the master's
        if (heldTrackBtn_ >= 0) {
          track_.sceneHoldClick(heldTrack());
          return;
        }
        if (ab_.aClick().act == mt::AbAction::AClick) {
          track_.sceneMasterUnset();
          return;
        }
      }
      if (heldTrackBtn_ >= 0 && !menu_.isOpen() && !bpmEdit_ && holdVolume()) {  // consumed: VOL -> PAN (-> FLIP)
        const int tr = heldTrack();
        holdMode_ = mt::holdNext(holdMode_, p_->trackInternal(tr), p_->genOn(tr));
        holdToast(tr);
        return;
      }
      break;
    case InputType::EncTurn:
      if (const mt::AbOut o = ab_.turn(ev.delta, ev.shift); o.act != mt::AbAction::Pass) {
        abAction(o);
        return;
      }
      if (heldTrackBtn_ >= 0 && sceneKeys()) {
        track_.sceneHoldTurn(heldTrack(), ev.delta * (ev.shift ? 8 : 1));  // SCENE view: the scene's value
        return;
      }
      if (heldTrackBtn_ >= 0 && !menu_.isOpen() && !bpmEdit_ && holdVolume()) {  // consumed
        const int d = ev.delta * (ev.shift ? 10 : 1);
        if (holdMode_ == mt::HoldMode::Pan) nudgeTrackPan(heldTrack(), d);
        else if (holdMode_ == mt::HoldMode::Flip) nudgeTrackFlip(heldTrack(), d);
        else nudgeTrackVol(heldTrack(), d);
        return;
      }
      break;
    case InputType::ShiftDown: shift_ = true; return;
    case InputType::ShiftUp: shift_ = false; return;
    case InputType::TrackPress:
      if (const mt::AbOut o = ab_.track(ev.delta, ev.shift); o.act != mt::AbAction::Pass) {
        abAction(o);  // a chord: no hold-button volume for this press
        return;
      }
      heldTrackBtn_ = ev.delta;
      // A new hold starts on the volume (a MIDI track with a generator: on its Flip).
      holdMode_ = mt::holdFirst(p_->trackInternal(heldTrack()), p_->genOn(heldTrack()));
      if (!menu_.isOpen()) trackKey(ev.delta, ev.shift);
      return;
    case InputType::EditEnd:  // A released: always ends the screen's hold edit (menu / BPM edit or not)
      screen()->onInput(ev);
      return;
    default: break;
  }
  // The pad hides the screen: the encoder only turns PERF's last pressed effect, A + turn the scene
  // crossfader (nothing held).
  if (xyMode() && !menu_.isOpen() && !bpmEdit_) {
    if (ev.type == InputType::EncTurn) perf_.turn(ev.delta, ev.shift);
    if (ev.type == InputType::EditTurn) perf_.xfadeTurn(ev.delta, ev.shift);
    if (ev.type == InputType::EncTurn || ev.type == InputType::EncClick || ev.type == InputType::EncLong ||
        ev.type == InputType::EditTurn || ev.type == InputType::EditCancel)
      return;
  }
  if (menu_.isOpen()) {
    menu_.onInput(ev);
    return;
  }
  if (bpmEdit_) {
    if (ev.type == InputType::EditCancel) return;  // A chord: BPM edit stays
    if (ev.type == InputType::EncTurn || ev.type == InputType::EditTurn) {
      const int bpm = bpmTarget_ + ev.delta * (ev.shift ? 10 : 1);
      bpmTarget_ = bpm < 20 ? 20 : (bpm > 300 ? 300 : bpm);
      engine::post(engine::Cmd::SetLiveBpm, static_cast<uint16_t>(bpmTarget_));
      bpmTurned_ = true;
      markDirty();
    } else {
      if (ev.type == InputType::EncClick && ev.shift && bpmTurned_) {
        bpmTarget_ = bpmOrig_;
        engine::post(engine::Cmd::SetLiveBpm, static_cast<uint16_t>(bpmTarget_));
        toast("CANCEL");
      }
      setBpmEdit(false);
    }
    return;
  }
  screen()->onInput(ev);
}

// A / B chords (mt::AbKeys). Edit actions go through onInput as InputTypes: the menu ignores them,
// the BPM edit takes EditTurn as a turn, otherwise the screen gets them.
void App::abAction(const mt::AbOut& o) {
  using mt::AbAction;
  hw::InputType t;
  switch (o.act) {
    case AbAction::EditTurn: t = hw::InputType::EditTurn; break;
    case AbAction::EditEnd: t = hw::InputType::EditEnd; break;
    case AbAction::EditCancel: t = hw::InputType::EditCancel; break;
    case AbAction::TabTurn:
      if (sceneKeys()) track_.sceneMasterParam(o.delta);  // SCENE view: B + turn = the master parameter
      else cycleTab(o.delta);
      return;
    case AbAction::PageTurn:
      if (xyMode()) return;  // the pad hides the screen
      if (!menu_.isOpen() && !bpmEdit_) screen()->onPage(o.delta);
      return;
    case AbAction::Undo:
      if (menu_.isOpen()) return;
      if (tab_ == Tab::Grid && grid_.dialogOpen()) return;  // the Fill / Transpose preview owns the pattern
      setBpmEdit(false);
      if (tab_ == Tab::Grid) {
        grid_.undo();
      } else if (tab_ == Tab::Song) {
        song_.undo();
      } else {
        const bool ok = doUndo();
        if (ok) engine::post(engine::Cmd::ReleaseTies);
        toast(ok ? "UNDO" : "NOTHING TO UNDO");
      }
      return;
    case AbAction::Back:
      if (menu_.isOpen()) menu_.back();
      else if (bpmEdit_) setBpmEdit(false);
      else if (xyMode()) {
        perf_.setXy(false);
        xyPad_.clear();
        toast("XY OFF");
      } else screen()->onBack();
      return;
    case AbAction::ATap:
      if (xyMode()) return;  // the pad hides the screen
      if (!menu_.isOpen() && !bpmEdit_) screen()->onATap(o.shift);
      return;
    case AbAction::Solo:
      if (!menu_.isOpen()) soloKey(o.delta);
      return;
    case AbAction::QueuePattern:
      if (!menu_.isOpen()) queueKey(o.delta);
      return;
    case AbAction::ATrack:  // MIX: scene N on crossfader end A / B; elsewhere consumed (free)
    case AbAction::BTrack:
      if (tab_ == Tab::Mix && !menu_.isOpen()) track_.setSceneEnd(o.act == AbAction::BTrack, o.delta);
      return;
    case AbAction::Both:
      if (!menu_.isOpen() && !bpmEdit_) toggleXy();
      return;
    case AbAction::AThenB:  // GRID, PERF off: hold A + B clears the step / selection; else Both follows
      if (tab_ == Tab::Grid && !perf_.on() && !xyMode() && !menu_.isOpen() && !bpmEdit_ &&
          grid_.chordClear()) return;
      if (!menu_.isOpen() && !bpmEdit_) toggleXy();
      return;
    default: return;
  }
  onInput(hw::InputEvent{t, o.delta, o.shift});
}

// B + turn: tabs in a ring.
void App::cycleTab(int d) {
  constexpr int n = static_cast<int>(Tab::Count);
  setTab(static_cast<Tab>(((static_cast<int>(tab_) + d) % n + n) % n));  // closes the menu and the BPM edit
}

// Shift + A + button N: solo track N of the half holding the cursor.
void App::soloKey(int n) {
  if (n < 0 || n >= mt::kTrackButtons) return;
  const int track = (curTrack_ / mt::kTrackButtons) * mt::kTrackButtons + n;
  mt::TrackCfg& t = p_->tracks[track];
  engine::lockProject();
  t.solo = !t.solo;
  engine::unlockProject();
  markDirty();
  char msg[16];
  snprintf(msg, sizeof(msg), "TRACK %d %s", track + 1, t.solo ? "SOLO" : "ON");
  toast(msg);
}

// B + Shift + button N: queue pattern N of the bank holding the edit pattern.
void App::queueKey(int n) {
  if (n < 0 || n >= mt::kTrackButtons) return;
  const int pat = (editPattern() / mt::kTrackButtons) * mt::kTrackButtons + n;
  if (pat >= mt::kPatterns) return;
  if (songPlays()) {  // the sequencer ignores a queue while the song plays
    toast("SONG MODE");
    return;
  }
  engine::post(engine::Cmd::QueuePattern, static_cast<uint16_t>(pat));
  char msg[20];
  snprintf(msg, sizeof(msg), "PATTERN %02d%s", pat + 1, status_.playing ? " NEXT" : "");
  toast(msg);
}

// The pad is the work area; a finger leaving it keeps the nearest edge.
void App::xyTouch(const TouchEvent& ev) {
  const int y = ev.y - kAreaY;
  const int py = y < 0 ? 0 : (y >= kAreaH ? kAreaH - 1 : y);
  const uint32_t now = millis();
  if (ev.type == TouchType::Down) {
    xyPad_.touch(ev.x, py, now);
    if (!perf_.xyDown(ev.x, py, kScreenW, kAreaH)) toast(perf_.on() ? "XY: PRESS PLAY" : "PERF OFF");
  } else if (ev.type == TouchType::Move) {
    xyPad_.touch(ev.x, py, now);
    perf_.xyMove(ev.x, py, kScreenW, kAreaH);
  } else {
    xyPad_.lift();
    perf_.xyUp();
  }
}

void App::toggleXy() {
  if (!perfTab(tab_)) return;
  if (!perf_.on()) {
    toast("XY: TURN PERF ON");
    return;
  }
  const bool on = !perf_.xy();
  perf_.setXy(on);
  xyPad_.clear();
  if (on && perf_.xyAxis(0) == mt::PerfFx::None && perf_.xyAxis(1) == mt::PerfFx::None)
    toast("XY: NO EFFECTS (PROJ > PERF)");
  else toast(on ? "XY PAD" : "XY OFF");
}

void App::onTouch(const TouchEvent& ev) {
  lastInputMs_ = millis();
  dirty_ = true;
  if (ev.type == TouchType::Down || ev.type == TouchType::Move || ev.type == TouchType::Up) {
    const RawTouchPhase phase = ev.type == TouchType::Down ? RawTouchPhase::Down :
                                ev.type == TouchType::Up ? RawTouchPhase::Up : RawTouchPhase::Move;
    switch (rawTouch_.event(phase, screen(), xyMode(), screen()->wantsRawTouch(), menu_.isOpen())) {
      case RawTouchRoute::Screen: screen()->onTouch(ev); break;
      case RawTouchRoute::Xy: xyTouch(ev); break;
      case RawTouchRoute::CancelXy: xyPad_.lift(); perf_.xyUp(); break;
      case RawTouchRoute::None: break;
    }
    return;
  }
  if (ev.type == TouchType::HDrag) {
    const int y0 = ev.y0;
    if (!menu_.isOpen() && y0 >= kAreaY && y0 < kTabY && screen()->wantsHDrag()) screen()->onTouch(ev);
    return;
  }
  if (menu_.isOpen()) {
    menu_.onTouch(ev);
    return;
  }
  if (ev.type == TouchType::Drag) {
    const int y0 = ev.y0;
    if (y0 >= kAreaY && y0 < kTabY) screen()->onTouch(ev);  // ignore drags started on the bars
    return;
  }
  if (ev.y < kStatusH) {
    if (toast_[0] && ev.x < kTransX0) return;
    if (ev.type != TouchType::Tap) return;
    if (ev.x >= kModeX0 && ev.x < kModeX1) {
      if (!status_.playing && !status_.paused) return;  // stopped, Play picks the mode by tab
      const bool on = !status_.song;
      if (on && p_->songLen == 0) {
        toast("SONG EMPTY");
        return;
      }
      postSong(on);
      toast(on ? "SONG" : "LOOP");
    } else if (ev.x >= kBpmX0 && ev.x < kBpmX1) setBpmEdit(!bpmEdit_);
    else if (ev.x >= kTransX0 && ev.x < kTransX1) transport();
    return;
  }
  if (ev.y >= kTabY) {
    if (ev.type == TouchType::Tap) {
      int i = ev.x / kTabW;
      if (i < 0) i = 0;
      if (i >= static_cast<int>(Tab::Count)) i = static_cast<int>(Tab::Count) - 1;
      if (i == static_cast<int>(Tab::Grid) && tab_ == Tab::Grid) grid_.nextView();
      else setTab(static_cast<Tab>(i));
    }
    return;
  }
  if (ev.type == TouchType::Tap) setBpmEdit(false);
  screen()->onTouch(ev);
}

void App::pushUndo(uint8_t pat, int genTrack) {
  if (!undoBuf_ || pat >= mt::kPatterns) return;
  // No lock: the engine never writes pattern data (only p_->bpm) nor GenCfg, so reading them is safe.
  const bool g = genTrack >= 0 && genTrack < mt::kTracks;
  undo_.push(pat, p_->patterns[pat], g ? genTrack : -1, g ? p_->gen[genTrack] : mt::GenCfg());
}

void App::projectReplaced() {
  rawTouch_.reset();
  setRecording(false, false);
  automationMute_ = 0;
  engine::post(engine::Cmd::AutomationMute, 0);
  if (mt::Undo* u = undo()) u->clear();
  markSaved();
  setBpmEdit(false);
  lastBpm_ = p_->bpm;
  mt::syncSceneLive(*p_);  // the crossfader's live ends (load / template / demo already match)
  // Let the sequencer re-read the song (stopped: no slot shown).
  engine::post(engine::Cmd::SongEdit, static_cast<uint16_t>(mt::SongOp::Edit));
  perf_.allOff();  // the loaded project does not inherit the held effects
  for (Screen* sc : screens_)
    if (sc) sc->onProjectReplaced();
  for (Screen* sc : screens_)
    if (sc) sc->onPatternChange();
  dirty_ = true;
}

bool App::doUndo() {
  setMotionRecording(false);
  if (!undoBuf_) return false;
  uint8_t pat;
  mt::Pattern& scratch = undoBuf_[mt::Undo::kDepth].data;
  int genTrack = -1;
  mt::GenCfg gen;
  if (!undo_.pop(pat, scratch, &genTrack, &gen)) return false;
  engine::lockProject();
  p_->patterns[pat] = scratch;
  if (genTrack >= 0) p_->gen[genTrack] = gen;  // Capture live: GEN back on
  engine::unlockProject();
  if (genTrack >= 0) engine::post(engine::Cmd::GenEdit);
  markDirty();
  return true;
}

// Button n (0-7) addresses track n of the half holding the cursor (1-8 or 9-16). Grid takes
// selection and note entry, the sample editor plays slices (button index), TRACK INS toggles the
// trance gate steps being edited; Shift + N mutes on every other screen too.
void App::trackKey(int n, bool shift) {
  if (n < 0 || n >= mt::kTrackButtons) return;
  const int track = (curTrack_ / mt::kTrackButtons) * mt::kTrackButtons + n;
  if (tab_ == Tab::Grid && grid_.trackKey(n, shift)) return;
  if (tab_ == Tab::Inst && inst_.trackKey(n, shift)) return;
  if (tab_ == Tab::Song && song_.trackKey(n, shift)) return;
  if (tab_ == Tab::Track && track_.trackKey(n, shift)) return;  // the trance gate's Steps editor
  if (tab_ == Tab::Mix && !xyMode() && track_.sceneView() && track_.trackKey(n, shift)) return;  // SCENE view: the scene to edit
  if (tab_ == Tab::Mix && !shift && perf_.press(n)) return;     // PERF while playing; Shift + N still mutes
  if (!shift) {
    setCurTrack(track);
    return;
  }
  mt::TrackCfg& t = p_->tracks[track];
  engine::lockProject();
  t.mute = !t.mute;
  engine::unlockProject();
  markDirty();
  char msg[16];
  snprintf(msg, sizeof(msg), "TRACK %d %s", track + 1, t.mute ? "MUTE" : "ON");
  toast(msg);
}

// MIX SCENE view shown and taking keys (no menu, BPM edit or XY pad over it).
bool App::sceneKeys() const {
  return tab_ == Tab::Mix && track_.sceneView() && !xyMode() && !menu_.isOpen() && !bpmEdit_;
}

bool App::holdVolume() const {
  return !(tab_ == Tab::Grid && grid_.buttonsBusy()) && !(tab_ == Tab::Inst && inst_.buttonsBusy()) &&
         !(tab_ == Tab::Song && perf_.on()) && !(tab_ == Tab::Mix && perf_.on()) &&
         !((tab_ == Tab::Track || tab_ == Tab::Mix) && track_.buttonsBusy()) && !busy_;
}

// Volume of a held track button's track (INT only), with a toast; the mixer redraws by itself.
void App::nudgeTrackVol(int track, int d) {
  mt::TrackCfg& t = p_->tracks[track];
  char msg[24];
  if (t.out != mt::TrackOut::Int) {
    snprintf(msg, sizeof(msg), "TRK%d MIDI", track + 1);
    toast(msg);
    return;
  }
  int v = t.vol + d;
  v = v < 0 ? 0 : (v > 127 ? 127 : v);
  if (v != t.vol) {
    engine::lockProject();
    t.vol = static_cast<uint8_t>(v);
    engine::unlockProject();
    markDirty();
  }
  snprintf(msg, sizeof(msg), "TRK%d VOL %d", track + 1, v);
  toast(msg);
}

// Pan of a held track button's track (INT only), with a toast; as nudgeTrackVol.
void App::nudgeTrackPan(int track, int d) {
  mt::TrackCfg& t = p_->tracks[track];
  if (t.out == mt::TrackOut::Int) {
    int v = t.pan + d;
    v = v < 0 ? 0 : (v > 127 ? 127 : v);
    if (v != t.pan) {
      motionObserve(true, track);
      engine::lockProject();
      t.pan = static_cast<uint8_t>(v);
      engine::unlockProject();
      motionObserve(false);
      markDirty();
    }
  }
  holdToast(track);
}

// Flip of a held track button's track's generator (MIDI or INT), with a toast; replans the unheard steps.
void App::nudgeTrackFlip(int track, int d) {
  mt::GenCfg& g = p_->gen[track];
  int v = g.flip + d;
  v = v < 0 ? 0 : (v > 100 ? 100 : v);
  if (v != g.flip) {
    engine::lockProject();
    g.flip = static_cast<uint8_t>(v);
    engine::unlockProject();
    markDirty();
    engine::post(engine::Cmd::GenEdit);
  }
  holdToast(track);
}

// What the held button's turn changes now, with its value.
void App::holdToast(int track) {
  const mt::TrackCfg& t = p_->tracks[track];
  char msg[24];
  if (holdMode_ == mt::HoldMode::Flip) {
    char v[8];
    mt::genFlipText(p_->gen[track].flip, v);
    snprintf(msg, sizeof(msg), "TRK%d FLIP %s", track + 1, v);
  } else if (t.out != mt::TrackOut::Int) {
    snprintf(msg, sizeof(msg), "TRK%d MIDI", track + 1);
  } else if (holdMode_ == mt::HoldMode::Pan) {
    char v[5];
    mt::fxFormat(mt::Fx::PAN, t.pan, v);
    snprintf(msg, sizeof(msg), "TRK%d PAN %s", track + 1, v);
  } else {
    snprintf(msg, sizeof(msg), "TRK%d VOL %d", track + 1, t.vol);
  }
  toast(msg);
}

void App::trackRelease(int n) {
  if (n < 0 || n >= mt::kTrackButtons) return;
  if (perfTab(tab_)) perf_.release(n);  // PERF: the effect the button held ends
  if (tab_ == Tab::Mix) track_.trackUp(n);  // SCENE view: the scene to edit
}

// Shift + Play while playing: fill while held; a short press is still Shift + Play (song start) on
// release.
void App::fillDown() {
  fillHeld_ = true;
  fillDownMs_ = millis();
  engine::post(engine::Cmd::Fill, 1);
}

void App::fillUp() {
  if (!fillHeld_) return;
  fillHeld_ = false;
  engine::postWait(engine::Cmd::Fill, 0);
  if (millis() - fillDownMs_ < hw::kLongPressMs) startSong();
}

void App::setTheme(int i) {
  i = i < 0 ? 0 : (i >= themeCount() ? themeCount() - 1 : i);
  if (i == theme_) return;
  theme_ = i;
  applyTheme(i);
  settingsChangedAt_ = millis() | 1;
  dirty_ = true;
}

void App::setAutosaveMin(int m) {
  autosaveMin_ = m < 0 ? 0 : (m > 60 ? 60 : m);
  autosaveDue_ = 0;  // the new interval counts from now
  settingsChangedAt_ = millis() | 1;
}

void App::setPhones(int v) {
  v = v < 0 ? 0 : (v > 100 ? 100 : v);
  if (v == phones_) return;
  phones_ = v;
  audio::setPhones(static_cast<uint8_t>(v));
  settingsChangedAt_ = millis() | 1;
}

void App::setPreview(bool on) {
  if (on == preview_) return;
  preview_ = on;
  settingsChangedAt_ = millis() | 1;
}

void App::setPerfVisuals(bool on) {
  if (on == visuals_) return;
  visuals_ = on;
  settingsChangedAt_ = millis() | 1;
}

// Device settings (theme, autosave, phones, preview, perf visuals): written once they stay put for a second (an encoder
// sweep = one write) and the transport is stopped. An NVS write can erase a flash sector with the
// caches off: never while playing, and with the audio task parked.
void App::saveSettingsIdle(uint32_t now) {
  if (settingsChangedAt_ == 0 || now - settingsChangedAt_ < 1000 || status_.playing || status_.countIn) return;
  settingsChangedAt_ = 0;
  proj_.saveDicePrefs();
  const uint8_t practiceBits = mt::practiceBits(practicePrefs_);
  if (practiceBits != savedPracticeBits_) storage::saveSetting("practice", practiceBits);
  if (practicePrefs_.clickLevel != savedClickLevel_) storage::saveSetting("clickvol", practicePrefs_.clickLevel);
  savedPracticeBits_ = practiceBits;
  savedClickLevel_ = practicePrefs_.clickLevel;
  if (theme_ == savedTheme_ && autosaveMin_ == savedAutosaveMin_ && phones_ == savedPhones_ &&
      preview_ == savedPreview_ && visuals_ == savedVisuals_)
    return;
  {
    audio::Paused parked;
    if (theme_ != savedTheme_) storage::saveTheme(static_cast<uint8_t>(theme_));
    if (autosaveMin_ != savedAutosaveMin_) storage::saveSetting("autosave", static_cast<uint8_t>(autosaveMin_));
    if (phones_ != savedPhones_) storage::saveSetting("phones", static_cast<uint8_t>(phones_));
    if (preview_ != savedPreview_) storage::saveSetting("preview", preview_ ? 1 : 0);
    if (visuals_ != savedVisuals_) storage::saveSetting("perfvis", visuals_ ? 1 : 0);
  }
  savedTheme_ = theme_;
  savedAutosaveMin_ = autosaveMin_;
  savedPhones_ = phones_;
  savedPreview_ = preview_;
  savedVisuals_ = visuals_;
}

// Unsaved changes go to /projects/<name>.auto every autosaveMin_ minutes: only while stopped (no project
// copy needed; paused, a song slot's BPM and mutes would still hold p.bpm and the mutes) and after 3 s
// without input (the write takes a moment).
void App::autosaveIdle(uint32_t now) {
  if (status_.countIn || autosaveMin_ == 0 || !projectDirty() || editSeq_ == autosavedSeq_ || !hw::sdReady()) {
    autosaveDue_ = 0;
    return;
  }
  if (autosaveDue_ == 0) {
    autosaveDue_ = (now + static_cast<uint32_t>(autosaveMin_) * 60000u) | 1;
    return;
  }
  if (static_cast<int32_t>(now - autosaveDue_) < 0) return;
  if (status_.playing || status_.paused || busy_ || menu_.isOpen() || now - lastInputMs_ < 3000) return;
  showBusy("AUTOSAVE...");
  const storage::Result r = storage::autosave(*p_);
  autosavedSeq_ = editSeq_;
  autosaveDue_ = 0;
  if (r != storage::Result::Ok) {
    char msg[40];
    snprintf(msg, sizeof(msg), "AUTOSAVE: %s", storage::resultText(r));
    toast(msg);
  }
  dirty_ = true;
}

void App::showProgress(const char* label, uint32_t done, uint32_t total) {
  const int pct = total ? static_cast<int>(static_cast<uint64_t>(done < total ? done : total) * 100 / total) : 100;
  const uint32_t now = millis();
  const bool fresh = strncmp(label, progLabel_, sizeof(progLabel_) - 1) != 0;
  if (!fresh && (pct == progPct_ || (now - progMs_ < 200 && pct < 100))) return;
  strlcpy(progLabel_, label, sizeof(progLabel_));
  progPct_ = pct;
  progMs_ = now;
  snprintf(progMsg_, sizeof(progMsg_), "%s %d%%", progLabel_, pct);
  showBusy(progMsg_);
}

void App::syncProgress(const char* file, uint32_t done, uint32_t total, void* app) {
  char label[32];
  snprintf(label, sizeof(label), "SAMPLE %s", file);
  static_cast<App*>(app)->showProgress(label, done, total);
}

bool App::renderProgress(uint32_t done, uint32_t total, void* app) {
  return labeledProgress("RENDER", done, total, app);
}

bool App::labeledProgress(const char* label, uint32_t done, uint32_t total, void* app) {
  App& a = *static_cast<App*>(app);
  a.showProgress(label, done, total);
  // Renders run for up to a minute on this task: let the idle task (watchdog) run now and then.
  static uint32_t lastYield = 0;
  if (millis() - lastYield >= 100) {
    lastYield = millis();
    vTaskDelay(1);
  }
  // The UI task is busy here: read the input queue for a cancel, drop the rest.
  hw::InputEvent ev;
  while (hw::inputPoll(ev, 0)) {
    a.dropInput(ev);
    if (ev.type == hw::InputType::EncLong || ev.type == hw::InputType::PlayPress) return false;
  }
  return true;
}

// An event read while the UI is busy: only the held-key state survives, and releases still end
// what their press started (fill, a punch-in effect, the hold-button volume).
void App::dropInput(const hw::InputEvent& ev) {
  switch (ev.type) {
    case hw::InputType::ShiftDown: shift_ = true; break;
    case hw::InputType::ShiftUp: shift_ = false; break;
    case hw::InputType::PlayRelease:
      if (fillHeld_) {
        fillHeld_ = false;
        engine::postWait(engine::Cmd::Fill, 0);
      }
      break;
    case hw::InputType::TrackRelease:
      if (ev.delta == heldTrackBtn_) {
        heldTrackBtn_ = -1;
        holdMode_ = mt::HoldMode::Vol;
      }
      trackRelease(ev.delta);
      break;
    case hw::InputType::AUp:
    case hw::InputType::BUp: ab_.reset(); break;  // no tap / edit end from a dropped chord
    default: break;
  }
}

void App::endProgress() {
  heldTrackBtn_ = -1;  // its release may have been dropped with the queued input
  holdMode_ = mt::HoldMode::Vol;
  ab_.reset();         // so may the A / B releases
  screen()->onInput(hw::InputEvent{hw::InputType::EditEnd, 0, false});  // no hold edit left stuck
  progLabel_[0] = 0;
  progPct_ = -1;
  hw::InputEvent ev;
  while (hw::inputPoll(ev, 0)) dropInput(ev);
  dirty_ = true;
}

void App::loadedToast(const char* what, int missing, bool folderFail) {
  char parts[5][40];
  int n = 0;
  if (what && what[0]) strlcpy(parts[n++], what, sizeof(parts[0]));
  if (missing > 0) snprintf(parts[n++], sizeof(parts[0]), "%d SAMPLES MISSING", missing);
  if (folderFail) strlcpy(parts[n++], "SAMPLES NOT SAVED", sizeof(parts[0]));
  if (storage::songTransposeDropped()) strlcpy(parts[n++], "SONG: TRANSPOSE DROPPED", sizeof(parts[0]));
  if (storage::tuningDropped()) strlcpy(parts[n++], "TUNING RESET", sizeof(parts[0]));
  for (int first = 0; first < n; ++first) {
    char msg[128] = "";
    for (int k = first; k < n; ++k) {
      if (msg[0]) strlcat(msg, ", ", sizeof(msg));
      strlcat(msg, parts[k], sizeof(msg));
    }
    if (strlen(msg) < sizeof(toast_) || first == n - 1) {
      toast(msg);
      return;
    }
  }
}

void App::toast(const char* msg) {
  strlcpy(toast_, msg, sizeof(toast_));
  toastUntil_ = millis() + kToastMs;
  dirty_ = true;
}

void App::showBusy(const char* msg) {
  busy_ = msg;
  draw();
  busy_ = nullptr;
  dirty_ = true;  // next frame without the overlay
  lastDraw_ = millis();
}

void App::setMotionRecording(bool on) {
  motionRec_ = on;
  syncRecordArm();
  motionObserving_ = false;
  motionPattern_ = -1;
  motionLoop_ = UINT32_MAX;
  for (auto& m : motion_) m.clear();
  invalidate();
}

void App::motionObserve(bool before, int track) {
  if (before) {
    motionObserving_ = motionRec_ && recordPermit() &&
        (track >= 0 || tab_ != Tab::Inst || inst_.onTrackInstr());
    if (!motionObserving_) return;
    motionTrack_ = track >= 0 && track < mt::kTracks ? track : curTrack_;
    const auto& t = p_->tracks[motionTrack_];
    if (t.out != mt::TrackOut::Int) { motionObserving_ = false; return; }
    motionInstr_ = t.instr % mt::kInstruments;
    const auto& i = p_->instruments[motionInstr_];
    motionType_ = i.type;
    for (int n = 0; n < mt::kAutomationParams; ++n)
      motionBefore_[n] = mt::automationValue(t,i,mt::automationFx(n));
    return;
  }
  if (!motionObserving_) return;
  motionObserving_ = false;
  const auto& t = p_->tracks[motionTrack_];
  const auto& i = p_->instruments[t.instr % mt::kInstruments];
  if (t.instr % mt::kInstruments != motionInstr_ || i.type != motionType_) return;
  int16_t after[mt::kAutomationParams];
  for (int n = 0; n < mt::kAutomationParams; ++n)
    after[n] = mt::automationValue(t,i,mt::automationFx(n));
  // Establish the pass before touching: a new pass must not discard the first movement.
  pollMotion();
  motion_[motionTrack_].touch(motionBefore_,after);
  pollMotion();
}

void App::pollMotion() {
  if (!motionRec_) return;
  const auto st = engine::status();
  if (!st.playing || !recordPermit()) {
    for (auto& m : motion_) m.clear();
    motionPattern_ = -1;
    motionLoop_ = UINT32_MAX;
    return;
  }
  if (motionPattern_ != st.pattern) {
    for (auto& m : motion_) m.clear();
    motionPattern_ = st.pattern;
    motionLoop_ = UINT32_MAX;
  }
  for (auto& m : motion_) m.beginPass(st.loop);
  auto& p = p_->patterns[st.pattern];
  const uint8_t phase = engine::stepPhase(st);
  bool changed = false, full = false;
  engine::lockProject();
  for (int tr = 0; tr < mt::kTracks; ++tr) {
    if (p_->tracks[tr].out != mt::TrackOut::Int) continue;
    auto& step = p.steps[tr][mt::automationStep(p,tr,st.pos,st.loop,phase)];
    mt::Step out = step;
    if (!motion_[tr].write(out)) { full = true; continue; }
    if (memcmp(&out,&step,sizeof(step)) == 0) continue;
    if (motionLoop_ != st.loop) { pushUndo(st.pattern); motionLoop_ = st.loop; }
    step = out;
    changed = true;
  }
  engine::unlockProject();
  if (changed) markDirty();
  if (full) { setMotionRecording(false); toast("MOTION STOP: NO FX SLOT"); }
}

void App::tick() {
  TouchEvent te;
  if (touch_.poll(*lcd_, te, !menu_.isOpen() && screen()->wantsHDrag(),
                  !menu_.isOpen() && (xyMode() || screen()->wantsRawTouch()))) onTouch(te);

  const engine::Status s = engine::status();
  if (!(s == status_)) {
    const bool patChanged = s.pattern != status_.pattern;
    status_ = s;
    if (patChanged)
      for (Screen* sc : screens_)
        if (sc) sc->onPatternChange();
    dirty_ = true;
  }
  if (status_.recordDone != recordDoneSeen_) {
    recordDoneSeen_ = status_.recordDone;
    setRecording(false, false);
    toast("ONE PASS: REC FINISHED");
  }
  if (p_->bpm != lastBpm_) {
    lastBpm_ = p_->bpm;
    dirty_ = true;
  }
  const uint32_t now = millis();
  saveSettingsIdle(now);
  autosaveIdle(now);
  pollCpu(now);
  pollBattery(now);
  if (toast_[0] && static_cast<int32_t>(now - toastUntil_) >= 0) {
    toast_[0] = 0;
    dirty_ = true;
  }
  pollMotion();
  perf_.poll(status_.playing);
  screen()->poll();
  if (screen()->wantsRedraw(status_)) dirty_ = true;
  if (!spr_ && !frameBuffer_.loaned && now-frameRetryAt_ >= 1000) {
    frameRetryAt_ = now;
    dirty_ = true;  // a failed framebuffer recreation is retried without requiring user input
  }
  if (perfVisible() || vis_.engaged()) dirty_ = true;  // moving while held; the last frame goes back to plain
  if (xyMode() && xyPad_.animating(now)) dirty_ = true;  // the trail fades

  if (dirty_ && now - lastDraw_ >= kFrameMs) {
    draw();
    dirty_ = false;
    lastDraw_ = now;
  }
}

// Average audio load over the last window: total render time / total block duration. Red when
// the audio stalled (an audible gap; held kCpuRedMs to be seen) or the average is >= 85 %, yellow
// when a block overran its period (covered by the DMA queue) or the average is >= 60 %. A window
// without blocks (audio parked) keeps the last value.
void App::pollCpu(uint32_t now) {
  if (now - cpuAt_ < kCpuMs) return;
  cpuAt_ = now;
  const audio::Load l = audio::takeLoad();
  if (!l.blocks) return;
  constexpr uint32_t kBlockUs = audio::kBlock * 1000000u / audio::kRate;
  const int pct = static_cast<int>(static_cast<uint64_t>(l.sumUs) * 100 / (static_cast<uint64_t>(l.blocks) * kBlockUs));
  if (l.stalls) cpuRedUntil_ = now + kCpuRedMs;
  const bool red = static_cast<int32_t>(cpuRedUntil_ - now) > 0 || pct >= 85;
  const uint16_t color = red ? kRed : ((l.peakUs > kBlockUs || pct >= 60) ? kYellow : kDim);
  if (pct != cpu_ || color != cpuColor_) {
    cpu_ = pct;
    cpuColor_ = color;
    dirty_ = true;
  }
}

// Feeds each new battery sample (taken once a second by trackio) to the gauge; polled every
// kBatMs so it does not drift against the reader (s.ms keeps a sample from being fed twice).
// A sample older than kBatStaleMs (reader stopped) counts as none: the status bar hides the
// battery, the SYS row shows "---". The status bar redraws when the percent, charging or
// freshness changes (its colour follows from them); batSeq_ tells FILE SYSTEM of every change.
void App::pollBattery(uint32_t now) {
  if (now - batAt_ < kBatMs) return;
  batAt_ = now;
  hw::BatterySample s;
  if (hw::batteryLast(s) && !(batHave_ && s.ms == batSample_.ms)) {
    const float dt = batHave_ ? (s.ms - batSample_.ms) / 1000.f : 0.f;
    gauge_.update(s.volts, s.amps, dt);
    batSample_ = s;
    batHave_ = true;
    ++batSeq_;
    if (gauge_.takeLowAlert()) toast("BATTERY LOW - SAVE");
  }
  const bool fresh = batHave_ && now - batSample_.ms <= kBatStaleMs;
  if (fresh != batFresh_) ++batSeq_;
  const int pct = fresh ? gauge_.percent() : -1;
  if (fresh != batFresh_ || pct != batPct_ || gauge_.charging() != batChg_) {
    batFresh_ = fresh;
    batPct_ = pct;
    batChg_ = gauge_.charging();
    dirty_ = true;
  }
}

void App::draw() {
  if (frameBuffer_.loaned) return;  // synchronous progress must not reclaim the staging loan
  if (!spr_ && !restoreFrameBuffer(frameBuffer_,spr_,App::createFrameBuffer,this)) {
    if (lcd_) {
      lcd_->fillScreen(kBg);
      lcd_->setFont(font());
      lcd_->setTextColor(kRed);
      lcd_->drawString("NO DISPLAY MEMORY: RETRYING",16,kAreaY+16);
    }
    return;
  }
  spr_->fillScreen(kBg);
  drawStatus();
  spr_->setClipRect(0, kAreaY, kScreenW, kAreaH);
  const bool xy = xyMode();
  if (xy) xyPad_.draw(*spr_, kAreaY, kAreaH, perf_, millis(), !perfVisible());
  else screen()->draw(*spr_, kAreaY, kAreaH);
  spr_->clearClipRect();
  drawTabs();
  menu_.draw(*spr_);
  if (busy_) drawBusy();
  if (perfVisible() && !busy_) {
    // The play position in sixteenths: the heard step plus how far into it.
    const int64_t into = static_cast<int64_t>(engine::nowUs() - status_.stepT);
    float steps = status_.pos;
    if (status_.stepUs && into > 0) steps += static_cast<float>(into) / status_.stepUs;
    mt::PerfTrail trail;
    if (xy) xyPad_.trail(trail, kAreaY, millis());
    vis_.push(*lcd_, *spr_, perf_.stack(), steps, status_.stepUs, xy ? &trail : nullptr, kYellow);
    return;
  }
  vis_.reset(*lcd_);
  spr_->pushSprite(0, 0);
}

void App::drawBusy() {
  const int w = static_cast<int>(strlen(busy_)) * kCharW + 48;
  const int h = kCharH + 32;
  const int x = (kScreenW - w) / 2;
  const int y = kAreaY + (kAreaH - h) / 2;
  spr_->fillRect(x, y, w, h, kStatusBg);
  spr_->drawRect(x, y, w, h, kCursor);
  spr_->setTextColor(kCursor);
  spr_->drawString(busy_, x + 24, y + 16);
}

void App::drawStatus() {
  char buf[24];
  spr_->fillRect(0, 0, kScreenW, kStatusH, kStatusBg);
  spr_->setTextColor(kText);
  // Song: "S03 P05" (heard slot, "S--" before the song is heard); no queue in song mode.
  // Stopped: Play picks the mode by tab, so no "S" here. No lock: the UI is the only writer of songLen.
  const bool song = songPlays();
  if (song) {
    if (status_.songPos >= 0) snprintf(buf, sizeof(buf), "S%02d P%02d%s", status_.songPos + 1, status_.pattern + 1,
                                       projectDirty() ? "*" : "");
    else snprintf(buf, sizeof(buf), "S-- P%02d%s", status_.pattern + 1, projectDirty() ? "*" : "");
  } else {
    snprintf(buf, sizeof(buf), "P%02d%s", status_.pattern + 1, projectDirty() ? "*" : "");
  }
  spr_->drawString(buf, 8, 4);
  if (!song && status_.queued >= 0) {
    snprintf(buf, sizeof(buf), ">P%02d", status_.queued + 1);
    spr_->drawString(buf, 40, 4);
  }
  spr_->setTextColor(bpmEdit_ ? kEditCursor : kText);
  snprintf(buf, sizeof(buf), "%3u BPM", bpmEdit_ ? static_cast<unsigned>(bpmTarget_) : p_->bpm);
  spr_->drawString(buf, 80, 4);

  spr_->setTextColor(kText);
  snprintf(buf, sizeof(buf), "%u/%u", status_.pos + 1, p_->patterns[status_.pattern].length);
  spr_->drawString(buf, 200, 4);  // up to "128/128"
  if (toast_[0]) {
    spr_->fillRect(0, 0, kTransX0 - 4, kStatusH, kStatusBg);
    spr_->setTextColor(kCursor);
    spr_->setClipRect(8, 0, kTransX0 - 12, kStatusH);
    spr_->drawString(toast_, 8, 4);
    spr_->clearClipRect();
    spr_->setTextColor(kText);
  }

  if (status_.playing) {
    // Playing: PLAY on a green box, readable at a glance.
    spr_->fillRect(260, 3, 40, 18, kGreen);
    spr_->setTextColor(kStatusBg);
    spr_->drawString("PLAY", 264, 4);
    spr_->setTextColor(kText);
  } else {
    spr_->drawString(status_.paused ? "PAUSE" : "STOP", 264, 4);
  }
  if (status_.fill) {
    spr_->setTextColor(kCursor);
    spr_->drawString("FILL", 312, 4);
    spr_->setTextColor(kText);
  } else {
    if (status_.loop <= 9999) snprintf(buf, sizeof(buf), "L%lu", static_cast<unsigned long>(status_.loop));
    else snprintf(buf, sizeof(buf), "L10K+");
    spr_->drawString(buf, 312, 4);  // up to 5 chars before the battery
  }
  drawBattery();
  if (status_.countIn) {
    snprintf(buf, sizeof(buf), "IN %u", status_.countIn);
    spr_->setTextColor(kRed);
    spr_->drawString(buf, 416, 4);
    return;
  }
  if (motionRec_ || grid_.recording()) {
    spr_->setTextColor(kRed);
    spr_->drawString(motionRec_ ? "MREC" : "REC", 416, 4);
    return;
  }
  snprintf(buf, sizeof(buf), "CPU %3d%%", cpu_ > 999 ? 999 : cpu_);
  spr_->setTextColor(cpuColor_);
  spr_->drawString(buf, 416, 4);
}

// Status bar battery: icon (body 14x10, nub, fill by percent, a bolt while charging) and "85%".
// Green while charging, red <= 10 %, yellow <= 20 %. Nothing without the sensor or a fresh sample.
void App::drawBattery() {
  const int pct = gauge_.percent();
  if (!hw::batteryPresent() || !batFresh_ || pct < 0) return;
  const bool chg = gauge_.charging();
  const uint16_t c = chg ? kGreen : (pct <= 10 ? kRed : (pct <= 20 ? kYellow : kDim));
  constexpr int x = 360, y = 7;
  spr_->drawRect(x, y, 14, 10, c);
  spr_->fillRect(x + 14, y + 3, 2, 4, c);
  int w = pct * 12 / 100;
  if (pct > 0 && w < 1) w = 1;
  if (w > 0) spr_->fillRect(x + 1, y + 1, w, 8, c);
  if (chg) {  // bolt: cut out of the fill, drawn in c over the empty part
    for (int part = 0; part < 2; ++part) {
      const int x0 = part ? x + 1 + w : x + 1, pw = part ? 12 - w : w;
      if (pw <= 0) continue;
      const uint16_t bc = part ? c : kStatusBg;
      spr_->setClipRect(x0, y + 1, pw, 8);
      spr_->fillTriangle(x + 8, y + 1, x + 4, y + 5, x + 7, y + 5, bc);
      spr_->fillTriangle(x + 6, y + 4, x + 9, y + 4, x + 5, y + 8, bc);
    }
    spr_->clearClipRect();
  }
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", pct);
  spr_->setTextColor(c);
  spr_->drawString(buf, 380, 4);  // up to "100%", ends at 412
}

void App::drawTabs() {
  static const char* const kNames[] = {"GRID", "TRACK", "INST", "MIX", "BANK", "SONG", "PROJ", "FILE"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<int>(Tab::Count), "tab names");
  spr_->fillRect(0, kTabY, kScreenW, kTabH, kStatusBg);
  for (int i = 0; i < static_cast<int>(Tab::Count); ++i) {
    const int x = i * kTabW;
    const bool active = i == static_cast<int>(tab_);
    if (active) spr_->fillRect(x, kTabY, kTabW, kTabH, kPlayBg);
    if (i > 0) spr_->drawFastVLine(x, kTabY + 4, kTabH - 8, kDim);
    const char* label = kNames[i];
    uint16_t color = !screens_[i] ? kDim : (active ? kCursor : kText);
    if (shift_ && i == static_cast<int>(Tab::Count) - 1) {  // SHIFT indicator over the last tab
      label = "SHIFT";
      color = kCursor;
    }
    const int w = static_cast<int>(strlen(label)) * kCharW;
    spr_->setTextColor(color);
    spr_->drawString(label, x + (kTabW - w) / 2, kTabY + 4);
  }
}

}  // namespace ui
