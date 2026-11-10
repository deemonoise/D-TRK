#include <unity.h>
#include "proj_pages.h"
using namespace mt;
void setUp() {}
void tearDown() {}
void physical_order_places_pattern_after_main_without_changing_stored_ids() {
 const int ids[]={0,6,1,2,3,4,5};
 for(int i=0;i<kProjectPageCount;++i){
  TEST_ASSERT_EQUAL(ids[i],projectPageAt(i));
  TEST_ASSERT_EQUAL(i,projectPagePosition(ids[i]));
 }
 TEST_ASSERT_EQUAL(kProjectPerf,projectPageAt(-1));
 TEST_ASSERT_EQUAL(kProjectMain,projectPageAt(kProjectPageCount));
}
void legacy_knob_bindings_keep_their_complete_original_row_ranges() {
 constexpr int starts[]={0,10,16,22,27,33,51};
 for(int page=0;page<6;++page){
  const auto rows=projectPageRows(page,starts,6,true);
  TEST_ASSERT_EQUAL(starts[page],rows.first);
  TEST_ASSERT_EQUAL(starts[page+1]-starts[page],rows.count);
  TEST_ASSERT_EQUAL(page,projectBindingPage(page));
 }
 // New PATTERN learns the same stored ID as old Length/Resolution/Swing/Groove bindings.
 TEST_ASSERT_EQUAL(0,projectBindingPage(kProjectPattern));
}
void main_and_pattern_ui_split_rows_without_out_of_bounds_page_access() {
 constexpr int starts[]={0,10,16,22,27,33,51};
 auto main=projectPageRows(kProjectMain,starts,6);
 auto pattern=projectPageRows(kProjectPattern,starts,6);
 TEST_ASSERT_EQUAL(0,main.first);TEST_ASSERT_EQUAL(6,main.count);
 TEST_ASSERT_EQUAL(6,pattern.first);TEST_ASSERT_EQUAL(4,pattern.count);
 auto bound=projectPageRows(kProjectPattern,starts,6,true);
 TEST_ASSERT_EQUAL(6,bound.first);TEST_ASSERT_EQUAL(4,bound.count);
 const int invalid[]={-1,7,100};
 for(int bad : invalid) {
  const auto rows=projectPageRows(bad,starts,6);
  TEST_ASSERT_EQUAL(0,rows.count);
 }
}
int main(){UNITY_BEGIN();
 RUN_TEST(physical_order_places_pattern_after_main_without_changing_stored_ids);
 RUN_TEST(legacy_knob_bindings_keep_their_complete_original_row_ranges);
 RUN_TEST(main_and_pattern_ui_split_rows_without_out_of_bounds_page_access);
 return UNITY_END();}
