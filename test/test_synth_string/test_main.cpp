#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include <type_traits>
#include <unity.h>
#include "synth_lines.h"
#include "synth_osc.h"
#include "synth_string.h"

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

// e 0: a single impulse of the level, nothing else (before the low-pass).
void test_exciter_impulse() {
  StringExciter x;
  x.trigger(0.8f, 0, 1, 220, 0);
  TEST_ASSERT_TRUE(x.active());
  int nz = 0;
  for (int i = 0; i < 4000; ++i) {
    const float v = x.raw();
    if (v != 0) {
      ++nz;
      TEST_ASSERT_EQUAL(0, i);
      TEST_ASSERT_EQUAL_FLOAT(0.8f, v);
    }
  }
  TEST_ASSERT_EQUAL(1, nz);
}

// e 1: a noise burst of 0.5 + 20 ms, then zeros.
void test_exciter_burst_length() {
  StringExciter x;
  x.trigger(1, 1, 1, 220, 0);
  const int n = static_cast<int>(0.0205f * kSynthRate);
  int last = -1, nz = 0;
  for (int i = 0; i < 4 * n; ++i) {
    const float v = x.raw();
    TEST_ASSERT_TRUE(fabsf(v) <= 1.0f);
    if (v != 0) last = i, ++nz;
  }
  TEST_ASSERT_INT_WITHIN(2, n - 1, last);
  TEST_ASSERT_TRUE(nz > n * 9 / 10);  // noise, not sparse clicks
  // Burst over and the low-pass tail gone: no longer active.
  for (int i = 0; i < 20000; ++i) x.next();
  TEST_ASSERT_FALSE(x.active());
}

// BRIGHT: the excitation low-pass at hz x 2^(1 + 6 b); b 0 has >= 10 dB less above 4 kHz than b 1.
void test_exciter_brightness() {
  constexpr int N = 2048;
  static float a[N], b[N];
  StringExciter x0, x1;
  x0.trigger(1, 1, 0, 110, 0);
  x1.trigger(1, 1, 1, 110, 0);
  for (int i = 0; i < N; ++i) a[i] = x0.next(), b[i] = x1.next();
  double ha = 0, hb = 0;
  for (float f = 4000; f < 20000; f += kSynthRate / N) ha += power(a, N, f), hb += power(b, N, f);
  TEST_ASSERT_TRUE(hb > 0);
  TEST_ASSERT_TRUE(10 * log10(hb / (ha + 1e-30)) >= 10);
}

// Twin LFSR: the delayed stream is the main stream P samples later, exactly.
void test_exciter_twin_lfsr() {
  for (int p : {1, 37, 200}) {
    constexpr int N = 1400;
    static float m[N], c[N];
    StringExciter x, ref;  // ref: the main stream alone (no comb)
    x.trigger(1, 1, 1, 220, p);
    ref.trigger(1, 1, 1, 220, 0);
    for (int i = 0; i < N; ++i) c[i] = x.raw(), m[i] = ref.raw();
    for (int i = 0; i < N; ++i) TEST_ASSERT_EQUAL_FLOAT(m[i] - (i >= p ? m[i - p] : 0.f), c[i]);
  }
}

// Position comb: raw output at n = x[n] - x[n - P], x the stream without the comb.
void test_exciter_position_comb() {
  for (int p : {1, 37, 200}) {
    constexpr int N = 1400;
    static float xs[N];
    StringExciter plain, comb;
    plain.trigger(0.7f, 0.6f, 0.5f, 220, 0);
    comb.trigger(0.7f, 0.6f, 0.5f, 220, p);
    for (int i = 0; i < N; ++i) xs[i] = plain.raw();
    for (int i = 0; i < N; ++i) TEST_ASSERT_EQUAL_FLOAT(xs[i] - (i >= p ? xs[i - p] : 0.f), comb.raw());
  }
  // Impulse: +level at 0, -level at P.
  StringExciter c;
  c.trigger(1, 0, 1, 220, 37);
  for (int i = 0; i < 100; ++i) {
    const float v = c.raw();
    TEST_ASSERT_EQUAL_FLOAT(i == 0 ? 1.f : i == 37 ? -1.f : 0.f, v);
  }
}

// Re-trigger: the low-pass keeps its state (the new burst adds to it), the burst restarts.
void test_exciter_retrigger() {
  StringExciter x;
  x.trigger(1, 0, 0, 110, 0);  // impulse, a = 1 - exp(-2 pi 220 / sr)
  float y = 0;
  for (int i = 0; i < 100; ++i) y = x.next();
  TEST_ASSERT_TRUE(y > 1e-4f);
  x.trigger(1, 0, 0, 110, 0);
  const float a = 1 - expf(-6.2831853f * 220 / kSynthRate);
  TEST_ASSERT_FLOAT_WITHIN(2e-3f, y + a * (1 - y), x.next());  // not a (a reset state)
  // A noise burst re-triggered mid-burst runs its full length from the re-trigger.
  x.trigger(1, 1, 1, 220, 0);
  for (int i = 0; i < 300; ++i) x.raw();
  x.trigger(1, 1, 1, 220, 0);
  const int n = static_cast<int>(0.0205f * kSynthRate);
  int last = -1;
  for (int i = 0; i < 3 * n; ++i)
    if (x.raw() != 0) last = i;
  TEST_ASSERT_INT_WITHIN(2, n - 1, last);
}

// fill(): exactly next() per sample (bit for bit) through click / burst, comb, re-trigger mid-burst
// and the low-pass tail, for any chunking (block lengths 1 .. 37).
void test_exciter_fill_matches_next() {
  struct Cfg {
    float e, b, hz;
    int p;
  };
  const Cfg cfgs[] = {{0, 0.3f, 110, 0}, {0, 1, 440, 37}, {0.1f, 0.5f, 220, 0}, {0.79f, 0.7f, 196, 30},
                      {1, 0, 55, 400}, {0.05f, 1, 1760, 3}};
  for (const Cfg& c : cfgs)
    for (int chunk : {1, 7, 32, 37}) {
      StringExciter a, b;
      a.seed(5), b.seed(5);
      a.trigger(0.8f, c.e, c.b, c.hz, c.p);
      b.trigger(0.8f, c.e, c.b, c.hz, c.p);
      float x[37];
      for (int at = 0; at < 3000; at += chunk) {
        if (at >= 400 && at < 400 + chunk) {  // a re-trigger mid-burst
          a.trigger(0.5f, c.e, c.b, c.hz, c.p);
          b.trigger(0.5f, c.e, c.b, c.hz, c.p);
        }
        b.fill(x, chunk);
        for (int i = 0; i < chunk; ++i) {
          const float ya = a.next();
          char msg[96];
          snprintf(msg, sizeof msg, "e %.2f p %d chunk %d at %d: %g vs %g", c.e, c.p, chunk, at + i, ya, x[i]);
          TEST_ASSERT_TRUE_MESSAGE(memcmp(&ya, &x[i], sizeof ya) == 0, msg);
        }
        TEST_ASSERT_EQUAL(a.active(), b.active());
      }
    }
}

// The low-pass -3 dB point is near hz x 2^(1 + 6 b) (+-25 %).
void test_exciter_lp_corner() {
  constexpr int N = 8192;
  static float h[N];
  StringExciter x;
  x.trigger(1, 0, 0.5f, 110, 0);  // fc 110 x 2^4 = 1760 Hz
  for (int i = 0; i < N; ++i) h[i] = x.next();
  const double dc = power(h, N, 0);
  float f3 = 0;
  for (float f = 10; f < 5000; f += 5)
    if (power(h, N, f) < 0.5 * dc) {
      f3 = f;
      break;
    }
  TEST_ASSERT_FLOAT_WITHIN(0.25f * 1760, 1760, f3);
}

// seed(): another noise stream, never stuck at zero.
void test_exciter_seed() {
  StringExciter a, b, c;
  b.seed(7);
  c.seed(0);
  a.trigger(1, 1, 1, 220, 0);
  b.trigger(1, 1, 1, 220, 0);
  c.trigger(1, 1, 1, 220, 0);
  int diff = 0, nzc = 0;
  for (int i = 0; i < 100; ++i) {
    const float va = a.raw(), vb = b.raw(), vc = c.raw();
    diff += va != vb;
    nzc += vc != 0;
  }
  TEST_ASSERT_TRUE(diff > 90);
  TEST_ASSERT_TRUE(nzc > 90);
}

// ---- Resonator helpers ----

static int16_t gLine[kLineLen];  // the voice's delay line (Synth: voiceLine(slot))

struct Hit {
  StringModel model = StringModel::String;
  float hz = 220, d = 0.8f, b = 0.5f, s = 0, p = 0.3f, e = 0.1f, level = 1;
};

static StringParams paramsOf(const Hit& h) {
  StringParams p;
  p.model = static_cast<uint8_t>(h.model);
  p.hz = h.hz;
  p.damp = h.d;
  p.bright = h.b;
  p.structure = h.s;
  p.pos = h.p;
  p.exc = h.e;
  return p;
}

// Renders n samples of one strike of a new voice into out (amp 1, control every 32 samples).
static void play(const Hit& h, float* out, int n) {
  StringVoice v;
  v.seed(1);
  v.trigger(h.level, false);
  const StringParams p = paramsOf(h);
  memset(out, 0, sizeof(float) * n);
  for (int i = 0; i < n; i += 32) {
    const int m = n - i < 32 ? n - i : 32;
    v.control(p, m);
    v.render(out + i, m, 1.f, gLine);
  }
}

// Period in samples near `nominal`: the normalized autocorrelation peak at about m periods (m x
// nominal ~ 2000 lags), parabolic interpolation, / m.
static double periodOf(const float* x, int n0, int len, double nominal) {
  int m = static_cast<int>(2000 / nominal);
  if (m < 1) m = 1;
  const int c = static_cast<int>(m * nominal + 0.5);
  int span = static_cast<int>(0.03 * m * nominal) + 2;  // +-50 cents, but within the m-th peak
  if (span > 0.45 * nominal) span = static_cast<int>(0.45 * nominal);
  double best = -2, r[3] = {0, 0, 0};
  int bestL = c;
  auto nsdf = [&](int L) {
    double xy = 0, xx = 0, yy = 0;
    for (int i = n0; i < n0 + len; ++i) xy += x[i] * x[i + L], xx += x[i] * x[i], yy += x[i + L] * x[i + L];
    return 2 * xy / (xx + yy + 1e-30);
  };
  for (int L = c - span; L <= c + span; ++L) {
    const double v = nsdf(L);
    if (v > best) best = v, bestL = L;
  }
  r[0] = nsdf(bestL - 1), r[1] = best, r[2] = nsdf(bestL + 1);
  const double den = r[0] - 2 * r[1] + r[2];
  const double off = den < 0 ? 0.5 * (r[0] - r[2]) / den : 0;
  return (bestL + off) / m;
}

static double centsOff(double period, double hz) { return 1200 * log2(kSynthRate / hz / period); }

// Power at hz over the window x[n0 .. n0 + len).
static double powerAt(const float* x, int n0, int len, float hz) { return power(x + n0, len, hz); }

// The strongest frequency in lo .. hi (Hann-windowed DFT magnitude, 0.25 Hz grid after a 2 Hz scan).
static double peakHz(const float* x, int n0, int len, double lo, double hi) {
  static float w[32768];
  for (int i = 0; i < len; ++i) w[i] = x[n0 + i] * (0.5f - 0.5f * cosf(6.2831853f * i / len));
  double bf = lo, bp = -1;
  for (double f = lo; f <= hi; f += 2) {
    const double pw = power(w, len, static_cast<float>(f));
    if (pw > bp) bp = pw, bf = f;
  }
  const double c = bf;
  for (double f = c - 2; f <= c + 2; f += 0.25) {
    const double pw = power(w, len, static_cast<float>(f));
    if (pw > bp) bp = pw, bf = f;
  }
  return bf;
}

// T60 of the component at hz from its power in two windows (len samples at a and b seconds).
static double t60At(const float* x, float hz, double a, double b, int len) {
  const double pa = powerAt(x, static_cast<int>(a * kSynthRate), len, hz);
  const double pb = powerAt(x, static_cast<int>(b * kSynthRate), len, hz);
  const double dbPerS = 10 * log10(pa / pb) / (b - a);
  return 60 / dbPerS;
}

static double t60Formula(float d) { return 0.05 * pow(200.0, d); }

// ---- STRING (Karplus-Strong) ----

// Fundamental within +-5 cents at 55, 110, 440, 1760 Hz (s 0, any BRIGHT); 30 Hz (quarter rate) and
// 60 Hz (half rate) +-10 / +-5 cents.
void test_string_tuning() {
  constexpr int N = 2048 + 8192 + 4096;
  static float y[N];
  for (float b : {0.f, 0.6f, 1.f})
    for (float hz : {55.f, 110.f, 440.f, 1760.f, 30.f, 60.f}) {
      Hit h;
      h.hz = hz, h.d = 0.9f, h.b = b;
      play(h, y, N);
      const double c = centsOff(periodOf(y, 2048, 8192, kSynthRate / hz), hz);
      char msg[48];
      snprintf(msg, sizeof msg, "%.0f Hz b %.1f: %.2f cents", hz, b, c);
      TEST_ASSERT_TRUE_MESSAGE(fabs(c) <= (hz < 43 ? 10 : 5), msg);
    }
}

// DAMP: the fundamental's T60 within +-25 % of 0.05 s x 200^d.
void test_string_t60() {
  constexpr int N = static_cast<int>(2.2 * kSynthRate);
  static float y[N];
  Hit h;
  h.d = 0.2f;
  play(h, y, N);
  double t = t60At(y, h.hz, 0.02, 0.10, 1024);
  TEST_ASSERT_FLOAT_WITHIN(0.25 * t60Formula(0.2f), t60Formula(0.2f), t);
  h.d = 0.8f;
  play(h, y, N);
  t = t60At(y, h.hz, 0.2, 2.0, 4096);
  TEST_ASSERT_FLOAT_WITHIN(0.25 * t60Formula(0.8f), t60Formula(0.8f), t);
}

// BRIGHT 0 has >= 10 dB less energy above 4 kHz than BRIGHT 1.
void test_string_brightness() {
  constexpr int N = 4096;
  static float a[N], b[N];
  Hit h;
  h.e = 0.5f, h.b = 0;
  play(h, a, N);
  h.b = 1;
  play(h, b, N);
  double ha = 0, hb = 0;
  for (float f = 4000; f < 20000; f += kSynthRate / N) ha += power(a, N, f), hb += power(b, N, f);
  TEST_ASSERT_TRUE(hb > 0);
  TEST_ASSERT_TRUE(10 * log10(hb / (ha + 1e-30)) >= 10);
}

// POS 0.5 (the middle): harmonics 2 and 4 >= 15 dB under 1 and 3.
void test_string_position() {
  constexpr int N = 8192;
  static float y[N];
  Hit h;
  h.hz = kSynthRate / 200.f;  // period 200: the comb's delay is exactly half of it
  h.p = 0.5f, h.b = 1, h.d = 0.8f, h.e = 0.3f;
  play(h, y, N);
  const double p1 = powerAt(y, 2048, 4096, h.hz), p2 = powerAt(y, 2048, 4096, 2 * h.hz);
  const double p3 = powerAt(y, 2048, 4096, 3 * h.hz), p4 = powerAt(y, 2048, 4096, 4 * h.hz);
  const double odd = p1 < p3 ? p1 : p3, even = p2 > p4 ? p2 : p4;
  TEST_ASSERT_TRUE(10 * log10(odd / (even + 1e-30)) >= 15);
}

// STRUCT: s 0 keeps partial 5 at 5 f, s 1 stretches it above 5.05 f.
void test_string_dispersion() {
  constexpr int N = 4096 + 16384;
  static float y[N];
  Hit h;
  h.hz = 440, h.d = 0.9f, h.b = 1, h.e = 0.3f, h.p = 0.13f;
  for (float s : {0.f, 1.f}) {
    h.s = s;
    play(h, y, N);
    const double f1 = peakHz(y, 4096, 16384, 0.97 * h.hz, 1.03 * h.hz);
    const double f5 = peakHz(y, 4096, 16384, 4.9 * h.hz, 6.5 * h.hz);
    if (s == 0) TEST_ASSERT_FLOAT_WITHIN(0.01, 5.0, f5 / f1);
    else TEST_ASSERT_TRUE(f5 / f1 > 5.05);
  }
}

// Bounded and finite for every macro at 0 / 0.5 / 1 and 30 Hz .. 4 kHz, both models.
void test_bounded_all_macros() {
  constexpr int N = 8192;
  static float y[N];
  const float v3[3] = {0, 0.5f, 1};
  for (int model = 0; model < static_cast<int>(StringModel::Count); ++model)
    for (float hz : {30.f, 110.f, 1000.f, 4000.f})
      for (int i = 0; i < 243; ++i) {
        Hit h;
        h.model = static_cast<StringModel>(model);
        h.hz = hz;
        h.d = v3[i % 3], h.b = v3[i / 3 % 3], h.s = v3[i / 9 % 3], h.p = v3[i / 27 % 3], h.e = v3[i / 81];
        play(h, y, N);
        for (int k = 0; k < N; ++k)
          if (!(fabsf(y[k]) < 2.f)) {
            char msg[96];
            snprintf(msg, sizeof msg, "model %d %.0f Hz d%.1f b%.1f s%.1f p%.1f e%.1f: y[%d] = %g", model, hz, h.d, h.b,
                     h.s, h.p, h.e, k, y[k]);
            TEST_FAIL_MESSAGE(msg);
          }
      }
}

// 100 re-strikes in a row (long decay, loud noise bursts) stay bounded: both models.
void test_restrikes_bounded() {
  static float y[64];
  for (int model = 0; model < static_cast<int>(StringModel::Count); ++model)
    for (float hz : {30.f, 110.f, 880.f}) {
      Hit h;
      h.model = static_cast<StringModel>(model);
      h.hz = hz, h.d = 1, h.b = 1, h.e = 1, h.p = 0.1f;
      StringVoice v;
      const StringParams p = paramsOf(h);
      for (int k = 0; k < 100; ++k) {
        v.trigger(1, k > 0);
        for (int j = 0; j < 8; ++j) {
          memset(y, 0, sizeof y);
          v.control(p, 32);
          v.render(y, 32, 1.f, gLine);
          for (int i = 0; i < 32; ++i) TEST_ASSERT_TRUE(fabsf(y[i]) < 2.f);
        }
      }
    }
}

// ---- MODAL (6 modes) ----

static Hit modal() {
  Hit h;
  h.model = StringModel::Modal;
  h.d = 0.8f, h.b = 1, h.e = 0, h.p = 0.13f;  // an impulse struck off the nodes of the low modes
  return h;
}

// s 0: partials at 1, 2, 3, 4 x f (+-2 %); s 1: mode 2 above 2.5 x mode 1 (inharmonic).
void test_modal_partials() {
  constexpr int N = 4096 + 16384;
  static float y[N];
  Hit h = modal();
  play(h, y, N);
  const double f1 = peakHz(y, 4096, 16384, 0.9 * h.hz, 1.1 * h.hz);
  TEST_ASSERT_FLOAT_WITHIN(0.02 * h.hz, h.hz, f1);
  for (int k = 2; k <= 4; ++k)
    TEST_ASSERT_FLOAT_WITHIN(0.02 * k, k, peakHz(y, 4096, 16384, (k - 0.4) * h.hz, (k + 0.4) * h.hz) / f1);
  h.s = 1;
  play(h, y, N);
  const double g1 = peakHz(y, 4096, 16384, 0.9 * h.hz, 1.1 * h.hz);
  const double g2 = peakHz(y, 4096, 16384, 1.5 * h.hz, 4.5 * h.hz);
  TEST_ASSERT_TRUE(g2 / g1 > 2.5);
}

// 6 modes (the CPU cut-off): s 0, POS off the nodes: modes 1..6 sound, 7 and 8 more than 40 dB under
// mode 6.
void test_modal_six_modes() {
  constexpr int N = 4096 + 16384;
  static float y[N];
  Hit h = modal();
  h.p = 0.07f;  // beta 0.087: sin(pi k beta) > 0.5 for k 1..8
  play(h, y, N);
  const double p6 = powerAt(y, 4096, 16384, 6 * h.hz);
  TEST_ASSERT_TRUE(p6 > 0);
  for (int k = 7; k <= 8; ++k) {
    const double pk = powerAt(y, 4096, 16384, k * h.hz);
    char msg[64];
    snprintf(msg, sizeof msg, "mode %d: %.1f dB under mode 6", k, 10 * log10(p6 / (pk + 1e-30)));
    TEST_ASSERT_TRUE_MESSAGE(10 * log10(p6 / (pk + 1e-30)) > 40, msg);
  }
}

// DAMP: mode 1's T60 within +-25 % of 0.05 s x 200^d.
void test_modal_t60() {
  constexpr int N = static_cast<int>(2.2 * kSynthRate);
  static float y[N];
  Hit h = modal();
  h.d = 0.2f;
  play(h, y, N);
  TEST_ASSERT_FLOAT_WITHIN(0.25 * t60Formula(0.2f), t60Formula(0.2f), t60At(y, h.hz, 0.02, 0.10, 1024));
  h.d = 0.8f;
  play(h, y, N);
  TEST_ASSERT_FLOAT_WITHIN(0.25 * t60Formula(0.8f), t60Formula(0.8f), t60At(y, h.hz, 0.2, 2.0, 4096));
}

// BRIGHT 0: the top modes 4..6 decay at least twice as fast (dB / s) as at BRIGHT 1.
void test_modal_brightness_decay() {
  constexpr int N = static_cast<int>(1.2 * kSynthRate);
  static float y0[N], y1[N];
  // Hann-windowed decay rate at hz between windows at a and b s: the fast modes stay above the
  // neighbours' leakage.
  auto rate = [](const float* x, float hz, double a, double b) {
    constexpr int W = 1024;
    static float w[W];
    double p[2];
    for (int j = 0; j < 2; ++j) {
      const int n0 = static_cast<int>((j ? b : a) * kSynthRate);
      for (int i = 0; i < W; ++i) w[i] = x[n0 + i] * (0.5f - 0.5f * cosf(6.2831853f * i / W));
      p[j] = power(w, W, hz);
    }
    return 10 * log10(p[0] / p[1]) / (b - a);
  };
  Hit h = modal();
  h.hz = 110, h.d = 0.9f, h.p = 0.07f;
  h.b = 0;
  play(h, y0, N);
  h.b = 1;
  play(h, y1, N);
  for (int k = 4; k <= 6; ++k) {
    const double r0 = rate(y0, k * h.hz, 0.03, 0.07), r1 = rate(y1, k * h.hz, 0.03, 0.07);
    char msg[64];
    snprintf(msg, sizeof msg, "mode %d: %.1f vs %.1f dB/s", k, r0, r1);
    TEST_ASSERT_TRUE_MESSAGE(r0 >= 2 * r1, msg);
  }
}

// POS 0.5 (the middle): mode 2 more than 30 dB under mode 1.
void test_modal_position() {
  constexpr int N = 8192;
  static float y[N];
  Hit h = modal();
  h.p = 0.5f;
  play(h, y, N);
  const double p1 = powerAt(y, 1024, 4096, h.hz), p2 = powerAt(y, 1024, 4096, 2 * h.hz);
  TEST_ASSERT_TRUE(10 * log10(p1 / (p2 + 1e-30)) > 30);
}

// Modes above 0.45 x the rate are skipped: 4 kHz with s 1 renders finite, and nothing above 0.45 sr.
void test_modal_top_modes_skipped() {
  constexpr int N = 8192;
  static float y[N];
  Hit h = modal();
  h.hz = 4000, h.s = 1;
  play(h, y, N);
  double lo = 0, hi = 0;
  for (int i = 0; i < N; ++i) TEST_ASSERT_TRUE(fabsf(y[i]) < 2.f);
  lo = powerAt(y, 0, N, h.hz);
  for (float f = 0.46f * kSynthRate; f < 0.5f * kSynthRate; f += 50) hi += powerAt(y, 0, N, f);
  TEST_ASSERT_TRUE(lo > 0);
  TEST_ASSERT_TRUE(10 * log10(lo / (hi + 1e-30)) > 40);
}

// The coefficient cache: unchanged inputs render exactly as a voice that recomputes every update.
void test_modal_cache_identical() {
  for (int model = 0; model < static_cast<int>(StringModel::Count); ++model) {
    if (static_cast<StringModel>(model) == StringModel::String) continue;  // its ramp target is cached
    static float a[64], b[64];
    static int16_t la[kLineLen], lb[kLineLen];
    Hit h = modal();
    StringVoice va, vb;
    va.trigger(1, false), vb.trigger(1, false);
    StringParams p = paramsOf(h);
    for (int blk = 0; blk < 200; ++blk) {
      if (blk == 100) p.hz = 330, p.damp = 0.5f;  // a change mid-way: both recompute
      memset(a, 0, sizeof a), memset(b, 0, sizeof b);
      va.control(p, 32);
      vb.invalidate();
      vb.control(p, 32);
      va.render(a, 32, 1.f, la);
      vb.render(b, 32, 1.f, lb);
      TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof(float) * 32);
    }
  }
}

// ---- StringVoice ----

static_assert(std::is_trivially_copyable<StringVoice>::value, "lives in the voice-engine union");

// The instrument's defaults: macros 80, 64, 0, 30, 10 of 127.
static Hit defaults(StringModel m) {
  Hit h;
  h.model = m;
  h.d = 80 / 127.f, h.b = 64 / 127.f, h.s = 0, h.p = 30 / 127.f, h.e = 10 / 127.f;
  return h;
}

// done(): false right after the trigger and during the burst; true within T60 + 0.1 s; both models.
void test_done_after_decay() {
  for (int model = 0; model < static_cast<int>(StringModel::Count); ++model)
    for (float d : {0.f, 0.2f}) {
      Hit h = defaults(static_cast<StringModel>(model));
      h.d = d, h.e = 1;  // a 20.5 ms burst
      StringVoice v;
      v.trigger(1, false);
      TEST_ASSERT_FALSE(v.done());
      const StringParams p = paramsOf(h);
      static float y[32];
      const int burst = static_cast<int>(0.0205f * kSynthRate), limit = static_cast<int>((t60Formula(d) + 0.1) * kSynthRate);
      int doneAt = -1;
      for (int i = 0; i < limit + 64 && doneAt < 0; i += 32) {
        v.control(p, 32);
        v.render(y, 32, 1.f, gLine);
        if (v.done()) doneAt = i;
      }
      char msg[48];
      snprintf(msg, sizeof msg, "model %d d %.1f: done at %d", model, d, doneAt);
      TEST_ASSERT_TRUE_MESSAGE(doneAt > burst, msg);
      TEST_ASSERT_TRUE_MESSAGE(doneAt <= limit, msg);
      // A new strike wakes it.
      v.trigger(1, true);
      TEST_ASSERT_FALSE(v.done());
      v.control(p, 32);
      v.render(y, 32, 1.f, gLine);
      TEST_ASSERT_FALSE(v.done());
    }
}

// A re-trigger while ringing adds energy without a click: the running resonance is kept (the output is
// the ringing voice + a fresh strike: no reset), and the jump at the re-trigger is no larger than a fresh
// strike's own onset (+ 0.1); both models. Impulse strikes (e 0): no noise stream to diverge.
void test_retrigger_no_click() {
  constexpr int N = 16384, At = 8192;
  static float y[N], ring[N], fresh[N];
  static int16_t l2[kLineLen], l3[kLineLen];
  for (int model = 0; model < static_cast<int>(StringModel::Count); ++model) {
    Hit h = defaults(static_cast<StringModel>(model));
    h.e = 0;
    const StringParams p = paramsOf(h);
    StringVoice v, r, f;
    v.trigger(1, false), r.trigger(1, false);
    memset(y, 0, sizeof y), memset(ring, 0, sizeof ring), memset(fresh, 0, sizeof fresh);
    for (int i = 0; i < N; i += 32) {
      if (i == At) v.trigger(1, true), f.trigger(1, false);
      v.control(p, 32), r.control(p, 32), f.control(p, 32);
      v.render(y + i, 32, 1.f, gLine);
      r.render(ring + i, 32, 1.f, l2);
      f.render(fresh + i, 32, 1.f, l3);
    }
    double before = 0, after = 0;
    for (int i = At - 2048; i < At; ++i) before += y[i] * y[i];
    for (int i = At; i < At + 2048; ++i) after += y[i] * y[i];
    char msg[64];
    snprintf(msg, sizeof msg, "model %d: energy %.3g -> %.3g", model, before, after);
    TEST_ASSERT_TRUE_MESSAGE(after > 2 * before, msg);
    float diff = 0, jump = 0, onset = 0;
    for (int i = At; i < N; ++i) diff = fmaxf(diff, fabsf(y[i] - ring[i] - fresh[i]));
    snprintf(msg, sizeof msg, "model %d: superposition off by %.4f", model, diff);
    TEST_ASSERT_TRUE_MESSAGE(diff < 0.01f, msg);
    for (int i = At - 64; i < At + 2048; ++i) jump = fmaxf(jump, fabsf(y[i] - y[i - 1]));
    for (int i = At; i < At + 2048; ++i) onset = fmaxf(onset, fabsf(fresh[i] - fresh[i - 1]));
    snprintf(msg, sizeof msg, "model %d: jump %.3f, a fresh strike's %.3f", model, jump, onset);
    TEST_ASSERT_TRUE_MESSAGE(jump <= onset + 0.1f, msg);
  }
}

// The model latches at trigger(): a control() with another model keeps the sounding one; the next
// trigger switches (state from silence: as a new voice of that model).
void test_model_switch_at_trigger() {
  static float a[32], b[32];
  static int16_t lb[kLineLen];
  Hit hs = defaults(StringModel::String), hm = defaults(StringModel::Modal);
  hs.e = hm.e = 0;  // impulses: no noise stream to diverge
  StringVoice v, ref;
  v.trigger(1, false), ref.trigger(1, false);
  const StringParams ps = paramsOf(hs), pm = paramsOf(hm);
  for (int blk = 0; blk < 200; ++blk) {
    memset(a, 0, sizeof a), memset(b, 0, sizeof b);
    v.control(blk < 50 ? ps : pm, 32);  // MODAL asked from block 50, no trigger
    ref.control(ps, 32);
    v.render(a, 32, 1.f, gLine);
    ref.render(b, 32, 1.f, lb);
    TEST_ASSERT_EQUAL_MEMORY(b, a, sizeof a);
  }
  StringVoice fresh;
  v.trigger(1, true);
  fresh.trigger(1, false);
  for (int blk = 0; blk < 100; ++blk) {
    memset(a, 0, sizeof a), memset(b, 0, sizeof b);
    v.control(pm, 32);
    fresh.control(pm, 32);
    v.render(a, 32, 1.f, gLine);
    fresh.render(b, 32, 1.f, lb);
    for (int i = 0; i < 32; ++i) TEST_ASSERT_FLOAT_WITHIN(1e-4f, b[i], a[i]);
  }
}

// A strike from silence (keep false) on a ringing voice renders as a new voice (within 1e-4: only the
// exciter low-pass's residue under 1e-6 differs): same inputs
// (the coefficients are kept), a new note, POS moved (MODAL), the half / quarter rate string.
void test_restrike_fresh_identical() {
  static float a[32], b[32];
  static int16_t la[kLineLen], lb[kLineLen];
  for (int model = 0; model < 2; ++model)
    for (float hz : {30.f, 60.f, 220.f, 1760.f})
      for (int change = 0; change < 3; ++change) {
        Hit h = defaults(static_cast<StringModel>(model));
        h.hz = hz, h.e = 0;  // an impulse: no noise stream to diverge
        StringVoice v, fresh;
        StringParams p = paramsOf(h);
        v.trigger(1, false);
        for (int blk = 0; blk < 60; ++blk) {
          memset(a, 0, sizeof a);
          v.control(p, 32);
          v.render(a, 32, 1.f, la);
        }
        if (change == 1) p.hz *= 1.26f;
        if (change == 2) p.pos = 0.4f;
        v.trigger(0.7f, false);
        fresh.trigger(0.7f, false);
        memset(lb, 0x55, sizeof lb);  // the new voice's line: stale data too
        for (int blk = 0; blk < 120; ++blk) {
          memset(a, 0, sizeof a), memset(b, 0, sizeof b);
          v.control(p, 32);
          fresh.control(p, 32);
          v.render(a, 32, 1.f, la);
          fresh.render(b, 32, 1.f, lb);
          char msg[64];
          snprintf(msg, sizeof msg, "model %d %.0f Hz change %d block %d", model, hz, change, blk);
          for (int i = 0; i < 32; ++i) TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-4f, b[i], a[i], msg);
        }
      }
}

// A new STRING voice clears its line (another voice or COMB left data there).
void test_new_voice_clears_line() {
  constexpr int N = 4096;
  static float a[N], b[N];
  const Hit h = defaults(StringModel::String);
  memset(gLine, 0, sizeof gLine);
  play(h, a, N);
  for (int i = 0; i < kLineLen; ++i) gLine[i] = static_cast<int16_t>(i * 977);
  play(h, b, N);
  TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof a);
}

// seed(): voices with other seeds strike other noise (uncorrelated chords).
void test_voice_seed() {
  constexpr int N = 1024;
  static float a[N], b[N];
  Hit h = defaults(StringModel::String);
  h.e = 0.5f;
  for (int k = 0; k < 2; ++k) {
    StringVoice v;
    v.seed(static_cast<uint32_t>(k));
    v.trigger(1, false);
    float* y = k ? b : a;
    memset(y, 0, sizeof(float) * N);
    for (int i = 0; i < N; i += 32) {
      v.control(paramsOf(h), 32);
      v.render(y + i, 32, 1.f, gLine);
    }
  }
  double ab = 0, aa = 0, bb = 0;
  for (int i = 0; i < N; ++i) ab += a[i] * b[i], aa += a[i] * a[i], bb += b[i] * b[i];
  TEST_ASSERT_TRUE(aa > 0 && bb > 0);
  TEST_ASSERT_TRUE(fabs(ab) / sqrt(aa * bb) < 0.9);
}

// ---- Review fixes ----

// done() stays false until the loop has wrapped: a click (or a short burst) into a long period leaves the
// line's read side silent for one period, which is not the end of the note. STRING and MODAL, e 0 / 0.1,
// 110 and 30 Hz (half / quarter rate), 440 Hz: not done before T60 / 2.
void test_done_not_before_first_period() {
  static float y[32];
  for (int model = 0; model < static_cast<int>(StringModel::Count); ++model)
    for (float hz : {440.f, 110.f, 30.f})
      for (float e : {0.f, 0.1f}) {
        Hit h = defaults(static_cast<StringModel>(model));
        h.hz = hz, h.e = e;
        StringVoice v;
        v.trigger(1, false);
        const StringParams p = paramsOf(h);
        const int until = static_cast<int>(0.5 * t60Formula(h.d) * kSynthRate);
        for (int i = 0; i < until; i += 32) {
          v.control(p, 32);
          v.render(y, 32, 1.f, gLine);
          if (v.done()) {
            char msg[64];
            snprintf(msg, sizeof msg, "model %d %.0f Hz e %.1f: done at %d", model, hz, e, i);
            TEST_FAIL_MESSAGE(msg);
          }
        }
      }
}

// MODAL: the T60 of high modes (4 .. 8 kHz) within +-25 % of DAMP's (the bilinear warp must not
// stretch it).
void test_modal_t60_high() {
  constexpr int N = static_cast<int>(2.2 * kSynthRate);
  static float y[N];
  for (float hz : {4000.f, 6000.f, 8000.f})
    for (float d : {0.6f, 1.f}) {
      Hit h = modal();
      h.hz = hz, h.d = d;
      play(h, y, N);
      const double t = t60At(y, hz, 0.2, 2.0, 4096);
      char msg[64];
      snprintf(msg, sizeof msg, "%.0f Hz d %.1f: T60 %.2f s, want %.2f", hz, d, t, t60Formula(d));
      TEST_ASSERT_TRUE_MESSAGE(fabs(t - t60Formula(d)) <= 0.25 * t60Formula(d), msg);
    }
}

// Headroom: a full-velocity strike stays under 1.9 (no rail at 2) for a click, a short and a long burst:
// at the defaults (220 Hz) and BRIGHT 1 at 55 Hz; both models.
void test_strike_headroom() {
  constexpr int N = static_cast<int>(0.5 * kSynthRate);
  static float y[N];
  for (int model = 0; model < static_cast<int>(StringModel::Count); ++model)
    for (int k = 0; k < 2; ++k)
      for (float e : {0.f, 0.5f, 1.f}) {
        Hit h = defaults(static_cast<StringModel>(model));
        if (k) h.hz = 55, h.b = 1;
        h.e = e;
        play(h, y, N);
        float pk = 0;
        for (int i = 0; i < N; ++i) pk = fmaxf(pk, fabsf(y[i]));
        char msg[64];
        snprintf(msg, sizeof msg, "model %d %.0f Hz b %.2f e %.1f: peak %.3f", model, h.hz, h.b, e, pk);
        TEST_ASSERT_TRUE_MESSAGE(pk < 1.9f, msg);
      }
}

// The decay does not depend on the velocity: STRING's T60 at level 0.1 within +-25 % of level 1's.
void test_string_t60_soft() {
  constexpr int N = static_cast<int>(2.2 * kSynthRate);
  static float y[N];
  for (float hz : {220.f, 880.f}) {
    Hit h;
    h.hz = hz;
    play(h, y, N);
    const double loud = t60At(y, hz, 0.2, 2.0, 4096);
    h.level = 0.1f;
    play(h, y, N);
    const double soft = t60At(y, hz, 0.2, 2.0, 4096);
    char msg[64];
    snprintf(msg, sizeof msg, "%.0f Hz: T60 %.2f s at level 1, %.2f s at 0.1", hz, loud, soft);
    TEST_ASSERT_TRUE_MESSAGE(fabs(soft - loud) <= 0.25 * loud, msg);
  }
}

// STRING's T60 at half (60 Hz) and quarter (30 Hz) rate within +-25 % of DAMP's.
void test_string_t60_low_rates() {
  constexpr int N = static_cast<int>(2.2 * kSynthRate);
  static float y[N];
  for (float hz : {60.f, 30.f}) {
    Hit h;
    h.hz = hz, h.d = 0.7f;
    play(h, y, N);
    const double t = t60At(y, hz, 0.2, 2.0, 8192);
    char msg[64];
    snprintf(msg, sizeof msg, "%.0f Hz: T60 %.2f s, want %.2f", hz, t, t60Formula(h.d));
    TEST_ASSERT_TRUE_MESSAGE(fabs(t - t60Formula(h.d)) <= 0.25 * t60Formula(h.d), msg);
  }
}

// Every voice ends: pitch x DAMP x BRIGHT x STRUCT x velocity, STRING and MODAL, reaches done() within
// 80 dB of its (capped) T60 + 0.5 s; at d 1 (10 s), 1 .. 4 kHz still decays over the first 3 s.
void test_every_voice_ends() {
  static float y[32];
  for (int model = 0; model < static_cast<int>(StringModel::Count); ++model)
    for (float hz : {30.f, 110.f, 440.f, 1000.f, 2000.f, 4000.f, 8000.f})
      for (float d : {0.5f, 1.f})
        for (float b : {0.f, 1.f})
          for (float s : {0.f, 1.f})
            for (float level : {1.f, 0.05f}) {
              Hit h;
              h.model = static_cast<StringModel>(model);
              h.hz = hz, h.d = d, h.b = b, h.s = s, h.level = level, h.e = 0.1f;
              StringVoice v;
              v.trigger(level, false);
              const StringParams p = paramsOf(h);
              double t60 = t60Formula(d);
              if (model == 0 && t60 > 20000.0 / hz) t60 = 20000.0 / hz;
              const int limit = static_cast<int>((t60 * 80 / 60 + 0.5) * kSynthRate);
              double e1 = 0, e3 = 0;
              int doneAt = -1;
              for (int i = 0; i < limit && doneAt < 0; i += 32) {
                memset(y, 0, sizeof y);
                v.control(p, 32);
                v.render(y, 32, 1.f, gLine);
                double e = 0;
                for (int j = 0; j < 32; ++j) e += y[j] * y[j];
                if (i >= kSynthRate / 2 && i < kSynthRate) e1 += e;
                if (i >= 5 * kSynthRate / 2 && i < 3 * kSynthRate) e3 += e;
                if (v.done()) doneAt = i;
              }
              char msg[96];
              snprintf(msg, sizeof msg, "model %d %.0f Hz d %.1f b %.0f s %.0f lvl %.2f: done at %d (limit %d)", model,
                       hz, d, b, s, level, doneAt, limit);
              TEST_ASSERT_TRUE_MESSAGE(doneAt >= 0, msg);
              // d 1, 1 .. 4 kHz: from 0.5 .. 1 s to 2.5 .. 3 s it falls by at least half the T60's 2 s drop.
              if (d == 1.f && hz >= 1000 && hz <= 4000)
                TEST_ASSERT_TRUE_MESSAGE(10 * log10(e1 / (e3 + 1e-30)) >= 0.5 * 120 / t60, msg);
            }
}

// Tiny filter states are flushed: a short decay never leaves subnormal output, and ends in exact zeros
// (no denormal tail); both models.
void test_states_flushed() {
  constexpr int N = 8 * kSynthRate;
  static float y[N];
  for (int model = 0; model < static_cast<int>(StringModel::Count); ++model) {
    Hit h = defaults(static_cast<StringModel>(model));
    h.d = 0;
    play(h, y, N);
    for (int i = 0; i < N; ++i)
      if (y[i] != 0 && fabsf(y[i]) < FLT_MIN) {
        char msg[64];
        snprintf(msg, sizeof msg, "model %d: y[%d] = %g subnormal", model, i, y[i]);
        TEST_FAIL_MESSAGE(msg);
      }
    for (int i = N - 1024; i < N; ++i) TEST_ASSERT_EQUAL_FLOAT(0.f, y[i]);
  }
}

// STRING's coefficient cache: unchanged inputs (and a POS move) render exactly as a voice that
// recomputes every update; a pitch / DAMP change mid-way ramps the same in both.
void test_string_cache_identical() {
  static float a[64], b[64];
  static int16_t la[kLineLen], lb[kLineLen];
  for (float hz : {220.f, 60.f}) {
    Hit h = defaults(StringModel::String);
    h.hz = hz, h.e = 0;
    StringVoice va, vb;
    va.trigger(1, false), vb.trigger(1, false);
    StringParams p = paramsOf(h);
    for (int blk = 0; blk < 300; ++blk) {
      if (blk == 100) p.hz = hz * 1.5f, p.damp = 0.5f;
      if (blk == 200) p.pos = 0.7f;
      memset(a, 0, sizeof a), memset(b, 0, sizeof b);
      va.control(p, 32);
      vb.invalidate();
      vb.control(p, 32);
      va.render(a, 32, 1.f, la);
      vb.render(b, 32, 1.f, lb);
      TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof(float) * 32);
    }
  }
}

// A re-strike at another velocity (louder or softer, after the tail's gain-up) adds a strike of its own
// velocity to the running resonance: the output is the ringing voice + a fresh strike; both models.
void test_restrike_velocity_superposition() {
  constexpr int N = 65536, At = 49152;
  static float y[N], ring[N], fresh[N];
  static int16_t l2[kLineLen], l3[kLineLen];
  for (int model = 0; model < static_cast<int>(StringModel::Count); ++model)
    for (int k = 0; k < 2; ++k) {
      const float first = k ? 1.f : 0.5f, second = k ? 0.25f : 1.f;
      Hit h = defaults(static_cast<StringModel>(model));
      h.e = 0, h.d = 0.5f;
      const StringParams p = paramsOf(h);
      StringVoice v, r, f;
      v.trigger(first, false), r.trigger(first, false);
      memset(y, 0, sizeof y), memset(ring, 0, sizeof ring), memset(fresh, 0, sizeof fresh);
      for (int i = 0; i < N; i += 32) {
        if (i == At) v.trigger(second, true), f.trigger(second, false);
        v.control(p, 32), r.control(p, 32), f.control(p, 32);
        v.render(y + i, 32, 1.f, gLine);
        r.render(ring + i, 32, 1.f, l2);
        f.render(fresh + i, 32, 1.f, l3);
      }
      float diff = 0, pk = 0;
      for (int i = At; i < N; ++i) diff = fmaxf(diff, fabsf(y[i] - ring[i] - fresh[i])), pk = fmaxf(pk, fabsf(fresh[i]));
      char msg[80];
      snprintf(msg, sizeof msg, "model %d %.2f -> %.2f: off by %.5f (strike peak %.3f)", model, first, second, diff, pk);
      TEST_ASSERT_TRUE_MESSAGE(pk > 0.02f && diff < 0.01f * second, msg);
    }
}

// MODAL low notes: done() only under -80 dB for a whole period, not at a slow mode's zero crossing: a
// click at 30 .. 110 Hz, DAMP 0.8 (peak ~0.3: -80 dB is ~-70 dB of it), is not freed before the T60.
void test_modal_low_done() {
  static float y[32];
  for (float hz : {30.f, 55.f, 110.f}) {
    Hit h = modal();
    h.hz = hz, h.d = 0.8f, h.e = 0, h.p = 0.3f;
    StringVoice v;
    v.trigger(1, false);
    const StringParams p = paramsOf(h);
    const int until = static_cast<int>(t60Formula(h.d) * kSynthRate);
    for (int i = 0; i < until; i += 32) {
      v.control(p, 32);
      v.render(y, 32, 1.f, gLine);
      if (v.done()) {
        char msg[64];
        snprintf(msg, sizeof msg, "%.0f Hz: done at %.2f s", hz, i / static_cast<double>(kSynthRate));
        TEST_FAIL_MESSAGE(msg);
      }
    }
  }
}

// Scaling a finished exciter (the voice's tail gain-up) never re-activates it.
void test_exciter_scale_inactive() {
  StringExciter x;
  x.trigger(1, 0.3f, 0, 220, 0);
  while (x.active()) x.next();
  for (int i = 0; i < 40; ++i) x.scale(2.f);
  TEST_ASSERT_FALSE(x.active());
}

// MODAL sounds about as loud as STRING at the same macros (peaks within 4 dB): defaults, 110 / 440 Hz.
void test_modal_level_matches_string() {
  constexpr int N = static_cast<int>(0.5 * kSynthRate);
  static float y[N];
  for (float hz : {110.f, 220.f, 440.f}) {
    float pk[2] = {0, 0};
    for (int model = 0; model < 2; ++model) {
      Hit h = defaults(static_cast<StringModel>(model));
      h.hz = hz;
      play(h, y, N);
      for (int i = 0; i < N; ++i) pk[model] = fmaxf(pk[model], fabsf(y[i]));
    }
    const double db = 20 * log10(pk[1] / pk[0]);
    char msg[64];
    snprintf(msg, sizeof msg, "%.0f Hz: MODAL %.3f vs STRING %.3f (%.1f dB)", hz, pk[1], pk[0], db);
    TEST_ASSERT_TRUE_MESSAGE(fabs(db) <= 4, msg);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_exciter_impulse);
  RUN_TEST(test_exciter_burst_length);
  RUN_TEST(test_exciter_brightness);
  RUN_TEST(test_exciter_twin_lfsr);
  RUN_TEST(test_exciter_position_comb);
  RUN_TEST(test_exciter_retrigger);
  RUN_TEST(test_exciter_lp_corner);
  RUN_TEST(test_exciter_fill_matches_next);
  RUN_TEST(test_exciter_seed);
  RUN_TEST(test_string_tuning);
  RUN_TEST(test_string_t60);
  RUN_TEST(test_string_brightness);
  RUN_TEST(test_string_position);
  RUN_TEST(test_string_dispersion);
  RUN_TEST(test_modal_partials);
  RUN_TEST(test_modal_six_modes);
  RUN_TEST(test_modal_t60);
  RUN_TEST(test_modal_brightness_decay);
  RUN_TEST(test_modal_position);
  RUN_TEST(test_modal_top_modes_skipped);
  RUN_TEST(test_modal_cache_identical);
  RUN_TEST(test_done_after_decay);
  RUN_TEST(test_retrigger_no_click);
  RUN_TEST(test_model_switch_at_trigger);
  RUN_TEST(test_new_voice_clears_line);
  RUN_TEST(test_restrike_fresh_identical);
  RUN_TEST(test_voice_seed);
  RUN_TEST(test_bounded_all_macros);
  RUN_TEST(test_restrikes_bounded);
  RUN_TEST(test_done_not_before_first_period);
  RUN_TEST(test_modal_t60_high);
  RUN_TEST(test_strike_headroom);
  RUN_TEST(test_string_t60_soft);
  RUN_TEST(test_string_t60_low_rates);
  RUN_TEST(test_every_voice_ends);
  RUN_TEST(test_states_flushed);
  RUN_TEST(test_string_cache_identical);
  RUN_TEST(test_restrike_velocity_superposition);
  RUN_TEST(test_modal_low_done);
  RUN_TEST(test_exciter_scale_inactive);
  RUN_TEST(test_modal_level_matches_string);
  return UNITY_END();
}
