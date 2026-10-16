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
  if (!on) allOff();
  on_ = on;
  if (on && attached_) takeBuffer();
  else if (!on) freeBuffer();
}

void PerfPad::attach() {
  attached_ = true;
  if (on_) takeBuffer();
}

void PerfPad::detach() {
  allOff();
  attached_ = false;
  freeBuffer();  // the other screens need the PSRAM (imports, renders)
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

bool PerfPad::press(int n) {
  if (!on_ || !app_.status().playing || n < 0) return false;
  const uint8_t fx = n < mt::kPerfButtons ? app_.project().perfMap[n] : 0;
  if (fx == 0 || fx >= static_cast<uint8_t>(mt::PerfFx::Count)) {
    app_.toast("PERF ---");
    return true;
  }
  const mt::PerfFx f = static_cast<mt::PerfFx>(fx);
  held_[n] = static_cast<int8_t>(fx);
  stack_.press(f);
  if (!audio::perf(0, fx)) lost_ = true;
  // The button's start value (PROJ -> PERF): a turn in the same audio block, so the default never sounds.
  const mt::PerfRange r = mt::perfRange(f);
  const int start = clampi(app_.project().perfDepth[n], r.lo, r.hi);
  const int d = clampi(start - stack_.depth(f), -128, 127);
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
  if (n < 0 || n >= mt::kPerfButtons || held_[n] < 0) return;
  const int8_t fx = held_[n];
  held_[n] = -1;
  for (const int8_t h : held_)
    if (h == fx) return;  // another button still holds it
  stack_.release(static_cast<mt::PerfFx>(fx));
  if (!audio::perf(1, static_cast<uint8_t>(fx))) lost_ = true;
  app_.invalidate();  // the header loses its short name
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
  stack_.releaseAll();
  lost_ = !audio::perf(3, 0);
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
  if (!on_) return 0;
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
