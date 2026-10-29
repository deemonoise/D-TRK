#include <unity.h>
#include <cstring>
#include "quantize.h"
#include "automation.h"
#include "record.h"
using namespace mt;
static Pattern p, scratch, original;
void setUp() { p.clear(); p.length=16; }
void tearDown() {}
void test_live_nudge_round_trips_heard_phase() {
 TEST_ASSERT_EQUAL(25,recordNudge(64)); TEST_ASSERT_EQUAL(-50,recordNudge(128));
 TEST_ASSERT_EQUAL(0,recordNudge(0)); TEST_ASSERT_EQUAL(0,recordNudge(255));
}
void test_clear_parts_preserve_other_data_and_count_all_slots() {
 auto& s=p.steps[0][2]; s.note=40; setLaneMask(s,0x8123);
 s.fx[0]={Fx::FLT,60}; s.fx[1]={Fx::NDG,20}; s.fx[2]={Fx::PRB,70}; s.fx[4]={Fx::LFD,20};
 TEST_ASSERT_EQUAL(4,automationSlotsUsed(s));
 clearAutomationSel(p,makeSel(0,2,0,2));
 TEST_ASSERT_EQUAL(40,s.note); TEST_ASSERT_EQUAL(0x8123,laneMask(s));
 TEST_ASSERT_EQUAL(Fx::None,s.fx[0].cmd); TEST_ASSERT_EQUAL(Fx::None,s.fx[4].cmd);
 TEST_ASSERT_EQUAL(Fx::NDG,s.fx[1].cmd); TEST_ASSERT_EQUAL(Fx::PRB,s.fx[2].cmd);
 clearNotesSel(p,makeSel(0,2,0,2));
 TEST_ASSERT_EQUAL(kNoteEmpty,s.note); TEST_ASSERT_EQUAL(0,laneMask(s));
 TEST_ASSERT_EQUAL(Fx::PRB,s.fx[2].cmd);
}
void test_strength_reduces_nudge_and_keeps_velocity_pitch_and_locks() {
 auto& s=p.steps[0][2]; s.note=60; s.vel=93; s.fx[0]={Fx::NDG,40}; s.fx[1]={Fx::FLT,80};
 TEST_ASSERT_EQUAL(QuantizeResult::Ok,quantizeNotes(p,scratch,makeSel(0,0,0,15),{Resolution::Sixteenth,50}));
 TEST_ASSERT_EQUAL(20,fxSigned(s.find(Fx::NDG)->val)); TEST_ASSERT_EQUAL(60,s.note); TEST_ASSERT_EQUAL(93,s.vel);
 TEST_ASSERT_EQUAL(80,s.find(Fx::FLT)->val);
 TEST_ASSERT_EQUAL(QuantizeResult::Ok,quantizeNotes(p,scratch,makeSel(0,0,0,15),{Resolution::Sixteenth,100}));
 TEST_ASSERT_NULL(s.find(Fx::NDG));
}
void test_coarse_grid_moves_musical_data_but_not_automation() {
 p.steps[0][3].note=65; p.steps[0][3].vel=80;
 p.steps[0][3].fx[0]={Fx::GAT,75}; p.steps[0][3].fx[1]={Fx::FLT,20};
 p.steps[0][4].fx[0]={Fx::RES,40};
 TEST_ASSERT_EQUAL(QuantizeResult::Ok,quantizeNotes(p,scratch,makeSel(0,0,0,15),{Resolution::Quarter,100}));
 TEST_ASSERT_EQUAL(kNoteEmpty,p.steps[0][3].note); TEST_ASSERT_EQUAL(20,p.steps[0][3].find(Fx::FLT)->val);
 TEST_ASSERT_EQUAL(65,p.steps[0][4].note); TEST_ASSERT_EQUAL(75,p.steps[0][4].find(Fx::GAT)->val);
 TEST_ASSERT_EQUAL(40,p.steps[0][4].find(Fx::RES)->val);
}
void test_collisions_and_capacity_fail_without_changes() {
 p.steps[0][1].note=60; p.steps[0][0].note=64; original=p;
 TEST_ASSERT_EQUAL(QuantizeResult::Collision,quantizeNotes(p,scratch,makeSel(0,0,0,15),{Resolution::Quarter,100}));
 TEST_ASSERT_EQUAL_MEMORY(&original,&p,sizeof(p));
 p.clear(); p.length=16; p.steps[0][3].note=60; p.steps[0][3].fx[0]={Fx::GAT,80};
 for(auto& f:p.steps[0][4].fx)f={Fx::FLT,50}; original=p;
 TEST_ASSERT_EQUAL(QuantizeResult::NoSlot,quantizeNotes(p,scratch,makeSel(0,0,0,15),{Resolution::Quarter,100}));
 TEST_ASSERT_EQUAL_MEMORY(&original,&p,sizeof(p));
}
void test_track_speed_selection_and_kit_masks() {
 p.trackSpeed[0]=static_cast<uint8_t>(TrackSpeed::X2);
 p.steps[0][7].note=90; setLaneMask(p.steps[0][7],0x8001);
 p.steps[1][7].note=70;
 TEST_ASSERT_EQUAL(QuantizeResult::Ok,quantizeNotes(p,scratch,makeSel(0,0,0,15),{Resolution::Quarter,100}));
 TEST_ASSERT_EQUAL(90,p.steps[0][8].note); TEST_ASSERT_EQUAL(0x8001,laneMask(p.steps[0][8]));
 TEST_ASSERT_EQUAL(70,p.steps[1][7].note);
 original=p;
 TEST_ASSERT_EQUAL(QuantizeResult::Unchanged,quantizeNotes(p,scratch,makeSel(0,0,0,15),{Resolution::Quarter,0}));
 TEST_ASSERT_EQUAL_MEMORY(&original,&p,sizeof(p));
}
void test_record_timing_preserves_commands_and_reports_capacity() {
 Step s; s.note=60; s.fx[0]={Fx::GAT,75};
 TEST_ASSERT_TRUE(recordTiming(s,64));
 TEST_ASSERT_EQUAL(25,fxSigned(s.find(Fx::NDG)->val)); TEST_ASSERT_EQUAL(75,s.fx[0].val);
 TEST_ASSERT_TRUE(recordTiming(s,0)); TEST_ASSERT_NULL(s.find(Fx::NDG));
 for(auto& f:s.fx) f={Fx::FLT,20}; const Step before=s;
 TEST_ASSERT_FALSE(recordTiming(s,100)); TEST_ASSERT_EQUAL_MEMORY(&before,&s,sizeof(s));
}
void test_quantize_respects_polymeter_and_off_steps_and_invalid_ranges() {
 p.trackLen[0]=5; p.steps[0][3].note=60; p.steps[0][6].note=70;
 TEST_ASSERT_EQUAL(QuantizeResult::Ok,quantizeNotes(p,scratch,makeSel(0,0,0,15),{Resolution::Quarter,100}));
 TEST_ASSERT_EQUAL(60,p.steps[0][4].note); TEST_ASSERT_EQUAL(70,p.steps[0][6].note);
 p.clear(); p.length=16; p.steps[0][3].note=60; p.steps[0][4].note=kNoteOff; original=p;
 TEST_ASSERT_EQUAL(QuantizeResult::Collision,quantizeNotes(p,scratch,makeSel(0,0,0,15),{Resolution::Quarter,100}));
 TEST_ASSERT_EQUAL_MEMORY(&original,&p,sizeof(p));
 p.steps[0][1].note=65; original=p;
 TEST_ASSERT_EQUAL(QuantizeResult::Invalid,quantizeNotes(p,scratch,makeSel(0,1,0,1),{Resolution::Quarter,100}));
 TEST_ASSERT_EQUAL_MEMORY(&original,&p,sizeof(p));
}
int main(){ UNITY_BEGIN();
 RUN_TEST(test_quantize_respects_polymeter_and_off_steps_and_invalid_ranges);
 RUN_TEST(test_record_timing_preserves_commands_and_reports_capacity);
 RUN_TEST(test_live_nudge_round_trips_heard_phase);
 RUN_TEST(test_clear_parts_preserve_other_data_and_count_all_slots);
 RUN_TEST(test_strength_reduces_nudge_and_keeps_velocity_pitch_and_locks);
 RUN_TEST(test_coarse_grid_moves_musical_data_but_not_automation);
 RUN_TEST(test_collisions_and_capacity_fail_without_changes);
 RUN_TEST(test_track_speed_selection_and_kit_masks);
 return UNITY_END(); }
