#pragma once
#include <stdint.h>
#include <math.h>
#include "hot.h"
#include "model.h"
#include "synth_osc.h"
#include "wt_mip.h"

namespace mt {

// Band-limited oscillator (PolyBLEP, one sample late so a discontinuity can correct the sample
// before it too). Phase 0..1. Each call returns the sample and, for the master, where it wrapped.
struct BlOut {
  float v;
  float wrap;  // phase wrap in the last interval: distance to the current sample (0..1), -1 none
};
struct BlOsc {
  float t = 0;      // phase
  float prev = 0;   // the sample being finished (n - 1)
  float corr = 0;   // correction already due for sample n
  float triv = -1;  // PWM: previous width (-1 before the first sample); reused state, no extra RAM
  void reset() { t = 0, prev = 0, corr = 0, triv = -1; }
  // sync: master wrap distance (BlOut::wrap) or -1. pw 0..1. Inlined into the block loops: a
  // call per sample (windowed register spills on the ESP32) cost more than the oscillator.
  // inv = 1 / dt, computed once per block by the caller (0 for dt 0): the edge distances multiply
  // by it (a float '/' is a __divsf3 call on the S3, two or three per period).
  MT_INLINE BlOut saw(float dt, float inv, float sync);
  MT_INLINE BlOut square(float dt, float inv, float pw, float sync);
  // dpw / edgeInv are prepared per block: width increment and 1 / (dt - dpw).
  // The returned pulse is DC-free; square() retains its raw +/-1 convention for the sub.
  MT_INLINE BlOut squarePwm(float dt, float inv, float pw, float dpw, float edgeInv, float sync);
  // Free-running SYNTH pulse after a block's first interval: width .5..95, fixed ramp direction.
  template <bool Reverse> MT_INLINE BlOut squarePwmRun(float dt, float inv, float pw, float edgeInv);
  MT_INLINE BlOut tri(float dt, float inv, float sync);
  MT_INLINE BlOut sine(float dt, float inv, float sync);
  MT_INLINE BlOut tri(float dt, float inv) { return tri(dt, inv, -1.f); }
  // Convenience (tests): inv from dt per call.
  BlOut saw(float dt, float sync) { return saw(dt, rcp(dt), sync); }
  BlOut square(float dt, float pw, float sync) { return square(dt, rcp(dt), pw, sync); }
  BlOut tri(float dt) { return tri(dt, rcp(dt)); }
  static float rcp(float dt) { return dt > 0.f ? 1.f / dt : 0.f; }

 private:
  static MT_INLINE float fracf(float x) { return x - static_cast<int>(x); }
  // Own square edges in phase interval (a, b], b - a <= 1.
  MT_INLINE void edges(float a, float b, float inv, float pw) {
    if (a < pw && b >= pw) blep(-2.f, (b - pw) * inv);
    if (b >= 1.f) {
      blep(2.f, (b - 1.f) * inv);
      if (b - 1.f >= pw) blep(-2.f, (b - 1.f - pw) * inv);
    }
  }
  MT_INLINE void pwmEdges(float a, float b, float pa, float pb, float inv, float ei, float offset) {
    if (b >= 1.f) blep(2.f, offset + (b - 1.f) * inv);
    // Relative phase crosses the moving PW edge, including its reverse crossing on a fast PWM.
    pwmEdge(a - pa, b - pb, ei, offset);
    if (b >= 1.f) pwmEdge(a - pa - 1.f, b - pb - 1.f, ei, offset);
  }
  MT_INLINE void pwmEdge(float da, float db, float ei, float offset) {
    if ((da < 0.f && db >= 0.f) || (da >= 0.f && db < 0.f)) {
      float x = offset + db * ei;
      x = x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
      blep(da < 0.f ? -2.f : 2.f, x);
    }
  }
  static MT_INLINE float triangle(float ph) { return ph < .5f ? 4.f * ph - 1.f : 3.f - 4.f * ph; }
  // PolyBLAMP for a slope change h (amplitude/sample), using the same one-sample delay as BLEP.
  MT_INLINE void blamp(float h, float x) {
    const float z = 1.f - x;
    prev += h * (1.f / 6.f) * x * x * x;
    corr += h * (1.f / 6.f) * z * z * z;
  }
  MT_INLINE void triEdges(float a, float b, float dt, float inv, float offset) {
    if (a < .5f && b >= .5f) blamp(-8.f * dt, offset + (b - .5f) * inv);
    if (b >= 1.f) {
      blamp(8.f * dt, offset + (b - 1.f) * inv);
      if (b >= 1.5f) blamp(-8.f * dt, offset + (b - 1.5f) * inv);
    }
  }
  // Step h at distance x (0..1) before sample n: corrections for n - 1 (prev) and n (corr).
  MT_INLINE void blep(float h, float x) {
    prev += 0.5f * h * x * x;
    corr += 0.5f * h * (2.f * x - x * x - 1.f);
  }
};

// 0..kWtLevels-1 (fractional): level k has 128 >> k harmonics; keeps the top one under 8 kHz.
// The nominal level is floor(pos); WtOsc adds a small hysteresis inside the Nyquist margin.
float wtLevelPos(float hz);

// Wavetable oscillator over a mip-mapped table (nullptr = silent). pos 0..63 (fractional frame).
// Phase is 0.32 fixed point: the index is a shift, no float conversions per read (ESP32 FPU
// latency). setLevel(hz) picks the mip level at control rate, with a short fade only on a change.
struct WtOsc {
  uint32_t ph = 0;
  float prev = 0;  // one sample late, aligned with BlOsc
  // Compact offsets leave room for a conditional mip transition without growing the oscillator.
  uint16_t off = 0, oldOff = 0;
  uint8_t lb = 8, oldLb = 8;
  uint8_t fade = 0, level = 0xff;  // fade: remaining samples / 32; level ff: first setLevel
  void reset() { ph = 0, prev = 0, fade = 0, level = 0xff; }
  void setLevel(float hz);
  float phase() const { return ph * (1.f / 4294967296.f); }
  static uint32_t step(float dt) {  // dt 0..0.5 -> 0.32 increment
    return static_cast<uint32_t>(static_cast<int32_t>((dt < 0.4999f ? dt : 0.4999f) * 2147483648.f)) << 1;
  }
  // Free run by dph.
  MT_INLINE float next(const int16_t* table, uint32_t dph, float pos) {
    const float r = prev;
    ph += dph;
    prev = table ? read(table, pos, ph) : 0.f;
    if (fade) --fade;
    return r;
  }
  // sync >= 0: hard reset at distance sync before this sample (phase sync * dt), else free run.
  MT_INLINE float next(const int16_t* table, float dt, float pos, float sync) {
    if (sync < 0) return next(table, step(dt), pos);
    // Mip filtering alone cannot band-limit a hard reset. Correct its value jump on both
    // sides, as BlOsc does; the extra table reads happen only at a sync edge.
    const uint32_t atReset = ph + step((1.f - sync) * dt);
    const float jump = table ? syncRead(table, pos, 0) - syncRead(table, pos, atReset) : 0.f;
    const float r = prev + 0.5f * jump * sync * sync;
    ph = step(sync * dt);
    prev = (table ? syncRead(table, pos, ph) : 0.f) + 0.5f * jump * (2.f * sync - sync * sync - 1.f);
    if (fade) --fade;
    return r;
  }

 private:
  // Share the three reset-edge reads rather than duplicating both mip interpolators
  // at every call site. Free-running samples retain the inline fast path below.
  __attribute__((noinline)) float syncRead(const int16_t* table, float pos, uint32_t phase) const;
  MT_INLINE float read(const int16_t* table, float pos, uint32_t phase) const {
    const float y = readLevel(table, pos, phase, off, lb);
    if (!fade) return y;
    const float old = readLevel(table, pos, phase, oldOff, oldLb);
    return y + (old - y) * (fade * (1.f / 32.f));
  }
  static MT_INLINE float readLevel(const int16_t* table, float pos, uint32_t phase, int offset, int bits) {
    const int f0 = static_cast<int>(pos);
    const float ff = pos - f0;
    const int f1 = f0 + 1 < kWtFrames ? f0 + 1 : f0;
    const int16_t* a = table + f0 * kWtFramePts + offset;
    const int16_t* b = table + f1 * kWtFramePts + offset;
    const uint32_t i0 = phase >> (32 - bits);
    const uint32_t i1 = (i0 + 1) & ((1u << bits) - 1);
    const float fr = static_cast<int32_t>((phase << bits) >> 8) * (1.f / 16777216.f);
    const float a0 = a[i0], b0 = b[i0];
    const float va = a0 + (a[i1] - a0) * fr;
    const float vb = b0 + (b[i1] - b0) * fr;
    return (va + (vb - va) * ff) * (1.f / kWtPeak);
  }
};
static_assert(sizeof(WtOsc) == 16, "mip transitions must not grow the voice pool");

// Control-rate inputs of a SYNTH voice (Synth::controlSyn).
struct SynParams {
  uint8_t mode[2] = {0, 0};             // SynOsc
  const int16_t* wt[2] = {nullptr, nullptr};
  float hz[2] = {220, 220};
  float shape[2] = {0, 0};              // 0..1: PW 0.5..0.95 / frame 0..63; Saw / Tri ignore it
  float mix = 0;                        // 0 = osc 1, 1 = osc 2
  bool sync = false;
  float sub = 0;                        // 0..1
  uint8_t subOct = 1;                   // 1 or 2 octaves below osc 1
  uint8_t subWave = 0;                  // SynSubWave; square is the existing default
  float noise = 0;                      // 0..1
  float fold = 0;                       // 0..1: wavefolder on the oscillator mix
  float ring = 0;                       // 0..1: crossfade of the mix to osc 1 x osc 2
};

// Output level: matches CHIP / FM at the same settings (was 0.5, about 6 dB quieter).
constexpr float kSynGain = 1.0f;

class SynVoice {
 public:
  // FREE retains phase only for a reused SYNTH voice. RANDOM uses an explicit seed,
  // so renders and previews can reproduce it. Phase offsets are turns / 128.
  void trigger(uint8_t phaseMode = 0, uint8_t phase1 = 0, uint8_t phase2 = 0,
               uint32_t seed = 0, bool keep = false);
  // Next control period of n samples: shape and mix ramp to p over it.
  void control(const SynParams& p, int n);
  void render(float* out, int n, float amp);

 private:
  // Oscillator k over n samples into dst; shape ramps from shape_[k]. sync: osc 1 wraps per
  // sample (or nullptr); wrap: this oscillator's wraps (or nullptr).
  void oscBlock(int k, float* dst, const float* sync, float* wrap, int n);
  // Wavefolder over a chunk of the oscillator mix; fold_ already includes pitch protection.
  void foldBlock(float* a, int m);
  SynParams p_;
  float shape_[2] = {0, 0}, shapeStep_[2] = {0, 0};
  float mix_ = 0, mixStep_ = 0;
  BlOsc bl_[2], sub_;
  WtOsc wt_[2];
  uint32_t noise_ = 0x12345678u;
  bool snap_ = true;  // next control() jumps to its values (after trigger)
  float fold_ = 0, foldStep_ = 0, ring_ = 0, ringStep_ = 0;
  float subGain_ = 0, noiseGain_ = 0, subStep_ = 0, noiseStep_ = 0;
};

MT_INLINE BlOut BlOsc::sine(float dt, float inv, float sync) {
  float tn = t + dt, wrap = -1.f;
  if (sync >= 0.f) {
    const float before = tn - sync * dt;
    blep(-phaseSine(before), sync);
    // A sine reset may change slope as well as value; correct that corner too.
    blamp(6.28318530718f * dt * (1.f - phaseSine(before + 0.25f)), sync);
    tn = sync * dt;
  } else if (tn >= 1.f) {
    tn -= 1.f;
    wrap = tn * inv;
  }
  t = tn;
  const BlOut r{prev, wrap};
  prev = phaseSine(t) + corr;
  corr = 0.f;
  return r;
}

MT_INLINE BlOut BlOsc::saw(float dt, float inv, float sync) {
  float wrap = -1;
  float tn = t + dt;
  if (sync >= 0) {
    // Reset at distance sync before sample n: the value just before the reset jumps to -1.
    const float te = tn - sync * dt;  // phase at the reset (unwrapped)
    if (te >= 1.f) blep(-2.f, (te - 1.f) * inv + sync);  // own wrap first
    const float before = 2.f * fracf(te) - 1.f;
    blep(-1.f - before, sync);
    tn = sync * dt;
  } else if (tn >= 1.f) {
    tn -= 1.f;
    wrap = tn * inv;
    blep(-2.f, wrap);
  }
  t = tn;
  const BlOut r{prev, wrap};
  prev = 2.f * t - 1.f + corr;
  corr = 0;
  return r;
}

MT_INLINE BlOut BlOsc::square(float dt, float inv, float pw, float sync) {
  float wrap = -1;
  const float t0 = t;
  float tn = t + dt;
  if (sync >= 0) {
    const float te = tn - sync * dt;
    edges(t0, te, inv, pw);
    const float fe = fracf(te);
    const float before = fe < pw ? 1.f : -1.f;
    blep(1.f - before, sync);  // reset to phase 0 = high
    tn = sync * dt;
    if (tn >= pw) blep(-2.f, (tn - pw) * inv);
  } else {
    edges(t0, tn, inv, pw);
    if (tn >= 1.f) {
      tn -= 1.f;
      wrap = tn * inv;
    }
  }
  t = tn;
  const BlOut r{prev, wrap};
  prev = (t < pw ? 1.f : -1.f) + corr;
  corr = 0;
  return r;
}

MT_INLINE BlOut BlOsc::squarePwm(float dt, float inv, float pw, float dpw, float edgeInv, float sync) {
  const bool fresh = triv < 0.f;
  const float pa = fresh ? pw : triv;
  const float delta = pw - pa;
  float ei = edgeInv;
  // A control-rate slope change leaves one interval at the preceding slope. Only that boundary
  // needs a reciprocal here; the normal PWM ramp uses its block's precomputed edgeInv.
  if (fabsf(delta - dpw) > 1e-6f) ei = dt != delta ? 1.f / (dt - delta) : 0.f;
  float tn = t + dt, wrap = -1.f;
  if (sync >= 0.f) {
    const float te = tn - sync * dt;
    const float pe = pw - sync * delta;
    pwmEdges(t, te, pa, pe, inv, ei, sync);
    const float before = fracf(te) < pe ? 1.f : -1.f;
    blep(1.f - before, sync);
    tn = sync * dt;
    pwmEdges(0.f, tn, pe, pw, inv, ei, 0.f);
  } else {
    pwmEdges(t, tn, pa, pw, inv, ei, 0.f);
    if (tn >= 1.f) { tn -= 1.f; wrap = tn * inv; }
  }
  t = tn;
  const BlOut r{prev - (fresh ? 0.f : 2.f * pa - 1.f), wrap};
  prev = (t < pw ? 1.f : -1.f) + corr;
  corr = 0.f;
  triv = pw;
  return r;
}

template <bool Reverse>
MT_INLINE BlOut BlOsc::squarePwmRun(float dt, float inv, float pw, float edgeInv) {
  float tn = t + dt, wrap = -1.f;
  const float da = t - triv, db = tn - pw;
  if (Reverse ? (da >= 0.f && db < 0.f) : (da < 0.f && db >= 0.f))
    blep(Reverse ? 2.f : -2.f, db * edgeInv);
  if (tn >= 1.f) {
    tn -= 1.f;
    wrap = tn * inv;
    blep(2.f, wrap);
    // dt < .5 and pw >= .5: the next cycle cannot also cross PW in this sample.
  }
  t = tn;
  const BlOut r{prev - (2.f * triv - 1.f), wrap};
  prev = (tn < pw ? 1.f : -1.f) + corr;
  corr = 0.f;
  triv = pw;
  return r;
}

MT_INLINE BlOut BlOsc::tri(float dt, float inv, float sync) {
  float tn = t + dt, wrap = -1.f;
  if (sync >= 0.f) {
    const float te = tn - sync * dt;
    triEdges(t, te, dt, inv, sync);
    const float before = fracf(te);
    blep(-1.f - triangle(before), sync);
    if (before >= .5f) blamp(8.f * dt, sync);  // reset changes falling slope to rising
    tn = sync * dt;
    triEdges(0.f, tn, dt, inv, 0.f);
  } else {
    triEdges(t, tn, dt, inv, 0.f);
    if (tn >= 1.f) { tn -= 1.f; wrap = tn * inv; }
  }
  t = tn;
  const BlOut r{prev, wrap};
  prev = triangle(t) + corr;
  corr = 0.f;
  return r;
}

}  // namespace mt
