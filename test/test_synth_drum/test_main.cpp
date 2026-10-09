#include <math.h>
#include <unity.h>
#include "synth_drum.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static float buf[kSynthRate];  // 1 s

static void run(DrumVoice& d, const DrumParams& p, int n) {
  for (int i = 0; i < n; ++i) buf[i] = 0;
  for (int pos = 0; pos < n; pos += 32) {
    d.control(p, 32);
    d.render(buf + pos, 32, 1.f);
  }
}

void test_tone_frequency() {
  DrumParams p;
  p.toneHz[0] = 200;
  p.toneLvl[0] = 1;
  p.toneMs = 0;  // no decay
  DrumVoice d;
  d.trigger(false);
  run(d, p, kSynthRate / 2);
  int c = 0;
  for (int i = 1; i < kSynthRate / 2; ++i) c += buf[i - 1] > 0 && buf[i] <= 0;
  TEST_ASSERT_INT_WITHIN(2, 100, c);
}

void test_decay_and_done() {
  DrumParams p;
  p.toneHz[0] = 100;
  p.toneLvl[0] = 1;
  p.toneMs = 100;
  DrumVoice d;
  d.trigger(false);
  run(d, p, kSynthRate / 4);  // 250 ms > 100 ms to -60 dB
  float peak = 0;
  for (int i = kSynthRate / 8; i < kSynthRate / 4; ++i) peak = fmaxf(peak, fabsf(buf[i]));
  TEST_ASSERT_TRUE(peak < 0.002f);
  run(d, p, kSynthRate / 4);
  TEST_ASSERT_TRUE(d.done());
}

void test_metal_bounded_finite() {
  DrumParams p;
  const float hz[kDrumMetal] = {205.3f, 304.4f, 369.6f, 522.7f, 540.f, 800.f};
  for (int k = 0; k < kDrumMetal; ++k) p.metalHz[k] = hz[k];
  p.metalLvl = 1;
  p.metalMs = 500;
  p.metalBp1 = 3440;
  p.metalBp2 = 7100;
  p.metalHp = 7000;
  p.noiseLvl = 1;
  p.noiseMs = 300;
  p.noiseHz = 8000;
  p.drive = 1;
  DrumVoice d;
  d.trigger(false);
  run(d, p, kSynthRate / 2);
  for (int i = 0; i < kSynthRate / 2; ++i) {
    TEST_ASSERT_TRUE(isfinite(buf[i]));
    TEST_ASSERT_TRUE(fabsf(buf[i]) <= 1.01f);
  }
}

void test_clap_bursts() {
  DrumParams p;
  p.noiseLvl = 1;
  p.noiseMs = 200;
  p.bursts = 3;
  p.burstMs = 10;
  p.tail = 1;
  DrumVoice d;
  d.trigger(false);
  run(d, p, kSynthRate / 10);
  // Envelope peaks near 0, 10, 20 ms: energy right after each restart beats the one before it.
  auto e = [](int ms) {
    float a = 0;
    for (int i = ms * kSynthRate / 1000; i < (ms + 1) * kSynthRate / 1000; ++i) a += buf[i] * buf[i];
    return a;
  };
  TEST_ASSERT_TRUE(e(10) > e(9) * 2);
  TEST_ASSERT_TRUE(e(20) > e(19) * 2);
}

void test_choke_tail_no_jump() {
  DrumParams p;
  p.toneHz[0] = 50;
  p.toneLvl[0] = 1;
  p.toneMs = 2000;
  DrumVoice d;
  d.trigger(false);
  run(d, p, 1024);  // whole control blocks: run() renders in 32s
  const float last = buf[1023];
  d.trigger(true);  // choke: the old output fades over ~2 ms
  float first[32] = {0};
  d.control(p, 32);
  d.render(first, 32, 1.f);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, last, first[0]);
}

// The tail is the voice's output (amp included, earlier tail included): a choke right after a
// choke continues from what was heard.
void test_choke_tail_is_output() {
  DrumParams p;
  p.toneHz[0] = 50;
  p.toneLvl[0] = 1;
  p.toneMs = 2000;
  DrumVoice d;
  d.trigger(false);
  float b[32];
  for (int blk = 0; blk < 8; ++blk) {
    for (float& x : b) x = 0;
    d.control(p, 32);
    d.render(b, 32, 0.5f);
  }
  float last = b[31];
  for (int hit = 0; hit < 2; ++hit) {
    d.trigger(true);
    for (float& x : b) x = 0;
    d.control(p, 32);
    d.render(b, 2, 0.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, last, b[0]);
    last = b[1];
  }
}

void test_clap_tail_uses_noise_decay() {
  DrumParams p;
  p.noiseLvl = 1;
  p.noiseMs = 200;
  p.bursts = 3;
  p.burstMs = 10;
  p.tail = 0.5f;
  DrumVoice d;
  d.trigger(false);
  run(d, p, kSynthRate / 10);
  // 30 ms: the tail starts at 0.5 and loses 3 dB by 40 ms (200 ms to -60 dB), not the burst decay.
  float a = 0;
  constexpr int kFrom = 40 * kSynthRate / 1000, kTo = 41 * kSynthRate / 1000;
  for (int i = kFrom; i < kTo; ++i) a += buf[i] * buf[i];
  const float rms = sqrtf(a / (kTo - kFrom));
  TEST_ASSERT_TRUE(rms > 0.12f && rms < 0.25f);
}

void test_fresh_voice_done() {
  DrumVoice d;
  TEST_ASSERT_TRUE(d.done());
  d.trigger(false);
  TEST_ASSERT_FALSE(d.done());  // until the first control() and the sound decays
}

void test_bursts_without_noise_done() {
  DrumParams p;
  p.toneHz[0] = 100;
  p.toneLvl[0] = 1;
  p.toneMs = 20;
  p.bursts = 4;
  p.burstMs = 10;
  DrumVoice d;
  d.trigger(false);
  run(d, p, kSynthRate / 5);
  TEST_ASSERT_TRUE(d.done());
}

// Envelopes run from the trigger even while their part is silent: a level raised later (LFO)
// doesn't bring the part in at full trigger level.
void test_late_level_stays_decayed() {
  DrumParams p;
  p.toneHz[0] = 200;
  p.toneMs = 50;
  for (int k = 0; k < kDrumMetal; ++k) p.metalHz[k] = 300.f + 100.f * k;
  p.metalMs = 50;
  p.noiseMs = 50;
  p.click = 0;
  p.clickMs = 5;
  DrumVoice d;
  d.trigger(false);
  run(d, p, kSynthRate / 5);  // 200 ms, all levels 0
  p.toneLvl[0] = 1;
  p.metalLvl = 1;
  p.noiseLvl = 1;
  p.click = 1;
  run(d, p, 3200);
  float peak = 0;
  for (int i = 0; i < 3200; ++i) peak = fmaxf(peak, fabsf(buf[i]));
  TEST_ASSERT_TRUE(peak < 0.002f);
  TEST_ASSERT_TRUE(d.done());
}

// The pitch ramp stops at its target when more than span samples are rendered.
void test_tone_ramp_clamped() {
  DrumParams p;
  p.toneHz[0] = 100;
  p.toneLvl[0] = 1;
  p.pitchEnv = 24;  // 400 Hz at the trigger
  p.pitchMs = 10;   // -6 dB = 12 semitones down in 1 ms: 200 Hz
  constexpr int kMs = kSynthRate / 1000, kN = kSynthRate / 10;
  DrumVoice d;
  d.trigger(false);
  for (int i = 0; i < kN; ++i) buf[i] = 0;
  d.control(p, kMs);
  d.render(buf, kN, 1.f);
  int c = 0;
  for (int i = kN / 10 + 1; i < kN; ++i) c += buf[i - 1] > 0 && buf[i] <= 0;
  TEST_ASSERT_INT_WITHIN(2, 18, c);  // 90 ms at 200 Hz
}

// Squares above the Nyquist limit (hz 0) drop out without raising the others' level.
void test_metal_norm_by_weights() {
  DrumParams p;
  for (int k = 0; k < kDrumMetal; ++k) p.metalHz[k] = 300.f + 100.f * k;
  p.metalHz[5] = 0;
  p.metalLvl = 1;
  DrumVoice d;
  d.trigger(false);
  float b[1] = {0};
  d.control(p, 32);
  d.render(b, 1, 1.f);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -5.f / 6.f, b[0]);  // phase 0: every square low
}

// Two voices triggered together don't play the same noise.
void test_noise_decorrelated() {
  DrumParams p;
  p.noiseLvl = 1;
  DrumVoice a, b;
  a.trigger(false);
  b.trigger(false);
  float x[32] = {0}, y[32] = {0};
  a.control(p, 32);
  b.control(p, 32);
  a.render(x, 32, 1.f);
  b.render(y, 32, 1.f);
  int same = 0;
  for (int i = 0; i < 32; ++i) same += x[i] == y[i];
  TEST_ASSERT_TRUE(same < 4);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_tone_frequency);
  RUN_TEST(test_decay_and_done);
  RUN_TEST(test_metal_bounded_finite);
  RUN_TEST(test_clap_bursts);
  RUN_TEST(test_choke_tail_no_jump);
  RUN_TEST(test_choke_tail_is_output);
  RUN_TEST(test_clap_tail_uses_noise_decay);
  RUN_TEST(test_fresh_voice_done);
  RUN_TEST(test_bursts_without_noise_done);
  RUN_TEST(test_late_level_stays_decayed);
  RUN_TEST(test_tone_ramp_clamped);
  RUN_TEST(test_metal_norm_by_weights);
  RUN_TEST(test_noise_decorrelated);
  return UNITY_END();
}
