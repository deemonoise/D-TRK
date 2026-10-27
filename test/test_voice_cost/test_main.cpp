#include <unity.h>
#include <initializer_list>
#include "model.h"
#include "voice_cost.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static Instrument of(InstrType t) {
  Instrument m;
  m.type = t;
  return m;
}

// The filter is an add-on over filter off, the same for LP / BP / HP (one SVF).
void test_chip_filter_addon() {
  Instrument m = of(InstrType::Chip);
  m.fltMode = static_cast<uint8_t>(FltMode::Off);
  TEST_ASSERT_EQUAL(kCostChip, voiceCost(m, false));
  for (FltMode f : {FltMode::Lp, FltMode::Bp, FltMode::Hp}) {
    m.fltMode = static_cast<uint8_t>(f);
    TEST_ASSERT_EQUAL(kCostChip + kCostFilter, voiceCost(m, false));
  }
  m.fltMode = static_cast<uint8_t>(FltMode::Count);  // out of range plays as off
  TEST_ASSERT_EQUAL(kCostChip, voiceCost(m, false));
}

// CHIP waves: PULSE / TRI / SAW run the templated loops (the base), the rest the generic loop.
static int chipWave(Wave w) {
  Instrument m = of(InstrType::Chip);
  m.wave = static_cast<uint8_t>(w);
  return voiceCost(m, false);
}

void test_chip_wave_costs() {
  for (Wave w : {Wave::Pulse, Wave::Triangle, Wave::Saw}) TEST_ASSERT_EQUAL(kCostChip, chipWave(w));
  for (Wave w : {Wave::Noise, Wave::Metal, Wave::Wt1}) TEST_ASSERT_EQUAL(kCostChip + kCostChipGeneric, chipWave(w));
  TEST_ASSERT_EQUAL(kCostChip + kCostChipGeneric, chipWave(static_cast<Wave>(kWaveCount - 1)));
}

// The CHIP wave add-on is CHIP's: another type keeps its price whatever wave it holds.
void test_chip_wave_only_on_chip() {
  Instrument m = of(InstrType::Kit);
  m.wave = static_cast<uint8_t>(Wave::Noise);
  TEST_ASSERT_EQUAL(kCostChip, voiceCost(m, false));
  m.type = InstrType::Fm;
  TEST_ASSERT_EQUAL(kCostFm[0], voiceCost(m, false));
  m.type = InstrType::Chip;
  TEST_ASSERT_EQUAL(kCostSample, voiceCost(m, true));  // a sample note of a CHIP instrument
}

void test_synth_wt_or_bl() {
  Instrument m = of(InstrType::Synth);
  m.synOsc[0] = static_cast<uint8_t>(SynOsc::Saw);
  m.synOsc[1] = static_cast<uint8_t>(SynOsc::Square);
  TEST_ASSERT_EQUAL(kCostSynBl, voiceCost(m, false));
  m.synOsc[1] = static_cast<uint8_t>(SynOsc::Wt);
  TEST_ASSERT_EQUAL(kCostSynWt, voiceCost(m, false));
  m.synOsc[0] = static_cast<uint8_t>(SynOsc::Wt);
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  TEST_ASSERT_EQUAL(kCostSynWt + kCostFilter, voiceCost(m, false));
}

void test_fm_by_machine() {
  Instrument m = of(InstrType::Fm);
  for (int k = 0; k < static_cast<int>(FmMachine::Count); ++k) {
    m.machine = static_cast<uint8_t>(k);
    TEST_ASSERT_EQUAL(kCostFm[k], voiceCost(m, false));
  }
  m.machine = 200;  // out of range plays KICK
  TEST_ASSERT_EQUAL(kCostFm[0], voiceCost(m, false));
}

void test_drum_by_machine() {
  Instrument m = of(InstrType::Drum);
  for (int k = 0; k < static_cast<int>(DrumMachine::Count); ++k) {
    m.machine = static_cast<uint8_t>(k);
    TEST_ASSERT_EQUAL(kCostDrum[k], voiceCost(m, false));
  }
  m.machine = 200;
  TEST_ASSERT_EQUAL(kCostDrum[0], voiceCost(m, false));
}

void test_lofi_by_machine() {
  Instrument m = of(InstrType::Lofi);
  m.machine = static_cast<uint8_t>(LofiMachine::Byte);
  TEST_ASSERT_EQUAL(kCostLofiByte, voiceCost(m, false));
  m.machine = static_cast<uint8_t>(LofiMachine::Apc);
  TEST_ASSERT_EQUAL(kCostLofiApc, voiceCost(m, false));
}

void test_kit() { TEST_ASSERT_EQUAL(kCostChip, voiceCost(of(InstrType::Kit), false)); }

void test_sample_base_and_filter() {
  Instrument m = of(InstrType::Sample);
  TEST_ASSERT_EQUAL(kCostSample, voiceCost(m, true));
  // SAMPLE voices run the instrument's filter too (controlFilter): the same add-on.
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  TEST_ASSERT_EQUAL(kCostSample + kCostFilter, voiceCost(m, true));
  // A KIT sampler lane (filter off): the flag decides, not the type.
  Instrument k = of(InstrType::Kit);
  TEST_ASSERT_EQUAL(kCostSample, voiceCost(k, true));
}

// Every price is positive and fits Voice::cost; the budget plus the fixed load is the target, under
// the CPU guard's 80 %.
void test_esp_budget_leaves_room() {
  TEST_ASSERT_EQUAL(kLoadTargetEsp, kVoiceBudgetEsp + kCostFixedEsp);
  TEST_ASSERT_TRUE(kLoadTargetEsp < 8000);
  TEST_ASSERT_TRUE(kVoiceBudgetEsp > 0);
  for (int c : {kCostChip, kCostSample, kCostSynBl, kCostSynWt, kCostLofiByte, kCostLofiApc}) {
    TEST_ASSERT_TRUE(c > 0 && c < 65535);
  }
  for (int c : kCostFm) TEST_ASSERT_TRUE(c > 0 && c < 65535);
  for (int c : kCostDrum) TEST_ASSERT_TRUE(c > 0 && c < 65535);
  TEST_ASSERT_TRUE(kCostChipGeneric >= 0 && kCostFilter >= 0);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_chip_filter_addon);
  RUN_TEST(test_chip_wave_costs);
  RUN_TEST(test_chip_wave_only_on_chip);
  RUN_TEST(test_synth_wt_or_bl);
  RUN_TEST(test_fm_by_machine);
  RUN_TEST(test_drum_by_machine);
  RUN_TEST(test_lofi_by_machine);
  RUN_TEST(test_kit);
  RUN_TEST(test_sample_base_and_filter);
  RUN_TEST(test_esp_budget_leaves_room);
  return UNITY_END();
}
