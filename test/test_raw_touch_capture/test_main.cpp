#include <unity.h>
#include "../../src/ui/raw_touch_capture.h"
void setUp() {}
void tearDown() {}
using namespace ui;
void test_gesture_stays_with_the_screen_that_received_down() {
 RawTouchCapture c;int a,b;
 TEST_ASSERT_EQUAL(RawTouchRoute::Screen,c.event(RawTouchPhase::Down,&a,false,true,false));
 TEST_ASSERT_EQUAL(RawTouchRoute::Screen,c.event(RawTouchPhase::Move,&a,false,true,false));
 TEST_ASSERT_EQUAL(RawTouchRoute::None,c.event(RawTouchPhase::Move,&b,false,true,false));
 TEST_ASSERT_EQUAL(RawTouchRoute::None,c.event(RawTouchPhase::Up,&b,false,true,false));
}
void test_menu_cancels_drawing_and_cannot_send_up_to_a_replacement_overlay() {
 RawTouchCapture c;int a;
 c.event(RawTouchPhase::Down,&a,false,true,false);
 TEST_ASSERT_EQUAL(RawTouchRoute::None,c.event(RawTouchPhase::Move,&a,false,true,true));
 TEST_ASSERT_EQUAL(RawTouchRoute::None,c.event(RawTouchPhase::Up,&a,false,true,false));
}
void test_xy_cancellation_releases_pad_once_and_never_enters_screen() {
 RawTouchCapture c;int a;
 TEST_ASSERT_EQUAL(RawTouchRoute::Xy,c.event(RawTouchPhase::Down,&a,true,true,false));
 TEST_ASSERT_EQUAL(RawTouchRoute::CancelXy,c.event(RawTouchPhase::Move,&a,false,true,false));
 TEST_ASSERT_EQUAL(RawTouchRoute::None,c.event(RawTouchPhase::Up,&a,false,true,false));
}
void test_release_and_tab_reset_end_capture() {
 RawTouchCapture c;int a;
 TEST_ASSERT_EQUAL(RawTouchRoute::None,c.event(RawTouchPhase::Up,&a,false,true,false));
 c.event(RawTouchPhase::Down,&a,false,true,false);
 TEST_ASSERT_EQUAL(RawTouchRoute::Screen,c.event(RawTouchPhase::Up,&a,false,true,false));
 TEST_ASSERT_EQUAL(RawTouchRoute::None,c.event(RawTouchPhase::Move,&a,false,true,false));
 c.event(RawTouchPhase::Down,&a,true,false,false);c.reset();
 TEST_ASSERT_EQUAL(RawTouchRoute::None,c.event(RawTouchPhase::Move,&a,true,false,false));
}
int main(){UNITY_BEGIN();RUN_TEST(test_gesture_stays_with_the_screen_that_received_down);RUN_TEST(test_menu_cancels_drawing_and_cannot_send_up_to_a_replacement_overlay);RUN_TEST(test_xy_cancellation_releases_pad_once_and_never_enters_screen);RUN_TEST(test_release_and_tab_reset_end_capture);return UNITY_END();}
