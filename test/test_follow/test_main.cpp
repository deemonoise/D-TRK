#include <unity.h>
#include <set>
#include <vector>
#include "follow.h"

using namespace mt;

static Project* proj(int len) {  // static storage: a Project is ~470 KB
  static Project p;
  p.reset();
  p.songLen = static_cast<uint8_t>(len);
  for (int i = 0; i < len; ++i) p.song[i] = static_cast<uint8_t>(i % kPatterns);
  return &p;
}

static void setFol(Project& p, int slot, FollowAct a, int chance = 100, int target = 0) {
  p.songFol[slot].act = static_cast<uint8_t>(a);
  p.songFol[slot].chance = static_cast<uint8_t>(chance);
  p.songFol[slot].target = static_cast<uint8_t>(target);
}

void setUp() {}
void tearDown() {}

void test_next_default_and_end() {
  Project& p = *proj(3);
  const uint32_t s = followRunSeed(p.seed, 0);
  TEST_ASSERT_EQUAL(1, followNext(p, 0, s, 0));
  TEST_ASSERT_EQUAL(2, followNext(p, 1, s, 1));
  TEST_ASSERT_EQUAL(kFollowEnd, followNext(p, 2, s, 2));  // past the last slot
  TEST_ASSERT_EQUAL(kFollowEnd, followNext(p, 5, s, 3));  // outside a shrunk song
  TEST_ASSERT_EQUAL(kFollowEnd, followNext(p, -1, s, 0));
}

void test_prev_stay_jump() {
  Project& p = *proj(4);
  const uint32_t s = followRunSeed(1, 0);
  setFol(p, 0, FollowAct::Prev);
  setFol(p, 1, FollowAct::Stay);
  setFol(p, 2, FollowAct::Jump, 100, 0);
  setFol(p, 3, FollowAct::Jump, 100, 99);  // past the song: the last slot
  for (uint32_t n = 0; n < 20; ++n) {
    TEST_ASSERT_EQUAL(3, followNext(p, 0, s, n));  // PREV from slot 0: the last
    TEST_ASSERT_EQUAL(1, followNext(p, 1, s, n));
    TEST_ASSERT_EQUAL(0, followNext(p, 2, s, n));
    TEST_ASSERT_EQUAL(3, followNext(p, 3, s, n));
  }
  setFol(p, 1, FollowAct::Prev);
  TEST_ASSERT_EQUAL(0, followNext(p, 1, s, 0));
}

void test_chance_zero_is_next_and_fifty_is_about_half() {
  Project& p = *proj(4);
  const uint32_t s = followRunSeed(42, 7);
  setFol(p, 1, FollowAct::Jump, 0, 3);
  for (uint32_t n = 0; n < 50; ++n) TEST_ASSERT_EQUAL(2, followNext(p, 1, s, n));
  setFol(p, 1, FollowAct::Jump, 50, 3);
  int jumps = 0;
  for (uint32_t n = 0; n < 1000; ++n) {
    const int nx = followNext(p, 1, s, n);
    TEST_ASSERT_TRUE(nx == 2 || nx == 3);
    jumps += nx == 3;
  }
  TEST_ASSERT_INT_WITHIN(100, 500, jumps);
  setFol(p, 3, FollowAct::Stay, 0);  // failed chance on the last slot: leaves the song
  TEST_ASSERT_EQUAL(kFollowEnd, followNext(p, 3, s, 0));
}

void test_random_never_current_covers_others() {
  Project& p = *proj(5);
  const uint32_t s = followRunSeed(3, 0);
  setFol(p, 2, FollowAct::Random);
  std::set<int> seen;
  for (uint32_t n = 0; n < 200; ++n) {
    const int nx = followNext(p, 2, s, n);
    TEST_ASSERT_TRUE(nx >= 0 && nx < 5);
    TEST_ASSERT_NOT_EQUAL(2, nx);
    seen.insert(nx);
  }
  TEST_ASSERT_EQUAL(4, static_cast<int>(seen.size()));
  Project& one = *proj(1);
  setFol(one, 0, FollowAct::Random);
  TEST_ASSERT_EQUAL(0, followNext(one, 0, s, 0));  // one slot: itself
}

void test_deterministic_and_seed_dependent() {
  Project& p = *proj(8);
  for (int i = 0; i < 8; ++i) setFol(p, i, FollowAct::Random);
  TEST_ASSERT_NOT_EQUAL(followRunSeed(1, 0), followRunSeed(1, 1));
  TEST_ASSERT_NOT_EQUAL(followRunSeed(1, 0), followRunSeed(2, 0));
  int diff = 0;
  for (uint32_t n = 0; n < 64; ++n) {
    TEST_ASSERT_EQUAL(followNext(p, 3, followRunSeed(1, 0), n), followNext(p, 3, followRunSeed(1, 0), n));
    diff += followNext(p, 3, followRunSeed(1, 0), n) != followNext(p, 3, followRunSeed(2, 0), n);
  }
  TEST_ASSERT_TRUE(diff > 20);
}

void test_unknown_action_is_next() {
  Project& p = *proj(3);
  p.songFol[0].act = 200;
  TEST_ASSERT_EQUAL(1, followNext(p, 0, followRunSeed(1, 0), 0));
}

void test_names() {
  TEST_ASSERT_EQUAL_STRING("NEXT", followName(0));
  TEST_ASSERT_EQUAL_STRING("PREV", followName(1));
  TEST_ASSERT_EQUAL_STRING("STAY", followName(2));
  TEST_ASSERT_EQUAL_STRING("JUMP", followName(3));
  TEST_ASSERT_EQUAL_STRING("RAND", followName(4));
  TEST_ASSERT_EQUAL_STRING("NEXT", followName(99));
}

static std::vector<int> walk(const Project& p, uint32_t salt, int maxSlots) {
  std::vector<int> v;
  const int n = followWalk(p, followRunSeed(p.seed, salt), maxSlots, [&](int s) { v.push_back(s); });
  TEST_ASSERT_EQUAL(static_cast<int>(v.size()), n);
  return v;
}

void test_walk_linear_song_once() {
  Project& p = *proj(4);
  TEST_ASSERT_TRUE(walk(p, 0, kSongMax) == std::vector<int>({0, 1, 2, 3}));
  TEST_ASSERT_TRUE(walk(p, 0, 2) == std::vector<int>({0, 1}));  // the slot limit
  TEST_ASSERT_TRUE(walk(*proj(0), 0, kSongMax).empty());
}

void test_walk_stay_and_jump_loop_stop_at_limit() {
  Project& p = *proj(3);
  setFol(p, 1, FollowAct::Jump, 100, 0);  // 0 1 0 1 ...
  TEST_ASSERT_TRUE(walk(p, 0, 5) == std::vector<int>({0, 1, 0, 1, 0}));
  Project& q = *proj(2);
  setFol(q, 0, FollowAct::Stay);
  TEST_ASSERT_TRUE(walk(q, 0, 3) == std::vector<int>({0, 0, 0}));
}

void test_walk_same_seed_same_path() {
  Project& p = *proj(6);
  for (int i = 0; i < 6; ++i) setFol(p, i, FollowAct::Random, 70);
  TEST_ASSERT_TRUE(walk(p, 0, 40) == walk(p, 0, 40));
  p.seed = 2;
  const std::vector<int> b = walk(p, 0, 40);
  p.seed = 1;
  TEST_ASSERT_FALSE(walk(p, 0, 40) == b);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_next_default_and_end);
  RUN_TEST(test_prev_stay_jump);
  RUN_TEST(test_chance_zero_is_next_and_fifty_is_about_half);
  RUN_TEST(test_random_never_current_covers_others);
  RUN_TEST(test_deterministic_and_seed_dependent);
  RUN_TEST(test_unknown_action_is_next);
  RUN_TEST(test_names);
  RUN_TEST(test_walk_linear_song_once);
  RUN_TEST(test_walk_stay_and_jump_loop_stop_at_limit);
  RUN_TEST(test_walk_same_seed_same_path);
  return UNITY_END();
}
