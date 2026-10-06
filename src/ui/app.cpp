#include "app.h"
#include <new>
#include <stdio.h>
#include <string.h>
#include "audio/audio.h"
#include "esp_heap_caps.h"
#include "hw/trackio.h"
#include "storage/settings.h"
#include "track_leds.h"

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

void App::begin(LGFX* lcd, mt::Project* p) {
  lcd_ = lcd;
  p_ = p;

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

  // Master volume is a device setting: it overrides the project's and is kept in NVS.
  p_->masterVol = storage::loadVolume(p_->masterVol);
  savedVol_ = p_->masterVol;

  status_ = engine::status();
  lastBpm_ = p_->bpm;
  screen()->onEnter();
  dirty_ = true;
}

void App::setTab(Tab t) {
  if (t == Tab::Count || !screens_[static_cast<int>(t)]) return;
  setBpmEdit(false);
  if (t == tab_) return;
  menu_.close();
  screen()->onLeave();
  tab_ = t;
  screen()->onEnter();
  dirty_ = true;
}

void App::transport() {
  if (transportLocked_) {
    toast("WI-FI MODE");
    return;
  }
  engine::post(shift_ ? engine::Cmd::TogglePlay : engine::Cmd::StartStop);
}

void App::setBpmEdit(bool on) {
  if (on && !bpmEdit_) bpmTarget_ = p_->bpm;
  bpmEdit_ = on;
}

void App::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  shift_ = ev.shift;
  dirty_ = true;
  switch (ev.type) {
    case InputType::PlayPress:
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
      trackRelease(ev.delta);
      return;
    case InputType::ShiftDown: shift_ = true; return;
    case InputType::ShiftUp: shift_ = false; return;
    case InputType::TrackPress:
      if (!menu_.isOpen()) trackKey(ev.delta, ev.shift);
      return;
    default: break;
  }
  if (menu_.isOpen()) {
    menu_.onInput(ev);
    return;
  }
  if (bpmEdit_) {
    if (ev.type == InputType::EncTurn) {
      const int bpm = bpmTarget_ + ev.delta * (ev.shift ? 10 : 1);
      bpmTarget_ = bpm < 20 ? 20 : (bpm > 300 ? 300 : bpm);
      engine::post(engine::Cmd::SetBpm, static_cast<uint16_t>(bpmTarget_));
      markDirty();
    } else {
      setBpmEdit(false);
    }
    return;
  }
  screen()->onInput(ev);
}

void App::onTouch(const TouchEvent& ev) {
  dirty_ = true;
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
    if (ev.type != TouchType::Tap) return;
    if (ev.x >= kBpmX0 && ev.x < kBpmX1) setBpmEdit(!bpmEdit_);
    else if (ev.x >= kTransX0 && ev.x < kTransX1) transport();
    return;
  }
  if (ev.y >= kTabY) {
    if (ev.type == TouchType::Tap) {
      int i = ev.x / kTabW;
      if (i < 0) i = 0;
      if (i >= static_cast<int>(Tab::Count)) i = static_cast<int>(Tab::Count) - 1;
      setTab(static_cast<Tab>(i));
    }
    return;
  }
  if (ev.type == TouchType::Tap) setBpmEdit(false);
  screen()->onTouch(ev);
}

void App::pushUndo() {
  if (!undoBuf_) return;
  // No lock: the engine never writes pattern data (only p_->bpm), so reading it is safe.
  const uint8_t pat = editPattern();
  undo_.push(pat, p_->patterns[pat]);
}

void App::projectReplaced() {
  if (mt::Undo* u = undo()) u->clear();
  markSaved();
  setBpmEdit(false);
  lastBpm_ = p_->bpm;
  p_->masterVol = savedVol_;  // the device volume, not the file's
  volChangedAt_ = 0;
  // Let the sequencer re-read songMode/chain (shows S01 while stopped).
  engine::post(engine::Cmd::ChainEdit, static_cast<uint16_t>(mt::ChainOp::Edit));
  for (Screen* sc : screens_)
    if (sc) sc->onProjectReplaced();
  for (Screen* sc : screens_)
    if (sc) sc->onPatternChange();
  dirty_ = true;
}

bool App::doUndo() {
  if (!undoBuf_) return false;
  uint8_t pat;
  mt::Pattern& scratch = undoBuf_[mt::Undo::kDepth].data;
  if (!undo_.pop(pat, scratch)) return false;
  engine::lockProject();
  p_->patterns[pat] = scratch;
  engine::unlockProject();
  markDirty();
  return true;
}

// Button n (0-7) addresses track n of the half holding the cursor (1-8 or 9-16). Grid takes
// selection and note entry, the sample editor plays slices (button index); Shift + N mutes on
// every other screen too.
void App::trackKey(int n, bool shift) {
  if (n < 0 || n >= mt::kTrackLeds) return;
  const int track = (curTrack_ / mt::kTrackLeds) * mt::kTrackLeds + n;
  if (tab_ == Tab::Grid && grid_.trackKey(n, shift)) return;
  if (tab_ == Tab::Inst && inst_.trackKey(n, shift)) return;
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

void App::trackRelease(int n) {
  if (n < 0 || n >= mt::kTrackLeds) return;
  if (tab_ == Tab::Grid) grid_.trackRelease(n);
}

// Shift + Play while playing: fill while held; a short press is still Shift + Play (pause) on release.
void App::fillDown() {
  fillHeld_ = true;
  fillDownMs_ = millis();
  engine::post(engine::Cmd::Fill, 1);
}

void App::fillUp() {
  if (!fillHeld_) return;
  fillHeld_ = false;
  engine::post(engine::Cmd::Fill, 0);
  if (millis() - fillDownMs_ < hw::kLongPressMs) engine::post(engine::Cmd::TogglePlay);
}

void App::updateLeds(uint32_t now) {
  const uint16_t act = engine::takeActivity();
  uint16_t flash = 0;
  for (int i = 0; i < mt::kTracks; ++i) {
    if (act & (1u << i)) flashUntil_[i] = now + kFlashMs;
    if (static_cast<int32_t>(flashUntil_[i] - now) > 0) flash |= static_cast<uint16_t>(1u << i);
  }
  hw::trackLeds(mt::trackLedMaskHalf(curTrack_, flash));
}

// Writes the volume to NVS once it has stayed put for a second (an encoder sweep = one write).
void App::saveVolumeIdle(uint32_t now) {
  if (p_->masterVol == savedVol_) {
    volChangedAt_ = 0;
    return;
  }
  if (volChangedAt_ == 0 || p_->masterVol != pendingVol_) {
    pendingVol_ = p_->masterVol;
    volChangedAt_ = now | 1;
    return;
  }
  if (now - volChangedAt_ < 1000) return;
  storage::saveVolume(pendingVol_);
  savedVol_ = pendingVol_;
  volChangedAt_ = 0;
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

void App::loadedToast(const char* what, int missing, bool folderFail) {
  char parts[3][40];
  int n = 0;
  if (what && what[0]) strlcpy(parts[n++], what, sizeof(parts[0]));
  if (missing > 0) snprintf(parts[n++], sizeof(parts[0]), "%d SAMPLES MISSING", missing);
  if (folderFail) strlcpy(parts[n++], "SAMPLES NOT SAVED", sizeof(parts[0]));
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

void App::tick() {
  TouchEvent te;
  if (touch_.poll(*lcd_, te, !menu_.isOpen() && screen()->wantsHDrag())) onTouch(te);

  const engine::Status s = engine::status();
  if (!(s == status_)) {
    const bool patChanged = s.pattern != status_.pattern;
    status_ = s;
    if (patChanged)
      for (Screen* sc : screens_)
        if (sc) sc->onPatternChange();
    dirty_ = true;
  }
  if (p_->bpm != lastBpm_) {
    lastBpm_ = p_->bpm;
    dirty_ = true;
  }
  const uint32_t now = millis();
  updateLeds(now);
  saveVolumeIdle(now);
  pollCpu(now);
  if (toast_[0] && static_cast<int32_t>(now - toastUntil_) >= 0) {
    toast_[0] = 0;
    dirty_ = true;
  }
  screen()->poll();
  if (screen()->wantsRedraw(status_)) dirty_ = true;

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

void App::draw() {
  if (!spr_) return;
  spr_->fillScreen(kBg);
  drawStatus();
  spr_->setClipRect(0, kAreaY, kScreenW, kAreaH);
  screen()->draw(*spr_, kAreaY, kAreaH);
  spr_->clearClipRect();
  drawTabs();
  menu_.draw(*spr_);
  if (busy_) drawBusy();
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
  // Song mode: "S03 P05" (heard chain entry, "S--" before the song is heard); no queue in song mode.
  // No lock: the UI is the only writer of songMode / chainLen.
  const bool song = p_->songMode && p_->chainLen > 0;
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

  if (toast_[0]) {
    spr_->setTextColor(kCursor);
    spr_->drawString(toast_, 192, 4);
    return;
  }
  spr_->setTextColor(kText);
  snprintf(buf, sizeof(buf), "%u/%u", status_.pos + 1, p_->patterns[status_.pattern].length);
  spr_->drawString(buf, 200, 4);  // up to "128/128"
  spr_->drawString(status_.playing ? "PLAY" : (status_.paused ? "PAUSE" : "STOP"), 264, 4);
  if (status_.fill) {
    spr_->setTextColor(kCursor);
    spr_->drawString("FILL", 328, 4);
    spr_->setTextColor(kText);
  } else {
    snprintf(buf, sizeof(buf), "L%lu", static_cast<unsigned long>(status_.loop));
    spr_->drawString(buf, 328, 4);  // up to 11 chars (uint32) before the CPU field
  }
  snprintf(buf, sizeof(buf), "CPU %3d%%", cpu_ > 999 ? 999 : cpu_);
  spr_->setTextColor(cpuColor_);
  spr_->drawString(buf, 416, 4);
}

void App::drawTabs() {
  static const char* const kNames[] = {"GRID", "TRACK", "BANK", "INST", "PROJ", "FILE"};
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
