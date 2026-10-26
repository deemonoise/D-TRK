// CHIP SID / AY waves through ChipOsc directly (docs/plans/2026-10-26-chip-sid-ay-design.md).
#include <math.h>
#include <type_traits>
#include <unity.h>
#include "synth_osc.h"

using namespace mt;

static_assert(std::is_trivially_copyable<ChipOsc>::value, "ChipOsc lives in Voice, copied as a whole");

void setUp() {}
void tearDown() {}

namespace {

void render(Wave w, float hz, float duty, float* out, int n) {
  ChipOsc o;
  const float inc = hz / kSynthRate;
  for (int i = 0; i < n; ++i) out[i] = o.next(w, inc, duty);
}
float mean(const float* x, int n) {
  double s = 0;
  for (int i = 0; i < n; ++i) s += x[i];
  return static_cast<float>(s / n);
}
float rms(const float* x, int n) {
  double s = 0;
  for (int i = 0; i < n; ++i) s += x[i] * x[i];
  return sqrtf(static_cast<float>(s / n));
}

int distinctLevels(const float* x, int n) {
  float seen[64];
  int d = 0;
  for (int i = 0; i < n && d < 64; ++i) {
    bool f = false;
    for (int j = 0; j < d; ++j) f |= fabsf(seen[j] - x[i]) < 1e-6f;
    if (!f) seen[d++] = x[i];
  }
  return d;
}

float buf[kSynthRate], ref[kSynthRate];

}  // namespace

void test_sid_combined_level_and_dc() {
  render(Wave::Pulse, 440, 0.5f, ref, kSynthRate);
  const float r0 = rms(ref, kSynthRate);
  const Wave ws[] = {Wave::SidTs, Wave::SidPs, Wave::SidPt, Wave::SidPts};
  for (Wave w : ws) {
    render(w, 440, 0.5f, buf, kSynthRate);
    for (int i = 0; i < kSynthRate; ++i) TEST_ASSERT_TRUE(fabsf(buf[i]) <= 1.05f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.f, mean(buf, kSynthRate));
    // Spiky waves normalised to the worst peak over duty: 9..16 dB under PULSE (RMS within 3 dB would clip).
    const float db = 20.f * log10f(rms(buf, kSynthRate) / r0);
    TEST_ASSERT_TRUE(db > -17.f && db < 3.f);
  }
}

void test_sid_combined_differs_from_parts() {
  render(Wave::Saw, 440, 0.5f, ref, 4410);
  render(Wave::SidTs, 440, 0.5f, buf, 4410);
  float d = 0;
  for (int i = 0; i < 4410; ++i) d += fabsf(buf[i] - ref[i]);
  TEST_ASSERT_TRUE(d / 4410 > 0.1f);
}

// With PULSE in the AND the output is one constant while the square is low.
void test_sid_pulse_combined_flat_when_pulse_low() {
  const Wave ws[] = {Wave::SidPs, Wave::SidPt, Wave::SidPts};
  for (Wave w : ws) {
    ChipOsc o;
    const float inc = 440.f / kSynthRate;
    bool have = false;
    float low = 0;
    for (int i = 0; i < 4410; ++i) {
      const bool off = o.phase >= 0.5f;
      const float v = o.next(w, inc, 0.5f);
      if (!off) continue;
      if (!have) low = v, have = true;
      TEST_ASSERT_EQUAL_FLOAT(low, v);
    }
    TEST_ASSERT_TRUE(have);
  }
}

void test_sid_noise_multilevel() {
  render(Wave::SidNoise, 440 * 16, 0.5f, buf, 4410);  // inc as Synth sets it: note x 16
  int distinct = 0;
  float seen[64];
  for (int i = 0; i < 4410 && distinct < 64; ++i) {
    bool f = false;
    for (int j = 0; j < distinct; ++j) f |= seen[j] == buf[i];
    if (!f) seen[distinct++] = buf[i];
  }
  TEST_ASSERT_TRUE(distinct > 8);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.f, mean(buf, 4410));
  for (int i = 0; i < 4410; ++i) {  // 8-bit output: (v + 1) x 127.5 is a whole 0..255
    const float k = (buf[i] + 1.f) * 127.5f;
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, roundf(k), k);
  }
}

void test_ay_noise_is_binary() {
  render(Wave::AyNoise, 440 * 93, 0.5f, buf, 4410);
  int ups = 0;
  for (int i = 0; i < 4410; ++i) {
    TEST_ASSERT_TRUE(fabsf(fabsf(buf[i]) - 1.f) < 1e-6f);
    ups += buf[i] > 0;
  }
  TEST_ASSERT_TRUE(ups > 4410 / 4 && ups < 4410 * 3 / 4);  // noise, not a constant
}

// 17-bit LFSR, feedback bit 0 ^ bit 3: maximal, period 2^17 - 1 (one step per oscillator cycle).
void test_ay_noise_period() {
  ChipOsc o;
  o.next(Wave::AyNoise, 1.f, 0.5f);  // inc 1: one LFSR step per call
  const uint32_t start = o.lfsr;
  int n = 1;
  for (; n <= (1 << 17); ++n) {
    o.next(Wave::AyNoise, 1.f, 0.5f);
    if (o.lfsr == start) break;
  }
  TEST_ASSERT_EQUAL((1 << 17) - 1, n);
}

void test_ay_tone_noise_silent_when_square_low() {
  ChipOsc o;
  const float inc = 440.f / kSynthRate;
  int highs = 0;
  for (int i = 0; i < 4410; ++i) {
    const bool low = o.phase >= 0.5f;  // phase before next() decides the sample
    const float v = o.next(Wave::AyToneNoise, inc, 0.5f);
    if (low) TEST_ASSERT_FLOAT_WITHIN(1e-6f, ChipOsc::kAyTnLow, v);
    highs += v == ChipOsc::kAyTnHigh;
    TEST_ASSERT_TRUE(fabsf(v) <= 1.05f);
  }
  TEST_ASSERT_TRUE(highs > 4410 / 8 && highs < 4410 * 3 / 8);  // ~25 %: square AND noise
}

void test_ay_saw_16_levels() {
  render(Wave::AySaw, 110, 0.5f, buf, 4410);
  TEST_ASSERT_EQUAL(16, distinctLevels(buf, 4410));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.f, mean(buf, 4410));
  for (int i = 0; i < 4410; ++i) TEST_ASSERT_TRUE(fabsf(buf[i]) <= 1.05f);
  const int half = kSynthRate / 110 / 2;  // saw down: falls within the first cycle
  for (int i = 1; i < 2 * half - 1; ++i) TEST_ASSERT_TRUE(buf[i] <= buf[i - 1] + 1e-6f);
}

void test_ay_tri_16_levels_32_steps() {
  render(Wave::AyTri, 110, 0.5f, buf, 4410);
  TEST_ASSERT_EQUAL(16, distinctLevels(buf, 4410));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.f, mean(buf, 4410));
  for (int i = 0; i < 4410; ++i) TEST_ASSERT_TRUE(fabsf(buf[i]) <= 1.05f);
  // monotonic down over the first half-cycle, up over the second
  const int half = kSynthRate / 110 / 2;
  for (int i = 1; i < half - 1; ++i) TEST_ASSERT_TRUE(buf[i] <= buf[i - 1] + 1e-6f);
  for (int i = half + 2; i < 2 * half - 1; ++i) TEST_ASSERT_TRUE(buf[i] >= buf[i - 1] - 1e-6f);
}

void test_ay_levels_log() {
  TEST_ASSERT_EQUAL_FLOAT(0.f, kAyLevel[0]);
  TEST_ASSERT_EQUAL_FLOAT(1.f, kAyLevel[15]);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.841f, kAyLevel[14]);  // -1.5 dB
  for (int n = 2; n < 16; ++n) TEST_ASSERT_TRUE(kAyLevel[n] > kAyLevel[n - 1]);
}

// DUTY moves the pulse part: every SID wave stays within +-1.05 at any duty.
// STEP: ayStep picks the nearest kAyLevel (ties: the upper level), as a linear scan does.
void test_ay_step_nearest_level() {
  for (int k = 0; k <= 1000; ++k) {
    const float e = k / 1000.f;
    int best = 0;
    for (int n = 1; n < 16; ++n)
      if (fabsf(e - kAyLevel[n]) <= fabsf(e - kAyLevel[best])) best = n;
    TEST_ASSERT_EQUAL_FLOAT(kAyLevel[best], ayStep(e));
  }
  for (int n = 0; n < 16; ++n) TEST_ASSERT_EQUAL_FLOAT(kAyLevel[n], ayStep(kAyLevel[n]));
}

void test_sid_combined_no_clip_any_duty() {
  const Wave ws[] = {Wave::SidTs, Wave::SidPs, Wave::SidPt, Wave::SidPts};
  const float duties[] = {0.01f, 0.25f, 0.5f, 0.75f, 0.99f};
  for (Wave w : ws)
    for (float d : duties) {
      render(w, 440, d, buf, 4410);
      for (int i = 0; i < 4410; ++i) TEST_ASSERT_TRUE(fabsf(buf[i]) <= 1.05f);
    }
}

void test_sid_ts_differs_from_pts() {
  render(Wave::SidTs, 440, 0.5f, ref, 4410);
  render(Wave::SidPts, 440, 0.5f, buf, 4410);
  float d = 0;
  for (int i = 0; i < 4410; ++i) d += fabsf(buf[i] - ref[i]);
  TEST_ASSERT_TRUE(d / 4410 > 0.05f);
}

// Bit-by-bit reference of the SID noise register: 23 bits, new bit 0 = bit 22 ^ bit 17.
void test_sid_noise_golden_from_seed() {
  uint8_t bit[23];
  for (int i = 0; i < 23; ++i) bit[i] = (0x7FFFF8u >> i) & 1;
  ChipOsc o;
  for (int step = 0; step < 8; ++step) {
    const uint8_t in = bit[22] ^ bit[17];
    for (int i = 22; i > 0; --i) bit[i] = bit[i - 1];
    bit[0] = in;
    uint32_t state = 0;
    for (int i = 0; i < 23; ++i) state |= static_cast<uint32_t>(bit[i]) << i;
    const int taps[8] = {20, 18, 14, 11, 9, 5, 2, 0};  // -> output bits 7..0
    int byte = 0;
    for (int k = 0; k < 8; ++k) byte = byte << 1 | bit[taps[k]];
    o.next(Wave::SidNoise, 1.f, 0.5f);  // inc 1: one LFSR step per call
    TEST_ASSERT_EQUAL_HEX32(state, o.lfsr & 0x7FFFFF);
    TEST_ASSERT_EQUAL(byte, static_cast<int>(lroundf((o.noiseVal + 1.f) * 127.5f)));
  }
}

// Taps 22 / 17 are maximal: the register comes back after 2^23 - 1 steps, never re-seeded on the way.
void test_sid_noise_period() {
  ChipOsc o;
  o.next(Wave::SidNoise, 1.f, 0.5f);
  const uint32_t start = o.lfsr;
  int n = 1;
  for (; n <= (1 << 23); ++n) {
    o.next(Wave::SidNoise, 1.f, 0.5f);
    if (o.lfsr == start) break;
  }
  TEST_ASSERT_EQUAL((1 << 23) - 1, n);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_sid_combined_level_and_dc);
  RUN_TEST(test_sid_combined_differs_from_parts);
  RUN_TEST(test_sid_pulse_combined_flat_when_pulse_low);
  RUN_TEST(test_sid_noise_multilevel);
  RUN_TEST(test_ay_noise_is_binary);
  RUN_TEST(test_ay_noise_period);
  RUN_TEST(test_ay_tone_noise_silent_when_square_low);
  RUN_TEST(test_ay_saw_16_levels);
  RUN_TEST(test_ay_tri_16_levels_32_steps);
  RUN_TEST(test_ay_levels_log);
  RUN_TEST(test_ay_step_nearest_level);
  RUN_TEST(test_sid_combined_no_clip_any_duty);
  RUN_TEST(test_sid_ts_differs_from_pts);
  RUN_TEST(test_sid_noise_golden_from_seed);
  RUN_TEST(test_sid_noise_period);
  return UNITY_END();
}
