#include "synth_perf.h"
#include <math.h>
#include <string.h>
#include "fastmath.h"
#include "hot.h"
#include "synth_osc.h"

namespace mt {

namespace {

constexpr float kFadeStep = 1.f / PerfBus::kXfade;
constexpr float kGateStep = 1.f / 32.f;  // gate open / close ramp: 1 ms

// One tone stage over a block. Fully wet (held, fade 1): block(l, r, n) applies the effect in place.
// Otherwise fx(l, r) turns each dry pair into the wet pair (the state runs on every sample, also
// while fading out) and the output is dry + (wet - dry) x fade, fade stepping towards on by
// 1 / kXfade after each sample (kXfade samples from dry to wet). Returns true when the stage has
// just faded out (its state to reset).
template <class F, class B>
MT_INLINE bool stage(float& fade, bool on, float* __restrict l, float* __restrict r, int n, F&& fx,
                     B&& block) {
  if (!on && fade == 0.f) return false;
  if (on && fade == 1.f) {
    block(l, r, n);
    return false;
  }
  float a = fade;
  for (int i = 0; i < n; ++i) {
    float wl = l[i], wr = r[i];
    fx(wl, wr);
    if (a >= 1.f) {
      l[i] = wl;
      r[i] = wr;
    } else {
      l[i] += (wl - l[i]) * a;
      r[i] += (wr - r[i]) * a;
    }
    if (on) a = a + kFadeStep >= 1.f ? 1.f : a + kFadeStep;
    else a = a - kFadeStep <= 0.f ? 0.f : a - kFadeStep;
  }
  fade = a;
  return a == 0.f;
}

// floor(x / step + 0.5) x step without floorf (see synth_crush.h). x clamped to +-1, NaN -> 0:
// the int conversion of a NaN / huge value is UB.
MT_INLINE float quantize(float x, float step, float inv) {
  x = x > 1.f ? 1.f : (x < -1.f ? -1.f : (x == x ? x : 0.f));
  const float y = x * inv + 0.5f;
  int i = static_cast<int>(y);
  if (static_cast<float>(i) > y) --i;
  return static_cast<float>(i) * step;
}

// Nearest int16 of x in +-32764 (no lroundf).
MT_INLINE int16_t roundS16(float x) { return static_cast<int16_t>(x < 0.f ? x - 0.5f : x + 0.5f); }

constexpr float kInvScale = 1.f / PerfBus::kScale;
constexpr uint32_t kGuard = 2 * 128;  // loops end this far behind the writer (2 blocks)
constexpr int32_t kRevMargin = PerfBus::kXfade + 3;  // REVERSE reads on below its window (seam)

// The ring at frame base + off, linearly interpolated (off may be negative: |off| < frames).
MT_INLINE void readRing(const int16_t* __restrict b, uint32_t frames, uint32_t base, float off,
                        float& l, float& r) {
  int k = static_cast<int>(off);
  if (static_cast<float>(k) > off) --k;  // floor
  const float fr = off - static_cast<float>(k);
  int32_t i0 = static_cast<int32_t>(base) + k;
  if (i0 >= static_cast<int32_t>(frames)) i0 -= frames;
  else if (i0 < 0) i0 += frames;
  const int32_t i1 = i0 + 1 >= static_cast<int32_t>(frames) ? 0 : i0 + 1;
  const float l0 = b[2 * i0], r0 = b[2 * i0 + 1];
  l = (l0 + (b[2 * i1] - l0) * fr) * kInvScale;
  r = (r0 + (b[2 * i1 + 1] - r0) * fr) * kInvScale;
}

}  // namespace

MT_INLINE void PerfBus::play(Head& h, const int16_t* __restrict b, uint32_t frames, float& l,
                             float& r) {
  switch (h.fx) {
    case PerfFx::Scratch: {
      if (h.phase >= 1.f) h.phase -= 1.f; // outgoing heads keep playing across a stroke boundary
      const bool back = h.phase < 0.25f;
      const float q = back ? h.phase * 4.f : (h.phase - 0.25f) * (4.f / 3.f);
      const float smooth = q * q * (3.f - 2.f * q);
      const float pos = back ? -h.amp * smooth : -h.amp * (1.f - smooth);
      readRing(b, frames, h.base, pos, l, r);
      // Brief crossfader closures make distinct strokes instead of a continuous pitch wobble.
      const float edge = (q < 0.5f ? q : 1.f - q) * 16.f;
      const float gain = 0.15f + 0.85f * (edge < 0.f ? 0.f : edge > 1.f ? 1.f : edge);
      l *= gain;
      r *= gain;
      h.phase += h.phaseInc;
      break;
    }
    case PerfFx::TapeStop:
      if (h.gain <= 0.f) {  // stopped and faded: exact silence
        l = r = 0.f;
        break;
      }
      readRing(b, frames, h.base, h.off, l, r);
      if (h.speed > 0.f) {
        h.off += h.speed;
        h.speed = h.speed - h.dec > 0.f ? h.speed - h.dec : 0.f;
        h.u += 1.f;
      } else {
        l *= h.gain;
        r *= h.gain;
        h.gain -= 1.f / kXfade;  // exact: 64 steps of 2^-6 reach 0
      }
      break;
    default:  // REPEAT, ROLL, REVERSE
      readRing(b, frames, h.base, h.off, l, r);
      h.off += h.speed;
      break;
  }
}

// TAPE STOP: the whole frames of off move into base, so off stays small and a slow speed still
// adds to it at full float precision (off reaches t / 2 = 64000 frames otherwise).
void PerfBus::settle(Head& h) const {
  if (h.fx == PerfFx::TapeStop) {
    const uint32_t k = static_cast<uint32_t>(h.off);  // off >= 0
    h.base = (h.base + k) % frames_;
    h.off -= static_cast<float>(k);
  }
}

void PerfBus::setBuffer(int16_t* buf, uint32_t frames, bool clear) {
  buf_ = frames ? buf : nullptr;
  frames_ = buf_ ? frames : 0;
  w_ = 0;
  fresh_ = 0;
  head_ = Head{};
  out_ = Head{};
  xf_ = 0;
  if (buf_ && clear) memset(buf_, 0, sizeof(int16_t) * 2 * frames_);
}

void PerfBus::reset() {
  stack_.releaseAll();
  dTail_ = rTail_ = mTail_ = 0;
  fresh_ = 0;  // the next capture does not reach back across the park
  head_ = Head{};
  out_ = Head{};
  xf_ = 0;
  for (float& f : fade_) f = 0;
  gateGain_ = 1;
  filtL_.reset();
  filtR_.reset();
  filtSign_ = 0;
  downLeft_ = 0;
  downL_ = downR_ = 0;
}

MT_HOT float PerfBus::delaySend() const {
  return stack_.held(PerfFx::DelayThrow) ? stack_.depth(PerfFx::DelayThrow) / 127.f : 0.f;
}

MT_HOT float PerfBus::reverbSend() const {
  return stack_.held(PerfFx::ReverbThrow) ? stack_.depth(PerfFx::ReverbThrow) / 127.f : 0.f;
}

MT_HOT float PerfBus::modSend() const {
  return stack_.held(PerfFx::ModThrow) ? stack_.depth(PerfFx::ModThrow) / 127.f : 0.f;
}

MT_HOT float PerfBus::freezeDry() const {
  return stack_.held(PerfFx::Freeze) ? stack_.depth(PerfFx::Freeze) / 100.f : 1.f;
}

MT_HOT void PerfBus::writeBuffer(const float* l, const float* r, int n) {
  if (!buf_) return;
  int16_t* __restrict b = buf_;
  uint32_t w = w_;
  for (int i = 0; i < n; ++i) {
    float x = l[i], y = r[i];
    x = !(x > -4.f) ? -4.f : (x > 4.f ? 4.f : x);  // NaN -> -4
    y = !(y > -4.f) ? -4.f : (y > 4.f ? 4.f : y);
    b[2 * w] = roundS16(x * kScale);
    b[2 * w + 1] = roundS16(y * kScale);
    if (++w >= frames_) w = 0;
  }
  w_ = w;
}

bool PerfBus::writeBlocked(uint32_t w, int n) const {
  // Each head reads a region that ends at its lowest frame lo = base + prot. The writer moves away
  // from it and would come round onto it after a full ring (a hold of about the ring length or
  // longer): it parks short of lo. REPEAT / ROLL: lo = base, and d == 0 is a loop that starts at
  // the writer (pressed on a step or after a pause), not a wrap. REVERSE / SCRATCH read only below
  // the press point, so the writer is never at lo legitimately. TAPE STOP trails the writer at a
  // lower speed and never needs it. Only the playing head blocks: the outgoing one lives for
  // kXfade samples, and holding the writer for it would leave a new capture reading frames the
  // writer never wrote. Its fading tail can read up to n just-overwritten frames after a hold of
  // about the ring length or longer.
  const PerfFx f = head_.fx;
  if (f == PerfFx::None || f == PerfFx::TapeStop) return false;
  const uint32_t lo =
      static_cast<uint32_t>((static_cast<int64_t>(head_.base) + frames_ + head_.prot) % frames_);
  const uint32_t d = (lo + frames_ - w) % frames_;
  if (f == PerfFx::Repeat || f == PerfFx::Roll) return d > 0 && d <= static_cast<uint32_t>(n);
  return d < static_cast<uint32_t>(n);
}

MT_HOT float PerfBus::loopLen(float steps, float samplesPerStep) const {
  // At least kXfade + 1 frames: with the carried overshoot (< 1) every loop lasts > kXfade
  // samples, so a seam never lands inside the previous seam's crossfade. (Switches come between
  // blocks; two within kXfade samples - blocks shorter than 2 ms - drop the older source.)
  constexpr float kMin = static_cast<float>(kXfade + 1);
  const float len = steps * samplesPerStep, max = static_cast<float>(frames_ - kGuard);
  return len > max ? max : (len < kMin ? kMin : len);
}

MT_HOT uint32_t PerfBus::intact(const Head& h, uint32_t wEnd) const {
  // Behind base: back to the writer (which has overwritten everything after it) and no further
  // than what was recorded without a pause before the press.
  const uint32_t d = (h.base + frames_ - wEnd) % frames_;
  return d < h.avail ? d : h.avail;
}

MT_HOT void PerfBus::repeatWindow(Head& h, float over, float samplesPerStep) const {
  float len = loopLen(h.steps, samplesPerStep);
  if (len > h.avail) len = static_cast<float>(h.avail);
  uint32_t span = static_cast<uint32_t>(len);
  if (span < len) ++span;
  h.base = (h.end + frames_ - span) % frames_;
  h.prot = static_cast<int32_t>(span) - static_cast<int32_t>(h.avail);
  h.off = 0.f;
  h.left = len - over;
}

MT_HOT void PerfBus::reverseWindow(Head& h, float over, float samplesPerStep, uint32_t wEnd) const {
  // Reads base - 1 - k for k < len, then (outgoing, under the seam crossfade) kXfade further.
  constexpr float kMin = static_cast<float>(kXfade + 1);
  const float room = static_cast<float>(intact(h, wEnd)) - static_cast<float>(kRevMargin + 1);
  float len = perfReverseSteps(stack_.depth(PerfFx::Reverse)) * samplesPerStep;
  if (len > room) len = room;
  if (len < kMin) len = kMin;  // nothing recorded: a short window of whatever the ring holds
  h.off = -1.f;
  h.speed = -1.f;
  h.left = len - over;
  h.prot = -static_cast<int32_t>(len) - kRevMargin;
}

MT_HOT void PerfBus::scratchPeriod(Head& h, float over, float samplesPerStep, uint32_t wEnd) const {
  float t = perfScratchSteps(stack_.depth(PerfFx::Scratch)) * samplesPerStep;
  if (t < static_cast<float>(kXfade + 1)) t = static_cast<float>(kXfade + 1);
  const float room = static_cast<float>(intact(h, wEnd)) - 4.f;
  h.amp = room > 0.f ? (0.75f * t < room ? 0.75f * t : room) : 0.f;
  h.period = t;
  h.phase = over / t;
  h.phaseInc = 1.f / t;
  h.left = t - over;
  int32_t span = static_cast<int32_t>(h.amp);
  if (static_cast<float>(span) < h.amp) ++span;
  h.prot = -span - 4;
}

bool PerfBus::canCapture(PerfFx f, int n, float samplesPerStep, double grid) const {
  if (f == PerfFx::Repeat || f == PerfFx::Roll) {
    const double sinceFrames = (grid - static_cast<double>(static_cast<int64_t>(grid))) * samplesPerStep;
    const uint32_t since = sinceFrames + 0.5 >= samplesPerStep ? 0 : static_cast<uint32_t>(sinceFrames + 0.5);
    const float wanted = loopLen(f == PerfFx::Roll ? 2.f : perfRepeatSteps(stack_.depth(f)), samplesPerStep);
    uint32_t needed = static_cast<uint32_t>(wanted);
    if (needed < wanted) ++needed;
    return fresh_ >= since && fresh_ - since >= needed;
  }
  // With little audio recorded behind the press point (just after a park or setBuffer) the
  // window / swing would be a few stale frames held for the whole press: play live instead until
  // the writer has recorded enough (the press point is then that block).
  if (f != PerfFx::Reverse && f != PerfFx::Scratch) return true;
  const float minimum = samplesPerStep * 0.25f > 512.f ? samplesPerStep * 0.25f : 512.f;
  const uint32_t ahead = frames_ - static_cast<uint32_t>(n);  // left behind after this block
  uint32_t got = fresh_ < ahead ? fresh_ : ahead;
  if (f == PerfFx::Scratch) got = got > 2 ? got - 2 : 0;
  const float room = static_cast<float>(got) -
                     (f == PerfFx::Reverse ? static_cast<float>(kRevMargin + 1) : 4.f);
  return room >= minimum;
}

void PerfBus::switchHead(PerfFx want, uint32_t w0, int n, double grid0, float samplesPerStep,
                         bool running) {
  out_ = head_;  // the old source keeps playing under the crossfade
  xf_ = kXfade;
  head_ = Head{};
  if (want == PerfFx::None) return;
  Head& h = head_;
  h.fx = want;
  const uint32_t wEnd = (w0 + static_cast<uint32_t>(n)) % frames_;  // the writer after this block
  switch (want) {
    case PerfFx::Repeat:
    case PerfFx::Roll: {
      // Freeze a complete window ending at the last clock boundary, never future input.
      uint32_t since = 0;
      if (running) {
        const double frames = (grid0 - static_cast<double>(static_cast<int64_t>(grid0))) * samplesPerStep;
        since = frames + 0.5 >= samplesPerStep ? 0 : static_cast<uint32_t>(frames + 0.5);
      }
      if (since > fresh_) since = fresh_;
      h.end = (w0 + frames_ - since) % frames_;
      h.avail = fresh_ - since;
      const float reserveFrames = loopLen(want == PerfFx::Roll ? 2.f : 8.f, samplesPerStep);
      uint32_t reserve = static_cast<uint32_t>(reserveFrames);
      if (reserve < reserveFrames) ++reserve;
      if (h.avail > reserve) h.avail = reserve;
      h.steps = want == PerfFx::Roll ? 2.f : perfRepeatSteps(stack_.depth(PerfFx::Repeat));
      repeatWindow(h, 0.f, samplesPerStep);
      break;
    }
    case PerfFx::Reverse:  // backwards from the press point
      h.base = w0;
      h.avail = fresh_;
      reverseWindow(h, 0.f, samplesPerStep, wEnd);
      break;
    case PerfFx::Scratch:  // around a point 2 frames back (the interpolation reads one ahead)
      h.base = (w0 + frames_ - 2) % frames_;
      h.avail = fresh_ > 2 ? fresh_ - 2 : 0;
      scratchPeriod(h, 0.f, samplesPerStep, wEnd);
      break;
    default:  // TapeStop: from the press point, speed 1 -> 0 (dec set per block)
      h.base = w0;
      h.seams = false;
      break;
  }
}

MT_HOT void PerfBus::seam(Head& h, Head& out, int& xf, float samplesPerStep) const {
  const float over = -h.left;  // 0..1: keeps fractional lengths on average
  switch (h.fx) {
    case PerfFx::Repeat:
    case PerfFx::Roll:
      out = h;  // reads on past the loop end (already recorded audio) under the crossfade
      xf = kXfade;
      if (h.fx == PerfFx::Repeat) {
        h.steps = perfRepeatSteps(stack_.depth(PerfFx::Repeat));  // a turn applies here
      } else if (++h.loops >= stack_.depth(PerfFx::Roll)) {
        h.loops = 0;
        h.steps = h.steps * 0.5f < 0.25f ? 0.25f : h.steps * 0.5f;  // floor 1/64
      }
      if (h.fx == PerfFx::Repeat) repeatWindow(h, over, samplesPerStep);
      else {
        h.off = 0;
        float len = loopLen(h.steps, samplesPerStep);
        if (len > h.avail) len = static_cast<float>(h.avail);
        h.left = len - over;
      }
      break;
    case PerfFx::Reverse:
      out = h;  // reads on below the window (protected) under the crossfade
      xf = kXfade;
      reverseWindow(h, over, samplesPerStep, w_);  // a turn applies here
      break;
    case PerfFx::Scratch: {
      // Period end: the position is back at -A. Same depth: continues seamlessly; a turn
      // (new T / A) crossfades from the old swing.
      const Head was = h;
      scratchPeriod(h, over, samplesPerStep, w_);
      if (h.period != was.period || h.amp != was.amp) {
        out = was;
        xf = kXfade;
      }
      break;
    }
    default:  // TapeStop: no seams
      break;
  }
}

MT_HOT void PerfBus::process(float* l, float* r, int n, float samplesPerStep, bool running) {
  // Buffer effects: the newest held one owns the head; a change (press / release) between blocks
  // switches sources at the block start; a returning effect captures afresh (no resume).
  const uint32_t w0 = w_;  // block start write frame
  if (syncStep_ >= 0) {  // re-anchor to the step clock at the block start
    if (running && samplesPerStep > 0.f) {
      double g = static_cast<double>(syncStep_ % static_cast<int>(kGridWrap)) -
                 static_cast<double>(syncOff_) / samplesPerStep;
      while (g < 0.0) g += kGridWrap;
      gridPos_ = g;
    }
    syncStep_ = -1;
  }
  const double grid0 = gridPos_;
  {
    PerfFx want = buf_ && frames_ > 2 * kGuard && samplesPerStep > 0.f ? stack_.buffered()
                                                                       : PerfFx::None;
    if (want != head_.fx && !canCapture(want, n, samplesPerStep, running ? grid0 : 0.0)) want = PerfFx::None;
    if (want != head_.fx) switchHead(want, w0, n, grid0, samplesPerStep, running);
    Head& h = head_;
    if (h.fx == PerfFx::TapeStop && h.speed > 0.f) {
      float t = perfTapeSteps(stack_.depth(PerfFx::TapeStop)) * samplesPerStep;
      // The writer must not lap the slowing head: it falls behind by up to t / 2 frames.
      const float max = 2.f * static_cast<float>(frames_ - 2 * kGuard);
      if (t > max) t = max;
      const float dec = 1.f / (t > 1.f ? t : 1.f);
      if (dec != h.dec) {  // pressed or turned: the current speed falls on the new slope
        h.v0 = h.speed;
        h.u = 0.f;
        h.dec = dec;
      }
      const float v = h.v0 - h.u * h.dec;  // exact per block: no drift over long stops
      h.speed = v > 0.f ? v : 0.f;
    }
  }
  // The buffer always records the live input (before every effect).
  if (buf_) {
    if (writeBlocked(w0, n)) {
      fresh_ = 0;
    } else {
      writeBuffer(l, r, n);
      fresh_ = fresh_ + n >= frames_ ? frames_ : fresh_ + n;
    }
  }
  const uint32_t un = static_cast<uint32_t>(n);
  dTail_ = stack_.held(PerfFx::DelayThrow) ? kTail : (dTail_ > un ? dTail_ - un : 0);
  rTail_ = stack_.held(PerfFx::ReverbThrow) || stack_.held(PerfFx::Freeze) ? kTail
                                                                           : (rTail_ > un ? rTail_ - un : 0);
  mTail_ = stack_.held(PerfFx::ModThrow) ? kTail : (mTail_ > un ? mTail_ - un : 0);
  // Grid: a double wrapped at kGridWrap steps (one add per block); the gate reads it as float.
  // Not running it stands still (and GATE with it). The synth passes "not paused" (it does not
  // know stop); the UI releases everything on stop.
  const float steps = running && samplesPerStep > 0.f ? n / samplesPerStep : 0.f;
  const float grid = static_cast<float>(gridPos_);  // block start
  gridPos_ += steps;
  if (gridPos_ >= kGridWrap) gridPos_ -= kGridWrap;

  bool idle = stack_.count() == 0 && head_.fx == PerfFx::None && xf_ == 0;
  for (int t = 0; t < kTones && idle; ++t) idle = fade_[t] == 0.f;
  if (idle) return;  // bit-exact

  // Buffer stage: the head (or live) with the outgoing source crossfaded over it.
  if (head_.fx != PerfFx::None || xf_ > 0) {
    const int16_t* b = buf_;
    const uint32_t fr = frames_;
    Head h = head_, o = out_;
    int xf = xf_;
    for (int i = 0; i < n; ++i) {
      float yl = l[i], yr = r[i];
      if (h.fx != PerfFx::None) {
        if (h.seams && h.left <= 0.f) seam(h, o, xf, samplesPerStep);
        play(h, b, fr, yl, yr);
        h.left -= 1.f;
      }
      if (xf > 0) {
        float ol = l[i], orr = r[i];
        if (o.fx != PerfFx::None) play(o, b, fr, ol, orr);
        const float g = static_cast<float>(xf) * kFadeStep;  // old source weight, 1 -> 1/kXfade
        yl += (ol - yl) * g;
        yr += (orr - yr) * g;
        --xf;
      }
      l[i] = yl;
      r[i] = yr;
    }
    settle(h);
    settle(o);
    head_ = h;
    out_ = o;
    xf_ = xf;
  }

  // GATE: open over the first half of each gate length on the grid.
  {
    const bool on = stack_.held(PerfFx::Gate);
    if (on || fade_[kGate] > 0.f) {
      if (on) gateLen_ = perfGateSteps(stack_.depth(PerfFx::Gate));
      const float inv = 1.f / gateLen_;
      float ph = grid * inv;
      ph -= static_cast<float>(static_cast<int>(ph));
      const float inc = steps * inv / static_cast<float>(n);
      float g = gateGain_;
      auto gate = [&](float& a, float& b) {
        const float target = ph < 0.5f ? 1.f : 0.f;
        if (g < target) g = g + kGateStep >= target ? target : g + kGateStep;
        else if (g > target) g = g - kGateStep <= target ? target : g - kGateStep;
        ph += inc;
        if (ph >= 1.f) ph -= 1.f;
        a *= g;
        b *= g;
      };
      if (stage(fade_[kGate], on, l, r, n, gate, [&](float* x, float* y, int m) {
            for (int i = 0; i < m; ++i) gate(x[i], y[i]);
          })) {
        g = 1;
      }
      gateGain_ = g;
    }
  }

  // FILTER: the old master DJ filter, depth -64..-1 low-pass, 1..63 high-pass, 0 off (bypass).
  // LP <-> HP while held (Shift skips 0): fade out on the old mode, reset, fade in on the new.
  {
    const int v = stack_.depth(PerfFx::Filter);
    const int8_t sign = v < 0 ? -1 : (v > 0 ? 1 : 0);
    const bool flip = filtSign_ != 0 && sign != filtSign_;
    const bool on = stack_.held(PerfFx::Filter) && v != 0 && !flip;
    if (on) {  // settings stay at their last values while fading out
      filtSign_ = sign;
      const float a = v < 0 ? -v / 64.f : v / 63.f;
      const float hz = v < 0 ? 20000.f * fastPow(100.f / 20000.f, a) : 20.f * fastPow(8000.f / 20.f, a);
      const Svf::Mode md = v < 0 ? Svf::Mode::Lp : Svf::Mode::Hp;
      filtL_.set(md, hz, 0.9f);
      filtR_.set(md, hz, 0.9f);
    }
    if (stage(fade_[kFilter], on, l, r, n,
              [&](float& a, float& b) {
                a = filtL_.process(a);
                b = filtR_.process(b);
              },
              [&](float* x, float* y, int m) {
                Svf f = filtL_;  // the state in registers
                for (int i = 0; i < m; ++i) x[i] = f.process(x[i]);
                filtL_ = f;
                f = filtR_;
                for (int i = 0; i < m; ++i) y[i] = f.process(y[i]);
                filtR_ = f;
              }) ||
        (flip && fade_[kFilter] == 0.f)) {
      filtL_.reset();
      filtR_.reset();
      filtSign_ = 0;
    }
  }

  // CRUSH: depth = bits, on the mix scaled to +-1. Stateless.
  {
    const bool on = stack_.held(PerfFx::Crush);
    if (on) {
      const int bits = stack_.depth(PerfFx::Crush);
      crushStep_ = 2.f / static_cast<float>(1 << (bits > 0 ? bits : 0));
    }
    const float step = crushStep_, inv = 1.f / step;
    stage(fade_[kCrush], on, l, r, n,
          [&](float& a, float& b) {
            a = quantize(a * 0.25f, step, inv) * 4.f;
            b = quantize(b * 0.25f, step, inv) * 4.f;
          },
          [&](float* x, float* y, int m) {
            for (int i = 0; i < m; ++i) x[i] = quantize(x[i] * 0.25f, step, inv) * 4.f;
            for (int i = 0; i < m; ++i) y[i] = quantize(y[i] * 0.25f, step, inv) * 4.f;
          });
  }

  // DOWNSAMPLE: hold both channels for kSynthRate / hz samples.
  {
    const bool on = stack_.held(PerfFx::Downsample);
    if (on) {
      const int hz = perfDownHz(stack_.depth(PerfFx::Downsample));
      downHold_ = (kSynthRate + hz / 2) / hz;
    }
    const int hold = downHold_;
    int left = downLeft_;
    float hl = downL_, hr = downR_;
    if (stage(fade_[kDown], on, l, r, n,
              [&](float& a, float& b) {
                if (left <= 0) {
                  hl = a;
                  hr = b;
                  left = hold;
                }
                --left;
                a = hl;
                b = hr;
              },
              [&](float* x, float* y, int m) {
                for (int i = 0; i < m; ++i) {
                  if (left <= 0) {
                    hl = x[i];
                    hr = y[i];
                    left = hold;
                  }
                  --left;
                  x[i] = hl;
                  y[i] = hr;
                }
              })) {
      left = 0;
    }
    downLeft_ = left;
    downL_ = hl;
    downR_ = hr;
  }

  // DRIVE: tanh on the mix scaled to +-1, depth 1..100 -> drive 1..127; depth 0 bypasses.
  {
    const int d = stack_.depth(PerfFx::Drive);
    const bool on = stack_.held(PerfFx::Drive) && d > 0;
    if (on) drive_.set(static_cast<uint8_t>(d * 127 / 100));
    const Drive& dr = drive_;
    stage(fade_[kDrive], on, l, r, n,
          [&](float& a, float& b) {
            a = dr.process(a * 0.25f) * 4.f;
            b = dr.process(b * 0.25f) * 4.f;
          },
          [&](float* x, float* y, int m) {
            for (int i = 0; i < m; ++i) x[i] *= 0.25f;
            for (int i = 0; i < m; ++i) y[i] *= 0.25f;
            dr.processBlock(x, m);
            dr.processBlock(y, m);
            for (int i = 0; i < m; ++i) x[i] *= 4.f;
            for (int i = 0; i < m; ++i) y[i] *= 4.f;
          });
  }
}

}  // namespace mt
