#include <unity.h>
#include <string.h>
#include "pattern_tools.h"
#include "automation.h"
#include "undo.h"
using namespace mt;
static Pattern p,scratch,before;
static Clipboard clip;
static uint8_t defaults[kTracks]; static bool drums[kTracks];
void setUp(){p=Pattern{};clip=Clipboard{};memset(drums,0,sizeof(drums));memset(defaults,90,sizeof(defaults));}
void tearDown(){}
void rotation_wraps_inside_scope_and_preserves_other_cells(){
 p.steps[2][3].note=60;p.steps[2][4].note=61;p.steps[2][5].note=62;
 p.steps[2][3].fx[0]={Fx::FLT,11};p.steps[2][2].note=77;
 TEST_ASSERT_EQUAL(ToolResult::Changed,rotateSteps(p,scratch,makeSel(2,3,2,5),1,StepPart::All));
 TEST_ASSERT_EQUAL(62,p.steps[2][3].note);TEST_ASSERT_EQUAL(60,p.steps[2][4].note);
 TEST_ASSERT_EQUAL(11,p.steps[2][4].fx[0].val);TEST_ASSERT_EQUAL(77,p.steps[2][2].note);
 rotateSteps(p,scratch,makeSel(2,3,2,5),-4,StepPart::All);
 TEST_ASSERT_EQUAL(60,p.steps[2][3].note);
 before=p;TEST_ASSERT_EQUAL(ToolResult::Unchanged,rotateSteps(p,scratch,makeSel(2,3,2,5),6,StepPart::All));
 TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(p));
}
void notes_rotation_leaves_fx_and_moves_kit_masks(){
 p.steps[0][0].note=100;setLaneMask(p.steps[0][0],0x8101);p.steps[0][0].fx[0]={Fx::NDG,10};
 p.steps[0][1].fx[0]={Fx::FLT,80};
 TEST_ASSERT_EQUAL(ToolResult::Changed,rotateSteps(p,scratch,makeSel(0,0,0,1),-1,StepPart::Notes));
 TEST_ASSERT_EQUAL(kNoteEmpty,p.steps[0][0].note);TEST_ASSERT_EQUAL(100,p.steps[0][1].note);
 TEST_ASSERT_EQUAL(0x8101,laneMask(p.steps[0][1]));TEST_ASSERT_EQUAL(Fx::NDG,p.steps[0][0].fx[0].cmd);
 TEST_ASSERT_EQUAL(Fx::FLT,p.steps[0][1].fx[0].cmd);
}
void automation_rotation_is_atomic_on_capacity_failure(){
 p.steps[0][0].fx[0]={Fx::FLT,88};p.steps[0][0].fx[1]={Fx::LFO,1};
 for(int i=0;i<kFxSlots;++i)p.steps[1][1].fx[i]={Fx::GAT,100};
 p.steps[1][0].fx[0]={Fx::PAN,64};before=p;
 TEST_ASSERT_EQUAL(ToolResult::NoSlot,rotateSteps(p,scratch,makeSel(0,0,1,1),1,StepPart::Automation));
 TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(p));
 p.steps[1][1]=Step{};
 TEST_ASSERT_EQUAL(ToolResult::Changed,rotateSteps(p,scratch,makeSel(0,0,1,1),1,StepPart::Automation));
 TEST_ASSERT_NULL(p.steps[0][0].find(Fx::FLT));TEST_ASSERT_NOT_NULL(p.steps[0][1].find(Fx::LFO));
 TEST_ASSERT_EQUAL(88,p.steps[0][1].find(Fx::FLT)->val);
}
void selective_paste_replaces_notes_but_preserves_all_destination_fx(){
 clip.tracks=1;clip.steps=2;clip.data[0][0].note=44;clip.data[0][0].vel=72;
 clip.data[0][0].fx[0]={Fx::FLT,50};p.steps[1][3].fx[0]={Fx::GAT,100};p.steps[1][4].note=99;
 TEST_ASSERT_EQUAL(ToolResult::Changed,pastePart(p,scratch,clip,1,3,StepPart::Notes));
 TEST_ASSERT_EQUAL(44,p.steps[1][3].note);TEST_ASSERT_EQUAL(72,p.steps[1][3].vel);
 TEST_ASSERT_EQUAL(Fx::GAT,p.steps[1][3].fx[0].cmd);TEST_ASSERT_NULL(p.steps[1][3].find(Fx::FLT));
 TEST_ASSERT_EQUAL(kNoteEmpty,p.steps[1][4].note);
}
void automation_paste_replaces_locks_and_preserves_notes_and_musical_fx(){
 clip.tracks=1;clip.steps=2;clip.data[0][0].fx[0]={Fx::PAN,20};clip.data[0][0].fx[1]={Fx::LFS,5};
 p.steps[0][0].note=60;p.steps[0][0].fx[0]={Fx::FLT,55};p.steps[0][0].fx[1]={Fx::NDG,8};
 p.steps[0][1].fx[0]={Fx::RES,99};
 TEST_ASSERT_EQUAL(ToolResult::Changed,pastePart(p,scratch,clip,0,0,StepPart::Automation));
 TEST_ASSERT_EQUAL(60,p.steps[0][0].note);TEST_ASSERT_NULL(p.steps[0][0].find(Fx::FLT));
 TEST_ASSERT_EQUAL(8,p.steps[0][0].find(Fx::NDG)->val);TEST_ASSERT_EQUAL(20,p.steps[0][0].find(Fx::PAN)->val);
 TEST_ASSERT_TRUE(p.steps[0][1].isEmpty());
 for(auto& f:p.steps[0][0].fx)f={Fx::GAT,10};before=p;
 TEST_ASSERT_EQUAL(ToolResult::NoSlot,pastePart(p,scratch,clip,0,0,StepPart::Automation));
 TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(p));
}
void paste_clips_edges_and_rejects_invalid_or_aliased_scratch(){
 clip.tracks=2;clip.steps=2;clip.data[0][0].note=70;clip.data[0][0].fx[0]={Fx::FLT,10};
 TEST_ASSERT_EQUAL(ToolResult::Changed,pastePart(p,scratch,clip,kTracks-1,p.length-1,StepPart::All));
 TEST_ASSERT_EQUAL(70,p.steps[kTracks-1][p.length-1].note);before=p;
 TEST_ASSERT_EQUAL(ToolResult::Invalid,pastePart(p,p,clip,0,0,StepPart::Notes));
 TEST_ASSERT_EQUAL(ToolResult::Invalid,pastePart(p,scratch,clip,-1,0,StepPart::Notes));
 clip.tracks=kTracks+1;TEST_ASSERT_EQUAL(ToolResult::Invalid,pastePart(p,scratch,clip,0,0,StepPart::All));
 TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(p));
}
void duplicate_copies_multiple_tracks_and_rejects_partial_fit(){
 p.steps[1][2].note=65;p.steps[2][3].fx[0]={Fx::DLY,20};p.steps[2][4].note=99;
 TEST_ASSERT_EQUAL(ToolResult::Changed,duplicateSteps(p,makeSel(1,2,2,3)));
 TEST_ASSERT_EQUAL(65,p.steps[1][4].note);TEST_ASSERT_EQUAL(kNoteEmpty,p.steps[2][4].note);
 TEST_ASSERT_EQUAL(20,p.steps[2][5].fx[0].val);before=p;
 TEST_ASSERT_EQUAL(ToolResult::NoRoom,duplicateSteps(p,makeSel(1,p.length-2,2,p.length-1)));
 TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(p));
}
void pattern_double_repeats_all_tracks_and_preserves_polymeter(){
 p.length=16;p.trackLen[2]=7;p.trackSpeed[2]=2;p.steps[15][15].note=90;p.steps[0][0].note=45;
 TEST_ASSERT_EQUAL(ToolResult::Changed,doublePattern(p));TEST_ASSERT_EQUAL(32,p.length);
 TEST_ASSERT_EQUAL(90,p.steps[15][31].note);TEST_ASSERT_EQUAL(45,p.steps[0][16].note);
 TEST_ASSERT_EQUAL(7,p.trackLen[2]);TEST_ASSERT_EQUAL(2,p.trackSpeed[2]);
 p.length=kMaxSteps;before=p;TEST_ASSERT_EQUAL(ToolResult::NoRoom,doublePattern(p));
 TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(p));
}
void velocity_set_and_add_resolve_defaults_and_preserve_kit_masks(){
 p.steps[0][0].note=60;p.steps[0][1].note=kNoteOff;
 p.steps[1][0].note=0;setLaneMask(p.steps[1][0],0x8001);drums[1]=true;
 VelocitySpec v;v.mode=VelocityMode::Add;v.amount=10;
 TEST_ASSERT_EQUAL(ToolResult::Changed,changeVelocity(p,makeSel(0,0,1,1),v,defaults,drums));
 TEST_ASSERT_EQUAL(100,p.steps[0][0].vel);TEST_ASSERT_EQUAL(100,p.steps[1][0].note);
 TEST_ASSERT_EQUAL(0x8001,laneMask(p.steps[1][0]));TEST_ASSERT_EQUAL(kVelDefault,p.steps[0][1].vel);
 v.mode=VelocityMode::Set;v.amount=200;changeVelocity(p,makeSel(0,0,1,1),v,defaults,drums);
 TEST_ASSERT_EQUAL(127,p.steps[0][0].vel);TEST_ASSERT_EQUAL(127,p.steps[1][0].note);
 v.mode=VelocityMode::Add;v.amount=-200;changeVelocity(p,makeSel(0,0,1,1),v,defaults,drums);
 TEST_ASSERT_EQUAL(1,p.steps[0][0].vel);
}
void velocity_ramp_uses_step_positions_and_leaves_empty_steps(){
 p.steps[0][2].note=60;p.steps[0][4].note=61;p.steps[0][6].note=62;p.steps[0][4].fx[0]={Fx::FLT,80};
 VelocitySpec v;v.mode=VelocityMode::Ramp;v.amount=20;v.end=100;
 TEST_ASSERT_EQUAL(ToolResult::Changed,changeVelocity(p,makeSel(0,2,0,6),v,defaults,drums));
 TEST_ASSERT_EQUAL(20,p.steps[0][2].vel);TEST_ASSERT_EQUAL(60,p.steps[0][4].vel);TEST_ASSERT_EQUAL(100,p.steps[0][6].vel);
 TEST_ASSERT_TRUE(p.steps[0][3].isEmpty());TEST_ASSERT_EQUAL(80,p.steps[0][4].fx[0].val);
 v.end=10;changeVelocity(p,makeSel(0,2,0,2),v,defaults,drums);TEST_ASSERT_EQUAL(20,p.steps[0][2].vel);
}
void velocity_humanize_is_bounded_reproducible_and_zero_is_noop(){
 for(int i=0;i<16;++i){p.steps[0][i].note=60;p.steps[0][i].vel=80;}before=p;
 VelocitySpec v;v.mode=VelocityMode::Humanize;v.amount=7;v.seed=123;
 TEST_ASSERT_EQUAL(ToolResult::Changed,changeVelocity(p,makeSel(0,0,0,15),v,defaults,drums));
 scratch=before;changeVelocity(scratch,makeSel(0,0,0,15),v,defaults,drums);
 TEST_ASSERT_EQUAL_MEMORY(&scratch,&p,sizeof(p));
 for(int i=0;i<16;++i)TEST_ASSERT_INT_WITHIN(7,80,p.steps[0][i].vel);
 v.amount=0;before=p;TEST_ASSERT_EQUAL(ToolResult::Unchanged,changeVelocity(p,makeSel(0,0,0,15),v,defaults,drums));
 TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(p));
}
void invalid_scopes_and_empty_operations_leave_pattern_unchanged(){
 before=p;
 TEST_ASSERT_EQUAL(ToolResult::Invalid,rotateSteps(p,p,makeSel(0,0,0,2),1,StepPart::All));
 TEST_ASSERT_EQUAL(ToolResult::Invalid,duplicateSteps(p,{0,0,0,static_cast<uint8_t>(p.length)}));
 TEST_ASSERT_EQUAL(ToolResult::Unchanged,rotateSteps(p,scratch,makeSel(0,0,0,2),1,StepPart::Automation));
 TEST_ASSERT_EQUAL(ToolResult::Unchanged,duplicateSteps(p,makeSel(0,0,0,2)));
 VelocitySpec v;TEST_ASSERT_EQUAL(ToolResult::Unchanged,changeVelocity(p,makeSel(0,0,0,2),v,defaults,drums));
 TEST_ASSERT_EQUAL(ToolResult::Invalid,changeVelocity(p,makeSel(0,0,0,2),v,nullptr,drums));
 TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(p));
}
void failed_paste_does_not_commit_earlier_cells(){
 clip.tracks=1;clip.steps=2;clip.data[0][0].fx[0]={Fx::FLT,80};clip.data[0][1].fx[0]={Fx::PAN,10};
 for(auto& f:p.steps[0][1].fx)f={Fx::GAT,100};before=p;
 TEST_ASSERT_EQUAL(ToolResult::NoSlot,pastePart(p,scratch,clip,0,0,StepPart::Automation));
 TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(p));
}
void pattern_double_at_limit_and_undo_restore_length_and_content(){
 static Undo::Entry entries[Undo::kDepth];Undo undo(entries);
 p.length=64;p.steps[0][63].note=77;p.trackLen[1]=11;before=p;undo.push(2,p);
 TEST_ASSERT_EQUAL(ToolResult::Changed,doublePattern(p));TEST_ASSERT_EQUAL(128,p.length);
 TEST_ASSERT_EQUAL(77,p.steps[0][127].note);uint8_t pat=0;
 TEST_ASSERT_TRUE(undo.pop(pat,p));TEST_ASSERT_EQUAL(2,pat);TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(p));
}
void kit_without_lanes_is_not_a_velocity_hit(){
 drums[0]=true;p.steps[0][0].note=0;before=p;VelocitySpec v;
 TEST_ASSERT_EQUAL(ToolResult::Unchanged,changeVelocity(p,makeSel(0,0,0,0),v,defaults,drums));
 TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(p));
}
int main(){UNITY_BEGIN();
 RUN_TEST(rotation_wraps_inside_scope_and_preserves_other_cells);
 RUN_TEST(notes_rotation_leaves_fx_and_moves_kit_masks);
 RUN_TEST(automation_rotation_is_atomic_on_capacity_failure);
 RUN_TEST(selective_paste_replaces_notes_but_preserves_all_destination_fx);
 RUN_TEST(automation_paste_replaces_locks_and_preserves_notes_and_musical_fx);
 RUN_TEST(paste_clips_edges_and_rejects_invalid_or_aliased_scratch);
 RUN_TEST(duplicate_copies_multiple_tracks_and_rejects_partial_fit);
 RUN_TEST(pattern_double_repeats_all_tracks_and_preserves_polymeter);
 RUN_TEST(velocity_set_and_add_resolve_defaults_and_preserve_kit_masks);
 RUN_TEST(velocity_ramp_uses_step_positions_and_leaves_empty_steps);
 RUN_TEST(velocity_humanize_is_bounded_reproducible_and_zero_is_noop);
 RUN_TEST(invalid_scopes_and_empty_operations_leave_pattern_unchanged);
 RUN_TEST(failed_paste_does_not_commit_earlier_cells);
 RUN_TEST(pattern_double_at_limit_and_undo_restore_length_and_content);
 RUN_TEST(kit_without_lanes_is_not_a_velocity_hit);
 return UNITY_END();}
