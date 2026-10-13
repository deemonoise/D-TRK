#include <math.h>
#include <string.h>
#include <unity.h>
#include "synth_reverb.h"

using namespace mt;

static float buf[Reverb::kBufLen];
static constexpr int kN = 128;
static constexpr int kRate = 32000;

void setUp() {}
void tearDown() {}

void test_silent_without_buffer() {
  Reverb r;
  float in[kN] = {1}, mix[kN] = {0}, mr[kN] = {0};
  r.process(in, mix, mr, kN, 60, 70, 127, false);
  for (int i = 0; i < kN; ++i) TEST_ASSERT_TRUE(mix[i] == 0.f && mr[i] == 0.f);
  r.setBuffer(buf, Reverb::kBufLen - 1);  // too short: off
  r.process(in, mix, mr, kN, 60, 70, 127, false);
  for (int i = 0; i < kN; ++i) TEST_ASSERT_TRUE(mix[i] == 0.f && mr[i] == 0.f);
}

// Peak of the output per 100 ms window after an impulse, for `seconds`.
static int tailWindows(uint8_t size, float* peaks, int maxWin) {
  Reverb r;
  r.setBuffer(buf, Reverb::kBufLen);
  const int win = kRate / 10;
  int w = 0, inWin = 0;
  float peak = 0;
  for (int b = 0; w < maxWin; ++b) {
    float in[kN] = {0}, mix[kN] = {0}, mr[kN] = {0};
    if (b == 0) in[0] = 1.f;
    r.process(in, mix, mr, kN, size, 70, 127, false);  // L: the mono path
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

// Stereo: an impulse rings on both sides, and the sides differ (the extra allpass on R).
void test_stereo_impulse_both_sides_differ() {
  Reverb r;
  r.setBuffer(buf, Reverb::kBufLen);
  bool anyL = false, anyR = false, diff = false;
  for (int b = 0; b < 40; ++b) {
    float in[kN] = {0}, l[kN] = {0}, rr[kN] = {0};
    if (b == 0) in[0] = 1.f;
    r.process(in, l, rr, kN, 60, 70, 127, false);
    for (int i = 0; i < kN; ++i) {
      anyL |= l[i] != 0;
      anyR |= rr[i] != 0;
      diff |= l[i] != rr[i];
    }
  }
  TEST_ASSERT_TRUE(anyL);
  TEST_ASSERT_TRUE(anyR);
  TEST_ASSERT_TRUE(diff);
}

// L is the old mono reverb: fixed samples of its output (recorded from the mono overload before it
// went), so the L path stays bit-exact to the reverb of earlier firmware.
void test_left_matches_old_mono_output() {
  Reverb s;
  s.setBuffer(buf, Reverb::kBufLen);
  for (int b = 0; b < 41; ++b) {
    float in[kN], l[kN], rr[kN];
    for (int i = 0; i < kN; ++i) {
      in[i] = b < 4 ? sinf((b * kN + i) * 0.37f) * 0.5f : 0.f;
      l[i] = rr[i] = 0.01f * i;
    }
    s.process(in, l, rr, kN, 90, 40, 100, false);
    if (b == 10) {
      TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.024409445f, l[0]);
      TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.383048922f, l[37]);
      TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.27066743f, l[127]);
    } else if (b == 20) {
      TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.0418658666f, l[0]);
      TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.459059447f, l[37]);
      TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.25382888f, l[127]);
    } else if (b == 40) {
      TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0524678901f, l[0]);
      TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.404035568f, l[37]);
      TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.19150639f, l[127]);
    }
  }
}

void test_stereo_level_zero_untouched() {
  Reverb r;
  r.setBuffer(buf, Reverb::kBufLen);
  float in[kN], l[kN], rr[kN], ref[kN];
  for (int i = 0; i < kN; ++i) {
    in[i] = 0.5f;
    l[i] = rr[i] = ref[i] = i * 0.001f;
  }
  r.process(in, l, rr, kN, 60, 70, 0, false);
  TEST_ASSERT_EQUAL_MEMORY(ref, l, sizeof(ref));
  TEST_ASSERT_EQUAL_MEMORY(ref, rr, sizeof(ref));
}

// The idle check covers both outputs: it rests only once both tails are gone, then wakes on input.
void test_stereo_idle_after_tail() {
  Reverb r;
  r.setBuffer(buf, Reverb::kBufLen);
  float in[kN] = {1}, l[kN] = {0}, rr[kN] = {0};
  r.process(in, l, rr, kN, 127, 0, 127, false);
  in[0] = 0;
  for (int b = 0; b < 32000 * 30 / kN; ++b) r.process(in, l, rr, kN, 127, 0, 127, false);  // 30 s
  float ref[kN];
  for (int i = 0; i < kN; ++i) l[i] = rr[i] = ref[i] = 0.25f;
  r.process(in, l, rr, kN, 127, 0, 127, false);
  TEST_ASSERT_EQUAL_MEMORY(ref, l, sizeof(ref));
  TEST_ASSERT_EQUAL_MEMORY(ref, rr, sizeof(ref));
  in[5] = 1;
  r.process(in, l, rr, kN, 127, 0, 127, false);
  in[5] = 0;
  bool anyL = false, anyR = false;
  for (int b = 0; b < 20; ++b) {
    float tl[kN] = {0}, tr[kN] = {0};
    r.process(in, tl, tr, kN, 127, 0, 127, false);
    for (int i = 0; i < kN; ++i) {
      anyL |= tl[i] != 0;
      anyR |= tr[i] != 0;
    }
  }
  TEST_ASSERT_TRUE(anyL);
  TEST_ASSERT_TRUE(anyR);
}

// FREEZE: the lines loop without loss or input; 3 s later the tail is as loud as right after the
// burst (within 1 dB).
void test_freeze_holds_tail() {
  Reverb r;
  r.setBuffer(buf, Reverb::kBufLen);
  uint32_t seed = 1;
  for (int b = 0; b < 32; ++b) {  // 128 ms of noise
    float in[kN], l[kN] = {0}, rr[kN] = {0};
    for (int i = 0; i < kN; ++i) {
      seed = seed * 1664525u + 1013904223u;
      in[i] = static_cast<int32_t>(seed) * (0.5f / 2147483648.f);
    }
    r.process(in, l, rr, kN, 80, 60, 127, false);
  }
  const int win = 4096;
  auto rms = [&](bool burstIn) {
    double sum = 0;
    for (int b = 0; b < win / kN; ++b) {
      float in[kN], l[kN] = {0}, rr[kN] = {0};
      for (int i = 0; i < kN; ++i) in[i] = burstIn ? 1.f : 0.f;  // frozen: the input is ignored
      r.process(in, l, rr, kN, 80, 60, 127, true);
      for (int i = 0; i < kN; ++i) sum += static_cast<double>(l[i]) * l[i] + static_cast<double>(rr[i]) * rr[i];
    }
    return sqrt(sum / (2.0 * win));
  };
  const double first = rms(false);
  TEST_ASSERT_TRUE(first > 1e-3);
  for (int b = 0; b < kRate * 3 / kN; ++b) {
    float in[kN] = {0}, l[kN] = {0}, rr[kN] = {0};
    if (b % 50 == 0) in[3] = 1.f;  // ignored while frozen
    r.process(in, l, rr, kN, 80, 60, 127, true);
  }
  const double later = rms(true);
  const double db = 20.0 * log10(later / first);
  TEST_ASSERT_TRUE(db > -1.0 && db < 1.0);
  // Released: the tail decays again.
  for (int b = 0; b < kRate * 3 / kN; ++b) {
    float in[kN] = {0}, l[kN] = {0}, rr[kN] = {0};
    r.process(in, l, rr, kN, 80, 60, 127, false);
  }
  float in[kN] = {0}, l[kN] = {0}, rr[kN] = {0};
  r.process(in, l, rr, kN, 80, 60, 127, false);
  for (int i = 0; i < kN; ++i) TEST_ASSERT_TRUE(fabsf(l[i]) < first * 0.01);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_silent_without_buffer);
  RUN_TEST(test_impulse_tail_decays);
  RUN_TEST(test_size_lengthens_tail);
  RUN_TEST(test_stereo_impulse_both_sides_differ);
  RUN_TEST(test_left_matches_old_mono_output);
  RUN_TEST(test_stereo_level_zero_untouched);
  RUN_TEST(test_stereo_idle_after_tail);
  RUN_TEST(test_freeze_holds_tail);
  return UNITY_END();
}
