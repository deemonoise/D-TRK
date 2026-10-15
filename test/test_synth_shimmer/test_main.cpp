#include <math.h>
#include <string.h>
#include <unity.h>
#include "synth_reverb.h"
#include "synth_shimmer.h"

using namespace mt;

static constexpr int kN = 128, kRate = kSynthRate;
static int16_t line[PitchShift::kLen];
static float rbuf[Reverb::kBufLen];

void setUp() {}
void tearDown() {}

// Goertzel power of x at hz.
static double power(const float* x, int n, float hz) {
  const double c = 2 * cos(2 * M_PI * hz / kRate);
  double s1 = 0, s2 = 0;
  for (int i = 0; i < n; ++i) {
    const double s = x[i] + c * s1 - s2;
    s2 = s1;
    s1 = s;
  }
  return s1 * s1 + s2 * s2 - c * s1 * s2;
}

// Shift a 440 Hz sine; the output's power at 440 x ratio vs at 440 and the neighbourhood.
static void checkShift(uint8_t interval) {
  PitchShift ps;
  ps.setBuffer(line, PitchShift::kLen);
  ps.setInterval(interval);
  const int n = kRate / kN * kN;  // ~1 s, whole blocks
  static float in[kRate], out[kRate];
  for (int i = 0; i < n; ++i) in[i] = 0.5f * sinf(6.2831853f * 440.f * i / kRate);
  for (int i = 0; i < n; i += kN) ps.process(in + i, out + i, kN);
  const float target = 440.f * PitchShift::ratio(interval);
  const float* o = out + kRate / 4;
  const int m = n - kRate / 4;
  const double pt = power(o, m, target);
  TEST_ASSERT_TRUE(pt > 20 * power(o, m, 440.f));
  TEST_ASSERT_TRUE(pt > 20 * power(o, m, target * 1.1f));
  TEST_ASSERT_TRUE(pt > 20 * power(o, m, target * 0.9f));
  float pk = 0;
  for (int i = 0; i < n; ++i) {
    TEST_ASSERT_FALSE(isnan(out[i]));
    pk = fmaxf(pk, fabsf(out[i]));
  }
  TEST_ASSERT_TRUE(pk < 0.7f && pk > 0.2f);
}

void test_octave_up() { checkShift(PitchShift::Oct); }
void test_fifth_up() { checkShift(PitchShift::Fifth); }
void test_two_octaves_up() { checkShift(PitchShift::TwoOct); }

void test_no_buffer_is_silent() {
  PitchShift ps;
  float in[kN], out[kN];
  for (int i = 0; i < kN; ++i) in[i] = 1.f, out[i] = 5.f;
  ps.process(in, out, kN);
  for (int i = 0; i < kN; ++i) TEST_ASSERT_EQUAL_FLOAT(0.f, out[i]);
  ps.setBuffer(line, PitchShift::kLen - 1);  // too short
  ps.process(in, out, kN);
  for (int i = 0; i < kN; ++i) TEST_ASSERT_EQUAL_FLOAT(0.f, out[i]);
}

void test_amount_zero_and_freeze_pass_send() {
  Shimmer sh;
  sh.setBuffer(line, PitchShift::kLen);
  float send[kN], wet[kN], in[kN];
  for (int i = 0; i < kN; ++i) send[i] = sinf(i * 0.3f), wet[i] = 0.9f;
  sh.process(send, wet, in, kN, 0, 64, false);
  TEST_ASSERT_EQUAL_MEMORY(send, in, sizeof(in));
  TEST_ASSERT_FALSE(sh.active());
  for (int b = 0; b < 4; ++b) sh.process(send, wet, in, kN, 127, 64, true);
  TEST_ASSERT_EQUAL_MEMORY(send, in, sizeof(in));
}

// Reverb + shimmer loop: a 500 Hz burst; after 1 s the tail has energy at 1 kHz (and 2 kHz), the
// plain reverb has next to none.
static void loop(uint8_t amount, uint8_t size, int seconds, float* tail, int tailLen, float* peak,
                 bool noise = false) {
  uint32_t s = 7;
  Reverb rv;
  rv.setBuffer(rbuf, Reverb::kBufLen);
  Shimmer sh;
  sh.setBuffer(line, PitchShift::kLen);
  float send[kN], in[kN], wl[kN], wr[kN], prev[kN] = {};
  const int blocks = seconds * kRate / kN;
  *peak = 0;
  for (int b = 0; b < blocks; ++b) {
    for (int i = 0; i < kN; ++i) {
      const int t = b * kN + i;
      s = s * 1664525u + 1013904223u;
      const float x = noise ? (s >> 8) * (2.f / 16777216.f) - 1.f : 0.5f * sinf(6.2831853f * 500.f * t / kRate);
      send[i] = t < kRate * 3 / 10 ? x : 0.f;
      wl[i] = wr[i] = 0;
    }
    sh.process(send, prev, in, kN, amount, size, false);
    rv.process(in, wl, wr, kN, size, 40, 127, false);
    for (int i = 0; i < kN; ++i) {
      TEST_ASSERT_FALSE(isnan(wl[i]) || isnan(wr[i]));
      *peak = fmaxf(*peak, fabsf(wl[i]));
      prev[i] = wl[i] * 2.f;  // the wet before the return level (level 127 -> x 0.5)
    }
    
    const int t0 = blocks * kN - tailLen;
    if (b * kN >= t0) memcpy(tail + (b * kN - t0), wl, sizeof(wl));
  }
}

void test_shimmer_climbs_an_octave() {
  constexpr int kTail = 8192;
  static float plain[kTail], shim[kTail];
  float pk;
  loop(0, 100, 1, plain, kTail, &pk);
  loop(127, 100, 1, shim, kTail, &pk);
  const double p1 = power(shim, kTail, 1000.f), p0 = power(plain, kTail, 1000.f);
  TEST_ASSERT_TRUE(p1 > 10 * p0);
  TEST_ASSERT_TRUE(p1 > 0.05 * power(shim, kTail, 500.f));
}

// Every size, shim 127, sine and noise bursts: bounded, and the tail dies out (last 0.5 s of 10 s
// well below the peak).
void test_bounded_and_decays() {
  constexpr int kTail = kRate / 2;
  static float tail[kTail];
  const uint8_t sizes[] = {0, 64, 100, 127};
  for (uint8_t sz : sizes)
    for (int nz = 0; nz < 2; ++nz) {
      float pk;
      loop(127, sz, 10, tail, kTail, &pk, nz == 1);
      TEST_ASSERT_TRUE(pk < 4.f);
      float last = 0;
      for (int i = 0; i < kTail; ++i) last = fmaxf(last, fabsf(tail[i]));
      TEST_ASSERT_TRUE_MESSAGE(last < 0.1f * pk, "shimmer tail does not die out");
    }
}

// An interval change keeps the read position: no jump in the output.
void test_interval_change_smooth() {
  PitchShift ps;
  ps.setBuffer(line, PitchShift::kLen);
  float in[kN], out[kN], prev = 0, step = 0;
  for (int b = 0; b < 200; ++b) {
    if (b == 100) ps.setInterval(PitchShift::TwoOct);
    for (int i = 0; i < kN; ++i) in[i] = 0.5f * sinf(6.2831853f * 100.f * (b * kN + i) / kRate);
    ps.process(in, out, kN);
    for (int i = 0; i < kN; ++i) {
      if (b > 10) step = fmaxf(step, fabsf(out[i] - prev));
      prev = out[i];
    }
  }
  // 400 Hz (x4) at 0.5: own step ~0.04.
  TEST_ASSERT_TRUE(step < 0.08f);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_octave_up);
  RUN_TEST(test_fifth_up);
  RUN_TEST(test_two_octaves_up);
  RUN_TEST(test_no_buffer_is_silent);
  RUN_TEST(test_amount_zero_and_freeze_pass_send);
  RUN_TEST(test_shimmer_climbs_an_octave);
  RUN_TEST(test_bounded_and_decays);
  RUN_TEST(test_interval_change_smooth);
  return UNITY_END();
}
