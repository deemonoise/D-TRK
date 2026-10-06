#include <math.h>
#include <string.h>
#include <unity.h>
#include "synth_drum_machines.h"
#include "synth_osc.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static bool sane(const DrumParams& p) {
  const float lim = kSynthRate * 0.45f;
  const float* f[] = {&p.toneHz[0], &p.toneHz[1], &p.toneMs, &p.pitchEnv, &p.pitchMs, &p.click,
                      &p.metalLvl, &p.metalMs, &p.metalBp1, &p.metalBp2, &p.metalHp, &p.noiseLvl,
                      &p.noiseMs, &p.noiseHz, &p.noiseQ, &p.burstMs, &p.tail, &p.drive};
  for (const float* x : f)
    if (!isfinite(*x) || *x < 0) return false;
  for (float h : p.metalHz)
    if (!isfinite(h) || h < 0) return false;
  return p.toneHz[0] < lim && p.noiseHz < lim && p.metalBp1 < lim && p.metalHp < lim && p.drive <= 1;
}

void test_all_machines_sane() {
  const float edges[] = {0, 64, 127};
  for (int mc = 0; mc < static_cast<int>(DrumMachine::Count); ++mc)
    for (float e : edges)
      for (int note = 24; note <= 108; note += 12) {
        float mac[kFmMacros];
        for (float& x : mac) x = e;
        DrumParams p;
        drumMachine(static_cast<uint8_t>(mc), mac, note, p);
        TEST_ASSERT_TRUE_MESSAGE(sane(p), drumMachineName(static_cast<uint8_t>(mc)));
      }
}

void test_bd8_base_pitch_c4() {
  float mac[kFmMacros] = {90, 0, 0, 0, 0};
  DrumParams p;
  drumMachine(static_cast<uint8_t>(DrumMachine::Bd8), mac, 60, p);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 55.f, p.toneHz[0]);
  drumMachine(static_cast<uint8_t>(DrumMachine::Bd8), mac, 72, p);
  TEST_ASSERT_FLOAT_WITHIN(1.f, 110.f, p.toneHz[0]);
}

void test_hh8_decay_monotonic() {
  float a[kFmMacros] = {10, 64, 30, 64, 64}, b[kFmMacros] = {120, 64, 30, 64, 64};
  DrumParams pa, pb;
  drumMachine(static_cast<uint8_t>(DrumMachine::Hh8), a, 60, pa);
  drumMachine(static_cast<uint8_t>(DrumMachine::Hh8), b, 60, pb);
  TEST_ASSERT_TRUE(pb.metalMs > pa.metalMs * 10);
}

void test_names() {
  TEST_ASSERT_EQUAL_STRING("BD8", drumMachineName(0));
  TEST_ASSERT_EQUAL_STRING("CY9", drumMachineName(15));
  TEST_ASSERT_EQUAL_STRING("BD8", drumMachineName(200));  // out of range = BD8
  TEST_ASSERT_EQUAL_STRING("SNAPPY", drumMacroName(static_cast<uint8_t>(DrumMachine::Sd8), kMacShp));
  TEST_ASSERT_EQUAL_STRING("", drumMacroName(static_cast<uint8_t>(DrumMachine::Rs8), kMacCon));
}

// Every machine at mid macros makes sound, stays bounded and ends (one-shot).
void test_every_machine_sounds_and_ends() {
  static float buf[32];
  for (int mc = 0; mc < static_cast<int>(DrumMachine::Count); ++mc) {
    float mac[kFmMacros] = {64, 64, 64, 64, 64};
    DrumParams p;
    drumMachine(static_cast<uint8_t>(mc), mac, 60, p);
    DrumVoice d;
    d.trigger(false);
    float peak = 0;
    int blk = 0;
    for (; blk < 4 * kSynthRate / 32 && !d.done(); ++blk) {
      memset(buf, 0, sizeof buf);
      d.control(p, 32);
      d.render(buf, 32, 1.f);
      for (float x : buf) {
        TEST_ASSERT_TRUE(isfinite(x));
        peak = fmaxf(peak, fabsf(x));
      }
    }
    TEST_ASSERT_TRUE_MESSAGE(peak > 0.05f && peak <= 1.f, drumMachineName(static_cast<uint8_t>(mc)));
    TEST_ASSERT_TRUE_MESSAGE(d.done(), drumMachineName(static_cast<uint8_t>(mc)));
  }
}

// DECAY follows a fractional macro (LFO), not just whole steps.
void test_decay_fractional() {
  float a[kFmMacros] = {64, 0, 0, 0, 0}, b[kFmMacros] = {64.5f, 0, 0, 0, 0};
  DrumParams pa, pb;
  drumMachine(static_cast<uint8_t>(DrumMachine::Bd8), a, 60, pa);
  drumMachine(static_cast<uint8_t>(DrumMachine::Bd8), b, 60, pb);
  TEST_ASSERT_TRUE(pb.toneMs > pa.toneMs);
}

static float clapRms(uint8_t machine, float freq, float q, float& peak) {
  static float b[kSynthRate / 10];
  float mac[kFmMacros] = {64, freq, 64, q, 64};
  DrumParams p;
  drumMachine(machine, mac, 60, p);
  DrumVoice d;
  d.trigger(false);
  memset(b, 0, sizeof b);
  for (int pos = 0; pos < kSynthRate / 10; pos += 32) {
    d.control(p, 32);
    d.render(b + pos, 32, 1.f);
  }
  float a = 0;
  for (float x : b) {
    a += x * x;
    peak = fmaxf(peak, fabsf(x));
  }
  return sqrtf(a / (kSynthRate / 10));
}

// The band-passed clap noise keeps its level across FREQ and Q, without clipping.
void test_clap_level_even() {
  const uint8_t ms[] = {static_cast<uint8_t>(DrumMachine::Cp8), static_cast<uint8_t>(DrumMachine::Cp9)};
  for (uint8_t m : ms) {
    float peak = 0, lo = 1e9f, hi = 0;
    const float edge[] = {0, 127};
    for (float f : edge)
      for (float q : edge) {
        const float r = clapRms(m, f, q, peak);
        lo = fminf(lo, r);
        hi = fmaxf(hi, r);
      }
    TEST_ASSERT_TRUE(hi < lo * 1.6f);  // within ~4 dB
    TEST_ASSERT_TRUE(peak < 0.999f);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_all_machines_sane);
  RUN_TEST(test_bd8_base_pitch_c4);
  RUN_TEST(test_hh8_decay_monotonic);
  RUN_TEST(test_names);
  RUN_TEST(test_every_machine_sounds_and_ends);
  RUN_TEST(test_decay_fractional);
  RUN_TEST(test_clap_level_even);
  return UNITY_END();
}
