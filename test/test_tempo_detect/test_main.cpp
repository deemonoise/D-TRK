#include <math.h>
#include <unity.h>
#include <vector>
#include "tempo_detect.h"
using namespace mt;
void setUp() {}
void tearDown() {}
static std::vector<Onset> beats(double bpm, uint32_t rate, int n=24, uint32_t offset=0, bool jitter=false) {
  std::vector<Onset> out;
  for (int i=0;i<n;++i) {
    const int shift=jitter ? (i%3-1)*static_cast<int>(rate/1000) : 0;
    out.push_back({static_cast<uint32_t>(offset + lround(i*60.*rate/bpm) + shift + (jitter?rate/1000:0)),
                   static_cast<uint16_t>(i%4==0?1000:700)});
  }
  return out;
}
static TempoEstimate detect(const std::vector<Onset>& o,uint32_t rate) {
 return estimateTempo(o.data(),o.size(),rate,o.empty()?0:o.back().pos+rate/2);
}
static void test_regular_tempos_at_all_sample_rates() {
 for (uint32_t rate:{22050u,32000u,44100u}) for(double bpm:{60.,90.,120.,174.,240.}) {
  const auto result=detect(beats(bpm,rate),rate);
  TEST_ASSERT_UINT16_WITHIN(3,static_cast<int>(bpm*10),result.bpm10);
  TEST_ASSERT_GREATER_OR_EQUAL(800,result.confidence);
  TEST_ASSERT_LESS_OR_EQUAL(1000,result.confidence);
  TEST_ASSERT_EQUAL(result.bpm10/2,result.halfBpm10);
  TEST_ASSERT_EQUAL(result.bpm10*2,result.doubleBpm10);
  TEST_ASSERT_TRUE(result.ambiguous);
 }
}
static void test_fractional_tempo_and_timestamp_shift() {
 const auto a=detect(beats(123.4,44100,64),44100);
 const auto b=detect(beats(123.4,44100,64,1000000000u),44100);
 TEST_ASSERT_UINT16_WITHIN(5,1234,a.bpm10);
 TEST_ASSERT_EQUAL(a.bpm10,b.bpm10); TEST_ASSERT_EQUAL(a.confidence,b.confidence);
}
static void test_jitter_and_missing_beats_keep_tempo() {
 auto o=beats(120,44100,32,1000,true);
 o.erase(o.begin()+24);o.erase(o.begin()+17);o.erase(o.begin()+10);o.erase(o.begin()+3);
 const auto result=detect(o,44100);
 TEST_ASSERT_UINT16_WITHIN(6,1200,result.bpm10);
 TEST_ASSERT_GREATER_OR_EQUAL(650,result.confidence);
}
static void test_swing_still_exposes_tempo_and_ambiguity() {
 std::vector<Onset> o;
 for(int beat=0;beat<16;++beat){
  o.push_back({static_cast<uint32_t>(beat*22050),1000});
  o.push_back({static_cast<uint32_t>(beat*22050+14700),600});
 }
 const auto result=detect(o,44100);
 TEST_ASSERT_UINT16_WITHIN(10,1200,result.bpm10);
 TEST_ASSERT_TRUE(result.ambiguous);
}
static void test_silence_single_and_too_short_are_unknown() {
 TEST_ASSERT_EQUAL(0,estimateTempo(nullptr,0,44100,441000).bpm10);
 Onset o[]={{0,1000},{10,1000},{20,1000},{30,1000}};
 TEST_ASSERT_EQUAL(0,estimateTempo(o,1,44100,441000).bpm10);
 TEST_ASSERT_EQUAL(0,estimateTempo(o,4,44100,441000).bpm10);
 TEST_ASSERT_EQUAL(0,estimateTempo(o,4,0,441000).bpm10);
}
static void test_irregular_onsets_are_unknown() {
 std::vector<Onset> o;uint32_t seed=11,pos=0;
 for(int i=0;i<64;++i){seed=seed*1664525u+1013904223u;pos+=5000+(seed%33000);o.push_back({pos,1000});}
 const auto result=detect(o,44100);
 TEST_ASSERT_EQUAL(0,result.bpm10); TEST_ASSERT_EQUAL(0,result.confidence);
 TEST_ASSERT_FALSE(result.ambiguous);
}
static void test_bad_candidates_do_not_read_past_or_produce_a_tempo() {
 auto o=beats(120,44100);
 TEST_ASSERT_EQUAL(0,estimateTempo(nullptr,24,44100,1000000).bpm10);
 TEST_ASSERT_EQUAL(0,estimateTempo(o.data(),129,44100,1000000).bpm10);
 TEST_ASSERT_EQUAL(0,estimateTempo(o.data(),o.size(),44100,o.back().pos).bpm10);
 o[5].pos=o[4].pos;
 TEST_ASSERT_EQUAL(0,detect(o,44100).bpm10);
}
static void test_weak_candidates_do_not_fake_confidence() {
 auto o=beats(120,44100);
 for(auto& onset:o)onset.strength=1;
 TEST_ASSERT_EQUAL(0,detect(o,44100).bpm10);
}
static void test_few_irregular_attacks_do_not_overfit_a_tempo() {
 for (int count : {4,8,16,32,64}) for(uint32_t start=1;start<=16;++start) {
  std::vector<Onset> o;uint32_t seed=start,pos=0;
  for(int i=0;i<count;++i){seed=seed*1664525u+1013904223u;pos+=5000+(seed%33000);o.push_back({pos,1000});}
  TEST_ASSERT_EQUAL(0,detect(o,44100).bpm10);
 }
 TEST_ASSERT_UINT16_WITHIN(3,1200,detect(beats(120,44100,4),44100).bpm10);
}
int main(){UNITY_BEGIN();
 RUN_TEST(test_regular_tempos_at_all_sample_rates);
 RUN_TEST(test_fractional_tempo_and_timestamp_shift);
 RUN_TEST(test_jitter_and_missing_beats_keep_tempo);
 RUN_TEST(test_swing_still_exposes_tempo_and_ambiguity);
 RUN_TEST(test_silence_single_and_too_short_are_unknown);
 RUN_TEST(test_irregular_onsets_are_unknown);
 RUN_TEST(test_bad_candidates_do_not_read_past_or_produce_a_tempo);
 RUN_TEST(test_weak_candidates_do_not_fake_confidence);
 RUN_TEST(test_few_irregular_attacks_do_not_overfit_a_tempo);
 return UNITY_END();}
