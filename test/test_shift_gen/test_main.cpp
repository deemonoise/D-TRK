#include <unity.h>
#include <vector>
#include "model.h"
#include "scale.h"
#include "shift_gen.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static GenCfg cfg(uint8_t len, uint8_t flip, uint16_t seed = 7) {
  GenCfg c;
  c.mode = static_cast<uint8_t>(GenMode::Pitch);
  c.len = len;
  c.flip = flip;
  c.seed = seed;
  return c;
}

static std::vector<uint16_t> run(const GenCfg& c, int track, int n) {
  GenState s;
  genReset(s, c, track);
  std::vector<uint16_t> out;
  for (int i = 0; i < n; ++i) {
    genShift(s, c, track, c.flip);
    out.push_back(s.reg);
  }
  return out;
}

void test_defaults() {
  static Project p;
  p.reset();
  for (int t = 0; t < kTracks; ++t) {
    const GenCfg& g = p.gen[t];
    TEST_ASSERT_EQUAL(0, g.mode);
    TEST_ASSERT_EQUAL(8, g.len);
    TEST_ASSERT_EQUAL(10, g.flip);
    TEST_ASSERT_EQUAL(48, g.low);
    TEST_ASSERT_EQUAL(2, g.oct);
    TEST_ASSERT_EQUAL(50, g.density);
    TEST_ASSERT_EQUAL(t + 1, g.seed);
    TEST_ASSERT_FALSE(p.genOn(t));
  }
  TEST_ASSERT_EQUAL(8, sizeof(GenState));
}

void test_reset_never_zero_and_depends_on_seed_and_track() {
  int diffSeed = 0, diffTrack = 0;
  for (int len = kGenLenMin; len <= kGenLenMax; ++len)
    for (uint16_t seed = 0; seed < 200; ++seed) {
      GenCfg c = cfg(static_cast<uint8_t>(len), 0, seed);
      GenState a, b, d;
      genReset(a, c, 0);
      TEST_ASSERT_NOT_EQUAL(0, a.reg);
      TEST_ASSERT_EQUAL(0, a.reg & ~genMask(c.len));
      TEST_ASSERT_EQUAL(0, a.n);
      genReset(d, c, 1);
      c.seed = static_cast<uint16_t>(seed + 1);
      genReset(b, c, 0);
      diffSeed += a.reg != b.reg;
      diffTrack += a.reg != d.reg;
    }
  TEST_ASSERT_GREATER_THAN(15 * 150, diffSeed);
  TEST_ASSERT_GREATER_THAN(15 * 150, diffTrack);
}

void test_flip0_period_is_length() {
  for (int len = kGenLenMin; len <= kGenLenMax; ++len) {
    const GenCfg c = cfg(static_cast<uint8_t>(len), 0);
    const auto v = run(c, 3, len * 4);
    for (int i = len; i < len * 4; ++i) TEST_ASSERT_EQUAL(v[i - len], v[i]);
  }
}

void test_flip100_period_twice_length_inverted_half() {
  for (int len = kGenLenMin; len <= kGenLenMax; ++len) {
    const GenCfg c = cfg(static_cast<uint8_t>(len), 100);
    const auto v = run(c, 2, len * 6);
    const uint16_t m = genMask(c.len);
    for (int i = 2 * len; i < len * 6; ++i) TEST_ASSERT_EQUAL(v[i - 2 * len], v[i]);
    for (int i = len; i < len * 6; ++i) TEST_ASSERT_EQUAL(v[i - len] ^ m, v[i]);
  }
}

void test_same_seed_same_sequence() {
  const GenCfg a = cfg(12, 40, 99), b = cfg(12, 40, 100);
  TEST_ASSERT_TRUE(run(a, 0, 200) == run(a, 0, 200));
  TEST_ASSERT_FALSE(run(a, 0, 200) == run(b, 0, 200));
  TEST_ASSERT_FALSE(run(a, 0, 200) == run(a, 1, 200));
}

void test_flip_share_matches() {
  for (uint8_t flip : {10, 30, 50, 90}) {
    const GenCfg c = cfg(16, flip, 1234);
    GenState s;
    genReset(s, c, 5);
    int flips = 0;
    for (int i = 0; i < 10000; ++i) {
      genShift(s, c, 5, flip);
      flips += s.flipped;
    }
    TEST_ASSERT_INT_WITHIN(300, flip * 100, flips);
  }
}

void test_flip_argument_overrides_cfg() {
  const GenCfg c = cfg(8, 0);
  GenState s;
  genReset(s, c, 0);
  const uint16_t r = s.reg;
  genShift(s, c, 0, 100);
  TEST_ASSERT_EQUAL(1, s.flipped);
  TEST_ASSERT_EQUAL(((r << 1) | (((r >> 7) & 1) ^ 1)) & 0xFF, s.reg);
  TEST_ASSERT_EQUAL(1, s.n);
  genShift(s, c, 0, 0);
  TEST_ASSERT_EQUAL(0, s.flipped);
}

void test_notes_in_scale_and_range() {
  for (int sc = 0; sc < static_cast<int>(ScaleType::Count); ++sc)
    for (uint8_t oct = 1; oct <= kGenOctMax; ++oct) {
      GenCfg c = cfg(16, 50, 77);
      c.oct = oct;
      c.low = 49;  // C#3: raised to the scale (except chromatic)
      const ScaleType t = static_cast<ScaleType>(sc);
      int lo = c.low;
      while (!inScale(lo, 2, t)) ++lo;
      GenState s;
      genReset(s, c, 0);
      for (int i = 0; i < 300; ++i) {
        genShift(s, c, 0, c.flip);
        const int n = genNote(s, c, 2, t);
        TEST_ASSERT_TRUE(inScale(n, 2, t));
        TEST_ASSERT_TRUE(n >= lo && n <= lo + 12 * oct);
      }
      GenState z;
      z.reg = 0;
      TEST_ASSERT_EQUAL(lo, genNote(z, c, 2, t));
      GenState f;
      f.reg = 0xFFFF;
      TEST_ASSERT_EQUAL(lo + 12 * oct, genNote(f, c, 2, t));
    }
}

void test_chromatic_window_steps() {
  GenCfg c = cfg(2, 0);
  c.oct = 1;
  c.low = 60;
  GenState s;
  const int want[4] = {60, 64, 68, 72};  // 13 semitones over a 2-bit window
  for (int v = 0; v < 4; ++v) {
    s.reg = static_cast<uint16_t>(v);
    TEST_ASSERT_EQUAL(want[v], genNote(s, c, 0, ScaleType::Chromatic));
  }
}

void test_range_stays_below_128() {
  GenCfg c = cfg(8, 0);
  c.low = 120;
  c.oct = 4;
  GenState f;
  f.reg = 0xFF;
  const int top = genNote(f, c, 0, ScaleType::Major);
  TEST_ASSERT_TRUE(top <= 127);
  TEST_ASSERT_TRUE(inScale(top, 0, ScaleType::Major));
  f.reg = 0;
  TEST_ASSERT_EQUAL(top - 48, genNote(f, c, 0, ScaleType::Major));
}

void test_gate_density_monotonic() {
  GenCfg c = cfg(8, 0);
  for (uint16_t r = 0; r < 256; ++r) {
    GenState s;
    s.reg = r;
    c.density = 0;
    TEST_ASSERT_FALSE(genGate(s, c));
    c.density = 100;
    TEST_ASSERT_TRUE(genGate(s, c));
    bool prev = false;
    for (int d = 0; d <= 100; ++d) {
      c.density = static_cast<uint8_t>(d);
      const bool g = genGate(s, c);
      TEST_ASSERT_TRUE(!prev || g);
      prev = g;
    }
  }
  int on = 0;
  c.density = 50;
  for (uint16_t r = 0; r < 256; ++r) {
    GenState s;
    s.reg = r;
    on += genGate(s, c);
  }
  TEST_ASSERT_EQUAL(128, on);
}

void test_shorter_length_masks_high_bits() {
  GenCfg c = cfg(16, 0);
  GenState s;
  s.reg = 0xFFFF;
  c.len = 4;
  genShift(s, c, 0, 0);
  TEST_ASSERT_EQUAL(0x0F, s.reg);
}

void test_clamp() {
  GenCfg c;
  c.mode = 9;
  c.len = 1;
  c.flip = 200;
  c.low = 200;
  c.oct = 0;
  c.density = 101;
  genClamp(c);
  TEST_ASSERT_EQUAL(0, c.mode);
  TEST_ASSERT_EQUAL(kGenLenMin, c.len);
  TEST_ASSERT_EQUAL(100, c.flip);
  TEST_ASSERT_EQUAL(127, c.low);
  TEST_ASSERT_EQUAL(1, c.oct);
  TEST_ASSERT_EQUAL(100, c.density);
  c.len = 40;
  c.oct = 9;
  genClamp(c);
  TEST_ASSERT_EQUAL(kGenLenMax, c.len);
  TEST_ASSERT_EQUAL(kGenOctMax, c.oct);
}

void test_hold_cycle() {
  // INT with a generator: VOL -> PAN -> FLIP -> VOL.
  TEST_ASSERT_EQUAL(HoldMode::Vol, holdFirst(true, true));
  TEST_ASSERT_EQUAL(HoldMode::Pan, holdNext(HoldMode::Vol, true, true));
  TEST_ASSERT_EQUAL(HoldMode::Flip, holdNext(HoldMode::Pan, true, true));
  TEST_ASSERT_EQUAL(HoldMode::Vol, holdNext(HoldMode::Flip, true, true));
  // INT without: VOL <-> PAN.
  TEST_ASSERT_EQUAL(HoldMode::Pan, holdNext(HoldMode::Vol, true, false));
  TEST_ASSERT_EQUAL(HoldMode::Vol, holdNext(HoldMode::Pan, true, false));
  TEST_ASSERT_EQUAL(HoldMode::Vol, holdNext(HoldMode::Flip, true, false));
  // MIDI with a generator: FLIP only; without: VOL (shows MIDI).
  TEST_ASSERT_EQUAL(HoldMode::Flip, holdFirst(false, true));
  TEST_ASSERT_EQUAL(HoldMode::Flip, holdNext(HoldMode::Flip, false, true));
  TEST_ASSERT_EQUAL(HoldMode::Flip, holdNext(HoldMode::Vol, false, true));
  TEST_ASSERT_EQUAL(HoldMode::Vol, holdFirst(false, false));
  TEST_ASSERT_EQUAL(HoldMode::Vol, holdNext(HoldMode::Vol, false, false));
}

void test_flip_text() {
  char b[8];
  genFlipText(0, b);
  TEST_ASSERT_EQUAL_STRING("LOCK", b);
  genFlipText(30, b);
  TEST_ASSERT_EQUAL_STRING("30%", b);
  genFlipText(100, b);
  TEST_ASSERT_EQUAL_STRING("x2 INV", b);
}

void test_gen_on_skips_drum_tracks() {
  static Project p;
  p.reset();
  p.gen[0].mode = static_cast<uint8_t>(GenMode::Pitch);
  TEST_ASSERT_TRUE(p.genOn(0));
  instrSetType(p.instruments[p.tracks[0].instr], InstrType::Kit);
  TEST_ASSERT_FALSE(p.genOn(0));
  p.gen[1].mode = 9;
  TEST_ASSERT_FALSE(p.genOn(1));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults);
  RUN_TEST(test_reset_never_zero_and_depends_on_seed_and_track);
  RUN_TEST(test_flip0_period_is_length);
  RUN_TEST(test_flip100_period_twice_length_inverted_half);
  RUN_TEST(test_same_seed_same_sequence);
  RUN_TEST(test_flip_share_matches);
  RUN_TEST(test_flip_argument_overrides_cfg);
  RUN_TEST(test_notes_in_scale_and_range);
  RUN_TEST(test_chromatic_window_steps);
  RUN_TEST(test_range_stays_below_128);
  RUN_TEST(test_gate_density_monotonic);
  RUN_TEST(test_shorter_length_masks_high_bits);
  RUN_TEST(test_clamp);
  RUN_TEST(test_hold_cycle);
  RUN_TEST(test_flip_text);
  RUN_TEST(test_gen_on_skips_drum_tracks);
  return UNITY_END();
}
