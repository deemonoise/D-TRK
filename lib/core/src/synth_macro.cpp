// Derived from the macro oscillator in github.com/pichenettes/eurorack, MIT License: a float
// re-implementation at 44.1 kHz. The vowel and consonant formant records (synth_macro.cpp) are its
// data, verbatim.
//
// Copyright 2012 Emilie Gillet.
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
#include "synth_macro.h"
#include <string.h>
#include "fastmath.h"
#include "synth_osc.h"
#include "synth_shape.h"

namespace mt {

namespace {

constexpr float kMaxDt = 0.4999f;  // phase step per sample, below Nyquist

MT_INLINE float clamp01(float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }

// Output gains: each model at 220 Hz, t = c = 0.5 as loud as a SYNTH saw (RMS 0.577), +-1.3 dB
// (SAW/SQR: -1.3, its gain held down by the narrow pulse's peak at t, c 1).
constexpr float kGainSawPlus = 1.f, kGainSawSqr = 1.2f, kGainMorph = 0.66f, kGainFold = 1.13f;
constexpr float kGainVosim = 1.12f, kGainVowel = 1.47f, kGainFm = 0.79f, kGainFbFm = 0.88f;

// SAW+: notch (a flat segment at the start of the ramp) up to 0.375 of the period, at least 4
// samples so the two edges stay BLEP-able.
constexpr float kNotchMax = 0.375f;
// SAW/SQR: the variable saw's second edge at 0.5 t, the square's low part 0.5 .. 0.0117 of the period;
// both at least 2 samples (a narrower pulse aliases as a naive one and fades to DC). The square at
// 148 / 256 under the saw.
constexpr float kSqrLevel = 148.f / 256.f;
// MORPH / SAW/SQR: the square's PW span over t (the original's 32000 / 32768 clamp).
constexpr float kPwSpan = 0.5f * 32000.f / 32768.f;
// MORPH overdrive: the low-pass corner at the pitch + 128 (1 - c) semitones (at most 12 kHz), the
// overdrive tanh(8 x). Its amount c fades out above note 80 (~830 Hz) by 1 / 16 per semitone, gone by
// ~2.1 kHz (our choice for 44.1 kHz: an overdriven wave above that aliases audibly).
constexpr float kLpMaxHz = 12000.f;
constexpr float kFuzzNote = 80.f, kFuzzSlope = 1.f / 16;
// DC trackers (MORPH's overdrive, VOSIM, VOWEL, FB FM): a ~14 Hz one-pole. FB FM's at most at the
// pitch / 16 (2 pi / 16 per pitch period): a bass note's tracker near its own pitch (or the slow
// sub-ratio wave) follows the wave and lifts its peaks past 1.5 (1.66 at 16 .. 41 Hz).
constexpr float kDc = 1.f / 512;
constexpr float kDcPerDt = 6.2831853f / 16;
// FM: the index fades out as the modulator (pitch x ratio) rises from note 108 (4.2 kHz) to 120
// (8.4 kHz): a faster modulator's sidebands alias.
constexpr float kFmFadeNote = 120.f, kFmFadeSpan = 12.f;
// FOLD: gain 1 .. 7.9 (sine fold: |phase| < 2 for phaseSine), the sine fold crossfaded in from the
// plain sine over the first 1 / 8 of the amount; full up to 1 kHz, off from 5 kHz (as SYNTH's fold).
constexpr float kFoldMax = 6.9f, kFoldFade = 8.f;
// VOWEL: 40 ms of consonant after the note-on; the formant table unit is 96 kHz / 2^20 per step of
// the shift 200 .. 711 (x 0.78 .. 2.78 of 23.4 Hz); the drive into the soft clip (the original's
// 4 x 255 / 32768 per amplitude unit), formant 3 a sine at a square's fundamental (0.32 of the others);
// the consonant noise level.
constexpr int kConsonant = kSynthRate * 40 / 1000;
constexpr float kFormantUnit = 96000.f / 1048576.f / kSynthRate;
constexpr float kVowelDrive = 4.f * 255.f / 32768.f, kF3Level = 0.32f, kNoise = 0.5f;

// Vowel and consonant formants (the original's data, MIT): frequencies (table units), amplitudes
// (0..15, gain e^(0.184 a)).
struct Phoneme {
  uint8_t f[3], a[3];
};
const Phoneme kVowels[9] = {
    {{27, 40, 89}, {15, 13, 1}}, {{18, 51, 62}, {13, 12, 6}}, {{15, 69, 93}, {14, 12, 7}},
    {{10, 84, 110}, {13, 10, 8}}, {{23, 44, 87}, {15, 12, 1}}, {{13, 29, 80}, {13, 8, 0}},
    {{6, 46, 81}, {12, 3, 0}},    {{9, 51, 95}, {15, 3, 0}},   {{6, 73, 99}, {7, 3, 14}}};
const Phoneme kConsonants[8] = {
    {{6, 54, 121}, {9, 9, 0}},  {{18, 50, 51}, {12, 10, 5}}, {{11, 24, 70}, {13, 8, 0}},
    {{15, 69, 74}, {14, 12, 7}}, {{16, 37, 111}, {14, 8, 1}}, {{18, 51, 62}, {14, 12, 6}},
    {{6, 26, 81}, {5, 5, 5}},   {{6, 73, 99}, {7, 10, 14}}};
constexpr int kNoisyConsonant = 6;  // consonants 6, 7 carry noise

// FM ratios (log2), 25 over COLOR; each holds for 3 / 4 of its step, then glides to the next.
const float kFmRatioLog2[25] = {
    -3.f, -2.f, -1.f, -0.98667f, -0.5f, -0.34850f, 0.f, 0.01333f, 0.5f, 0.65150f, 0.80735f, 1.f,
    1.01333f, 1.16993f, 1.45943f, 1.5f, 1.58496f, 1.65150f, 1.79248f, 2.f, 2.08496f, 2.23557f,
    2.32193f, 2.5f, 3.f};

MT_INLINE uint32_t xorshift(uint32_t& x) {
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  return x;
}

// PolyBLEP accumulator, one sample late as BlOsc, in locals for the loops (out does not alias it).
struct Bl {
  float prev, corr, inv;
  // A step h at distance x (0..1 of a sample) before the current sample.
  MT_INLINE void step(float h, float x) {
    prev += 0.5f * h * x * x;
    corr += 0.5f * h * (2.f * x - x * x - 1.f);
  }
  // The finished sample n - 1; v (sample n, naive) becomes pending.
  MT_INLINE float emit(float v) {
    const float r = prev;
    prev = v + corr;
    corr = 0;
    return r;
  }
  // A moving edge: a = phase - edge after the last sample, b after this one (b - a = dt minus the
  // edge's motion). The phase passing the edge steps by h; the edge passing the phase (it moved
  // forward faster than the phase), by -h. The crossing time is linear in between.
  MT_INLINE void cross(float a, float b, float h) {
    // Most samples cross nothing: a and b of one sign (by the sign bits: no float compare, whose VMRS
    // waits for it). They never are -0 (differences of unequal values, or +0).
    if (static_cast<int32_t>(fbits(a) ^ fbits(b)) >= 0) return;
    if (a < 0.f) {
      if (b >= 0.f) step(h, b * fastRcp(b - a));
    } else if (b < 0.f) {
      step(-h, b * -fastRcp(a - b));
    }
  }
};

// MORPH's zone weights at t (x = 3 t): triangle, saw, the square's gain and its low part pq. The square
// is taken without its mean, scaled by 0.5 + pq (~1 / (2 - 2 pq)): a narrow pulse with its DC removed
// would peak near -2.
struct MorphW {
  float wt, ws, gq, pq;
};
MT_INLINE MorphW morphW(float t, float wMin) {
  const float x = 3.f * t;
  const float wq = minf(maxf(x - 1.f, 0.f), 1.f);
  const float pq = maxf(0.5f - kPwSpan * minf(maxf(x - 2.f, 0.f), 1.f), wMin);
  return {maxf(0.f, 1.f - x), maxf(0.f, 1.f - fabsf(x - 1.f)), wq * (0.5f + pq), pq};
}

// synth_shape.h's foldAnti for |u| < 11 (FOLD's u: |u| <= 7.9), branch-free: u + 11 > 0, so the int
// cast floors (u - 1) / 4 + 3 (no wrap4 test, no clamp); with v = wrap4(u - 1) - 2 in -2 .. 2, both
// pieces are v (|v| / 2 - 1).
MT_INLINE float foldAntiNear(float u) {
  const int q = static_cast<int>((u + 11.f) * 0.25f);
  const float v = (u - 3.f) - static_cast<float>(4 * q - 12);
  return v * (0.5f * fabsf(v) - 1.f);
}

}  // namespace

void MacroVoice::trigger(uint32_t seed) {
  ph_ = 0;
  rng_ = seed * 2654435761u + 0x9E3779B9u;
  if (!rng_) rng_ = 1;
  model_ = 0xFF;
  snap_ = true;
}

MT_HOT void MacroVoice::control(const MacroParams& p, int n) {
  p_ = p;
  const float hzDt = p.hz * (1.f / kSynthRate);
  dt_ = hzDt < kMaxDt ? (hzDt > 0.f ? hzDt : 0.f) : kMaxDt;
  inv_ = dt_ > 1e-30f ? fastRcp(dt_) : 0.f;
  note_ = p.hz > 1e-3f ? 69.f + 12.f * fastLog2(p.hz * (1.f / 440.f)) : 0.f;
  const float t = clamp01(p.timbre), c = clamp01(p.color);
  if (model_ != p.model) {
    model_ = p.model;
    memset(&s_, 0, sizeof(s_));
    fresh_ = true;
    if (static_cast<MacroModel>(p.model) == MacroModel::Vowel) {
      s_.fo.consonant = kConsonant;
      s_.fo.phon = static_cast<uint8_t>(xorshift(rng_) >> 29);  // high bits: the low ones of a
                                                                // hashed small seed repeat
    }
  }
  switch (static_cast<MacroModel>(p.model)) {
    case MacroModel::Morph: {
      float fc = noteHz(note_ + 128.f * (1.f - c));
      fc = fc < kLpMaxHz ? fc : kLpMaxHz;
      s_.an.lpK = 1.f - noteHz(69.f + 12.f * -6.2831853f * 1.44269504f * (1.f / kSynthRate) * fc) *
                            (1.f / 440.f);  // 1 - e^(-2 pi fc / rate)
      const float off = (note_ - kFuzzNote) * kFuzzSlope;
      s_.an.fuzzOff = off > 0.f ? off : 0.f;
      break;
    }
    case MacroModel::Fold:
      s_.fd.lim = clamp01((5000.f - p.hz) * (1.f / 4000.f));
      break;
    case MacroModel::Vosim:  // formants at notes 128 t and 128 c
      s_.fo.inc[0] = minf(noteHz(128.f * t) * (1.f / kSynthRate), kMaxDt);
      s_.fo.inc[1] = minf(noteHz(128.f * c) * (1.f / kSynthRate), kMaxDt);
      break;
    case MacroModel::Vowel:
      controlVowel(t, c, n);
      break;
    case MacroModel::Fm:
    case MacroModel::FbFm: {
      const float x = c * 24.f;
      int i = static_cast<int>(x);
      i = i > 23 ? 23 : i;
      const float f = clamp01((x - static_cast<float>(i) - 0.375f) * 4.f);
      const float lr = kFmRatioLog2[i] + (kFmRatioLog2[i + 1] - kFmRatioLog2[i]) * f;
      s_.fm.dt = dt_ * noteHz(69.f + 12.f * lr) * (1.f / 440.f);
      const float mnote = note_ + 12.f * lr;  // the modulator's note
      // FB FM: the index falls by 1 / 64 per semitone of pitch x ratio above note 72; FM: over the
      // octave below note 120.
      s_.fm.depth = static_cast<MacroModel>(p.model) == MacroModel::FbFm
                        ? clamp01(1.f - (mnote - 72.f) * (1.f / 64))
                        : clamp01((kFmFadeNote - mnote) * (1.f / kFmFadeSpan));
      s_.fm.kdc = minf(kDc, kDcPerDt * dt_);
      break;
    }
    default:
      break;
  }
  if (snap_) {
    snap_ = false;
    t_ = t, c_ = c, tStep_ = cStep_ = 0;
    return;
  }
  const float inv = n == 32 ? 1.f / 32 : (n > 0 ? 1.f / n : 1.f);  // kControl: no __divsf3
  tStep_ = (t - t_) * inv;
  cStep_ = (c - c_) * inv;
}

// VOWEL: vowel t x 8 (neighbours blended), formants shifted by (200 + 511 c) / 256; the note's
// consonant for its first 40 ms.
MT_HOT void MacroVoice::controlVowel(float t, float c, int n) {
  Formant& fo = s_.fo;
  const float shift = (200.f + 511.f * c) * kFormantUnit;
  float f[3], a[3];
  fo.noisy = 0;
  if (fo.consonant > 0) {
    fo.consonant -= n;
    fo.noisy = fo.phon >= kNoisyConsonant;
    const Phoneme& q = kConsonants[fo.phon];
    for (int k = 0; k < 3; ++k) f[k] = q.f[k], a[k] = q.a[k];
  } else {
    const float x = t * 8.f;
    int i = static_cast<int>(x);
    i = i > 7 ? 7 : i;
    const float b = x - static_cast<float>(i);
    const Phoneme &p0 = kVowels[i], &p1 = kVowels[i + 1];
    for (int k = 0; k < 3; ++k) {
      f[k] = p0.f[k] + (static_cast<float>(p1.f[k]) - p0.f[k]) * b;
      a[k] = p0.a[k] + (static_cast<float>(p1.a[k]) - p0.a[k]) * b;
    }
  }
  for (int k = 0; k < 3; ++k) {
    // A formant below the pitch is restarted before it rises: at least the pitch (else a high note of
    // a low vowel is a near-DC blip, removed by the DC tracker).
    fo.inc[k] = minf(maxf(f[k] * shift, dt_), kMaxDt);
    // e^(0.184 a), 0 at a = 0 (linear below a = 1).
    const float g = noteHz(69.f + 12.f * 0.26545f * maxf(a[k], 1.f)) * (1.f / 440.f);
    fo.amp[k] = (a[k] < 1.f ? a[k] * g : g) * kVowelDrive * (k == 2 ? kF3Level : 1.f);
  }
}

MT_HOT void MacroVoice::render(float* out, int n, float amp) {
  if (model_ != p_.model) return;  // no control() since the trigger: no state to render from
  switch (static_cast<MacroModel>(p_.model)) {
    case MacroModel::SawPlus: renderSawPlus(out, n, amp * kGainSawPlus); break;
    case MacroModel::Morph: renderMorph(out, n, amp * kGainMorph); break;
    case MacroModel::SawSqr: renderSawSqr(out, n, amp * kGainSawSqr); break;
    case MacroModel::Fold: renderFold(out, n, amp * kGainFold); break;
    case MacroModel::Vosim: renderVosim(out, n, amp * kGainVosim); break;
    case MacroModel::Vowel: renderVowel(out, n, amp * kGainVowel); break;
    case MacroModel::Fm: renderFm(out, n, amp * kGainFm, false); break;
    case MacroModel::FbFm: renderFm(out, n, amp * kGainFbFm, true); break;
    default: break;
  }
  fresh_ = false;
}

// Ramp from -1 to 1 with a flat notch at depth d = c - 1.25 for phase < pw (the original's -0.625 ..
// -0.125 on its half-scale ramp). Its mean pw d + pw - pw^2 is subtracted (the original's DC shift
// 0.125 (1 - c) with it).
MT_HOT void MacroVoice::renderSawPlus(float* __restrict out, int n, float g) {
  Analog& st = s_.an;
  float t = t_, c = c_, ph = ph_;
  const float dt = dt_, dtS = tStep_, dcS = cStep_;
  const float pwMin = 4.f * dt < 0.5f ? 4.f * dt : 0.5f;
  if (fresh_) st.e[0] = maxf(kNotchMax * t, pwMin);
  float ep = st.e[0];
  Bl b{st.prev, st.corr, inv_};
  for (int i = 0; i < n; ++i) {
    t += dtS, c += dcS;  // the ramp reaches the target on the last sample
    const float pw = maxf(kNotchMax * t, pwMin);
    const float d = c - 1.25f;
    const float h = 2.f * pw - 1.f - d;  // notch -> ramp
    float tn = ph + dt;
    if (tn >= 1.f) {
      tn -= 1.f;
      b.step(d - 1.f, tn * b.inv);
      b.cross(ph - ep - 1.f, tn - pw, h);  // the notch's end in the new period
    } else {
      b.cross(ph - ep, tn - pw, h);
    }
    ph = tn, ep = pw;
    const float v = (ph < pw ? d : 2.f * ph - 1.f) - pw * (d + 1.f - pw);
    out[i] += b.emit(v) * g;
  }
  t_ = t, c_ = c, ph_ = ph, st.e[0] = ep;
  st.prev = b.prev, st.corr = b.corr;
}

// Variable saw (two ramps offset by ps, summed: mean 0) to square balance by c. The square's mean
// wq (1 - 2 pq) is subtracted.
MT_HOT void MacroVoice::renderSawSqr(float* __restrict out, int n, float g) {
  Analog& st = s_.an;
  float t = t_, c = c_, ph = ph_;
  const float dt = dt_, dtS = tStep_, dcS = cStep_;
  const float wMin = 2.f * dt < 0.5f ? 2.f * dt : 0.5f;
  const float sMin = maxf(wMin, 1.f / 64);
  if (fresh_) st.e[0] = maxf(0.5f * t, sMin), st.e[1] = maxf(0.5f - kPwSpan * t, wMin);
  float es = st.e[0], eq = st.e[1];
  Bl b{st.prev, st.corr, inv_};
  for (int i = 0; i < n; ++i) {
    t += dtS, c += dcS;
    const float ps = maxf(0.5f * t, sMin);              // saw's second edge
    const float pq = maxf(0.5f - kPwSpan * t, wMin);    // square: low below
    // The linear crossfade dips ~4 dB mid-way (the two waves are partly out of phase): lifted by
    // 1 + 1.2 c (1 - c), so one gain fits the middle without the ends' peaks passing 1.5.
    const float lift = 1.f + 1.2f * c * (1.f - c);
    const float ws = (1.f - c) * lift, wq = kSqrLevel * c * lift;
    float tn = ph + dt;
    if (tn >= 1.f) {  // ph > 0.5 >= ps, pq: no crossing before the wrap
      tn -= 1.f;
      b.step(-ws - 2.f * wq, tn * b.inv);
      b.cross(ph - es - 1.f, tn - ps, -ws);
      b.cross(ph - eq - 1.f, tn - pq, 2.f * wq);
    } else {
      b.cross(ph - es, tn - ps, -ws);
      b.cross(ph - eq, tn - pq, 2.f * wq);
    }
    ph = tn, es = ps, eq = pq;
    const float saw = 2.f * ph - ps - (ph < ps ? 0.f : 1.f);
    const float sq = (ph < pq ? -1.f : 1.f) - (1.f - 2.f * pq);
    out[i] += b.emit(ws * saw + wq * sq) * g;
  }
  t_ = t, c_ = c, ph_ = ph, st.e[0] = es, st.e[1] = eq;
  st.prev = b.prev, st.corr = b.corr;
}

// Triangle -> saw -> square -> narrowing pulse over t (three zones, crossfaded by weights ramped per
// sample: no zone switch, no oscillator state), the square's mean subtracted; then an overdriven copy of a one-pole
// low-pass mixed in by c. The overdrive runs only when its amount (c - pitch offset) is above 0
// somewhere in the block (a flag per block: one loop, no second copy in ITCM).
MT_HOT void MacroVoice::renderMorph(float* __restrict out, int n, float g) {
  Analog& st = s_.an;
  float ph = ph_, lp = st.lp, dc = st.fuzzDc;
  const float dt = dt_, k = st.lpK, off = st.fuzzOff;
  const float wMin = 2.f * dt < 0.5f ? 2.f * dt : 0.5f;
  // The zone weights, the pulse width and the overdrive amount are piecewise linear in t and c, which
  // ramp linearly: ramped per sample from their values at the block's ends (exact inside a zone; a block
  // where t crosses a zone border bends over its 32 samples instead of at one).
  const float t1 = t_ + tStep_ * n, c1 = c_ + cStep_ * n;
  const MorphW w0 = morphW(t_, wMin), w1 = morphW(t1, wMin);
  const float a0 = maxf(0.f, c_ - off), a1 = maxf(0.f, c1 - off);
  const bool fuzz = a0 > 0.f || a1 > 0.f;
  const float inv = n == 32 ? 1.f / 32 : (n > 0 ? fastRcp(static_cast<float>(n)) : 0.f);
  const float sWt = (w1.wt - w0.wt) * inv, sWs = (w1.ws - w0.ws) * inv, sGq = (w1.gq - w0.gq) * inv;
  const float sPq = (w1.pq - w0.pq) * inv, sA = (a1 - a0) * inv;
  float wt = w0.wt, ws = w0.ws, gq = w0.gq, pq = w0.pq, am = a0;
  if (fresh_) st.e[0] = pq;
  float eq = st.e[0];
  Bl b{st.prev, st.corr, inv_};
  for (int i = 0; i < n; ++i) {
    wt += sWt, ws += sWs, gq += sGq, pq += sPq, am += sA;
    const float hq = 2.f * gq;
    float tn = ph + dt;
    if (tn >= 1.f) {  // ph > 0.5 >= pq: no crossing before the wrap
      tn -= 1.f;
      b.step(-2.f * ws - hq, tn * b.inv);
      b.cross(ph - eq - 1.f, tn - pq, hq);
    } else {
      b.cross(ph - eq, tn - pq, hq);
    }
    ph = tn, eq = pq;
    const float tri = 1.f - 4.f * fabsf(ph - 0.5f);  // naive: its kinks alias at -12 dB / octave
    const float sq = (ph < pq ? -1.f : 1.f) - (1.f - 2.f * pq);
    float y = b.emit(wt * tri + ws * (2.f * ph - 1.f) + gq * sq);
    if (fuzz) {
      lp += (y - lp) * k;
      // The overdrive of an asymmetric wave has DC: tracked out.
      const float f = tanhSoft(8.f * lp);
      dc += (f - dc) * kDc;
      y += (f - dc - y) * am;
    }
    out[i] += y * g;
  }
  t_ = t1, c_ = c1, ph_ = ph, st.e[0] = eq;
  st.prev = b.prev, st.corr = b.corr;
  st.lp = lp, st.fuzzDc = dc;
}

// Sine fold (sin of a scaled sine) to triangle fold (synth_shape.h's fold of a scaled triangle, first
// order ADAA) by c. The sine fold is averaged over two samples: the ADAA's half-sample delay.
MT_HOT void MacroVoice::renderFold(float* __restrict out, int n, float g) {
  Fold& st = s_.fd;
  if (fresh_) st.F1 = foldAntiNear(0.f);
  float t = t_, c = c_, ph = ph_, u1 = st.u1, F1 = st.F1, s1 = st.s1;
  const float dt = dt_, dtS = tStep_, dcS = cStep_, lim = st.lim;
  for (int i = 0; i < n; ++i) {
    t += dtS, c += dcS;
    ph += dt;
    ph -= ph >= 1.f ? 1.f : 0.f;
    const float e = t * lim;
    const float gain = 1.f + kFoldMax * e;
    const float s = phaseSine(ph);
    // sin(pi / 2 * gain * s): the phase gain * s / 4 offset by 2 whole turns into 0 .. 4.
    const float sf = phaseSine(2.f + 0.25f * gain * s);
    const float ys = s + (sf - s) * minf(e * kFoldFade, 1.f);
    // Triangle in phase with the sine: 1 - |4 ph - 1| up to 3 / 4 of the period, then 4 ph - 4 (no wrap).
    const float u = maxf(1.f - fabsf(4.f * ph - 1.f), 4.f * ph - 4.f) * gain;
    const float F = foldAntiNear(u);
    const float yt = adaaStep(u, u1, F, F1, foldCurve);
    u1 = u, F1 = F;
    const float ya = 0.5f * (ys + s1);
    s1 = ys;
    out[i] += (ya + (yt - ya) * c) * g;
  }
  t_ = t, c_ = c, ph_ = ph, st.u1 = u1, st.F1 = F1, st.s1 = s1;
}

// Two sines restarted every period (formant 1 at 1 / 2, formant 2 at 1 / 4 over a 3 / 4 offset) under
// a bell: a Hann rise over the first 1 / 16 of the period, a Hann fall over the rest. The bell's mean
// (0.375) is subtracted, the formants' (it depends on where they sit) tracked out.
MT_HOT void MacroVoice::renderVosim(float* __restrict out, int n, float g) {
  Formant& st = s_.fo;
  float ph = ph_, f0 = st.ph[0], f1 = st.ph[1], dc = st.dc;
  const float dt = dt_, i0 = st.inc[0], i1 = st.inc[1];
  for (int i = 0; i < n; ++i) {
    ph += dt, f0 += i0, f1 += i1;
    f0 -= f0 >= 1.f ? 1.f : 0.f;
    f1 -= f1 >= 1.f ? 1.f : 0.f;
    if (ph >= 1.f) ph -= 1.f, f0 = f1 = 0.f;
    const bool rise = ph < 1.f / 16;
    const float w = 0.5f + (rise ? -0.5f : 0.5f) *
                               phaseSine(rise ? 8.f * ph + 0.25f : (ph - 1.f / 16) * (8.f / 15) + 0.25f);
    const float y = (0.75f + 0.5f * phaseSine(f0) + 0.25f * phaseSine(f1)) * w - 0.375f;
    dc += (y - dc) * kDc;
    out[i] += (y - dc) * g;
  }
  ph_ = ph, st.ph[0] = f0, st.ph[1] = f1, st.dc = dc;
  t_ += tStep_ * n, c_ += cStep_ * n;
}

// Three formant sines restarted every period, decaying over it (x (1 - phase)), soft-clipped (tanh 2x,
// as the original's moderate overdrive); a noisy consonant adds white noise under the same decay.
MT_HOT void MacroVoice::renderVowel(float* __restrict out, int n, float g) {
  Formant& st = s_.fo;
  float ph = ph_, f0 = st.ph[0], f1 = st.ph[1], f2 = st.ph[2], dc = st.dc;
  const float dt = dt_, i0 = st.inc[0], i1 = st.inc[1], i2 = st.inc[2];
  const float a0 = st.amp[0], a1 = st.amp[1], a2 = st.amp[2];
  const bool noisy = st.noisy;  // an int test per sample (a float one waits on VMRS)
  const float nz = kNoise * (1.f / 2147483648.f);
  uint32_t r = rng_;
  for (int i = 0; i < n; ++i) {
    ph += dt, f0 += i0, f1 += i1, f2 += i2;
    f0 -= f0 >= 1.f ? 1.f : 0.f;
    f1 -= f1 >= 1.f ? 1.f : 0.f;
    f2 -= f2 >= 1.f ? 1.f : 0.f;
    if (ph >= 1.f) ph -= 1.f, f0 = f1 = f2 = 0.f;
    float x = a0 * phaseSine(f0) + a1 * phaseSine(f1) + a2 * phaseSine(f2);
    if (noisy) x += static_cast<float>(static_cast<int32_t>(xorshift(r))) * nz;
    const float y = tanhSoft(2.f * x * (1.f - ph)) * (1.f / 0.96402759f);  // / tanh 2
    dc += (y - dc) * kDc;
    out[i] += (y - dc) * g;
  }
  rng_ = r;
  ph_ = ph, st.ph[0] = f0, st.ph[1] = f1, st.ph[2] = f2, st.dc = dc;
  t_ += tStep_ * n, c_ += cStep_ * n;
}

// 2-operator phase modulation: the carrier at the pitch, the modulator at pitch x ratio, index t
// (one cycle at t 1). FB FM: the modulator's phase also takes 1 / 8 of the last output, the index is
// half and falls with pitch x ratio (FM's fades above 4.2 kHz); feedback skews the wave (DC up to
// ~0.2): tracked out. One loop for both, branch-free (FM: feedback 0, DC tracker rate 0).
MT_HOT void MacroVoice::renderFm(float* __restrict out, int n, float g, bool fb) {
  Fm& st = s_.fm;
  float t = t_, ph = ph_, mph = st.ph, prev = st.prev, dc = st.dc;
  const float dt = dt_, mdt = st.dt, dtS = tStep_;
  const float k = fb ? 0.5f * st.depth : st.depth, kfb = fb ? 0.125f : 0.f, kdc = fb ? st.kdc : 0.f;
  for (int i = 0; i < n; ++i) {
    t += dtS;
    ph += dt;
    ph -= ph >= 1.f ? 1.f : 0.f;
    mph += mdt;
    mph -= static_cast<float>(static_cast<int>(mph));
    const float m = phaseSine(mph + 1.f + kfb * prev);
    prev = phaseSine(ph + 1.f + k * t * m);
    dc += (prev - dc) * kdc;
    out[i] += (prev - dc) * g;
  }
  t_ = t, c_ += cStep_ * n, ph_ = ph, st.ph = mph, st.prev = prev, st.dc = dc;
}

}  // namespace mt
