#include "app.h"
#include <new>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"

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

void App::transport() { engine::post(shift_ ? engine::Cmd::TogglePlay : engine::Cmd::StartStop); }

void App::setBpmEdit(bool on) {
  if (on && !bpmEdit_) bpmTarget_ = p_->bpm;
  bpmEdit_ = on;
}

void App::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  shift_ = ev.shift;
  dirty_ = true;
  switch (ev.type) {
    case InputType::PlayPress: transport(); return;
    case InputType::ShiftDown: shift_ = true; return;
    case InputType::ShiftUp: shift_ = false; return;
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
  if (menu_.isOpen()) {
    menu_.onTouch(ev);
    return;
  }
  if (ev.type == TouchType::Drag) {
    const int y0 = touch_.startY();
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
  if (touch_.poll(*lcd_, te)) onTouch(te);

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
  if (toast_[0] && static_cast<int32_t>(now - toastUntil_) >= 0) {
    toast_[0] = 0;
    dirty_ = true;
  }
  if (screen()->wantsRedraw(status_)) dirty_ = true;

  if (dirty_ && now - lastDraw_ >= kFrameMs) {
    draw();
    dirty_ = false;
    lastDraw_ = now;
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
  spr_->drawString(buf, 200, 4);
  spr_->drawString(status_.playing ? "PLAY" : (status_.paused ? "PAUSE" : "STOP"), 300, 4);
  snprintf(buf, sizeof(buf), "L%lu", static_cast<unsigned long>(status_.loop));
  spr_->drawString(buf, 400, 4);
}

void App::drawTabs() {
  static const char* const kNames[] = {"GRID", "TRACK", "BANK", "PROJ", "FILE"};
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
