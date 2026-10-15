// STRING instrument: a string / modal resonator with an internal click / noise exciter. Derived in parts
// from the resonator in github.com/pichenettes/eurorack (rings/dsp/string.cc, resonator.cc), MIT
// License: a float re-implementation at 44.1 kHz on this project's structures.
//
// Copyright 2015 Emilie Gillet.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
#include "synth_string.h"
#include <string.h>
#include "fastmath.h"
#include "synth_lines.h"
#include "synth_osc.h"

namespace mt {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kLineScale = 8192.f;      // line int16 = sample x 8192: +-4.0 (12 dB headroom)
constexpr float kLineMax = 3.99f;         // saturation of the loop (and the output)
constexpr float kExcGain = 2.6f;          // strike level into the loop: a click at BRIGHT 1 / 55 .. 110 Hz
                                          // peaks ~3.5 of the 4.0 rail (|out| < 1.9)
constexpr float kBurst = 4.5f;            // noise burst of len samples: kBurst / sqrt(len) of a click
constexpr float kBurstMin = 0.2f;         // .. at least (20.5 ms: 0.15 would match a click's energy)
constexpr float kTail = 0.25f;            // STRING: a period's peak under which the tail is scaled up x2
constexpr float kMaxGainUp = 65536.f;     // .. at most this much in all (then the int16 floor decides)
constexpr float kOut = 0.5f;              // output gain: |out| < 2 (the line saturates at 4)
constexpr int kMaxDelay = kLineLen - 2;   // read delay in line samples (one slot is being written)
constexpr float kMinHz = static_cast<float>(kSynthRate) / (4 * kMaxDelay);  // 21.6 Hz: lower notes hold
constexpr float kMaxHz = 0.25f * kSynthRate;  // a loop of 4 samples
constexpr float kQuiet = 24.f;            // line LSB below which the quantizer truncates (renderKs): only
                                          // reached past kMaxGainUp (the tail is scaled up before)
constexpr float kMaxPeriods = 20000.f;    // T60 <= this many periods (2 s at 10 kHz): above ~2 kHz a
                                          // longer loop lets the int16 rounding noise sustain itself
constexpr float kModalGain = 2.42f;       // MODAL: mode level for an impulse at 220 Hz: peaks about STRING's
                                          // at the same macros (mean of the macro grid, as with 8 modes at
                                          // 2.2), max ~1.3
constexpr float kModalRefG = 0.0156745f;  // tan(pi 220 / rate)
constexpr float kDonePeak = 1e-4f;        // done(): the output's peak under -80 dB
constexpr float kMaxLoop = 1.1f;          // loop gain bound (see controlKs)
constexpr float kDispersion = 0.12f;      // STRUCT 1: the all-pass's DC delay = 0.12 x the period

MT_INLINE float clamp01(float x) { return fclamp(x, 0.f, 1.f); }

// 1 / sqrt(x), x > 0 normal: a bit-trick seed and three Newton steps (control rate, no libm).
MT_INLINE float rsqrtF(float x) {
  float r = ffrom(0x5F3759DFu - (fbits(x) >> 1));
  for (int i = 0; i < 3; ++i) r = r * (1.5f - 0.5f * x * r * r);
  return r;
}

// sin / cos of w in 0 .. pi: Taylor series of the half angle (to h^12 / h^13, < 6e-8), doubled.
MT_INLINE void sinCos(float w, float& s, float& c) {
  const float h = 0.5f * w, h2 = h * h;
  const float sh = h * (1.f - h2 * (1.f / 6 - h2 * (1.f / 120 - h2 * (1.f / 5040 - h2 * (1.f / 362880 -
                                                                                        h2 * (1.f / 39916800))))));
  const float ch = 1.f - h2 * (0.5f - h2 * (1.f / 24 - h2 * (1.f / 720 - h2 * (1.f / 40320 -
                                                                            h2 * (1.f / 3628800 - h2 * (1.f / 479001600))))));
  s = 2.f * sh * ch;
  c = ch * ch - sh * sh;
}

// sin(pi x), any x >= 0.
MT_INLINE float sinPi(float x) {
  x -= 2.f * static_cast<float>(static_cast<int>(x * 0.5f));  // 0 .. 2
  float s, c;
  if (x <= 1.f) {
    sinCos(kPi * x, s, c);
    return s;
  }
  sinCos(kPi * (x - 1.f), s, c);
  return -s;
}

// atan2(y, x): reduced to |t| <= tan(pi / 8), series to t^15 (< 2e-8).
float atan2F(float y, float x) {
  const float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
  if (ax == 0.f && ay == 0.f) return 0.f;
  const bool swap = ay > ax;
  float t = swap ? ax * fastRcp(ay) : ay * fastRcp(ax), off = 0.f;
  if (t > 0.41421356f) {
    t = (t - 1.f) * fastRcp(t + 1.f);
    off = 0.78539816f;
  }
  const float t2 = t * t;
  float a = off + t * (1.f - t2 * (1.f / 3 - t2 * (1.f / 5 - t2 * (1.f / 7 - t2 * (1.f / 9 - t2 * (1.f / 11 -
                                                                                                 t2 * (1.f / 13 - t2 * (1.f / 15))))))));
  if (swap) a = 1.57079633f - a;
  if (x < 0) a = kPi - a;
  return y < 0 ? -a : a;
}

// Once per block: a filter state under 1e-20 (-400 dB) is 0 (no denormal tail).
MT_INLINE float flushTiny(float x) { return x < 1e-20f && x > -1e-20f ? 0.f : x; }

// T60 in seconds from DAMP: 0.05 s x 200^d.
MT_INLINE float t60Of(float d) { return 0.05f * fastExp2(7.64385619f * d); }

}  // namespace

void StringExciter::trigger(float level, float e, float b, float hz, int p) {
  e = e < 0 ? 0 : e > 1 ? 1 : e;
  b = b < 0 ? 0 : b > 1 ? 1 : b;
  noise_ = e > 0;
  len_ = burstLen(e);
  p_ = p > 0 ? static_cast<uint32_t>(p) : 0;
  end_ = len_ + p_;
  pos_ = 0;
  // Fast decay: -30 dB over the burst (log2 0.0316 = -4.98).
  k_ = noise_ ? fastExp2(-4.98f / static_cast<float>(len_)) : 1;
  env1_ = env2_ = level;
  s2_ = s1_;  // the twin replays the main stream from here, P samples later
  a_ = lpCoef(b, hz);
}

uint32_t StringExciter::burstLen(float e) {
  if (!(e > 0)) return 1;
  e = e > 1 ? 1 : e;
  const uint32_t n = static_cast<uint32_t>((0.5f + 20.f * e * e) * 0.001f * kSynthRate + 0.5f);
  return n < 1 ? 1 : n;
}

float StringExciter::lpCoef(float b, float hz) {
  b = b < 0 ? 0 : b > 1 ? 1 : b;
  float fc = hz * fastExp2(1 + 6 * b);
  if (fc > 0.45f * kSynthRate) fc = 0.45f * kSynthRate;
  if (!(fc >= 1.f)) fc = 1.f;  // also NaN
  return 1 - fastExp(-6.2831853f * fc / kSynthRate);
}

void StringVoice::trigger(float level, bool keep) {
  level_ = level;
  keep_ = pending_ ? (keep_ && keep) : keep;
  pending_ = true;
  done_ = false;
  quiet_ = 0;
}

MT_HOT void StringVoice::scaleState(float f) {
  exc_.scale(f);
  if (static_cast<StringModel>(model_) == StringModel::String) {
    Ks& k = s_.ks;
    k.lp *= f, k.hpX *= f, k.hpY *= f, k.apX *= f, k.apY *= f, k.qe *= f, k.acc *= f, k.y0 *= f, k.y1 *= f;
    rescale_ *= f;  // the line: by the next render()
  } else {
    for (int i = 0; i < kModes; ++i) s_.md.m[i].ic1 *= f, s_.md.m[i].ic2 *= f;
  }
}

void StringVoice::reset(uint8_t model, bool coefs) {
  const bool ks = static_cast<StringModel>(model) == StringModel::String;
  if (coefs) {
    // The same model at unchanged inputs: the coefficients (all computed from them, the KS delay snapped
    // to its target) stay; only the state starts from silence, as a new voice's after its first control().
    if (ks) {
      Ks& k = s_.ks;
      k.lp = k.hpX = k.hpY = k.apX = k.apY = k.qe = k.acc = k.y0 = k.y1 = 0.f;
      k.d = k.dTarget, k.dStep = 0.f;
      k.w = 0, k.ph = 0;
    } else {
      for (int i = 0; i < kModes; ++i) s_.md.m[i].ic1 = s_.md.m[i].ic2 = 0.f;
    }
  } else {
    memset(&s_, 0, sizeof(s_));
    if (ks) {
      s_.ks.r = 1;
      s_.ks.rs = s_.ks.invR = 1.f;
      s_.ks.g = 0.f;
    }
    key_[0] = -1;
  }
  model_ = model;
  if (ks) clear_ = true;
  rescale_ = lg_ = 1.f;
  winPeak_ = 0, winN_ = 0;
}

void StringVoice::control(const StringParams& in, int n) {
  StringParams p = in;
  p.damp = clamp01(p.damp), p.bright = clamp01(p.bright), p.structure = clamp01(p.structure);
  p.pos = clamp01(p.pos), p.exc = clamp01(p.exc);
  p.hz = fclamp(p.hz, kMinHz, kMaxHz);
  if (pending_) {
    p.model = p.model < static_cast<uint8_t>(StringModel::Count) ? p.model : 0;
    // The loop always runs at full scale (its int16 floor, and so the decay, do not depend on the
    // velocity); vel_ scales the output. A softer re-strike enters at level / vel_; a louder one first
    // scales the running resonance down by vel_ / level.
    float strike = 1.f;
    if (!keep_ || p.model != model_) {
      // A strike from silence on the same model at unchanged inputs (a re-struck note) keeps the
      // coefficients: no recompute at the note-on (controlKs / controlModal).
      const bool ksIn = static_cast<StringModel>(p.model) == StringModel::String;
      const bool coefs = p.model == model_ && key_[0] >= 0 && key_[1] >= 0 && key_[0] == p.hz &&
                         key_[1] == p.damp && key_[2] == p.bright && key_[3] == p.structure &&
                         (ksIn || key_[4] == p.pos);
      reset(p.model, coefs);
      vel_ = level_;
    } else {
      float f = fastRcp(lg_);  // the tail's gain-up (renderKs) back to 1 first
      lg_ = 1.f;
      if (level_ <= vel_) {
        strike = vel_ > 0.f ? level_ * fastRcp(vel_) : 0.f;
      } else {
        f *= vel_ * fastRcp(level_);
        vel_ = level_;
      }
      if (f != 1.f) scaleState(f);
      winPeak_ = 0, winN_ = 0;
    }
    // Pluck position: the comb delay min(beta, 1 - beta) x the period, beta 0.02 .. 0.98 (0.5 at 0.5).
    const float beta = 0.02f + 0.96f * p.pos;
    const float side = beta < 0.5f ? beta : 1.f - beta;
    // Strike gain: 1 / sqrt(the exciter low-pass's coefficient) keeps the excitation's energy across
    // BRIGHT; (period / 100)^(1/4) keeps low notes (energy spread over a longer period) about as loud.
    // A noise burst of len samples starts at kBurst / sqrt(len) of the click (its energy about a
    // click's), at least kBurstMin (long bursts stay audible).
    const float per = kSynthRate * (1.f / 100) * fastRcp(p.hz);
    const uint32_t len = StringExciter::burstLen(p.exc);
    const float burst = len > 1 ? maxf(minf(kBurst * rsqrtF(static_cast<float>(len)), 1.f), kBurstMin) : 1.f;
    const float gain = kExcGain * burst * rsqrtF(StringExciter::lpCoef(p.bright, p.hz)) * rsqrtF(rsqrtF(per));
    exc_.trigger(strike * gain, p.exc, p.bright, p.hz,
                 model_ == static_cast<uint8_t>(StringModel::String)
                     ? static_cast<int>(side * kSynthRate * fastRcp(p.hz) + 0.5f)
                     : 0);
    pending_ = false;
    done_ = false;
  }
  if (model_ == 0xFF) return;  // never triggered
  const bool ks = static_cast<StringModel>(model_) == StringModel::String;
  // POS only moves the strike (latched above) for STRING: not a coefficient input there.
  const bool same = key_[0] == p.hz && key_[1] == p.damp && key_[2] == p.bright && key_[3] == p.structure &&
                    (ks || key_[4] == p.pos);
  if (ks) {
    if (same)
      s_.ks.dStep = 0;
    else
      controlKs(p, n);
    // done() needs a quiet period: a narrow strike circulates as a pulse once per period, the blocks
    // between its passes (and before the first wrap) read near-silence from the line.
    span_ = static_cast<uint16_t>(s_.ks.dTarget * static_cast<float>(s_.ks.r)) + 2;
  } else {
    // MODAL: a period too (a slow mode's zero crossing is not the end).
    span_ = static_cast<uint16_t>(minf(kSynthRate * fastRcp(p.hz), 2042.f)) + 2;
    if (!same) controlModal(p);
  }
  if (!same) key_[0] = p.hz, key_[1] = p.damp, key_[2] = p.bright, key_[3] = p.structure, key_[4] = p.pos;
}

// Loop at rate sr / r: read (linear, delay d), damping low-pass, DC blocker, dispersion all-pass, gain.
// d = period - the filters' phase delay at the fundamental (exact, from their responses), so the
// fundamental is in tune for any BRIGHT / STRUCT; the gain makes the fundamental's T60 the DAMP one.
void StringVoice::controlKs(const StringParams& p, int n) {
  Ks& k = s_.ks;
  const float hz = p.hz;
  const uint8_t r = hz * kMaxDelay >= kSynthRate ? 1 : hz * (2 * kMaxDelay) >= kSynthRate ? 2 : 4;
  bool snap = key_[0] < 0;
  // A glide across 86 / 43 Hz changes the rate divider: the line's content (written at the old rate) is
  // read at the new one from here, and the delay snaps instead of ramping (a ramp between delays in
  // different units would sweep the pitch). The ringing note jumps by the period's rounding, inaudible
  // under the glide; the phase and the input accumulator restart.
  if (r != k.r) {
    k.r = r, k.ph = 0, k.acc = 0;
    k.rs = r == 1 ? 1.f : r == 2 ? 0.70710678f : 0.5f;
    k.invR = r == 1 ? 1.f : r == 2 ? 0.5f : 0.25f;
    snap = true;
  }
  const float srL = static_cast<float>(kSynthRate) * k.invR;
  const float period = srL * fastRcp(hz);
  const float w0 = 2.f * kPi * hz * fastRcp(srL);
  float sw, cw;
  sinCos(w0, sw, cw);
  // Damping low-pass at hz x 2^(3 + 4 b) (8 .. 128 x hz), at most 0.45 x the loop rate.
  float fc = hz * fastExp2(3.f + 4.f * p.bright);
  fc = minf(fc, 0.45f * srL);
  k.a = 1.f - fastExp(-2.f * kPi * fc * fastRcp(srL));
  const float pl = 1.f - k.a;
  const float lpRe = 1.f - pl * cw, lpIm = pl * sw;  // denominator 1 - p e^-jw
  const float lpMag2 = k.a * k.a * fastRcp(lpRe * lpRe + lpIm * lpIm);
  const float lpArg = -atan2F(lpIm, lpRe);
  // DC blocker at hz x (hz / fc) / 4 (<= hz / 32): the loop's gain still falls with every partial above
  // the first, and its phase lead (compensated at the fundamental) detunes the partials by < 5 cents.
  const float fh = 0.25f * hz * hz * fastRcp(fc);
  // (1 + R) / 2 (1 - z^-1) / (1 - R z^-1): gain 1 at Nyquist, just under it at the fundamental.
  k.R = fastExp(-2.f * kPi * fh * fastRcp(srL));
  k.kh = 0.5f * (1.f + k.R);
  const float nRe = 1.f - cw, nIm = sw, hdRe = 1.f - k.R * cw, hdIm = k.R * sw;
  const float hpMag2 = k.kh * k.kh * (nRe * nRe + nIm * nIm) * fastRcp(hdRe * hdRe + hdIm * hdIm);
  const float hpArg = atan2F(nIm, nRe) - atan2F(hdIm, hdRe);
  // Dispersion: a first-order all-pass with a DC delay of 1 + 0.12 s x the period (s 0: a unit delay).
  float tau = 1.f + kDispersion * p.structure * period;
  tau = minf(tau, maxf(1.f, 0.5f * period));
  k.c = (1.f - tau) * fastRcp(1.f + tau);
  const float apArg = atan2F(-sw, k.c + cw) - atan2F(-k.c * sw, 1.f + k.c * cw);
  const float invW = fastRcp(w0);
  float d = period + (lpArg + hpArg + apArg) * invW;
  d = fclamp(d, 1.f, static_cast<float>(kMaxDelay));
  // Linear interpolation's phase delay is not exactly its fraction: one correction step.
  {
    const float f = d - static_cast<float>(static_cast<int>(d));
    const float idel = -atan2F(-f * sw, 1.f - f + f * cw) * invW;
    d = fclamp(d - (idel - f), 1.f, static_cast<float>(kMaxDelay));
  }
  const float f = d - static_cast<float>(static_cast<int>(d));
  const float inMag2 = (1.f - f) * (1.f - f) + f * f + 2.f * f * (1.f - f) * cw;
  // Per pass (one period): 10^(-3 / (hz T60)), over the filters' loss at the fundamental. Partials above
  // lose more per pass (the low-pass and the interpolation fall, the DC blocker rises less: its cutoff is
  // <= hz / 32); the quasi-DC loop mode is cut to <= 0.87 by the blocker, so a gain up to kMaxLoop keeps
  // every mode decaying. It binds only near the top (a loop of a few samples).
  const float g = fastExp2(-9.96578428f * fastRcp(minf(hz * t60Of(p.damp), kMaxPeriods)));
  k.g = minf(g * rsqrtF(lpMag2 * hpMag2 * inMag2), kMaxLoop);
  k.dTarget = d;
  if (snap || n <= 0) {
    k.d = d, k.dStep = 0;
  } else {
    k.dStep = (d - k.d) * (n == 32 ? 1.f / 32 : fastRcp(static_cast<float>(n)));
  }
}

// Mode k (1..kModes = 6) at hz x k x stretch_k: stretch_1 = 1, each next one adds 0.32 s, shrinking by 0.8 per
// mode (s 0 harmonic, 0.5 about a bar's 1 : 2.3 : 3.9, 1 a plate / bell's 1 : 2.6 : 4.6); modes above
// 0.45 x the rate are dropped. Q from the T60 of DAMP for mode 1, x (0.6 + 0.38 b) per mode above (dark:
// the high modes die first). Amplitude sin(pi k beta) of the position (0.5: the even modes vanish), the
// input scaled by 1 / g_k so every mode sounds at the same level for an impulse (~ sqrt(hz) overall).
void StringVoice::controlModal(const StringParams& p) {
  Modal& md = s_.md;
  const float hz = p.hz;
  const float loss = 0.6f + 0.38f * p.bright;
  const float beta = 0.02f + 0.96f * p.pos;
  const float g1 = fastTan(kPi * hz * (1.f / kSynthRate));
  const float gNorm = kModalGain * g1 * rsqrtF(g1 * (1.f / kModalRefG));  // ~ sqrt(hz): level ~ STRING's
  float t60 = t60Of(p.damp), st = 0.32f * p.structure, stretch = 1.f;
  int n = 0;
  for (int k = 1; k <= kModes; ++k) {
    const float f = hz * static_cast<float>(k) * stretch;
    if (f > 0.45f * kSynthRate) break;
    Mode& m = md.m[n++];
    const float g = fastTan(kPi * f * (1.f / kSynthRate));
    // 1 / Q from the per-sample pole radius rho = 10^(-3 / (rate T60)): the bilinear SVF's poles have
    // rho^2 = (1 + g^2 - g k) / (1 + g^2 + g k), so k = (1 + g^2) / g x (1 - rho^2) / (1 + rho^2) (exact at
    // any frequency: the warp does not stretch the high modes' decay).
    const float rho2 = fastExp2(-19.9315686f * fastRcp(static_cast<float>(kSynthRate) * t60));
    const float kq = minf((1.f + g * g) * fastRcp(g) * (1.f - rho2) * fastRcp(1.f + rho2), 2.f);  // 1 / Q
    m.a1 = fastRcp(1.f + g * (g + kq));
    m.a2 = g * m.a1;
    m.a3 = g * m.a2;
    m.gin = sinPi(static_cast<float>(k) * beta) * gNorm * fastRcp(g);
    stretch += st;
    st *= 0.8f;
    t60 *= loss;
  }
  for (int k = n; k < kModes; ++k) md.m[k].ic1 = md.m[k].ic2 = 0;  // dropped: silent if they return
  md.n = n;
}

MT_HOT void StringVoice::render(float* out, int n, float amp, int16_t* line) {
  if (model_ == 0xFF) return;
  const bool burst = exc_.active();  // this block still strikes
  float peak;
  if (static_cast<StringModel>(model_) == StringModel::String) {
    if (clear_) {
      memset(line, 0, sizeof(int16_t) * kLineLen);
      clear_ = false;
    } else if (rescale_ != 1.f) {
      for (int i = 0; i < kLineLen; ++i)
        line[i] = static_cast<int16_t>(fclamp(static_cast<float>(line[i]) * rescale_, -32767.f, 32767.f));
    }
    rescale_ = 1.f;
    const float vs = vel_ * fastRcp(lg_);  // the line holds the tail lg_ x up
    const float lp = renderKs(out, n, amp * kOut * vs, line);
    peak = lp * vs;
    // Block floating point for the tail: once a whole period peaks under kTail, the loop's content
    // (line and states) doubles and the output gain halves. The int16 rounding floor stays ~60 dB
    // under the signal, so the decay follows T60 down to done() at any velocity, and no rounding limit
    // cycle forms (it needs a signal within a few LSB).
    winPeak_ = maxf(winPeak_, lp);
    winN_ = winN_ > 0xFFFF - n ? 0xFFFF : static_cast<uint16_t>(winN_ + n);
    if (winN_ >= span_) {
      if (!burst && !pending_ && winPeak_ < kTail && lg_ < kMaxGainUp) {
        scaleState(2.f);
        lg_ *= 2.f;
      }
      winPeak_ = 0, winN_ = 0;
    }
  } else {
    peak = renderModal(out, n, amp * kOut * vel_) * vel_;
  }
  // Silent: no strike in this block or pending and the peak (with the velocity) under -80 dB for at least
  // one period.
  if (pending_ || burst || !(peak * kOut < kDonePeak))
    quiet_ = 0;
  else
    quiet_ = quiet_ > 0xFFFF - n ? 0xFFFF : static_cast<uint16_t>(quiet_ + n);
  done_ = quiet_ >= span_;
}

MT_HOT float StringVoice::renderKs(float* __restrict out, int n, float g, int16_t* __restrict line) {
  Ks& k = s_.ks;
  float d = k.d, lp = k.lp, hpX = k.hpX, hpY = k.hpY, apX = k.apX, apY = k.apY, qe = k.qe;
  const float a = k.a, R = k.R, kh = k.kh, c = k.c, gl = k.g, dStep = k.dStep;
  int w = k.w;
  // One loop step: g x allpass(dcblock(lowpass(read(d)))) + x, saturated into the line. The int16
  // quantization rounds and feeds its error into the next sample (qe): unbiased and uncorrelated with
  // the sign, so the tail decays at the loop's rate (truncation damps it faster). Below kQuiet LSB it
  // truncates toward zero without the feedback: no limit cycle survives (render() scales the tail up, so
  // this is reached only past kMaxGainUp).
  auto step = [&](float x) __attribute__((always_inline)) {
    const int di = static_cast<int>(d);
    const float fr = d - static_cast<float>(di);
    const int i0 = (w - di) & (kLineLen - 1);
    const float r0 = static_cast<float>(line[i0]);
    const float rd = (r0 + (static_cast<float>(line[(i0 - 1) & (kLineLen - 1)]) - r0) * fr) * (1.f / kLineScale);
    lp += a * (rd - lp);
    hpY = R * hpY + kh * (lp - hpX);
    hpX = lp;
    const float ap = c * hpY + apX - c * apY;
    apX = hpY;
    apY = ap;
    const float s = fclamp(gl * ap + x, -kLineMax, kLineMax);
    const float qv = s * kLineScale + qe;
    const bool big = qv > kQuiet || qv < -kQuiet;
    const int q = big ? static_cast<int>(qv + 32768.5f) - 32768 : static_cast<int>(qv);  // round / truncate
    qe = big ? qv - static_cast<float>(q) : 0.f;
    line[w] = static_cast<int16_t>(q);
    w = (w + 1) & (kLineLen - 1);
    return s;
  };
  float peak = 0;
  // The excitation over chunks of up to 32 samples (StringExciter::fill: its state in registers), zeros
  // once the strike has ended.
  const bool ex = exc_.active();
  float xb[32];
  if (!ex)
    for (float& v : xb) v = 0.f;
  const int r = k.r;
  const float invR = k.invR, rs = k.rs;
  float acc = k.acc, y0 = k.y0, y1 = k.y1;
  int ph = k.ph;
  for (int at = 0; at < n; at += 32) {
    const int len = n - at < 32 ? n - at : 32;
    if (ex) exc_.fill(xb, len);
    float* __restrict o = out + at;
    if (r == 1) {
      for (int i = 0; i < len; ++i) {
        const float s = step(xb[i]);
        o[i] += s * g;
        peak = maxf(peak, s > 0 ? s : -s);
        d += dStep;
      }
    } else {
      for (int i = 0; i < len; ++i) {
        acc += xb[i];
        if (++ph == r) {
          ph = 0;
          const float s = step(acc * rs);
          acc = 0;
          y0 = y1, y1 = s;
          peak = maxf(peak, s > 0 ? s : -s);
          d += dStep * static_cast<float>(r);
        }
        o[i] += (y0 + (y1 - y0) * static_cast<float>(ph) * invR) * g;
      }
    }
  }
  if (r != 1) k.acc = acc, k.y0 = y0, k.y1 = y1, k.ph = static_cast<uint8_t>(ph);
  k.d = k.dTarget;  // the ramp ends on the target (unchanged inputs keep it exactly)
  k.lp = flushTiny(lp), k.hpX = flushTiny(hpX), k.hpY = flushTiny(hpY), k.apX = flushTiny(apX);
  k.apY = flushTiny(apY), k.qe = qe;
  k.w = static_cast<uint16_t>(w);
  return peak;
}

// Mode by mode over chunks of up to 32 samples (the states stay in registers), summed, saturated.
MT_HOT float StringVoice::renderModal(float* __restrict out, int n, float g) {
  Modal& md = s_.md;
  float x[32], acc[32], peak = 0;
  for (int at = 0; at < n; at += 32) {
    const int len = n - at < 32 ? n - at : 32;
    const bool ex = exc_.active();
    for (int i = 0; i < len; ++i) acc[i] = 0;
    if (ex) exc_.fill(x, len);  // the state in registers (StringExciter::fill)
    for (int k = 0; k < md.n; ++k) {
      Mode& m = md.m[k];
      float ic1 = m.ic1, ic2 = m.ic2;
      const float a1 = m.a1, a2 = m.a2, a3 = m.a3, gin = m.gin;
      if (ex) {
        for (int i = 0; i < len; ++i) {
          const float v3 = x[i] * gin - ic2;
          const float v1 = a1 * ic1 + a2 * v3;
          const float v2 = ic2 + a2 * ic1 + a3 * v3;
          ic1 = 2.f * v1 - ic1;
          ic2 = 2.f * v2 - ic2;
          acc[i] += v1;
        }
      } else {
        for (int i = 0; i < len; ++i) {
          const float v1 = a1 * ic1 - a2 * ic2;
          const float v2 = ic2 + a2 * ic1 - a3 * ic2;
          ic1 = 2.f * v1 - ic1;
          ic2 = 2.f * v2 - ic2;
          acc[i] += v1;
        }
      }
      m.ic1 = ic1, m.ic2 = ic2;
    }
    for (int i = 0; i < len; ++i) {
      const float y = fclamp(acc[i], -kLineMax, kLineMax);
      out[at + i] += y * g;
      peak = maxf(peak, y > 0 ? y : -y);
    }
  }
  for (int k = 0; k < md.n; ++k) md.m[k].ic1 = flushTiny(md.m[k].ic1), md.m[k].ic2 = flushTiny(md.m[k].ic2);
  return peak;
}

}  // namespace mt
