#include <unity.h>
#include <functional>
#include "menu_history.h"
#include "grid_context.h"
using namespace mt;
void setUp() {}
void tearDown() {}
struct Frame { int selection=0, scroll=0; std::function<int()> action; };
void test_history_restores_parent_position_and_action_in_order() {
 MenuHistory<Frame,2> h;
 Frame a{8,3,[]{return 11;}}, b{2,0,[]{return 22;}}, out;
 TEST_ASSERT_TRUE(h.push(a)); TEST_ASSERT_TRUE(h.push(b));
 TEST_ASSERT_TRUE(h.pop(out)); TEST_ASSERT_EQUAL(2,out.selection); TEST_ASSERT_EQUAL(22,out.action());
 TEST_ASSERT_TRUE(h.pop(out)); TEST_ASSERT_EQUAL(8,out.selection); TEST_ASSERT_EQUAL(3,out.scroll);
 TEST_ASSERT_EQUAL(11,out.action()); TEST_ASSERT_FALSE(h.pop(out));
}
void test_history_overflow_preserves_existing_parents_and_close_clears() {
 MenuHistory<Frame,1> h; Frame out;
 TEST_ASSERT_TRUE(h.push({5,1,{}})); TEST_ASSERT_FALSE(h.push({9,3,{}}));
 TEST_ASSERT_TRUE(h.pop(out)); TEST_ASSERT_EQUAL(5,out.selection);
 h.push({3,0,{}}); h.clear(); TEST_ASSERT_EQUAL(0,h.size()); TEST_ASSERT_FALSE(h.pop(out));
}
void test_step_process_and_track_have_distinct_scope() {
 GridContext c; c.track=4; c.step=9; c.length=32;
 auto edit=c.editScope(), process=c.processScope();
 TEST_ASSERT_EQUAL(4,edit.t0); TEST_ASSERT_EQUAL(9,edit.s0); TEST_ASSERT_EQUAL(9,edit.s1);
 TEST_ASSERT_EQUAL(0,process.s0); TEST_ASSERT_EQUAL(31,process.s1);
 c.selected=true; c.selection=makeSel(1,3,2,7);
 edit=c.editScope(); process=c.processScope(); auto track=c.trackScope();
 TEST_ASSERT_EQUAL(1,edit.t0); TEST_ASSERT_EQUAL(2,edit.t1); TEST_ASSERT_EQUAL(7,process.s1);
 TEST_ASSERT_EQUAL(4,track.t0); TEST_ASSERT_EQUAL(0,track.s0); TEST_ASSERT_EQUAL(31,track.s1);
 // Context is a value snapshot: moving the live cursor cannot change a pending action's range.
 auto snapshot=c; c.track=8; c.step=20; c.selection=makeSel(8,20,8,21);
 TEST_ASSERT_EQUAL(1,snapshot.editScope().t0); TEST_ASSERT_EQUAL(4,snapshot.trackScope().t0);
}
int main(){UNITY_BEGIN();
 RUN_TEST(test_history_restores_parent_position_and_action_in_order);
 RUN_TEST(test_history_overflow_preserves_existing_parents_and_close_clears);
 RUN_TEST(test_step_process_and_track_have_distinct_scope);
 return UNITY_END();}
