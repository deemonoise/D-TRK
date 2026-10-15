#include <math.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include <unity.h>
#include "model.h"
#include "synth_macro.h"
#include "synth_osc.h"
#include "synth_syn.h"

using namespace mt;

void setUp() {}
void tearDown() {}

// Goertzel power at hz over x[0..n).
static double power(const float* x, int n, float hz) {
  const double w = 6.283185307179586 * hz / kSynthRate, c = 2 * cos(w);
  double s1 = 0, s2 = 0;
  for (int i = 0; i < n; ++i) {
    const double s = x[i] + c * s1 - s2;
    s2 = s1;
    s1 = s;
  }
  return s1 * s1 + s2 * s2 - c * s1 * s2;
}

// Pitches with an integer period in samples (no leakage between harmonics over whole periods).
constexpr float kF110 = kSynthRate / 400.f;   // 110.25 Hz
constexpr float kF220 = kSynthRate / 200.f;   // 220.5 Hz
constexpr float kF441 = kSynthRate / 100.f;   // 441 Hz
constexpr float kF2k = kSynthRate / 22.f;     // 2004.5 Hz
constexpr float kF6k = kSynthRate / 7.f;      // 6300 Hz
constexpr int kSkip = 1600;                   // settle (the MORPH low-pass, the BLEP delay)
constexpr int kLen = 8000;                    // 20 .. 40 whole periods of the pitches above

static float buf[kSkip + kLen + 64];
static float buf2[kSkip + kLen + 64];

// Note-on, then n samples in control periods of 32 (t, c constant). Into dst (cleared first).
static void render(float* dst, MacroModel model, float hz, float t, float c, int n, uint32_t seed = 0) {
  MacroVoice v;
  v.trigger(seed);
  MacroParams p;
  p.model = static_cast<uint8_t>(model);
  p.hz = hz, p.timbre = t, p.color = c;
  memset(dst, 0, sizeof(float) * n);
  for (int pos = 0; pos < n; pos += 32) {
    const int m = n - pos < 32 ? n - pos : 32;
    v.control(p, m);
    v.render(dst + pos, m, 1.f);
  }
}

// Steady part of a render: kLen samples after kSkip.
static const float* steady(MacroModel model, float hz, float t, float c, float* dst = buf) {
  render(dst, model, hz, t, c, kSkip + kLen);
  return dst + kSkip;
}

// Amplitude of harmonic k (relative units).
static double harm(const float* x, float f0, int k) { return sqrt(power(x, kLen, f0 * k)); }
static double db(double a, double b) { return 20 * log10(a / b); }

// Amplitude-weighted centroid of harmonics 1..20 (those under 20 kHz), in harmonic numbers.
static double centroid(const float* x, float f0) {
  double s = 0, w = 0;
  for (int k = 1; k <= 20 && f0 * k < 20000.f; ++k) {
    const double a = harm(x, f0, k);
    s += a * k, w += a;
  }
  return s / w;
}

// Harmonic k re the fundamental, dB, floored at -80.
static double rel(const float* x, float f0, int k) {
  const double a = harm(x, f0, k) / harm(x, f0, 1);
  return 20 * log10(a > 1e-4 ? a : 1e-4);
}

// Spectral shape distance: mean |difference| of harmonics 2..20 (under 20 kHz) re the fundamental, dB,
// each capped at 20 dB. For changes the centroid does not see (a notch reshapes a 1 / k spectrum
// without moving its balance point).
static double profileDist(const float* x, const float* y, float f0) {
  double s = 0;
  int m = 0;
  for (int k = 2; k <= 20 && f0 * k < 20000.f; ++k, ++m) {
    const double d = fabs(rel(x, f0, k) - rel(y, f0, k));
    s += d < 20 ? d : 20;
  }
  return s / m;
}

// Energy of harmonics 3..20 (under 20 kHz) re the fundamental, dB.
static double upper(const float* x, float f0) {
  double s = 0;
  for (int k = 3; k <= 20 && f0 * k < 20000.f; ++k) s += power(x, kLen, f0 * k);
  return 10 * log10(s / power(x, kLen, f0));
}

// --- shared ----------------------------------------------------------------------------------------

static void checkFundamental(MacroModel m, float t, float c) {
  const float* x = steady(m, kF220, t, c);
  const double f = power(x, kLen, kF220);
  TEST_ASSERT_TRUE(f > power(x, kLen, kF220 * 0.5f));
  TEST_ASSERT_TRUE(f > power(x, kLen, kF220 * 1.5f));
}

static void checkBounded(MacroModel m) {
  static const float kHz[] = {30, 220, 2000, 8000};
  static const float kTc[] = {0, 0.5f, 1};
  for (float hz : kHz)
    for (float t : kTc)
      for (float c : kTc) {
        render(buf, m, hz, t, c, 4096);
        double sum = 0, sq = 0;
        for (int i = 0; i < 4096; ++i) {
          TEST_ASSERT_FALSE(isnan(buf[i]) || isinf(buf[i]));
          TEST_ASSERT_TRUE(fabsf(buf[i]) <= 1.5f);
          if (i >= 1024) sum += buf[i], sq += buf[i] * buf[i];
        }
        const double mean = sum / 3072;
        TEST_ASSERT_TRUE(sq / 3072 - mean * mean > 0.05 * 0.05);  // AC RMS: not silent, not just DC
      }
}

// Bass notes (C0 .. A1) over a 0.1 grid of t and c, 8192 samples (the steady state of the DC
// trackers): no NaN, |peak| <= 1.5. A slow sub-pitch wave at a low pitch is where a DC tracker that
// follows the wave adds to the peak.
static void checkBoundedBass(MacroModel m) {
  static const float kHz[] = {16.35f, 20.6f, 27.5f, 32.7f, 41.2f, 55.f};
  constexpr int n = 8192;
  float worst = 0;
  for (float hz : kHz)
    for (int ti = 0; ti <= 10; ++ti)
      for (int ci = 0; ci <= 10; ++ci) {
        render(buf, m, hz, 0.1f * ti, 0.1f * ci, n);
        for (int i = 0; i < n; ++i) {
          TEST_ASSERT_FALSE(isnan(buf[i]) || isinf(buf[i]));
          worst = fabsf(buf[i]) > worst ? fabsf(buf[i]) : worst;
        }
      }
  char msg[64];
  snprintf(msg, sizeof(msg), "%s bass peak %.3f", macroModelName(static_cast<uint8_t>(m)), worst);
  TEST_MESSAGE(msg);
  TEST_ASSERT_TRUE_MESSAGE(worst <= 1.5f, msg);
}

// TIMBRE 0 vs 1 (COLOR 0.5) and COLOR 0 vs 1 (TIMBRE 0.5) each move the centroid by > 5 %.
static void checkCentroidMoves(MacroModel m) {
  const double t0 = centroid(steady(m, kF220, 0, 0.5f), kF220);
  const double t1 = centroid(steady(m, kF220, 1, 0.5f), kF220);
  TEST_ASSERT_TRUE(fabs(t1 / t0 - 1) > 0.05);
  const double c0 = centroid(steady(m, kF220, 0.5f, 0), kF220);
  const double c1 = centroid(steady(m, kF220, 0.5f, 1), kF220);
  TEST_ASSERT_TRUE(fabs(c1 / c0 - 1) > 0.05);
}

// As above, with the shape distance (> 1.5 dB per harmonic on average).
static void checkProfileMoves(MacroModel m) {
  const float* t0 = steady(m, kF220, 0, 0.5f, buf);
  TEST_ASSERT_TRUE(profileDist(t0, steady(m, kF220, 1, 0.5f, buf2), kF220) > 1.5);
  const float* c0 = steady(m, kF220, 0.5f, 0, buf);
  TEST_ASSERT_TRUE(profileDist(c0, steady(m, kF220, 0.5f, 1, buf2), kF220) > 1.5);
}

// At 5 kHz the harmonics 8, 9, 7 (40, 45, 35 kHz) fold onto 4.1, 0.9, 9.1 kHz: none of them near a
// harmonic of 5 kHz. Band-limited output: >= 10 dB less there than a naive saw.
static void checkAliasBelowNaive(MacroModel m, float t, float c) {
  constexpr int n = 8192;
  const float dt = 5000.f / kSynthRate;
  render(buf, m, 5000.f, t, c, n);
  const double bl = power(buf, n, 4100) + power(buf, n, 900) + power(buf, n, 9100);
  const double fund = power(buf, n, 5000);
  ChipOsc o;
  for (int i = 0; i < n; ++i) buf[i] = o.next(Wave::Saw, dt, 0.5f);
  const double naive = power(buf, n, 4100) + power(buf, n, 900) + power(buf, n, 9100);
  TEST_ASSERT_TRUE(bl < naive * 0.1);
  TEST_ASSERT_TRUE(fund > power(buf, n, 5000) * 0.1);  // not silent: the fundamental within 10 dB
}

// No DC (nothing downstream blocks it: thumps at note on / off, swells under TIMBRE motion).
static void checkDcFree(MacroModel m) {
  static const float kTc[] = {0, 0.5f, 1};
  for (float t : kTc)
    for (float c : kTc) {
      const float* x = steady(m, kF220, t, c);
      double sum = 0;
      for (int i = 0; i < kLen; ++i) sum += x[i];
      TEST_ASSERT_TRUE(fabs(sum / kLen) < 0.02);
    }
}

// Low-pass (Blackman-windowed sinc, len taps, cutoff fc / rate) of x[0..n) into y (valid part only:
// y[i] = filtered x at i + len - 1, delay (len - 1) / 2).
static int lowpass(const float* x, int n, float* y, int len, double fc) {
  static double h[2048];
  const double m = (len - 1) / 2.0;
  double sum = 0;
  for (int k = 0; k < len; ++k) {
    const double u = k - m, w = 0.42 - 0.5 * cos(2 * M_PI * k / (len - 1)) + 0.08 * cos(4 * M_PI * k / (len - 1));
    h[k] = (u == 0 ? 2 * fc : sin(2 * M_PI * fc * u) / (M_PI * u)) * w;
    sum += h[k];
  }
  for (int k = 0; k < len; ++k) h[k] /= sum;
  int o = 0;
  for (int i = len - 1; i < n; ++i, ++o) {
    double a = 0;
    for (int k = 0; k < len; ++k) a += h[k] * x[i - k];
    y[o] = static_cast<float>(a);
  }
  return o;
}

// t switching between t0 and t1 every control period (each a full ramp over 32 samples).
static void renderSwitching(float* dst, MacroModel m, float hz, float t0, float t1, int n, int ctl) {
  MacroVoice v;
  v.trigger();
  MacroParams p;
  p.model = static_cast<uint8_t>(m);
  p.hz = hz, p.color = 0;
  memset(dst, 0, sizeof(float) * n);
  for (int pos = 0, k = 0; pos < n; pos += ctl, ++k) {
    p.timbre = (k & 1) ? t1 : t0;
    v.control(p, ctl);
    v.render(dst + pos, ctl, 1.f);
  }
}

// Error under 3 kHz against a 4x-rate render of the same continuous wave (hz / 4, control periods of
// 128), dB re the signal. Aliasing (a missed or doubled edge correction) lands in that band.
static double inBandError(MacroModel m, float hz, float t0, float t1) {
  constexpr int n = 6144, n4 = 4 * n;
  static float a[n], b[n4], fa[n4], fb[n4];
  renderSwitching(a, m, hz, t0, t1, n, 32);
  renderSwitching(b, m, hz * 0.25f, t0, t1, n4, 128);
  const int la = lowpass(a, n, fa, 255, 3000.0 / kSynthRate);    // fa[i] ~ a at i + 127
  lowpass(b, n4, fb, 1017, 750.0 / kSynthRate);                  // fb[j] ~ b at j + 508
  // Both one sample late, the phase advanced before each sample: a[i] holds phase i dt, b[j] phase
  // j dt / 4, so a[i] ~ b[4 i].
  double e = 0, sig = 0;
  for (int i = 200; i < la - 10; ++i) {
    const int j = 4 * i;  // 4 (i + 127) - 508
    const double d = fa[i] - fb[j];
    e += d * d, sig += fb[j] * fb[j];
  }
  return 10 * log10(e / sig);
}

// TIMBRE switching every 32 samples moves the edges faster than the phase at 300 Hz (an edge can pass
// the phase going forward): its error stays within 3 dB of the static wave's (~-52 dB; ignoring the
// edge passing the phase costs 6..15 dB).
static void checkMovingEdges(MacroModel m, float t0, float t1) {
  const float tm = 0.5f * (t0 + t1);
  const double still = inBandError(m, 300.f, tm, tm);
  const double moving = inBandError(m, 300.f, t0, t1);
  TEST_ASSERT_TRUE(still < -30);
  TEST_ASSERT_TRUE(moving < still + 3);
}

// --- Task 4: SAW+, SAW/SQR, skeleton ----------------------------------------------------------------

void test_sawplus_fundamental() { checkFundamental(MacroModel::SawPlus, 0.5f, 0.5f); }
void test_sawsqr_fundamental() { checkFundamental(MacroModel::SawSqr, 0.5f, 0.5f); }
void test_sawplus_bounded() { checkBounded(MacroModel::SawPlus); }
void test_sawplus_bounded_bass() { checkBoundedBass(MacroModel::SawPlus); }
void test_sawsqr_bounded() { checkBounded(MacroModel::SawSqr); }
void test_sawsqr_bounded_bass() { checkBoundedBass(MacroModel::SawSqr); }
// The notch reshapes the saw's 1 / k spectrum (comb-like dips near harmonic 1 / pw) but hardly moves
// its centroid (< 1 %): the shape distance instead.
void test_sawplus_spectrum_moves() { checkProfileMoves(MacroModel::SawPlus); }
void test_sawsqr_centroid_moves() { checkCentroidMoves(MacroModel::SawSqr); }
void test_sawplus_alias_below_naive() { checkAliasBelowNaive(MacroModel::SawPlus, 0.5f, 0.5f); }
void test_sawsqr_alias_below_naive() { checkAliasBelowNaive(MacroModel::SawSqr, 0.5f, 0.5f); }

void test_sawplus_dc_free() { checkDcFree(MacroModel::SawPlus); }
void test_sawsqr_dc_free() { checkDcFree(MacroModel::SawSqr); }
// Narrow square at t 1 (pulse width floored at 2 samples): band-limited too.
void test_sawsqr_pulse_alias_below_naive() { checkAliasBelowNaive(MacroModel::SawSqr, 1, 1); }
// From t 0.08: the notch above its 4-sample floor at both rates.
void test_sawplus_moving_edges() { checkMovingEdges(MacroModel::SawPlus, 0.08f, 1); }
void test_sawsqr_moving_edges() { checkMovingEdges(MacroModel::SawSqr, 0.2f, 0.95f); }

void test_trigger_resets() {
  MacroVoice v;  // one voice through every model: each first pass starts from the last model's state
  for (int m = 0; m < static_cast<int>(MacroModel::Count); ++m) {
    MacroParams p;
    p.model = static_cast<uint8_t>(m);
    p.hz = 317.f, p.timbre = 0.3f, p.color = 0.6f;
    for (int pass = 0; pass < 2; ++pass) {
      float* dst = pass ? buf2 : buf;
      v.trigger(9);
      memset(dst, 0, sizeof(float) * 2048);
      for (int pos = 0; pos < 2048; pos += 32) {
        v.control(p, 32);
        v.render(dst + pos, 32, 0.8f);
      }
    }
    TEST_ASSERT_EQUAL_MEMORY(buf, buf2, sizeof(float) * 2048);
    float peak = 0;
    for (int i = 0; i < 2048; ++i) peak = fabsf(buf[i]) > peak ? fabsf(buf[i]) : peak;
    TEST_ASSERT_TRUE(peak > 0.1f);  // not silent
  }
}

// render() before the first control() after a trigger (or on a fresh voice) adds nothing: the model's
// state is not set up yet.
void test_render_before_control_silent() {
  MacroVoice fresh;
  for (int i = 0; i < 64; ++i) buf[i] = 0.25f;
  fresh.render(buf, 64, 1.f);
  for (int i = 0; i < 64; ++i) TEST_ASSERT_EQUAL_FLOAT(0.25f, buf[i]);
  for (int m = 0; m < static_cast<int>(MacroModel::Count); ++m) {
    MacroVoice v;
    v.trigger(3);
    MacroParams p;
    p.model = static_cast<uint8_t>(m);
    p.hz = 220.f, p.timbre = 0.7f, p.color = 0.4f;
    for (int pos = 0; pos < 256; pos += 32) {
      v.control(p, 32);
      v.render(buf2 + pos, 32, 1.f);
    }
    v.trigger(4);
    for (int i = 0; i < 64; ++i) buf[i] = 0.25f;
    v.render(buf, 64, 1.f);
    for (int i = 0; i < 64; ++i) TEST_ASSERT_EQUAL_FLOAT(0.25f, buf[i]);
  }
}

// render() adds into the buffer, scaled by amp.
void test_render_adds_scaled() {
  render(buf, MacroModel::SawPlus, 220.f, 0.5f, 0.5f, 256);
  MacroVoice v;
  v.trigger();
  MacroParams p;
  p.model = static_cast<uint8_t>(MacroModel::SawPlus);
  p.hz = 220.f, p.timbre = 0.5f, p.color = 0.5f;
  for (int i = 0; i < 256; ++i) buf2[i] = 1.f;
  for (int pos = 0; pos < 256; pos += 32) {
    v.control(p, 32);
    v.render(buf2 + pos, 32, 0.5f);
  }
  for (int i = 0; i < 256; ++i) TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.f + 0.5f * buf[i], buf2[i]);
  TEST_ASSERT_TRUE(fabsf(buf[100]) + fabsf(buf[150]) > 0.f);  // not silent
}

// --- Task 5: MORPH -----------------------------------------------------------------------------------

void test_morph_fundamental() { checkFundamental(MacroModel::Morph, 0.5f, 0.5f); }
void test_morph_bounded() { checkBounded(MacroModel::Morph); }
void test_morph_bounded_bass() { checkBoundedBass(MacroModel::Morph); }

// t 0: triangle, the 3rd harmonic 1/9 of the fundamental (+-3 dB).
void test_morph_t0_triangle() {
  const float* x = steady(MacroModel::Morph, kF220, 0, 0);
  TEST_ASSERT_FLOAT_WITHIN(3.0, db(1, 9), db(harm(x, kF220, 3), harm(x, kF220, 1)));
  TEST_ASSERT_TRUE(db(harm(x, kF220, 2), harm(x, kF220, 1)) < -40);  // odd only
}

// t 1/3: saw (2nd harmonic at about half the fundamental).
void test_morph_t033_saw() {
  const float* x = steady(MacroModel::Morph, kF220, 0.33f, 0);
  TEST_ASSERT_FLOAT_WITHIN(3.0, db(1, 2), db(harm(x, kF220, 2), harm(x, kF220, 1)));
}

// t 2/3: square (no 2nd harmonic).
void test_morph_t066_square() {
  const float* x = steady(MacroModel::Morph, kF220, 0.66f, 0);
  TEST_ASSERT_TRUE(db(harm(x, kF220, 2), harm(x, kF220, 1)) < -30);
  TEST_ASSERT_FLOAT_WITHIN(3.0, db(1, 3), db(harm(x, kF220, 3), harm(x, kF220, 1)));
}

// t 1: narrow pulse (the 2nd harmonic is back, about as strong as the fundamental).
void test_morph_t1_pulse() {
  const float* x = steady(MacroModel::Morph, kF220, 1, 0);
  TEST_ASSERT_TRUE(db(harm(x, kF220, 2), harm(x, kF220, 1)) > -3);
}

// c 1 overdrives the low-passed signal: at 110 Hz the triangle gains upper harmonics (> 6 dB more
// energy in harmonics 3..20 re the fundamental). The centroid moves too little to separate it from
// noise (x 1.27: the low-pass at the pitch keeps the overdriven wave round).
void test_morph_color_overdrive_low() {
  const double c0 = upper(steady(MacroModel::Morph, kF110, 0, 0), kF110);
  const double c1 = upper(steady(MacroModel::Morph, kF110, 0, 1), kF110);
  TEST_ASSERT_TRUE(c1 > c0 + 6);
}

// The overdrive fades out above ~830 Hz: at 441 Hz c 1 still adds > 6 dB of upper harmonics, at 2 kHz
// c 1 stays within 3 dB of c 0.
void test_morph_overdrive_pitch_limit() {
  const double l0 = upper(steady(MacroModel::Morph, kF441, 0, 0), kF441);
  const double l1 = upper(steady(MacroModel::Morph, kF441, 0, 1), kF441);
  TEST_ASSERT_TRUE(l1 > l0 + 6);
  const double h0 = upper(steady(MacroModel::Morph, kF2k, 0, 0), kF2k);
  const double h1 = upper(steady(MacroModel::Morph, kF2k, 0, 1), kF2k);
  TEST_ASSERT_TRUE(fabs(h1 - h0) < 3);
}

void test_morph_alias_below_naive() { checkAliasBelowNaive(MacroModel::Morph, 0.33f, 0); }
void test_morph_pulse_alias_below_naive() { checkAliasBelowNaive(MacroModel::Morph, 1, 0); }
void test_morph_dc_free() { checkDcFree(MacroModel::Morph); }
void test_morph_moving_edges() { checkMovingEdges(MacroModel::Morph, 0.7f, 0.95f); }

// --- Task 6: FOLD -------------------------------------------------------------------------------------

void test_fold_fundamental() { checkFundamental(MacroModel::Fold, 0.5f, 0.5f); }
void test_fold_bounded() { checkBounded(MacroModel::Fold); }
void test_fold_bounded_bass() { checkBoundedBass(MacroModel::Fold); }

// t 0, c 0: a pure sine.
void test_fold_t0_sine() {
  const float* x = steady(MacroModel::Fold, kF220, 0, 0);
  const double a1 = harm(x, kF220, 1);
  TEST_ASSERT_TRUE(db(harm(x, kF220, 2), a1) < -50);
  TEST_ASSERT_TRUE(db(harm(x, kF220, 3), a1) < -50);
}

// t 1 raises the 3rd and 5th harmonics by >= 20 dB (re the fundamental).
void test_fold_t1_harmonics() {
  const float* x0 = steady(MacroModel::Fold, kF220, 0, 0, buf);
  const float* x1 = steady(MacroModel::Fold, kF220, 1, 0, buf2);
  for (int k : {3, 5}) {
    const double r0 = db(harm(x0, kF220, k), harm(x0, kF220, 1));
    const double r1 = db(harm(x1, kF220, k), harm(x1, kF220, 1));
    TEST_ASSERT_TRUE(r1 > r0 + 20);
    TEST_ASSERT_TRUE(r1 > -30);  // audible, not just above the table's noise floor
  }
}

// c 1: the triangle fold (odd harmonics only), a different spectrum from the sine fold.
void test_fold_color_triangle() {
  const float* x1 = steady(MacroModel::Fold, kF220, 1, 1, buf);
  const double a1 = harm(x1, kF220, 1);
  TEST_ASSERT_TRUE(db(harm(x1, kF220, 2), a1) < -40);
  TEST_ASSERT_TRUE(db(harm(x1, kF220, 4), a1) < -40);
  // Both folds spread over similar bands (centroids within 3 %): the shape distance instead.
  TEST_ASSERT_TRUE(profileDist(x1, steady(MacroModel::Fold, kF220, 1, 0, buf2), kF220) > 1.5);
}

void test_fold_dc_free() { checkDcFree(MacroModel::Fold); }

// Fold aliasing at 1.9 kHz: 44.1 kHz = 23 x 1.9 kHz + 400 Hz, so every alias lands 400 Hz off a
// harmonic (400, 1500, 2300, 3400, 4200 Hz). Both folds >= 10 dB below a naive saw there.
static double foldBins(const float* x, int n) {
  double s = 0;
  for (float f : {400.f, 1500.f, 2300.f, 3400.f, 4200.f}) s += power(x, n, f);
  return s;
}
void test_fold_alias_below_naive() {
  constexpr int n = 8192;
  ChipOsc o;
  for (int i = 0; i < n; ++i) buf2[i] = o.next(Wave::Saw, 1900.f / kSynthRate, 0.5f);
  const double naive = foldBins(buf2, n);
  for (float c : {0.f, 1.f}) {
    render(buf, MacroModel::Fold, 1900.f, 1, c, n);
    TEST_ASSERT_TRUE(foldBins(buf, n) < naive * 0.1);
  }
}

// Pitch limit: at 6.3 kHz the fold is off, t 1 renders as t 0.
void test_fold_pitch_limit() {
  for (float c : {0.f, 1.f}) {
    render(buf, MacroModel::Fold, kF6k, 0, c, 2048);
    render(buf2, MacroModel::Fold, kF6k, 1, c, 2048);
    double d = 0, e = 0;
    for (int i = 0; i < 2048; ++i) d += (buf[i] - buf2[i]) * (buf[i] - buf2[i]), e += buf[i] * buf[i];
    TEST_ASSERT_TRUE(d < 1e-8);
    TEST_ASSERT_TRUE(e > 100);  // not silent
  }
}

// --- shared (formant / FM models) ------------------------------------------------------------------

// Amplitude-weighted centroid of harmonics 1..kmax (under 20 kHz).
static double centroidK(const float* x, float f0, int kmax) {
  double s = 0, w = 0;
  for (int k = 1; k <= kmax && f0 * k < 20000.f; ++k) {
    const double a = harm(x, f0, k);
    s += a * k, w += a;
  }
  return s / w;
}

// Harmonic nearest to hz, dB re the strongest of harmonics 1..60: a formant at hz lifts it.
static double nearDb(const float* x, float f0, float hz) {
  int k = static_cast<int>(hz / f0 + 0.5f);
  k = k < 1 ? 1 : k;
  double mx = 0;
  for (int j = 1; j <= 60 && f0 * j < 20000.f; ++j) mx = harm(x, f0, j) > mx ? harm(x, f0, j) : mx;
  return db(harm(x, f0, k), mx);
}

// Normalised autocorrelation of x[0..n) at lag.
static double autocorr(const float* x, int n, int lag) {
  double xy = 0, xx = 0, yy = 0;
  for (int i = 0; i + lag < n; ++i) xy += x[i] * x[i + lag], xx += x[i] * x[i], yy += x[i + lag] * x[i + lag];
  return xy / sqrt(xx * yy);
}

// Zero crossings per sample over x[a..b).
static double zcr(const float* x, int a, int b) {
  int z = 0;
  for (int i = a + 1; i < b; ++i) z += (x[i - 1] < 0) != (x[i] < 0);
  return static_cast<double>(z) / (b - a);
}

static float noteHzT(float note) { return 440.f * powf(2.f, (note - 69.f) / 12.f); }

// --- Task 7: VOSIM -----------------------------------------------------------------------------------

void test_vosim_fundamental() { checkFundamental(MacroModel::Vosim, 0.5f, 0.5f); }
void test_vosim_bounded() { checkBounded(MacroModel::Vosim); }
void test_vosim_bounded_bass() { checkBoundedBass(MacroModel::Vosim); }
void test_vosim_dc_free() { checkDcFree(MacroModel::Vosim); }

// Formant 1 at note 128 t, formant 2 at note 128 c (a 110 Hz pitch). The strongest-harmonic metric of
// the plan picks the fundamental at every t (the window's own pulse train dominates): instead the
// harmonic nearest each formant is lifted by > 6 dB when the formant sits there.
void test_vosim_formants_follow_t_and_c() {
  const float lo = noteHzT(128 * 0.6f), hi = noteHzT(128 * 0.8f);  // ~0.94, ~2.9 kHz
  const float* a = steady(MacroModel::Vosim, kF110, 0.6f, 0.3f, buf);
  const float* b = steady(MacroModel::Vosim, kF110, 0.8f, 0.3f, buf2);
  TEST_ASSERT_TRUE(nearDb(a, kF110, lo) > nearDb(b, kF110, lo) + 6);
  TEST_ASSERT_TRUE(nearDb(b, kF110, hi) > nearDb(a, kF110, hi) + 6);
  const float c2 = noteHzT(128 * 0.85f);  // ~4 kHz, formant 2 (weaker: half of formant 1)
  const float* c = steady(MacroModel::Vosim, kF110, 0.6f, 0.3f, buf);
  const float* d = steady(MacroModel::Vosim, kF110, 0.6f, 0.85f, buf2);
  TEST_ASSERT_TRUE(nearDb(d, kF110, c2) > nearDb(c, kF110, c2) + 6);
}

// The formants restart with every period: the output repeats with the pitch.
void test_vosim_periodic() {
  const float* x = steady(MacroModel::Vosim, kF110, 0.63f, 0.71f);
  TEST_ASSERT_TRUE(autocorr(x, kLen, 400) > 0.9);
}

// --- Task 8: VOWEL -----------------------------------------------------------------------------------

void test_vowel_fundamental() { checkFundamental(MacroModel::Vowel, 0.5f, 0.5f); }
void test_vowel_bounded() { checkBounded(MacroModel::Vowel); }
void test_vowel_bounded_bass() { checkBoundedBass(MacroModel::Vowel); }
void test_vowel_dc_free() { checkDcFree(MacroModel::Vowel); }

// The first vowel (t 0: formants 27 / 40 x 41.7 Hz at c 0.5 = 1.13 / 1.67 kHz) and the last (t 1:
// 6 / 73 x 41.7 = 0.25 / 3.04 kHz): each one's F1 / F2 harmonic stands > 6 dB higher in its own vowel.
void test_vowel_t_moves_formants() {
  const float u = 96000.f / 4096.f * (200.f + 511.f * 0.5f) / 256.f;  // Hz per table unit at c 0.5
  const float* a = steady(MacroModel::Vowel, kF110, 0, 0.5f, buf);
  const float* b = steady(MacroModel::Vowel, kF110, 1, 0.5f, buf2);
  TEST_ASSERT_TRUE(nearDb(a, kF110, 27 * u) > nearDb(b, kF110, 27 * u) + 6);
  TEST_ASSERT_TRUE(nearDb(a, kF110, 40 * u) > nearDb(b, kF110, 40 * u) + 6);
  TEST_ASSERT_TRUE(nearDb(b, kF110, 6 * u) > nearDb(a, kF110, 6 * u) + 6);
  TEST_ASSERT_TRUE(nearDb(b, kF110, 73 * u) > nearDb(a, kF110, 73 * u) + 6);
}

// c 0 -> 1 scales every formant by 711 / 200: the centroid (harmonics up to 100) rises > 1.5x.
void test_vowel_c_shifts_formants() {
  const double a = centroidK(steady(MacroModel::Vowel, kF110, 0.5f, 0), kF110, 100);
  const double b = centroidK(steady(MacroModel::Vowel, kF110, 0.5f, 1), kF110, 100);
  TEST_ASSERT_TRUE(b > a * 1.5);
}

// A consonant (from the note's seed) for the first 40 ms; the noisy ones (consonants 6, 7) raise the
// zero-crossing rate of the onset well above the vowel's. Some seeds pick one, others do not; exactly
// the seeds whose consonant is 6 or 7 (the voice's pick: the top 3 bits of the first xorshift32 of the
// hashed seed) are noisy, and the noise stops after 40 ms (samples 1800..2000 cross zero as the vowel).
static int consonantOf(uint32_t seed) {
  uint32_t x = seed * 2654435761u + 0x9E3779B9u;
  if (!x) x = 1;
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  return static_cast<int>(x >> 29);
}
void test_vowel_consonant_noise() {
  int noisy = 0, clean = 0;
  for (uint32_t seed = 0; seed < 16; ++seed) {
    render(buf, MacroModel::Vowel, kF110, 0.5f, 0.5f, 3200, seed);
    const double on = zcr(buf, 10, 160), vow = zcr(buf, 2400, 3200), after = zcr(buf, 1800, 2000);
    const bool isNoisy = on > 2 * vow;
    (isNoisy ? noisy : clean)++;
    TEST_ASSERT_EQUAL(consonantOf(seed) >= 6, isNoisy);
    TEST_ASSERT_TRUE(after < 1.5 * vow + 0.01);
  }
  TEST_ASSERT_TRUE(noisy > 0);
  TEST_ASSERT_TRUE(clean > 0);
}

// The same seed renders the same note.
void test_vowel_seed_deterministic() {
  render(buf, MacroModel::Vowel, 300.f, 0.4f, 0.6f, 2048, 5);
  render(buf2, MacroModel::Vowel, 300.f, 0.4f, 0.6f, 2048, 5);
  TEST_ASSERT_EQUAL_MEMORY(buf, buf2, sizeof(float) * 2048);
}

// --- Task 9: FM and FB FM ----------------------------------------------------------------------------

void test_fm_fundamental() { checkFundamental(MacroModel::Fm, 0.5f, 0.25f); }
void test_fbfm_fundamental() { checkFundamental(MacroModel::FbFm, 0.5f, 0.25f); }
void test_fm_bounded() { checkBounded(MacroModel::Fm); }
void test_fm_bounded_bass() { checkBoundedBass(MacroModel::Fm); }
void test_fbfm_bounded() { checkBounded(MacroModel::FbFm); }
void test_fbfm_bounded_bass() { checkBoundedBass(MacroModel::FbFm); }
void test_fm_dc_free() { checkDcFree(MacroModel::Fm); }
void test_fbfm_dc_free() { checkDcFree(MacroModel::FbFm); }

// t 0: a sine.
void test_fm_t0_sine() {
  const float* x = steady(MacroModel::Fm, kF220, 0, 0.5f);
  TEST_ASSERT_TRUE(db(harm(x, kF220, 2), harm(x, kF220, 1)) < -60);
}

// t 1 at ratio 1 (c 0.25): harmonics 2..5 all present (> -30 dB re the strongest).
void test_fm_t1_rich() {
  const float* x = steady(MacroModel::Fm, kF220, 1, 0.25f);
  for (int k = 2; k <= 5; ++k) TEST_ASSERT_TRUE(nearDb(x, kF220, kF220 * k) > -30);
}

// c picks quantised ratios (0.125 at c 0, 2 x 16 cents at c 0.5, 8 at c 1): at a small index the
// upper sideband sits at f (1 + ratio), > 20 dB over probes halfway to its neighbours (+-f ratio / 2,
// at most 40 Hz: at ratio 0.125 a +-40 Hz probe lands on the carrier); c 0.505 lands on the same ratio.
static double sideband(float c, float ratio) {
  constexpr int n = 16384;
  static float x[n];
  render(x, MacroModel::Fm, 300.f, 0.15f, c, n);
  const float f = 300.f * (1 + ratio), d = 150.f * ratio < 40.f ? 150.f * ratio : 40.f;
  const double p = power(x, n, f);
  return 10 * log10(p / (power(x, n, f - d) + power(x, n, f + d)));
}
void test_fm_ratio_quantised() {
  const float r2 = 2.f * powf(2.f, 16.f / 1200.f);
  TEST_ASSERT_TRUE(sideband(0, 0.125f) > 20);
  TEST_ASSERT_TRUE(sideband(0.5f, r2) > 20);
  TEST_ASSERT_TRUE(sideband(0.505f, r2) > 20);
  TEST_ASSERT_TRUE(sideband(1, 8) > 20);
}

// FB FM's index falls with pitch x ratio (1 / 64 per semitone above note 72, gone 64 semitones up). A
// graded comparison across pitches is not clean in general (test_fbfm_index_graded holds one point): the one-sample feedback delay makes FB FM's spectrum
// pitch-dependent by itself (at ratio 1, t 1 it gets brighter from 441 Hz to 2 kHz despite the
// attenuation), and FM's index is twice FB FM's, so the plan's comparison with FM mixes the two. The
// end points are clean: at ratio 8 (c 1) a 4 kHz note is past the limit -> a plain sine at t 1, while
// at 220 Hz the same t = c = 1 is rich.
void test_fbfm_index_attenuated_high() {
  const float f4 = kSynthRate / 11.f;  // 4009 Hz: note 107 + 36 semitones of ratio, > 72 + 64
  const float* hi = steady(MacroModel::FbFm, f4, 1, 1, buf);
  TEST_ASSERT_TRUE(rel(hi, f4, 2) < -50 && rel(hi, f4, 3) < -50);
  const float* lo = steady(MacroModel::FbFm, kF220, 1, 1, buf2);
  TEST_ASSERT_TRUE(upper(lo, kF220) > -20);
}

// FM's index fades as the modulator (pitch x ratio) passes 4.2 .. 8.4 kHz (notes 108 .. 120): at
// 4 kHz x ratio 8 (32 kHz, past Nyquist) t 1 renders a plain sine; at 220 Hz the same t, c is rich.
void test_fm_index_attenuated_high() {
  const float f4 = kSynthRate / 11.f;
  const float* hi = steady(MacroModel::Fm, f4, 1, 1, buf);
  TEST_ASSERT_TRUE(rel(hi, f4, 2) < -50 && rel(hi, f4, 3) < -50);
  const float* lo = steady(MacroModel::Fm, kF220, 1, 1, buf2);
  TEST_ASSERT_TRUE(upper(lo, kF220) > -20);
}

// One graded point: ratio 1 (c 0.25), t 1, depth 0.8 at 2 kHz (note 95) vs 0.45 at 4 kHz (note
// 107): harmonic 3 re harmonic 1 is lower at 4 kHz.
void test_fbfm_index_graded() {
  const float* a = steady(MacroModel::FbFm, kF2k, 1, 0.25f, buf);
  const double r2 = rel(a, kF2k, 3);
  const float f4 = kSynthRate / 11.f;
  const float* b = steady(MacroModel::FbFm, f4, 1, 0.25f, buf2);
  const double r4 = rel(b, f4, 3);
  char msg[64];
  snprintf(msg, sizeof(msg), "h3 2k %.1f dB 4k %.1f dB", r2, r4);
  TEST_MESSAGE(msg);
  TEST_ASSERT_TRUE_MESSAGE(r4 < r2, msg);
}

// --- Task 10: level ----------------------------------------------------------------------------------

// Every model at 220 Hz, t = c = 0.5: RMS within +-3 dB of a SYNTH saw (mix 0) at the same amp.
void test_level_matches_synth_saw() {
  SynVoice sv;
  sv.trigger();
  SynParams sp;
  sp.hz[0] = sp.hz[1] = kF220;
  memset(buf2, 0, sizeof(buf2));
  for (int pos = 0; pos < kSkip + kLen; pos += 32) {
    sv.control(sp, 32);
    sv.render(buf2 + pos, 32, 1.f);
  }
  double ref = 0;
  for (int i = kSkip; i < kSkip + kLen; ++i) ref += buf2[i] * buf2[i];
  for (int m = 0; m < static_cast<int>(MacroModel::Count); ++m) {
    const float* x = steady(static_cast<MacroModel>(m), kF220, 0.5f, 0.5f);
    double e = 0;
    for (int i = 0; i < kLen; ++i) e += x[i] * x[i];
    const double d = 10 * log10(e / ref);
    TEST_ASSERT_TRUE_MESSAGE(fabs(d) < 3, macroModelName(static_cast<uint8_t>(m)));
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_sawplus_fundamental);
  RUN_TEST(test_sawsqr_fundamental);
  RUN_TEST(test_sawplus_bounded);
  RUN_TEST(test_sawplus_bounded_bass);
  RUN_TEST(test_sawsqr_bounded);
  RUN_TEST(test_sawsqr_bounded_bass);
  RUN_TEST(test_sawplus_spectrum_moves);
  RUN_TEST(test_sawsqr_centroid_moves);
  RUN_TEST(test_sawplus_alias_below_naive);
  RUN_TEST(test_sawsqr_alias_below_naive);
  RUN_TEST(test_sawplus_dc_free);
  RUN_TEST(test_sawsqr_dc_free);
  RUN_TEST(test_sawsqr_pulse_alias_below_naive);
  RUN_TEST(test_sawplus_moving_edges);
  RUN_TEST(test_sawsqr_moving_edges);
  RUN_TEST(test_trigger_resets);
  RUN_TEST(test_render_before_control_silent);
  RUN_TEST(test_render_adds_scaled);
  RUN_TEST(test_morph_fundamental);
  RUN_TEST(test_morph_bounded);
  RUN_TEST(test_morph_bounded_bass);
  RUN_TEST(test_morph_t0_triangle);
  RUN_TEST(test_morph_t033_saw);
  RUN_TEST(test_morph_t066_square);
  RUN_TEST(test_morph_t1_pulse);
  RUN_TEST(test_morph_color_overdrive_low);
  RUN_TEST(test_morph_overdrive_pitch_limit);
  RUN_TEST(test_morph_alias_below_naive);
  RUN_TEST(test_morph_pulse_alias_below_naive);
  RUN_TEST(test_morph_dc_free);
  RUN_TEST(test_morph_moving_edges);
  RUN_TEST(test_fold_fundamental);
  RUN_TEST(test_fold_bounded);
  RUN_TEST(test_fold_bounded_bass);
  RUN_TEST(test_fold_t0_sine);
  RUN_TEST(test_fold_t1_harmonics);
  RUN_TEST(test_fold_color_triangle);
  RUN_TEST(test_fold_dc_free);
  RUN_TEST(test_fold_alias_below_naive);
  RUN_TEST(test_fold_pitch_limit);
  RUN_TEST(test_vosim_fundamental);
  RUN_TEST(test_vosim_bounded);
  RUN_TEST(test_vosim_bounded_bass);
  RUN_TEST(test_vosim_dc_free);
  RUN_TEST(test_vosim_formants_follow_t_and_c);
  RUN_TEST(test_vosim_periodic);
  RUN_TEST(test_vowel_fundamental);
  RUN_TEST(test_vowel_bounded);
  RUN_TEST(test_vowel_bounded_bass);
  RUN_TEST(test_vowel_dc_free);
  RUN_TEST(test_vowel_t_moves_formants);
  RUN_TEST(test_vowel_c_shifts_formants);
  RUN_TEST(test_vowel_consonant_noise);
  RUN_TEST(test_vowel_seed_deterministic);
  RUN_TEST(test_fm_fundamental);
  RUN_TEST(test_fbfm_fundamental);
  RUN_TEST(test_fm_bounded);
  RUN_TEST(test_fm_bounded_bass);
  RUN_TEST(test_fbfm_bounded);
  RUN_TEST(test_fbfm_bounded_bass);
  RUN_TEST(test_fm_dc_free);
  RUN_TEST(test_fbfm_dc_free);
  RUN_TEST(test_fm_t0_sine);
  RUN_TEST(test_fm_t1_rich);
  RUN_TEST(test_fm_ratio_quantised);
  RUN_TEST(test_fbfm_index_attenuated_high);
  RUN_TEST(test_fbfm_index_graded);
  RUN_TEST(test_fm_index_attenuated_high);
  RUN_TEST(test_level_matches_synth_saw);
  return UNITY_END();
}
