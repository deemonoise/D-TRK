#include <stdio.h>
#include <string.h>
#include <unity.h>
#include "model.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_step_is_14_bytes_and_empty_by_default() {
  TEST_ASSERT_EQUAL(6, kFxSlots);
  TEST_ASSERT_EQUAL(14, sizeof(Step));
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

void test_fx_in_last_slot() {
  Step s;
  TEST_ASSERT_FALSE(s.hasFx());
  s.fx[5] = {Fx::GAT, 10};
  TEST_ASSERT_TRUE(s.hasFx());
  TEST_ASSERT_FALSE(s.isEmpty());
  TEST_ASSERT_EQUAL_PTR(&s.fx[5], s.find(Fx::GAT));
  s.fx[2] = {Fx::GAT, 20};
  TEST_ASSERT_EQUAL_PTR(&s.fx[2], s.find(Fx::GAT));  // the first slot wins
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
  TEST_ASSERT_EQUAL(static_cast<int>(TrackOut::Int), static_cast<int>(p->tracks[3].out));
  TEST_ASSERT_EQUAL(3, p->tracks[3].instr);  // track N defaults to instrument N
  TEST_ASSERT_EQUAL(100, p->tracks[3].vol);
  TEST_ASSERT_EQUAL(40, p->masterVol);
  TEST_ASSERT_TRUE(p->preview);
  TEST_ASSERT_EQUAL(3, p->dlyTime);
  TEST_ASSERT_EQUAL(50, p->dlyFb);
  TEST_ASSERT_EQUAL(90, p->dlyTone);
  TEST_ASSERT_EQUAL(100, p->dlyLevel);
  TEST_ASSERT_EQUAL(0, p->instruments[0].send);
  TEST_ASSERT_EQUAL_STRING("INS1", p->instruments[0].name);
  TEST_ASSERT_EQUAL_STRING("INS16", p->instruments[15].name);
  TEST_ASSERT_EQUAL(static_cast<int>(InstrType::Fm), static_cast<int>(p->instruments[0].type));
  TEST_ASSERT_EQUAL(static_cast<int>(FmMachine::Tone), p->instruments[0].machine);
  TEST_ASSERT_TRUE(p->trackInternal(3));
  p->tracks[3].out = TrackOut::Midi;
  TEST_ASSERT_FALSE(p->trackInternal(3));
  p->instruments[2].vol = 5;
  p->masterVol = 0;
  p->dlyTime = 9;
  p->dlyLevel = 1;
  p->reset();
  TEST_ASSERT_EQUAL(3, p->dlyTime);
  TEST_ASSERT_EQUAL(100, p->dlyLevel);
  TEST_ASSERT_EQUAL(100, p->instruments[2].vol);
  TEST_ASSERT_EQUAL(40, p->masterVol);
  TEST_ASSERT_TRUE(p->trackInternal(3));
  delete p;
}

void test_instr_type_order() {
  TEST_ASSERT_TRUE(instrTypeAt(0) == InstrType::Fm);
  TEST_ASSERT_TRUE(instrTypeAt(1) == InstrType::Synth);
  TEST_ASSERT_TRUE(instrTypeAt(2) == InstrType::Drum);
  TEST_ASSERT_TRUE(instrTypeAt(3) == InstrType::Sample);
  TEST_ASSERT_TRUE(instrTypeAt(4) == InstrType::Chip);
  for (int k = 0; k < static_cast<int>(InstrType::Count); ++k) TEST_ASSERT_EQUAL(k, instrTypePos(instrTypeAt(k)));
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

void test_reset_clears_samples() {
  static Project p;
  p.sampleCount = 3;
  strcpy(p.samples[0].name, "kick");
  p.samples[0].crc = 5;
  p.reset();
  TEST_ASSERT_EQUAL(0, p.sampleCount);
  TEST_ASSERT_EQUAL_STRING("", p.samples[0].name);
  TEST_ASSERT_EQUAL(0, p.samples[0].crc);
}

// = kSynthRate (synth_osc.h): this test does not link the synth.
constexpr uint32_t kSampleRateHz = 32000;

void test_cutoff_hz_range() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.f, cutoffHz(0));
  TEST_ASSERT_FLOAT_WITHIN(1.f, 14000.f, cutoffHz(127));
  TEST_ASSERT_TRUE(cutoffHz(64) > cutoffHz(63));
}

void test_reso_q_range() {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, resoQ(0));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.f, resoQ(127));
}

void test_filter_env_shape() {
  // fAtk 0: peak at once; fDec 0: holds the peak.
  TEST_ASSERT_EQUAL_FLOAT(1.f, filterEnv(0, 0, 0));
  TEST_ASSERT_EQUAL_FLOAT(1.f, filterEnv(100000, 0, 0));
  // Attack: linear 0 -> 1 over envTimeMs(fAtk).
  const uint32_t a = envTimeMs(60) * kSampleRateHz / 1000;
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.5f, filterEnv(a / 2, 60, 40));
  // Decay: -60 dB at envTimeMs(fDec) after the attack.
  const uint32_t d = envTimeMs(40) * kSampleRateHz / 1000;
  TEST_ASSERT_FLOAT_WITHIN(0.0005f, 0.001f, filterEnv(a + d, 60, 40));
  TEST_ASSERT_TRUE(filterEnv(a + d / 2, 60, 40) < 1.f);
}

void test_drum_set_machine() {
  Instrument m;
  drumSetMachine(m, static_cast<uint8_t>(DrumMachine::Hh8));
  TEST_ASSERT_EQUAL(static_cast<int>(DrumMachine::Hh8), m.machine);
  drumSetMachine(m, 200);
  TEST_ASSERT_EQUAL(static_cast<int>(DrumMachine::Count) - 1, m.machine);
}

void test_instr_set_type() {
  Instrument m;
  m.machine = 12;
  instrSetType(m, InstrType::Fm);
  TEST_ASSERT_TRUE(m.machine < static_cast<int>(FmMachine::Count));
  instrSetType(m, InstrType::Drum);
  TEST_ASSERT_TRUE(m.type == InstrType::Drum);
  // Macro LFO targets exist on FM / DRUM only.
  m.lfoDest = static_cast<uint8_t>(LfoDest::Col);
  instrSetType(m, InstrType::Chip);
  TEST_ASSERT_EQUAL(static_cast<int>(LfoDest::Pitch), m.lfoDest);
  m.lfoDest = static_cast<uint8_t>(LfoDest::Cutoff);
  instrSetType(m, InstrType::Sample);
  TEST_ASSERT_EQUAL(static_cast<int>(LfoDest::Cutoff), m.lfoDest);
}

void test_lfo_dest_step() {
  const uint8_t vol = static_cast<uint8_t>(LfoDest::Vol), cut = static_cast<uint8_t>(LfoDest::Cutoff);
  const uint8_t drv = static_cast<uint8_t>(LfoDest::Drive);
  // FM / DRUM: every target in order, clamped at the ends.
  TEST_ASSERT_EQUAL(1, lfoDestStep(0, 1, true));
  TEST_ASSERT_EQUAL(cut, lfoDestStep(vol, 1, true));
  TEST_ASSERT_EQUAL(drv, lfoDestStep(vol, 5, true));
  TEST_ASSERT_EQUAL(drv, lfoDestStep(cut, 1, false));  // DRIVE on every type
  TEST_ASSERT_EQUAL(0, lfoDestStep(2, -9, true));
  // CHIP / SAMPLE: the macro targets are skipped.
  TEST_ASSERT_EQUAL(vol, lfoDestStep(0, 1, false));
  TEST_ASSERT_EQUAL(cut, lfoDestStep(0, 2, false));
  TEST_ASSERT_EQUAL(drv, lfoDestStep(0, 9, false));
  TEST_ASSERT_EQUAL(0, lfoDestStep(vol, -1, false));
  TEST_ASSERT_EQUAL(0, lfoDestStep(0, -1, false));
}

void test_filter_defaults_off() {
  Instrument m;
  TEST_ASSERT_EQUAL(static_cast<int>(FltMode::Off), m.fltMode);
  TEST_ASSERT_EQUAL(127, m.cutoff);
}

void test_synth_type_defaults() {
  Instrument m;
  m.lfoDest = static_cast<uint8_t>(LfoDest::Shp);
  instrSetType(m, InstrType::Synth);
  TEST_ASSERT_EQUAL(InstrType::Synth, m.type);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(SynOsc::Saw), m.synOsc[0]);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(SynOsc::Saw), m.synOsc[1]);
  TEST_ASSERT_EQUAL(0, m.macro[kMacShp1]);
  TEST_ASSERT_EQUAL(0, m.macro[kMacMix]);
  TEST_ASSERT_EQUAL(64, m.macro[kMacDet]);
  TEST_ASSERT_EQUAL(64, m.macro[kMacSenv]);
  // SYNTH keeps macro LFO targets.
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(LfoDest::Shp), m.lfoDest);
}

void test_synth_type_order() {
  // UI order: FM, SYNTH, DRUM, SAMPLE, CHIP.
  TEST_ASSERT_EQUAL(InstrType::Fm, instrTypeAt(0));
  TEST_ASSERT_EQUAL(InstrType::Synth, instrTypeAt(1));
  TEST_ASSERT_EQUAL(1, instrTypePos(InstrType::Synth));
  TEST_ASSERT_EQUAL(InstrType::Chip, instrTypeAt(4));
}

void test_project_reset_clears_wavetables() {
  Project p;
  strcpy(p.wavetables[0].name, "X");
  p.wavetableCount = 1;
  p.reset();
  TEST_ASSERT_EQUAL(0, p.wavetableCount);
  TEST_ASSERT_EQUAL(0, p.wavetables[0].name[0]);
}

// 16 tracks, each with its own default channel, instrument and name TRKn.
void test_sixteen_tracks_defaults() {
  Project* p = new Project();
  TEST_ASSERT_EQUAL(16, kTracks);
  for (int i = 0; i < kTracks; ++i) {
    TEST_ASSERT_EQUAL(i, p->tracks[i].channel);
    TEST_ASSERT_EQUAL(i, p->tracks[i].instr);
    char nm[9];
    snprintf(nm, sizeof(nm), "TRK%d", i + 1);
    TEST_ASSERT_EQUAL_STRING(nm, p->tracks[i].name);
  }
  delete p;
}

void test_kit_defaults_and_drum_track() {
  Project* p = new Project();
  Instrument& k = p->instruments[3];
  instrSetType(k, InstrType::Kit);
  TEST_ASSERT_EQUAL(22, sizeof(KitLane));
  for (int i = 0; i < kKitLanes; ++i) {
    TEST_ASSERT_EQUAL(kNoInstr, k.kit[i].instr);
    TEST_ASSERT_EQUAL_STRING("", k.kit[i].sample);
    TEST_ASSERT_EQUAL(100, k.kit[i].vol);
    TEST_ASSERT_EQUAL(0, k.kit[i].pitch);
    TEST_ASSERT_EQUAL(0, k.kit[i].decay);
    TEST_ASSERT_EQUAL(60 + i, k.kit[i].note);
  }
  TEST_ASSERT_FALSE(p->trackIsDrum(0));
  p->tracks[0].instr = 3;
  TEST_ASSERT_TRUE(p->trackIsDrum(0));
  TEST_ASSERT_EQUAL_PTR(&k, p->kitOf(0));
  TEST_ASSERT_NULL(p->kitOf(1));
  TEST_ASSERT_NULL(p->kitOf(kTracks));
  // KIT is the last type in the UI order.
  TEST_ASSERT_EQUAL(static_cast<int>(InstrType::Kit),
                    static_cast<int>(instrTypeAt(static_cast<int>(InstrType::Count) - 1)));
  delete p;
}

void test_sound_fx_defaults() {
  static Project p;
  p.reset();
  const Instrument m;
  TEST_ASSERT_EQUAL(0, m.drive);
  TEST_ASSERT_EQUAL(0, m.rsend);
  TEST_ASSERT_EQUAL(0, m.velCut);
  TEST_ASSERT_EQUAL(0, m.velMac);
  TEST_ASSERT_EQUAL(60, p.rvbSize);
  TEST_ASSERT_EQUAL(70, p.rvbDamp);
  TEST_ASSERT_EQUAL(80, p.rvbLevel);
  TEST_ASSERT_EQUAL(0, p.compAmt);
  TEST_ASSERT_EQUAL(50, p.compRel);
  TEST_ASSERT_EQUAL(0, p.scTrack);
  TEST_ASSERT_EQUAL(64, p.scDepth);
  TEST_ASSERT_EQUAL(12, kLocks);  // + BIT, SRR
  TEST_ASSERT_EQUAL(static_cast<int>(Fx::DCY) + kLockFlt, static_cast<int>(Fx::FLT));  // lock order unchanged
}

void test_lfo_sync_and_refs() {
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 4.f, lfoSyncHz(4, 120));    // 1/8 at 120 BPM: 4 per second
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f, lfoSyncHz(8, 120));   // 1 bar
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 16.f, lfoSyncHz(0, 120));   // 1/32
  TEST_ASSERT_EQUAL_STRING("1/16T", lfoSyncName(1));
  TEST_ASSERT_EQUAL_STRING("8 BARS", lfoSyncName(99));
  Instrument m;
  lfoRef(m, 0).depth = 12;
  lfoRef(m, 2).dest = static_cast<uint8_t>(LfoDest::Vol);
  TEST_ASSERT_EQUAL(12, m.lfoDepth);
  TEST_ASSERT_EQUAL(static_cast<int>(LfoDest::Vol), m.lfo[1].dest);
  TEST_ASSERT_EQUAL(12, lfoAt(m, 0).depth);
  // A macro target on any LFO becomes PITCH on CHIP / SAMPLE.
  m.lfo[2].dest = static_cast<uint8_t>(LfoDest::Col);
  instrSetType(m, InstrType::Chip);
  TEST_ASSERT_EQUAL(static_cast<int>(LfoDest::Pitch), m.lfo[2].dest);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_step_is_14_bytes_and_empty_by_default);
  RUN_TEST(test_fx_in_last_slot);
  RUN_TEST(test_find_fx);
  RUN_TEST(test_ticks_per_step);
  RUN_TEST(test_gate_percent_encoding);
  RUN_TEST(test_project_reset_defaults);
  RUN_TEST(test_project_reset_clears_song_mode);
  RUN_TEST(test_track_audible_mute_solo);
  RUN_TEST(test_reset_audio_defaults);
  RUN_TEST(test_instr_type_order);
  RUN_TEST(test_env_time_curve);
  RUN_TEST(test_fm_decay_ms_range);
  RUN_TEST(test_lfo_hz_range);
  RUN_TEST(test_fm_gated_machines);
  RUN_TEST(test_fm_set_machine_defaults);
  RUN_TEST(test_reset_clears_samples);
  RUN_TEST(test_cutoff_hz_range);
  RUN_TEST(test_reso_q_range);
  RUN_TEST(test_filter_env_shape);
  RUN_TEST(test_drum_set_machine);
  RUN_TEST(test_instr_set_type);
  RUN_TEST(test_lfo_dest_step);
  RUN_TEST(test_filter_defaults_off);
  RUN_TEST(test_synth_type_defaults);
  RUN_TEST(test_synth_type_order);
  RUN_TEST(test_project_reset_clears_wavetables);
  RUN_TEST(test_sixteen_tracks_defaults);
  RUN_TEST(test_kit_defaults_and_drum_track);
  RUN_TEST(test_sound_fx_defaults);
  RUN_TEST(test_lfo_sync_and_refs);
  return UNITY_END();
}
