#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "midi_import.h"
#include "model.h"

using namespace mt;

static Project P;
static SmfInfo I;
static SmfNote N[64];
static uint32_t nN;
static ImportMap M;

void setUp() {
  P.reset();
  I = SmfInfo();
  I.ppq = 96;
  I.firstTempoUsPerQ = 500000;
  I.sourceCount = 2;
  I.src[0].track = 1;
  I.src[0].channel = 0;
  I.src[1].track = 2;
  I.src[1].channel = 9;
  nN = 0;
  M = ImportMap();
  M.target[0] = 0;
}
void tearDown() {}

static void add(uint32_t tick, uint8_t note, uint32_t len = 12, uint8_t vel = 100, uint8_t src = 0) {
  N[nN++] = SmfNote{tick, len, note, vel, src};
}
static ImportResult run() { return importSmf(I, N, nN, M, P); }
static const Step& st(int pat, int trk, int step) { return P.patterns[pat].steps[trk][step]; }

void test_map_defaults() {
  ImportMap m;
  for (int i = 0; i < kSmfMaxSources; ++i) {
    TEST_ASSERT_EQUAL(-1, m.target[i]);
    TEST_ASSERT_EQUAL(0, m.transpose[i]);
  }
}

void test_quantize_nearest_step() {
  add(0, 60);
  add(25, 61);   // 1.04 -> step 1
  add(59, 62);   // 2.46 -> step 2
  add(61, 63);   // 2.54 -> step 3
  add(83, 64, 12, 77);  // 3.46 -> step 3, collides with 63: Highest keeps 64
  ImportResult r = run();
  TEST_ASSERT_EQUAL(1, r.patternsWritten);
  TEST_ASSERT_EQUAL(60, st(0, 0, 0).note);
  TEST_ASSERT_EQUAL(61, st(0, 0, 1).note);
  TEST_ASSERT_EQUAL(62, st(0, 0, 2).note);
  TEST_ASSERT_EQUAL(64, st(0, 0, 3).note);
  TEST_ASSERT_EQUAL(77, st(0, 0, 3).vel);
  TEST_ASSERT_EQUAL(kVelDefault, st(0, 0, 0).vel);  // == track defVel 100
  TEST_ASSERT_EQUAL_UINT32(1, r.notesDropped);
  TEST_ASSERT_EQUAL(16, P.patterns[0].length);
  TEST_ASSERT_EQUAL(Resolution::Sixteenth, P.patterns[0].res);
}

void test_triplet_step_length() {
  M.quant = Resolution::EighthTriplet;  // 96 * 32 / 96 = 32 ticks
  add(64, 60);
  run();
  TEST_ASSERT_EQUAL(60, st(0, 0, 2).note);
  TEST_ASSERT_EQUAL(Resolution::EighthTriplet, P.patterns[0].res);

  setUp();
  I.ppq = 480;
  M.quant = Resolution::SixteenthTriplet;  // 480 * 16 / 96 = 80 ticks
  add(240, 60, 40);
  add(80 * 100 + 39, 61, 40);  // exact fractions: no drift at step 100
  M.patternLen = 128;
  run();
  TEST_ASSERT_EQUAL(60, st(0, 0, 3).note);
  TEST_ASSERT_EQUAL(61, st(0, 0, 100).note);
}

void test_triplet_ppq_not_divisible() {
  I.ppq = 100;  // 1/16T = 100 * 16 / 96 = 16.67 ticks
  M.quant = Resolution::SixteenthTriplet;
  M.patternLen = 64;
  add(1000, 60, 16);  // 1000 / 16.667 = 60.0 exactly
  add(1008, 62, 16);  // 60.48 -> step 60 (collides, First keeps 60)
  M.mono = MonoMode::First;
  ImportResult r = run();
  TEST_ASSERT_EQUAL(60, st(0, 0, 60).note);
  TEST_ASSERT_EQUAL_UINT32(1, r.notesDropped);
}

void test_mono_modes() {
  const MonoMode modes[] = {MonoMode::Highest, MonoMode::Lowest, MonoMode::First};
  const uint8_t want[] = {64, 55, 60};
  for (int i = 0; i < 3; ++i) {
    setUp();
    add(0, 60);
    add(1, 64);
    add(2, 55);
    M.mono = modes[i];
    ImportResult r = run();
    TEST_ASSERT_EQUAL(want[i], st(0, 0, 0).note);
    TEST_ASSERT_EQUAL_UINT32(2, r.notesDropped);
  }
}

void test_transpose_and_range() {
  M.transpose[0] = 12;
  add(0, 60);
  add(24, 120);  // 132: out of range -> dropped
  ImportResult r = run();
  TEST_ASSERT_EQUAL(72, st(0, 0, 0).note);
  TEST_ASSERT_FALSE(st(0, 0, 1).hasNote());
  TEST_ASSERT_EQUAL_UINT32(1, r.notesDropped);

  setUp();
  M.transpose[0] = -24;
  add(0, 10);
  r = run();
  TEST_ASSERT_EQUAL_UINT32(1, r.notesDropped);
  TEST_ASSERT_EQUAL(0, r.patternsWritten);
}

void test_offset_bars() {
  M.offsetBars = 1;  // 4 * 96 = 384 ticks
  add(100, 50);      // before offset: ignored
  add(384, 60);
  add(384 + 48, 62);
  ImportResult r = run();
  TEST_ASSERT_EQUAL(60, st(0, 0, 0).note);
  TEST_ASSERT_EQUAL(62, st(0, 0, 2).note);
  TEST_ASSERT_EQUAL_UINT32(0, r.notesDropped);
}

void test_split_into_two_patterns() {
  M.firstPattern = 3;
  M.patternLen = 8;
  add(0, 60);
  add(24 * 10, 62);  // step 10 -> pattern 4 step 2
  ImportResult r = run();
  TEST_ASSERT_EQUAL(2, r.patternsWritten);
  TEST_ASSERT_EQUAL(60, st(3, 0, 0).note);
  TEST_ASSERT_EQUAL(62, st(4, 0, 2).note);
  TEST_ASSERT_EQUAL(8, P.patterns[3].length);
  TEST_ASSERT_EQUAL(8, P.patterns[4].length);
  TEST_ASSERT_EQUAL(16, P.patterns[5].length);
}

void test_limit_p16() {
  M.firstPattern = 15;
  add(0, 60);
  add(24 * 16, 62);  // would be P17
  add(24 * 40, 63);
  ImportResult r = run();
  TEST_ASSERT_EQUAL(1, r.patternsWritten);
  TEST_ASSERT_EQUAL(60, st(15, 0, 0).note);
  TEST_ASSERT_EQUAL_UINT32(2, r.notesDropped);
}

void test_clears_only_target_tracks() {
  P.patterns[0].steps[0][5].note = 40;
  P.patterns[0].steps[3][5].note = 41;
  P.patterns[1].steps[0][5].note = 42;  // pattern not written
  add(0, 60);
  run();
  TEST_ASSERT_TRUE(st(0, 0, 5).isEmpty());
  TEST_ASSERT_EQUAL(41, st(0, 3, 5).note);
  TEST_ASSERT_EQUAL(42, st(1, 0, 5).note);
}

void test_unmapped_source_ignored() {
  add(0, 60, 12, 100, 1);  // source 1 -> target -1
  ImportResult r = run();
  TEST_ASSERT_EQUAL(0, r.patternsWritten);
  TEST_ASSERT_EQUAL_UINT32(0, r.notesDropped);
  TEST_ASSERT_FALSE(st(0, 0, 0).hasNote());
}

void test_gate() {
  // track defGate 50 %, step = 24 ticks
  add(0, 60, 12);    // 50 %: none
  add(24, 60, 13);   // 54 %: within 10 %: none
  add(48, 60, 24);   // 100 % -> GAT 100
  add(72, 60, 48);   // 200 % -> nearest encoded 114 (198 %)
  add(96, 60, 4);    // 16.7 % -> 17
  run();
  TEST_ASSERT_NULL(st(0, 0, 0).find(Fx::GAT));
  TEST_ASSERT_NULL(st(0, 0, 1).find(Fx::GAT));
  TEST_ASSERT_NOT_NULL(st(0, 0, 2).find(Fx::GAT));
  TEST_ASSERT_EQUAL(100, st(0, 0, 2).find(Fx::GAT)->val);
  TEST_ASSERT_EQUAL(114, st(0, 0, 3).find(Fx::GAT)->val);
  TEST_ASSERT_EQUAL(17, st(0, 0, 4).find(Fx::GAT)->val);
}

void test_microtiming_ndg() {
  add(0, 60);
  add(24 + 1, 61);   // +4 %
  add(48 + 5, 62);   // +21 %
  add(96 - 1, 63);   // step 4, -4 %
  add(96 + 24 + 12, 64);  // exactly half: rounds to step 6 at -50 %
  run();  // keepMicrotiming off
  TEST_ASSERT_NULL(st(0, 0, 2).find(Fx::NDG));

  setUp();
  M.keepMicrotiming = true;
  add(0, 60);
  add(24 + 1, 61);
  add(48 + 5, 62);
  add(96 - 1, 63);
  add(144 + 12, 64);  // 6.5 -> step 7 (round half up), -50 %
  add(192 + 24 * 2 / 100, 65);  // +0: none
  run();
  TEST_ASSERT_NULL(st(0, 0, 0).find(Fx::NDG));
  TEST_ASSERT_EQUAL(4, fxSigned(st(0, 0, 1).find(Fx::NDG)->val));
  TEST_ASSERT_EQUAL(21, fxSigned(st(0, 0, 2).find(Fx::NDG)->val));
  TEST_ASSERT_EQUAL(-4, fxSigned(st(0, 0, 4).find(Fx::NDG)->val));
  TEST_ASSERT_EQUAL(-50, fxSigned(st(0, 0, 7).find(Fx::NDG)->val));
  TEST_ASSERT_NULL(st(0, 0, 8).find(Fx::NDG));
}

void test_ndg_below_threshold() {
  I.ppq = 960;  // step 240 ticks
  M.keepMicrotiming = true;
  add(5, 60, 120);   // 2.1 % -> none
  add(240 + 8, 61, 120);  // 3.3 % -> 3
  run();
  TEST_ASSERT_NULL(st(0, 0, 0).find(Fx::NDG));
  TEST_ASSERT_EQUAL(3, fxSigned(st(0, 0, 1).find(Fx::NDG)->val));
}

void test_source_channel_chn() {
  M.useSourceChannel = true;
  M.target[1] = 1;       // source 1 (ch 9) -> track 1 (ch 1)
  add(0, 60);            // source 0 ch 0 -> track 0 ch 0: no CHN
  add(0, 36, 12, 100, 1);
  run();
  TEST_ASSERT_NULL(st(0, 0, 0).find(Fx::CHN));
  TEST_ASSERT_EQUAL(10, st(0, 1, 0).find(Fx::CHN)->val);

  setUp();
  M.target[1] = 1;
  add(0, 36, 12, 100, 1);
  run();  // useSourceChannel off
  TEST_ASSERT_NULL(st(0, 1, 0).find(Fx::CHN));
}

// 12 sources (channels 1-12) with an in-order map (what the import dialog builds by default)
// fill tracks 0-11; the channel-12 note lands on track 11, which 8 tracks could not hold.
void test_twelve_channels_to_twelve_tracks() {
  I.sourceCount = 12;
  for (int s = 0; s < 12; ++s) {
    I.src[s].track = static_cast<uint8_t>(s + 1);
    I.src[s].channel = static_cast<uint8_t>(s);
    M.target[s] = static_cast<int8_t>(s);
    add(0, static_cast<uint8_t>(48 + s), 12, 100, static_cast<uint8_t>(s));
  }
  ImportResult r = run();
  TEST_ASSERT_EQUAL(0, r.notesDropped);
  for (int t = 0; t < 12; ++t) TEST_ASSERT_EQUAL(48 + t, st(0, t, 0).note);
  TEST_ASSERT_EQUAL(59, st(0, 11, 0).note);
  TEST_ASSERT_TRUE(st(0, 12, 0).isEmpty());
}

void test_fx_priority() {
  M.useSourceChannel = true;
  M.keepMicrotiming = true;
  M.target[1] = 0;
  add(5, 36, 48, 100, 1);  // CHN, GAT, NDG all apply
  run();
  const Step& s = st(0, 0, 0);
  TEST_ASSERT_EQUAL(Fx::CHN, s.fx[0].cmd);
  TEST_ASSERT_EQUAL(Fx::GAT, s.fx[1].cmd);

  setUp();
  M.keepMicrotiming = true;
  add(5, 36, 48);  // GAT, NDG
  run();
  TEST_ASSERT_EQUAL(Fx::GAT, st(0, 0, 0).fx[0].cmd);
  TEST_ASSERT_EQUAL(Fx::NDG, st(0, 0, 0).fx[1].cmd);
}

void test_mono_replacement_rewrites_fx() {
  M.keepMicrotiming = true;
  M.mono = MonoMode::Highest;
  add(5, 60, 12);   // NDG +21
  add(6, 64, 12);   // replaces, NDG +25
  run();
  TEST_ASSERT_EQUAL(64, st(0, 0, 0).note);
  TEST_ASSERT_EQUAL(25, fxSigned(st(0, 0, 0).find(Fx::NDG)->val));
  TEST_ASSERT_EQUAL(Fx::None, st(0, 0, 0).fx[1].cmd);
}

void test_tempo() {
  I.firstTempoUsPerQ = 600000;
  add(0, 60);
  ImportResult r = run();
  TEST_ASSERT_EQUAL(100, r.bpm);
  TEST_ASSERT_EQUAL(100, P.bpm);

  setUp();
  I.firstTempoUsPerQ = 100000;  // 600 bpm -> 300
  TEST_ASSERT_EQUAL(300, run().bpm);

  setUp();
  I.firstTempoUsPerQ = 4000000;  // 15 bpm -> 20
  TEST_ASSERT_EQUAL(20, run().bpm);

  setUp();
  I.firstTempoUsPerQ = 600000;
  M.useTempo = false;
  TEST_ASSERT_EQUAL(120, run().bpm);
  TEST_ASSERT_EQUAL(120, P.bpm);
}

void test_velocity_default_stored_as_zero() {
  P.tracks[0].defVel = 90;
  add(0, 60, 12, 90);
  add(24, 61, 12, 100);
  run();
  TEST_ASSERT_EQUAL(kVelDefault, st(0, 0, 0).vel);
  TEST_ASSERT_EQUAL(100, st(0, 0, 1).vel);
}

void test_trailing_silence_patterns() {
  I.lastTick = 2 * 384;  // end of track at bar 2 end
  add(0, 60);
  add(96, 62);           // notes only in bar 1
  ImportResult r = run();
  TEST_ASSERT_EQUAL(2, r.patternsWritten);
  TEST_ASSERT_EQUAL(16, P.patterns[1].length);

  setUp();
  I.lastTick = 2 * 384 + 5;  // a few ticks past the bar: rounds to the bar line
  add(0, 60);
  TEST_ASSERT_EQUAL(2, run().patternsWritten);

  setUp();
  M.firstPattern = 14;
  I.lastTick = 10 * 384;  // clamped to the patterns left
  add(0, 60);
  TEST_ASSERT_EQUAL(2, run().patternsWritten);

  setUp();
  M.offsetBars = 1;
  I.lastTick = 3 * 384;
  add(384, 60);
  TEST_ASSERT_EQUAL(2, run().patternsWritten);
}

void test_first_pattern_out_of_range() {
  M.firstPattern = 16;
  P.patterns[15].steps[0][0].note = 40;
  I.lastTick = 384;
  add(0, 60);
  ImportResult r = run();
  TEST_ASSERT_EQUAL(0, r.patternsWritten);
  TEST_ASSERT_EQUAL_UINT32(1, r.notesDropped);
  TEST_ASSERT_EQUAL(40, st(15, 0, 0).note);

  setUp();
  M.firstPattern = 255;
  add(0, 60);
  TEST_ASSERT_EQUAL(0, run().patternsWritten);
}

void test_note_slightly_before_offset() {
  M.offsetBars = 1;
  M.keepMicrotiming = true;
  add(384 - 5, 60);   // -0.21 step -> step 0, NDG -21
  add(384 - 13, 61);  // -0.54 step -> before the offset: ignored
  ImportResult r = run();
  TEST_ASSERT_EQUAL(60, st(0, 0, 0).note);
  TEST_ASSERT_EQUAL(-21, fxSigned(st(0, 0, 0).find(Fx::NDG)->val));
  TEST_ASSERT_EQUAL_UINT32(0, r.notesDropped);
  TEST_ASSERT_EQUAL(1, r.patternsWritten);
}

// Reference: the original exhaustive search.
static uint8_t gateRef(uint32_t len, int64_t q) {
  const int64_t want = static_cast<int64_t>(len) * kPpqn * 100;
  uint8_t best = 1;
  int64_t bestErr = -1;
  for (int v = 1; v <= 200; ++v) {
    int64_t err = static_cast<int64_t>(gatePercent(static_cast<uint8_t>(v))) * q - want;
    if (err < 0) err = -err;
    if (bestErr < 0 || err < bestErr) {
      bestErr = err;
      best = static_cast<uint8_t>(v);
    }
  }
  return best;
}

void test_nearest_gate_matches_search() {
  const int64_t qs[] = {96 * 24, 96 * 6, 480 * 16, 100 * 16, 960 * 24, 24 * 3, 1};
  for (int64_t q : qs) {
    const uint32_t maxLen = static_cast<uint32_t>(q * 10 / kPpqn + 50);
    for (uint32_t len = 0; len <= maxLen; ++len) {
      if (nearestGate(len, q) != gateRef(len, q)) {
        char msg[64];
        snprintf(msg, sizeof msg, "q=%lld len=%u", static_cast<long long>(q), len);
        TEST_FAIL_MESSAGE(msg);
      }
    }
  }
  TEST_ASSERT_EQUAL(gateRef(0xFFFFFFFFu, 96 * 24), nearestGate(0xFFFFFFFFu, 96 * 24));
}

void test_notes_to_kit_lanes() {
  instrSetType(P.instruments[3], InstrType::Kit);  // lanes 60..67
  P.tracks[0].instr = 3;
  add(0, 60, 12, 90);
  add(0, 62, 12, 50);
  add(24, 40);  // no lane
  add(48, 61, 12, P.tracks[0].defVel);
  const ImportResult r = run();
  TEST_ASSERT_EQUAL_HEX8(0b101, st(0, 0, 0).vel);
  TEST_ASSERT_EQUAL(90, st(0, 0, 0).note);  // the first note's velocity
  TEST_ASSERT_TRUE(st(0, 0, 1).isEmpty());
  TEST_ASSERT_EQUAL_HEX8(0b10, st(0, 0, 2).vel);
  TEST_ASSERT_EQUAL(0, st(0, 0, 2).note);   // the track's velocity
  TEST_ASSERT_EQUAL(1u, r.notesDropped);
}

// importPlan: the pattern count and tempo of importSmf, without touching the project.
void test_plan_matches_import() {
  M.firstPattern = 3;
  M.patternLen = 8;
  I.firstTempoUsPerQ = 600000;
  add(0, 60);
  add(24 * 10, 62);
  const uint8_t before = P.patterns[3].length;
  const ImportResult plan = importPlan(I, N, nN, M, P.bpm);
  TEST_ASSERT_EQUAL(before, P.patterns[3].length);  // nothing written
  TEST_ASSERT_EQUAL(120, P.bpm);
  const ImportResult r = run();
  TEST_ASSERT_EQUAL(r.patternsWritten, plan.patternsWritten);
  TEST_ASSERT_EQUAL(2, plan.patternsWritten);
  TEST_ASSERT_EQUAL(r.bpm, plan.bpm);
  TEST_ASSERT_EQUAL(100, plan.bpm);
  M.useTempo = false;
  TEST_ASSERT_EQUAL(77, importPlan(I, N, nN, M, 77).bpm);
}

// A target track's own length is reset: notes past its old loop play.
void test_resets_target_track_length() {
  P.patterns[0].trackLen[0] = 8;
  P.patterns[0].trackLen[1] = 12;
  M.patternLen = 16;
  add(24 * 10, 62);  // step 10, past the old loop of 8
  run();
  TEST_ASSERT_EQUAL(0, P.patterns[0].trackLen[0]);
  TEST_ASSERT_EQUAL(12, P.patterns[0].trackLen[1]);  // not a target: kept (fits 16)
  TEST_ASSERT_EQUAL(62, st(0, 0, 10).note);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_map_defaults);
  RUN_TEST(test_quantize_nearest_step);
  RUN_TEST(test_triplet_step_length);
  RUN_TEST(test_triplet_ppq_not_divisible);
  RUN_TEST(test_mono_modes);
  RUN_TEST(test_transpose_and_range);
  RUN_TEST(test_offset_bars);
  RUN_TEST(test_split_into_two_patterns);
  RUN_TEST(test_limit_p16);
  RUN_TEST(test_clears_only_target_tracks);
  RUN_TEST(test_unmapped_source_ignored);
  RUN_TEST(test_gate);
  RUN_TEST(test_microtiming_ndg);
  RUN_TEST(test_ndg_below_threshold);
  RUN_TEST(test_source_channel_chn);
  RUN_TEST(test_twelve_channels_to_twelve_tracks);
  RUN_TEST(test_fx_priority);
  RUN_TEST(test_mono_replacement_rewrites_fx);
  RUN_TEST(test_tempo);
  RUN_TEST(test_plan_matches_import);
  RUN_TEST(test_resets_target_track_length);
  RUN_TEST(test_velocity_default_stored_as_zero);
  RUN_TEST(test_trailing_silence_patterns);
  RUN_TEST(test_first_pattern_out_of_range);
  RUN_TEST(test_note_slightly_before_offset);
  RUN_TEST(test_nearest_gate_matches_search);
  RUN_TEST(test_notes_to_kit_lanes);
  return UNITY_END();
}
