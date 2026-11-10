#include <unity.h>
#include "../../src/ui/automation_graph.h"
using namespace mt;
void setUp(){} void tearDown(){}
void test_matrix_touch_reaches_both_polarities_and_encodes_zero(){
 const ui::AutomationGraph graph(Fx::M01);
 TEST_ASSERT_EQUAL(-64,fxSigned(graph.valueAt(0,127)));
 TEST_ASSERT_EQUAL(0,fxSigned(graph.valueAt(64,127)));
 TEST_ASSERT_EQUAL(63,fxSigned(graph.valueAt(127,127)));
 TEST_ASSERT_EQUAL(-64,fxSigned(graph.valueAt(-50,127)));
 TEST_ASSERT_EQUAL(63,fxSigned(graph.valueAt(999,127)));
}
void test_matrix_bars_decode_signed_bytes_into_graph_bounds(){
 const ui::AutomationGraph graph(Fx::M08);
 TEST_ASSERT_EQUAL(0,graph.height(uint8_t(-64),127));
 TEST_ASSERT_EQUAL(64,graph.height(0,127));
 TEST_ASSERT_EQUAL(127,graph.height(63,127));
 TEST_ASSERT_EQUAL(0,graph.height(uint8_t(-100),127));
 TEST_ASSERT_EQUAL(127,graph.height(100,127));
}
void test_existing_unsigned_automation_keeps_its_range_and_short_graph_is_safe(){
 const ui::AutomationGraph graph(Fx::FLT);
 TEST_ASSERT_EQUAL(0,graph.valueAt(0,127));
 TEST_ASSERT_EQUAL(64,graph.valueAt(64,127));
 TEST_ASSERT_EQUAL(127,graph.valueAt(127,127));
 TEST_ASSERT_EQUAL(64,graph.height(64,127));
 TEST_ASSERT_EQUAL(fxDefault(Fx::FLT),graph.valueAt(20,0));
 TEST_ASSERT_EQUAL(0,graph.height(64,0));
}
int main(){UNITY_BEGIN();RUN_TEST(test_matrix_touch_reaches_both_polarities_and_encodes_zero);RUN_TEST(test_matrix_bars_decode_signed_bytes_into_graph_bounds);RUN_TEST(test_existing_unsigned_automation_keeps_its_range_and_short_graph_is_safe);return UNITY_END();}
