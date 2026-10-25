#include <unity.h>
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

void test_chip_lp_is_base() {
  Instrument m = of(InstrType::Chip);
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  TEST_ASSERT_EQUAL(119, voiceCost(m, false));
  m.fltMode = static_cast<uint8_t>(FltMode::Off);
  TEST_ASSERT_EQUAL(119, voiceCost(m, false));
}

void test_chip_filter_addons() {
  Instrument m = of(InstrType::Chip);
  m.fltMode = static_cast<uint8_t>(FltMode::Ladder);
  TEST_ASSERT_EQUAL(154, voiceCost(m, false));
  m.fltMode = static_cast<uint8_t>(FltMode::Ms20);
  TEST_ASSERT_EQUAL(142, voiceCost(m, false));
  m.fltMode = static_cast<uint8_t>(FltMode::Comb);
  TEST_ASSERT_EQUAL(153, voiceCost(m, false));
  m.fltMode = static_cast<uint8_t>(FltMode::Formant);
  TEST_ASSERT_EQUAL(159, voiceCost(m, false));
}

void test_synth_wt_addons_only_when_on() {
  Instrument m = of(InstrType::Synth);
  m.synOsc[0] = static_cast<uint8_t>(SynOsc::Wt);
  TEST_ASSERT_EQUAL(240, voiceCost(m, false));
  m.synFold = 100;
  TEST_ASSERT_EQUAL(279, voiceCost(m, false));
  m.synRing = 64;
  m.fltMode = static_cast<uint8_t>(FltMode::Ladder);
  TEST_ASSERT_EQUAL(323, voiceCost(m, false));
}

void test_synth_wt_on_osc_two() {
  Instrument m = of(InstrType::Synth);
  m.synOsc[1] = static_cast<uint8_t>(SynOsc::Wt);
  TEST_ASSERT_EQUAL(240, voiceCost(m, false));
}

void test_synth_bl() {
  Instrument m = of(InstrType::Synth);
  m.synOsc[0] = static_cast<uint8_t>(SynOsc::Saw);
  m.synOsc[1] = static_cast<uint8_t>(SynOsc::Square);
  TEST_ASSERT_EQUAL(130, voiceCost(m, false));
}

void test_fm_drum() {
  TEST_ASSERT_EQUAL(195, voiceCost(of(InstrType::Fm), false));
  TEST_ASSERT_EQUAL(290, voiceCost(of(InstrType::Drum), false));
}

void test_drum2_costs() {
  Instrument m = of(InstrType::Drum);
  for (int mc = 0; mc < kDrum2First; ++mc) {
    m.machine = static_cast<uint8_t>(mc);
    TEST_ASSERT_EQUAL(kCostDrum, voiceCost(m, false));
  }
  for (int k = 0; k < static_cast<int>(DrumMachine::Count) - kDrum2First; ++k) {
    m.machine = static_cast<uint8_t>(kDrum2First + k);
    TEST_ASSERT_EQUAL(kCostDrum2[k], voiceCost(m, false));
    TEST_ASSERT_TRUE(kCostDrum2[k] >= 200 && kCostDrum2[k] <= 600);  // bench-max: about the old DRUM
  }
}

void test_string_by_model() {
  const int want[] = {203, 228};
  Instrument m = of(InstrType::String);
  for (int k = 0; k < static_cast<int>(StringModel::Count); ++k) {
    m.machine = static_cast<uint8_t>(k);
    TEST_ASSERT_EQUAL(want[k], voiceCost(m, false));
  }
  m.machine = 200;  // out of range clamps to the last model
  TEST_ASSERT_EQUAL(kCostString[static_cast<int>(StringModel::Count) - 1], voiceCost(m, false));
}

void test_kit() { TEST_ASSERT_EQUAL(kCostChip, voiceCost(of(InstrType::Kit), false)); }

// COMB on STRING renders as filter Off (the voice's line is the string's): no add-on.
void test_string_comb_costs_nothing() {
  Instrument m = of(InstrType::String);
  m.fltMode = static_cast<uint8_t>(FltMode::Comb);
  TEST_ASSERT_EQUAL(kCostString[0], voiceCost(m, false));
  m.fltMode = static_cast<uint8_t>(FltMode::Ladder);
  TEST_ASSERT_EQUAL(kCostString[0] + kCostLadder, voiceCost(m, false));
}

void test_sample_base_and_filter() {
  Instrument m = of(InstrType::Sample);
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  TEST_ASSERT_EQUAL(kCostSample, voiceCost(m, true));
  // SAMPLE voices run the instrument's filter too (controlFilter): the same add-ons.
  m.fltMode = static_cast<uint8_t>(FltMode::Ladder);
  TEST_ASSERT_EQUAL(kCostSample + kCostLadder, voiceCost(m, true));
  m.fltMode = static_cast<uint8_t>(FltMode::Formant);
  TEST_ASSERT_EQUAL(kCostSample + kCostFormant, voiceCost(m, true));
  // A KIT sampler lane (filter off): the flag decides, not the type.
  Instrument k = of(InstrType::Kit);
  k.fltMode = static_cast<uint8_t>(FltMode::Off);
  TEST_ASSERT_EQUAL(kCostSample, voiceCost(k, true));
}

// The Teensy budget plus the load around the voices (fx, track strips) stays at the target, <= 75 %:
// at 7000 the bench's 32-voice worst case settled at ~77.5 % and the CPU guard shed.
void test_teensy_budget_leaves_room() {
  TEST_ASSERT_EQUAL(kLoadTargetTeensy, kVoiceBudgetTeensy + kCostFixedTeensy);
  TEST_ASSERT_TRUE(kLoadTargetTeensy <= 7500);
  // 30 live voices of the worst case (14 WT fold + ring + LADDER, 16 CHIP LADDER) do not fit.
  TEST_ASSERT_TRUE(14 * (kCostSynWt + kCostFold + kCostRing + kCostLadder) + 16 * (kCostChip + kCostLadder) >
                   kVoiceBudgetTeensy);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_chip_lp_is_base);
  RUN_TEST(test_chip_filter_addons);
  RUN_TEST(test_synth_wt_addons_only_when_on);
  RUN_TEST(test_synth_wt_on_osc_two);
  RUN_TEST(test_synth_bl);
  RUN_TEST(test_fm_drum);
  RUN_TEST(test_drum2_costs);
  RUN_TEST(test_string_by_model);
  RUN_TEST(test_kit);
  RUN_TEST(test_string_comb_costs_nothing);
  RUN_TEST(test_sample_base_and_filter);
  RUN_TEST(test_teensy_budget_leaves_room);
  return UNITY_END();
}
