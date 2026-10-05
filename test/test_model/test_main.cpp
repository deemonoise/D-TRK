#include <unity.h>
#include "model.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_step_is_six_bytes_and_empty_by_default() {
  TEST_ASSERT_EQUAL(6, sizeof(Step));
  Step s;
  TEST_ASSERT_TRUE(s.isEmpty());
  TEST_ASSERT_FALSE(s.hasNote());
  s.note = 60;
  TEST_ASSERT_TRUE(s.hasNote());
  s.note = kNoteOff;
  TEST_ASSERT_FALSE(s.hasNote());
}

void test_find_fx() {
  Step s;
  s.fx[1] = {Fx::RAT, 4};
  TEST_ASSERT_NULL(s.find(Fx::PRB));
  TEST_ASSERT_NOT_NULL(s.find(Fx::RAT));
  TEST_ASSERT_EQUAL(4, s.find(Fx::RAT)->val);
}

void test_ticks_per_step() {
  TEST_ASSERT_EQUAL(96, ticksPerStep(Resolution::Quarter));
  TEST_ASSERT_EQUAL(24, ticksPerStep(Resolution::Sixteenth));
  TEST_ASSERT_EQUAL(12, ticksPerStep(Resolution::ThirtySecond));
  TEST_ASSERT_EQUAL(32, ticksPerStep(Resolution::EighthTriplet));
  TEST_ASSERT_EQUAL(16, ticksPerStep(Resolution::SixteenthTriplet));
}

void test_gate_percent_encoding() {
  TEST_ASSERT_EQUAL(1, gatePercent(1));
  TEST_ASSERT_EQUAL(100, gatePercent(100));
  TEST_ASSERT_EQUAL(107, gatePercent(101));
  TEST_ASSERT_EQUAL(800, gatePercent(200));
  TEST_ASSERT_EQUAL(800, gatePercent(255));
}

void test_project_reset_defaults() {
  Project* p = new Project();
  TEST_ASSERT_EQUAL(120, p->bpm);
  for (int i = 0; i < kTracks; ++i) TEST_ASSERT_EQUAL(i, p->tracks[i].channel);
  TEST_ASSERT_EQUAL_STRING("TRK1", p->tracks[0].name);
  TEST_ASSERT_EQUAL(16, p->patterns[3].length);
  TEST_ASSERT_TRUE(p->patterns[3].isEmpty());
  delete p;
}

void test_project_reset_clears_song_mode() {
  Project* p = new Project();
  TEST_ASSERT_FALSE(p->songMode);
  p->songMode = true;
  p->reset();
  TEST_ASSERT_FALSE(p->songMode);
  delete p;
}

void test_track_audible_mute_solo() {
  Project* p = new Project();
  TEST_ASSERT_TRUE(p->trackAudible(0));
  p->tracks[0].mute = true;
  TEST_ASSERT_FALSE(p->trackAudible(0));
  p->tracks[2].solo = true;
  TEST_ASSERT_TRUE(p->trackAudible(2));
  TEST_ASSERT_FALSE(p->trackAudible(1));
  delete p;
}

void test_reset_audio_defaults() {
  Project* p = new Project();
  TEST_ASSERT_EQUAL(static_cast<int>(TrackOut::Midi), static_cast<int>(p->tracks[3].out));
  TEST_ASSERT_EQUAL(3, p->tracks[3].instr);  // track N defaults to instrument N
  TEST_ASSERT_EQUAL(100, p->tracks[3].vol);
  TEST_ASSERT_EQUAL(40, p->masterVol);
  TEST_ASSERT_TRUE(p->preview);
  TEST_ASSERT_EQUAL_STRING("INS1", p->instruments[0].name);
  TEST_ASSERT_EQUAL_STRING("INS16", p->instruments[15].name);
  TEST_ASSERT_EQUAL(static_cast<int>(InstrType::Chip), static_cast<int>(p->instruments[0].type));
  TEST_ASSERT_FALSE(p->trackInternal(3));
  p->tracks[3].out = TrackOut::Int;
  TEST_ASSERT_TRUE(p->trackInternal(3));
  p->instruments[2].vol = 5;
  p->masterVol = 0;
  p->reset();
  TEST_ASSERT_EQUAL(100, p->instruments[2].vol);
  TEST_ASSERT_EQUAL(40, p->masterVol);
  TEST_ASSERT_FALSE(p->trackInternal(3));
  delete p;
}

void test_env_time_curve() {
  TEST_ASSERT_EQUAL(0, envTimeMs(0));
  TEST_ASSERT_EQUAL(1, envTimeMs(1));
  TEST_ASSERT_EQUAL(10000, envTimeMs(127));
  for (int v = 1; v < 127; ++v) TEST_ASSERT_TRUE(envTimeMs(v) <= envTimeMs(v + 1));
}

void test_fm_decay_ms_range() {
  TEST_ASSERT_EQUAL(5, fmDecayMs(0));
  TEST_ASSERT_EQUAL(4000, fmDecayMs(127));
  for (int v = 1; v < 128; ++v) TEST_ASSERT_TRUE(fmDecayMs(v) >= fmDecayMs(v - 1));
}

void test_lfo_hz_range() {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.05f, lfoHz(0));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 30.f, lfoHz(127));
}

void test_fm_gated_machines() {
  TEST_ASSERT_TRUE(fmGated(static_cast<uint8_t>(FmMachine::Tone)));
  TEST_ASSERT_TRUE(fmGated(static_cast<uint8_t>(FmMachine::Chord)));
  TEST_ASSERT_FALSE(fmGated(static_cast<uint8_t>(FmMachine::Kick)));
  TEST_ASSERT_FALSE(fmGated(static_cast<uint8_t>(FmMachine::Hat)));
  TEST_ASSERT_FALSE(fmGated(200));  // out of range = Kick
}

void test_fm_set_machine_defaults() {
  Instrument m;
  TEST_ASSERT_EQUAL(static_cast<int>(FmMachine::Kick), m.machine);
  m.macro[kMacCol] = 1;
  fmSetMachine(m, static_cast<uint8_t>(FmMachine::Chord));
  TEST_ASSERT_EQUAL(static_cast<int>(FmMachine::Chord), m.machine);
  TEST_ASSERT_EQUAL(0, m.macro[kMacShp]);  // chord type maj
  TEST_ASSERT_TRUE(m.macro[kMacCol] != 1);
  fmSetMachine(m, 99);
  TEST_ASSERT_EQUAL(static_cast<int>(FmMachine::Count) - 1, m.machine);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_step_is_six_bytes_and_empty_by_default);
  RUN_TEST(test_find_fx);
  RUN_TEST(test_ticks_per_step);
  RUN_TEST(test_gate_percent_encoding);
  RUN_TEST(test_project_reset_defaults);
  RUN_TEST(test_project_reset_clears_song_mode);
  RUN_TEST(test_track_audible_mute_solo);
  RUN_TEST(test_reset_audio_defaults);
  RUN_TEST(test_env_time_curve);
  RUN_TEST(test_fm_decay_ms_range);
  RUN_TEST(test_lfo_hz_range);
  RUN_TEST(test_fm_gated_machines);
  RUN_TEST(test_fm_set_machine_defaults);
  return UNITY_END();
}
