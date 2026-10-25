#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>
#include "synth_fm.h"
#include "synth_osc.h"

using namespace mt;

void setUp() {}
void tearDown() {}

constexpr int kSpan = 32;  // control period, as Synth::kControl

// Renders n samples, calling control() every kSpan samples like the synth does.
static void run(FmVoice& v, const FmParams& p, float* out, int n) {
  for (int i = 0; i < n; ++i) out[i] = 0;
  for (int pos = 0; pos < n; pos += kSpan) {
    v.control(p, kSpan);
    v.render(out + pos, n - pos < kSpan ? n - pos : kSpan, 1.f);
  }
}

static FmParams sine(float hz) {
  FmParams p;
  p.alg = FmAlg::Stack;
  p.hz = hz;
  p.op[0].level = 1;
  p.oneShot = false;
  return p;
}

static int crossings(const float* b, int n) {
  int c = 0;
  for (int i = 1; i < n; ++i) c += b[i - 1] >= 0 && b[i] < 0;
  return c;
}

static float buf[kSynthRate];

void test_carrier_frequency() {
  FmVoice v;
  v.trigger(false);
  run(v, sine(1000), buf, kSynthRate);
  TEST_ASSERT_INT_WITHIN(2, 1000, crossings(buf, kSynthRate));
}

void test_zero_index_is_pure_sine() {
  FmVoice v;
  v.trigger(false);
  FmParams p = sine(500);
  p.op[1].ratio = 1;  // modulator present, level 0
  run(v, p, buf, 2000);
  for (int i = 0; i < 2000; ++i) TEST_ASSERT_FLOAT_WITHIN(0.005f, sinf(6.2831853f * 500 * i / kSynthRate), buf[i]);
}

void test_modulation_changes_wave() {
  FmVoice v;
  v.trigger(false);
  FmParams p = sine(500);
  p.op[1].level = 3;
  run(v, p, buf, 2000);
  float diff = 0;
  for (int i = 0; i < 2000; ++i) diff += fabsf(buf[i] - sinf(6.2831853f * 500 * i / kSynthRate));
  TEST_ASSERT_TRUE(diff / 2000 > 0.1f);
}

void test_carriers_by_algorithm() {
  TEST_ASSERT_TRUE(fmCarrier(FmAlg::Stack, 0));
  TEST_ASSERT_FALSE(fmCarrier(FmAlg::Stack, 1));
  TEST_ASSERT_TRUE(fmCarrier(FmAlg::TwoPairs, 2));
  TEST_ASSERT_FALSE(fmCarrier(FmAlg::TwoPairs, 3));
  TEST_ASSERT_TRUE(fmCarrier(FmAlg::OneToThree, 2));
  TEST_ASSERT_FALSE(fmCarrier(FmAlg::OneToThree, 3));
  TEST_ASSERT_TRUE(fmCarrier(FmAlg::Additive, 3));
  // A modulator alone is silent; a carrier alone sounds.
  FmVoice v;
  v.trigger(false);
  FmParams p = sine(500);
  p.op[0].level = 0;
  p.op[2].level = 1;
  run(v, p, buf, 1000);
  for (int i = 0; i < 1000; ++i) TEST_ASSERT_EQUAL_FLOAT(0, buf[i]);
  p.alg = FmAlg::TwoPairs;
  FmVoice w;
  w.trigger(false);
  run(w, p, buf, 1000);
  TEST_ASSERT_TRUE(crossings(buf, 1000) > 10);
}

void test_one_shot_decays_and_ends() {
  FmVoice v;
  v.trigger(false);
  FmParams p = sine(200);
  p.oneShot = true;
  p.ampMs = 50;
  run(v, p, buf, 160);  // 5 ms
  float peak = 0;
  for (int i = 0; i < 160; ++i) peak = fmaxf(peak, fabsf(buf[i]));
  TEST_ASSERT_TRUE(peak > 0.5f);
  TEST_ASSERT_FALSE(v.done());
  run(v, p, buf, 3200);  // +100 ms
  TEST_ASSERT_TRUE(v.done());
}

void test_pitch_envelope_starts_high() {
  FmVoice v;
  v.trigger(false);
  FmParams p = sine(500);
  p.pitchEnv = 12;
  p.pitchMs = 300;
  run(v, p, buf, kSynthRate);
  const int early = crossings(buf, 640);                  // first 20 ms
  const int late = crossings(buf + kSynthRate - 640, 640);  // last 20 ms
  TEST_ASSERT_TRUE(early > late * 3 / 2);
}

void test_above_nyquist_is_silent() {
  FmVoice v;
  v.trigger(false);
  run(v, sine(20000), buf, 1000);
  for (int i = 0; i < 1000; ++i) TEST_ASSERT_EQUAL_FLOAT(0, buf[i]);
}

void test_max_index_falls_with_pitch() {
  TEST_ASSERT_EQUAL_FLOAT(12, fmMaxIndex(100));
  TEST_ASSERT_EQUAL_FLOAT(12, fmMaxIndex(0));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.f, fmMaxIndex(2000));  // sidebands below ~16 kHz
  TEST_ASSERT_TRUE(fmMaxIndex(6000) < 2.f);
  TEST_ASSERT_TRUE(fmMaxIndex(20000) >= 0.2f);
}

void test_feedback_bounded() {
  FmVoice v;
  v.trigger(false);
  FmParams p = sine(300);
  p.op[0].fb = 20;
  run(v, p, buf, 4000);
  float diff = 0;
  for (int i = 0; i < 4000; ++i) {
    TEST_ASSERT_FALSE(isnan(buf[i]));
    TEST_ASSERT_TRUE(fabsf(buf[i]) <= 1.001f);
    diff += fabsf(buf[i] - sinf(6.2831853f * 300 * i / kSynthRate));
  }
  TEST_ASSERT_TRUE(diff / 4000 > 0.1f);
}

void test_noise_filter_modes() {
  FmParams p;
  p.oneShot = false;
  p.noise = 1;
  p.filterMode = Svf::Mode::Lp;
  p.filterHz = 300;
  FmVoice lp;
  lp.trigger(false);
  run(lp, p, buf, 8000);
  const int lpc = crossings(buf, 8000);
  p.filterMode = Svf::Mode::Hp;
  p.filterHz = 6000;
  FmVoice hp;
  hp.trigger(false);
  run(hp, p, buf, 8000);
  TEST_ASSERT_TRUE(crossings(buf, 8000) > lpc * 3);
}

void test_clap_bursts_retrigger_noise() {
  FmParams p;
  p.oneShot = false;
  p.noise = 1;
  p.bursts = 3;
  p.burstMs = 10;
  p.noiseMs = 100;
  p.tail = 0.5f;
  FmVoice v;
  v.trigger(false);
  run(v, p, buf, 50 * kSynthRate / 1000);  // 50 ms
  // Energy just after the 2nd burst start (10 ms) is above the energy just before it.
  constexpr int k2nd = 10 * kSynthRate / 1000;
  float before = 0, after = 0;
  for (int i = k2nd - 40; i < k2nd; ++i) before += fabsf(buf[i]);
  for (int i = k2nd + 10; i < k2nd + 50; ++i) after += fabsf(buf[i]);
  TEST_ASSERT_TRUE(after > before * 2);
}

void test_choke_retrigger_has_no_jump() {
  FmVoice v;
  v.trigger(false);
  FmParams p = sine(200);
  p.oneShot = true;
  p.ampMs = 2000;
  run(v, p, buf, 1000);
  const float last = buf[999];
  v.trigger(true);
  run(v, p, buf, 1);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, last, buf[0]);
}

// Operator levels decayed before a choke retrigger ramp back up instead of snapping.
void test_choke_retrigger_ramps_levels() {
  static float b[3296 + 64];
  FmVoice v;
  v.trigger(false);
  FmParams p = sine(200);  // held amplitude: only the operator envelope moves
  p.op[0].decayMs = 100;
  run(v, p, b, 3296);  // 103 ms, 20.6 cycles: carrier at ~-0.6 x level
  v.trigger(true);
  run(v, p, b + 3296, 64);
  float jump = 0;
  for (int i = 3200; i < 3296 + 64; ++i) jump = fmaxf(jump, fabsf(b[i] - b[i - 1]));
  TEST_ASSERT_TRUE(jump < 0.1f);
}

// FNV-1a over the float bits of n samples.
static uint32_t hashOf(const float* b, int n) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < n; ++i) {
    uint32_t x;
    memcpy(&x, &b[i], 4);
    for (int k = 0; k < 4; ++k) h = (h ^ ((x >> (8 * k)) & 0xFFu)) * 16777619u;
  }
  return h;
}

// Every path of the core (two pairs, feedback, pitch sweep, noise through the filter, one-shot amp) with
// indices below any fmMaxIndex limit: its render does not depend on the limits.
static FmParams goldenPatch() {
  FmParams p;
  p.alg = FmAlg::TwoPairs;
  p.hz = 220;
  p.op[0] = {1, 0.8f, 0.2f, 300, 0};
  p.op[1] = {2, 3, 0, 150, 0};
  p.op[2] = {3, 0.3f, 0, 0, 0.8f};
  p.op[3] = {1, 1, 0.5f, 400, 0};
  p.pitchEnv = 12;
  p.pitchMs = 50;
  p.noise = 0.1f;
  p.noiseMs = 100;
  p.filterMode = Svf::Mode::Bp;
  p.filterHz = 2000;
  p.filterQ = 1;
  p.ampMs = 800;
  return p;
}

// SAT 0 (the default): the shaper is skipped, the render is bit for bit the one from before SAT.
void test_sat_zero_bit_exact() {
  constexpr uint32_t kGolden = 0xEF26D32Cu;  // the hash of this render on the code before SAT
  FmVoice v;
  v.trigger(false);
  run(v, goldenPatch(), buf, 8192);
  const uint32_t h = hashOf(buf, 8192);
  char msg[32];
  snprintf(msg, sizeof msg, "hash 0x%08X", static_cast<unsigned>(h));
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(kGolden, h, msg);
}

// SAT keeps the full-scale peak (tanh(g) / tanh(g) = 1) and fattens the wave: RMS up.
void test_sat_fattens_keeps_peak() {
  static float x[4410];
  FmVoice a, b;
  a.trigger(false);
  b.trigger(false);
  FmParams p = sine(220);
  run(a, p, buf, 4410);
  p.sat = 1;
  run(b, p, x, 4410);
  double ra = 0, rb = 0;
  float peak = 0;
  for (int i = 1000; i < 4410; ++i) {
    ra += static_cast<double>(buf[i]) * buf[i];
    rb += static_cast<double>(x[i]) * x[i];
    peak = fmaxf(peak, fabsf(x[i]));
  }
  TEST_ASSERT_TRUE(rb > ra * 1.3);
  TEST_ASSERT_TRUE(peak <= 1.01f);
}

// SAT jumping 0 -> 1 between two ticks ramps over the span: no step in the wave (unramped: ~0.66 here).
void test_sat_change_ramps() {
  static float x[2048];
  FmVoice v;
  v.trigger(false);
  FmParams p = sine(110);
  for (int i = 0; i < 2048; ++i) x[i] = 0;
  for (int pos = 0; pos < 2048; pos += kSpan) {
    p.sat = pos < 1024 ? 0.f : 1.f;
    v.control(p, kSpan);
    v.render(x + pos, kSpan, 1.f);
  }
  float jump = 0;
  for (int i = 1; i < 2048; ++i) jump = fmaxf(jump, fabsf(x[i] - x[i - 1]));
  TEST_ASSERT_TRUE(jump < 0.25f);
}

// Back to SAT 0: after one ramp span the shaper is off again, bit for bit the clean voice.
void test_sat_back_to_zero_is_bypass() {
  static float xa[1024], xb[1024];
  FmVoice a, b;
  a.trigger(false);
  b.trigger(false);
  FmParams pa = sine(220), pb = sine(220);
  for (int i = 0; i < 1024; ++i) xa[i] = xb[i] = 0;
  for (int pos = 0; pos < 1024; pos += kSpan) {
    pb.sat = pos < 256 ? 0.5f : 0.f;
    a.control(pa, kSpan);
    a.render(xa + pos, kSpan, 1.f);
    b.control(pb, kSpan);
    b.render(xb + pos, kSpan, 1.f);
  }
  const int from = 256 + kSpan;
  TEST_ASSERT_EQUAL_MEMORY(xa + from, xb + from, sizeof(float) * (1024 - from));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_carrier_frequency);
  RUN_TEST(test_zero_index_is_pure_sine);
  RUN_TEST(test_modulation_changes_wave);
  RUN_TEST(test_carriers_by_algorithm);
  RUN_TEST(test_one_shot_decays_and_ends);
  RUN_TEST(test_pitch_envelope_starts_high);
  RUN_TEST(test_above_nyquist_is_silent);
  RUN_TEST(test_max_index_falls_with_pitch);
  RUN_TEST(test_feedback_bounded);
  RUN_TEST(test_noise_filter_modes);
  RUN_TEST(test_clap_bursts_retrigger_noise);
  RUN_TEST(test_choke_retrigger_has_no_jump);
  RUN_TEST(test_choke_retrigger_ramps_levels);
  RUN_TEST(test_sat_zero_bit_exact);
  RUN_TEST(test_sat_fattens_keeps_peak);
  RUN_TEST(test_sat_change_ramps);
  RUN_TEST(test_sat_back_to_zero_is_bypass);
  return UNITY_END();
}
