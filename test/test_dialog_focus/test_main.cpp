#include <unity.h>
#include "../../src/ui/dialog_focus.h"
void setUp(){}
void tearDown(){}
void focus_reaches_both_actions_and_wraps(){
 TEST_ASSERT_EQUAL(3,ui::dialogNextFocus(2,1,3));
 TEST_ASSERT_EQUAL(4,ui::dialogNextFocus(3,1,3));
 TEST_ASSERT_EQUAL(0,ui::dialogNextFocus(4,1,3));
 TEST_ASSERT_EQUAL(4,ui::dialogNextFocus(0,-1,3));
}
void focus_handles_coarse_turns_and_zero_parameters(){
 TEST_ASSERT_EQUAL(4,ui::dialogNextFocus(1,-7,3));
 TEST_ASSERT_EQUAL(1,ui::dialogNextFocus(0,3,0));
}
int main(){UNITY_BEGIN();RUN_TEST(focus_reaches_both_actions_and_wraps);RUN_TEST(focus_handles_coarse_turns_and_zero_parameters);return UNITY_END();}
