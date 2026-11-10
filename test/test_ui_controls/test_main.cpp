#include <unity.h>
#include "ui_controls.h"
void setUp(){} void tearDown(){}
void test_inactive_explains_its_condition_and_active_has_no_warning(){
 TEST_ASSERT_EQUAL_STRING("ENABLE FILTER",mt::parameterHint(true,true,"ENABLE FILTER"));
 TEST_ASSERT_NULL(mt::parameterHint(false,true,"ENABLE FILTER"));
 TEST_ASSERT_EQUAL_STRING("INACTIVE WITH CURRENT SETTINGS",mt::parameterHint(true,true,nullptr));
}
void test_read_only_is_not_presented_as_an_editable_value(){
 TEST_ASSERT_EQUAL_STRING("READ ONLY",mt::parameterHint(false,false,nullptr));
 TEST_ASSERT_EQUAL_STRING("MIDI TRACK",mt::parameterHint(true,false,"MIDI TRACK"));
 TEST_ASSERT_NULL(mt::parameterHint(false,true,nullptr));
}
int main(){UNITY_BEGIN();RUN_TEST(test_inactive_explains_its_condition_and_active_has_no_warning);RUN_TEST(test_read_only_is_not_presented_as_an_editable_value);return UNITY_END();}
