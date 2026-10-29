#include "app.h"
#include "fx_info.h"
#include "grid_screen.h"
#include <stdio.h>
#include <string.h>

namespace ui {

void GridScreen::automationEdit(int value, bool erase) {
  app_.setMotionRecording(false);
  auto &step = pat().steps[track()][cur()];
  mt::Step out = step;
  const mt::Fx fx = mt::automationFx(autoParam_);
  if (erase)
    mt::automationClear(out, fx);
  else if (!mt::automationSet(out, fx, static_cast<uint8_t>(value))) {
    app_.toast("NO FX SLOT");
    return;
  }
  if (memcmp(&out, &step, sizeof(step)) == 0)
    return;
  if (!editPushed_) {
    app_.pushUndo();
    editPushed_ = true;
  }
  engine::lockProject();
  step = out;
  engine::unlockProject();
  app_.markDirty();
}

void GridScreen::automationInput(const hw::InputEvent &ev) {
  using hw::InputType;
  const mt::Fx fx = mt::automationFx(autoParam_);
  const int old = mt::automationGet(pat().steps[track()][cur()], fx);
  const uint8_t value = old < 0 ? mt::fxDefault(fx) : static_cast<uint8_t>(old);
  switch (ev.type) {
  case InputType::EncTurn:
    if (ev.shift && !edit_) {
      autoParam_ = ((autoParam_ + ev.delta) % mt::kAutomationParams +
                    mt::kAutomationParams) %
                   mt::kAutomationParams;
      autoAnchor_ = -1;
    } else if (edit_)
      automationEdit(mt::fxStep(fx, value, ev.delta * (ev.shift ? 10 : 1)));
    else {
      moveStep(ev.delta);
      editPushed_ = false;
    }
    break;
  case InputType::EditTurn:
    if (!holdEdit_) editPushed_ = false;
    holdEdit_ = true;
    automationEdit(mt::fxStep(fx, value, ev.delta * (ev.shift ? 10 : 1)));
    break;
  case InputType::EditEnd:
    holdEdit_ = false;
    editPushed_ = false;
    break;
  case InputType::EncClick:
    if (ev.shift) {
      setView(View::Overview);
      break;
    }
    edit_ = !edit_;
    editPushed_ = false;
    break;
  case InputType::EncLong:
    edit_ = false;
    editPushed_ = false;
    openAutomationMenu();
    break;
  default:
    break;
  }
  app_.invalidate();
}

void GridScreen::automationTouch(const TouchEvent &ev) {
  if (ev.type == TouchType::LongPress) {
    openAutomationMenu();
    return;
  }
  if (ev.type != TouchType::Tap && ev.type != TouchType::Drag &&
      ev.type != TouchType::HDrag)
    return;
  if (ev.y < y0_ + 24 || ev.y >= y0_ + h_ - 24 || ev.x < 32 || ev.x >= kScreenW)
    return;
  const int step = autoLeft_ + (ev.x - 32) / 28;
  if (step >= len())
    return;
  if (autoGesture_ != ev.id || ev.type == TouchType::Tap) {
    editPushed_ = false;
    autoGesture_ = ev.id;
  }
  curStep_ = step;
  cursorMoved();
  const auto fx = mt::automationFx(autoParam_);
  const int max = mt::fxStep(fx, 0, 1000);
  const int graphH = h_ - 64;
  int v = (y0_ + h_ - 40 - ev.y) * max / graphH;
  v = v < 0 ? 0 : (v > max ? max : v);
  automationEdit(v);
}

void GridScreen::openAutomationMenu(bool refresh) {
  const MenuItem items[] = {
      {"Parameter...", 0},
      {app_.automationMuted(track()) ? "Automation: OFF" : "Automation: ON", 11, app_.project().trackInternal(track())},
      {"Track...", 10},
      {"Set ramp start", 1},
      {"Ramp to cursor", 2, autoAnchor_ >= 0},
      {"Clear lock", 3},
      {"Clear parameter lane", 4},
      {"Copy parameter lane", 5},
      {"Paste parameter lane", 6, autoClip_.len > 0}};
  const int pattern = app_.editPattern(), tr = track(), param = autoParam_;
  auto cb = [this, pattern, tr, param](int id) {
    if (id == 11) { app_.toggleAutomationMute(tr); return; }
    if (id == 0) {
      openAutomationParams();
      return;
    }
    if (id == 10) {
      openAutomationTracks();
      return;
    }
    if (pattern != app_.editPattern() || tr != track() || param != autoParam_) {
      app_.toast("TARGET CHANGED");
      return;
    }
    const auto fx = mt::automationFx(autoParam_);
    auto *steps = pat().steps[track()];
    const int v = mt::automationGet(steps[cur()], fx);
    if (id == 1) {
      autoAnchor_ = cur();
      autoAnchorValue_ = v < 0 ? mt::fxDefault(fx) : v;
      app_.toast("RAMP START SET");
      return;
    }
    if (id == 5) {
      mt::automationCopy(steps, len(), fx, autoClip_);
      app_.toast("PARAMETER COPIED");
      return;
    }
    editPushed_ = false;
    if (id == 3) {
      automationEdit(0, true);
      editPushed_ = false;
      return;
    }
    if (id != 2 && id != 4 && id != 6)
      return;
    app_.setMotionRecording(false);
    app_.pushUndo();
    engine::lockProject();
    bool ok = true;
    if (id == 2)
      ok = mt::automationRamp(steps, len(), fx, autoAnchor_, cur(),
                              autoAnchorValue_, v < 0 ? mt::fxDefault(fx) : v);
    if (id == 4)
      for (int n = 0; n < len(); ++n)
        mt::automationClear(steps[n], fx);
    if (id == 6)
      ok = mt::automationPaste(steps, len(), fx, autoClip_);
    engine::unlockProject();
    if (!ok) {
      app_.dropUndo();
      app_.toast("NO FX SLOT / RANGE");
    } else {
      app_.markDirty();
      app_.toast("AUTOMATION APPLIED");
    }
  };
  if (refresh) app_.menu().replace("AUTOMATION",items,9,cb);
  else app_.menu().open("AUTOMATION",items,9,cb);
}

void GridScreen::openAutomationParams() {
  MenuItem items[mt::kAutomationParams];
  for (int n = 0; n < mt::kAutomationParams; ++n)
    items[n] = {mt::fxLongName(mt::automationFx(n)), n};
  app_.menu().openChild("PARAMETER", items, mt::kAutomationParams, [this](int n) {
    autoParam_ = n;
    autoAnchor_ = -1;
    editPushed_ = false;
    app_.menu().back(); openAutomationMenu(true);
    app_.invalidate();
  });
}

void GridScreen::openAutomationTracks() {
  MenuItem items[mt::kTracks];
  for (int n = 0; n < mt::kTracks; ++n)
    items[n] = {app_.project().tracks[n].name, n};
  app_.menu().openChild("TRACK", items, mt::kTracks, [this](int n) {
    app_.setCurTrack(n);
    autoAnchor_ = -1;
    editPushed_ = false;
    app_.menu().back(); openAutomationMenu(true);
    app_.invalidate();
  });
}

void GridScreen::drawAutomation(LGFX_Sprite &s, int y, int h) {
  autoLeft_ = cur() / 16 * 16;
  const auto fx = mt::automationFx(autoParam_);
  char buf[64], val[5];
  const int current = mt::automationGet(pat().steps[track()][cur()], fx);
  if (current < 0)
    snprintf(val, sizeof(val), "---");
  else
    mt::fxFormat(fx, current, val);
  const int badgeW = drawBadge(s, y);
  const int slots = mt::automationSlotsUsed(pat().steps[track()][cur()]);
  snprintf(buf, sizeof(buf), "T%d %s %s %d-%d FX%d/%d%s", track() + 1,
           mt::fxName(fx), val, autoLeft_ + 1, autoLeft_ + 16 < len() ? autoLeft_ + 16 : len(),
           slots, mt::kFxSlots, app_.automationMuted(track()) && (rec_ || app_.motionRecording() || perf()) ? " AUTO OFF" : "");
  s.setTextColor(slots == mt::kFxSlots ? kRed : kCursor);
  s.drawString(buf, badgeW ? badgeW + 8 : 8, y + 2);
  const int base = y + h - 40, graphH = h - 64, max = mt::fxStep(fx, 0, 1000);
  const auto &st = app_.status();
  const int heard = st.playing
                        ? mt::automationStep(pat(), track(), st.pos, st.loop,
                                             engine::stepPhase(st))
                        : -1;
  for (int c = 0; c < 16; ++c) {
    const int n = autoLeft_ + c, x = 32 + c * 28;
    if (n >= len())
      break;
    s.fillRect(x, y + 24, 27, graphH + 1,
               n == heard ? kPlayBg : (n % 4 == 0 ? kBeatBg : kBg));
    const int v = mt::automationGet(pat().steps[track()][n], fx);
    if (v >= 0) {
      const int bh = max > 0 ? v * graphH / max : 0;
      s.fillRect(x + 4, base - bh, 19, bh + 1, kCyan);
    } else {
      s.setTextColor(kDim);
      s.drawString("-", x + 10, base - 16);
    }
    if (n == cur())
      s.drawRect(x, y + 24, 27, graphH + 1, edit_ ? kEditCursor : kCursor);
    if (n == autoAnchor_)
      s.fillTriangle(x + 8, y + 18, x + 18, y + 18, x + 13, y + 23, kYellow);
    snprintf(buf, sizeof(buf), "%d", n + 1);
    s.setTextColor(n == cur() ? kCursor : kDim);
    s.drawString(buf, x + 2, base + 4);
  }
  s.setTextColor(kDim);
  s.drawString("SHIFT: PARAM  A+TURN: VALUE  HOLD: MENU", 8, y + h - 18);
}
} // namespace ui
