#include <string.h>
#include <unity.h>
#include "scene.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_scene_size_and_defaults() {
  TEST_ASSERT_EQUAL(240, static_cast<int>(sizeof(Scene)));
  Project* p = new Project();
  TEST_ASSERT_EQUAL(0, p->sceneA);
  TEST_ASSERT_EQUAL(1, p->sceneB);
  TEST_ASSERT_EQUAL(0, p->xfade);
  for (const Scene& s : p->scenes) TEST_ASSERT_TRUE(sceneEmpty(s));
  TEST_ASSERT_TRUE(sceneEmpty(p->sceneLive[0]) && sceneEmpty(p->sceneLive[1]));
  sceneSetTrk(p->scenes[3], 2, kScnVol, 10);
  p->sceneA = 3;
  p->xfade = 99;
  p->reset();
  TEST_ASSERT_TRUE(sceneEmpty(p->scenes[3]));
  TEST_ASSERT_EQUAL(0, p->sceneA);
  TEST_ASSERT_EQUAL(0, p->xfade);
  delete p;
}

void test_mix_unset_is_base_bit_exact() {
  for (int b = 0; b <= 200; b += 7)
    for (int x = 0; x <= 127; ++x) {
      TEST_ASSERT_EQUAL(b, sceneMix(static_cast<uint8_t>(b), false, 3, false, 99, static_cast<float>(x), false));
      TEST_ASSERT_EQUAL(b, sceneMix(static_cast<uint8_t>(b), false, 3, false, 99, static_cast<float>(x), true));
    }
}

void test_mix_only_a_returns_to_base() {
  TEST_ASSERT_EQUAL(10, sceneMix(100, true, 10, false, 0, 0.f, false));
  TEST_ASSERT_EQUAL(100, sceneMix(100, true, 10, false, 0, 127.f, false));
  TEST_ASSERT_EQUAL(30, sceneMix(10, false, 0, true, 30, 127.f, false));
}

void test_mix_midpoint_and_rounding() {
  TEST_ASSERT_EQUAL(64, sceneMix(0, true, 0, true, 127, 64.f, false));
  TEST_ASSERT_EQUAL(1, sceneMix(0, true, 0, true, 127, 1.f, false));
  TEST_ASSERT_EQUAL(1, sceneMix(0, true, 0, true, 2, 32.f, false));   // 0.504 -> 1
  TEST_ASSERT_EQUAL(0, sceneMix(0, true, 0, true, 2, 31.f, false));   // 0.488 -> 0
  TEST_ASSERT_EQUAL(200, sceneMix(0, true, 0, true, 200, 127.f, false));
  TEST_ASSERT_EQUAL(127, sceneMix(0, true, 127, true, 0, 0.f, false));
  TEST_ASSERT_EQUAL(0, sceneMix(0, true, 127, true, 0, 127.f, false));
}

void test_mix_discrete_switches_at_64() {
  TEST_ASSERT_EQUAL(1, sceneMix(0, true, 1, true, 4, 63.f, true));
  TEST_ASSERT_EQUAL(1, sceneMix(0, true, 1, true, 4, 63.9f, true));
  TEST_ASSERT_EQUAL(4, sceneMix(0, true, 1, true, 4, 64.f, true));
  TEST_ASSERT_TRUE(sceneTrkDiscrete(kScnDty));
  TEST_ASSERT_FALSE(sceneTrkDiscrete(kScnVol));
  TEST_ASSERT_TRUE(sceneMstDiscrete(kScnDTime));
  TEST_ASSERT_FALSE(sceneMstDiscrete(kScnDFb));
}

void test_base_reads_project_fields() {
  Project* p = new Project();
  TrackCfg& t = p->tracks[5];
  t.vol = 11, t.pan = 12, t.drive = 13, t.driveModel = 2, t.crushBits = 14, t.crushRate = 15, t.gateDepth = 16,
  t.modSend = 17, t.instr = 7;
  Instrument& m = p->instruments[7];
  m.send = 18, m.rsend = 19, m.cutoff = 20, m.reso = 21;
  const uint8_t want[kScnTrk] = {11, 12, 13, 2, 14, 15, 16, 18, 19, 17, 20, 21};
  for (int k = 0; k < kScnTrk; ++k) TEST_ASSERT_EQUAL_MESSAGE(want[k], sceneTrkBase(*p, 5, k), sceneTrkName(k));
  p->masterVol = 150, p->dlyTime = 5, p->dlyFb = 1, p->dlyTone = 2, p->dlyChar = 3, p->dlyLevel = 4, p->rvbSize = 5,
  p->rvbDamp = 6, p->rvbLevel = 7, p->modRate = 8, p->modDepth = 9, p->modLevel = 10, p->eqLow = 11, p->eqHigh = 12;
  const uint8_t wm[kScnMst] = {150, 5, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  for (int k = 0; k < kScnMst; ++k) TEST_ASSERT_EQUAL_MESSAGE(wm[k], sceneMstBase(*p, k), sceneMstName(k));
  delete p;
}

void test_effective_values_use_live_scenes() {
  Project* p = new Project();
  p->tracks[0].vol = 100;
  p->masterVol = 100;
  sceneSetTrk(p->scenes[2], 0, kScnVol, 0);
  sceneSetTrk(p->scenes[5], 0, kScnVol, 120);
  sceneSetMst(p->scenes[5], kScnMain, 200);
  p->sceneA = 2;
  p->sceneB = 5;
  TEST_ASSERT_EQUAL(100, sceneTrkEff(*p, 0, kScnVol));  // not synced yet
  syncSceneLive(*p);
  TEST_ASSERT_EQUAL(0, memcmp(&p->sceneLive[0], &p->scenes[2], sizeof(Scene)));
  TEST_ASSERT_EQUAL(0, memcmp(&p->sceneLive[1], &p->scenes[5], sizeof(Scene)));
  TEST_ASSERT_EQUAL(0, sceneTrkEff(*p, 0, kScnVol));
  TEST_ASSERT_EQUAL(100, sceneMstEff(*p, kScnMain));
  TEST_ASSERT_TRUE(sceneTrkLive(*p, 0, kScnVol));
  TEST_ASSERT_FALSE(sceneTrkLive(*p, 0, kScnPan));
  TEST_ASSERT_FALSE(sceneTrkLive(*p, 1, kScnVol));
  p->xfade = 127;
  TEST_ASSERT_EQUAL(120, sceneTrkEff(*p, 0, kScnVol));
  TEST_ASSERT_EQUAL(200, sceneMstEff(*p, kScnMain));
  TEST_ASSERT_EQUAL(100, sceneTrkEff(*p, 0, kScnPan) + 36);  // pan 64: base
  // Indices out of range are clamped by the sync.
  p->sceneA = 200;
  syncSceneLive(*p);
  TEST_ASSERT_TRUE(p->sceneA < kScenes);
  delete p;
}

void test_high_mask_bits_ignored() {
  Project* p = new Project();
  p->tracks[0].vol = 90;
  p->scenes[0].trkMask[0] = 0xF000;  // bits >= kScnTrk: nothing
  p->scenes[0].mstMask = 0xC000;
  syncSceneLive(*p);
  for (int k = 0; k < kScnTrk; ++k) TEST_ASSERT_FALSE(sceneTrkLive(*p, 0, k));
  TEST_ASSERT_EQUAL(90, sceneTrkEff(*p, 0, kScnVol));
  TEST_ASSERT_TRUE(sceneEmpty(p->scenes[0]));
  delete p;
}

void test_set_unset_clear_capture() {
  Project* p = new Project();
  Scene& s = p->scenes[1];
  sceneSetTrk(s, 3, kScnDty, 9);  // clamped to the last model
  TEST_ASSERT_TRUE(sceneTrkIsSet(s, 3, kScnDty));
  TEST_ASSERT_EQUAL(static_cast<int>(DriveModel::Count) - 1, s.trk[3][kScnDty]);
  sceneUnsetTrk(s, 3, kScnDty);
  TEST_ASSERT_FALSE(sceneTrkIsSet(s, 3, kScnDty));
  sceneSetMst(s, kScnDTime, 0);
  TEST_ASSERT_EQUAL(1, s.mst[kScnDTime]);
  TEST_ASSERT_TRUE(sceneMstIsSet(s, kScnDTime));
  sceneUnsetMst(s, kScnDTime);
  TEST_ASSERT_TRUE(sceneEmpty(s));
  p->tracks[0].vol = 33;
  p->tracks[0].pan = 10;
  p->tracks[1].out = TrackOut::Midi;
  sceneCaptureMix(*p, 1);
  TEST_ASSERT_TRUE(sceneTrkIsSet(s, 0, kScnVol) && sceneTrkIsSet(s, 0, kScnPan));
  TEST_ASSERT_EQUAL(33, s.trk[0][kScnVol]);
  TEST_ASSERT_EQUAL(10, s.trk[0][kScnPan]);
  TEST_ASSERT_FALSE(sceneTrkIsSet(s, 1, kScnVol));  // MIDI track: not captured
  TEST_ASSERT_FALSE(sceneTrkIsSet(s, 0, kScnDrv));
  // The live copy follows (scene 1 is B).
  TEST_ASSERT_EQUAL(0, memcmp(&p->sceneLive[1], &s, sizeof(Scene)));
  sceneClear(*p, 1);
  TEST_ASSERT_TRUE(sceneEmpty(s));
  TEST_ASSERT_TRUE(sceneEmpty(p->sceneLive[1]));
  delete p;
}

void test_clamps_and_ranges() {
  TEST_ASSERT_EQUAL(0, sceneClampTrk(kScnDty, 7));
  TEST_ASSERT_EQUAL(127, sceneClampTrk(kScnVol, 250));
  TEST_ASSERT_EQUAL(1, sceneClampMst(kScnDTime, 0));
  TEST_ASSERT_EQUAL(kDlyTimeMax, sceneClampMst(kScnDTime, 40));
  TEST_ASSERT_EQUAL(kMasterVolMax, sceneClampMst(kScnMain, 255));
  TEST_ASSERT_EQUAL(127, sceneClampMst(kScnEqHi, 200));
  TEST_ASSERT_EQUAL(kMasterVolMax, sceneMstMax(kScnMain));
  TEST_ASSERT_EQUAL(1, sceneMstMin(kScnDTime));
  TEST_ASSERT_EQUAL(static_cast<int>(DriveModel::Count) - 1, sceneTrkMax(kScnDty));
  TEST_ASSERT_EQUAL_STRING("VOL", sceneTrkName(kScnVol));
  TEST_ASSERT_EQUAL_STRING("RES", sceneTrkName(kScnRes));
  TEST_ASSERT_EQUAL_STRING("MAIN", sceneMstName(kScnMain));
  TEST_ASSERT_EQUAL_STRING("EQHI", sceneMstName(kScnEqHi));
  TEST_ASSERT_EQUAL_STRING("?", sceneTrkName(kScnTrk));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_scene_size_and_defaults);
  RUN_TEST(test_mix_unset_is_base_bit_exact);
  RUN_TEST(test_mix_only_a_returns_to_base);
  RUN_TEST(test_mix_midpoint_and_rounding);
  RUN_TEST(test_mix_discrete_switches_at_64);
  RUN_TEST(test_base_reads_project_fields);
  RUN_TEST(test_effective_values_use_live_scenes);
  RUN_TEST(test_high_mask_bits_ignored);
  RUN_TEST(test_set_unset_clear_capture);
  RUN_TEST(test_clamps_and_ranges);
  return UNITY_END();
}
