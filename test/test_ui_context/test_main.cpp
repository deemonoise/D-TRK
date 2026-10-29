#include <unity.h>
#include "ui_context.h"
using namespace mt;
static Project p;
void setUp(){p=Project{};for(auto& track:p.tracks)track.out=TrackOut::Midi;}
void tearDown(){}
void test_instrument_use_counts_internal_tracks_and_kit_consumers_once(){
 p.tracks[0].out=TrackOut::Int;p.tracks[2].out=TrackOut::Int;
 p.tracks[0].instr=5;p.tracks[1].instr=5;p.tracks[1].out=TrackOut::Midi;
 p.tracks[2].instr=7;p.instruments[7].type=InstrType::Kit;
 p.instruments[7].kit[0].instr=5;p.instruments[7].kit[1].instr=5;
 const auto use=instrumentUse(p,5,2);
 TEST_ASSERT_EQUAL(2,use.count);TEST_ASSERT_EQUAL(5,use.tracks);TEST_ASSERT_TRUE(use.current);TEST_ASSERT_TRUE(use.currentViaKit);
 TEST_ASSERT_FALSE(instrumentUse(p,5,0).currentViaKit);
 TEST_ASSERT_FALSE(instrumentUse(p,5,1).current);
 TEST_ASSERT_EQUAL(0,instrumentUse(p,kInstruments,0).count);
}
void test_marker_position_shows_percent_and_elapsed_seconds(){
 char text[32];markerPosition(32768,48000,48000,text,sizeof(text));
 TEST_ASSERT_EQUAL_STRING("50.0% 0.500s",text);
 markerPosition(65535,96000,48000,text,sizeof(text));TEST_ASSERT_EQUAL_STRING("100.0% 2.000s",text);
 markerPosition(0,1,0,text,sizeof(text));TEST_ASSERT_EQUAL_STRING("0.0%",text);
}
int main(){UNITY_BEGIN();RUN_TEST(test_instrument_use_counts_internal_tracks_and_kit_consumers_once);RUN_TEST(test_marker_position_shows_percent_and_elapsed_seconds);return UNITY_END();}
