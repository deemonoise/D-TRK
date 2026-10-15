#pragma once
#include <math.h>
#include <stdint.h>
#include "fastmath.h"
#include "hot.h"

namespace mt {

// tanh waveshaper: y = tanh(g x) / tanh(g), g = 1..8 from drive 1..127; drive 0 = bypass (bit-exact).
// A 257-point tanh table over [-8, 8] with linear interpolation: no transcendental per sample.
// Stateless (the perf FX DRIVE runs both channels through one).
class Drive {
 public:
  static constexpr int kTab = 257;
  MT_INLINE void set(uint8_t drive) {  // per control update (inlined: in IRAM with the caller)
    if (drive == drive_) return;
    drive_ = drive;
    g_ = drive ? 1.f + (drive > 127 ? 127 : drive) * (7.f / 127.f) : 0.f;
    a_ = g_ * 16.f;
    norm_ = drive ? 1.f / fastTanh(g_) : 1.f;  // per control update with an LFO on DRIVE
  }
  bool on() const { return g_ != 0.f; }
  float g() const { return g_; }
  float process(float x) const {
    if (!on()) return x;
    return lut(x * a_ + 128.f, table(), slope()) * norm_;
  }
  // tanh(u) from the table, u clamped to [-8, 8]; in a loop, with the tables fetched once.
  static MT_INLINE float tanhLut(float u) { return lut(u * 16.f + 128.f, table(), slope()); }
  static MT_INLINE float tanhLut(float u, const float* __restrict t, const float* __restrict s) {
    return lut(u * 16.f + 128.f, t, s);
  }
  static MT_INLINE void tables(const float*& t, const float*& s) {
    t = table();
    s = slope();
  }
  // x[i] = process(x[i]) over a block, drive on.
  MT_HOT MT_INLINE void processBlock(float* __restrict x, int n) const {
    const float* __restrict t = table();
    const float* __restrict s = slope();
    const float a = a_, norm = norm_;
    for (int i = 0; i < n; ++i) x[i] = lut(x[i] * a + 128.f, t, s) * norm;
  }
  // Builds the tables (257 tanhf, once): call outside the audio path to keep the first block clean.
  static void init() { table(); }

 private:
  // Table position p = (u + 8) x 16, clamped to 0 .. just under 256 (no index check after the cast).
  static MT_INLINE float lut(float p, const float* __restrict t, const float* __restrict s) {
    p = p < 0.f ? 0.f : (p > 255.99997f ? 255.99997f : p);
    const int k = static_cast<int>(p);
    return t[k] + s[k] * (p - static_cast<float>(k));
  }
  // The tables live in synth_drive.cpp; the first call builds them (out of line, in flash: the
  // build loop is not copied into every IRAM caller that inlines a lookup).
  static const float* table() {
    if (!ready_) build();
    return tab_;
  }
  static void build();
  static float* slopeMut() { return slope_; }
  static float tab_[kTab], slope_[kTab];
  static bool ready_;
  static const float* slope() { return slopeMut(); }
  uint8_t drive_ = 0;
  float g_ = 0, a_ = 0, norm_ = 1;
};

// The track insert's drive (TrackStrip): the Drive's g = 1..8 from drive 1..127 with a choice of
// curve (DriveModel): TANH (the Drive itself, bit-exact), CLIP (hard clip), FOLD (triangle
// wavefolder, period 4), RECT (full-wave rectifier of the clipped signal, octave up), ASYM (tanh
// with a bias of 0.5: even harmonics). CLIP / FOLD / RECT run first-order ADAA (antiderivative
// anti-aliasing: y = (F(u) - F(u1)) / (u - u1), half a sample of delay); RECT and ASYM end in a DC
// blocker (~10 Hz). drive 0 = bypass for every model (bit-exact). State: the last input, F of it,
// the DC blocker's two, the last output; reset() on a strip snap. A model move keeps the state
// continuous (no zeroing): ADAA restarts from the last input, the DC blocker from the last output.
class ModelDrive {
 public:
  enum : uint8_t { kTanh, kClip, kFold, kRect, kAsym, kModels };
  // Per control update: the unchanged case inline, a change out of line (hot too: an LFO on DRIVE
  // moves it every control period).
  MT_INLINE void set(uint8_t drive, uint8_t model) {
    if (model >= kModels) model = kTanh;
    if (drive == drive_ && model == model_) return;
    change(drive, model);
  }
  bool on() const { return tanh_.on(); }
  uint8_t model() const { return model_; }
  // set(drive, model) would change the sound (a model move with the drive off does not).
  bool differs(uint8_t drive, uint8_t model) const {
    if (model >= kModels) model = kTanh;
    return drive != drive_ || (drive && model != model_);
  }
  float makeup() const { return k_; }
  void reset();  // a strip snap, or the drive turning on in change() (hot, synth_drive.cpp)
  float process(float x) {
    if (!on()) return x;
    processBlock(&x, 1);
    return x;
  }
  // x[i] = process(x[i]) over a block, drive on.
  void processBlock(float* __restrict x, int n);  // MT_HOT at the definition
  // The curves without the make-up gain (CLIP, FOLD, RECT; ASYM: the bias-shifted tanh, unscaled)
  // and the antiderivatives of CLIP / FOLD / RECT.
  static float curve(uint8_t model, float u);
  static float anti(uint8_t model, float u);  // on a move / reset only (hot: reset() under an LFO)
  static constexpr float kBias = 0.5f;  // ASYM

 private:
  void change(uint8_t drive, uint8_t model);
  MT_INLINE void retune() {
    const float g = tanh_.g();
    k_ = model_ == kFold ? kFoldGain : model_ == kRect ? kRectGain : model_ == kAsym ? kAsymGain : 1.f;
    if (model_ == kAsym) {
      // Normalised by the stronger (negative) side: a full-scale input peaks at -1.
      tb_ = Drive::tanhLut(kBias);
      an_ = k_ / (tb_ + fastTanh(g - kBias));
    }
    if (model_ == kClip || model_ == kFold || model_ == kRect) F1_ = anti(model_, g * x1_);  // no step on a move
  }
  // RECT / ASYM before the DC blocker for a steady input x (per control update on a model move).
  MT_INLINE float raw(float x) const {
    const float u = tanh_.g() * x;
    if (model_ == kAsym) return (Drive::tanhLut(u + kBias) - tb_) * an_;
    const float a = fabsf(u);
    return (a < 1.f ? a : 1.f) * k_;
  }
  // Make-up gains: a 220 Hz sine at drive 64 within +-3 dB RMS of TANH. RECT loses most of its
  // energy to the DC blocker once the clip flattens it, so its notches peak up to ~2x full scale.
  static constexpr float kFoldGain = 1.3f, kRectGain = 2.2f, kAsymGain = 1.3f;
  Drive tanh_;
  uint8_t drive_ = 0, model_ = kTanh;
  float k_ = 1, tb_ = 0, an_ = 1;
  float x1_ = 0, F1_ = 0;    // the last input (before g, every model: ADAA after a move) and F(g x1)
  float y1_ = 0;             // the last output (the DC blocker's hand-off on a model move)
  float dx1_ = 0, dy1_ = 0;  // DC blocker
};

}  // namespace mt
