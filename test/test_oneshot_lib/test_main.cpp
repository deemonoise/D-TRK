#include <string.h>
#include <unity.h>
#include "oneshot_lib.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static Project* mkProj() {  // static storage: a Project is large
  static Project p;
  p = Project();
  return &p;
}

static const char* dirOf(InstrType t, uint8_t machine) {
  static char d[kOneshotDirMax];
  Instrument in;
  in.type = t;
  in.machine = machine;
  oneshotDir(in, d);
  return d;
}

static uint8_t dm(DrumMachine m) { return static_cast<uint8_t>(m); }

void test_drum_dirs() {
  TEST_ASSERT_EQUAL_STRING("808/BD", dirOf(InstrType::Drum, dm(DrumMachine::Bd8)));
  TEST_ASSERT_EQUAL_STRING("909/BD", dirOf(InstrType::Drum, dm(DrumMachine::Bd9)));
  TEST_ASSERT_EQUAL_STRING("RES/BD", dirOf(InstrType::Drum, dm(DrumMachine::Bda)));
  TEST_ASSERT_EQUAL_STRING("SYN/BD", dirOf(InstrType::Drum, dm(DrumMachine::Bds)));
  TEST_ASSERT_EQUAL_STRING("MODAL/SD", dirOf(InstrType::Drum, dm(DrumMachine::Sda)));
  TEST_ASSERT_EQUAL_STRING("SYN/SD", dirOf(InstrType::Drum, dm(DrumMachine::Sds)));
  TEST_ASSERT_EQUAL_STRING("909/TOM", dirOf(InstrType::Drum, dm(DrumMachine::Tom9)));
  TEST_ASSERT_EQUAL_STRING("MULTI/CP", dirOf(InstrType::Drum, dm(DrumMachine::Cpx)));
  TEST_ASSERT_EQUAL_STRING("MODAL/RS", dirOf(InstrType::Drum, dm(DrumMachine::Rsx)));
  TEST_ASSERT_EQUAL_STRING("RING/HH", dirOf(InstrType::Drum, dm(DrumMachine::Hhr)));
  TEST_ASSERT_EQUAL_STRING("808/CY", dirOf(InstrType::Drum, dm(DrumMachine::Cy8)));
  TEST_ASSERT_EQUAL_STRING("CLAVE/PERC", dirOf(InstrType::Drum, dm(DrumMachine::Cl8)));
  TEST_ASSERT_EQUAL_STRING("RES/BD", dirOf(InstrType::Drum, dm(DrumMachine::Unused22)));
  TEST_ASSERT_EQUAL_STRING("RES/BD", dirOf(InstrType::Drum, 200));  // out of range as BDA
}

void test_fm_dirs() {
  TEST_ASSERT_EQUAL_STRING("FM/KICK", dirOf(InstrType::Fm, 0));
  TEST_ASSERT_EQUAL_STRING("FM/HAT", dirOf(InstrType::Fm, static_cast<uint8_t>(FmMachine::Hat)));
  TEST_ASSERT_EQUAL_STRING("FM/KICK", dirOf(InstrType::Fm, 99));
}

void test_fm_machine_names() {
  TEST_ASSERT_EQUAL_STRING("CHORD", fmMachineName(static_cast<uint8_t>(FmMachine::Chord)));
  TEST_ASSERT_EQUAL_STRING("KICK", fmMachineName(255));
}

void test_chip_wave_names() {
  char o[16];
  chipWaveName(static_cast<uint8_t>(Wave::Pulse), o, sizeof o);
  TEST_ASSERT_EQUAL_STRING("PULSE", o);
  chipWaveName(static_cast<uint8_t>(Wave::Wt1) + 2, o, sizeof o);
  TEST_ASSERT_EQUAL_STRING("WT3", o);
  chipWaveName(static_cast<uint8_t>(Wave::SidTs), o, sizeof o);
  TEST_ASSERT_EQUAL_STRING("SID TS", o);
  chipWaveName(static_cast<uint8_t>(Wave::AyToneNoise), o, sizeof o);
  TEST_ASSERT_EQUAL_STRING("AY TN", o);
  chipWaveName(250, o, sizeof o);  // out of range -> PULSE
  TEST_ASSERT_EQUAL_STRING("PULSE", o);
}

void test_chip_dirs_sanitized() {
  Instrument in;
  in.type = InstrType::Chip;
  char d[kOneshotDirMax];
  in.wave = static_cast<uint8_t>(Wave::Saw);
  oneshotDir(in, d);
  TEST_ASSERT_EQUAL_STRING("CHIP/SAW", d);
  in.wave = static_cast<uint8_t>(Wave::Wt1) + 15;
  oneshotDir(in, d);
  TEST_ASSERT_EQUAL_STRING("CHIP/WT16", d);
  in.wave = static_cast<uint8_t>(Wave::SidPs);
  oneshotDir(in, d);
  TEST_ASSERT_EQUAL_STRING("CHIP/SID_PS", d);
}

void test_synth_dirs() {
  Instrument in;
  in.type = InstrType::Synth;
  char d[kOneshotDirMax];
  const char* const want[] = {"SYNTH/SAW", "SYNTH/SQUARE", "SYNTH/TRI", "SYNTH/WT"};
  for (int k = 0; k < 4; ++k) {
    in.synOsc[0] = static_cast<uint8_t>(k);
    in.synOsc[1] = static_cast<uint8_t>(3 - k);  // osc 2 does not matter
    oneshotDir(in, d);
    TEST_ASSERT_EQUAL_STRING(want[k], d);
  }
  in.synOsc[0] = 77;
  oneshotDir(in, d);
  TEST_ASSERT_EQUAL_STRING("SYNTH/SAW", d);
}

void test_other_dirs() {
  TEST_ASSERT_EQUAL_STRING("STRING/MODAL", dirOf(InstrType::String, static_cast<uint8_t>(StringModel::Modal)));
  TEST_ASSERT_EQUAL_STRING("STRING/STRING", dirOf(InstrType::String, 9));
  TEST_ASSERT_EQUAL_STRING("LOFI/APC", dirOf(InstrType::Lofi, 1));
  TEST_ASSERT_EQUAL_STRING("LOFI/BYTE", dirOf(InstrType::Lofi, 0));
  TEST_ASSERT_EQUAL_STRING("SAMPLE", dirOf(InstrType::Sample, 0));
  TEST_ASSERT_EQUAL_STRING("KIT", dirOf(InstrType::Kit, 0));
  TEST_ASSERT_EQUAL_STRING("OTHER", dirOf(InstrType::Unused7, 0));
  TEST_ASSERT_EQUAL_STRING("OTHER", dirOf(static_cast<InstrType>(200), 0));
}

// exists() over a list of taken paths.
static bool inList(const char* path, void* ctx) {
  for (const char* const* p = static_cast<const char* const*>(ctx); *p; ++p)
    if (strcmp(*p, path) == 0) return true;
  return false;
}

void test_path_free() {
  const char* taken[] = {nullptr};
  char o[64];
  TEST_ASSERT_TRUE(oneshotFilePath("909/BD", "KICK1", inList, taken, o, sizeof o));
  TEST_ASSERT_EQUAL_STRING("/samples/oneshots/909/BD/KICK1.wav", o);
}

void test_path_collisions() {
  const char* taken[] = {"/samples/oneshots/FM/KICK/K.wav", "/samples/oneshots/FM/KICK/K_1.wav", nullptr};
  char o[64];
  TEST_ASSERT_TRUE(oneshotFilePath("FM/KICK", "K", inList, taken, o, sizeof o));
  TEST_ASSERT_EQUAL_STRING("/samples/oneshots/FM/KICK/K_2.wav", o);
}

static bool always(const char*, void*) { return true; }

void test_path_none_free() {
  char o[64];
  TEST_ASSERT_FALSE(oneshotFilePath("SAMPLE", "X", always, nullptr, o, sizeof o));
}

void test_path_no_fit() {
  const char* taken[] = {nullptr};
  char o[64];
  const char* p = "/samples/oneshots/SAMPLE/ABC.wav";  // 32 chars
  TEST_ASSERT_TRUE(oneshotFilePath("SAMPLE", "ABC", inList, taken, o, static_cast<int>(strlen(p)) + 1));
  TEST_ASSERT_FALSE(oneshotFilePath("SAMPLE", "ABC", inList, taken, o, static_cast<int>(strlen(p))));
  // the suffixed name no longer fits
  const char* taken2[] = {p, nullptr};
  TEST_ASSERT_FALSE(oneshotFilePath("SAMPLE", "ABC", inList, taken2, o, static_cast<int>(strlen(p)) + 1));
}

void test_bake_lanes() {
  Project& p = *mkProj();
  Instrument& k = p.instruments[5];
  instrSetType(k, InstrType::Kit);
  p.instruments[1].type = InstrType::Fm;
  p.instruments[2].type = InstrType::Drum;
  p.instruments[6].type = InstrType::Kit;
  k.kit[0].instr = kNoInstr;  // sampler
  k.kit[1].instr = 2;
  k.kit[2].instr = 6;         // KIT in KIT: skipped
  k.kit[3].instr = 1;
  k.kit[4].instr = kInstruments;  // invalid
  k.kit[7].instr = 2;
  uint8_t lanes[kKitLanes];
  TEST_ASSERT_EQUAL_INT(3, kitBakeLanes(p, 5, lanes));
  TEST_ASSERT_EQUAL_UINT8(1, lanes[0]);
  TEST_ASSERT_EQUAL_UINT8(3, lanes[1]);
  TEST_ASSERT_EQUAL_UINT8(7, lanes[2]);
  p.instruments[3].type = InstrType::Sample;
  k.kit[5].instr = 3;  // a SAMPLE instrument: skipped
  TEST_ASSERT_EQUAL_INT(3, kitBakeLanes(p, 5, lanes));
  TEST_ASSERT_EQUAL_UINT8(7, lanes[2]);
}

void test_bake_lanes_sixteen() {  // lanes 9-16 bake too
  Project& p = *mkProj();
  Instrument& k = p.instruments[5];
  instrSetType(k, InstrType::Kit);  // notes 60..75: all distinct
  p.instruments[1].type = InstrType::Fm;
  k.kit[12].instr = 1;
  k.kit[15].instr = 1;
  uint8_t lanes[kKitLanes];
  TEST_ASSERT_EQUAL_INT(2, kitBakeLanes(p, 5, lanes));
  TEST_ASSERT_EQUAL_UINT8(12, lanes[0]);
  TEST_ASSERT_EQUAL_UINT8(15, lanes[1]);
}

void test_bake_lanes_shared_note() {  // the note-on takes the first lane with the note
  Project& p = *mkProj();
  Instrument& k = p.instruments[5];
  instrSetType(k, InstrType::Kit);
  p.instruments[1].type = InstrType::Fm;
  p.instruments[2].type = InstrType::Drum;
  k.kit[0].note = 40;  // a sampler lane holds the note: lane 2 never sounds
  k.kit[1].instr = 1;
  k.kit[2].instr = 2;
  k.kit[2].note = 40;
  k.kit[3].instr = 2;
  k.kit[4].instr = 1;
  k.kit[4].note = k.kit[3].note;  // an earlier baked lane holds it
  uint8_t lanes[kKitLanes];
  TEST_ASSERT_EQUAL_INT(2, kitBakeLanes(p, 5, lanes));
  TEST_ASSERT_EQUAL_UINT8(1, lanes[0]);
  TEST_ASSERT_EQUAL_UINT8(3, lanes[1]);
  TEST_ASSERT_EQUAL_INT(0, kitBakeLanes(p, 1, lanes));    // not a KIT
  TEST_ASSERT_EQUAL_INT(0, kitBakeLanes(p, -1, lanes));
  TEST_ASSERT_EQUAL_INT(0, kitBakeLanes(p, kInstruments, lanes));
}

void test_lane_to_sample() {
  Instrument k;
  instrSetType(k, InstrType::Kit);
  KitLane& l = k.kit[3];
  l.instr = 4;
  l.vol = 90;
  l.pitch = -7;
  l.decay = 33;
  l.note = 64;
  kitLaneToSample(k, 3, "KIT_BD");
  TEST_ASSERT_EQUAL_UINT8(kNoInstr, l.instr);
  TEST_ASSERT_EQUAL_STRING("KIT_BD", l.sample);
  TEST_ASSERT_EQUAL_INT8(0, l.pitch);
  TEST_ASSERT_EQUAL_UINT8(kLaneUnityVol, l.vol);  // the rendered level
  TEST_ASSERT_EQUAL_UINT8(127, kLaneUnityVol);
  TEST_ASSERT_EQUAL_UINT8(0, l.decay);  // full
  TEST_ASSERT_EQUAL_UINT8(64, l.note);
  kitLaneToSample(k, 3, "ABCDEFGHIJKLMNOPQRSTUV");  // longer than a sample name: cut
  TEST_ASSERT_EQUAL_INT(kSampleNameMax, static_cast<int>(strlen(l.sample)));
  TEST_ASSERT_EQUAL_STRING_LEN("ABCDEFGHIJKLMNOP", l.sample, kSampleNameMax);
  kitLaneToSample(k, 9, "X");  // out of range: nothing
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_drum_dirs);
  RUN_TEST(test_fm_dirs);
  RUN_TEST(test_fm_machine_names);
  RUN_TEST(test_chip_wave_names);
  RUN_TEST(test_chip_dirs_sanitized);
  RUN_TEST(test_synth_dirs);
  RUN_TEST(test_other_dirs);
  RUN_TEST(test_path_free);
  RUN_TEST(test_path_collisions);
  RUN_TEST(test_path_none_free);
  RUN_TEST(test_path_no_fit);
  RUN_TEST(test_bake_lanes);
  RUN_TEST(test_bake_lanes_sixteen);
  RUN_TEST(test_bake_lanes_shared_note);
  RUN_TEST(test_lane_to_sample);
  return UNITY_END();
}
