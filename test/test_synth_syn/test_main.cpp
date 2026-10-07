#include <math.h>
#include <string.h>
#include <unity.h>
#include "synth_osc.h"
#include "synth_syn.h"
#include "wt_builtin.h"

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

static float out[8192];

// f0 = 5 kHz: harmonics 5/10/15 kHz, aliases of 20/25/30 kHz land on 12/7/2 kHz.
void test_bl_saw_alias_below_naive() {
  BlOsc o;
  const float dt = 5000.f / kSynthRate;
  for (int i = 0; i < 8192; ++i) out[i] = o.saw(dt, -1).v;
  const double bl = power(out, 8192, 2000) + power(out, 8192, 7000);
  ChipOsc c;
  for (int i = 0; i < 8192; ++i) out[i] = c.next(Wave::Saw, dt, 0.5f);
  const double naive = power(out, 8192, 2000) + power(out, 8192, 7000);
  TEST_ASSERT_TRUE(bl < naive * 0.05);  // > 13 dB less
}

void test_bl_square_pw_duty() {
  BlOsc o;
  const float dt = 100.f / kSynthRate;
  double mean = 0;
  for (int i = 0; i < kSynthRate; ++i) mean += o.square(dt, 0.25f, -1).v;
  // 25 % high: mean = 0.25 - 0.75 = -0.5.
  TEST_ASSERT_FLOAT_WITHIN(0.02f, -0.5f, static_cast<float>(mean / kSynthRate));
}

void test_bl_tri_bounded() {
  BlOsc o;
  const float dt = 440.f / kSynthRate;
  float lo = 0, hi = 0;
  for (int i = 0; i < kSynthRate; ++i) {
    const float v = o.tri(dt).v;
    if (i > kSynthRate / 2) lo = v < lo ? v : lo, hi = v > hi ? v : hi;
  }
  TEST_ASSERT_FLOAT_WITHIN(0.15f, 1.f, hi);
  TEST_ASSERT_FLOAT_WITHIN(0.15f, -1.f, lo);
}

void test_master_reports_wrap() {
  BlOsc o;
  const float dt = 1000.f / kSynthRate;  // 32 samples per cycle
  int wraps = 0;
  for (int i = 0; i < 3200; ++i) wraps += o.saw(dt, -1).wrap >= 0;
  TEST_ASSERT_INT_WITHIN(1, 100, wraps);
}

void test_sync_no_jumps() {
  // Slave at 2.37 x master, hard synced: no sample-to-sample jump near the full step size.
  BlOsc m, s;
  const float d1 = 300.f / kSynthRate, d2 = d1 * 2.37f;
  float prev = 0, maxStep = 0;
  for (int i = 0; i < 4000; ++i) {
    const float w = m.saw(d1, -1).wrap;
    const float v = s.saw(d2, w).v;
    if (i > 10) maxStep = fabsf(v - prev) > maxStep ? fabsf(v - prev) : maxStep;
    prev = v;
  }
  TEST_ASSERT_TRUE(maxStep < 1.6f);  // naive resets jump by up to 2
}

void test_wt_reads_level_and_frame() {
  static int16_t t[kWtTableSamples];
  WtBuiltinSrc src(0);
  wtBuild(src, t);
  WtOsc o;
  o.setLevel(220.f);
  float mx = 0;
  for (int i = 0; i < 2000; ++i) {
    const float v = o.next(t, 220.f / kSynthRate, 0.f, -1);
    TEST_ASSERT_FALSE(isnan(v));
    mx = fabsf(v) > mx ? fabsf(v) : mx;
  }
  TEST_ASSERT_TRUE(mx > 0.5f && mx <= 1.05f);
  o.next(nullptr, 0.01f, 0.f, -1);  // one sample late: flushes the last table sample
  TEST_ASSERT_EQUAL_FLOAT(0.f, o.next(nullptr, 0.01f, 0.f, -1));  // missing table = silence
}

void test_wt_level_choice() {
  TEST_ASSERT_EQUAL_FLOAT(0.f, wtLevelPos(50.f));       // low notes: level 0
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.f, wtLevelPos(125.f));  // 125 * 128 = 16 kHz -> 1 octave above 8 kHz
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.f, wtLevelPos(20000.f));  // clamped
}

void test_voice_mix_and_silence() {
  SynVoice v;
  SynParams p;
  p.mode[0] = p.mode[1] = static_cast<uint8_t>(SynOsc::Saw);
  p.hz[0] = p.hz[1] = 220;
  p.mix = 0;
  v.trigger();
  v.control(p, 32);
  float buf[32] = {0};
  v.render(buf, 32, 1.f);
  float e = 0;
  for (float x : buf) e += fabsf(x);
  TEST_ASSERT_TRUE(e > 1.f);
  // Both oscillators on missing wavetables, no sub / noise: silent.
  p.mode[0] = p.mode[1] = static_cast<uint8_t>(SynOsc::Wt);
  p.wt[0] = p.wt[1] = nullptr;
  v.trigger();
  v.control(p, 32);
  memset(buf, 0, sizeof(buf));
  for (int k = 0; k < 4; ++k) v.render(buf, 32, 1.f);
  for (float x : buf) TEST_ASSERT_EQUAL_FLOAT(0.f, x);
}

// Above the sample rate (high note + transpose + osc semitones) the BL oscillators stay bounded.
void test_voice_bounded_above_sample_rate() {
  const uint8_t modes[] = {static_cast<uint8_t>(SynOsc::Saw), static_cast<uint8_t>(SynOsc::Square),
                           static_cast<uint8_t>(SynOsc::Tri)};
  for (uint8_t m : modes) {
    SynVoice v;
    SynParams p;
    p.mode[0] = p.mode[1] = m;
    p.hz[0] = p.hz[1] = 50000;
    p.mix = 64;
    p.subOct = 2;
    v.trigger();
    float buf[32];
    float peak = 0;
    for (int k = 0; k < 1000; ++k) {  // 1 s
      v.control(p, 32);
      memset(buf, 0, sizeof(buf));
      v.render(buf, 32, 1.f);
      for (float x : buf) peak = fmaxf(peak, fabsf(x));
    }
    TEST_ASSERT_TRUE(peak < 80.f);  // a 220 Hz saw at this gain peaks near 64 (kSynGain 1)
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_bl_saw_alias_below_naive);
  RUN_TEST(test_bl_square_pw_duty);
  RUN_TEST(test_bl_tri_bounded);
  RUN_TEST(test_master_reports_wrap);
  RUN_TEST(test_sync_no_jumps);
  RUN_TEST(test_wt_reads_level_and_frame);
  RUN_TEST(test_wt_level_choice);
  RUN_TEST(test_voice_mix_and_silence);
  RUN_TEST(test_voice_bounded_above_sample_rate);
  return UNITY_END();
}
