#include <unity.h>
#include <cstring>
#include "practice.h"
using namespace mt;
void setUp() {}
void tearDown() {}
void test_count_in_four_beats_then_start_at_exact_time() {
  RecordPractice p; p.arm(true,false,0,0); p.startCount(1000,120,1);
  TEST_ASSERT_TRUE(p.counting()); TEST_ASSERT_FALSE(p.active());
  TEST_ASSERT_EQUAL(4,p.remaining());
  TEST_ASSERT_EQUAL(RecordPractice::Tick::Accent,p.tick(1000));
  TEST_ASSERT_EQUAL(RecordPractice::Tick::None,p.tick(500999));
  for (int n=1;n<4;++n) TEST_ASSERT_EQUAL(RecordPractice::Tick::Beat,p.tick(1000+n*500000));
  TEST_ASSERT_EQUAL(1,p.remaining());
  TEST_ASSERT_EQUAL(RecordPractice::Tick::Start,p.tick(2001000));
  p.started(0,0); TEST_ASSERT_TRUE(p.active()); TEST_ASSERT_FALSE(p.counting());
}
void test_two_bars_and_cancel_never_start() {
  RecordPractice p; p.arm(true,false,0,0); p.startCount(0,60,2);
  for (int n=0;n<8;++n)
    TEST_ASSERT_EQUAL(n%4==0 ? RecordPractice::Tick::Accent : RecordPractice::Tick::Beat,p.tick(n*1000000ull));
  TEST_ASSERT_EQUAL(RecordPractice::Tick::Start,p.tick(8000000));
  p.startCount(0,120,1); p.stop();
  TEST_ASSERT_EQUAL(RecordPractice::Tick::None,p.tick(3000000));
  TEST_ASSERT_FALSE(p.active()); TEST_ASSERT_EQUAL(UINT64_MAX,p.next());
}
void test_one_pass_disarms_on_boundary_only_and_loop_keeps_recording() {
  RecordPractice p; p.arm(true,true,2,7);
  TEST_ASSERT_TRUE(p.active());
  p.observe(true,2,7,999,true); TEST_ASSERT_TRUE(p.active());
  p.observe(true,2,8,1234,true);
  TEST_ASSERT_FALSE(p.active()); TEST_ASSERT_FALSE(p.armed());
  TEST_ASSERT_EQUAL(1,p.completed()); TEST_ASSERT_EQUAL(1234,p.endedAt());
  p.observe(true,2,9,2345,true); TEST_ASSERT_EQUAL(1,p.completed());
  p.arm(true,true,2,9); p.observe(true,3,0,3000,true);
  TEST_ASSERT_EQUAL(2,p.completed());
  p.arm(true,true,3,0); p.observe(true,3,1,4000,false); TEST_ASSERT_TRUE(p.active());
  p.stop(); TEST_ASSERT_FALSE(p.active());
}
void test_repeated_arm_does_not_restart_take_and_disarm_cancels_count() {
  RecordPractice p; p.arm(true,true,0,4); p.arm(true,true,0,5);
  p.observe(true,0,5,100,true); TEST_ASSERT_FALSE(p.active());
  p.arm(true,false,0,0); p.startCount(0,120,1); p.arm(false,false,0,0);
  TEST_ASSERT_FALSE(p.counting()); TEST_ASSERT_FALSE(p.armed());
}
void test_click_offset_decay_saturation_and_reset() {
  MetronomeVoice v; int16_t l[128]{}, r[128]{};
  v.trigger(64,true,127); v.mix(l,r,128);
  for(int n=0;n<64;++n) TEST_ASSERT_EQUAL(0,l[n]);
  bool nonzero=false; for(int n=64;n<128;++n) if(l[n]) nonzero=true;
  TEST_ASSERT_TRUE(nonzero); TEST_ASSERT_EQUAL_MEMORY(l,r,sizeof(l));
  for(int b=0;b<10;++b) { memset(l,0,sizeof(l)); memset(r,0,sizeof(r)); v.mix(l,r,128); }
  for(auto x:l) TEST_ASSERT_EQUAL(0,x);
  for(auto& x:l)x=32760; for(auto& x:r)x=-32760;
  v.trigger(0,false,127); v.mix(l,r,128);
  bool clamped=false; for(auto x:l) if(x==32767) clamped=true;
  TEST_ASSERT_TRUE(clamped);
  v.reset(); memset(l,0,sizeof(l)); memset(r,0,sizeof(r)); v.mix(l,r,128);
  for(auto x:l) TEST_ASSERT_EQUAL(0,x);
}
void test_muted_click_and_preferences_round_trip() {
  PracticePrefs p{2,2,96,true};
  const auto q=practicePrefs(practiceBits(p),p.clickLevel);
  TEST_ASSERT_EQUAL(2,q.countBars); TEST_ASSERT_EQUAL(2,q.clickMode);
  TEST_ASSERT_EQUAL(96,q.clickLevel); TEST_ASSERT_TRUE(q.onePass);
  const auto bad=practicePrefs(255,255);
  TEST_ASSERT_EQUAL(0,bad.countBars); TEST_ASSERT_EQUAL(0,bad.clickMode);
  TEST_ASSERT_EQUAL(127,bad.clickLevel);
  MetronomeVoice v; int16_t l[128]{},r[128]{};
  v.trigger(0,true,0); v.mix(l,r,128);
  for(auto x:l) TEST_ASSERT_EQUAL(0,x);
}
int main() {
 UNITY_BEGIN();
 RUN_TEST(test_muted_click_and_preferences_round_trip);
 RUN_TEST(test_count_in_four_beats_then_start_at_exact_time);
 RUN_TEST(test_two_bars_and_cancel_never_start);
 RUN_TEST(test_one_pass_disarms_on_boundary_only_and_loop_keeps_recording);
 RUN_TEST(test_repeated_arm_does_not_restart_take_and_disarm_cancels_count);
 RUN_TEST(test_click_offset_decay_saturation_and_reset);
 return UNITY_END();
}
