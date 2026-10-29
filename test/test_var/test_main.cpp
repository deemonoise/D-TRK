#include <string.h>
#include <unity.h>
#include "evolve.h"
#include "var_gen.h"

using namespace mt;

static Pattern pt;  // big: static storage

void setUp() {
  pt.clear();
  pt.length = 16;
}
void tearDown() {}

static void note(int tr, int i, uint8_t n, uint8_t vel = kVelDefault) {
  pt.steps[tr][i].note = n;
  pt.steps[tr][i].vel = vel;
}

static int countBits(const VarCells& c, int rows, int len) {
  int k = 0;
  for (int r = 0; r < rows; ++r)
    for (int i = 0; i < len; ++i) k += varBit(c.r[r], i);
  return k;
}

// The reference: Wolfram rule from its bits, circular.
static void refCa1d(const bool* in, bool* out, int len, uint8_t rule) {
  for (int i = 0; i < len; ++i) {
    const int idx = in[(i + len - 1) % len] << 2 | in[i] << 1 | in[(i + 1) % len];
    out[i] = (rule >> idx) & 1;
  }
}

void test_cfg_defaults_and_clamp() {
  VarCfg c;
  TEST_ASSERT_EQUAL(0, c.mode);
  TEST_ASSERT_EQUAL(2, c.every);
  TEST_ASSERT_EQUAL(90, c.rule);
  TEST_ASSERT_EQUAL(0, c.rule2);
  TEST_ASSERT_EQUAL(16, c.reset);
  TEST_ASSERT_EQUAL(0, c.noise);
  TEST_ASSERT_EQUAL(50, c.amount);
  TEST_ASSERT_EQUAL(1, c.order);
  TEST_ASSERT_EQUAL(0, c.rhythm);
  c.mode = 9;
  c.every = 0;
  c.rule2 = 7;
  c.reset = 200;
  c.noise = 101;
  c.amount = 255;
  c.order = 5;
  c.rhythm = 3;
  varClamp(c);
  TEST_ASSERT_EQUAL(0, c.mode);
  TEST_ASSERT_EQUAL(1, c.every);
  TEST_ASSERT_EQUAL(0, c.rule2);
  TEST_ASSERT_EQUAL(kVarResetMax, c.reset);
  TEST_ASSERT_EQUAL(100, c.noise);
  TEST_ASSERT_EQUAL(100, c.amount);
  TEST_ASSERT_EQUAL(2, c.order);
  TEST_ASSERT_EQUAL(1, c.rhythm);
  c.every = 40;
  c.order = 0;
  varClamp(c);
  TEST_ASSERT_EQUAL(kVarEveryMax, c.every);
  TEST_ASSERT_EQUAL(1, c.order);
}

void test_mode_of() {
  VarCfg c;
  c.mode = static_cast<uint8_t>(VarMode::Ca2d);
  TEST_ASSERT_EQUAL(VarMode::Ca2d, varModeOf(c, true));
  TEST_ASSERT_EQUAL(VarMode::Ca1d, varModeOf(c, false));  // melodic: CA1D
  c.mode = static_cast<uint8_t>(VarMode::Markov);
  TEST_ASSERT_EQUAL(VarMode::Markov, varModeOf(c, false));
  c.mode = 4;
  TEST_ASSERT_EQUAL(VarMode::Off, varModeOf(c, true));
}

void test_ca1d_rules_vs_reference() {
  const uint8_t rules[] = {90, 30, 110};
  const int lens[] = {16, 128, 13, 1, 2};
  for (uint8_t rule : rules)
    for (int len : lens) {
      bool cur[kMaxSteps] = {}, next[kMaxSteps];
      uint8_t row[kVarRowBytes] = {};
      cur[len / 2] = true;
      varSetBit(row, len / 2, true);
      for (int g = 0; g < 20; ++g) {
        refCa1d(cur, next, len, rule);
        varCa1d(row, len, rule);
        for (int i = 0; i < len; ++i) TEST_ASSERT_EQUAL(next[i], varBit(row, i));
        for (int i = len; i < kMaxSteps; ++i) TEST_ASSERT_FALSE(varBit(row, i));  // past len: dead
        memcpy(cur, next, sizeof(cur));
      }
    }
}

void test_ca1d_wraps() {
  uint8_t row[kVarRowBytes] = {};
  varSetBit(row, 0, true);
  varCa1d(row, 16, 90);  // 90: left xor right
  TEST_ASSERT_TRUE(varBit(row, 1));
  TEST_ASSERT_TRUE(varBit(row, 15));  // circular
  TEST_ASSERT_FALSE(varBit(row, 0));
  TEST_ASSERT_FALSE(varBit(row, 16));
}

void test_ca2d_life_blinker() {
  VarCells c = {};
  for (int i = 4; i <= 6; ++i) varSetBit(c.r[3], i, true);  // horizontal
  VarCells g0 = c;
  varCa2d(c, 16, static_cast<uint8_t>(VarRule2::Life));
  for (int r = 2; r <= 4; ++r) TEST_ASSERT_TRUE(varBit(c.r[r], 5));  // vertical
  TEST_ASSERT_EQUAL(3, countBits(c, kVarRows, 16));
  varCa2d(c, 16, static_cast<uint8_t>(VarRule2::Life));
  TEST_ASSERT_EQUAL_MEMORY(&g0, &c, sizeof(c));  // period 2
}

void test_ca2d_glider_moves_on_torus() {
  VarCells c = {};
  // .X.
  // ..X
  // XXX
  varSetBit(c.r[0], 1, true);
  varSetBit(c.r[1], 2, true);
  varSetBit(c.r[2], 0, true);
  varSetBit(c.r[2], 1, true);
  varSetBit(c.r[2], 2, true);
  const int len = 10;
  VarCells g0 = c;
  for (int g = 0; g < 4; ++g) varCa2d(c, len, static_cast<uint8_t>(VarRule2::Life));
  // 4 generations: one row down, one step right.
  for (int r = 0; r < kVarRows; ++r)
    for (int i = 0; i < len; ++i)
      TEST_ASSERT_EQUAL(varBit(g0.r[(r + kVarRows - 1) % kVarRows], (i + len - 1) % len), varBit(c.r[r], i));
  // Around the torus: 16 rows down = back to the same rows, 16 steps right (6 on len 10); 64
  // generations. (8 KIT rows before 16 lanes: 32 generations, 8 steps right.)
  for (int g = 0; g < 60; ++g) varCa2d(c, len, static_cast<uint8_t>(VarRule2::Life));
  for (int r = 0; r < kVarRows; ++r)
    for (int i = 0; i < len; ++i) TEST_ASSERT_EQUAL(varBit(g0.r[r], (i + 2 * len - 16) % len), varBit(c.r[r], i));
  TEST_ASSERT_EQUAL(5, countBits(c, kVarRows, len));
}

// KIT CA2D has 16 rows (lanes 1-16): a glider crosses lane 8 -> 9 whole and wraps 16 -> 1.
void test_ca2d_sixteen_rows() {
  TEST_ASSERT_EQUAL(16, kVarRows);
  VarCells c = {};
  varSetBit(c.r[5], 1, true);  // the glider of test_ca2d_glider_moves_on_torus, rows 5..7
  varSetBit(c.r[6], 2, true);
  varSetBit(c.r[7], 0, true);
  varSetBit(c.r[7], 1, true);
  varSetBit(c.r[7], 2, true);
  const int len = 32;
  for (int g = 0; g < 8; ++g) varCa2d(c, len, static_cast<uint8_t>(VarRule2::Life));
  // 2 rows down, 2 steps right: rows 7..9, across lanes 8 / 9.
  TEST_ASSERT_TRUE(varBit(c.r[7], 3));
  TEST_ASSERT_TRUE(varBit(c.r[8], 4));
  TEST_ASSERT_TRUE(varBit(c.r[9], 2));
  TEST_ASSERT_TRUE(varBit(c.r[9], 3));
  TEST_ASSERT_TRUE(varBit(c.r[9], 4));
  TEST_ASSERT_EQUAL(5, countBits(c, kVarRows, len));
  // 28 more: rows 14..16 -> 0 (wrap 16 -> 1), 9 steps right.
  for (int g = 0; g < 28; ++g) varCa2d(c, len, static_cast<uint8_t>(VarRule2::Life));
  TEST_ASSERT_TRUE(varBit(c.r[14], 10));
  TEST_ASSERT_TRUE(varBit(c.r[15], 11));
  TEST_ASSERT_TRUE(varBit(c.r[0], 9));
  TEST_ASSERT_TRUE(varBit(c.r[0], 10));
  TEST_ASSERT_TRUE(varBit(c.r[0], 11));
  TEST_ASSERT_EQUAL(5, countBits(c, kVarRows, len));
}

void test_ca2d_seeds_and_highlife() {
  VarCells c = {};
  varSetBit(c.r[2], 3, true);
  varSetBit(c.r[2], 4, true);
  varCa2d(c, 16, static_cast<uint8_t>(VarRule2::Seeds));
  TEST_ASSERT_FALSE(varBit(c.r[2], 3));  // every live cell dies
  TEST_ASSERT_FALSE(varBit(c.r[2], 4));
  TEST_ASSERT_TRUE(varBit(c.r[1], 3));  // 2 neighbours: birth
  TEST_ASSERT_TRUE(varBit(c.r[3], 4));
  // A dead cell with 6 neighbours: born in HighLife, not in Life.
  VarCells h = {};
  for (int i = 4; i <= 6; ++i) {
    varSetBit(h.r[2], i, true);
    varSetBit(h.r[4], i, true);
  }
  VarCells l = h;
  varCa2d(h, 16, static_cast<uint8_t>(VarRule2::HighLife));
  varCa2d(l, 16, static_cast<uint8_t>(VarRule2::Life));
  TEST_ASSERT_TRUE(varBit(h.r[3], 5));
  TEST_ASSERT_FALSE(varBit(l.r[3], 5));
}

// The reference: a Life-like rule from its B/S string, torus 8 x len, Moore neighbourhood.
static void refCa2d(const bool in[kVarRows][kMaxSteps], bool out[kVarRows][kMaxSteps], int len, const char* bs) {
  bool born[9] = {}, surv[9] = {};
  bool* set = born;
  for (const char* p = bs; *p; ++p) {
    if (*p == '/') set = surv;
    else if (*p >= '0' && *p <= '8') set[*p - '0'] = true;
  }
  for (int r = 0; r < kVarRows; ++r)
    for (int i = 0; i < len; ++i) {
      int k = 0;
      for (int dr = -1; dr <= 1; ++dr)
        for (int di = -1; di <= 1; ++di)
          if (dr || di) k += in[(r + dr + kVarRows) % kVarRows][(i + di + len) % len];
      out[r][i] = in[r][i] ? surv[k] : born[k];
    }
}

void test_ca2d_all_rules_vs_reference() {
  const char* bs[] = {"B3/S23", "B36/S23", "B2/S", "B3678/S34678", "B3/S12345"};
  static_assert(sizeof(bs) / sizeof(bs[0]) == static_cast<int>(VarRule2::Count), "a B/S per rule");
  const int lens[] = {1, 2, 3, 13, 16, 128};
  for (int rule = 0; rule < static_cast<int>(VarRule2::Count); ++rule)
    for (int len : lens) {
      static bool cur[kVarRows][kMaxSteps], next[kVarRows][kMaxSteps];
      memset(cur, 0, sizeof(cur));
      VarCells c = {};
      uint32_t x = 12345u + static_cast<uint32_t>(rule * 131 + len);
      for (int r = 0; r < kVarRows; ++r)
        for (int i = 0; i < len; ++i) {
          x = x * 1103515245u + 12345u;
          cur[r][i] = (x >> 16) % 100 < 40;
          varSetBit(c.r[r], i, cur[r][i]);
        }
      for (int g = 0; g < 8; ++g) {
        refCa2d(cur, next, len, bs[rule]);
        varCa2d(c, len, static_cast<uint8_t>(rule));
        for (int r = 0; r < kVarRows; ++r) {
          for (int i = 0; i < len; ++i) TEST_ASSERT_EQUAL(next[r][i], varBit(c.r[r], i));
          for (int i = len; i < kMaxSteps; ++i) TEST_ASSERT_FALSE(varBit(c.r[r], i));
        }
        memcpy(cur, next, sizeof(cur));
      }
    }
}

void test_gen0_melodic_and_kit() {
  note(0, 0, 60);
  note(0, 5, 62);
  pt.steps[0][7].note = kNoteOff;
  VarCells c;
  varGen0(pt, 0, false, c);
  TEST_ASSERT_EQUAL(2, countBits(c, kVarRows, kMaxSteps));
  TEST_ASSERT_TRUE(varBit(c.r[0], 0));
  TEST_ASSERT_TRUE(varBit(c.r[0], 5));
  note(1, 2, 0, 0x05);  // lanes 1, 3
  note(1, 9, 0, 0x80);
  varGen0(pt, 1, true, c);
  TEST_ASSERT_EQUAL(3, countBits(c, kVarRows, kMaxSteps));
  TEST_ASSERT_TRUE(varBit(c.r[0], 2));
  TEST_ASSERT_TRUE(varBit(c.r[2], 2));
  TEST_ASSERT_TRUE(varBit(c.r[7], 9));
  pt.steps[1][11].note = 0;
  setLaneMask(pt.steps[1][11], 0x8100);  // lanes 9, 16
  varGen0(pt, 1, true, c);
  TEST_ASSERT_EQUAL(5, countBits(c, kVarRows, kMaxSteps));
  TEST_ASSERT_TRUE(varBit(c.r[8], 11));
  TEST_ASSERT_TRUE(varBit(c.r[15], 11));
}

static VarCfg caCfg(uint8_t rule, uint8_t reset, uint8_t noise) {
  VarCfg c;
  c.mode = static_cast<uint8_t>(VarMode::Ca1d);
  c.rule = rule;
  c.reset = reset;
  c.noise = noise;
  return c;
}

void test_ca_next_reset_after_n() {
  VarCells g0 = {};
  varSetBit(g0.r[0], 3, true);
  VarTrack s;
  varReset(s);
  const VarCfg c = caCfg(90, 3, 0);
  for (int g = 1; g <= 2; ++g) {
    varCaNext(s, c, g0, 16, false, 1, 0);
    TEST_ASSERT_EQUAL(g, s.n);
    TEST_ASSERT_EQUAL(g, s.sinceReset);
  }
  VarCells e = g0;
  varCa1d(e.r[0], 16, 90);
  varCa1d(e.r[0], 16, 90);
  TEST_ASSERT_EQUAL_MEMORY(&e, &s.cells, sizeof(e));
  varCaNext(s, c, g0, 16, false, 1, 0);  // the 3rd: back to generation 0
  TEST_ASSERT_EQUAL(3, s.n);
  TEST_ASSERT_EQUAL(0, s.sinceReset);
  TEST_ASSERT_EQUAL_MEMORY(&g0, &s.cells, sizeof(g0));
  // reset 0: never (240 moves the cell right: never dead, never still).
  varReset(s);
  for (int g = 0; g < 100; ++g) varCaNext(s, caCfg(240, 0, 0), g0, 16, false, 1, 0);
  TEST_ASSERT_EQUAL(100, s.sinceReset);
}

void test_ca_next_back_on_death_and_stasis() {
  VarCells g0 = {};
  varSetBit(g0.r[0], 3, true);
  VarTrack s;
  varReset(s);
  varCaNext(s, caCfg(0, 0, 0), g0, 16, false, 1, 0);  // rule 0: all die
  TEST_ASSERT_EQUAL(1, s.n);
  TEST_ASSERT_EQUAL(0, s.sinceReset);
  varReset(s);
  varCaNext(s, caCfg(204, 0, 0), g0, 16, false, 1, 0);  // 204: identity, nothing changes
  TEST_ASSERT_EQUAL(0, s.sinceReset);
  varSetBit(g0.r[0], 4, true);  // a pair moving right (rule 240: left neighbour) never stalls
  varReset(s);
  for (int g = 0; g < 5; ++g) varCaNext(s, caCfg(240, 0, 0), g0, 16, false, 1, 0);
  TEST_ASSERT_EQUAL(5, s.sinceReset);
  TEST_ASSERT_TRUE(varBit(s.cells.r[0], 8));
  TEST_ASSERT_TRUE(varBit(s.cells.r[0], 9));
}

static int diffBits(const VarCells& a, const VarCells& b) {
  int k = 0;
  for (int r = 0; r < kVarRows; ++r)
    for (int i = 0; i < kMaxSteps; ++i) k += varBit(a.r[r], i) != varBit(b.r[r], i);
  return k;
}

void test_ca_noise() {
  VarCells g0 = {};
  varSetBit(g0.r[0], 1, true);
  varSetBit(g0.r[0], 6, true);
  varSetBit(g0.r[0], 11, true);
  VarTrack s, t;
  varReset(s);
  for (int g = 0; g < 10; ++g) varCaNext(s, caCfg(240, 0, 0), g0, 16, false, 5, 2);  // noise 0: no flips
  VarCells e = g0;
  for (int g = 0; g < 10; ++g) varCa1d(e.r[0], 16, 240);
  TEST_ASSERT_EQUAL_MEMORY(&e, &s.cells, sizeof(e));
  // noise 100 on identity: exactly one flip, within the track's row and length.
  bool moved = false;
  for (uint32_t seed = 1; seed < 30; ++seed) {
    varReset(s);
    varCaNext(s, caCfg(204, 0, 100), g0, 16, false, seed, 2);
    TEST_ASSERT_EQUAL(1, s.sinceReset);
    TEST_ASSERT_EQUAL(1, diffBits(g0, s.cells));
    TEST_ASSERT_EQUAL(countBits(s.cells, 1, 16), countBits(s.cells, kVarRows, kMaxSteps));
    varReset(t);
    varCaNext(t, caCfg(204, 0, 100), g0, 16, false, seed, 2);
    TEST_ASSERT_EQUAL_MEMORY(&s.cells, &t.cells, sizeof(s.cells));  // deterministic
    varReset(t);
    varCaNext(t, caCfg(204, 0, 100), g0, 16, false, seed, 3);
    if (memcmp(&s.cells, &t.cells, sizeof(s.cells))) moved = true;  // another track, another cell
  }
  TEST_ASSERT_TRUE(moved);
  // KIT: the flips reach other rows.
  bool other = false;
  for (uint32_t seed = 1; seed < 40; ++seed) {
    varReset(s);
    varCaNext(s, caCfg(204, 0, 100), g0, 16, true, seed, 2);
    TEST_ASSERT_EQUAL(1, diffBits(g0, s.cells));
    if (countBits(s.cells, 1, 16) == 3) other = true;
  }
  TEST_ASSERT_TRUE(other);
}

// KIT noise flips only rows of lanes in use (`lanes`) or holding generation 0's cells; none: lanes 1-8.
void test_ca_noise_used_lanes() {
  VarCells g0 = {};
  varSetBit(g0.r[0], 1, true);
  varSetBit(g0.r[0], 5, true);  // a flip never kills them all
  VarTrack s;
  bool hit0 = false, hit10 = false;
  for (uint32_t seed = 1; seed < 60; ++seed) {
    varReset(s);
    varCaNext(s, caCfg(204, 0, 100), g0, 16, true, seed, 2, 0x0400);
    TEST_ASSERT_EQUAL(1, diffBits(g0, s.cells));
    for (int r = 0; r < kVarRows; ++r) {
      const int n = countBits(s.cells, r + 1, 16) - countBits(s.cells, r, 16);
      if (r != 0 && r != 10) TEST_ASSERT_EQUAL(0, n);
    }
    hit0 |= countBits(s.cells, 1, 16) != 2;
    hit10 |= countBits(s.cells, 11, 16) != countBits(s.cells, 10, 16);
  }
  TEST_ASSERT_TRUE(hit0);
  TEST_ASSERT_TRUE(hit10);
  // No lane in use, no cells: rows 1-8 (as before 16 lanes).
  const VarCells none = {};
  bool hi = false;
  for (uint32_t seed = 1; seed < 60; ++seed) {
    varReset(s);
    varCaNext(s, caCfg(204, 0, 100), none, 16, true, seed, 2, 0);
    hi |= countBits(s.cells, kVarRows, 16) != countBits(s.cells, 8, 16);
  }
  TEST_ASSERT_FALSE(hi);
}

void test_ca_kit_1d_each_row() {
  VarCells g0 = {};
  varSetBit(g0.r[0], 2, true);
  varSetBit(g0.r[5], 9, true);
  VarTrack s;
  varReset(s);
  varCaNext(s, caCfg(240, 0, 0), g0, 16, true, 1, 0);
  TEST_ASSERT_TRUE(varBit(s.cells.r[0], 3));
  TEST_ASSERT_TRUE(varBit(s.cells.r[5], 10));
  TEST_ASSERT_EQUAL(2, countBits(s.cells, kVarRows, kMaxSteps));
  // melodic: row 0 only.
  varReset(s);
  varCaNext(s, caCfg(240, 0, 0), g0, 16, false, 1, 0);
  TEST_ASSERT_EQUAL(1, countBits(s.cells, kVarRows, kMaxSteps));
}

void test_cell_step_melodic() {
  note(0, 2, 64, 90);
  pt.steps[0][2].fx[0] = {Fx::RAT, 2};
  note(0, 6, 67);
  pt.steps[0][9].fx[0] = {Fx::RAT, 3};  // empty with an fx
  VarCells c = {};
  const int alive[] = {2, 4, 9, 0};
  for (int i : alive) varSetBit(c.r[0], i, true);
  Step tmp;
  TEST_ASSERT_EQUAL_PTR(&pt.steps[0][2], &varCellStep(pt, 0, 2, c, false, 0, tmp));  // alive + note
  const Step& a = varCellStep(pt, 0, 4, c, false, 0, tmp);  // alive + empty: step 2 whole
  TEST_ASSERT_EQUAL(64, a.note);
  TEST_ASSERT_EQUAL(90, a.vel);
  TEST_ASSERT_EQUAL(Fx::RAT, a.fx[0].cmd);
  TEST_ASSERT_EQUAL(67, varCellStep(pt, 0, 9, c, false, 0, tmp).note);  // nearest to the left: 6
  TEST_ASSERT_EQUAL(67, varCellStep(pt, 0, 0, c, false, 0, tmp).note);  // circular: 6
  const Step& d = varCellStep(pt, 0, 6, c, false, 0, tmp);  // dead: no note
  TEST_ASSERT_EQUAL(kNoteEmpty, d.note);
  TEST_ASSERT_EQUAL_PTR(&pt.steps[0][5], &varCellStep(pt, 0, 5, c, false, 0, tmp));  // dead + empty
  // No note on the track: the root.
  varSetBit(c.r[0], 3, true);
  const Step& r = varCellStep(pt, 1, 3, c, false, 14, tmp);
  TEST_ASSERT_EQUAL(62, r.note);  // D-4: root 14 % 12 = 2, octave 4 (60 = C-4)
  TEST_ASSERT_EQUAL(kVelDefault, r.vel);
  TEST_ASSERT_FALSE(r.hasFx());
}

void test_cell_step_kit() {
  note(1, 4, 0, 0x03);
  pt.steps[1][4].fx[0] = {Fx::RAT, 2};
  VarCells c = {};
  varSetBit(c.r[0], 4, true);
  varSetBit(c.r[1], 4, true);
  Step tmp;
  TEST_ASSERT_EQUAL_PTR(&pt.steps[1][4], &varCellStep(pt, 1, 4, c, true, 0, tmp));  // same lanes
  varSetBit(c.r[1], 4, false);
  varSetBit(c.r[6], 4, true);
  const Step& a = varCellStep(pt, 1, 4, c, true, 0, tmp);
  TEST_ASSERT_EQUAL(0, a.note);
  TEST_ASSERT_EQUAL(0x41, a.vel);
  varSetBit(c.r[2], 7, true);  // empty step: step 4's velocity / fx, its own lanes
  const Step& b = varCellStep(pt, 1, 7, c, true, 0, tmp);
  TEST_ASSERT_EQUAL(0, b.note);
  TEST_ASSERT_EQUAL(0x04, b.vel);
  TEST_ASSERT_EQUAL(Fx::RAT, b.fx[0].cmd);
  for (int r = 0; r < kVarRows; ++r) varSetBit(c.r[r], 4, false);
  TEST_ASSERT_FALSE(varCellStep(pt, 1, 4, c, true, 0, tmp).hasNote());  // dead
  varSetBit(c.r[3], 1, true);  // a track without notes
  const Step& e = varCellStep(pt, 2, 1, c, true, 5, tmp);
  TEST_ASSERT_EQUAL(0, e.note);  // the track's velocity
  TEST_ASSERT_EQUAL(0x08, e.vel);
}

// Rows 8..15 = lanes 9-16 (lanesHi); a step with those lanes and a stale one are handled whole.
void test_cell_step_kit_lanes_hi() {
  note(1, 4, 0, 0x01);
  pt.steps[1][4].lanesHi = 0x02;  // lanes 1, 10
  VarCells c = {};
  varSetBit(c.r[0], 4, true);
  varSetBit(c.r[9], 4, true);
  Step tmp;
  TEST_ASSERT_EQUAL_PTR(&pt.steps[1][4], &varCellStep(pt, 1, 4, c, true, 0, tmp));  // same lanes
  varSetBit(c.r[0], 4, false);
  varSetBit(c.r[15], 4, true);
  const Step& a = varCellStep(pt, 1, 4, c, true, 0, tmp);
  TEST_ASSERT_EQUAL_HEX16(0x8200, laneMask(a));
  varSetBit(c.r[12], 7, true);  // empty step: step 4's velocity, its own lanes
  const Step& b = varCellStep(pt, 1, 7, c, true, 0, tmp);
  TEST_ASSERT_EQUAL_HEX16(0x1000, laneMask(b));
  for (int r = 0; r < kVarRows; ++r) varSetBit(c.r[r], 4, false);
  const Step& d = varCellStep(pt, 1, 4, c, true, 0, tmp);  // dead: no lanes left
  TEST_ASSERT_FALSE(d.hasNote());
  TEST_ASSERT_EQUAL_HEX8(0, d.lanesHi);
}

void test_seq_tokens() {
  note(0, 0, 60);
  note(0, 3, 64);
  note(0, 5, 60);
  VarSeq q;
  varSeq(pt, 0, false, false, q);
  TEST_ASSERT_EQUAL(3, q.m);
  TEST_ASSERT_EQUAL(2, q.ntok);
  TEST_ASSERT_EQUAL(60, q.val[0]);
  TEST_ASSERT_EQUAL(64, q.val[1]);
  TEST_ASSERT_EQUAL(3, q.first[1]);
  TEST_ASSERT_EQUAL(0, q.tok[2]);
  TEST_ASSERT_EQUAL(5, q.pos[2]);
  varSeq(pt, 0, false, true, q);  // rhythm: every step, REST = ntok
  TEST_ASSERT_EQUAL(16, q.m);
  TEST_ASSERT_EQUAL(2, q.tok[1]);
  TEST_ASSERT_EQUAL(1, q.tok[3]);
  // > 16 distinct notes: the rest map to the nearest.
  pt.clear();
  pt.length = 32;
  for (int i = 0; i < 16; ++i) note(0, i, static_cast<uint8_t>(40 + 2 * i));
  note(0, 16, 47);  // between 46 and 48: the first nearest (46)
  note(0, 17, 100);  // the top: 70
  varSeq(pt, 0, false, false, q);
  TEST_ASSERT_EQUAL(16, q.ntok);
  TEST_ASSERT_EQUAL(18, q.m);
  TEST_ASSERT_EQUAL(3, q.tok[16]);
  TEST_ASSERT_EQUAL(15, q.tok[17]);
  // KIT: lane masks, nearest = fewest lanes apart.
  pt.clear();
  pt.length = 32;
  for (int i = 0; i < 16; ++i) note(1, i, 0, static_cast<uint8_t>(1 + i));
  note(1, 20, 0, 0x30);
  varSeq(pt, 1, true, false, q);
  TEST_ASSERT_EQUAL(16, q.ntok);
  TEST_ASSERT_EQUAL(15, q.tok[16]);  // 0x30 vs 0x10 (token 15): one lane
}

static VarCfg mkCfg(uint8_t amount, uint8_t order, uint8_t rhythm) {
  VarCfg c;
  c.mode = static_cast<uint8_t>(VarMode::Markov);
  c.amount = amount;
  c.order = order;
  c.rhythm = rhythm;
  return c;
}

void test_markov_amount0_is_pattern() {
  for (int i = 0; i < 16; i += 2) note(0, i, static_cast<uint8_t>(60 + (i * 5) % 12));
  uint8_t src[kMaxSteps];
  for (uint8_t r = 0; r < 2; ++r) {
    varMarkov(pt, 0, mkCfg(0, 1, r), false, 7, 3, src);
    for (int i = 0; i < 16; ++i) TEST_ASSERT_EQUAL(i, src[i]);
  }
}

static void playMarkov(const VarCfg& c, uint32_t seed, uint32_t n, int tr, bool kit, Step* out) {
  VarTrack s;
  varReset(s);
  varMarkov(pt, tr, c, kit, seed, n, s.src);
  s.sinceReset = 1;
  s.mode = static_cast<uint8_t>(VarMode::Markov);
  const VarApply a = varApply(c, kit, 0);
  for (int i = 0; i < evoLen(pt, tr); ++i) {
    Step tmp;
    out[i] = varStep(pt, tr, i, s, a, tmp);
  }
}

void test_markov_notes_from_pattern_deterministic() {
  const uint8_t ns[] = {60, 63, 67, 70, 72};
  for (int i = 0; i < 16; ++i)
    if (i % 3 != 2) note(0, i, ns[(i * 7) % 5], static_cast<uint8_t>(80 + i));
  bool changed = false;
  for (uint32_t seed = 1; seed < 20; ++seed)
    for (uint8_t r = 0; r < 2; ++r) {
      Step a[kMaxSteps], b[kMaxSteps];
      const VarCfg c = mkCfg(100, 1, r);
      playMarkov(c, seed, 4, 0, false, a);
      playMarkov(c, seed, 4, 0, false, b);
      TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof(Step) * 16);
      for (int i = 0; i < 16; ++i) {
        if (!a[i].hasNote()) continue;
        bool found = false;
        for (int j = 0; j < 16; ++j)
          if (pt.steps[0][j].hasNote() && pt.steps[0][j].note == a[i].note) {
            // the vol of the first step holding it (rhythm), else the step's own
            if (r && !found) TEST_ASSERT_TRUE(a[i].vel == pt.steps[0][j].vel || a[i].vel == pt.steps[0][i].vel);
            found = true;
          }
        TEST_ASSERT_TRUE(found);
        if (a[i].note != pt.steps[0][i].note) changed = true;
        if (!r) TEST_ASSERT_TRUE(pt.steps[0][i].hasNote());  // rhythm off: empty stays empty
      }
      if (!r)
        for (int i = 0; i < 16; ++i) {
          TEST_ASSERT_EQUAL(pt.steps[0][i].hasNote(), a[i].hasNote());
          if (a[i].hasNote()) TEST_ASSERT_EQUAL(pt.steps[0][i].vel, a[i].vel);  // pitch only
        }
    }
  TEST_ASSERT_TRUE(changed);
  // Another seed / generation / track: another sequence somewhere.
  Step a[kMaxSteps], b[kMaxSteps];
  bool differ = false;
  for (uint32_t n = 1; n < 10 && !differ; ++n) {
    playMarkov(mkCfg(100, 1, 1), 3, n, 0, false, a);
    playMarkov(mkCfg(100, 1, 1), 3, n + 1, 0, false, b);
    differ = memcmp(a, b, sizeof(Step) * 16) != 0;
  }
  TEST_ASSERT_TRUE(differ);
}

void test_markov_rhythm_rest() {
  note(0, 0, 60);
  note(0, 1, 62);
  note(0, 4, 64);
  bool rest = false, filled = false;
  for (uint32_t seed = 1; seed < 40; ++seed) {
    Step a[kMaxSteps];
    playMarkov(mkCfg(100, 1, 1), seed, 1, 0, false, a);
    for (int i = 0; i < 16; ++i) {
      if (pt.steps[0][i].hasNote() && !a[i].hasNote()) rest = true;
      if (!pt.steps[0][i].hasNote() && a[i].hasNote()) filled = true;
    }
  }
  TEST_ASSERT_TRUE(rest);
  TEST_ASSERT_TRUE(filled);
}

void test_markov_order2() {
  // A B A C, twice: order 2 knows (B, A) -> C and (C, A) -> B, so it plays the pattern.
  const uint8_t ns[] = {60, 62, 60, 64};
  for (int i = 0; i < 8; ++i) note(0, i, ns[i % 4]);
  pt.length = 8;
  bool o1differs = false;
  for (uint32_t seed = 1; seed < 30; ++seed) {
    uint8_t src[kMaxSteps];
    varMarkov(pt, 0, mkCfg(100, 2, 0), false, seed, 1, src);
    for (int i = 0; i < 8; ++i) TEST_ASSERT_EQUAL(i, src[i]);
    Step a[kMaxSteps];
    playMarkov(mkCfg(100, 1, 0), seed, 1, 0, false, a);
    for (int i = 0; i < 8; ++i)
      if (a[i].note != pt.steps[0][i].note) o1differs = true;
  }
  TEST_ASSERT_TRUE(o1differs);
  // A richer line: every order-2 output triple is in the pattern (circular).
  pt.clear();
  pt.length = 16;
  const uint8_t ln[] = {60, 62, 64, 62, 60, 67, 64, 60, 62, 60, 64, 67, 62, 64, 60, 67};
  for (int i = 0; i < 16; ++i) note(0, i, ln[i]);
  for (uint32_t seed = 1; seed < 30; ++seed) {
    Step a[kMaxSteps];
    playMarkov(mkCfg(100, 2, 0), seed, 2, 0, false, a);
    for (int i = 0; i + 2 < 16; ++i) {
      bool in = false;
      for (int j = 0; j < 16 && !in; ++j)
        in = ln[j] == a[i].note && ln[(j + 1) % 16] == a[i + 1].note && ln[(j + 2) % 16] == a[i + 2].note;
      TEST_ASSERT_TRUE(in);
    }
  }
}

void test_markov_dead_end_keeps_pattern() {
  // Order 2, amount 50: a step kept from the pattern after a chain step can make a pair the pattern never
  // has; the step after it is then the pattern's whatever the roll.
  const uint8_t ln[] = {60, 62, 64, 62, 60, 67, 64, 60, 62, 60, 64, 67, 62, 64, 60, 67};
  for (int i = 0; i < 16; ++i) note(0, i, ln[i]);
  auto seen = [&](uint8_t x, uint8_t y) {
    for (int j = 0; j < 16; ++j)
      if (ln[j] == x && ln[(j + 1) % 16] == y) return true;
    return false;
  };
  int ends = 0;
  for (uint32_t seed = 1; seed < 60; ++seed) {
    Step a[kMaxSteps];
    playMarkov(mkCfg(50, 2, 0), seed, 1, 0, false, a);
    for (int k = 2; k < 16; ++k)
      if (!seen(a[k - 2].note, a[k - 1].note)) {
        ++ends;
        TEST_ASSERT_EQUAL(ln[k], a[k].note);
      }
  }
  TEST_ASSERT_TRUE(ends > 0);
}

void test_markov_kit_masks() {
  note(1, 0, 0, 0x01);
  note(1, 2, 0, 0x06);
  note(1, 4, 90, 0x01);
  note(1, 6, 0, 0x80);
  pt.steps[1][6].fx[0] = {Fx::RAT, 2};
  bool changed = false;
  for (uint32_t seed = 1; seed < 30; ++seed) {
    Step a[kMaxSteps];
    playMarkov(mkCfg(100, 1, 0), seed, 1, 1, true, a);
    for (int i = 0; i < 16; ++i) {
      TEST_ASSERT_EQUAL(pt.steps[1][i].hasNote(), a[i].hasNote());
      if (!a[i].hasNote()) continue;
      TEST_ASSERT_TRUE(a[i].vel == 0x01 || a[i].vel == 0x06 || a[i].vel == 0x80);
      TEST_ASSERT_EQUAL(pt.steps[1][i].note, a[i].note);  // rhythm off: the mask only
      if (a[i].vel != pt.steps[1][i].vel) changed = true;
    }
  }
  TEST_ASSERT_TRUE(changed);
}

// KIT tokens are 16-bit masks: lane 9 (0x0100) and lane 1 (0x0001) are distinct; Markov plays
// lanesHi masks whole.
void test_markov_kit_hi_mask() {
  note(1, 0, 0, 0x01);
  note(1, 2, 0, 0x00);
  pt.steps[1][2].lanesHi = 0x01;  // lane 9
  note(1, 4, 0, 0x01);
  note(1, 6, 0, 0x00);
  pt.steps[1][6].lanesHi = 0x80;  // lane 16
  VarSeq q;
  varSeq(pt, 1, true, false, q);
  TEST_ASSERT_EQUAL(3, q.ntok);
  TEST_ASSERT_EQUAL_HEX16(0x0001, q.val[0]);
  TEST_ASSERT_EQUAL_HEX16(0x0100, q.val[1]);
  TEST_ASSERT_EQUAL_HEX16(0x8000, q.val[2]);
  bool changed = false;
  for (uint32_t seed = 1; seed < 30; ++seed) {
    Step a[kMaxSteps];
    playMarkov(mkCfg(100, 1, 0), seed, 1, 1, true, a);
    for (int i = 0; i < 16; ++i) {
      if (!a[i].hasNote()) continue;
      const uint16_t m = laneMask(a[i]);
      TEST_ASSERT_TRUE(m == 0x0001 || m == 0x0100 || m == 0x8000);
      if (m != laneMask(pt.steps[1][i])) changed = true;
    }
  }
  TEST_ASSERT_TRUE(changed);
  // > 16 masks: nearest by lanes over 16 bits (0x0300 is one lane from 0x0100).
  pt.clear();
  pt.length = 32;
  for (int i = 0; i < 16; ++i) {
    note(1, i, 0, 0);
    setLaneMask(pt.steps[1][i], static_cast<uint16_t>(1u << i));
  }
  note(1, 20, 0, 0);
  setLaneMask(pt.steps[1][20], 0x0300);
  varSeq(pt, 1, true, false, q);
  TEST_ASSERT_EQUAL(16, q.ntok);
  TEST_ASSERT_EQUAL(8, q.tok[16]);  // 0x0300 vs 0x0100 (token 8): one lane, the first nearest
}

void test_next_and_step_dispatch() {
  note(0, 0, 60);
  note(0, 4, 62);
  VarTrack s;
  varReset(s);
  VarCfg c;  // OFF
  varNext(s, c, pt, 0, false, 1);
  TEST_ASSERT_EQUAL(0, s.n);
  c.mode = static_cast<uint8_t>(VarMode::Ca2d);  // melodic: CA1D
  c.rule = 240;
  Step tmp;
  VarApply a = varApply(c, false, 0);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(VarMode::Ca1d), a.mode);
  TEST_ASSERT_EQUAL_PTR(&pt.steps[0][4], &varStep(pt, 0, 4, s, a, tmp));  // generation 0: the pattern
  varNext(s, c, pt, 0, false, 1);
  TEST_ASSERT_EQUAL(1, s.n);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(VarMode::Ca1d), s.mode);
  TEST_ASSERT_FALSE(varStep(pt, 0, 0, s, a, tmp).hasNote());  // moved right
  TEST_ASSERT_EQUAL(60, varStep(pt, 0, 1, s, a, tmp).note);
  TEST_ASSERT_EQUAL(62, varStep(pt, 0, 5, s, a, tmp).note);
  // Another mode in effect than the state's: the pattern.
  VarCfg m = mkCfg(100, 1, 0);
  const VarApply am = varApply(m, false, 0);
  TEST_ASSERT_EQUAL_PTR(&pt.steps[0][0], &varStep(pt, 0, 0, s, am, tmp));
  varNext(s, m, pt, 0, false, 1);
  TEST_ASSERT_EQUAL(2, s.n);
  TEST_ASSERT_EQUAL(1, s.sinceReset);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(VarMode::Markov), s.mode);
  // Past the track length: the pattern step.
  pt.trackLen[0] = 8;
  TEST_ASSERT_EQUAL_PTR(&pt.steps[0][9], &varStep(pt, 0, 9, s, am, tmp));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_cfg_defaults_and_clamp);
  RUN_TEST(test_mode_of);
  RUN_TEST(test_ca1d_rules_vs_reference);
  RUN_TEST(test_ca1d_wraps);
  RUN_TEST(test_ca2d_life_blinker);
  RUN_TEST(test_ca2d_glider_moves_on_torus);
  RUN_TEST(test_ca2d_sixteen_rows);
  RUN_TEST(test_ca2d_seeds_and_highlife);
  RUN_TEST(test_ca2d_all_rules_vs_reference);
  RUN_TEST(test_gen0_melodic_and_kit);
  RUN_TEST(test_ca_next_reset_after_n);
  RUN_TEST(test_ca_next_back_on_death_and_stasis);
  RUN_TEST(test_ca_noise);
  RUN_TEST(test_ca_noise_used_lanes);
  RUN_TEST(test_ca_kit_1d_each_row);
  RUN_TEST(test_cell_step_melodic);
  RUN_TEST(test_cell_step_kit);
  RUN_TEST(test_cell_step_kit_lanes_hi);
  RUN_TEST(test_seq_tokens);
  RUN_TEST(test_markov_amount0_is_pattern);
  RUN_TEST(test_markov_notes_from_pattern_deterministic);
  RUN_TEST(test_markov_rhythm_rest);
  RUN_TEST(test_markov_order2);
  RUN_TEST(test_markov_dead_end_keeps_pattern);
  RUN_TEST(test_markov_kit_masks);
  RUN_TEST(test_markov_kit_hi_mask);
  RUN_TEST(test_next_and_step_dispatch);
  return UNITY_END();
}
