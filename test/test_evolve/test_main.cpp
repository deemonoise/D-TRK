#include <string.h>
#include <unity.h>
#include "evolve.h"
#include "mut.h"

using namespace mt;

static Project proj;  // big: static storage

void setUp() { proj.reset(); }
void tearDown() {}

void test_cfg_defaults() {
  const EvoCfg c;
  TEST_ASSERT_EQUAL(0, c.amount);
  TEST_ASSERT_EQUAL(4, c.every);
  TEST_ASSERT_EQUAL(25, c.home);
  TEST_ASSERT_EQUAL(kEvoNote | kEvoRhythm | kEvoRtrgVol, c.mask);
  proj.evo[3].amount = 70;
  proj.reset();
  TEST_ASSERT_EQUAL(0, proj.evo[3].amount);
  TEST_ASSERT_FALSE(proj.evoOn(3));
  proj.evo[3].amount = 1;
  TEST_ASSERT_FALSE(proj.evoOn(3));  // only in MUT mode EVO
  proj.var[3].mode = static_cast<uint8_t>(VarMode::Evo);
  TEST_ASSERT_TRUE(proj.evoOn(3));
  TEST_ASSERT_FALSE(proj.evoOn(-1));
  TEST_ASSERT_FALSE(proj.evoOn(kTracks));
}

void test_len_follows_track_len() {
  Pattern& pt = proj.patterns[0];
  pt.length = 16;
  TEST_ASSERT_EQUAL(16, evoLen(pt, 0));
  pt.trackLen[0] = 5;
  TEST_ASSERT_EQUAL(5, evoLen(pt, 0));
  pt.trackLen[0] = 20;  // past the length: the length
  TEST_ASSERT_EQUAL(16, evoLen(pt, 0));
  TEST_ASSERT_EQUAL(0, evoLen(pt, -1));
  TEST_ASSERT_EQUAL(0, evoLen(pt, kTracks));
}

void test_reset_idle_changed() {
  static EvoTrack s;
  evoReset(s);
  TEST_ASSERT_TRUE(evoIdle(s));
  TEST_ASSERT_EQUAL(0, s.n);
  s.step[3].deg = 1;
  s.step[100].flags = kEvoDrop;
  TEST_ASSERT_FALSE(evoIdle(s));
  TEST_ASSERT_EQUAL(1, evoChanged(s, 16));  // step 100 is past the length
  TEST_ASSERT_EQUAL(2, evoChanged(s, 128));
  s.n = 7;
  s.rot = -2;
  evoClearDeltas(s);
  TEST_ASSERT_TRUE(evoIdle(s));
  TEST_ASSERT_EQUAL(7, s.n);  // the mutation count goes on
  s.rot = 1;
  TEST_ASSERT_FALSE(evoIdle(s));
}

void test_rand_deterministic_and_keyed() {
  const uint32_t a = evoRand(1234, 2, 5, 7, 3);
  TEST_ASSERT_EQUAL_HEX32(a, evoRand(1234, 2, 5, 7, 3));
  TEST_ASSERT_NOT_EQUAL(a, evoRand(1235, 2, 5, 7, 3));
  TEST_ASSERT_NOT_EQUAL(a, evoRand(1234, 3, 5, 7, 3));
  TEST_ASSERT_NOT_EQUAL(a, evoRand(1234, 2, 6, 7, 3));
  TEST_ASSERT_NOT_EQUAL(a, evoRand(1234, 2, 5, 8, 3));
  TEST_ASSERT_NOT_EQUAL(a, evoRand(1234, 2, 5, 7, 4));
}

void test_clamp() {
  EvoCfg c{200, 0, 255, 0xFF};
  evoClamp(c);
  TEST_ASSERT_EQUAL(100, c.amount);
  TEST_ASSERT_EQUAL(1, c.every);
  TEST_ASSERT_EQUAL(100, c.home);
  TEST_ASSERT_EQUAL(kEvoMaskAll, c.mask);
  c.every = 40;
  evoClamp(c);
  TEST_ASSERT_EQUAL(kEvoEveryMax, c.every);
}

void test_source_identity_rotation_swap() {
  static EvoTrack s;
  evoReset(s);
  for (int i = 0; i < 8; ++i) TEST_ASSERT_EQUAL(i, evoSource(s, i, 8, kEvoMaskAll));
  s.rot = 1;  // the pattern one step later
  TEST_ASSERT_EQUAL(7, evoSource(s, 0, 8, kEvoMaskAll));
  TEST_ASSERT_EQUAL(0, evoSource(s, 1, 8, kEvoMaskAll));
  s.rot = -1;
  TEST_ASSERT_EQUAL(1, evoSource(s, 0, 8, kEvoMaskAll));
  TEST_ASSERT_EQUAL(0, evoSource(s, 7, 8, kEvoMaskAll));
  s.rot = 0;
  s.step[2].flags = kEvoSwapNext;
  TEST_ASSERT_EQUAL(3, evoSource(s, 2, 8, kEvoMaskAll));
  TEST_ASSERT_EQUAL(2, evoSource(s, 3, 8, kEvoMaskAll));
  TEST_ASSERT_EQUAL(4, evoSource(s, 4, 8, kEvoMaskAll));
  TEST_ASSERT_EQUAL(3, evoSource(s, 3, 8, kEvoNote));  // SHIFT off: as is
  s.step[7].flags = kEvoSwapNext;  // the last step has no next: ignored
  TEST_ASSERT_EQUAL(7, evoSource(s, 7, 8, kEvoMaskAll));
  s.rot = 1;  // swap first, then the rotation
  TEST_ASSERT_EQUAL(2, evoSource(s, 2, 8, kEvoMaskAll));
}

void test_view_kinds() {
  Pattern& pt = proj.patterns[0];
  pt.length = 4;
  pt.steps[1][0].note = 60;
  pt.steps[1][1].note = kNoteOff;
  pt.steps[1][2].fx[0] = {Fx::CCA, 10};  // no note: empty for RHYTHM
  static EvoTrack s;
  evoReset(s);
  static EvoView v;
  evoMakeView(pt, 1, s, kEvoMaskAll, true, ScaleType::Major, v);
  TEST_ASSERT_EQUAL(4, v.len);
  TEST_ASSERT_EQUAL(7, v.degs);
  TEST_ASSERT_TRUE(v.noteOk);
  TEST_ASSERT_EQUAL(kEvoKNote, v.kind[0]);
  TEST_ASSERT_EQUAL(kEvoKOther, v.kind[1]);
  TEST_ASSERT_EQUAL(kEvoKEmpty, v.kind[2]);
  TEST_ASSERT_EQUAL(kEvoKEmpty, v.kind[3]);
  s.rot = 1;  // the view sees the steps where they play now
  evoMakeView(pt, 1, s, kEvoMaskAll, false, ScaleType::Chromatic, v);
  TEST_ASSERT_EQUAL(12, v.degs);
  TEST_ASSERT_EQUAL(kEvoKEmpty, v.kind[0]);
  TEST_ASSERT_EQUAL(kEvoKNote, v.kind[1]);
}

static EvoApply melodic(uint8_t mask = kEvoMaskAll) {
  EvoApply a;
  a.mask = mask;
  a.defVel = 100;
  a.root = 0;
  a.scale = ScaleType::Major;
  return a;
}

void test_step_idle_is_the_pattern_step() {
  Pattern& pt = proj.patterns[0];
  pt.steps[0][0].note = 60;
  static EvoTrack s;
  evoReset(s);
  Step tmp;
  TEST_ASSERT_EQUAL_PTR(&pt.steps[0][0], &evoStep(pt, 0, 0, s, melodic(), tmp));
  s.step[0].deg = 1;
  TEST_ASSERT_EQUAL_PTR(&pt.steps[0][0], &evoStep(pt, 0, 0, s, melodic(0), tmp));  // mask 0: off
}

void test_step_drop_and_ghost() {
  Pattern& pt = proj.patterns[0];
  pt.length = 4;
  pt.steps[0][0] = Step{};
  pt.steps[0][0].note = 64;
  pt.steps[0][0].vel = 90;
  pt.steps[0][0].fx[0] = {Fx::CHD, 0};
  pt.steps[0][2].note = 67;
  pt.steps[0][3].note = kNoteOff;
  static EvoTrack s;
  evoReset(s);
  Step tmp;
  s.step[0].flags = kEvoDrop;
  const Step& d = evoStep(pt, 0, 0, s, melodic(), tmp);
  TEST_ASSERT_EQUAL(kNoteEmpty, d.note);
  TEST_ASSERT_EQUAL(Fx::CHD, d.fx[0].cmd);  // fx stay
  TEST_ASSERT_EQUAL(64, evoStep(pt, 0, 0, s, melodic(kEvoNote), tmp).note);  // RHYTHM off
  s.step[1].flags = kEvoGhost;  // previous: step 0's note (its own drop does not matter)
  const Step& g = evoStep(pt, 0, 1, s, melodic(), tmp);
  TEST_ASSERT_EQUAL(64, g.note);
  TEST_ASSERT_EQUAL(40, g.vel);
  TEST_ASSERT_EQUAL(Fx::None, g.fx[0].cmd);  // the neighbour's CHD is not copied
  s.step[1].flags = kEvoGhost | kEvoGhostNext;
  TEST_ASSERT_EQUAL(67, evoStep(pt, 0, 1, s, melodic(), tmp).note);
  s.step[3].flags = kEvoGhost;  // OFF is not empty: no ghost
  TEST_ASSERT_EQUAL(kNoteOff, evoStep(pt, 0, 3, s, melodic(), tmp).note);
  s.step[2].flags = kEvoGhost;  // a step with a note: no ghost
  TEST_ASSERT_EQUAL(67, evoStep(pt, 0, 2, s, melodic(), tmp).note);
}

void test_step_drum_ghost_and_vol() {
  Pattern& pt = proj.patterns[0];
  pt.length = 4;
  pt.steps[0][0].note = 0;    // velocity 0 = the track's
  pt.steps[0][0].vel = 0x05;  // lanes 1 and 3
  static EvoTrack s;
  evoReset(s);
  EvoApply a = melodic();
  a.drum = true;
  a.noteOk = false;
  Step tmp;
  s.step[1].flags = kEvoGhost;
  const Step& g = evoStep(pt, 0, 1, s, a, tmp);
  TEST_ASSERT_EQUAL(40, g.note);   // the step velocity
  TEST_ASSERT_EQUAL(0x05, g.vel);  // the lane mask
  s.step[0].vol = -20;
  s.step[0].deg = 3;  // NOTE: never on a drum track
  const Step& v = evoStep(pt, 0, 0, s, a, tmp);
  TEST_ASSERT_EQUAL(80, v.note);
  TEST_ASSERT_EQUAL(0x05, v.vel);
}

void test_step_degrees_vol_rtrg() {
  Pattern& pt = proj.patterns[0];
  pt.length = 4;
  pt.steps[0][0].note = 60;  // C, C major
  static EvoTrack s;
  evoReset(s);
  Step tmp;
  s.step[0].deg = 1;
  TEST_ASSERT_EQUAL(62, evoStep(pt, 0, 0, s, melodic(), tmp).note);
  s.step[0].deg = -7;
  TEST_ASSERT_EQUAL(48, evoStep(pt, 0, 0, s, melodic(), tmp).note);
  EvoApply gen = melodic();
  gen.noteOk = false;  // a GEN track
  TEST_ASSERT_EQUAL(60, evoStep(pt, 0, 0, s, gen, tmp).note);
  s.step[0].deg = 0;
  s.step[0].vol = 40;
  TEST_ASSERT_EQUAL(127, evoStep(pt, 0, 0, s, melodic(), tmp).vel);  // 100 + 40, clamped
  s.step[0].vol = 0;
  s.step[0].flags = static_cast<uint8_t>(2 << kEvoRtrgShift);
  const Step& r = evoStep(pt, 0, 0, s, melodic(), tmp);
  const FxSlot* rat = r.find(Fx::RAT);
  TEST_ASSERT_NOT_NULL(rat);
  TEST_ASSERT_EQUAL(3, rat->val);
  pt.steps[0][0].fx[2] = {Fx::RAT, 0x14};  // its own RAT wins
  TEST_ASSERT_EQUAL(0x14, evoStep(pt, 0, 0, s, melodic(), tmp).find(Fx::RAT)->val);
  for (FxSlot& f : pt.steps[0][0].fx) f = {Fx::CCA, 1};  // no free slot
  TEST_ASSERT_NULL(evoStep(pt, 0, 0, s, melodic(), tmp).find(Fx::RAT));
}

void test_step_shift_reads_the_source() {
  Pattern& pt = proj.patterns[0];
  pt.length = 4;
  for (int i = 0; i < 4; ++i) pt.steps[0][i].note = static_cast<uint8_t>(60 + i);
  static EvoTrack s;
  evoReset(s);
  s.rot = 1;
  Step tmp;
  TEST_ASSERT_EQUAL(63, evoStep(pt, 0, 0, s, melodic(), tmp).note);
  TEST_ASSERT_EQUAL(60, evoStep(pt, 0, 1, s, melodic(), tmp).note);
  TEST_ASSERT_EQUAL(60, evoStep(pt, 0, 0, s, melodic(kEvoNote), tmp).note);  // SHIFT off
}

// What the track plays with: drum / GEN tracks without NOTE, GEN GATE also without RHYTHM.
void test_apply_of_track_kinds() {
  proj.evo[2].mask = kEvoMaskAll;
  proj.tracks[2].defVel = 77;
  proj.scaleRoot = 2;
  proj.scaleType = static_cast<uint8_t>(ScaleType::Minor);
  EvoApply a = evoApplyOf(proj, 2);
  TEST_ASSERT_EQUAL(kEvoMaskAll, a.mask);
  TEST_ASSERT_FALSE(a.drum);
  TEST_ASSERT_TRUE(a.noteOk);
  TEST_ASSERT_EQUAL(77, a.defVel);
  TEST_ASSERT_EQUAL(2, a.root);
  TEST_ASSERT_EQUAL(static_cast<int>(ScaleType::Minor), static_cast<int>(a.scale));
  proj.gen[2].mode = static_cast<uint8_t>(GenMode::Pitch);
  a = evoApplyOf(proj, 2);
  TEST_ASSERT_FALSE(a.noteOk);
  TEST_ASSERT_EQUAL(kEvoMaskAll, a.mask);
  proj.gen[2].mode = static_cast<uint8_t>(GenMode::Gate);
  a = evoApplyOf(proj, 2);
  TEST_ASSERT_FALSE(a.noteOk);
  TEST_ASSERT_EQUAL(kEvoMaskAll & ~kEvoRhythm, a.mask);
  TEST_ASSERT_FALSE(evoRhythmOk(proj, 2));
  TEST_ASSERT_TRUE(evoRhythmOk(proj, 3));
  proj.gen[2].mode = 0;
  instrSetType(proj.instruments[proj.tracks[2].instr], InstrType::Kit);
  a = evoApplyOf(proj, 2);
  TEST_ASSERT_TRUE(a.drum);
  TEST_ASSERT_FALSE(a.noteOk);
  TEST_ASSERT_EQUAL(kEvoMaskAll, a.mask);
}

static EvoView noteView(int len, uint8_t degs = 7) {
  EvoView v;
  v.len = static_cast<uint8_t>(len);
  v.degs = degs;
  for (int i = 0; i < len; ++i) v.kind[i] = kEvoKNote;
  return v;
}

void test_mutate_counts_and_amount0_home0_keeps() {
  static EvoTrack s, before;
  evoReset(s);
  s.step[2].deg = 3;
  s.step[5].flags = kEvoDrop;
  s.rot = 2;
  before = s;
  const EvoCfg c{0, 1, 0, kEvoMaskAll};
  const EvoView v = noteView(16);
  evoMutate(s, c, v, 1234, 0);
  TEST_ASSERT_EQUAL(1, s.n);
  TEST_ASSERT_EQUAL_MEMORY(before.step, s.step, sizeof(s.step));
  TEST_ASSERT_EQUAL(2, s.rot);
}

void test_home_100_steps_back() {
  static EvoTrack s;
  evoReset(s);
  s.step[2].deg = 3;
  s.step[3].deg = -1;
  s.step[4].vol = 20;
  s.step[6].vol = -5;
  s.step[5].flags = kEvoDrop | (1 << kEvoRtrgShift);
  s.step[7].flags = kEvoSwapNext;
  s.rot = -2;
  const EvoCfg c{0, 1, 100, kEvoMaskAll};
  evoMutate(s, c, noteView(16), 99, 3);
  TEST_ASSERT_EQUAL(2, s.step[2].deg);
  TEST_ASSERT_EQUAL(0, s.step[3].deg);
  TEST_ASSERT_EQUAL(12, s.step[4].vol);
  TEST_ASSERT_EQUAL(0, s.step[6].vol);
  TEST_ASSERT_EQUAL(0, s.step[5].flags);
  TEST_ASSERT_EQUAL(0, s.step[7].flags);
  TEST_ASSERT_EQUAL(-1, s.rot);
  for (int k = 0; k < 4; ++k) evoMutate(s, c, noteView(16), 99, 3);
  TEST_ASSERT_TRUE(evoIdle(s));
}

void test_home_past_len_untouched() {
  static EvoTrack s;
  evoReset(s);
  s.step[20].deg = 2;  // past the track length: kept for later
  evoMutate(s, EvoCfg{0, 1, 100, kEvoMaskAll}, noteView(16), 1, 0);
  TEST_ASSERT_EQUAL(2, s.step[20].deg);
}

// Runs k mutations from the reset state.
static void runMut(EvoTrack& s, const EvoCfg& c, const EvoView& v, int k, uint32_t seed = 1234, int tr = 0) {
  evoReset(s);
  for (int i = 0; i < k; ++i) evoMutate(s, c, v, seed, tr);
}

void test_mutate_deterministic_and_seeded() {
  static EvoTrack x, y;
  const EvoCfg c{100, 1, 10, kEvoMaskAll};
  const EvoView v = noteView(16);
  runMut(x, c, v, 20);
  runMut(y, c, v, 20);
  TEST_ASSERT_EQUAL_MEMORY(&x, &y, sizeof(x));
  TEST_ASSERT_FALSE(evoIdle(x));
  runMut(y, c, v, 20, 4321);
  TEST_ASSERT_TRUE(memcmp(x.step, y.step, sizeof(x.step)) != 0);
  runMut(y, c, v, 20, 1234, 1);  // another track drifts its own way
  TEST_ASSERT_TRUE(memcmp(x.step, y.step, sizeof(x.step)) != 0);
}

void test_mask_note_only() {
  static EvoTrack s;
  runMut(s, EvoCfg{100, 1, 0, kEvoNote}, noteView(16), 50);
  bool any = false;
  for (int i = 0; i < kMaxSteps; ++i) {
    TEST_ASSERT_EQUAL(0, s.step[i].vol);
    TEST_ASSERT_EQUAL(0, s.step[i].flags);
    TEST_ASSERT_TRUE(s.step[i].deg >= -kEvoDegMax && s.step[i].deg <= kEvoDegMax);
    if (i >= 16) TEST_ASSERT_EQUAL(0, s.step[i].deg);
    any |= s.step[i].deg != 0;
  }
  TEST_ASSERT_TRUE(any);
  TEST_ASSERT_EQUAL(0, s.rot);
}

void test_note_off_when_not_allowed() {
  static EvoTrack s;
  EvoView v = noteView(16);
  v.noteOk = false;  // drum / GEN
  runMut(s, EvoCfg{100, 1, 0, kEvoNote}, v, 50);
  TEST_ASSERT_TRUE(evoIdle(s));
}

void test_rhythm_drop_on_notes_ghost_on_empty() {
  static EvoTrack s;
  EvoView v;
  v.len = 16;
  for (int i = 0; i < 16; ++i) v.kind[i] = i % 4 == 0 ? kEvoKNote : (i % 4 == 2 ? kEvoKOther : kEvoKEmpty);
  runMut(s, EvoCfg{100, 1, 0, kEvoRhythm}, v, 60);
  bool drop = false, ghost = false;
  for (int i = 0; i < 16; ++i) {
    const uint8_t f = s.step[i].flags;
    if (f & kEvoDrop) TEST_ASSERT_EQUAL(kEvoKNote, v.kind[i]);
    if (f & kEvoGhost) {
      TEST_ASSERT_EQUAL(kEvoKEmpty, v.kind[i]);
      // the neighbour it copies has a note (steps 1 mod 4: the previous, 3 mod 4: the next)
      TEST_ASSERT_EQUAL(kEvoKNote, v.kind[(f & kEvoGhostNext) ? (i + 1) % 16 : (i + 15) % 16]);
    }
    TEST_ASSERT_EQUAL(0, f & (kEvoRtrgBits | kEvoSwapNext));
    drop |= (f & kEvoDrop) != 0;
    ghost |= (f & kEvoGhost) != 0;
  }
  TEST_ASSERT_TRUE(drop);
  TEST_ASSERT_TRUE(ghost);
}

void test_rtrg_vol_only_on_notes_and_clamped() {
  static EvoTrack s;
  EvoView v = noteView(16);
  for (int i = 8; i < 16; ++i) v.kind[i] = kEvoKEmpty;
  runMut(s, EvoCfg{100, 1, 0, kEvoRtrgVol}, v, 200);
  bool rt = false, vol = false;
  for (int i = 0; i < 16; ++i) {
    TEST_ASSERT_TRUE(s.step[i].vol >= -kEvoVolMax && s.step[i].vol <= kEvoVolMax);
    const int x = (s.step[i].flags & kEvoRtrgBits) >> kEvoRtrgShift;
    TEST_ASSERT_TRUE(x <= 2);
    if (i >= 8) {
      TEST_ASSERT_EQUAL(0, s.step[i].vol);
      TEST_ASSERT_EQUAL(0, x);
    }
    rt |= x != 0;
    vol |= s.step[i].vol != 0;
  }
  TEST_ASSERT_TRUE(rt);
  TEST_ASSERT_TRUE(vol);
}

void test_shift_swaps_never_adjacent_rot_bounded() {
  static EvoTrack s;
  evoReset(s);
  const EvoCfg c{100, 1, 5, kEvoShift};
  const EvoView v = noteView(8);
  bool swapped = false, rotated = false;
  for (int k = 0; k < 300; ++k) {
    evoMutate(s, c, v, 77, 2);
    TEST_ASSERT_TRUE(s.rot >= -kEvoRotMax && s.rot <= kEvoRotMax && s.rot > -8 && s.rot < 8);
    TEST_ASSERT_EQUAL(0, s.step[7].flags & kEvoSwapNext);  // never at len - 1
    for (int i = 0; i + 1 < 8; ++i)
      TEST_ASSERT_FALSE((s.step[i].flags & kEvoSwapNext) && (s.step[i + 1].flags & kEvoSwapNext));
    for (int i = 0; i < 8; ++i) swapped |= (s.step[i].flags & kEvoSwapNext) != 0;
    rotated |= s.rot != 0;
  }
  TEST_ASSERT_TRUE(swapped);
  TEST_ASSERT_TRUE(rotated);
}

// Capture live (mut.h) in MUT EVO: writes the track's steps as the state plays them, past the track
// length and other tracks kept.
void test_capture_writes_what_plays() {
  Pattern& pt = proj.patterns[0];
  pt.length = 8;
  pt.trackLen[1] = 6;
  for (int i = 0; i < 8; ++i) pt.steps[1][i].note = static_cast<uint8_t>(60 + i);
  pt.steps[1][7].note = 99;  // past the track length: kept
  pt.steps[2][0].note = 50;  // another track: kept
  static EvoTrack s;
  evoReset(s);
  s.rot = 1;
  s.step[0].deg = 1;
  s.step[2].flags = kEvoDrop;
  s.step[3].flags = static_cast<uint8_t>(1 << kEvoRtrgShift);
  proj.var[1].mode = static_cast<uint8_t>(VarMode::Evo);
  proj.evo[1].mask = kEvoMaskAll;
  const EvoApply a = evoApplyOf(proj, 1);
  static Pattern expect;
  expect = pt;
  for (int i = 0; i < 6; ++i) {
    Step tmp;
    expect.steps[1][i] = evoStep(pt, 1, i, s, a, tmp);
  }
  static Step scratch[kMaxSteps];
  static VarTrack v;
  captureLive(proj, pt, 1, s, v, GenState(), 0, scratch);
  TEST_ASSERT_EQUAL_MEMORY(expect.steps[1], pt.steps[1], sizeof(pt.steps[1]));
  TEST_ASSERT_EQUAL(99, pt.steps[1][7].note);
  TEST_ASSERT_EQUAL(50, pt.steps[2][0].note);
  TEST_ASSERT_EQUAL(kNoteEmpty, pt.steps[1][2].note);
  TEST_ASSERT_NOT_NULL(pt.steps[1][3].find(Fx::RAT));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_cfg_defaults);
  RUN_TEST(test_len_follows_track_len);
  RUN_TEST(test_reset_idle_changed);
  RUN_TEST(test_rand_deterministic_and_keyed);
  RUN_TEST(test_clamp);
  RUN_TEST(test_source_identity_rotation_swap);
  RUN_TEST(test_view_kinds);
  RUN_TEST(test_step_idle_is_the_pattern_step);
  RUN_TEST(test_step_drop_and_ghost);
  RUN_TEST(test_step_drum_ghost_and_vol);
  RUN_TEST(test_step_degrees_vol_rtrg);
  RUN_TEST(test_step_shift_reads_the_source);
  RUN_TEST(test_apply_of_track_kinds);
  RUN_TEST(test_mutate_counts_and_amount0_home0_keeps);
  RUN_TEST(test_home_100_steps_back);
  RUN_TEST(test_home_past_len_untouched);
  RUN_TEST(test_mutate_deterministic_and_seeded);
  RUN_TEST(test_mask_note_only);
  RUN_TEST(test_note_off_when_not_allowed);
  RUN_TEST(test_rhythm_drop_on_notes_ghost_on_empty);
  RUN_TEST(test_rtrg_vol_only_on_notes_and_clamped);
  RUN_TEST(test_shift_swaps_never_adjacent_rot_bounded);
  RUN_TEST(test_capture_writes_what_plays);
  return UNITY_END();
}
