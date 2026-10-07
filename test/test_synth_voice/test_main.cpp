#include <unity.h>
#include "synth_voice.h"

using namespace mt;

static Voice v[kVoices];
static uint32_t age;

void setUp() {
  for (auto& x : v) x = Voice();
  age = 0;
}
void tearDown() {}

static int alloc(uint8_t track, bool mono, bool& legato) { return allocVoice(v, track, mono, age, legato); }
static int alloc(uint8_t track, bool mono = false) {
  bool l;
  return alloc(track, mono, l);
}

void test_alloc_marks_voice() {
  const int i = alloc(5);
  TEST_ASSERT_TRUE(i >= 0 && i < kVoices);
  TEST_ASSERT_TRUE(v[i].on);
  TEST_ASSERT_EQUAL(5, v[i].track);
  TEST_ASSERT_EQUAL(1u, v[i].age);
  TEST_ASSERT_EQUAL(1u, age);
}

void test_poly_steals_oldest_of_track() {
  int got[4];
  for (int k = 0; k < 4; ++k) {
    got[k] = alloc(0);
    for (int j = 0; j < k; ++j) TEST_ASSERT_NOT_EQUAL(got[j], got[k]);
  }
  const int other = alloc(1);
  const int fifth = alloc(0);
  TEST_ASSERT_EQUAL(got[0], fifth);
  TEST_ASSERT_TRUE(v[other].on);
  TEST_ASSERT_EQUAL(1, v[other].track);
  int track0 = 0;
  for (const auto& x : v) track0 += x.on && x.track == 0;
  TEST_ASSERT_EQUAL(4, track0);
}

void test_mono_legato() {
  bool legato = true;
  const int a = alloc(2, true, legato);
  TEST_ASSERT_FALSE(legato);
  const int b = alloc(2, true, legato);
  TEST_ASSERT_EQUAL(a, b);
  TEST_ASSERT_TRUE(legato);
  v[a].on = false;  // envelope went idle
  alloc(2, true, legato);
  TEST_ASSERT_FALSE(legato);
}

void test_mono_ignores_other_tracks() {
  bool legato = true;
  const int a = alloc(3, true, legato);
  const int b = alloc(4, true, legato);
  TEST_ASSERT_NOT_EQUAL(a, b);
  TEST_ASSERT_FALSE(legato);
}

// Every voice holding a note (an on voice with an idle env is a filter tail, reused first).
static void fillPool() {
  for (int t = 0; t < kVoices / 2; ++t)
    for (int k = 0; k < 2; ++k) {
      const int i = alloc(static_cast<uint8_t>(t));
      v[i].env.set(0, 0, 1.f, 100);
      v[i].env.gate(true);
      v[i].env.next();
    }
}

void test_full_pool_steals_oldest_releasing() {
  fillPool();
  // Voice allocated 5th goes to release.
  int rel = -1;
  for (int i = 0; i < kVoices; ++i)
    if (v[i].age == 5) rel = i;
  TEST_ASSERT_TRUE(rel >= 0);
  v[rel].env.set(0, 0, 1.f, 100);
  v[rel].env.gate(true);
  v[rel].env.next();
  v[rel].env.gate(false);
  const int got = alloc(7);
  TEST_ASSERT_EQUAL(rel, got);
  TEST_ASSERT_EQUAL(7, v[got].track);
}

void test_full_pool_steals_globally_oldest() {
  fillPool();
  int oldest = -1;
  for (int i = 0; i < kVoices; ++i)
    if (v[i].age == 1) oldest = i;
  const int got = alloc(7);
  TEST_ASSERT_EQUAL(oldest, got);
}

// ---- FM voice limit ----

static int allocFm(uint8_t track, bool mono, bool& legato) {
  const int i = allocVoice(v, track, mono, age, legato, true);
  v[i].fm = true;
  v[i].env.set(0, 0, 1.f, 100);
  v[i].env.gate(true);
  v[i].env.next();
  return i;
}
static int allocFm(uint8_t track, bool mono = false) {
  bool l;
  return allocFm(track, mono, l);
}
static int fmCount() {
  int n = 0;
  for (const auto& x : v) n += heavyLoad(x);
  return n;
}

// 2 CHIP voices on track 0, then 8 FM voices on tracks 1..4 (ages 3..10).
static void fillFm() {
  alloc(0);
  alloc(0);
  for (int t = 1; t <= 4; ++t)
    for (int k = 0; k < 2; ++k) allocFm(static_cast<uint8_t>(t));
}

static int byAge(uint32_t a) {
  for (int i = 0; i < kVoices; ++i)
    if (v[i].on && v[i].age == a) return i;
  return -1;
}

void test_fm_limit_steals_oldest_fm_not_free_or_chip() {
  fillFm();
  const int oldestFm = byAge(3);
  const int got = allocFm(5);
  // The victim fades out (no hard cut) and the note takes a free voice.
  TEST_ASSERT_NOT_EQUAL(oldestFm, got);
  TEST_ASSERT_TRUE(v[oldestFm].stolen);
  TEST_ASSERT_TRUE(v[oldestFm].env.stage() == Env::Stage::Release);
  TEST_ASSERT_EQUAL(5, v[got].track);
  TEST_ASSERT_EQUAL(kFmVoiceMax, fmCount());
  int chip = 0;
  for (const auto& x : v) chip += x.on && !x.fm && x.track == 0;
  TEST_ASSERT_EQUAL(2, chip);
}

void test_fm_limit_prefers_released_fm() {
  fillFm();
  const int rel = byAge(6);
  v[rel].env.gate(false);
  TEST_ASSERT_NOT_EQUAL(rel, allocFm(6));
  TEST_ASSERT_TRUE(v[rel].stolen);
  TEST_ASSERT_EQUAL(kFmVoiceMax, fmCount());
}

void test_fm_limit_mono_retrigger_keeps_own_voice() {
  fillFm();
  bool legato = false;
  const int own = byAge(10);  // newest of track 4
  v[byAge(9)].on = false;     // track 4 mono: one voice
  allocFm(7);                 // back to 8 FM voices
  const int got = allocFm(4, true, legato);
  TEST_ASSERT_EQUAL(own, got);
  TEST_ASSERT_TRUE(legato);
  TEST_ASSERT_EQUAL(kFmVoiceMax, fmCount());
}

void test_fm_limit_below_max_takes_free_voice() {
  alloc(0);
  for (int k = 0; k < 7; ++k) allocFm(static_cast<uint8_t>(1 + k % 4));
  bool legato;
  const int got = allocVoice(v, 6, false, age, legato, true);
  TEST_ASSERT_FALSE(v[got].fm);  // a free voice, nothing stolen
}

void test_fm_limit_mono_from_chip_releases_chip_voice() {
  fillFm();
  bool legato = true;
  const int chip = alloc(5, true);
  v[chip].env.set(0, 0, 1.f, 100);
  v[chip].env.gate(true);
  v[chip].env.next();
  const int got = allocFm(5, true, legato);
  TEST_ASSERT_NOT_EQUAL(chip, got);
  TEST_ASSERT_FALSE(legato);
  TEST_ASSERT_TRUE(v[chip].env.stage() == Env::Stage::Release);  // not left hanging
  TEST_ASSERT_EQUAL(kFmVoiceMax, fmCount());
}

void test_heavy_limit_counts_drum_with_fm() {
  // 4 FM + 4 DRUM voices fill kFmVoiceMax together: a 9th heavy note steals the oldest of them.
  alloc(0);
  for (int k = 0; k < kFmVoiceMax; ++k) {
    const int i = allocFm(static_cast<uint8_t>(1 + k % 4));
    if (k % 2 == 0) v[i].fm = false, v[i].drum = true;  // the oldest is DRUM
  }
  const int oldest = byAge(2);
  allocFm(5);
  TEST_ASSERT_TRUE(v[oldest].stolen);
  TEST_ASSERT_EQUAL(kFmVoiceMax, fmCount());
}

void test_heavy_limit_no_free_voice_takes_victim() {
  // CHIP voices fill the rest of the pool: with nothing free the victim itself is reused.
  for (int k = 0; k < kVoices - kFmVoiceMax; ++k) alloc(static_cast<uint8_t>(8 + k));
  for (int k = 0; k < kFmVoiceMax; ++k) allocFm(static_cast<uint8_t>(1 + k % 4));
  const int oldest = byAge(kVoices - kFmVoiceMax + 1);
  TEST_ASSERT_EQUAL(oldest, allocFm(5));
  TEST_ASSERT_FALSE(v[oldest].stolen);
}

void test_heavy_limit_skips_stolen_and_tails() {
  // A stolen (fading) voice and a filter tail (env idle) do not count against the limit.
  fillFm();
  allocFm(5);  // steals age 3
  const int tail = byAge(4);
  v[tail].env.kill();
  const int got = allocFm(6);
  TEST_ASSERT_TRUE(v[got].fm || !v[got].on || v[got].track == 6);
  int stolen = 0;
  for (const auto& x : v) stolen += x.on && x.stolen;
  TEST_ASSERT_EQUAL(1, stolen);  // 7 counted + the tail freed a slot: nothing more stolen
}

void test_poly_max_eight() {
  int got[8];
  for (int k = 0; k < 8; ++k) {
    bool l;
    got[k] = allocVoice(v, 0, false, age, l, false, 8);
    for (int j = 0; j < k; ++j) TEST_ASSERT_NOT_EQUAL(got[j], got[k]);
  }
  bool l;
  TEST_ASSERT_EQUAL(got[0], allocVoice(v, 0, false, age, l, false, 8));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_alloc_marks_voice);
  RUN_TEST(test_poly_steals_oldest_of_track);
  RUN_TEST(test_mono_legato);
  RUN_TEST(test_mono_ignores_other_tracks);
  RUN_TEST(test_full_pool_steals_oldest_releasing);
  RUN_TEST(test_full_pool_steals_globally_oldest);
  RUN_TEST(test_fm_limit_steals_oldest_fm_not_free_or_chip);
  RUN_TEST(test_fm_limit_prefers_released_fm);
  RUN_TEST(test_fm_limit_mono_retrigger_keeps_own_voice);
  RUN_TEST(test_fm_limit_below_max_takes_free_voice);
  RUN_TEST(test_fm_limit_mono_from_chip_releases_chip_voice);
  RUN_TEST(test_heavy_limit_counts_drum_with_fm);
  RUN_TEST(test_heavy_limit_no_free_voice_takes_victim);
  RUN_TEST(test_heavy_limit_skips_stolen_and_tails);
  RUN_TEST(test_poly_max_eight);
  return UNITY_END();
}
