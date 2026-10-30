#include "perf_pad.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "audio/audio.h"
#include "engine/engine.h"
#include "theme.h"

namespace ui {
namespace {
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

void PerfPad::setOn(bool on) {
  if (!on) {
    allOff();
    xy_ = false;
  }
  on_ = on;
  if (on && attached_) takeBuffer();
  else if (!on) freeBuffer();
}

void PerfPad::attach() {
  attached_ = true;
  if (on_) takeBuffer();
}

void PerfPad::detach() {
  buttonsOff();
  attached_ = false;
  fitBuffer();  // the other screens need the PSRAM (imports, renders); a pad's effect keeps it
}

void PerfPad::buttonsOff() {
  if (!padsHeld()) {
    allOff();  // release all goes out even with nothing held
    return;
  }
  xyTouch_ = false;
  for (int h = 0; h < kPadHolder; ++h) releaseHolder(h);
}

// Without a buffer the buffer effects are silent: say so.
void PerfPad::takeBuffer() {
  if (buffer_) return;
  buffer_ = true;
  if (audio::perfBuffer(true) == 0) {
    buffer_ = false;  // retried on the next attach / on
    app_.toast("PERF: NO MEMORY FOR REPEAT");
  }
}

void PerfPad::freeBuffer() {
  if (!buffer_) return;
  buffer_ = false;
  audio::perfBuffer(false);
}

void PerfPad::fitBuffer() {
  if (!(on_ && attached_) && !padsHeld()) freeBuffer();
}

bool PerfPad::press(int n) {
  if (!on_ || !app_.status().playing || n < 0) return false;
  return hold(n, n);
}

bool PerfPad::padPress(int n) {
  if (n < 0 || n >= mt::kPerfButtons) return false;
  releaseHolder(kPadHolder + n);  // a lost release (perfMap changed meanwhile) must not leak it
  if (!app_.status().playing) {
    fitBuffer();
    app_.toast("PERF: PRESS PLAY");
    return false;
  }
  const uint8_t fx = app_.project().perfMap[n];
  if (fx != 0 && fx < static_cast<uint8_t>(mt::PerfFx::Count)) takeBuffer();  // no-op when held
  const bool r = hold(kPadHolder + n, n);
  fitBuffer();  // "PERF ---": nothing held
  return r;
}

void PerfPad::padRelease(int n) {
  if (n < 0 || n >= mt::kPerfButtons) return;
  releaseHolder(kPadHolder + n);
  fitBuffer();
}

void PerfPad::padsOff() {
  for (int n = 0; n < mt::kPerfButtons; ++n) releaseHolder(kPadHolder + n);
  fitBuffer();
}

bool PerfPad::padsHeld() const {
  for (int n = 0; n < mt::kPerfButtons; ++n)
    if (held_[kPadHolder + n] >= 0) return true;
  return false;
}

bool PerfPad::hold(int h, int n) {
  const uint8_t fx = n < mt::kPerfButtons ? app_.project().perfMap[n] : 0;
  if (fx == 0 || fx >= static_cast<uint8_t>(mt::PerfFx::Count)) {
    app_.toast("PERF ---");
    return true;
  }
  const mt::PerfFx f = static_cast<mt::PerfFx>(fx);
  held_[h] = static_cast<int8_t>(fx);
  stack_.press(f);
  if (!audio::perf(0, fx)) lost_ = true;
  // The button's start value (PROJ -> PERF): a turn in the same audio block, so the default never sounds.
  const int start = mt::perfDepthNormalize(f, app_.project().perfDepth[n]);
  const int d = mt::perfDepthDelta(f, stack_.depth(f), start);
  if (d != 0) {
    stack_.turn(d);
    if (!audio::perf(2, static_cast<uint8_t>(static_cast<int8_t>(d)))) lost_ = true;
  }
  char msg[40], val[12];
  mt::perfValue(f, stack_.depth(f), val, sizeof(val));
  snprintf(msg, sizeof(msg), "PERF %s %s", mt::perfName(f), val);
  app_.toast(msg);
  return true;
}

void PerfPad::release(int n) {
  if (n < 0 || n >= mt::kPerfButtons) return;
  releaseHolder(n);
}

void PerfPad::releaseHolder(int h) {
  if (h < 0 || h >= kHolders || held_[h] < 0) return;
  const int8_t fx = held_[h];
  held_[h] = -1;
  for (const int8_t o : held_)
    if (o == fx) return;  // another button or axis still holds it
  stack_.release(static_cast<mt::PerfFx>(fx));
  if (!audio::perf(1, static_cast<uint8_t>(fx))) lost_ = true;
  app_.invalidate();  // the header loses its short name
}

void PerfPad::setXy(bool on) {
  if (!on) xyUp();
  xy_ = on && on_;
  app_.invalidate();
}

mt::PerfFx PerfPad::xyAxis(int a) const { return mt::perfXyAxis(app_.project().perfXy, a); }

bool PerfPad::xyDown(int x, int y, int w, int h) {
  if (!on_ || !xy_ || !app_.status().playing) return false;
  xyTouch_ = true;
  xyApply(x, y, w, h);
  return true;
}

void PerfPad::xyMove(int x, int y, int w, int h) {
  if (xyTouch_ && app_.status().playing) xyApply(x, y, w, h);  // a stop before poll(): nothing new
}

void PerfPad::xyUp() {
  if (!xyTouch_) return;
  xyTouch_ = false;
  releaseHolder(kXyHolder);
  releaseHolder(kXyHolder + 1);
}

// Y grows upwards. An axis effect a button already holds is joined: its depth follows the finger.
void PerfPad::xyApply(int x, int y, int w, int h) {
  const int pos[2] = {x, h - 1 - y}, len[2] = {w, h};
  for (int a = 0; a < 2; ++a) {
    const mt::PerfFx f = xyAxis(a);
    if (f == mt::PerfFx::None) continue;
    mt::PerfMsg m[2];
    send(m, stack_.setDepth(f, mt::perfAxisDepth(f, pos[a], len[a]), m));
    if (stack_.held(f)) held_[kXyHolder + a] = static_cast<int8_t>(f);
  }
}

void PerfPad::send(const mt::PerfMsg* m, int n) {
  for (int i = 0; i < n; ++i)
    if (!audio::perf(m[i].op, m[i].val)) lost_ = true;
}

bool PerfPad::turn(int delta, bool shift) {
  if (!on_ || stack_.count() == 0) return false;
  const mt::PerfFx last = stack_.last();
  const int d = clampi(delta * (shift && mt::perfRange(last).coarse ? 10 : 1), -127, 127);
  stack_.turn(d);
  if (!audio::perf(2, static_cast<uint8_t>(static_cast<int8_t>(d)))) lost_ = true;
  char msg[40], val[12];
  mt::perfValue(last, stack_.depth(last), val, sizeof(val));
  snprintf(msg, sizeof(msg), "%s %s", mt::perfName(last), val);
  app_.toast(msg);
  return true;
}

bool PerfPad::xfadeTurn(int delta, bool shift) {
  if (!on_ || stack_.count() > 0) return false;
  mt::Project& p = app_.project();
  int v = p.xfade + delta * (shift ? 8 : 1);
  v = v < 0 ? 0 : (v > 127 ? 127 : v);
  if (v != p.xfade) {
    engine::lockProject();
    p.xfade = static_cast<uint8_t>(v);
    engine::unlockProject();
    app_.markDirty();
  }
  char msg[32];
  snprintf(msg, sizeof(msg), "XFADE %d A%d>B%d", v, p.sceneA + 1, p.sceneB + 1);
  app_.toast(msg);
  return true;
}

// Always sent: a press lost in a full queue must not leave the synth holding an effect.
void PerfPad::allOff() {
  for (int8_t& h : held_) h = -1;
  xyTouch_ = false;
  stack_.releaseAll();
  lost_ = !audio::perf(3, 0);
  fitBuffer();  // a pad's effect may have kept it off a PERF tab
}

// The transport stopped under held effects (Play, a stop from elsewhere) or the audio parked (the
// synth released them): they end. A lost message: release all again once nothing is held.
void PerfPad::poll(bool playing) {
  const uint32_t parks = audio::parkCount();
  const bool parked = parks != parks_;
  parks_ = parks;
  if (stack_.count() > 0 && (parked || !playing)) {
    allOff();
    app_.invalidate();
  } else if (lost_ && stack_.count() == 0) {
    lost_ = !audio::perf(3, 0);
  }
}

int PerfPad::badge(char* out, int size) const {
  if (size <= 0) return 0;
  out[0] = 0;
  if (!on_ && !padsHeld()) return 0;  // pad effects show with PERF off too
  const uint8_t xf = app_.project().xfade;
  int n = xf ? snprintf(out, size, "PERF X%u", xf) : snprintf(out, size, "PERF");  // the crossfader off A
  if (n >= size) return size - 1;
  for (int i = 0; i < stack_.count(); ++i) {
    const char* sh = mt::perfShort(stack_.at(i));
    const int w = static_cast<int>(strlen(sh)) + 1;  // " RPT"
    if (n + w >= size) break;
    out[n++] = ' ';
    memcpy(out + n, sh, w - 1);
    n += w - 1;
    out[n] = 0;
  }
  return n;
}

// As many shorts as the header line holds (kCharW per char).
int PerfPad::drawBadge(LGFX_Sprite& s, int x, int y) const {
  char txt[kScreenW / kCharW + 1];
  const int n = badge(txt, static_cast<int>(sizeof(txt)) - x / kCharW);
  if (n == 0) return x;
  s.setTextColor(kYellow);
  s.drawString(txt, x, y);
  return x + n * kCharW;
}

}  // namespace ui
