#include <unity.h>
#include <memory>
#include "voice_cost.h"
#include "synth.h"
using namespace mt;
void setUp() {}
void tearDown() {}
static void put(Instrument& m,int at,ModSource s,LfoDest d,int amount=0,ModSource via=ModSource::Off) {
  m.mod[at]={uint8_t(s),uint8_t(d),int8_t(amount),uint8_t(ModPolarity::Native),uint8_t(via)};
}
void test_no_routes_preserve_engine_prices_and_legacy_lfo_price() {
  Instrument m;instrSetType(m,InstrType::Chip);
  TEST_ASSERT_EQUAL(kCostChip,voiceCost(m,false));
  m.lfoDepth=63;TEST_ASSERT_EQUAL(kCostChip,voiceCost(m,false));
}
void test_zero_amount_routes_are_reserved_for_signed_locks() {
  Instrument m;instrSetType(m,InstrType::Chip);
  put(m,0,ModSource::Velocity,LfoDest::Pan);
  TEST_ASSERT_EQUAL(kCostChip+224+8,voiceCost(m,false));
  for(int i=1;i<8;++i)put(m,i,ModSource::Velocity,LfoDest::Pan);
  TEST_ASSERT_EQUAL(kCostChip+224+8*8,voiceCost(m,false));
}
void test_raw_lfo_sources_and_via_are_charged_once_each() {
  Instrument m;instrSetType(m,InstrType::Chip);
  put(m,0,ModSource::Velocity,LfoDest::Pan,1,ModSource::Lfo1);
  put(m,1,ModSource::Lfo1,LfoDest::Vol);
  TEST_ASSERT_EQUAL(kCostChip+224+16+112,voiceCost(m,false));
  m.mod[1].source=uint8_t(ModSource::Lfo2);
  TEST_ASSERT_EQUAL(kCostChip+224+16+224,voiceCost(m,false));
}
void test_costly_source_masks_and_parameter_work_are_bounded() {
  Instrument m;instrSetType(m,InstrType::Chip);
  put(m,0,ModSource::FilterEnv,LfoDest::Pan,1,ModSource::Random);
  put(m,1,ModSource::Follower,LfoDest::Vol);
  TEST_ASSERT_EQUAL(kCostChip+224+16+3*24,voiceCost(m,false));
  m.mod[1].dest=uint8_t(LfoDest::Drive);
  TEST_ASSERT_EQUAL(kCostChip+224+16+3*24+128,voiceCost(m,false));
}
void test_disabled_or_invalid_routes_do_not_reserve_voices() {
  Instrument m;instrSetType(m,InstrType::Chip);
  put(m,0,ModSource::Velocity,LfoDest::Count);put(m,1,ModSource::Velocity,LfoDest::Rtrg1);
  put(m,2,ModSource::Off,LfoDest::Sat);put(m,3,ModSource::Off,LfoDest::Pan);
  put(m,4,ModSource::Count,LfoDest::Pan);
  TEST_ASSERT_EQUAL(kCostChip,voiceCost(m,false));
}
void test_unconsumed_destinations_still_reserve_runtime_source_work() {
  Instrument m;instrSetType(m,InstrType::Chip);
  put(m,0,ModSource::Velocity,LfoDest::Fold);put(m,1,ModSource::Velocity,LfoDest::Ring);
  put(m,2,ModSource::Velocity,LfoDest::Sat);
  TEST_ASSERT_EQUAL(kCostChip+224+3*8+128,voiceCost(m,false));
}
void test_full_supported_matrix_reserves_more_than_measured_peak_increment() {
  Instrument m;instrSetType(m,InstrType::Chip);
  for(int i=0;i<8;++i)put(m,i,static_cast<ModSource>(1+i%4),LfoDest::Pan);
  m.mod[4].via=uint8_t(ModSource::FilterEnv);m.mod[5].via=uint8_t(ModSource::Follower);
  m.mod[6].via=uint8_t(ModSource::Random);m.mod[7].dest=uint8_t(LfoDest::Pitch);
  TEST_ASSERT_EQUAL(kCostChip+936,voiceCost(m,false));
  TEST_ASSERT_GREATER_THAN(166, (voiceCost(m,false)-kCostChip)*4/10);
}
void test_live_matrix_edits_reclaim_voices_and_release_without_a_stuck_tail() {
  auto p=std::make_unique<Project>();auto& m=p->instruments[0];instrSetType(m,InstrType::Chip);
  m.mono=false;m.wave=uint8_t(Wave::Saw);m.vol=64;m.attack=0;m.decay=0;m.sustain=127;m.release=0;
  for(auto& tr:p->tracks){tr.out=TrackOut::Int;tr.instr=0;tr.vol=127;}
  auto synth=std::make_unique<Synth>(*p);synth->setVoiceBudget(8*kCostChip);
  int16_t pcm[Synth::kBlock];
  for(int t=0;t<8;++t){const uint8_t msg[]={0x90,uint8_t(48+t),100};TEST_ASSERT_TRUE(synth->event(0,t,msg,3));}
  synth->render(pcm);TEST_ASSERT_EQUAL(8,synth->activeVoices());
  for(int i=0;i<8;++i)put(m,i,ModSource::Velocity,LfoDest::Pan);
  for(int b=0;b<8;++b)synth->render(pcm);
  TEST_ASSERT_EQUAL((8*kCostChip)/(kCostChip+288),synth->activeVoices());
  bool audible=false;for(auto v:pcm)audible|=v!=0;TEST_ASSERT_TRUE(audible);
  for(int t=0;t<8;++t){const uint8_t msg[]={0x80,uint8_t(48+t),0};TEST_ASSERT_TRUE(synth->event(0,t,msg,3));}
  for(int b=0;b<32;++b)synth->render(pcm);
  TEST_ASSERT_EQUAL(0,synth->activeVoices());for(auto v:pcm)TEST_ASSERT_EQUAL_INT16(0,v);
}
void test_type_change_reserves_new_matrix_work_on_existing_chip_voices() {
  auto p=std::make_unique<Project>();auto& m=p->instruments[0];instrSetType(m,InstrType::Chip);
  m.mono=false;m.wave=uint8_t(Wave::Saw);m.vol=64;m.attack=0;m.decay=0;m.sustain=127;m.release=0;
  for(auto& tr:p->tracks){tr.out=TrackOut::Int;tr.instr=0;tr.vol=127;}
  auto synth=std::make_unique<Synth>(*p);synth->setVoiceBudget(8*kCostChip);
  int16_t pcm[Synth::kBlock];
  for(int t=0;t<8;++t){const uint8_t msg[]={0x90,uint8_t(48+t),100};TEST_ASSERT_TRUE(synth->event(0,t,msg,3));}
  synth->render(pcm);TEST_ASSERT_EQUAL(8,synth->activeVoices());
  instrSetType(m,InstrType::Fm);
  for(int i=0;i<8;++i)put(m,i,static_cast<ModSource>(1+i%4),LfoDest::Sat,63);
  // The already sounding engine remains CHIP, but all four matrix LFOs still run.
  for(int b=0;b<8;++b)synth->render(pcm);
  TEST_ASSERT_EQUAL((8*kCostChip)/(kCostChip+864),synth->activeVoices());
  TEST_ASSERT_EQUAL(kCostChip+864,voiceCost(m,false,int(InstrType::Chip),0));
}
int main(){UNITY_BEGIN();RUN_TEST(test_no_routes_preserve_engine_prices_and_legacy_lfo_price);RUN_TEST(test_zero_amount_routes_are_reserved_for_signed_locks);RUN_TEST(test_raw_lfo_sources_and_via_are_charged_once_each);RUN_TEST(test_costly_source_masks_and_parameter_work_are_bounded);RUN_TEST(test_disabled_or_invalid_routes_do_not_reserve_voices);RUN_TEST(test_unconsumed_destinations_still_reserve_runtime_source_work);RUN_TEST(test_full_supported_matrix_reserves_more_than_measured_peak_increment);RUN_TEST(test_live_matrix_edits_reclaim_voices_and_release_without_a_stuck_tail);RUN_TEST(test_type_change_reserves_new_matrix_work_on_existing_chip_voices);return UNITY_END();}
