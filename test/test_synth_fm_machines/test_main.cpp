#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>
#include "synth_fm_machines.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static float buf[4000];

static void macros(float* m, float v) {
  for (int k = 0; k < kFmMacros; ++k) m[k] = v;
}

void test_all_machines_finite() {
  const float vals[] = {0, 64, 127};
  const float notes[] = {24, 60, 108};
  for (int mc = 0; mc < static_cast<int>(FmMachine::Count); ++mc)
    for (float v : vals)
      for (float note : notes) {
        float m[kFmMacros];
        macros(m, v);
        FmParams p;
        fmMachine(static_cast<uint8_t>(mc), m, note, p);
        FmVoice fv;
        fv.trigger(false);
        for (int i = 0; i < 4000; ++i) buf[i] = 0;
        for (int pos = 0; pos < 4000; pos += 32) {
          fmMachine(static_cast<uint8_t>(mc), m, note, p);
          fv.control(p, 32);
          fv.render(buf + pos, 32, 1.f);
        }
        for (int i = 0; i < 4000; ++i) {
          TEST_ASSERT_FALSE(isnan(buf[i]) || isinf(buf[i]));
          TEST_ASSERT_TRUE(fabsf(buf[i]) < 4.f);
        }
      }
}

void test_one_shot_vs_gated() {
  float m[kFmMacros];
  macros(m, 64);
  FmParams p;
  for (int mc = 0; mc < static_cast<int>(FmMachine::Count); ++mc) {
    fmMachine(static_cast<uint8_t>(mc), m, 60, p);
    TEST_ASSERT_EQUAL(!fmGated(static_cast<uint8_t>(mc)), p.oneShot);
  }
}

void test_kick_color_raises_index() {
  float m[kFmMacros];
  macros(m, 64);
  FmParams a, b, c;
  m[kMacCol] = 0;
  fmMachine(static_cast<uint8_t>(FmMachine::Kick), m, 60, a);
  m[kMacCol] = 64;
  fmMachine(static_cast<uint8_t>(FmMachine::Kick), m, 60, b);
  m[kMacCol] = 127;
  fmMachine(static_cast<uint8_t>(FmMachine::Kick), m, 60, c);
  TEST_ASSERT_TRUE(a.op[1].level < b.op[1].level);
  TEST_ASSERT_TRUE(b.op[1].level < c.op[1].level);
}

void test_kick_pitch_follows_note() {
  float m[kFmMacros];
  macros(m, 64);
  FmParams p;
  fmMachine(static_cast<uint8_t>(FmMachine::Kick), m, 60, p);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 55.f, p.hz);
  fmMachine(static_cast<uint8_t>(FmMachine::Kick), m, 72, p);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 110.f, p.hz);
}

void test_chord_maj_ratios() {
  float m[kFmMacros];
  macros(m, 64);
  m[kMacShp] = 0;
  FmParams p;
  fmMachine(static_cast<uint8_t>(FmMachine::Chord), m, 60, p);
  TEST_ASSERT_TRUE(p.alg == FmAlg::Additive);
  // MAJ 0 4 7 12 with the fixed detune -6 / +4 / -2 / +7 cents.
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.996540f, p.op[0].ratio);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.262835f, p.op[1].ratio);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.496577f, p.op[2].ratio);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.008103f, p.op[3].ratio);
  TEST_ASSERT_EQUAL_STRING("MAJ", fmChordName(0));
  TEST_ASSERT_EQUAL_STRING("MI9", fmChordName(127));
  TEST_ASSERT_EQUAL_STRING("MI7", fmChordName(5 * 127 / 12 + 1));
}

// TONE: two voices -d / +d cents, d = 4 + 8 x COLOR. Sine, square, bell: two pairs with the same timbre;
// saw: two feedback carriers; stack: the modulators.
void test_tone_detune() {
  float m[kFmMacros];
  macros(m, 64);
  const float pairZones[] = {0, 127 * 2.5f / 5, 127};  // zones 0, 2, 4
  const float cols[] = {0, 127};
  const float lo[] = {0.997692f, 0.993092f}, hi[] = {1.002313f, 1.006956f};  // -+4, -+12 cents
  FmParams p;
  for (int c = 0; c < 2; ++c) {
    m[kMacCol] = cols[c];
    for (float z : pairZones) {
      m[kMacShp] = z;
      fmMachine(static_cast<uint8_t>(FmMachine::Tone), m, 60, p);
      TEST_ASSERT_TRUE(p.alg == FmAlg::TwoPairs);
      TEST_ASSERT_FLOAT_WITHIN(1e-4f, lo[c], p.op[0].ratio);
      TEST_ASSERT_FLOAT_WITHIN(1e-4f, hi[c], p.op[2].ratio);
      TEST_ASSERT_FLOAT_WITHIN(1e-4f, p.op[1].ratio / p.op[0].ratio, p.op[3].ratio / p.op[2].ratio);
      TEST_ASSERT_EQUAL_FLOAT(p.op[0].level, p.op[2].level);
      TEST_ASSERT_EQUAL_FLOAT(p.op[1].level, p.op[3].level);
    }
    m[kMacShp] = 127 * 1.5f / 5;  // zone 1: saw
    fmMachine(static_cast<uint8_t>(FmMachine::Tone), m, 60, p);
    TEST_ASSERT_TRUE(p.alg == FmAlg::Additive);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, lo[c], p.op[0].ratio);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, hi[c], p.op[1].ratio);
    TEST_ASSERT_EQUAL_FLOAT(p.op[0].fb, p.op[1].fb);
    TEST_ASSERT_EQUAL_FLOAT(0, p.op[2].level);
    m[kMacShp] = 127 * 3.5f / 5;  // zone 3: stack
    fmMachine(static_cast<uint8_t>(FmMachine::Tone), m, 60, p);
    TEST_ASSERT_TRUE(p.alg == FmAlg::Stack);
    TEST_ASSERT_EQUAL_FLOAT(1, p.op[0].ratio);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, hi[c], p.op[1].ratio);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2 * lo[c], p.op[2].ratio);
  }
}

void test_hat_shape_mixes_metal_and_noise() {
  float m[kFmMacros];
  macros(m, 64);
  FmParams p;
  m[kMacShp] = 0;
  fmMachine(static_cast<uint8_t>(FmMachine::Hat), m, 60, p);
  TEST_ASSERT_EQUAL_FLOAT(0, p.noise);
  TEST_ASSERT_TRUE(p.op[0].level > 0);
  m[kMacShp] = 127;
  fmMachine(static_cast<uint8_t>(FmMachine::Hat), m, 60, p);
  TEST_ASSERT_EQUAL_FLOAT(1, p.noise);
  TEST_ASSERT_EQUAL_FLOAT(0, p.op[0].level);
  TEST_ASSERT_TRUE(p.filterAll);
}

void test_decay_macro_lengthens() {
  float m[kFmMacros];
  macros(m, 64);
  FmParams a, b;
  m[kMacDec] = 10;
  fmMachine(static_cast<uint8_t>(FmMachine::Snare), m, 60, a);
  m[kMacDec] = 110;
  fmMachine(static_cast<uint8_t>(FmMachine::Snare), m, 60, b);
  TEST_ASSERT_TRUE(b.ampMs > a.ampMs * 10);
}

// Renders a machine's first n samples into out (zeroed first); sat: FmParams::sat (SAT 127 = 1).
static void render(uint8_t mc, const float* m, float note, float* out, int n, float sat = 0) {
  FmParams p;
  FmVoice fv;
  fv.trigger(false);
  for (int i = 0; i < n; ++i) out[i] = 0;
  for (int pos = 0; pos < n; pos += 32) {
    fmMachine(mc, m, note, p);
    p.sat = sat;
    fv.control(p, 32);
    fv.render(out + pos, 32, 1.f);
  }
}

void test_out_of_range_machine_is_kick() {
  float m[kFmMacros];
  macros(m, 64);
  FmParams a, b;
  fmMachine(200, m, 60, a);
  fmMachine(static_cast<uint8_t>(FmMachine::Kick), m, 60, b);
  TEST_ASSERT_EQUAL_FLOAT(b.hz, a.hz);
  TEST_ASSERT_TRUE(a.alg == b.alg);
  TEST_ASSERT_EQUAL_FLOAT(b.ampMs, a.ampMs);
  TEST_ASSERT_EQUAL_FLOAT(b.pitchEnv, a.pitchEnv);
  for (int i = 0; i < kFmOps; ++i) {
    TEST_ASSERT_EQUAL_FLOAT(b.op[i].ratio, a.op[i].ratio);
    TEST_ASSERT_EQUAL_FLOAT(b.op[i].level, a.op[i].level);
  }
  static float other[4000];
  render(200, m, 60, buf, 4000);
  render(static_cast<uint8_t>(FmMachine::Kick), m, 60, other, 4000);
  for (int i = 0; i < 4000; ++i) TEST_ASSERT_EQUAL_FLOAT(other[i], buf[i]);
}

// KICK: TwoPairs; op3 the body carrier at x2.5..3 (SHAPE), level 0.5 x COLOR, 30..80 ms in total (DECAY);
// op4 its modulator. COLOR 0: no body.
void test_kick_body_params() {
  float m[kFmMacros];
  macros(m, 64);
  FmParams p;
  m[kMacCol] = 0;
  fmMachine(static_cast<uint8_t>(FmMachine::Kick), m, 60, p);
  TEST_ASSERT_TRUE(p.alg == FmAlg::TwoPairs);
  TEST_ASSERT_EQUAL_FLOAT(0, p.op[2].level);
  TEST_ASSERT_EQUAL_FLOAT(0, p.op[3].level);
  m[kMacCol] = 127;
  m[kMacShp] = 0;
  m[kMacDec] = 127;
  fmMachine(static_cast<uint8_t>(FmMachine::Kick), m, 60, p);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.5f, p.op[2].level);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.5f, p.op[2].ratio);
  TEST_ASSERT_EQUAL_FLOAT(p.op[2].ratio, p.op[3].ratio);
  TEST_ASSERT_TRUE(p.op[3].level > 0);
  // Decays multiply (rates add): the body lasts 80 ms under the 4 s amp.
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 80.f, 1.f / (1.f / p.op[2].decayMs + 1.f / p.ampMs));
  m[kMacShp] = 127;
  fmMachine(static_cast<uint8_t>(FmMachine::Kick), m, 60, p);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3.f, p.op[2].ratio);
}

// COLOR 0: the kick is the 2-operator one (as a Stack) sample for sample.
void test_kick_color0_is_two_op() {
  float m[kFmMacros];
  macros(m, 64);
  m[kMacCol] = 0;
  static float a[4000], b[4000];
  render(static_cast<uint8_t>(FmMachine::Kick), m, 60, a, 4000);
  FmVoice fv;
  fv.trigger(false);
  for (int i = 0; i < 4000; ++i) b[i] = 0;
  for (int pos = 0; pos < 4000; pos += 32) {
    FmParams p;
    fmMachine(static_cast<uint8_t>(FmMachine::Kick), m, 60, p);
    p.alg = FmAlg::Stack;
    fv.control(p, 32);
    fv.render(b + pos, 32, 1.f);
  }
  TEST_ASSERT_EQUAL_MEMORY(b, a, sizeof(a));
}

// KICK's sweep cap follows COLOR continuously: COLOR 1 (body level ~0.004) caps the sweep as COLOR 0 does
// (the sub's ratio), not as the full body (x2.5..3); the cap reaches the body's by body level 0.05.
void test_kick_sweep_cap_continuous_in_color() {
  float m[kFmMacros];
  macros(m, 64);
  m[kMacSwp] = 127;
  m[kMacShp] = 0;  // sub modulator x0.5: the widest gap to the body (x2.5)
  FmParams a, b;
  for (float note = 72; note <= 127; note += 6) {
    m[kMacCol] = 0;
    fmMachine(static_cast<uint8_t>(FmMachine::Kick), m, note, a);
    float prev = a.pitchEnv;
    for (int col = 1; col <= 127; ++col) {
      m[kMacCol] = col;
      fmMachine(static_cast<uint8_t>(FmMachine::Kick), m, note, b);
      char msg[48];
      snprintf(msg, sizeof msg, "note %.0f color %d: %.2f -> %.2f", note, col, prev, b.pitchEnv);
      TEST_ASSERT_TRUE_MESSAGE(fabsf(b.pitchEnv - prev) < 1.5f, msg);  // no step from one COLOR to the next
      prev = b.pitchEnv;
    }
  }
}

// Goertzel energy of x over lo..hi Hz in 5 Hz steps.
static double bandEnergy(const float* x, int n, float lo, float hi) {
  double e = 0;
  for (float f = lo; f <= hi; f += 5) {
    const double k = 2 * cos(2 * M_PI * f / kSynthRate);
    double s1 = 0, s2 = 0;
    for (int i = 0; i < n; ++i) {
      const double s0 = x[i] + k * s1 - s2;
      s2 = s1;
      s1 = s0;
    }
    e += s1 * s1 + s2 * s2 - k * s1 * s2;
  }
  return e;
}

// Kick render with or without the body (op3 silenced every tick).
static void renderKick(const float* m, float* out, int n, bool body) {
  FmVoice fv;
  fv.trigger(false);
  for (int i = 0; i < n; ++i) out[i] = 0;
  for (int pos = 0; pos < n; pos += 32) {
    FmParams p;
    fmMachine(static_cast<uint8_t>(FmMachine::Kick), m, 60, p);
    if (!body) p.op[2].level = 0;
    fv.control(p, 32);
    fv.render(out + pos, 32, 1.f);
  }
}

// The body fills 150..400 Hz: well above the same kick without it, and more with COLOR (SWEEP 0).
void test_kick_body_fills_low_mids() {
  float m[kFmMacros];
  macros(m, 64);
  m[kMacSwp] = 0;
  static float a[4096], b[4096];
  renderKick(m, a, 4096, true);
  renderKick(m, b, 4096, false);
  TEST_ASSERT_TRUE(bandEnergy(a, 4096, 150, 400) > 1.5 * bandEnergy(b, 4096, 150, 400));
  double e[3];
  const float cols[] = {32, 64, 96};
  for (int k = 0; k < 3; ++k) {
    m[kMacCol] = cols[k];
    renderKick(m, a, 4096, true);
    e[k] = bandEnergy(a, 4096, 150, 400);
  }
  TEST_ASSERT_TRUE(e[0] < e[1] && e[1] < e[2]);
}

void test_every_machine_sounds() {
  const float m[kFmMacros] = {85, 40, 32, 70, 50};  // Instrument defaults
  for (int mc = 0; mc < static_cast<int>(FmMachine::Count); ++mc) {
    render(static_cast<uint8_t>(mc), m, 60, buf, 4000);
    double sum = 0;
    for (int i = 0; i < 4000; ++i) sum += static_cast<double>(buf[i]) * buf[i];
    TEST_ASSERT_TRUE_MESSAGE(sqrt(sum / 4000) > 0.01, "silent machine");
  }
}

// Carrier feedback peak (fb x (1 + fbEnv)) stays below the chaotic range at any COLOR/SWEEP/note.
void test_feedback_peak_bounded() {
  const FmMachine mcs[] = {FmMachine::Tone, FmMachine::Chord, FmMachine::Hat, FmMachine::Kick};
  const float notes[] = {24, 60, 108};
  float m[kFmMacros];
  for (FmMachine mc : mcs)
    for (float note : notes)
      for (int shp = 0; shp <= 127; shp += 9) {
        macros(m, 127);
        m[kMacShp] = static_cast<float>(shp);
        FmParams p;
        fmMachine(static_cast<uint8_t>(mc), m, note, p);
        for (int i = 0; i < kFmOps; ++i) TEST_ASSERT_TRUE(p.op[i].fb * (1 + p.fbEnv) <= 2.5f);
      }
  // The TONE saw at C4 reaches the raised peak (kFbMax 2.4).
  macros(m, 127);
  m[kMacShp] = 127 * 1.5f / 5;  // zone 1
  FmParams p;
  fmMachine(static_cast<uint8_t>(FmMachine::Tone), m, 60, p);
  TEST_ASSERT_TRUE(p.op[0].fb * (1 + p.fbEnv) > 2.3f);
}

// SWEEP/CONTOUR shape the TONE saw even at COLOR 0.
void test_tone_sweep_at_zero_color() {
  float m[kFmMacros];
  macros(m, 64);
  m[kMacShp] = 127 * 1.5f / 5;  // zone 1: feedback saw
  m[kMacCol] = 0;
  m[kMacSwp] = 127;
  FmParams p;
  fmMachine(static_cast<uint8_t>(FmMachine::Tone), m, 60, p);
  TEST_ASSERT_TRUE(p.op[0].fb > 0);
  TEST_ASSERT_TRUE(p.op[0].fb * (1 + p.fbEnv) > 1.f);
  m[kMacSwp] = 0;
  fmMachine(static_cast<uint8_t>(FmMachine::Tone), m, 60, p);
  TEST_ASSERT_EQUAL_FLOAT(0, p.op[0].fb * (1 + p.fbEnv));
}

// One-shot parts multiply with the amp decay: the total length follows DECAY, not about half of it.
void test_decay_not_doubled() {
  float m[kFmMacros];
  macros(m, 64);
  m[kMacCon] = 127;  // HAT/METAL: parts as long as the amp
  const FmMachine mcs[] = {FmMachine::Metal, FmMachine::Hat};
  for (FmMachine mc : mcs) {
    FmParams p;
    fmMachine(static_cast<uint8_t>(mc), m, 60, p);
    TEST_ASSERT_EQUAL_FLOAT(0, p.op[0].decayMs);
  }
  FmParams p;
  fmMachine(static_cast<uint8_t>(FmMachine::Clap), m, 60, p);
  TEST_ASSERT_EQUAL_FLOAT(0, p.noiseMs);  // CLAP: the amp decay (after the bursts) alone
  TEST_ASSERT_EQUAL_FLOAT(5.f * powf(800.f, 64.f / 127.f), p.ampMs);
  fmMachine(static_cast<uint8_t>(FmMachine::Snare), m, 60, p);  // CONTOUR 127: noise rings the longest
  TEST_ASSERT_EQUAL_FLOAT(0, p.noiseMs);
}

// CLAP at low DECAY, loosest SHAPE: the amp decay leaves the last burst within 6 dB (RMS) of the first.
void test_clap_bursts_even() {
  float m[kFmMacros];
  macros(m, 64);
  m[kMacDec] = 0;
  m[kMacShp] = 127;
  FmParams p;
  fmMachine(static_cast<uint8_t>(FmMachine::Clap), m, 60, p);
  const int burst = static_cast<int>(p.burstMs * kSynthRate / 1000.f);
  const int n = burst * p.bursts;
  static float out[8192];
  TEST_ASSERT_TRUE(n <= 8192 - 32);
  render(static_cast<uint8_t>(FmMachine::Clap), m, 60, out, (n + 31) / 32 * 32);
  double first = 0, last = 0;  // energy of each burst (noise peaks vary too much)
  for (int i = 0; i < burst; ++i) {
    first += static_cast<double>(out[i]) * out[i];
    last += static_cast<double>(out[n - burst + i]) * out[n - burst + i];
  }
  TEST_ASSERT_TRUE(first > 0);
  TEST_ASSERT_TRUE(last > first * 0.25);  // RMS within 6 dB
}

// CLAP voice frees right after its tail: bursts + DECAY (to -80 dB: x 4/3) + a little.
void test_clap_done_after_tail() {
  float m[kFmMacros];
  macros(m, 64);
  const float decs[] = {0, 64, 100};
  for (float d : decs) {
    m[kMacDec] = d;
    FmParams p;
    fmMachine(static_cast<uint8_t>(FmMachine::Clap), m, 60, p);
    const float limitMs = p.bursts * p.burstMs + p.ampMs * 4.f / 3.f + 5.f;
    const int limit = static_cast<int>(limitMs * kSynthRate / 1000.f);
    FmVoice fv;
    fv.trigger(false);
    int t = 0;
    while (!fv.done() && t < limit + 64) {
      fmMachine(static_cast<uint8_t>(FmMachine::Clap), m, 60, p);
      fv.control(p, 32);
      fv.render(buf, 32, 1.f);
      t += 32;
    }
    TEST_ASSERT_TRUE(fv.done());
    TEST_ASSERT_TRUE(t <= limit);
    TEST_ASSERT_TRUE(t >= static_cast<int>(p.bursts * p.burstMs * kSynthRate / 1000.f));
  }
}

// High notes: all HAT partials and the KICK sweep peak stay below ~13 kHz.
void test_high_pitch_partials_audible() {
  float m[kFmMacros];
  macros(m, 127);
  FmParams p;
  for (float note = 60; note <= 127; note += 6) {
    fmMachine(static_cast<uint8_t>(FmMachine::Hat), m, note, p);
    for (int i = 0; i < kFmOps; ++i) TEST_ASSERT_TRUE(p.hz * p.op[i].ratio <= 13001.f);
    fmMachine(static_cast<uint8_t>(FmMachine::Kick), m, note, p);
    TEST_ASSERT_TRUE(p.hz * p.op[1].ratio * exp2f(p.pitchEnv / 12) <= 13001.f);
    TEST_ASSERT_TRUE(p.hz * p.op[2].ratio * exp2f(p.pitchEnv / 12) <= 13001.f);  // the body too
  }
}

// In-place radix-2 FFT; n a power of two.
static void fft(double* re, double* im, int n) {
  for (int i = 1, j = 0; i < n; ++i) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      double t = re[i];
      re[i] = re[j], re[j] = t;
      t = im[i];
      im[i] = im[j], im[j] = t;
    }
  }
  for (int len = 2; len <= n; len <<= 1) {
    const double a = -2 * M_PI / len, wr = cos(a), wi = sin(a);
    for (int i = 0; i < n; i += len) {
      double xr = 1, xi = 0;
      for (int k = 0; k < len / 2; ++k) {
        const int p = i + k, q = p + len / 2;
        const double vr = re[q] * xr - im[q] * xi, vi = re[q] * xi + im[q] * xr;
        re[q] = re[p] - vr, im[q] = im[p] - vi;
        re[p] += vr, im[p] += vi;
        const double t = xr * wr - xi * wi;
        xi = xr * wi + xi * wr;
        xr = t;
      }
    }
  }
}

// Share of a render's energy at >= 19 kHz (first 4096 samples, Hann window): partials and aliases at the top.
static double hfShare(uint8_t mc, const float* m, float note, float sat = 0) {
  constexpr int kN = 4096;
  static float x[kN];
  static double re[kN], im[kN];
  render(mc, m, note, x, kN, sat);
  for (int i = 0; i < kN; ++i) {
    re[i] = x[i] * (0.5 - 0.5 * cos(2 * M_PI * i / kN));
    im[i] = 0;
  }
  fft(re, im, kN);
  double all = 0, top = 0;
  for (int k = 1; k < kN / 2; ++k) {
    const double e = re[k] * re[k] + im[k] * im[k];
    all += e;
    if (k * static_cast<double>(kSynthRate) / kN >= 19000) top += e;
  }
  return all > 0 ? top / all : 0;
}

// At full COLOR / SWEEP / CONTOUR the operator machines keep < 1 % of their energy above 19 kHz (measured
// <= 0.6 %). SNARE, CLAP, HAT are out: their noise is broadband by design. TONE skips the stack zone (3):
// its nested modulator's sidebands are not bounded by fmMaxIndex (as before).
void test_limits_keep_top_clean() {
  const FmMachine mcs[] = {FmMachine::Kick, FmMachine::Metal, FmMachine::Perc, FmMachine::Tone, FmMachine::Chord};
  const float notes[] = {36, 60, 72, 84, 96};
  const float shapes[] = {0, 32, 64, 127};  // TONE zones 0, 1, 2, 4
  for (FmMachine mc : mcs)
    for (float note : notes)
      for (float shp : shapes) {
        const float m[kFmMacros] = {100, 127, shp, 127, 127};
        const double r = hfShare(static_cast<uint8_t>(mc), m, note);
        char msg[64];
        snprintf(msg, sizeof msg, "machine %d note %.0f shape %.0f: %.4f", static_cast<int>(mc), note, shp, r);
        TEST_ASSERT_TRUE_MESSAGE(r < 0.01, msg);
      }
}

// SAT 127 (tanh drive x8, no oversampling) at the top notes: the operator machines keep < 5 % of their energy
// above 19 kHz. Measured max (over SHAPE 0, 32, 64, 127; full COLOR / SWEEP / CONTOUR), SAT 127 vs SAT 0:
//   note 84: KICK 0.12 % (0.01), METAL 0.78 % (0.06), PERC 2.75 % (0.31), TONE 3.87 % (0.59), CHORD 1.81 % (0.12)
//   note 96: KICK 0.26 % (0.01), METAL 3.14 % (0.54), PERC 2.85 % (0.47), TONE 3.72 % (0.21), CHORD 3.57 % (0.27)
// What is up there is the shaper's harmonics and their aliases (no oversampling): the guard is for regressions.
void test_sat_keeps_top_bounded() {
  const FmMachine mcs[] = {FmMachine::Kick, FmMachine::Metal, FmMachine::Perc, FmMachine::Tone, FmMachine::Chord};
  const float notes[] = {84, 96};
  const float shapes[] = {0, 32, 64, 127};
  for (FmMachine mc : mcs)
    for (float note : notes)
      for (float shp : shapes) {
        const float m[kFmMacros] = {100, 127, shp, 127, 127};
        const double r = hfShare(static_cast<uint8_t>(mc), m, note, 1.f);
        char msg[64];
        snprintf(msg, sizeof msg, "machine %d note %.0f shape %.0f: %.4f", static_cast<int>(mc), note, shp, r);
        TEST_ASSERT_TRUE_MESSAGE(r < 0.05, msg);
      }
}

// Golden renders of every machine: macro sets x notes x SAT, odd spans, a choke. Captured on the code
// before the run() fast paths.
static uint32_t goldenMachine(uint8_t mc) {
  static const int spans[] = {32, 32, 7, 25, 32, 1, 31, 32};
  static const float sets[4][kFmMacros] = {{0, 0, 0, 0, 0}, {64, 64, 64, 64, 64}, {127, 127, 127, 127, 127},
                                           {100, 20, 90, 110, 30}};
  static const float notes[] = {36, 60, 90};
  static float out[2048];
  uint32_t h = 2166136261u;
  for (int si = 0; si < 4; ++si)
    for (float note : notes)
      for (int sat = 0; sat < 2; ++sat) {
        FmVoice v;
        v.trigger(false);
        for (int i = 0; i < 2048; ++i) out[i] = 0;
        int pos = 0;
        for (int k = 0; pos < 2048; ++k) {
          const int sp = spans[k % 8] < 2048 - pos ? spans[k % 8] : 2048 - pos;
          float m[kFmMacros];
          for (int j = 0; j < kFmMacros; ++j) m[j] = sets[si][j];
          m[2] += (k % 9) * 0.37f;  // an LFO on SHAPE
          FmParams p;
          fmMachine(mc, m, note + (k % 13) * 0.05f, p);
          p.sat = sat ? 0.5f : 0;
          if (k == 40) v.trigger(true);
          v.control(p, sp);
          v.render(out + pos, sp, 0.9f);
          pos += sp;
        }
        for (int i = 0; i < 2048; ++i) {
          uint32_t x;
          memcpy(&x, &out[i], 4);
          for (int b = 0; b < 4; ++b) h = (h ^ ((x >> (8 * b)) & 0xFFu)) * 16777619u;
        }
      }
  return h;
}

void test_golden_machines() {
  static const uint32_t kGolden[8] = {0x748B7456u, 0xB4C0E6C6u, 0xCC753302u, 0x366E26DEu,
                                       0x8D0931FBu, 0xA6EEE19Cu, 0x60B95DF4u, 0xB2E6652Au};
  bool ok = true;
  char msg[160] = "";
  for (int mc = 0; mc < 8; ++mc) {
    const uint32_t g = goldenMachine(static_cast<uint8_t>(mc));
    ok = ok && g == kGolden[mc];
    snprintf(msg + strlen(msg), sizeof msg - strlen(msg), " 0x%08X", static_cast<unsigned>(g));
  }
  TEST_ASSERT_TRUE_MESSAGE(ok, msg);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_golden_machines);
  RUN_TEST(test_all_machines_finite);
  RUN_TEST(test_one_shot_vs_gated);
  RUN_TEST(test_kick_color_raises_index);
  RUN_TEST(test_kick_pitch_follows_note);
  RUN_TEST(test_chord_maj_ratios);
  RUN_TEST(test_tone_detune);
  RUN_TEST(test_hat_shape_mixes_metal_and_noise);
  RUN_TEST(test_decay_macro_lengthens);
  RUN_TEST(test_out_of_range_machine_is_kick);
  RUN_TEST(test_kick_body_params);
  RUN_TEST(test_kick_color0_is_two_op);
  RUN_TEST(test_kick_sweep_cap_continuous_in_color);
  RUN_TEST(test_kick_body_fills_low_mids);
  RUN_TEST(test_every_machine_sounds);
  RUN_TEST(test_feedback_peak_bounded);
  RUN_TEST(test_tone_sweep_at_zero_color);
  RUN_TEST(test_decay_not_doubled);
  RUN_TEST(test_clap_bursts_even);
  RUN_TEST(test_clap_done_after_tail);
  RUN_TEST(test_high_pitch_partials_audible);
  RUN_TEST(test_limits_keep_top_clean);
  RUN_TEST(test_sat_keeps_top_bounded);
  return UNITY_END();
}
