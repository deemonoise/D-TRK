#pragma once
#include <stdint.h>
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
  float triv = -1;  // Tri: leaky integral of the square
  void reset() { t = 0, prev = 0, corr = 0, triv = -1; }
  // sync: master wrap distance (BlOut::wrap) or -1. pw 0..1. Inlined into the block loops: a
  // call per sample (windowed register spills on the ESP32) cost more than the oscillator.
  MT_INLINE BlOut saw(float dt, float sync);
  MT_INLINE BlOut square(float dt, float pw, float sync);
  MT_INLINE BlOut tri(float dt);  // no sync

 private:
  static MT_INLINE float fracf(float x) { return x - static_cast<int>(x); }
  // Own square edges in phase interval (a, b], b - a <= 1.
  MT_INLINE void edges(float a, float b, float dt, float pw) {
    if (a < pw && b >= pw) blep(-2.f, (b - pw) / dt);
    if (b >= 1.f) {
      blep(2.f, (b - 1.f) / dt);
      if (b - 1.f >= pw) blep(-2.f, (b - 1.f - pw) / dt);
    }
  }
  // Step h at distance x (0..1) before sample n: corrections for n - 1 (prev) and n (corr).
  MT_INLINE void blep(float h, float x) {
    prev += 0.5f * h * x * x;
    corr += 0.5f * h * (2.f * x - x * x - 1.f);
  }
};

// 0..kWtLevels-1 (fractional): level k has 128 >> k harmonics; keeps the top one under 8 kHz.
// The oscillator plays level floor(pos): its top harmonic stays under Nyquist (one octave of margin).
float wtLevelPos(float hz);

// Wavetable oscillator over a mip-mapped table (nullptr = silent). pos 0..63 (fractional frame).
// Phase is 0.32 fixed point: the index is a shift, no float conversions per read (ESP32 FPU
// latency). setLevel(hz) picks the mip level (log2f): call it at control rate, not per sample.
struct WtOsc {
  uint32_t ph = 0;
  float prev = 0;  // one sample late, aligned with BlOsc
  int off = 0;     // level offset in a frame
  int lb = 8;      // log2 of the level length
  void reset() { ph = 0, prev = 0; }
  void setLevel(float hz);
  float phase() const { return ph * (1.f / 4294967296.f); }
  static uint32_t step(float dt) {  // dt 0..0.5 -> 0.32 increment
    return static_cast<uint32_t>(static_cast<int32_t>((dt < 0.4999f ? dt : 0.4999f) * 2147483648.f)) << 1;
  }
  // Free run by dph.
  MT_INLINE float next(const int16_t* table, uint32_t dph, float pos) {
    const float r = prev;
    ph += dph;
    prev = table ? read(table, pos) : 0.f;
    return r;
  }
  // sync >= 0: hard reset at distance sync before this sample (phase sync * dt), else free run.
  MT_INLINE float next(const int16_t* table, float dt, float pos, float sync) {
    if (sync < 0) return next(table, step(dt), pos);
    const float r = prev;
    ph = step(sync * dt);
    prev = table ? read(table, pos) : 0.f;
    return r;
  }

 private:
  MT_INLINE float read(const int16_t* table, float pos) const {
    const int f0 = static_cast<int>(pos);
    const float ff = pos - f0;
    const int f1 = f0 + 1 < kWtFrames ? f0 + 1 : f0;
    const int16_t* a = table + f0 * kWtFramePts + off;
    const int16_t* b = table + f1 * kWtFramePts + off;
    const uint32_t i0 = ph >> (32 - lb);
    const uint32_t i1 = (i0 + 1) & ((1u << lb) - 1);
    const float fr = static_cast<int32_t>((ph << lb) >> 8) * (1.f / 16777216.f);
    const float a0 = a[i0], b0 = b[i0];
    const float va = a0 + (a[i1] - a0) * fr;
    const float vb = b0 + (b[i1] - b0) * fr;
    return (va + (vb - va) * ff) * (1.f / kWtPeak);
  }
};

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
  float noise = 0;                      // 0..1
};

// Output level: matches CHIP / FM at the same settings (was 0.5, about 6 dB quieter).
constexpr float kSynGain = 1.0f;

class SynVoice {
 public:
  void trigger();  // phases and ramps from zero (not legato)
  // Next control period of n samples: shape and mix ramp to p over it.
  void control(const SynParams& p, int n);
  void render(float* out, int n, float amp);

 private:
  // Oscillator k over n samples into dst; shape ramps from shape_[k]. sync: osc 1 wraps per
  // sample (or nullptr); wrap: this oscillator's wraps (or nullptr).
  void oscBlock(int k, float* dst, const float* sync, float* wrap, int n);
  SynParams p_;
  float shape_[2] = {0, 0}, shapeStep_[2] = {0, 0};
  float mix_ = 0, mixStep_ = 0;
  BlOsc bl_[2], sub_;
  WtOsc wt_[2];
  uint32_t noise_ = 0x12345678u;
  bool snap_ = true;  // next control() jumps to its values (after trigger)
};

MT_INLINE BlOut BlOsc::saw(float dt, float sync) {
  float wrap = -1;
  float tn = t + dt;
  if (sync >= 0) {
    // Reset at distance sync before sample n: the value just before the reset jumps to -1.
    const float te = tn - sync * dt;  // phase at the reset (unwrapped)
    if (te >= 1.f) blep(-2.f, (te - 1.f) / dt + sync);  // own wrap first
    const float before = 2.f * fracf(te) - 1.f;
    blep(-1.f - before, sync);
    tn = sync * dt;
  } else if (tn >= 1.f) {
    tn -= 1.f;
    wrap = tn / dt;
    blep(-2.f, wrap);
  }
  t = tn;
  const BlOut r{prev, wrap};
  prev = 2.f * t - 1.f + corr;
  corr = 0;
  return r;
}

MT_INLINE BlOut BlOsc::square(float dt, float pw, float sync) {
  float wrap = -1;
  const float t0 = t;
  float tn = t + dt;
  if (sync >= 0) {
    const float te = tn - sync * dt;
    edges(t0, te, dt, pw);
    const float fe = fracf(te);
    const float before = fe < pw ? 1.f : -1.f;
    blep(1.f - before, sync);  // reset to phase 0 = high
    tn = sync * dt;
    if (tn >= pw) blep(-2.f, (tn - pw) / dt);
  } else {
    edges(t0, tn, dt, pw);
    if (tn >= 1.f) {
      tn -= 1.f;
      wrap = tn / dt;
    }
  }
  t = tn;
  const BlOut r{prev, wrap};
  prev = (t < pw ? 1.f : -1.f) + corr;
  corr = 0;
  return r;
}

MT_INLINE BlOut BlOsc::tri(float dt) {
  const BlOut q = square(dt, 0.5f, -1);
  triv = triv * 0.9995f + q.v * 4.f * dt;
  return {triv, q.wrap};
}

}  // namespace mt
