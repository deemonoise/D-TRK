#include <stdio.h>
#include <string.h>
#include <unity.h>
#include <vector>
#include "bytebeat_lib.h"
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

// Length lowered while playing: the bits left above the new mask must not leave the register at 0.
void test_shorter_length_never_zero() {
  const GenCfg c = cfg(8, 0);
  GenState s;
  s.reg = 0x0100;  // a 16-step register with only bit 8 set
  for (int i = 0; i < 32; ++i) {
    genShift(s, c, 0, 0);
    TEST_ASSERT_TRUE(s.reg != 0);
  }
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
  // BYTE fields: formula past the library -> 0, A / B / C <= 127, zoom <= 12; Byte modes kept.
  c.mode = static_cast<uint8_t>(GenMode::ByteGate);
  c.formula = 250;
  c.a = c.b = c.c = 200;
  c.zoom = 13;
  genClamp(c);
  TEST_ASSERT_EQUAL(static_cast<int>(GenMode::ByteGate), c.mode);
  TEST_ASSERT_EQUAL(0, c.formula);
  TEST_ASSERT_EQUAL(127, c.a);
  TEST_ASSERT_EQUAL(127, c.b);
  TEST_ASSERT_EQUAL(127, c.c);
  TEST_ASSERT_EQUAL(12, c.zoom);
  c.formula = static_cast<uint8_t>(byteFormulaCount() - 1);
  genClamp(c);
  TEST_ASSERT_EQUAL(byteFormulaCount() - 1, c.formula);
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

// GEN BYTE: a bytebeat formula in place of the register.
static GenCfg byteCfg(GenMode m, int formula, uint8_t zoom = 10) {
  GenCfg c;
  c.mode = static_cast<uint8_t>(m);
  c.formula = static_cast<uint8_t>(formula);
  const ByteFormula& f = byteFormula(formula);
  c.a = f.a;
  c.b = f.b;
  c.c = f.c;
  c.zoom = zoom;
  return c;
}

// The step window: W = 1 << zoom ticks per clock, 8 points (W < 8: tick by tick), output bytes.
void test_byte_window_samples_the_step() {
  const GenCfg c = byteCfg(GenMode::BytePitch, 0, 10);
  GenState s;
  s.n = 3;
  uint8_t v[8];
  TEST_ASSERT_EQUAL(8, genByteBytes(s, c, v));
  for (int k = 0; k < 8; ++k)
    TEST_ASSERT_EQUAL(byteEval(byteCode(0), 3u * 1024u + k * 128u + (k * 37u) % 128u, c.a, c.b, c.c) & 255, v[k]);
  const GenCfg z1 = byteCfg(GenMode::BytePitch, 0, 1);
  TEST_ASSERT_EQUAL(8, genByteBytes(s, z1, v));
  for (int k = 0; k < 8; ++k) TEST_ASSERT_EQUAL(byteEval(byteCode(0), 6u + k, c.a, c.b, c.c) & 255, v[k]);
}

// Gate level: twice the mean distance of the samples from their mean, 0..255; silence is 0.
void test_byte_level() {
  for (int f = 0; f < byteFormulaCount(); ++f) {
    const GenCfg c = byteCfg(GenMode::ByteGate, f, 10);
    GenState s;
    s.n = 7;
    uint8_t v[8];
    genByteBytes(s, c, v);
    int x[8], mean = 0, dev = 0;
    for (int k = 0; k < 8; ++k) mean += x[k] = byteFormula(f).mode == kByteSigned ? static_cast<int8_t>(v[k]) : v[k];
    mean /= 8;
    for (int k = 0; k < 8; ++k) dev += x[k] > mean ? x[k] - mean : mean - x[k];
    TEST_ASSERT_EQUAL(dev / 4 > 255 ? 255 : dev / 4, genByteLevel(s, c));
  }
  GenCfg kick = byteCfg(GenMode::ByteGate, 0);
  for (int f = 0; f < byteFormulaCount(); ++f)
    if (!strcmp(byteFormula(f).name, "KICK")) kick = byteCfg(GenMode::ByteGate, f);
  GenState s;
  s.n = 1;  // KICK: the second step of 8 is silent
  TEST_ASSERT_EQUAL(0, genByteLevel(s, kick));
}

void test_byte_notes_in_scale_and_range() {
  for (int f = 0; f < byteFormulaCount(); ++f) {
    GenCfg c = byteCfg(GenMode::BytePitch, f);
    c.low = 50;  // raised to D (C major)
    c.oct = 2;
    GenState s;
    for (uint32_t n = 0; n < 200; ++n) {
      s.n = n;
      const uint8_t m = genNote(s, c, 0, ScaleType::Major);
      TEST_ASSERT_TRUE(inScale(m, 0, ScaleType::Major));
      TEST_ASSERT_TRUE(m >= 50 && m <= 50 + 24);
    }
  }
  GenCfg hi = byteCfg(GenMode::BytePitch, 1);
  hi.low = 127;
  hi.oct = 4;
  GenState s;
  for (uint32_t n = 0; n < 100; ++n) {
    s.n = n;
    TEST_ASSERT_TRUE(genNote(s, hi, 0, ScaleType::Chromatic) <= 127);
  }
}

// Byte modes: the note depends on n only (not the register, not flip).
void test_byte_deterministic() {
  GenCfg c = byteCfg(GenMode::BytePitch, 2);
  GenState a, b;
  a.reg = 0x55;
  b.reg = 0xAA;
  a.n = b.n = 37;
  TEST_ASSERT_EQUAL(genNote(a, c, 0, ScaleType::Minor), genNote(b, c, 0, ScaleType::Minor));
  c.flip = 100;
  GenState s;
  genReset(s, c, 0);
  genShift(s, c, 0, 100);
  TEST_ASSERT_EQUAL(0, s.flipped);  // no flips in byte modes
  TEST_ASSERT_EQUAL_UINT32(1, s.n);
}

void test_byte_gate_monotonic_in_density() {
  GenCfg c = byteCfg(GenMode::ByteGate, 3);
  GenState s;
  for (uint32_t n = 0; n < 128; ++n) {
    s.n = n;
    c.density = 0;
    TEST_ASSERT_FALSE(genGate(s, c));
    c.density = 100;
    TEST_ASSERT_TRUE(genGate(s, c));
    bool was = false;
    for (int d = 0; d <= 100; ++d) {
      c.density = static_cast<uint8_t>(d);
      const bool g = genGate(s, c);
      TEST_ASSERT_TRUE(!was || g);
      was = g;
    }
  }
  // Half density: some formulas gate, none of them plays nothing at all.
  int some = 0;
  for (int f = 0; f < byteFormulaCount(); ++f) {
    GenCfg g = byteCfg(GenMode::ByteGate, f);
    g.density = 50;
    int hits = 0;
    for (uint32_t n = 0; n < 256; ++n) {
      s.n = n;
      hits += genGate(s, g);
    }
    some += hits > 0 && hits < 256;
  }
  TEST_ASSERT_TRUE(some * 2 > byteFormulaCount());
}

void test_gate_mode_helpers() {
  TEST_ASSERT_TRUE(genGateMode(static_cast<uint8_t>(GenMode::Gate)));
  TEST_ASSERT_TRUE(genGateMode(static_cast<uint8_t>(GenMode::ByteGate)));
  TEST_ASSERT_FALSE(genGateMode(static_cast<uint8_t>(GenMode::Pitch)));
  TEST_ASSERT_FALSE(genGateMode(static_cast<uint8_t>(GenMode::BytePitch)));
  TEST_ASSERT_TRUE(genByteMode(static_cast<uint8_t>(GenMode::BytePitch)));
  TEST_ASSERT_TRUE(genByteMode(static_cast<uint8_t>(GenMode::ByteGate)));
  TEST_ASSERT_FALSE(genByteMode(static_cast<uint8_t>(GenMode::Gate)));
}

// Formulas playing fewer than 2 degrees in 64 clocks at zoom z (default A B C, C major over 2 octaves).
static int stillAt(uint8_t z, bool print) {
  int failed = 0;
  for (int f = 0; f < byteFormulaCount(); ++f) {
    const GenCfg c = byteCfg(GenMode::BytePitch, f, z);
    bool seen[128] = {};
    int k = 0;
    GenState s;
    for (uint32_t n = 1; n <= 64; ++n) {
      s.n = n;
      const uint8_t m = genNote(s, c, 0, ScaleType::Major);
      if (!seen[m]) ++k;
      seen[m] = true;
    }
    if (k >= 2) continue;
    ++failed;
    if (print) printf("GEN BYTE zoom %u: %s still\n", z, byteFormula(f).name);
  }
  return failed;
}

// Every library formula at the default zoom moves (>= 2 degrees in 64 clocks); at most 5 may not (kept in
// the library, see the design). Still at zoom 10: SGNPULSE. Still per zoom 0..12: 16 12 11 7 4 2 0 1 1 1
// 1 2 6 (of 45).
void test_byte_every_formula_moves() {
  for (uint8_t z = 0; z <= kGenZoomMax; ++z) printf("GEN BYTE zoom %u: %d still\n", z, stillAt(z, false));
  TEST_ASSERT_TRUE(stillAt(GenCfg().zoom, true) <= 5);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults);
  RUN_TEST(test_reset_never_zero_and_depends_on_seed_and_track);
  RUN_TEST(test_shorter_length_never_zero);
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
  RUN_TEST(test_byte_window_samples_the_step);
  RUN_TEST(test_byte_level);
  RUN_TEST(test_byte_notes_in_scale_and_range);
  RUN_TEST(test_byte_deterministic);
  RUN_TEST(test_byte_gate_monotonic_in_density);
  RUN_TEST(test_gate_mode_helpers);
  RUN_TEST(test_byte_every_formula_moves);
  return UNITY_END();
}
