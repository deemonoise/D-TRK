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
#pragma once
#include <stdint.h>
#include "hot.h"
#include "model.h"

namespace mt {

// Note-on excitation of a STRING voice: a single impulse (e 0) or a noise burst of 0.5 + 20 e^2 ms
// with a fast decay, comb-filtered by the pluck position (x[n] - x[n - P]) and low-passed at
// hz x 2^(1 + 6 b). No history buffer: a twin noise generator, started P samples after the main one,
// regenerates x[n - P] exactly. A re-trigger restarts the burst and keeps the low-pass state.
class StringExciter {
 public:
  // level: velocity gain; e EXC, b BRIGHT 0..1; hz the note; p the comb delay in samples (0 = none).
  void trigger(float level, float e, float b, float hz, int p);
  // Per-exciter noise seed (e.g. the voice index): another stream; the state never ends up 0.
  void seed(uint32_t s) {
    s1_ = 0x9E3779B9u ^ (s * 0x85EBCA6Bu);
    if (!s1_) s1_ = 1;
  }
  // The excitation before the low-pass (main - delayed); advances one sample.
  MT_INLINE float raw() {
    float xm = 0, xd = 0;
    if (pos_ < end_) {
      if (pos_ < len_) {
        xm = noise_ ? white(s1_) * env1_ : env1_;
        env1_ *= k_;
      }
      if (p_ && pos_ >= p_ && pos_ - p_ < len_) {
        xd = noise_ ? white(s2_) * env2_ : env2_;
        env2_ *= k_;
      }
      ++pos_;
    }
    return xm - xd;
  }
  // Scales the low-pass state (the voice's resonance is rescaled); a finished exciter's residue is
  // cleared instead (scaling it up could re-activate it).
  void scale(float f) { y_ = active() ? y_ * f : 0.f; }
  // The low-passed excitation; advances one sample.
  MT_INLINE float next() {
    y_ += a_ * (raw() - y_);
    return y_;
  }
  // n samples of next() into x, bit for bit, at a lower cost: the state stays in registers and the burst's
  // phases (main, main + delayed, delayed, tail) run as branch-free segments.
  MT_INLINE void fill(float* __restrict x, int n) {
    uint32_t pos = pos_, s1 = s1_, s2 = s2_;
    float e1 = env1_, e2 = env2_, y = y_;
    const float a = a_, k = k_;
    const uint32_t len = len_, p = p_, end = end_;
    const bool noise = noise_;
    int i = 0;
    while (i < n) {
      if (pos >= end) {  // the low-pass tail (raw() is 0 - 0)
        for (; i < n; ++i) {
          y += a * (0.f - y);
          x[i] = y;
        }
        break;
      }
      // The segment up to the next phase change: the main burst's end, the delayed one's start or end.
      const bool m = pos < len, d = p && pos >= p;
      uint32_t stop = end;
      if (m && len < stop) stop = len;
      if (p && pos < p && p < stop) stop = p;
      int cnt = static_cast<int>(stop - pos);
      if (cnt > n - i) cnt = n - i;
      pos += static_cast<uint32_t>(cnt);
      for (const int to = i + cnt; i < to; ++i) {
        float xm = 0, xd = 0;
        if (m) {
          xm = noise ? white(s1) * e1 : e1;
          e1 *= k;
        }
        if (d) {
          xd = noise ? white(s2) * e2 : e2;
          e2 *= k;
        }
        y += a * ((xm - xd) - y);
        x[i] = y;
      }
    }
    pos_ = pos, s1_ = s1, s2_ = s2, env1_ = e1, env2_ = e2, y_ = y;
  }
  // The low-pass coefficient trigger() uses for BRIGHT b at hz (1 - e^(-2 pi fc / rate)).
  static float lpCoef(float b, float hz);
  // The burst length in samples for EXC e (1 for a click).
  static uint32_t burstLen(float e);
  // The burst (with its comb) is still running or the low-pass has not decayed (< 1e-6).
  bool active() const { return pos_ < end_ || y_ > 1e-6f || y_ < -1e-6f; }

 private:
  // xorshift32 step -> -1..1.
  static MT_INLINE float white(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return static_cast<float>(static_cast<int32_t>(s)) * (1.0f / 2147483648.0f);
  }
  uint32_t s1_ = 0x9E3779B9u, s2_ = 0;  // main / delayed noise state
  uint32_t pos_ = 0, len_ = 0, p_ = 0, end_ = 0;  // samples since trigger, burst length, comb delay, len + p
  float env1_ = 0, env2_ = 0, k_ = 1;  // burst envelopes (start at level), per-sample decay
  float a_ = 1, y_ = 0;                // one-pole low-pass
  bool noise_ = false;
};

// Control-rate inputs of a STRING voice (Synth::controlString); macros 0..1.
struct StringParams {
  uint8_t model = 0;      // StringModel, latched at the trigger
  float hz = 220;
  float damp = 0.63f;     // DAMP: T60 = 0.05 s x 200^damp (0.05 .. 10 s)
  float bright = 0.5f;    // BRIGHT: excitation low-pass, string damping / mode Q loss
  float structure = 0;    // STRUCT: 0 harmonic .. 1 stiff (string dispersion, mode stretch)
  float pos = 0.24f;      // POS: excitation position, 0.5 = the middle (even partials null)
  float exc = 0.08f;      // EXC: 0 click, else a noise burst of 0.5 + 20 e^2 ms
};

// One STRING voice: the exciter into a Karplus-Strong string (StringModel::String, on the voice's
// delay line) or 6 tuned band-pass modes (StringModel::Modal). Trivially copyable (lives in the
// voice-engine union). A full-velocity strike peaks about 1, |out| < 1.9; render() adds into out.
class StringVoice {
 public:
  // MODAL's modes: 6, the design's CPU cut-off (8 measured 2.6 %/voice on the Teensy, over the 2.4 % gate).
  static constexpr int kModes = 6;
  // Noise seed of the exciter (the voice index: chords get uncorrelated bursts).
  void seed(uint32_t s) { exc_.seed(s); }
  // A strike of `level` (velocity), fired by the next control() with its parameters. keep: add it into
  // the running resonance (a re-trigger of a sounding voice); else, or when the model changed, the
  // state starts from silence (the delay line is cleared by the next render()). The resonator runs at
  // full scale whatever the velocity (the decay does not depend on it); the velocity scales its output.
  void trigger(float level, bool keep);
  // Next control period of n samples. Coefficients are recomputed only when hz / damp / bright /
  // structure / pos moved.
  void control(const StringParams& p, int n);
  // line: the voice's delay line (kLineLen int16, STRING only).
  void render(float* out, int n, float amp, int16_t* line);
  // Silent: no strike in the last renders (none pending) and the peak stayed below 1e-4 (-80 dB) for at
  // least one period (a pulse circulating in the loop, or the line before its first wrap, leaves blocks
  // near-silent): the Synth may free the voice (a held note too).
  bool done() const { return done_; }
  // Tests: the next control() recomputes every coefficient (as after an input change: a STRING delay
  // still ramps).
  void invalidate() { key_[1] = -1; }

 private:
  // Karplus-Strong string. The loop runs at the rate / r (r 1, 2, 4: periods up to 510 line samples).
  struct Ks {
    float d, dStep, dTarget;  // read delay in loop samples, its per-sample ramp, the control's target
    float lp, a;              // damping one-pole low-pass
    float hpX, hpY, R, kh;    // DC blocker (cutoff far below the fundamental), its gain
    float apX, apY, c;        // dispersion first-order all-pass
    float g;                  // loop gain per pass (T60 of the fundamental)
    float qe;                 // quantization error carried to the next line write
    float acc, y0, y1;        // r > 1: input sum, previous / last loop output
    float rs, invR;           // input scale 1 / sqrt(r), 1 / r
    uint16_t w;               // write index
    uint8_t r, ph;            // rate divider, sample phase in it
  };
  struct Mode {
    float ic1, ic2;           // TPT state-variable filter (band-pass output)
    float a1, a2, a3;
    float gin;                // input gain: position amplitude, level normalization
  };
  struct Modal {
    Mode m[kModes];
    int n;                    // modes below 0.45 x the rate
  };
  // The state of `model` from silence (the line cleared by the next render()). coefs: the model is the
  // current one and the inputs are the coefficients' (key_): they stay, else all is recomputed.
  void reset(uint8_t model, bool coefs);
  // Scales the running resonance (a louder re-strike).
  void scaleState(float f);
  void controlKs(const StringParams& p, int n);
  void controlModal(const StringParams& p);
  // Both return the block's peak before the output gain.
  float renderKs(float* out, int n, float g, int16_t* line);
  float renderModal(float* out, int n, float g);

  StringExciter exc_;
  float key_[5] = {-1, 0, 0, 0, 0};  // hz, damp (< 0: stale), bright, structure, pos of the coefficients
  float level_ = 0;                   // the pending strike's velocity
  float vel_ = 0;                     // output scale: the velocity of the loop's content
  float rescale_ = 1;                 // render() scales the line by this first (a re-strike, the tail)
  float lg_ = 1;                      // STRING: the tail's gain-up in the loop (a power of 2)
  float winPeak_ = 0;                 // STRING: the loop's peak over the current period window
  uint16_t winN_ = 0;                 // .. its samples so far
  uint16_t span_ = 1;                 // output samples of one period (STRING and MODAL)
  uint16_t quiet_ = 0;                // output samples since the peak was last above -80 dB
  uint8_t model_ = 0xFF;              // the model the state belongs to (0xFF: none yet)
  bool pending_ = false, keep_ = false;  // a trigger waiting for control()
  bool clear_ = false;                // render() clears the line first
  bool done_ = false;
  union {
    Ks ks;
    Modal md;
  } s_;
};

}  // namespace mt
