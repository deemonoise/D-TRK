#include <unity.h>
#include <initializer_list>
#include "model.h"
#include "voice_cost.h"
#include "bytebeat_lib.h"
#include "synth_lofi.h"

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
  m.fltMode = static_cast<uint8_t>(FltMode::Sid);
  TEST_ASSERT_EQUAL(157, voiceCost(m, false));  // measured +38 over LP
}

// CHIP waves: PULSE / TRI / SAW run the templated loops (the base); the rest the generic loop (+10),
// SID TS +31 over SAW; AY TN uses clock buckets (unknown frequency reserves the extreme).
static int chipWave(Wave w, bool step = false) {
  Instrument m = of(InstrType::Chip);
  m.wave = static_cast<uint8_t>(w);
  m.chipStep = step;
  return voiceCost(m, false);
}

void test_chip_wave_costs() {
  TEST_ASSERT_EQUAL(119, chipWave(Wave::Pulse));
  TEST_ASSERT_EQUAL(119, chipWave(Wave::Triangle));
  TEST_ASSERT_EQUAL(119, chipWave(Wave::Saw));
  for (Wave w : {Wave::Noise, Wave::Metal, Wave::Wt1, Wave::SidNoise, Wave::AySaw, Wave::AyTri, Wave::AyNoise}) {
    TEST_ASSERT_EQUAL(119 + kCostChipGeneric, chipWave(w));
  }
  TEST_ASSERT_EQUAL(119 + kCostChipGeneric, chipWave(static_cast<Wave>(static_cast<int>(Wave::Wt1) + 15)));
  for (Wave w : {Wave::SidTs, Wave::SidPs, Wave::SidPt, Wave::SidPts}) TEST_ASSERT_EQUAL(150, chipWave(w));
  TEST_ASSERT_EQUAL(kCostChip + kCostChipAyTnExtreme, chipWave(Wave::AyToneNoise));
  TEST_ASSERT_EQUAL(10, kCostChipGeneric);
  TEST_ASSERT_EQUAL(31, kCostChipSid);
  TEST_ASSERT_EQUAL(80, kCostChipAyTn);
}

// STEP: every wave in the generic loop (PULSE too), plus the chunked quantiser.
void test_chip_step_cost() {
  TEST_ASSERT_TRUE(kCostChipStep > 0);
  TEST_ASSERT_EQUAL(119 + kCostChipGeneric + kCostChipStep, chipWave(Wave::Pulse, true));
  TEST_ASSERT_EQUAL(119 + kCostChipGeneric + kCostChipStep, chipWave(Wave::Noise, true));
  TEST_ASSERT_EQUAL(150 + kCostChipStep, chipWave(Wave::SidTs, true));
}

// The CHIP wave / STEP add-ons are CHIP's: a KIT or another type keeps its price whatever wave holds.
void test_chip_wave_only_on_chip() {
  Instrument m = of(InstrType::Kit);
  m.wave = static_cast<uint8_t>(Wave::SidTs);
  m.chipStep = true;
  TEST_ASSERT_EQUAL(kCostChip, voiceCost(m, false));
  m.type = InstrType::Fm;
  TEST_ASSERT_EQUAL(kCostFm, voiceCost(m, false));
  m.type = InstrType::Chip;
  TEST_ASSERT_EQUAL(kCostSample, voiceCost(m, true));  // a sample note of a CHIP instrument
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
  TEST_ASSERT_EQUAL(200, voiceCost(m, false));
}

void test_fm_drum() {
  TEST_ASSERT_EQUAL(225, voiceCost(of(InstrType::Fm), false));
  TEST_ASSERT_EQUAL(kCostDrum[0], voiceCost(of(InstrType::Drum), false));  // machine 0: BD8
}

void test_drum2_costs() {
  Instrument m = of(InstrType::Drum);
  for (int mc = 0; mc < kDrum2First; ++mc) {
    m.machine = static_cast<uint8_t>(mc);
    TEST_ASSERT_EQUAL(kCostDrum[mc], voiceCost(m, false));
    TEST_ASSERT_TRUE(kCostDrum[mc] >= 200 && kCostDrum[mc] <= 600);  // bench-max: about the old 290
  }
  for (int k = 0; k < static_cast<int>(DrumMachine::Count) - kDrum2First; ++k) {
    m.machine = static_cast<uint8_t>(kDrum2First + k);
    if (m.machine == static_cast<uint8_t>(DrumMachine::Unused22)) {  // was BDF: costs as BDA
      TEST_ASSERT_EQUAL(kCostDrum2[0], voiceCost(m, false));
      continue;
    }
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
  m.fltMode = static_cast<uint8_t>(FltMode::Sid);
  TEST_ASSERT_EQUAL(kCostSample + kCostSid, voiceCost(m, true));
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

void test_synth_modulators_reserve_fold_and_ring() {
  Instrument m = of(InstrType::Synth);
  m.synEnvDest = static_cast<uint8_t>(SynEnvDest::Fold);
  m.macro[kMacSenv] = 64; // A CON lock can move this neutral base during the note.
  TEST_ASSERT_EQUAL(kCostSynBl + kCostFold, voiceCost(m, false));
  m.synEnvDest = static_cast<uint8_t>(SynEnvDest::Ring);
  TEST_ASSERT_EQUAL(kCostSynBl + kCostRing, voiceCost(m, false));
  m.lfoDest = static_cast<uint8_t>(LfoDest::Fold);
  m.lfoDepth = 0; // A depth lock can enable this configured destination.
  TEST_ASSERT_EQUAL(kCostSynBl + kCostFold + kCostRing, voiceCost(m, false));
  m.synFold = m.synRing = 127;
  TEST_ASSERT_EQUAL(kCostSynBl + kCostFold + kCostRing, voiceCost(m, false));
}

void test_potential_mask_and_lfo_targets_do_not_double_charge() {
  Instrument m = of(InstrType::Synth);
  const uint8_t both=kCostPotentialFold|kCostPotentialRing;
  TEST_ASSERT_EQUAL(kCostSynBl+kCostFold+kCostRing, voiceCost(m,false,both));
  m.synFold=1; m.synRing=1;
  TEST_ASSERT_EQUAL(kCostSynBl+kCostFold+kCostRing, voiceCost(m,false,both));
  TEST_ASSERT_EQUAL(kCostSample, voiceCost(m,true,both));
  m.type=InstrType::Chip;
  TEST_ASSERT_EQUAL(kCostChip, voiceCost(m,false,both));
  m=of(InstrType::Synth);
  m.lfo[2].dest=static_cast<uint8_t>(LfoDest::Ring);
  TEST_ASSERT_EQUAL(kCostSynBl+kCostRing,voiceCost(m,false));
}
void test_fm_sat_base_lfo_and_per_note_cost() {
  Instrument m=of(InstrType::Fm);
  TEST_ASSERT_EQUAL(kCostFm,voiceCost(m,false));
  m.fmSat=1;
  TEST_ASSERT_EQUAL(kCostFm+kCostFmSat,voiceCost(m,false));
  m.lfoDest=static_cast<uint8_t>(LfoDest::Sat);
  TEST_ASSERT_EQUAL(kCostFm+kCostFmSat,voiceCost(m,false,kCostPotentialSat));
  m.fmSat=0;
  TEST_ASSERT_EQUAL(kCostFm+kCostFmSat,voiceCost(m,false));
  m.lfoDest=static_cast<uint8_t>(LfoDest::Pitch);
  TEST_ASSERT_EQUAL(kCostFm+kCostFmSat,voiceCost(m,false,kCostPotentialSat));
}

void test_byte_score_boot_metadata_and_no_lazy_initialization() {
  // The first cost lookup must not compile bytecode on an audio interrupt.
  TEST_ASSERT_EQUAL_UINT16(kByteWorkScoreMax, byteWorkScore(32));
  const int n=byteFormulaCount();
  for(int k=0;k<n;++k) {
    const auto& code=byteCode(k);unsigned score=0;
    for(int j=0;j<code.len;++j) {
      const uint8_t op=code.ops[j];
      score+=(op==static_cast<uint8_t>(ByteOp::Div)||op==static_cast<uint8_t>(ByteOp::Mod))?5:1;
      j+=byteOpImm(op);
    }
    TEST_ASSERT_EQUAL_UINT16(score,byteWorkScore(k));
  }
  TEST_ASSERT_EQUAL_UINT16(53,byteWorkScore(32));
  TEST_ASSERT_EQUAL_UINT16(byteWorkScore(0),byteWorkScore(-1));
  TEST_ASSERT_EQUAL_UINT16(byteWorkScore(0),byteWorkScore(n));
}
void test_lofi_byte_cost_scales_with_formula_and_evaluation_rate() {
  byteFormulaCount();Instrument m=of(InstrType::Lofi);
  m.machine=static_cast<uint8_t>(LofiMachine::Byte);m.lofiFormula=32;
  const float middleC=261.625565f;
  const int full=voiceCost(m,false,0,20000.f), middle=voiceCost(m,false,0,middleC);
  TEST_ASSERT_EQUAL(kCostLofiByte,full);
  TEST_ASSERT_EQUAL(full,voiceCost(m,false)); // Unknown pitch is conservative.
  TEST_ASSERT_EQUAL(full,voiceCost(m,false,0,-1.f));
  TEST_ASSERT_TRUE(middle<full);
  TEST_ASSERT_TRUE(voiceCost(m,false,0,middleC*.5f)<middle);
  // Adjacent small bends in the same evaluation-rate bucket keep one price.
  TEST_ASSERT_EQUAL(middle,voiceCost(m,false,0,middleC*1.001f));
  m.lofiFormula=0;TEST_ASSERT_TRUE(voiceCost(m,false,0,20000.f)<full);
  const int zero=voiceCost(m,false,0,20000.f);m.lofiFormula=255;
  TEST_ASSERT_EQUAL(zero,voiceCost(m,false,0,20000.f));
  m.machine=static_cast<uint8_t>(LofiMachine::Apc);
  TEST_ASSERT_EQUAL(kCostLofiApc,voiceCost(m,false,0,1.f));
  TEST_ASSERT_EQUAL(kCostLofiApc,voiceCost(m,false,0,20000.f));
}

void test_latched_engine_type_and_machine_override_live_instrument() {
  byteFormulaCount();Instrument m=of(InstrType::Lofi);m.lofiFormula=32;
  m.machine=static_cast<uint8_t>(LofiMachine::Apc);
  TEST_ASSERT_EQUAL(kCostLofiByte,voiceCost(m,false,0,20000.f,InstrType::Lofi,static_cast<uint8_t>(LofiMachine::Byte)));
  TEST_ASSERT_EQUAL(kCostLofiApc,voiceCost(m,false,0,20000.f));
  m=of(InstrType::Drum);m.machine=static_cast<uint8_t>(DrumMachine::Bda);
  TEST_ASSERT_EQUAL(kCostDrum2[7],voiceCost(m,false,0,0,InstrType::Drum,static_cast<uint8_t>(DrumMachine::Hhr)));
  m=of(InstrType::Chip);
  TEST_ASSERT_EQUAL(kCostFm,voiceCost(m,false,0,0,InstrType::Fm));
  m.fltMode=static_cast<uint8_t>(FltMode::Comb);
  TEST_ASSERT_EQUAL(kCostString[1],voiceCost(m,false,0,0,InstrType::String,static_cast<uint8_t>(StringModel::Modal)));
  TEST_ASSERT_EQUAL(kCostSample+kCostComb,voiceCost(m,true,0,0,InstrType::String,static_cast<uint8_t>(StringModel::Modal)));
}

void test_ay_tone_noise_cost_tracks_effective_clock() {
  Instrument m=of(InstrType::Chip);m.wave=static_cast<uint8_t>(Wave::AyToneNoise);
  TEST_ASSERT_EQUAL(199,voiceCost(m,false,0,261.625565f));
  TEST_ASSERT_EQUAL(199,voiceCost(m,false,0,44100.f/8));
  TEST_ASSERT_EQUAL(249,voiceCost(m,false,0,44100.f/8+1));
  TEST_ASSERT_EQUAL(249,voiceCost(m,false,0,44100.f*2));
  TEST_ASSERT_EQUAL(349,voiceCost(m,false,0,44100.f*2+1));
  TEST_ASSERT_EQUAL(349,voiceCost(m,false));
  TEST_ASSERT_EQUAL(349,voiceCost(m,false,0,-1));
  m.type=InstrType::Fm;
  TEST_ASSERT_EQUAL(199,voiceCost(m,false,0,261.625565f,InstrType::Chip));
  TEST_ASSERT_EQUAL(kCostSample,voiceCost(m,true,0,200000));
  m.type=InstrType::Chip;m.wave=static_cast<uint8_t>(Wave::Saw);
  TEST_ASSERT_EQUAL(kCostChip,voiceCost(m,false,0,200000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_byte_score_boot_metadata_and_no_lazy_initialization);
  RUN_TEST(test_ay_tone_noise_cost_tracks_effective_clock);
  RUN_TEST(test_lofi_byte_cost_scales_with_formula_and_evaluation_rate);
  RUN_TEST(test_latched_engine_type_and_machine_override_live_instrument);
  RUN_TEST(test_synth_modulators_reserve_fold_and_ring);
  RUN_TEST(test_potential_mask_and_lfo_targets_do_not_double_charge);
  RUN_TEST(test_fm_sat_base_lfo_and_per_note_cost);
  RUN_TEST(test_chip_lp_is_base);
  RUN_TEST(test_chip_filter_addons);
  RUN_TEST(test_chip_wave_costs);
  RUN_TEST(test_chip_step_cost);
  RUN_TEST(test_chip_wave_only_on_chip);
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
