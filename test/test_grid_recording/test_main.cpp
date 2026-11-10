#include <unity.h>
#include "../../src/ui/grid_recording.h"
using namespace mt;
void setUp(){} void tearDown(){}
void test_another_kit_lane_keeps_the_chords_first_recorded_nudge(){
 Step first;first.note=0;first.vel=1;recordTiming(first,64);
 Step second=first;second.vel|=2;
 TEST_ASSERT_TRUE(ui::recordGridTiming(second,first,true,false,192));
 TEST_ASSERT_EQUAL(3,second.vel);
 TEST_ASSERT_EQUAL(Fx::NDG,second.fx[0].cmd);
 TEST_ASSERT_EQUAL(25,fxSigned(second.fx[0].val));
}
void test_kit_chord_does_not_need_an_additional_timing_slot(){
 Step first;first.note=0;first.vel=1;
 for(auto& f:first.fx)f={Fx::RVB,64};
 Step second=first;second.vel|=2;
 TEST_ASSERT_TRUE(ui::recordGridTiming(second,first,true,false,64));
 TEST_ASSERT_EQUAL(3,second.vel);
 for(auto f:second.fx)TEST_ASSERT_EQUAL(Fx::RVB,f.cmd);
}
void test_first_kit_hit_and_replaced_melodic_note_keep_live_timing(){
 Step empty;Step hit=empty;hit.note=0;hit.vel=1;
 TEST_ASSERT_TRUE(ui::recordGridTiming(hit,empty,true,false,64));
 TEST_ASSERT_EQUAL(25,fxSigned(hit.fx[0].val));
 Step melodic=hit;melodic.note=60;melodic.vel=100;
 TEST_ASSERT_TRUE(ui::recordGridTiming(melodic,hit,false,false,192));
 TEST_ASSERT_EQUAL(-25,fxSigned(melodic.fx[0].val));
 Step cleared=hit;cleared.vel=0;
 TEST_ASSERT_TRUE(ui::recordGridTiming(cleared,hit,true,true,192));
 TEST_ASSERT_EQUAL(25,fxSigned(cleared.fx[0].val));
}
void test_rehit_of_an_existing_kit_lane_updates_its_recorded_timing(){
 Step before;before.note=0;before.vel=1;recordTiming(before,64);
 Step rehit=before;
 TEST_ASSERT_TRUE(ui::recordGridTiming(rehit,before,true,false,192));
 TEST_ASSERT_EQUAL(1,rehit.vel);
 TEST_ASSERT_EQUAL(Fx::NDG,rehit.fx[0].cmd);
 TEST_ASSERT_EQUAL(-25,fxSigned(rehit.fx[0].val));
}
void test_existing_kit_lane_rehit_reports_full_fx_without_changing_the_step(){
 Step before;before.note=0;before.vel=1;
 for(auto& f:before.fx)f={Fx::RVB,64};
 Step rehit=before;
 TEST_ASSERT_FALSE(ui::recordGridTiming(rehit,before,true,false,64));
 for(auto f:rehit.fx){TEST_ASSERT_EQUAL(Fx::RVB,f.cmd);TEST_ASSERT_EQUAL(64,f.val);}
 TEST_ASSERT_EQUAL(1,rehit.vel);
 // A quantized hit has no NDG to store, even when every slot is occupied.
 TEST_ASSERT_TRUE(ui::recordGridTiming(rehit,before,true,false,0));
 for(auto f:rehit.fx){TEST_ASSERT_EQUAL(Fx::RVB,f.cmd);TEST_ASSERT_EQUAL(64,f.val);}
}
int main(){UNITY_BEGIN();RUN_TEST(test_another_kit_lane_keeps_the_chords_first_recorded_nudge);RUN_TEST(test_kit_chord_does_not_need_an_additional_timing_slot);RUN_TEST(test_first_kit_hit_and_replaced_melodic_note_keep_live_timing);RUN_TEST(test_rehit_of_an_existing_kit_lane_updates_its_recorded_timing);RUN_TEST(test_existing_kit_lane_rehit_reports_full_fx_without_changing_the_step);return UNITY_END();}
