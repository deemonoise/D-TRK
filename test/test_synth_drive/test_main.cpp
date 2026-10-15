#include <math.h>
#include <stdio.h>
#include <initializer_list>
#include <unity.h>
#include "model.h"
#include "synth_drive.h"
#include "synth_shape.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_drive_zero_is_identity() {
  Drive d;
  d.set(0);
  TEST_ASSERT_FALSE(d.on());
  for (float x : {-1.5f, -0.3f, 0.f, 0.25f, 1.f, 7.f}) TEST_ASSERT_TRUE(d.process(x) == x);
  d.set(90);
  d.set(0);  // back off: bypass again
  TEST_ASSERT_TRUE(d.process(0.37f) == 0.37f);
}

void test_drive_saturates_and_is_monotonic() {
  Drive d;
  d.set(127);
  TEST_ASSERT_TRUE(d.on());
  float prev = -2;
  for (int i = -40; i <= 40; ++i) {
    const float y = d.process(i * 0.1f);
    TEST_ASSERT_TRUE(y >= prev - 1e-6f);
    prev = y;
    TEST_ASSERT_TRUE(fabsf(y) <= 1.0001f);
  }
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.f, d.process(1.f));  // normalized: tanh(g) / tanh(g)
  TEST_ASSERT_FLOAT_WITHIN(0.01f, tanhf(0.8f) / tanhf(8.f), d.process(0.1f));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -1.f, d.process(-50.f));  // clamped input
}

void test_drive_low_is_gentle() {
  Drive d;
  d.set(1);  // g just above 1: close to tanh(x) / tanh(1)
  TEST_ASSERT_FLOAT_WITHIN(0.01f, tanhf(0.5f * (1.f + 7.f / 127.f)) / tanhf(1.f + 7.f / 127.f), d.process(0.5f));
}

// The feature-1 Drive, verbatim: the reference the faster block path must match.
static float refTanh(uint8_t drive, float x) {
  if (!drive) return x;
  const float g = 1.f + (drive > 127 ? 127 : drive) * (7.f / 127.f);
  float u = x * g;
  u = u < -8.f ? -8.f : (u > 8.f ? 8.f : u);
  const float p = (u + 8.f) * 16.f;
  int i = static_cast<int>(p);
  if (i > 255) i = 255;
  const float f = p - i;
  const float a = tanhf(-8.f + i * (1.f / 16.f)), b = tanhf(-8.f + (i + 1) * (1.f / 16.f));
  return (a + (b - a) * f) / fastTanh(g);
}

static uint32_t rng = 12345;
static float frand() {  // -1 .. 1
  rng = rng * 1664525u + 1013904223u;
  return static_cast<float>(rng >> 8) * (2.f / 16777216.f) - 1.f;
}

constexpr int kModels = static_cast<int>(DriveModel::Count);
constexpr float kPi = 3.14159265f;
constexpr float kFs = 32000.f;

void test_drive_block_matches_feature1() {
  for (int drive : {1, 20, 64, 100, 127}) {
    Drive d;
    d.set(static_cast<uint8_t>(drive));
    float x[256];
    for (int i = 0; i < 256; ++i) x[i] = i < 200 ? frand() * 1.5f : (i - 228) * 0.5f;  // incl. far outside
    float y[256];
    for (int i = 0; i < 256; ++i) y[i] = x[i];
    d.processBlock(y, 256);
    for (int i = 0; i < 256; ++i) {
      TEST_ASSERT_FLOAT_WITHIN(2e-6f, refTanh(static_cast<uint8_t>(drive), x[i]), y[i]);
      TEST_ASSERT_FLOAT_WITHIN(2e-6f, refTanh(static_cast<uint8_t>(drive), x[i]), d.process(x[i]));
    }
  }
}

void test_model_drive_zero_bypasses_every_model() {
  for (int m = 0; m < kModels; ++m) {
    ModelDrive d;
    d.set(0, static_cast<uint8_t>(m));
    TEST_ASSERT_FALSE(d.on());
    float x[64], y[64];
    for (int i = 0; i < 64; ++i) x[i] = y[i] = frand() * 2.f;
    d.processBlock(y, 64);
    for (int i = 0; i < 64; ++i) TEST_ASSERT_TRUE(x[i] == y[i]);
    d.set(80, static_cast<uint8_t>(m));
    TEST_ASSERT_TRUE(d.on());
  }
}

void test_model_tanh_is_the_plain_drive() {
  for (int drive : {1, 64, 127}) {
    ModelDrive md;
    Drive d;
    md.set(static_cast<uint8_t>(drive), 0);
    d.set(static_cast<uint8_t>(drive));
    float a[128], b[128];
    for (int i = 0; i < 128; ++i) a[i] = b[i] = frand() * 1.3f;
    md.processBlock(a, 128);
    d.processBlock(b, 128);
    for (int i = 0; i < 128; ++i) TEST_ASSERT_TRUE(a[i] == b[i]);
  }
}

void test_models_bounded_finite_silent() {
  for (int m = 0; m < kModels; ++m)
    for (int drive : {1, 30, 64, 100, 127}) {
      ModelDrive d;
      d.set(static_cast<uint8_t>(drive), static_cast<uint8_t>(m));
      float x[512];
      for (int i = 0; i < 512; ++i)  // a sweep over -4..4, then noise
        x[i] = i < 256 ? -4.f + i * (8.f / 255.f) : frand() * (i & 1 ? 1.f : 4.f);
      d.processBlock(x, 512);
      for (int i = 0; i < 512; ++i) {
        TEST_ASSERT_TRUE(isfinite(x[i]));
        TEST_ASSERT_TRUE(fabsf(x[i]) <= 1.6f * d.makeup());  // TANH at drive 1: tanh(4 g) / tanh(g)
      }
      // A full-scale sine stays within ~full scale (x the make-up gain).
      d.reset();
      float mx = 0;
      for (int i = 0; i < 6400; ++i) {
        float s = sinf(2 * kPi * 220.f * i / kFs);
        s = d.process(s);
        if (i > 3200 && fabsf(s) > mx) mx = fabsf(s);
      }
      TEST_ASSERT_TRUE(mx <= 1.05f * d.makeup());
      d.reset();
      float z[64] = {};
      d.processBlock(z, 64);
      for (float v : z) TEST_ASSERT_TRUE(v == 0.f);
    }
}

void test_adaa_antiderivatives_match_the_curves() {
  for (int m : {1, 2, 3}) {
    for (int k = 0; k < 200; ++k) {
      const float a = frand() * 10.f, b = frand() * 10.f;
      const int n = 4000;
      double sum = 0;
      for (int i = 0; i < n; ++i) {  // midpoint rule
        const double u = a + (b - a) * (i + 0.5) / n;
        sum += ModelDrive::curve(static_cast<uint8_t>(m), static_cast<float>(u));
      }
      sum *= (b - a) / n;
      const float got = ModelDrive::anti(static_cast<uint8_t>(m), b) - ModelDrive::anti(static_cast<uint8_t>(m), a);
      TEST_ASSERT_FLOAT_WITHIN(2e-3f, static_cast<float>(sum), got);
    }
  }
}

void test_adaa_constant_input_gives_the_curve() {
  for (int m : {1, 2, 3}) {
    ModelDrive d;
    d.set(64, static_cast<uint8_t>(m));
    const float g = 1.f + 64 * (7.f / 127.f);
    float y = 0;
    for (int i = 0; i < 4; ++i) y = d.process(0.3f);
    if (m != 3) TEST_ASSERT_FLOAT_WITHIN(1e-4f, ModelDrive::curve(static_cast<uint8_t>(m), 0.3f * g) * d.makeup(), y);
  }
}

// |DFT| of one bin (integer number of cycles in n).
static float bin(const float* x, int n, float hz) {
  double re = 0, im = 0;
  for (int i = 0; i < n; ++i) {
    const double w = 2 * 3.14159265358979 * hz * i / kFs;
    re += x[i] * cos(w);
    im += x[i] * sin(w);
  }
  return static_cast<float>(sqrt(re * re + im * im) / n);
}

static float db(float a, float b) { return 20.f * log10f(a / b); }

void test_adaa_clip_aliases_less_than_naive() {
  const int n = 3200;
  static float a[n], b[n];
  ModelDrive d;
  d.set(100, 1);
  const float g = 1.f + 100 * (7.f / 127.f);
  for (int i = 0; i < n; ++i) a[i] = b[i] = 0.8f * sinf(2 * kPi * 5000.f * i / kFs);
  d.processBlock(a, n);
  for (int i = 0; i < n; ++i) b[i] = ModelDrive::curve(1, b[i] * g);
  // 1 kHz: the 13th harmonic (65 kHz) folded back; nothing harmonic lands there.
  TEST_ASSERT_TRUE(db(bin(b, n, 1000.f), bin(a, n, 1000.f)) >= 10.f);
  // The fundamental stays.
  TEST_ASSERT_FLOAT_WITHIN(1.5f, 0.f, db(bin(a, n, 5000.f), bin(b, n, 5000.f)));
}

void test_fold_shape() {
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.f, ModelDrive::curve(2, 0.f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.f, ModelDrive::curve(2, 1.f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.f, ModelDrive::curve(2, 2.f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -1.f, ModelDrive::curve(2, 3.f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, ModelDrive::curve(2, 0.5f));
  for (int k = 0; k < 100; ++k) {
    const float u = frand() * 20.f;
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -ModelDrive::curve(2, u), ModelDrive::curve(2, -u));
  }
}

// One second of a 220 Hz sine through the model, the last 0.5 s (110 cycles) analysed.
static float* run(int model, int drive, float amp, float hz = 220.f) {
  static float x[32000];
  ModelDrive d;
  d.set(static_cast<uint8_t>(drive), static_cast<uint8_t>(model));
  for (int i = 0; i < 32000; ++i) x[i] = amp * sinf(2 * kPi * hz * i / kFs);
  d.processBlock(x, 32000);
  return x + 16000;
}

static float mean(const float* x, int n) {
  double s = 0;
  for (int i = 0; i < n; ++i) s += x[i];
  return static_cast<float>(s / n);
}

static float rms(const float* x, int n) {
  double s = 0;
  for (int i = 0; i < n; ++i) s += x[i] * x[i];
  return static_cast<float>(sqrt(s / n));
}

void test_rect_octave_up_no_dc() {
  const float* y = run(3, 64, 0.5f);
  TEST_ASSERT_TRUE(bin(y, 16000, 440.f) > 4.f * bin(y, 16000, 220.f));
  TEST_ASSERT_TRUE(fabsf(mean(y, 16000)) < 1e-3f);
}

void test_asym_even_harmonic_no_dc() {
  const float* y = run(4, 64, 0.5f);
  TEST_ASSERT_TRUE(db(bin(y, 16000, 440.f), bin(y, 16000, 220.f)) > -40.f);
  TEST_ASSERT_TRUE(fabsf(mean(y, 16000)) < 1e-3f);
  // TANH: odd only.
  y = run(0, 64, 0.5f);
  TEST_ASSERT_TRUE(db(bin(y, 16000, 440.f), bin(y, 16000, 220.f)) < -60.f);
}

void test_models_loudness_near_tanh() {
  for (float amp : {0.5f}) {
    const float ref = rms(run(0, 64, amp), 16000);
    for (int m = 1; m < kModels; ++m) {
      const float l = rms(run(m, 64, amp), 16000);
      TEST_ASSERT_FLOAT_WITHIN(3.f, 0.f, db(l, ref));
    }
  }
}

// A drive move at a control boundary: no step bigger than the memoryless curve's own jump plus the
// signal's largest step (ADAA must not spike: F of the last input follows the new g).
void test_drive_jump_is_smooth() {
  for (int m = 1; m < kModels; ++m) {
    ModelDrive d;
    d.set(20, static_cast<uint8_t>(m));
    float x[4096];
    for (int i = 0; i < 4096; ++i) x[i] = 0.5f * sinf(2 * kPi * 220.f * i / kFs);
    const float g1 = 1.f + 20 * (7.f / 127.f), g2 = 1.f + 100 * (7.f / 127.f);
    const float curveJump =
        d.makeup() * fabsf(ModelDrive::curve(static_cast<uint8_t>(m), x[2048] * g2) -
                           ModelDrive::curve(static_cast<uint8_t>(m), x[2047] * g1));
    d.processBlock(x, 2048);
    d.set(100, static_cast<uint8_t>(m));
    d.processBlock(x + 2048, 2048);
    float mx = 0;
    for (int i = 2200; i < 4096; ++i) mx = fmaxf(mx, fabsf(x[i] - x[i - 1]));
    char msg[64];
    snprintf(msg, sizeof(msg), "model %d jump %f max %f", m, fabsf(x[2048] - x[2047]), mx);
    TEST_ASSERT_TRUE_MESSAGE(fabsf(x[2048] - x[2047]) <= curveJump * 1.6f + mx, msg);
  }
}


// A model move mid-signal: the first sample of the new model jumps no further than the two curves'
// own steady-state outputs differ (plus the signal's step): the ADAA state follows the last input
// whatever the old model, the DC blocker is not zeroed.
void test_model_switch_is_smooth() {
  constexpr int kN = 16384;
  static float sig[kN], ref[kModels][kN], y[kN];
  for (int i = 0; i < kN; ++i) sig[i] = 0.6f * sinf(2 * kPi * 220.f * i / kFs);
  for (int m = 0; m < kModels; ++m) {
    ModelDrive d;
    d.set(64, static_cast<uint8_t>(m));
    for (int i = 0; i < kN; ++i) ref[m][i] = sig[i];
    for (int i = 0; i < kN; i += 32) d.processBlock(ref[m] + i, 32);
  }
  float step = 0;
  for (int i = 1; i < kN; ++i) step = fmaxf(step, fabsf(sig[i] - sig[i - 1]));
  for (int a = 0; a < kModels; ++a)
    for (int b = 0; b < kModels; ++b) {
      if (a == b) continue;
      for (int sw : {12000, 12032, 12064, 12096}) {
        ModelDrive d;
        d.set(64, static_cast<uint8_t>(a));
        for (int i = 0; i < kN; ++i) y[i] = sig[i];
        for (int i = 0; i < sw; i += 32) d.processBlock(y + i, 32);
        d.set(64, static_cast<uint8_t>(b));
        for (int i = sw; i < kN; i += 32) d.processBlock(y + i, 32);
        const float jump = fabsf(y[sw] - y[sw - 1]);
        const float allowed = fabsf(ref[b][sw] - ref[a][sw - 1]) + 4.f * step + 0.02f;
        char msg[80];
        snprintf(msg, sizeof(msg), "%d->%d at %d jump %f allowed %f", a, b, sw, jump, allowed);
        TEST_ASSERT_TRUE_MESSAGE(jump <= allowed, msg);
      }
    }
}

void test_reset_clears_state() {
  for (int m = 1; m < kModels; ++m) {
    ModelDrive d;
    d.set(90, static_cast<uint8_t>(m));
    float x[64];
    for (int i = 0; i < 64; ++i) x[i] = 0.9f;
    d.processBlock(x, 64);
    d.reset();
    float z[8] = {};
    d.processBlock(z, 8);
    for (float v : z) TEST_ASSERT_TRUE(v == 0.f);
  }
}

static uint32_t fnv(const float* x, int n) {
  uint32_t h = 2166136261u;
  const uint8_t* b = reinterpret_cast<const uint8_t*>(x);
  for (int i = 0; i < n * 4; ++i) h = (h ^ b[i]) * 16777619u;
  return h;
}

// FOLD bit-exact across the move of its curve into synth_shape.h (hash of the pre-move output,
// a sine with a jump and small steps: the ADAA quotient and the midpoint fallback both run).
void test_fold_bit_exact() {
  static float x[4096];
  for (int i = 0; i < 4096; ++i) x[i] = (i < 2048 ? 0.9f : 0.3f) * sinf(2 * kPi * 220.f * i / kFs);
  for (int i = 3000; i < 3100; ++i) x[i] = 0.25f + i * 1e-6f;
  ModelDrive d;
  d.set(100, 2);
  for (int k = 0; k < 4096; k += 64) d.processBlock(x + k, 64);
  const uint32_t h = fnv(x, 4096);
  TEST_ASSERT_EQUAL_HEX32(0xc0ddea23u, h);
}

// The shared fold curve (synth_shape.h): period 4, odd; its antiderivative against a trapezoid sum.
void test_shape_fold_curve_and_anti() {
  const float pts[] = {0.f, 1.f, 2.f, 3.f}, want[] = {0.f, 1.f, 0.f, -1.f};
  for (int k = 0; k < 4; ++k) {
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, want[k], foldCurve(pts[k]));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -want[k], foldCurve(-pts[k]));
  }
  for (int k = 0; k < 100; ++k) {
    const float u = frand() * 20.f;
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -foldCurve(u), foldCurve(-u));
  }
  const double h = 1e-3;
  double acc = 0;
  const double base = foldAnti(-6.f);
  for (int i = 0; i <= 12000; ++i) {
    const double u = -6.0 + i * h;
    if (i > 0) acc += 0.5 * h * (foldCurve(static_cast<float>(u - h)) + foldCurve(static_cast<float>(u)));
    if (i % 50 == 0) TEST_ASSERT_FLOAT_WITHIN(1e-3f, static_cast<float>(acc), static_cast<float>(foldAnti(static_cast<float>(u)) - base));
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_drive_zero_is_identity);
  RUN_TEST(test_drive_saturates_and_is_monotonic);
  RUN_TEST(test_drive_low_is_gentle);
  RUN_TEST(test_drive_block_matches_feature1);
  RUN_TEST(test_model_drive_zero_bypasses_every_model);
  RUN_TEST(test_model_tanh_is_the_plain_drive);
  RUN_TEST(test_models_bounded_finite_silent);
  RUN_TEST(test_adaa_antiderivatives_match_the_curves);
  RUN_TEST(test_adaa_constant_input_gives_the_curve);
  RUN_TEST(test_adaa_clip_aliases_less_than_naive);
  RUN_TEST(test_fold_shape);
  RUN_TEST(test_rect_octave_up_no_dc);
  RUN_TEST(test_asym_even_harmonic_no_dc);
  RUN_TEST(test_models_loudness_near_tanh);
  RUN_TEST(test_drive_jump_is_smooth);
  RUN_TEST(test_model_switch_is_smooth);
  RUN_TEST(test_reset_clears_state);
  RUN_TEST(test_fold_bit_exact);
  RUN_TEST(test_shape_fold_curve_and_anti);
  return UNITY_END();
}
