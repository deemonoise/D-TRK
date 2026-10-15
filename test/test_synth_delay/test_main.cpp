#include <math.h>
#include <string.h>
#include <unity.h>
#include "synth_delay.h"

using namespace mt;

constexpr uint32_t kLen = 1000;
static int16_t line[2 * kLen];
static Delay* d;

void setUp() {
  d = new Delay();
  d->setBuffer(line, kLen);
}
void tearDown() { delete d; }

// Runs n samples with an impulse of height a at sample 0 (none if at < 0), collects the mix (L;
// R must get the same return).
static void run(float* mix, int n, uint32_t delay, uint8_t fb, uint8_t tone, uint8_t level, float a = 1.f) {
  float send[64], r[64];
  for (int i = 0; i < n; i += 64) {
    const int k = n - i < 64 ? n - i : 64;
    for (int j = 0; j < k; ++j) send[j] = (i + j == 0) ? a : 0.f;
    for (int j = 0; j < k; ++j) mix[i + j] = r[j] = 0;
    d->process(send, mix + i, r, k, delay, fb, tone, level);
    TEST_ASSERT_EQUAL_MEMORY(mix + i, r, sizeof(float) * k);
  }
}

static int peakAt(const float* mix, int from, int to) {
  int best = from;
  for (int i = from; i < to; ++i)
    if (fabsf(mix[i]) > fabsf(mix[best])) best = i;
  return best;
}

void test_impulse_returns_after_delay() {
  static float mix[512];
  run(mix, 512, 100, 0, 127, 127);
  for (int i = 0; i < 100; ++i) TEST_ASSERT_EQUAL_FLOAT(0.f, mix[i]);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.f, mix[100]);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.f, mix[101]);
}

void test_level_scales_the_return() {
  static float mix[256];
  run(mix, 256, 50, 0, 127, 64);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 64.f / 127.f, mix[50]);
}

void test_no_feedback_one_echo() {
  static float mix[512];
  run(mix, 512, 100, 0, 127, 127);
  for (int i = 101; i < 512; ++i) TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.f, mix[i]);
}

void test_feedback_repeats_decaying() {
  static float mix[512];
  run(mix, 512, 100, 127, 127, 127);
  TEST_ASSERT_TRUE(mix[200] > 0.5f && mix[200] < mix[100]);
  TEST_ASSERT_TRUE(mix[300] > 0.f && mix[300] < mix[200]);
}

void test_tone_darkens_the_echo() {
  static float mix[512];
  run(mix, 512, 100, 0, 0, 127);
  const int p = peakAt(mix, 0, 512);
  TEST_ASSERT_TRUE(p >= 100);
  TEST_ASSERT_TRUE(mix[p] < 0.5f);  // smeared: the energy spreads over many samples
  TEST_ASSERT_TRUE(mix[150] > 0.f);
}

void test_no_buffer_leaves_mix() {
  d->setBuffer(nullptr, 0);
  float send[8] = {1, 1, 1, 1, 1, 1, 1, 1}, mix[8] = {0.5f}, r[8] = {0.25f};
  d->process(send, mix, r, 8, 4, 127, 127, 127);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, mix[0]);
  TEST_ASSERT_EQUAL_FLOAT(0.25f, r[0]);
  for (int i = 1; i < 8; ++i) {
    TEST_ASSERT_EQUAL_FLOAT(0.f, mix[i]);
    TEST_ASSERT_EQUAL_FLOAT(0.f, r[i]);
  }
}

void test_delay_clamped_to_buffer() {
  static float mix[2048];
  run(mix, 2048, 5000, 0, 127, 127);
  TEST_ASSERT_EQUAL(static_cast<int>(kLen - 1), peakAt(mix, 0, 2048));
}

void test_clear_silences_the_line() {
  static float mix[256];
  float send[1] = {1.f}, m[1] = {0}, r[1] = {0};
  d->process(send, m, r, 1, 100, 127, 127, 127);
  d->clear();
  run(mix, 256, 100, 127, 127, 127, 0.f);
  for (int i = 0; i < 256; ++i) TEST_ASSERT_EQUAL_FLOAT(0.f, mix[i]);
}

void test_line_saturates_instead_of_wrapping() {
  static float mix[256];
  run(mix, 256, 100, 0, 127, 127, 10.f);  // above the line's +-4 range
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 32767.f / Delay::kScale, mix[100]);
}

// The same return on both sides, added onto whatever each side holds.
void test_return_on_both_sides() {
  float l[256], r[256];
  float send[256] = {1.f};
  for (int i = 0; i < 256; ++i) {
    l[i] = 0.5f;
    r[i] = -0.25f;
  }
  d->process(send, l, r, 256, 100, 0, 127, 127);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.5f, l[100]);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.75f, r[100]);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, l[99]);
  TEST_ASSERT_EQUAL_FLOAT(-0.25f, r[99]);
}


// ---- Modes (TAPE, PING-PONG) ----

constexpr uint8_t kDig = 0, kTape = 1, kPP = 2;
constexpr uint32_t kBig = 32000;
static int16_t big[kBig], big2[kBig];

static uint32_t seed = 3;
static float noise() {
  seed = seed * 1664525u + 1013904223u;
  return (seed >> 8) * (2.f / 16777216.f) - 1.f;
}

// The mode overload with DIGITAL and the old call: bit-exact; TAPE with Wear 0 too.
void test_digital_and_tape_wear0_bit_exact() {
  for (uint8_t mode = kDig; mode <= kTape; ++mode) {
    Delay a, b;
    a.setBuffer(big, kBig);
    b.setBuffer(big2, kBig);
    float s[128], al[128], ar[128], bl[128], br[128];
    for (int blk = 0; blk < 300; ++blk) {
      for (int i = 0; i < 128; ++i) s[i] = blk < 50 ? noise() : 0.f, al[i] = ar[i] = bl[i] = br[i] = 0;
      a.process(s, al, ar, 128, 3000, 110, 70, 100);
      b.process(s, bl, br, 128, 3000, 110, 70, 100, mode, 0);
      TEST_ASSERT_EQUAL_MEMORY(al, bl, sizeof(al));
      TEST_ASSERT_EQUAL_MEMORY(ar, br, sizeof(ar));
    }
  }
}

// Impulses every 4000 samples, fb 0: the echo time (peak position) for each.
static void tapeEchoes(uint8_t wear, int* t, int count, uint32_t delay = 2000) {
  Delay a;
  a.setBuffer(big, kBig);
  static float l[4000], r[4000];
  float s[128];
  for (int e = 0; e < count; ++e) {
    for (int i = 0; i < 4000; i += 128) {
      const int k = 4000 - i < 128 ? 4000 - i : 128;
      for (int j = 0; j < k; ++j) s[j] = (i + j == 0) ? 1.f : 0.f, l[i + j] = r[i + j] = 0;
      a.process(s, l + i, r + i, k, delay, 0, 127, 127, kTape, wear);
    }
    int best = 0;
    for (int i = 0; i < 4000; ++i)
      if (fabsf(l[i]) > fabsf(l[best])) best = i;
    TEST_ASSERT_EQUAL_MEMORY(l, r, sizeof(l));
    t[e] = best;
  }
}

void test_tape_wear_jitters_echo_time() {
  int t[16];
  tapeEchoes(0, t, 4);
  for (int e = 0; e < 4; ++e) TEST_ASSERT_EQUAL_INT(2000, t[e]);
  tapeEchoes(127, t, 16);
  int lo = t[0], hi = t[0];
  // Wow + flutter at most, in samples (+4: the peak search).
  const int jit = static_cast<int>((Delay::kWowMs + Delay::kFlutterMs) * kSynthRate / 1000.f) + 4;
  for (int e = 0; e < 16; ++e) {
    lo = t[e] < lo ? t[e] : lo;
    hi = t[e] > hi ? t[e] : hi;
    TEST_ASSERT_TRUE(t[e] >= 2000 - jit && t[e] <= 2000 + jit);
  }
  TEST_ASSERT_TRUE(hi - lo > 10);
}

void test_tape_bounded_at_full_feedback() {
  Delay a;
  a.setBuffer(big, kBig);
  float s[128], l[128], r[128], pk = 0, late = 0;
  for (int blk = 0; blk < 10 * 250; ++blk) {
    for (int i = 0; i < 128; ++i) s[i] = blk < 250 ? 2.f * noise() : 0.f, l[i] = r[i] = 0;
    a.process(s, l, r, 128, 1500, 127, 127, 127, kTape, 127);
    for (int i = 0; i < 128; ++i) {
      TEST_ASSERT_FALSE(isnan(l[i]));
      pk = fmaxf(pk, fabsf(l[i]));
      if (blk >= 9 * 250) late = fmaxf(late, fabsf(l[i]));
    }
  }
  TEST_ASSERT_TRUE(pk < 4.1f);
  TEST_ASSERT_TRUE(late < 0.5f * pk);  // the repeats die out (HP + saturation + fb < 1)
}

// A time change: DIGITAL jumps (a large step), TAPE glides (sample steps stay small).
static float timeChangeStep(uint8_t mode) {
  Delay a;
  a.setBuffer(big, kBig);
  float s[128], l[128], r[128], prev = 0, step = 0;
  for (int blk = 0; blk < 400; ++blk) {
    for (int i = 0; i < 128; ++i) s[i] = 0.5f * sinf(6.2831853f * 200.f * (blk * 128 + i) / static_cast<float>(kSynthRate)), l[i] = r[i] = 0;
    a.process(s, l, r, 128, blk < 200 ? 1000 : 5037, 0, 127, 127, mode, mode == kTape ? 1 : 0);
    for (int i = 0; i < 128; ++i) {
      if (blk > 20) step = fmaxf(step, fabsf(l[i] - prev));
      prev = l[i];
    }
  }
  return step;
}

void test_tape_time_change_glides() {
  TEST_ASSERT_TRUE(timeChangeStep(kDig) > 0.2f);
  TEST_ASSERT_TRUE(timeChangeStep(kTape) < 0.05f);  // 200 Hz x 1.5 at 0.5: ~0.03
}

static void ppRun(Delay& a, float* l, float* r, int n, uint32_t delay, uint8_t fb, uint8_t width) {
  float s[128];
  for (int i = 0; i < n; i += 128) {
    const int k = n - i < 128 ? n - i : 128;
    for (int j = 0; j < k; ++j) s[j] = (i + j == 0) ? 1.f : 0.f, l[i + j] = r[i + j] = 0;
    a.process(s, l + i, r + i, k, delay, fb, 127, 127, kPP, width);
  }
}

void test_ping_pong_alternates() {
  Delay a;
  a.setBuffer(big, kBig);
  static float l[2048], r[2048];
  float z[128] = {}, zl[128], zr[128];
  a.process(z, zl, zr, 128, 100, 0, 127, 127, kPP, 127);  // the switch from DIGITAL: fade + wipe
  while (a.wiping()) a.process(z, zl, zr, 128, 100, 0, 127, 127, kPP, 127);
  ppRun(a, l, r, 2048, 100, 0, 127);
  for (int i = 0; i < 2048; ++i) {
    if (i == 100) continue;
    TEST_ASSERT_EQUAL_FLOAT(0.f, l[i]);
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.f, l[100]);
  for (int i = 0; i < 2048; ++i) {
    if (i == 200) continue;
    TEST_ASSERT_EQUAL_FLOAT(0.f, r[i]);
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.f, r[200]);
  // fb: the third echo on L at 300, x fb.
  ppRun(a, l, r, 2048, 100, 127, 127);
  TEST_ASSERT_FLOAT_WITHIN(1e-2f, 0.95f, l[300]);
  TEST_ASSERT_TRUE(fabsf(r[300]) < 1e-3f);
  // width 0: L == R.
  Delay b;
  b.setBuffer(big2, kBig);
  b.process(z, zl, zr, 128, 100, 0, 127, 127, kPP, 0);
  while (b.wiping()) b.process(z, zl, zr, 128, 100, 0, 127, 127, kPP, 0);
  ppRun(b, l, r, 2048, 100, 80, 0);
  TEST_ASSERT_EQUAL_MEMORY(l, r, sizeof(float) * 2048);
  TEST_ASSERT_TRUE(l[100] > 0.4f && l[200] > 0.4f);
}

void test_ping_pong_time_clamped_to_half() {
  Delay a;
  a.setBuffer(line, kLen);  // 1000: halves of 500
  static float l[2048], r[2048];
  float z[128] = {}, zl[128], zr[128];
  a.process(z, zl, zr, 128, 100, 0, 127, 127, kPP, 127);
  while (a.wiping()) a.process(z, zl, zr, 128, 100, 0, 127, 127, kPP, 127);
  ppRun(a, l, r, 2048, 5000, 0, 127);
  TEST_ASSERT_EQUAL_INT(499, peakAt(l, 0, 2048));
}

void test_mode_buffer_none_leaves_mix() {
  Delay a;
  float s[8] = {1, 1, 1, 1, 1, 1, 1, 1}, l[8] = {0.5f}, r[8] = {0.25f};
  for (uint8_t m = kDig; m <= kPP; ++m) a.process(s, l, r, 8, 4, 127, 127, 127, m, 64);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, l[0]);
  TEST_ASSERT_EQUAL_FLOAT(0.25f, r[0]);
  for (int i = 1; i < 8; ++i) TEST_ASSERT_TRUE(l[i] == 0.f && r[i] == 0.f);
}

// DIGITAL with a full line -> PING-PONG: the return fades out over the block, then silence (the
// line is wiped, nothing stale comes back); and back the same way.
void test_layout_switch_fades_and_wipes() {
  Delay a;
  a.setBuffer(big, kBig);
  float s[128], l[128], r[128];
  float last = 0;
  for (int blk = 0; blk < 400; ++blk) {
    for (int i = 0; i < 128; ++i) s[i] = noise(), l[i] = r[i] = 0;
    a.process(s, l, r, 128, 3000, 120, 127, 127, kDig, 0);
    last = l[127];
  }
  for (int i = 0; i < 128; ++i) s[i] = 0, l[i] = r[i] = 0;
  a.process(s, l, r, 128, 3000, 120, 127, 127, kPP, 127);
  TEST_ASSERT_TRUE(fabsf(l[0] - last) < 2.5f);
  TEST_ASSERT_TRUE(fabsf(l[127]) < 0.05f);
  for (int blk = 0; blk < 200; ++blk) {
    for (int i = 0; i < 128; ++i) l[i] = r[i] = 0;
    a.process(s, l, r, 128, 3000, 120, 127, 127, kPP, 127);
    for (int i = 0; i < 128; ++i) TEST_ASSERT_TRUE(l[i] == 0.f && r[i] == 0.f);
  }
  TEST_ASSERT_FALSE(a.wiping());
}

// DIGITAL <-> TAPE needs no wipe: a steady echo continues through the switch.
void test_digital_tape_switch_seamless() {
  Delay a;
  a.setBuffer(big, kBig);
  float s[128], l[128], r[128], prev = 0, step = 0;
  for (int blk = 0; blk < 300; ++blk) {
    for (int i = 0; i < 128; ++i) s[i] = 0.5f * sinf(6.2831853f * 200.f * (blk * 128 + i) / static_cast<float>(kSynthRate)), l[i] = r[i] = 0;
    const uint8_t mode = (blk / 50) % 2 ? kTape : kDig;
    a.process(s, l, r, 128, 2000, 60, 127, 127, mode, 64);
    for (int i = 0; i < 128; ++i) {
      if (blk > 20) step = fmaxf(step, fabsf(l[i] - prev));
      prev = l[i];
    }
    TEST_ASSERT_FALSE(a.wiping());
  }
  TEST_ASSERT_TRUE(step < 0.08f);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_impulse_returns_after_delay);
  RUN_TEST(test_level_scales_the_return);
  RUN_TEST(test_no_feedback_one_echo);
  RUN_TEST(test_feedback_repeats_decaying);
  RUN_TEST(test_tone_darkens_the_echo);
  RUN_TEST(test_no_buffer_leaves_mix);
  RUN_TEST(test_delay_clamped_to_buffer);
  RUN_TEST(test_clear_silences_the_line);
  RUN_TEST(test_line_saturates_instead_of_wrapping);
  RUN_TEST(test_return_on_both_sides);
  RUN_TEST(test_digital_and_tape_wear0_bit_exact);
  RUN_TEST(test_tape_wear_jitters_echo_time);
  RUN_TEST(test_tape_bounded_at_full_feedback);
  RUN_TEST(test_tape_time_change_glides);
  RUN_TEST(test_ping_pong_alternates);
  RUN_TEST(test_ping_pong_time_clamped_to_half);
  RUN_TEST(test_mode_buffer_none_leaves_mix);
  RUN_TEST(test_layout_switch_fades_and_wipes);
  RUN_TEST(test_digital_tape_switch_seamless);
  return UNITY_END();
}
