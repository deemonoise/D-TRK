#include <math.h>
#include <string.h>
#include <unity.h>
#include "synth_reverb.h"

using namespace mt;

static float buf[Reverb::kBufLen];
static constexpr int kN = 128;
static constexpr int kRate = 44100;
static float right[kN];  // R output where a test only looks at L

void setUp() {}
void tearDown() {}

void test_silent_without_buffer() {
  Reverb r;
  float in[kN] = {1}, mix[kN] = {0};
  r.process(in, mix, right, kN, 60, 70, 127);
  for (float x : mix) TEST_ASSERT_TRUE(x == 0.f);
  r.setBuffer(buf, Reverb::kBufLen - 1);  // too short: off
  r.process(in, mix, right, kN, 60, 70, 127);
  for (float x : mix) TEST_ASSERT_TRUE(x == 0.f);
}

void test_level_zero_bit_exact() {
  Reverb r;
  r.setBuffer(buf, Reverb::kBufLen);
  float in[kN], mix[kN], mixR[kN], ref[kN];
  for (int i = 0; i < kN; ++i) {
    in[i] = 0.5f;
    mix[i] = mixR[i] = ref[i] = i * 0.001f;
  }
  r.process(in, mix, mixR, kN, 60, 70, 0);
  TEST_ASSERT_EQUAL_MEMORY(ref, mix, sizeof(ref));
  TEST_ASSERT_EQUAL_MEMORY(ref, mixR, sizeof(ref));
}

// Peak of the output per 100 ms window after an impulse, for `seconds`.
static int tailWindows(uint8_t size, float* peaks, int maxWin) {
  Reverb r;
  r.setBuffer(buf, Reverb::kBufLen);
  const int win = kRate / 10;
  int w = 0, inWin = 0;
  float peak = 0;
  for (int b = 0; w < maxWin; ++b) {
    float in[kN] = {0}, mix[kN] = {0}, mixR[kN] = {0};
    if (b == 0) in[0] = 1.f;
    r.process(in, mix, mixR, kN, size, 70, 127);
    for (int i = 0; i < kN; ++i) mix[i] = fabsf(mix[i]) > fabsf(mixR[i]) ? mix[i] : mixR[i];
    for (int i = 0; i < kN && w < maxWin; ++i) {
      if (fabsf(mix[i]) > peak) peak = fabsf(mix[i]);
      if (++inWin == win) {
        peaks[w++] = peak;
        peak = 0;
        inWin = 0;
      }
    }
  }
  return w;
}

void test_impulse_tail_decays() {
  float peaks[30];
  tailWindows(60, peaks, 30);  // 3 s
  float top = 0;
  for (float p : peaks) top = p > top ? p : top;
  TEST_ASSERT_TRUE(top > 0);
  TEST_ASSERT_TRUE(peaks[1] > 0.01f * top);   // still ringing at 100..200 ms
  TEST_ASSERT_TRUE(peaks[29] < 1e-3f * top);  // below -60 dB by 3 s
}

static int decayWindow(uint8_t size) {
  float peaks[60];
  tailWindows(size, peaks, 60);
  float top = 0;
  for (float p : peaks) top = p > top ? p : top;
  for (int w = 1; w < 60; ++w)
    if (peaks[w] < 1e-3f * top) return w;
  return 60;
}

void test_size_lengthens_tail() { TEST_ASSERT_TRUE(decayWindow(0) < decayWindow(127)); }

// After the tail dies out the reverb rests: the mix is untouched (no denormal noise, no work).
void test_idle_after_tail() {
  Reverb r;
  r.setBuffer(buf, Reverb::kBufLen);
  float in[kN] = {1}, mix[kN] = {0};
  r.process(in, mix, right, kN, 127, 0, 127);
  in[0] = 0;
  for (int b = 0; b < kRate * 30 / kN; ++b) r.process(in, mix, right, kN, 127, 0, 127);  // 30 s
  float ref[kN];
  float mixR[kN];
  for (int i = 0; i < kN; ++i) mix[i] = mixR[i] = ref[i] = 0.25f;
  r.process(in, mix, mixR, kN, 127, 0, 127);
  TEST_ASSERT_EQUAL_MEMORY(ref, mix, sizeof(ref));
  TEST_ASSERT_EQUAL_MEMORY(ref, mixR, sizeof(ref));
  in[5] = 1;  // new input wakes it
  r.process(in, mix, right, kN, 127, 0, 127);
  in[5] = 0;
  bool any = false;
  for (int b = 0; b < 20; ++b) {  // the first echo needs > 1000 samples
    float tail[kN] = {0};
    r.process(in, tail, right, kN, 127, 0, 127);
    for (float x : tail) any |= x != 0;
  }
  TEST_ASSERT_TRUE(any);
}

// A mono impulse comes back different on L and R (the spread), with about the same energy on each:
// the stereo mean within 10 % of the L channel alone (the mono reverb's lines).
void test_stereo_spread() {
  Reverb r;
  r.setBuffer(buf, Reverb::kBufLen);
  double el = 0, er = 0;
  int differ = 0;
  for (int b = 0; b < kRate / kN; ++b) {  // 1 s
    float in[kN] = {0}, l[kN] = {0}, rr[kN] = {0};
    if (b == 0) in[0] = 1.f;
    r.process(in, l, rr, kN, 80, 40, 127);
    for (int i = 0; i < kN; ++i) {
      el += l[i] * l[i];
      er += rr[i] * rr[i];
      differ += fabsf(l[i] - rr[i]) > 1e-6f;
    }
  }
  TEST_ASSERT_TRUE(el > 0);
  TEST_ASSERT_TRUE(differ > kRate / 4);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 1.f, static_cast<float>((el + er) / 2 / el));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_silent_without_buffer);
  RUN_TEST(test_level_zero_bit_exact);
  RUN_TEST(test_impulse_tail_decays);
  RUN_TEST(test_size_lengthens_tail);
  RUN_TEST(test_idle_after_tail);
  RUN_TEST(test_stereo_spread);
  return UNITY_END();
}
