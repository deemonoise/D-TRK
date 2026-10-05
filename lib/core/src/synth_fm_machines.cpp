#include "synth_fm_machines.h"
#include <math.h>
#include "synth_osc.h"

namespace mt {
namespace {

constexpr float kTopHz = 13000.f;  // highest partial a machine makes (the core drops what aliases)
constexpr float kFbMax = 1.7f;     // carrier feedback peak, rad: above ~2 the saw turns to noise

float u(float v) { return v <= 0 ? 0 : (v >= 127 ? 1 : v * (1.f / 127.f)); }
float lerp(float a, float b, float x) { return a + (b - a) * x; }
float expMap(float lo, float hi, float x) { return lo * powf(hi / lo, x); }
float decayMs(float d) { return expMap(5.f, 4000.f, u(d)); }  // as fmDecayMs, fractional
float toC4(float pitch) { return exp2f((pitch - 60.f) * (1.f / 12.f)); }
// Interpolated entry of a table at x 0..1.
float tableAt(const float* t, int n, float x) {
  const float pos = x * (n - 1);
  int a = static_cast<int>(pos);
  if (a > n - 2) a = n - 2;
  return lerp(t[a], t[a + 1], pos - a);
}
// Decay of a part that, under the one-shot amp decay ampMs, makes it last ms in total
// (decays multiply: rates add). 0 = the amp decay alone (ms >= ampMs).
float within(float ms, float ampMs) {
  if (ms <= 0 || ms >= ampMs) return 0;
  return 1.f / (1.f / ms - 1.f / ampMs);
}
// Pitch sweep, semitones, capped so that topHz x the sweep stays below kTopHz.
float sweepCap(float semis, float topHz) {
  if (topHz <= 0) return semis;
  const float cap = 12.f * log2f(kTopHz / topHz);
  return semis < cap ? semis : (cap > 0 ? cap : 0);
}
// Carrier feedback of an operator at hz: less at high pitch (aliasing).
float fbLimit(float hz) {
  const float m = 0.5f * fmMaxIndex(hz);
  return m < kFbMax ? m : kFbMax;
}
// Feedback as fractions of fbLimit: COLOR the held part (floor with SWEEP, so SWEEP always acts),
// SWEEP a decaying boost towards the limit. Peak = held x (1 + fbEnv) <= 1.
float fbHeld(float c, float w) {
  const float f = 0.1f * w;
  return c > f ? c : f;
}
float fbEnvOf(float c, float w) {
  const float b = fbHeld(c, w);
  return b > 0 ? (1.f - b) * w / b : 0;
}

FmOp carrier(float ratio, float level, float decay = 0, float fb = 0) { return {ratio, level, 0, decay, fb}; }
FmOp modulator(float ratio, float index, float decay, float sustain = 0) { return {ratio, index, sustain, decay, 0}; }

void kick(const float* m, float pitch, FmParams& p) {
  const float c = u(m[kMacCol]), s = u(m[kMacShp]);
  p.alg = FmAlg::Stack;
  p.hz = 55.f * toC4(pitch);
  // SHAPE: modulator ratio 0.5..2 ("body"), upper half adds carrier feedback ("909").
  const float ratio = s < 0.5f ? lerp(0.5f, 1, s * 2) : lerp(1, 2, (s - 0.5f) * 2);
  p.op[0] = carrier(1, 1, 0, s > 0.5f ? (s - 0.5f) * 2.f : 0);
  p.op[1] = modulator(ratio, c * c * 6, lerp(15, 150, c));
  p.pitchEnv = sweepCap(u(m[kMacSwp]) * 48, p.hz * (ratio > 1 ? ratio : 1));
  p.pitchMs = expMap(5, 200, u(m[kMacCon]));
  p.ampMs = decayMs(m[kMacDec]);
}

void snare(const float* m, float pitch, FmParams& p) {
  const float c = u(m[kMacCol]), s = u(m[kMacShp]);
  const float amp = decayMs(m[kMacDec]);
  const float tone = 1.f - c;
  const float len = lerp(0.3f, 1.5f, u(m[kMacCon]));
  p.ampMs = amp * (len > 1 ? len : 1);
  p.alg = FmAlg::TwoPairs;
  p.hz = 180.f * toC4(pitch);
  p.op[0] = carrier(1, tone * 0.8f, within(amp, p.ampMs));
  p.op[1] = modulator(1, 0.6f, amp * 0.5f);
  p.op[2] = carrier(1.6f, tone * 0.4f, within(amp * 0.6f, p.ampMs));
  p.pitchEnv = u(m[kMacSwp]) * 12;
  p.pitchMs = 30;
  p.noise = c;
  p.noiseMs = within(amp * len, p.ampMs);
  if (s < 0.5f) {
    p.filterMode = Svf::Mode::Bp;
    p.filterHz = expMap(800, 4000, s * 2);
    p.filterQ = 1;
  } else {
    p.filterMode = Svf::Mode::Hp;
    p.filterHz = expMap(2000, 8000, (s - 0.5f) * 2);
  }
}

void metal(const float* m, float pitch, FmParams& p) {
  // Carrier 1..3 and modulator ratios: bell, cowbell, gong, cymbal.
  static const float kSets[kFmOps][4] = {
      {1.f, 1.f, 1.f, 1.f}, {2.76f, 1.48f, 1.19f, 1.34f}, {5.40f, 2.20f, 1.53f, 1.87f}, {3.50f, 1.41f, 0.71f, 2.41f}};
  const float s = u(m[kMacShp]);
  const float amp = decayMs(m[kMacDec]);
  p.alg = FmAlg::OneToThree;
  p.hz = 400.f * toC4(pitch);
  const float lv[3] = {0.4f, 0.3f, 0.25f}, dl[3] = {1.f, 0.8f, 0.6f};
  for (int i = 0; i < 3; ++i) p.op[i] = carrier(tableAt(kSets[i], 4, s), lv[i], within(amp * dl[i], amp));
  p.op[3] = modulator(tableAt(kSets[3], 4, s), u(m[kMacCol]) * 5, amp * lerp(1, 0.1f, u(m[kMacCon])));
  p.pitchEnv = u(m[kMacSwp]) * 5;
  p.pitchMs = 40;
  p.ampMs = amp;
}

void perc(const float* m, float pitch, FmParams& p) {
  static const float kRatios[8] = {1.f, 1.5f, 2.f, 2.76f, 3.5f, 4.2f, 5.4f, 7.f};
  const float c = u(m[kMacCol]);
  const float amp = decayMs(m[kMacDec]);
  const float ratio = tableAt(kRatios, 8, u(m[kMacShp]));
  p.alg = FmAlg::Stack;
  p.hz = 200.f * toC4(pitch);
  p.op[0] = carrier(1, 1);
  p.op[1] = modulator(ratio, c * c * 8, amp * 0.5f);
  p.pitchEnv = sweepCap(u(m[kMacSwp]) * 24, p.hz * ratio);
  p.pitchMs = expMap(5, 200, u(m[kMacCon]));
  p.ampMs = amp;
}

void tone(const float* m, float pitch, FmParams& p) {
  const float c = u(m[kMacCol]), w = u(m[kMacSwp]);
  const float envMs = expMap(20, 2000, u(m[kMacCon]));
  // Index = COLOR part (held) + SWEEP part (decays: pluck).
  auto mod = [&](float ratio, float scale) {
    const float held = c * scale, pluck = w * scale * 2;
    const float sum = held + pluck;
    return modulator(ratio, sum, envMs, sum > 0 ? held / sum : 0);
  };
  p.oneShot = false;
  p.alg = FmAlg::Stack;
  p.hz = noteHz(pitch);
  p.op[0] = carrier(1, 1);
  int zone = static_cast<int>(u(m[kMacShp]) * 5);
  if (zone > 4) zone = 4;
  switch (zone) {
    case 0: p.op[1] = mod(1, 1.5f); break;  // sine, brighter with COLOR
    case 1:                                 // saw-like: feedback
      p.op[0].fb = fbLimit(p.hz) * fbHeld(c, w);
      p.fbEnv = fbEnvOf(c, w);
      p.fbMs = envMs;
      break;
    case 2: p.op[1] = mod(2, 3); break;  // square-like: odd harmonics
    case 3:                              // stack
      p.op[1] = mod(1, 2);
      p.op[2] = mod(2, 1.5f);
      break;
    default: p.op[1] = mod(3.5f, 3); break;  // bell
  }
}

const int8_t kChords[kFmChords][kFmOps] = {
    {0, 4, 7, 12}, {0, 3, 7, 12}, {0, 2, 7, 12}, {0, 5, 7, 12}, {0, 4, 7, 10},  {0, 3, 7, 10},
    {0, 4, 7, 11}, {0, 3, 6, 9},  {0, 4, 8, 12}, {0, 7, 12, 19}, {-12, 0, 12, 24}, {0, 3, 10, 14}};
const char* const kChordNames[kFmChords] = {"MAJ", "MIN", "SUS2", "SUS4", "7", "MI7",
                                            "MAJ7", "DIM", "AUG", "5", "OCT", "MI9"};

int chordIndex(float shape) {
  const int i = static_cast<int>(u(shape) * kFmChords);
  return i >= kFmChords ? kFmChords - 1 : i;
}

void chord(const float* m, float pitch, FmParams& p) {
  const float c = u(m[kMacCol]), w = u(m[kMacSwp]);
  const int8_t* iv = kChords[chordIndex(m[kMacShp])];
  p.oneShot = false;
  p.alg = FmAlg::Additive;
  p.hz = noteHz(pitch);
  const float held = fbHeld(c, w);
  for (int i = 0; i < kFmOps; ++i) {
    const float ratio = exp2f(iv[i] * (1.f / 12.f));
    p.op[i] = carrier(ratio, 0.3f, 0, fbLimit(p.hz * ratio) * held);
  }
  p.fbEnv = fbEnvOf(c, w);
  p.fbMs = expMap(20, 2000, u(m[kMacCon]));
}

void clap(const float* m, float pitch, FmParams& p) {
  const float s = u(m[kMacShp]);
  p.noise = 1;
  p.bursts = static_cast<uint8_t>(2 + static_cast<int>(s * 3.99f));  // 2..5, looser with SHAPE
  p.burstMs = lerp(5, 15, s);
  p.tail = lerp(0.2f, 1, u(m[kMacCon]));
  p.filterMode = Svf::Mode::Bp;
  p.filterHz = expMap(600, 3000, u(m[kMacCol])) * toC4(pitch);
  p.filterQ = lerp(0.7f, 6, u(m[kMacSwp]));
  p.noiseMs = 0;                  // the tail is held: the amp decay, which starts after the
  p.ampMs = decayMs(m[kMacDec]);  // bursts, shapes it alone
}

void hat(const float* m, float pitch, FmParams& p) {
  static const float kRatios[kFmOps] = {1.f, 1.342f, 1.756f, 2.15f};
  const float s = u(m[kMacShp]);
  const float amp = decayMs(m[kMacDec]);
  const float env = within(amp * lerp(0.25f, 1, u(m[kMacCon])), amp);
  float spread = lerp(0.5f, 1.5f, u(m[kMacSwp]));
  p.alg = FmAlg::Additive;
  p.hz = 3500.f * toC4(pitch);
  // High pitch: the top partial (1 + 1.15 x spread) stays below kTopHz, narrowing first.
  if (p.hz > kTopHz) p.hz = kTopHz;
  const float maxSpread = (kTopHz / p.hz - 1.f) * (1.f / (kRatios[kFmOps - 1] - 1.f));
  if (spread > maxSpread) spread = maxSpread;
  for (int i = 0; i < kFmOps; ++i) {
    const float ratio = 1 + (kRatios[i] - 1) * spread;
    const float fb = fbLimit(p.hz * ratio);
    p.op[i] = carrier(ratio, (1 - s) * 0.25f, env, fb < 1.2f ? fb : 1.2f);
  }
  p.noise = s;
  p.noiseMs = env;
  p.filterMode = Svf::Mode::Hp;
  p.filterHz = expMap(3000, 12000, u(m[kMacCol]));
  p.filterAll = true;
  p.ampMs = amp;
}

}  // namespace

const char* fmChordName(uint8_t shape) { return kChordNames[chordIndex(shape)]; }

void fmMachine(uint8_t machine, const float mac[kFmMacros], float pitch, FmParams& out) {
  out = FmParams();
  switch (static_cast<FmMachine>(machine)) {
    case FmMachine::Snare: snare(mac, pitch, out); break;
    case FmMachine::Metal: metal(mac, pitch, out); break;
    case FmMachine::Perc: perc(mac, pitch, out); break;
    case FmMachine::Tone: tone(mac, pitch, out); break;
    case FmMachine::Chord: chord(mac, pitch, out); break;
    case FmMachine::Clap: clap(mac, pitch, out); break;
    case FmMachine::Hat: hat(mac, pitch, out); break;
    default: kick(mac, pitch, out); break;
  }
}

}  // namespace mt
