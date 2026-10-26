// DRUM machines BDA, SDA, BDS, SDS, CPX, RSX, HHR (Drum2Voice). This project is GPL-3.0; parts of
// this file are float re-implementations at 44.1 kHz, on this project's structures, of the drum voices
// collected in the ctag-tbd DrumRack (github.com/ctag-fh-kiel/ctag-tbd):
//
// - BDA, SDA, BDS, SDS, HHR, the state-variable filter, the soft clip and the tangent approximations:
//   after the 2016 drum models and DSP helpers of github.com/pichenettes/eurorack and stmlib, MIT License:
//
//   Copyright 2016 Emilie Gillet.
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in
//   all copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//   THE SOFTWARE.
//
// - CPX, RSX: after ctag-tbd synthesis/Clap and synthesis/Rimshot, (c) 2024 Robert Manzke, GPL-3.0
//   (https://www.gnu.org/licenses/gpl-3.0.txt); the clap's 16 patterns of delays and amplitudes are
//   taken from it. RSX derives from the MIT snare above (notice above).
//
// Differences from the sources: envelopes run per sample; the rim shot renders exactly n samples; noise is reseeded per hit (repeatable hits); DC
// blockers after the synthetic kick / snare; knob ranges fitted to five 0..1 macros.
#include "synth_drum2.h"
#include <math.h>
#include <string.h>
#include "fastmath.h"
#include "synth_osc.h"

namespace mt {
namespace {

constexpr float kFs = static_cast<float>(kSynthRate);
constexpr float kPi = 3.14159265f;
constexpr float kOff = 1e-4f;                // -80 dB: done
constexpr float kChokeK = 0.92466f;          // per sample: -60 dB in 2 ms
constexpr float kPeakK = 0.99968569f;        // peak follower: -6 dB in 50 ms
constexpr float kLog1e4 = 9.2103404f;        // -ln(1e-4): an AD envelope's -80 dB

MT_INLINE float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
MT_INLINE float semi(float x) { return fastExp2(x * (1.f / 12.f)); }  // 2^(x / 12)
// tan(pi f), f = hz / rate: below ~8 kHz, and a wider fit (16 Hz .. 16 kHz).
MT_INLINE float tanDirty(float f) { return f * (kPi + 3.736e-01f * kPi * kPi * kPi * f * f); }
MT_INLINE float tanFast(float f) {
  const float f2 = f * f;
  return f * (kPi + f2 * (3.260e-01f * kPi * kPi * kPi + 1.823e-01f * kPi * kPi * kPi * kPi * kPi * f2));
}
MT_INLINE float onePole(float& y, float x, float g) { return y += g * (x - y); }
// Per-sample multiplier: -80 dB after s seconds.
MT_INLINE float adK(float s) { return fastExp(-kLog1e4 * fastRcp(s * kFs)); }

using Svf = Drum2Voice::Svf;
using OnePole = Drum2Voice::OnePole;

MT_INLINE void svfSet(Svf& s, float g, float q) {
  s.g = g;
  s.r = fastRcp(q);
  s.h = fastRcp(1.f + s.r * g + g * g);
}
MT_INLINE void svfRun(Svf& s, float in, float& hp, float& bp, float& lp) {
  hp = (in - s.r * s.s1 - s.g * s.s1 - s.s2) * s.h;
  bp = s.g * hp + s.s1;
  s.s1 = s.g * hp + bp;
  lp = s.g * bp + s.s2;
  s.s2 = s.g * bp + lp;
}
MT_INLINE float svfBp(Svf& s, float in) {
  float h, b, l;
  svfRun(s, in, h, b, l);
  return b;
}
MT_INLINE float svfLp(Svf& s, float in) {
  float h, b, l;
  svfRun(s, in, h, b, l);
  return l;
}
MT_INLINE float svfHp(Svf& s, float in) {
  float h, b, l;
  svfRun(s, in, h, b, l);
  return h;
}
MT_INLINE void opSet(OnePole& o, float g) {
  o.g = g;
  o.gi = fastRcp(1.f + g);
}
MT_INLINE float opLp(OnePole& o, float in) {
  const float lp = (o.g * in + o.s) * o.gi;
  o.s = o.g * (in - lp) + lp;
  return lp;
}
MT_INLINE float opHp(OnePole& o, float in) { return in - opLp(o, in); }

MT_INLINE float softClip(float x) {
  if (x < -3.f) return -1.f;
  if (x > 3.f) return 1.f;
  return x * (27.f + x * x) * fastRcp(27.f + 9.f * x * x);
}
MT_INLINE float wrap1(float x) {  // x - floor(x)
  const int i = static_cast<int>(x);
  const float f = x - static_cast<float>(i);
  return f < 0 ? f + 1.f : f;
}
MT_INLINE float snap(float x) { return fabsf(x) < 1e-7f ? 0.f : x; }

// polyBLEP residual at phase t (0..1) for an increment dt, rdt = 1 / dt.
MT_INLINE float blep(float t, float dt, float rdt) {
  if (t < dt) {
    t *= rdt;
    return t + t - t * t - 1.f;
  }
  if (t > 1.f - dt) {
    t = (t - 1.f) * rdt;
    return t * t + t + t + 1.f;
  }
  return 0.f;
}

// Output trims: default knobs peak around -4 dBFS, everything stays below 0 dBFS.
constexpr float kGainBda = 1.15f, kGainSda = 0.55f, kGainBds = 0.7f, kGainSds = 0.6f, kGainCpx = 0.5f, kGainRsx = 0.25f, kGainHhr = 0.85f;

// CPX: 16 patterns x (3 clap delays + the tail's), ms at scale 1, and their amplitudes (ctag-tbd Clap,
// (c) 2024 Robert Manzke, GPL-3.0). Cold: read at the trigger.
const uint8_t kClapDelay[16][4] = {
    {10, 20, 30, 40}, {20, 30, 40, 60}, {20, 40, 60, 80},  {10, 25, 40, 60}, {30, 45, 60, 90}, {10, 20, 35, 60},
    {15, 30, 50, 65}, {10, 20, 40, 80}, {10, 20, 60, 70},  {15, 30, 45, 60}, {40, 80, 90, 110}, {30, 40, 70, 75},
    {5, 30, 40, 50},  {15, 45, 55, 65}, {15, 20, 35, 40},  {10, 40, 50, 70}};
const float kClapAmp[16][4] = {
    {1.f, 1.f, 1.f, .1f},  {1.f, .5f, 1.f, .1f},  {1.f, .5f, 1.f, .1f}, {.5f, 1.f, 1.f, .1f},
    {1.f, .1f, 1.f, .1f},  {1.f, .5f, .5f, .2f},  {1.f, .2f, .2f, .5f}, {.25f, .5f, 1.f, .2f},
    {.25f, 1.f, .25f, .5f}, {1.f, .5f, .25f, .5f}, {1.f, .2f, 1.f, .2f}, {.25f, .5f, .25f, 1.f},
    {.5f, 1.f, .25f, .1f}, {.5f, .1f, 1.f, .25f}, {1.f, .1f, 1.f, .2f}, {1.f, .5f, 1.f, .2f}};
float clapScale(float spread) { return 0.5f + 1.5f * clamp01(spread); }
int clapPattern(float knob) { return static_cast<int>(clamp01(knob) * 15.99f); }

}  // namespace

float drum2BaseHz(Drum2Kind k) {
  static const float kHz[] = {55, 200, 50, 180, 1, 470, 55, 1};  // Unused6 plays as BDA
  const int i = static_cast<int>(k);
  return i < static_cast<int>(Drum2Kind::Count) ? kHz[i] : 1.f;
}

float drum2ClapBurstMs(int p, float spread, int i) {
  p = p < 0 ? 0 : (p > 15 ? 15 : p);
  return i <= 0 ? 0.f : kClapDelay[p][i > 3 ? 3 : i - 1] * clapScale(spread);
}

MT_HOT float Drum2Voice::rnd() {
  rng_ = rng_ * 1664525u + 1013904223u;
  return static_cast<float>(rng_ >> 8) * (1.f / 16777216.f);
}

void Drum2Voice::trigger(Drum2Kind k, float accent, bool keepTail) {
  tailV_ = keepTail ? last_ : 0.f;
  kind_ = k < Drum2Kind::Count && k != Drum2Kind::Unused6 ? k : Drum2Kind::Bda;  // Unused6: was BDF
  accent_ = clamp01(accent);
  fresh_ = pending_ = live_ = true;
  rng_ = 0x9E3779B9u;
  pk_ = 0;
  memset(&s_, 0, sizeof(s_));
  const float a = accent_;
  switch (kind_) {
    case Drum2Kind::Bda:
      s_.bda.pulseLeft = static_cast<int>(1.0e-3f * kFs);
      s_.bda.fmPulseLeft = static_cast<int>(6.0e-3f * kFs);
      s_.bda.pulseH = 3.f + 7.f * a;
      break;
    case Drum2Kind::Sda:
      s_.sda.pulseLeft = static_cast<int>(1.0e-3f * kFs);
      s_.sda.pulseH = 3.f + 7.f * a;
      s_.sda.noiseEnv = 2.f;
      break;
    case Drum2Kind::Bds:
      s_.bds.fm = 1.f;
      s_.bds.bodyEnv = s_.bds.trEnv = 0.3f + 0.7f * a;
      s_.bds.bodyPw = static_cast<int>(kFs * 0.001f);
      s_.bds.fmPw = static_cast<int>(kFs * 0.0013f);
      svfSet(s_.bds.click, tanFast(5000.f / kFs), 2.f);
      break;
    case Drum2Kind::Sds:
      s_.sds.snareAmp = s_.sds.drumAmp = 0.3f + 0.7f * a;
      s_.sds.fm = 1.f;
      break;
    case Drum2Kind::Cpx:
      s_.cpx.timer[0] = 1;  // first clap on the first sample
      break;
    case Drum2Kind::Rsx:
      s_.rsx.pulseLeft = static_cast<int>(0.25e-3f * kFs);
      s_.rsx.pulseH = 3.f + 7.f * a;
      s_.rsx.env = 1.f;
      break;
    default:
      break;
  }
}

void Drum2Voice::control(const Drum2Params& p, int span) {
  (void)span;
  if (!live_) return;
  float m[5];
  for (int i = 0; i < 5; ++i) m[i] = clamp01(p.mac[i]);
  const float hz = p.hz > 1e-3f ? p.hz : 1e-3f;
  const float a = accent_;
  const bool latch = pending_;
  pending_ = false;
  switch (kind_) {
    case Drum2Kind::Bda: {  // DECAY TONE ATTACK PUNCH
      Bda& v = s_.bda;
      v.f0 = fclamp(hz / kFs, 1e-4f, 0.4f);
      v.scale = 0.001f * fastRcp(v.f0);
      v.q = 1500.f * semi(0.75f * m[0] * 80.f);
      v.toneF = minf(4.f * v.f0 * semi(m[1] * 108.f), 1.f);
      v.leak = 0.08f * (m[1] + 0.25f);
      v.atk = m[2];
      v.self = m[3];
      v.gain = kGainBda;
      break;
    }
    case Drum2Kind::Sda: {  // DECAY TONE SNAPPY
      Sda& v = s_.sda;
      const float f0 = fclamp(hz / kFs, 1e-4f, 0.499f);
      const float decay = 0.75f * m[0];
      const float xt = decay * (1.f + decay * (decay - 1.f));
      const float q = 2000.f * semi(xt * 84.f);
      float snappy = m[2];
      v.noiseK = 1.f - 0.0017f * semi(-decay * (50.f + snappy * 10.f));
      v.leak = snappy * (2.f - snappy) * 0.1f;
      v.snappy = clamp01(snappy * 1.1f - 0.05f);
      static const float kMode[5] = {1.f, 2.f, 3.18f, 4.16f, 5.62f};
      for (int i = 0; i < 5; ++i) {
        const float f = minf(f0 * kMode[i], 0.499f);
        svfSet(v.res[i], tanFast(f), 1.f + f * (i == 0 ? q : q * 0.25f));
      }
      float tone = m[1];
      if (tone < 0.666667f) {
        tone *= 1.5f;
        v.gain[0] = 1.5f + (1.f - tone) * (1.f - tone) * 4.5f;
        v.gain[1] = 2.f * tone + 0.15f;
        v.gain[2] = v.gain[3] = v.gain[4] = 0;
      } else {
        tone = (tone - 0.666667f) * 3.f;
        v.gain[0] = 1.5f - tone * 0.5f;
        v.gain[1] = 2.15f - tone * 0.7f;
        for (int i = 2; i < 5; ++i) {
          v.gain[i] = tone;
          tone *= tone;
        }
      }
      const float fn = minf(f0 * 16.f, 0.499f);
      svfSet(v.noise, tanFast(fn), 1.f + fn * 1.5f);
      v.noiseEnv = snap(v.noiseEnv);
      break;
    }
    case Drum2Kind::Bds: {  // DECAY TONE DIRT SWEEP TIME
      Bds& v = s_.bds;
      v.f0 = fclamp(hz / kFs, 1e-4f, 0.25f);
      const float decay = 0.85f * m[0], d2 = decay * decay;
      const float fmd = 1.5f * m[4], fmd2 = fmd * fmd;
      v.dirt = m[2] * maxf(1.f - 8.f * v.f0, 0.f);
      v.fmAmt = 2.f * m[3];
      v.fmK = 1.f - fastRcp(0.008f * (1.f + fmd2 * 4.f) * kFs);
      v.bodyK = 1.f - fastRcp(0.02f * kFs) * semi(-d2 * 60.f);
      v.trK = 1.f - fastRcp(0.005f * kFs);
      v.toneF = minf(4.f * v.f0 * semi(m[1] * 108.f), 1.f);
      v.trLevel = m[1];
      v.bodyEnv = snap(v.bodyEnv), v.trEnv = snap(v.trEnv), v.fm = snap(v.fm);
      break;
    }
    case Drum2Kind::Sds: {  // DECAY TONE SNAPPY FM
      Sds& v = s_.sds;
      v.f0 = fclamp(hz / kFs, 1e-4f, 0.25f);
      const float decay = 0.7f * m[0];
      const float xt = decay * (1.f + decay * (decay - 1.f));
      v.fmAmt = m[3] * m[3];
      float snappy = m[2];
      v.drumK = 1.f - fastRcp(0.015f * kFs) * semi(-xt * 72.f - v.fmAmt * 12.f + snappy * 7.f);
      v.snareK = 1.f - fastRcp(0.01f * kFs) * semi(-decay * 60.f - snappy * 7.f);
      v.fmK = 1.f - fastRcp(0.007f * kFs);
      snappy = clamp01(snappy * 1.1f - 0.05f);
      v.drumLevel = sqrtf(1.f - snappy);
      v.snareLevel = sqrtf(snappy);
      const float t = fastExp2((m[1] - 0.5f) * 2.f);  // TONE: the snare band, 0.5x .. 2x
      opSet(v.snareHp, tanFast(minf(10.f * v.f0 * t, 0.49f)));
      svfSet(v.snareLp, tanFast(minf(35.f * v.f0 * t, 0.49f)), 0.5f + 2.f * snappy);
      opSet(v.drumLp, tanFast(minf(3.f * v.f0, 0.49f)));
      if (latch) v.hold = static_cast<int>((0.04f + decay * 0.03f) * kFs);
      v.drumAmp = snap(v.drumAmp), v.snareAmp = snap(v.snareAmp), v.fm = snap(v.fm);
      break;
    }
    case Drum2Kind::Cpx: {  // DECAY FREQ TONE SPREAD PATTERN
      Cpx& v = s_.cpx;
      const float f1 = minf(350.f * fastExp2(m[1] * 3.5f) * hz / kFs, 0.45f);
      const float f2 = minf(300.f * fastExp2(m[1] * 3.32f) * hz / kFs, 0.45f);
      svfSet(v.bp1, tanDirty(f1), 1.f + 1.5f * m[2]);
      svfSet(v.bp2, tanDirty(f2), 0.75f + 5.75f * m[2]);
      v.k1 = adK(0.05f + 0.1f * m[0]);
      v.k2 = adK(0.05f + 1.95f * m[0] * m[0]);
      v.att2K = fastExp(kLog1e4 * fastRcp(0.002f * kFs));
      v.gain = (0.4f + 0.6f * a) * kGainCpx;
      if (latch) {
        const int pat = clapPattern(m[4]);
        for (int i = 1; i < 5; ++i) {
          v.timer[i] = 1 + static_cast<int>(drum2ClapBurstMs(pat, m[3], i) * (kFs / 1000.f));
          v.amp[i - 1] = kClapAmp[pat][i - 1];
        }
      }
      break;
    }
    case Drum2Kind::Rsx: {  // DECAY TONE NOISE
      Rsx& v = s_.rsx;
      const float f0 = fclamp(hz / kFs, 1e-4f, 0.1f);
      const float d = 0.1f + 0.65f * m[0];
      const float xt = d * (1.f + d * (d - 1.f));
      const float q = 250.f * semi(xt * 84.f);
      static const float kMode[3] = {1.f, 2.2727f, 4.5454f};
      static const float kQ[3] = {0.5f, 0.25f, 0.175f};
      for (int i = 0; i < 3; ++i) {
        const float f = minf(f0 * kMode[i], 0.45f);
        svfSet(v.res[i], tanDirty(f), 1.f + f * q * kQ[i]);
      }
      svfSet(v.hp, tanDirty(minf(2.f * f0, 0.45f)), 5.f - 4.f * m[1]);
      v.base = 0.35f + 0.3f * m[1];
      v.noise = 0.2f * m[2];
      v.envK = adK(d);
      v.oscInc = static_cast<uint32_t>(f0 * 4294967296.f);
      v.gain = (0.5f + 0.5f * a) * kGainRsx;
      v.env = snap(v.env);
      break;
    }
    case Drum2Kind::Hhr: {  // DECAY TONE NOISE
      Hhr& v = s_.hhr;
      static const float kPair[3][2] = {{200.f, 7530.f}, {510.f, 8075.f}, {730.f, 10500.f}};
      const float r = 0.6667f * hz / kFs;
      v.pairs = 0;
      for (int i = 0; i < 3; ++i) {
        const float fa = kPair[i][0] * r, fb = kPair[i][1] * r;
        if (fb < 0.45f) v.pairs |= static_cast<uint8_t>(1u << i);
        v.inc[2 * i] = minf(fa, 0.45f), v.inc[2 * i + 1] = minf(fb, 0.45f);
      }
      const float cut = minf(150.f * semi(m[1] * 72.f) * hz, 16000.f) / kFs;
      svfSet(v.bp, fastTan(kPi * cut), 1.f);
      svfSet(v.hp, fastTan(kPi * cut), 0.5f);
      v.noisiness = m[2] * m[2];
      v.noiseF = minf(0.01f * hz * (16.f + 16.f * (1.f - v.noisiness)), 0.5f);
      v.envK = 1.f - 0.003f * semi(-m[0] * 84.f);
      v.cutK = 1.f - 0.0025f * semi(-m[0] * 36.f);
      if (latch) v.env = (1.5f + 0.5f * (1.f - m[0])) * (0.3f + 0.7f * a);
      v.env = snap(v.env);
      break;
    }
    default:
      break;
  }
}

// ---- BDA: a pulse rings a resonant band-pass; attack FM and self FM from its low-pass ----

MT_HOT void Drum2Voice::renderBda(float* o, int n) {
  Bda& v = s_.bda;
  const float kPulseDecay = 1.f - 1.f / (0.2e-3f * kFs);
  const float kPulseFilter = 1.f / (0.1e-3f * kFs);
  const float kRetrigDecay = 1.f - 1.f / (0.05f * kFs);
  for (int i = 0; i < n; ++i) {
    float pulse;
    if (v.pulseLeft) {
      --v.pulseLeft;
      pulse = v.pulseLeft ? v.pulseH : v.pulseH - 1.f;
      v.pulse = pulse;
    } else {
      v.pulse *= kPulseDecay;
      pulse = v.pulse;
    }
    onePole(v.pulseLp, pulse, kPulseFilter);
    float x = (pulse - v.pulseLp) + pulse * 0.044f;
    // Diode: passes the positive half, folds the negative one softly.
    if (x < 0) {
      x *= 2.f;
      x = 0.7f * x * fastRcp(1.f - x);
    }
    pulse = x;
    float fmPulse = 0;
    if (v.fmPulseLeft) {
      --v.fmPulseLeft;
      fmPulse = 1.f;
      v.retrig = v.fmPulseLeft ? 0.f : -0.8f;
    } else {
      v.retrig *= kRetrigDecay;
    }
    onePole(v.fmPulseLp, fmPulse, kPulseFilter);
    float pd = 10.f * v.lpOut - 1.f;
    if (pd < 0) {
      pd *= 2.f;
      pd = 0.7f * pd * fastRcp(1.f - pd);
    }
    const float punch = 0.7f + pd;
    float f = v.f0 * (1.f + v.fmPulseLp * 1.7f * v.atk + punch * 0.08f * v.self);
    f = fclamp(f, 0.f, 0.4f);
    svfSet(v.res, tanDirty(f), 1.f + v.q * f);
    float hp, bp;
    svfRun(v.res, (pulse - v.retrig * 0.2f) * v.scale, hp, bp, v.lpOut);
    onePole(v.toneLp, pulse * v.leak + bp, v.toneF);
    o[i] = v.toneLp * v.gain;
  }
}

// ---- SDA: a pulse rings 5 shell modes, soft-clipped; half-wave noise through a band-pass ----

MT_HOT void Drum2Voice::renderSda(float* o, int n) {
  Sda& v = s_.sda;
  const float kPulseDecay = 1.f - 1.f / (0.1e-3f * kFs);
  const int modes = v.gain[2] > 0 ? 5 : 2;
  const float g = (0.6f + 0.4f * accent_) * kGainSda;  // accent: the pulse alone barely moves the peak
  for (int i = 0; i < n; ++i) {
    float pulse;
    if (v.pulseLeft) {
      --v.pulseLeft;
      pulse = v.pulseLeft ? v.pulseH : v.pulseH - 1.f;
      v.pulse = pulse;
    } else {
      v.pulse *= kPulseDecay;
      pulse = v.pulse;
    }
    onePole(v.pulseLp, pulse, 0.75f);
    float shell = 0;
    for (int k = 0; k < modes; ++k) {
      const float e = k == 0 ? (pulse - v.pulseLp) + 0.006f * pulse : 0.026f * pulse;
      shell += v.gain[k] * (svfBp(v.res[k], e) + e * v.leak);
    }
    shell = softClip(shell);
    float noise = 2.f * rnd() - 1.f;
    if (noise < 0) noise = 0;
    v.noiseEnv *= v.noiseK;
    noise *= v.noiseEnv * v.snappy * 2.f;
    noise = svfBp(v.noise, noise);
    o[i] = (noise + shell * (1.f - v.snappy)) * g;
  }
}

// ---- BDS: distorted sine with a pitch sweep, transistor VCA, click + noise ----

MT_HOT void Drum2Voice::renderBds(float* o, int n) {
  Bds& v = s_.bds;
  for (int i = 0; i < n; ++i) {
    onePole(v.phaseNoise, rnd() - 0.5f, 0.002f);
    if (v.fmPw) {
      --v.fmPw;
      v.phase = 0.25f;
    } else {
      v.fm *= v.fmK;
      const float fm = 1.f + v.fmAmt * 3.5f * v.fmLp;
      v.phase += minf(v.f0 * fm, 0.5f);
      if (v.phase >= 1.f) v.phase -= 1.f;
    }
    if (v.bodyPw) {
      --v.bodyPw;
    } else {
      v.bodyEnv *= v.bodyK;
      v.trEnv *= v.trK;
    }
    onePole(v.bodyEnvLp, v.bodyEnv, 0.1f);
    onePole(v.trEnvLp, v.trEnv, 0.1f);
    onePole(v.fmLp, v.fm, 0.1f);
    // Distorted sine: a triangle through x / (1 + |x|), jittered, cross-faded with a clean sine.
    const float ph = wrap1(v.phase + v.phaseNoise * v.dirt);
    const float tri = (ph < 0.5f ? ph : 1.f - ph) * 4.f - 1.f;
    const float sine = 2.f * tri * fastRcp(1.f + fabsf(tri));
    const float clean = phaseSine(ph + 0.75f);
    const float body = sine + (1.f - v.dirt) * (clean - sine);
    // Click: a slewed step, high-passed, low-passed at 5 kHz; plus filtered noise.
    const float step = v.bodyPw ? 0.f : 1.f;
    const float err = step - v.clickLp;
    v.clickLp += (err > 0 ? 0.5f : 0.1f) * err;
    onePole(v.clickHp, v.clickLp, 0.04f);
    const float click = svfLp(v.click, v.clickLp - v.clickHp);
    onePole(v.noiseLp, rnd(), 0.05f);
    onePole(v.noiseHp, v.noiseLp, 0.005f);
    const float transient = click + v.noiseLp - v.noiseHp;
    // Transistor VCA.
    const float g = v.bodyEnvLp;
    const float s = (body - 0.6f) * g;
    float mix = -(3.f * s * fastRcp(2.f + fabsf(s)) + g * 0.3f);
    mix -= transient * v.trEnvLp * v.trLevel;
    onePole(v.toneLp, mix, v.toneF);
    onePole(v.dc, v.toneLp, 0.0015f);  // DC blocker, ~10 Hz
    o[i] = (v.toneLp - v.dc) * kGainBds;
  }
}

// ---- SDS: two distorted sines with a short FM, noise through a band (low-pass + high-pass) ----

MT_HOT void Drum2Voice::renderSds(float* o, int n) {
  Sds& v = s_.sds;
  float rna = fclamp((0.125f - v.f0) * 8.f, 0.f, 1.f);
  rna = rna * rna * v.fmAmt;
  for (int i = 0; i < n; ++i) {
    v.drumAmp *= (v.drumAmp > 0.03f || (i & 1)) ? v.drumK : 1.f;
    if (v.hold) {
      --v.hold;
    } else {
      v.snareAmp *= v.snareK;
    }
    v.fm *= v.fmK;
    float rn = (v.phase[0] > 0.5f ? -1.f : 1.f) + (v.phase[1] > 0.5f ? -1.f : 1.f);
    rn *= rna * 0.025f;
    const float f = v.f0 * (1.f + v.fmAmt * 4.f * v.fm);
    v.phase[0] += f;
    v.phase[1] += f * 1.47f;
    if (rna > 0.1f) {
      if (v.phase[0] >= 1.f + rn) v.phase[0] = 1.f - v.phase[0];
      if (v.phase[1] >= 1.f + rn) v.phase[1] = 1.f - v.phase[1];
    } else {
      if (v.phase[0] >= 1.f) v.phase[0] -= 1.f;
      if (v.phase[1] >= 1.f) v.phase[1] -= 1.f;
    }
    float drum = -0.1f;
    for (int k = 0; k < 2; ++k) {
      const float p = v.phase[k];
      const float t = (p < 0.5f ? p : 1.f - p) * 4.f - 1.3f;
      drum += 2.f * t * fastRcp(1.f + fabsf(t)) * (k ? 0.25f : 0.6f);
    }
    drum *= v.drumAmp * v.drumLevel;
    drum = opLp(v.drumLp, drum);
    float snare = svfLp(v.snareLp, rnd());
    snare = opHp(v.snareHp, snare);
    snare = (snare + 0.1f) * (v.snareAmp + v.fm) * v.snareLevel;
    const float x = snare + drum;
    onePole(v.dc, x, 0.0015f);  // DC blocker, ~10 Hz
    o[i] = (x - v.dc) * kGainSds;
  }
}

// ---- CPX: binary noise through two band-passes; claps by timers, then the tail ----

MT_HOT void Drum2Voice::renderCpx(float* o, int n) {
  Cpx& v = s_.cpx;
  for (int i = 0; i < n; ++i) {
    bool f1 = false;
    for (int t = 0; t < 4; ++t)
      if (v.timer[t] > 0 && --v.timer[t] == 0) {
        v.a1 = t == 0 ? 1.f : v.amp[t - 1];
        f1 = true;
      }
    if (v.timer[4] > 0 && --v.timer[4] == 0) {
      v.a2 = v.amp[3];
      v.e2 = 1e-4f;
      v.att2 = true;
    }
    if (f1) {
      v.e1 = 1.f;
    } else if (v.e1 > 1e-4f) {
      v.e1 *= v.k1;
    } else {
      v.e1 = 0;
    }
    if (v.att2) {
      v.e2 *= v.att2K;
      if (v.e2 >= 1.f) v.e2 = 1.f, v.att2 = false;
    } else if (v.e2 > 1e-4f) {
      v.e2 *= v.k2;
    } else {
      v.e2 = 0;
    }
    const float noise = rnd() > 0.5f ? 1.f : -1.f;
    const float tr = svfBp(v.bp1, noise) * v.e1 * v.a1;
    const float tail = svfBp(v.bp2, noise) * v.e2 * v.a2;
    o[i] = (tr + tail) * v.gain;
  }
}

// ---- RSX: a short pulse rings 3 modes, hard-clipped; enveloped noise; resonant high-pass ----

MT_HOT void Drum2Voice::renderRsx(float* o, int n) {
  Rsx& v = s_.rsx;
  const float kPulseDecay = 1.f - 1.f / (0.05e-3f * kFs);
  for (int i = 0; i < n; ++i) {
    float pulse;
    if (v.pulseLeft) {
      --v.pulseLeft;
      pulse = v.pulseLeft ? v.pulseH : v.pulseH - 1.f;
      v.pulse = pulse;
    } else {
      v.pulse *= kPulseDecay;
      pulse = v.pulse;
    }
    onePole(v.pulseLp, pulse, v.base);
    const float e0 = (pulse - v.pulseLp) + 0.006f * pulse, e1 = 0.025f * pulse;
    const float shell = svfBp(v.res[0], e0) + 0.5f * svfBp(v.res[1], e1) + svfBp(v.res[2], e1);
    v.env = v.env > 1e-4f ? v.env * v.envK : 0.f;
    v.oscPh += v.oscInc;
    const float nb = v.env * (tableSine(v.oscPh) + 0.25f + rnd());
    const float x = fclamp(50.f * shell, -1.f, 1.f) + v.noise * nb;
    o[i] = svfHp(v.hp, x) * v.gain;
  }
}

// ---- HHR: three ring-modulated square x saw pairs, band-pass, S&H noise, two-stage decay, high-pass ----

MT_HOT void Drum2Voice::renderHhr(float* o, int n) {
  Hhr& v = s_.hhr;
  float rinc[6];
  for (int k = 0; k < 6; ++k) rinc[k] = fastRcp(v.inc[k] > 1e-6f ? v.inc[k] : 1e-6f);
  for (int i = 0; i < n; ++i) {
    float sum = 0;
    for (int p = 0; p < 3; ++p) {
      if (!(v.pairs & (1u << p))) continue;
      const int a = 2 * p, b = a + 1;
      float t = v.ph[a] += v.inc[a];
      if (t >= 1.f) t = v.ph[a] -= 1.f;
      float th = t + 0.5f;
      if (th >= 1.f) th -= 1.f;
      const float sq = (t < 0.5f ? 1.f : -1.f) + blep(t, v.inc[a], rinc[a]) - blep(th, v.inc[a], rinc[a]);
      float u = v.ph[b] += v.inc[b];
      if (u >= 1.f) u = v.ph[b] -= 1.f;
      const float saw = 2.f * u - 1.f - blep(u, v.inc[b], rinc[b]);
      sum += sq * saw;
    }
    float x = svfBp(v.bp, sum);
    v.noiseClock += v.noiseF;
    if (v.noiseClock >= 1.f) {
      v.noiseClock -= 1.f;
      v.noiseSample = rnd() - 0.5f;
    }
    x += v.noisiness * (v.noiseSample - x);
    v.env *= v.env > 0.5f ? v.envK : v.cutK;
    o[i] = svfHp(v.hp, x * v.env) * kGainHhr;
  }
}

MT_HOT void Drum2Voice::render(float* __restrict out, int n, float amp) {
  float buf[64];
  float tailV = tailV_, last = last_, pk = pk_;
  for (int off = 0; off < n; off += 64) {
    const int m = n - off < 64 ? n - off : 64;
    if (live_) {
      switch (kind_) {
        case Drum2Kind::Bda: renderBda(buf, m); break;
        case Drum2Kind::Sda: renderSda(buf, m); break;
        case Drum2Kind::Bds: renderBds(buf, m); break;
        case Drum2Kind::Sds: renderSds(buf, m); break;
        case Drum2Kind::Cpx: renderCpx(buf, m); break;
        case Drum2Kind::Rsx: renderRsx(buf, m); break;
        case Drum2Kind::Hhr: renderHhr(buf, m); break;
        default: memset(buf, 0, sizeof(float) * m); break;
      }
    } else {
      memset(buf, 0, sizeof(float) * m);
    }
    float* o = out + off;
    for (int i = 0; i < m; ++i) {
      float s = buf[i];
      s = s > 1.f ? 1.f : (s < -1.f ? -1.f : s);
      const float as = fabsf(s);
      pk = as > pk ? as : pk * kPeakK;
      last = s * amp + tailV;
      o[i] += last;
      tailV *= kChokeK;
    }
  }
  tailV_ = snap(tailV), last_ = last, pk_ = snap(pk);
  fresh_ = false;
}

bool Drum2Voice::done() const {
  if (!live_) return fabsf(tailV_) < kOff;
  if (fresh_) return false;
  if (kind_ == Drum2Kind::Cpx)
    for (int t : s_.cpx.timer)
      if (t > 0) return false;
  return pk_ < kOff && fabsf(tailV_) < kOff;
}

}  // namespace mt
