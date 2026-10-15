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
  v[i].heavy = true;
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

void test_heavy_load_reads_heavy_flag() {
  // Voice::heavy is the single source (set at note-on): the engine flags alone do not count.
  const int i = allocFm(1);
  TEST_ASSERT_TRUE(heavyLoad(v[i]));
  v[i].heavy = false;
  TEST_ASSERT_FALSE(heavyLoad(v[i]));  // fm still true
  v[i].fm = false;
  v[i].heavy = true;
  TEST_ASSERT_TRUE(heavyLoad(v[i]));
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

// ---- Cost budget ----

// A held note of cost on track (budget in the same units).
static int allocCost(uint8_t track, int cost, int budget, bool mono = false, int polyMax = kPolyPerTrack) {
  bool l;
  const int i = allocVoice(v, track, mono, age, l, false, polyMax, kVoices, cost, budget);
  v[i].env.set(0, 0, 1.f, 100);
  v[i].env.gate(true);
  v[i].env.next();
  return i;
}
static int stolenCount() {
  int n = 0;
  for (const auto& x : v) n += x.on && x.stolen;
  return n;
}

void test_budget_off_steals_nothing() {
  for (int k = 0; k < 40; ++k) alloc(static_cast<uint8_t>(k % 20));
  TEST_ASSERT_EQUAL(0, stolenCount());
}

void test_budget_over_steals_oldest() {
  const int a = allocCost(0, 300, 1000);
  const int b = allocCost(1, 300, 1000);
  const int c = allocCost(2, 300, 1000);
  TEST_ASSERT_EQUAL(0, stolenCount());
  const int d = allocCost(3, 300, 1000);
  TEST_ASSERT_TRUE(v[a].stolen);
  TEST_ASSERT_TRUE(v[a].env.stage() == Env::Stage::Release);  // fades, no hard cut
  TEST_ASSERT_FALSE(v[b].stolen);
  TEST_ASSERT_FALSE(v[c].stolen);
  TEST_ASSERT_NOT_EQUAL(a, d);
  TEST_ASSERT_TRUE(v[d].on);
  TEST_ASSERT_FALSE(v[d].stolen);
  TEST_ASSERT_EQUAL(300, v[d].cost);
  TEST_ASSERT_EQUAL(1, stolenCount());
}

void test_budget_prefers_releasing_victim() {
  const int a = allocCost(0, 300, 1000);
  const int b = allocCost(1, 300, 1000);
  allocCost(2, 300, 1000);
  v[b].env.gate(false);
  allocCost(3, 300, 1000);
  TEST_ASSERT_TRUE(v[b].stolen);
  TEST_ASSERT_FALSE(v[a].stolen);
  TEST_ASSERT_EQUAL(1, stolenCount());
}

void test_budget_big_note_steals_until_it_fits() {
  // used 900: 900 + 900 > 1000 -> a (600 left), 1500 > 1000 -> b (300), 1200 > 1000 -> c (0): all three.
  const int a = allocCost(0, 300, 1000);
  const int b = allocCost(1, 300, 1000);
  const int c = allocCost(2, 300, 1000);
  const int d = allocCost(3, 900, 1000);
  TEST_ASSERT_TRUE(v[a].stolen && v[b].stolen && v[c].stolen);
  TEST_ASSERT_FALSE(v[d].stolen);
  TEST_ASSERT_EQUAL(3, stolenCount());
}

void test_budget_note_always_plays() {
  const int a = allocCost(0, 300, 1000);
  const int d = allocCost(1, 1500, 1000);  // never fits: steals all, plays anyway
  TEST_ASSERT_TRUE(v[a].stolen);
  TEST_ASSERT_TRUE(v[d].on);
  TEST_ASSERT_FALSE(v[d].stolen);
}

void test_budget_mono_legato_not_counted_twice() {
  // 300 + 600 fits; the retrigger reuses the 600 voice, so 300 + 600 again, not 300 + 600 + 600.
  const int other = allocCost(1, 300, 1000);
  const int a = allocCost(0, 600, 1000, true);
  const int b = allocCost(0, 600, 1000, true);
  TEST_ASSERT_EQUAL(a, b);
  TEST_ASSERT_FALSE(v[other].stolen);
  TEST_ASSERT_EQUAL(0, stolenCount());
}

void test_budget_poly_track_steal_not_counted_twice() {
  // Track 0 at polyMax 2 reuses its oldest voice: 600 + 300, nothing stolen.
  const int a = allocCost(0, 300, 1000, false, 2);
  allocCost(0, 300, 1000, false, 2);
  const int other = allocCost(1, 300, 1000, false, 2);
  TEST_ASSERT_EQUAL(a, allocCost(0, 300, 1000, false, 2));
  TEST_ASSERT_FALSE(v[other].stolen);
  TEST_ASSERT_EQUAL(0, stolenCount());
}

void test_budget_cheap_notes_steal_nothing() {
  for (int k = 0; k < 10; ++k) allocCost(static_cast<uint8_t>(k), 100, 1000);
  TEST_ASSERT_EQUAL(0, stolenCount());
}

void test_budget_ignores_stolen_voices() {
  // A stolen (fading) voice no longer counts: the 5th note steals one more, not two.
  for (int k = 0; k < 4; ++k) allocCost(static_cast<uint8_t>(k), 300, 1000);  // steals the 1st
  allocCost(4, 300, 1000);
  TEST_ASSERT_EQUAL(2, stolenCount());
}

// A held heavy note of cost on track.
static int allocHeavyCost(uint8_t track, int cost, int budget) {
  bool l;
  const int i = allocVoice(v, track, false, age, l, true, kPolyPerTrack, kVoices, cost, budget);
  v[i].fm = true;
  v[i].heavy = true;
  v[i].env.set(0, 0, 1.f, 100);
  v[i].env.gate(true);
  v[i].env.next();
  return i;
}

void test_budget_heavy_victim_counted_once() {
  // kFmVoiceMax heavy voices of 100 fill the budget. A heavy 200 note steals the oldest heavy voice
  // (heavy limit, 100 back) and still needs 100 more: exactly one more victim.
  const int budget = kFmVoiceMax * 100;
  for (int k = 0; k < kFmVoiceMax; ++k) allocHeavyCost(static_cast<uint8_t>(1 + k % 4), 100, budget);
  const int got = allocHeavyCost(5, 200, budget);
  TEST_ASSERT_TRUE(v[byAge(1)].stolen);
  TEST_ASSERT_TRUE(v[byAge(2)].stolen);
  TEST_ASSERT_FALSE(v[got].stolen);
  TEST_ASSERT_EQUAL(2, stolenCount());
}

void test_budget_heavy_victim_reused_as_pick() {
  // No free voice: the heavy victim itself is the pick and gives its 100 back; a 200 note needs one more.
  const int budget = kVoices * 100;
  for (int k = 0; k < kVoices - kFmVoiceMax; ++k) allocCost(static_cast<uint8_t>(8 + k), 100, budget);
  for (int k = 0; k < kFmVoiceMax; ++k) allocHeavyCost(static_cast<uint8_t>(1 + k % 4), 100, budget);
  const int victim = byAge(kVoices - kFmVoiceMax + 1);
  const int chip = byAge(1);
  TEST_ASSERT_EQUAL(victim, allocHeavyCost(5, 200, budget));
  TEST_ASSERT_FALSE(v[victim].stolen);
  TEST_ASSERT_TRUE(v[chip].stolen);
  TEST_ASSERT_EQUAL(1, stolenCount());
}

void test_budget_stolen_pick_not_given_back() {
  // Full pool, the oldest voice already stolen (not in the sum) and picked: kVoices - 1 live of 100
  // plus the new 100 is over a budget of kVoices * 100 - 50, so one more voice goes.
  const int budget = kVoices * 100 - 50;
  for (int k = 0; k < kVoices; ++k) allocCost(static_cast<uint8_t>(k), 100, kVoiceBudgetOff);
  const int first = byAge(1);
  v[first].env.fade(kStealMs);
  v[first].stolen = true;
  TEST_ASSERT_EQUAL(first, allocCost(30, 100, budget));
  TEST_ASSERT_FALSE(v[first].stolen);
  TEST_ASSERT_TRUE(v[byAge(2)].stolen);
  TEST_ASSERT_EQUAL(1, stolenCount());
}

void test_budget_skips_zero_cost_victims() {
  // Stealing a 0-cost voice frees nothing: the oldest priced voice goes instead.
  const int free0 = allocCost(0, 0, 1000);
  const int a = allocCost(1, 300, 1000);
  allocCost(2, 300, 1000);
  allocCost(3, 300, 1000);
  allocCost(4, 300, 1000);
  TEST_ASSERT_FALSE(v[free0].stolen);
  TEST_ASSERT_TRUE(v[a].stolen);
  TEST_ASSERT_EQUAL(1, stolenCount());
}

void test_budget_cost_clamped() {
  const int i = allocCost(0, 70000, kVoiceBudgetOff);
  TEST_ASSERT_EQUAL(65535, v[i].cost);
  const int j = allocCost(1, -5, kVoiceBudgetOff);
  TEST_ASSERT_EQUAL(0, v[j].cost);
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
  RUN_TEST(test_heavy_load_reads_heavy_flag);
  RUN_TEST(test_poly_max_eight);
  RUN_TEST(test_budget_off_steals_nothing);
  RUN_TEST(test_budget_over_steals_oldest);
  RUN_TEST(test_budget_prefers_releasing_victim);
  RUN_TEST(test_budget_big_note_steals_until_it_fits);
  RUN_TEST(test_budget_note_always_plays);
  RUN_TEST(test_budget_mono_legato_not_counted_twice);
  RUN_TEST(test_budget_poly_track_steal_not_counted_twice);
  RUN_TEST(test_budget_cheap_notes_steal_nothing);
  RUN_TEST(test_budget_ignores_stolen_voices);
  RUN_TEST(test_budget_heavy_victim_counted_once);
  RUN_TEST(test_budget_heavy_victim_reused_as_pick);
  RUN_TEST(test_budget_stolen_pick_not_given_back);
  RUN_TEST(test_budget_skips_zero_cost_victims);
  RUN_TEST(test_budget_cost_clamped);
  return UNITY_END();
}
