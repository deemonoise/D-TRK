#include "synth_delay.h"
#include <math.h>
#include <string.h>
#include "hot.h"
#include "synth_osc.h"

namespace mt {
namespace {

constexpr float kFbMax = 0.95f;  // fb 127: repeats still die out
constexpr uint8_t kDigital = static_cast<uint8_t>(DelayMode::Digital);
constexpr uint8_t kTape = static_cast<uint8_t>(DelayMode::Tape);
constexpr uint8_t kPingPong = static_cast<uint8_t>(DelayMode::PingPong);
constexpr float kMs = kSynthRate / 1000.f;
constexpr float kModMax = (Delay::kWowMs + Delay::kFlutterMs) * kMs + 2.f;  // TAPE offset headroom
constexpr int kChunk = 128;  // fade-out block work size

// TONE 0..126 -> LP cutoff 300 Hz .. 16 kHz exponentially; one-pole coefficient.
float toneCoef(uint8_t tone) {
  if (tone >= 127) return 1.f;
  const float hz = 300.f * powf(16000.f / 300.f, tone / 127.f);
  return 1.f - expf(-6.2831853f * hz / kSynthRate);
}

const float kGlideK = 1.f - expf(-kFxSeg / (Delay::kGlideMs * kMs));
const float kWearK = 1.f - expf(-kFxSeg / (Delay::kWearMs * kMs));

float fbGain(uint8_t fb) { return (fb > 127 ? 127 : fb) * (kFbMax / 127.f); }
float levelGain(uint8_t level) { return (level > 127 ? 127 : level) * (1.f / 127.f); }

}  // namespace

void Delay::setBuffer(int16_t* buf, uint32_t len) {
  buf_ = len > 1 ? buf : nullptr;
  len_ = buf_ ? len : 0;
  clear();
}

void Delay::resetModeState() {
  w_ = 0;
  lp_ = 0;
  lpR_ = 0;
  dCur_ = -1;
  hp_ = 0;
  seg_ = 0;
}

void Delay::clear() {
  if (buf_) memset(buf_, 0, sizeof(int16_t) * len_);
  resetModeState();
  wipe_ = len_;
}

void Delay::process(const float* send, float* mixL, float* mixR, int n, uint32_t delay, uint8_t fb, uint8_t tone,
                    uint8_t level) {
  process(send, mixL, mixR, n, delay, fb, tone, level, kDigital, 0);
}

MT_HOT void Delay::run(uint8_t mode, const float* send, float* mixL, float* mixR, int n, uint32_t delay, uint8_t fb,
                uint8_t level, uint8_t chr) {
  if (mode == kTape) {
    tape(send, mixL, mixR, n, delay, fb, level, chr);
  } else if (mode == kPingPong) {
    pingPong(send, mixL, mixR, n, delay, fb, level, chr);
  } else if (dCur_ >= 0) {
    // From TAPE: Wear fades out and the glide ends first (no jump), then the DIGITAL loop.
    tape(send, mixL, mixR, n, delay, fb, level, 0);
    float hi = static_cast<float>(len_) - 2.f - kModMax;
    const float t = static_cast<float>(delay < 1 ? 1 : delay);
    if (tapeSettled(t < hi ? t : hi) || delay > hi) dCur_ = -1;
  } else {
    digital(send, mixL, mixR, n, delay, fb, level);
  }
}

MT_HOT bool Delay::tapeSettled(float target) const {
  return wear_ == 0.f && dCur_ == target && seg_ == 0 && off_ == target;
}

MT_HOT void Delay::process(const float* send, float* mixL, float* mixR, int n, uint32_t delay, uint8_t fb,
                           uint8_t tone, uint8_t level, uint8_t mode, uint8_t chr) {
  if (!buf_ || n <= 0) return;
  if (mode >= static_cast<uint8_t>(DelayMode::Count)) mode = kDigital;
  if (tone != tone_) {
    tone_ = tone;
    coef_ = toneCoef(tone);
  }
  // A wipe or a layout change (PING-PONG <-> the others): out of line, per change.
  if (wipe_ < len_ || (mode == kPingPong) != (mode_ == kPingPong)) {
    transition(send, mixL, mixR, n, delay, fb, level, mode, chr);
    return;
  }
  mode_ = mode;
  run(mode, send, mixL, mixR, n, delay, fb, level, chr);
}

// Flash: a block of the wipe (muted) or the layout change's fade-out (then the wipe starts).
void Delay::transition(const float* send, float* mixL, float* mixR, int n, uint32_t delay, uint8_t fb,
                       uint8_t level, uint8_t mode, uint8_t chr) {
  if (wipe_ < len_) {  // muted while the old layout is wiped
    const uint32_t k = len_ - wipe_ < kWipe ? len_ - wipe_ : kWipe;
    memset(buf_ + wipe_, 0, sizeof(int16_t) * k);
    wipe_ += k;
    mode_ = mode;
    if (wipe_ >= len_) resetModeState();
    return;
  }
  // Layout change: the old mode's return fades out over this block, then the wipe starts.
  const float step = 1.f / n;
  float g = 1.f;
  for (int i = 0; i < n; i += kChunk) {
    const int k = n - i < kChunk ? n - i : kChunk;
    float l[kChunk] = {}, r[kChunk] = {};
    run(mode_, send + i, l, r, k, delay, fb, level, chr);
    for (int j = 0; j < k; ++j) {
      g -= step;
      mixL[i + j] += l[j] * g;
      mixR[i + j] += r[j] * g;
    }
  }
  mode_ = mode;
  wipe_ = 0;
}

MT_HOT void Delay::digital(const float* send, float* mixL, float* mixR, int n, uint32_t delay, uint8_t fb,
                           uint8_t level) {
  if (delay < 1) delay = 1;
  if (delay > len_ - 1) delay = len_ - 1;
  const float a = coef_;
  const float g = (fb > 127 ? 127 : fb) * (kFbMax / 127.f);
  const float lv = (level > 127 ? 127 : level) * (1.f / 127.f);
  uint32_t w = w_;
  uint32_t r = w >= delay ? w - delay : w + len_ - delay;
  float lp = lp_;
  for (int i = 0; i < n; ++i) {
    lp += (buf_[r] * (1.f / kScale) - lp) * a;
    const float y = lp * lv;
    mixL[i] += y;
    mixR[i] += y;
    float x = (send[i] + lp * g) * kScale;
    x = x > 32767.f ? 32767.f : (x < -32768.f ? -32768.f : x);
    buf_[w] = static_cast<int16_t>(x < 0 ? x - 0.5f : x + 0.5f);  // no lrintf: a flash call per sample
    if (++w == len_) w = 0;
    if (++r == len_) r = 0;
  }
  // Denormals: a long silent tail would crawl through them on the S3's FPU.
  lp_ = fabsf(lp) < 1e-9f ? 0.f : lp;
  w_ = w;
}

// TAPE: the next segment's glide, Wear and LFOs (per kFxSeg samples, ~1 kHz: flash); the read
// offset ramps from offQ (the last segment's end) by dQ per sample.
void Delay::tapeSegment(float target, float lim, float wearT, int32_t& offQ, int32_t& dQ, float& hpA,
                        float& satK) {
  constexpr float kQ = static_cast<float>(1 << kOffFrac);
  const float glide = kGlideK;
  const float wearK = kWearK;
  float step = (target - dCur_) * glide;
  const float mx = kMaxSlew * kFxSeg;
  step = step > mx ? mx : (step < -mx ? -mx : step);
  dCur_ += step;
  if (fabsf(target - dCur_) < 0.01f) dCur_ = target;
  wear_ += (wearT - wear_) * wearK;
  if (fabsf(wearT - wear_) < 1e-4f) wear_ = wearT;
  wow_.advance(kWowHz, kFxSeg);
  flutter_.advance(kFlutterHz, kFxSeg);
  float end = dCur_ + wear_ * (kWowMs * kMs * wow_.at(0.f) + kFlutterMs * kMs * flutter_.at(0.f));
  end = end < 1.f ? 1.f : (end > lim ? lim : end);
  off_ = end;
  offQ = endQ_;  // the last segment's end, exactly
  endQ_ = static_cast<int32_t>(end * kQ + 0.5f);
  dQ = (endQ_ - offQ) / kFxSeg;  // toward zero: the ramp stays inside [start, end]
  const float wc = (60.f + 190.f * wear_) * (6.2831853f / kSynthRate);  // HP 60 -> 250 Hz, no __divsf3
  hpA = wc * (1.f - 0.5f * wc);                                       // ~1 - e^-wc
  satK = 1.5f * wear_;
}

MT_HOT void Delay::tape(const float* send, float* mixL, float* mixR, int n, uint32_t delay, uint8_t fb,
                        uint8_t level, uint8_t chr) {
  constexpr float kQ = static_cast<float>(1 << kOffFrac), kRq = 1.f / kQ;
  constexpr int32_t kFMask = (1 << kOffFrac) - 1;
  const float wearT = (chr > 127 ? 127 : chr) * (1.f / 127.f);
  // The read stays inside the line (and inside the fixed-point range).
  constexpr float kQMax = static_cast<float>((1u << (31 - kOffFrac)) - 2u);
  float lim = static_cast<float>(len_) - 2.f;
  if (lim > kQMax) lim = kQMax;
  // Target time, leaving room for the modulation inside the line.
  float hi = lim - kModMax;
  if (hi < 1.f) hi = 1.f;
  float target = static_cast<float>(delay);
  target = target < 1.f ? 1.f : (target > hi ? hi : target);
  if (dCur_ < 0) {  // from DIGITAL: the read stays put, Wear fades in
    dCur_ = target;
    off_ = target;
    offQ_ = endQ_ = static_cast<int32_t>(target * kQ + 0.5f);
    seg_ = 0;
    wear_ = 0;
  }
  if (tapeSettled(target) && wearT == 0.f) {
    digital(send, mixL, mixR, n, static_cast<uint32_t>(target), fb, level);
    return;
  }
  const float a = coef_;
  const float g = fbGain(fb);
  const float lv = levelGain(level);
  int16_t* const buf = buf_;
  const int32_t len = static_cast<int32_t>(len_);
  int32_t w = static_cast<int32_t>(w_);
  float lp = lp_, hp = hp_;
  float hpA = hpA_, satK = satK_;
  int32_t offQ = offQ_, dQ = dQ_;
  int seg = seg_;
  for (int i = 0; i < n;) {
    if (seg == 0) {  // next segment: glide, Wear and LFOs at its end; the offset ramps there
      tapeSegment(target, lim, wearT, offQ, dQ, hpA, satK);
      seg = kFxSeg;
    }
    const int k = n - i < seg ? n - i : seg;
    for (int j = i; j < i + k; ++j) {
      offQ += dQ;
      int32_t i0 = w - (offQ >> kOffFrac);  // newer sample
      if (i0 < 0) i0 += len;
      int32_t i1 = i0 - 1;  // older
      if (i1 < 0) i1 += len;
      const float x0 = buf[i0];
      const float x = x0 + (static_cast<float>(buf[i1]) - x0) * (static_cast<float>(offQ & kFMask) * kRq);
      lp += (x * (1.f / kScale) - lp) * a;
      const float y = lp * lv;
      mixL[j] += y;
      mixR[j] += y;
      hp += (lp - hp) * hpA;
      const float f = softSat((lp - hp) * g, satK);
      buf[w] = toLine((send[j] + f) * kScale);
      if (++w == len) w = 0;
    }
    i += k;
    seg -= k;
  }
  // Wear 0 and the glide done: land exactly on the target so the DIGITAL loop can take over.
  if (seg == 0 && wear_ == 0.f && dCur_ == target && fabsf(off_ - target) < 1e-3f) {
    off_ = target;
    offQ = endQ_ = static_cast<int32_t>(target * kQ + 0.5f);
  }
  lp_ = fabsf(lp) < 1e-9f ? 0.f : lp;
  hp_ = fabsf(hp) < 1e-9f ? 0.f : hp;
  offQ_ = offQ;
  dQ_ = dQ;
  seg_ = seg;
  hpA_ = hpA;
  satK_ = satK;
  w_ = static_cast<uint32_t>(w);
}

MT_HOT void Delay::pingPong(const float* send, float* mixL, float* mixR, int n, uint32_t delay, uint8_t fb,
                            uint8_t level, uint8_t chr) {
  const uint32_t half = len_ / 2;
  if (half < 2) return;
  if (delay < 1) delay = 1;
  if (delay > half - 1) delay = half - 1;
  const float wd = (chr > 127 ? 127 : chr) * (1.f / 127.f);
  const float a = coef_;
  const float g = fbGain(fb);
  const float lv = levelGain(level);
  const float same = (0.5f + 0.5f * wd) * lv, cross = (0.5f - 0.5f * wd) * lv;
  int16_t* L = buf_;
  int16_t* R = buf_ + half;
  uint32_t w = w_ % half;
  uint32_t r = w >= delay ? w - delay : w + half - delay;
  float lpL = lp_, lpR = lpR_;
  for (int i = 0; i < n; ++i) {
    lpL += (L[r] * (1.f / kScale) - lpL) * a;
    lpR += (R[r] * (1.f / kScale) - lpR) * a;
    mixL[i] += lpL * same + lpR * cross;
    mixR[i] += lpR * same + lpL * cross;
    L[w] = toLine((send[i] + lpR * g) * kScale);
    R[w] = toLine(lpL * kScale);
    if (++w == half) w = 0;
    if (++r == half) r = 0;
  }
  lp_ = fabsf(lpL) < 1e-9f ? 0.f : lpL;
  lpR_ = fabsf(lpR) < 1e-9f ? 0.f : lpR;
  w_ = w;
}

}  // namespace mt
