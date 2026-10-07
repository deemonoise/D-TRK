#include <unity.h>
#include <string.h>
#include "templates.h"

using namespace mt;

static Project p;

void setUp() {}
void tearDown() {}

void test_names_and_empty() {
  TEST_ASSERT_TRUE(templateCount() >= 5);
  TEST_ASSERT_EQUAL_STRING("EMPTY", templateName(0));
  TEST_ASSERT_EQUAL_STRING("EMPTY", templateName(99));
  for (int i = 0; i < templateCount(); ++i) TEST_ASSERT_TRUE(strlen(templateName(i)) <= 10);
  p.bpm = 99;
  templateBuild(0, p);
  TEST_ASSERT_EQUAL(120, p.bpm);
  TEST_ASSERT_EQUAL_STRING("untitled", p.name);
}

// Every preset a template names exists: no instrument is left at the reset default by a typo.
void test_every_template_fills_its_instruments() {
  for (int i = 1; i < templateCount(); ++i) {
    templateBuild(i, p);
    if (p.tracks[0].out == TrackOut::Midi) continue;  // MIDI 8
    for (int t = 0; t < 4; ++t) {
      const Instrument& m = p.instruments[p.tracks[t].instr];
      TEST_ASSERT_FALSE_MESSAGE(strncmp(m.name, "INS", 3) == 0, templateName(i));
    }
  }
}

void test_808_kit_lanes_play_drum_instruments() {
  templateBuild(1, p);
  const Instrument& k = p.instruments[p.tracks[0].instr];
  TEST_ASSERT_TRUE(k.type == InstrType::Kit);
  TEST_ASSERT_TRUE(p.trackIsDrum(0));
  for (int l = 0; l < 6; ++l) {
    TEST_ASSERT_EQUAL(8 + l, k.kit[l].instr);
    TEST_ASSERT_TRUE(p.instruments[8 + l].type == InstrType::Drum);
    TEST_ASSERT_EQUAL(-l, p.instruments[8 + l].transpose);  // lane note 60 + l plays at its own pitch
  }
}

void test_midi8() {
  templateBuild(5, p);
  for (int t = 0; t < 8; ++t) {
    TEST_ASSERT_TRUE(p.tracks[t].out == TrackOut::Midi);
    TEST_ASSERT_EQUAL(t, p.tracks[t].channel);
  }
  TEST_ASSERT_TRUE(p.tracks[8].out == TrackOut::Int);
}

void test_strip_keeps_sound_drops_notes() {
  templateBuild(1, p);
  strcpy(p.name, "SONG");
  p.patterns[2].steps[1][3].note = 60;
  p.patterns[2].length = 32;
  p.chainLen = 3;
  p.songMode = true;
  p.scenes[1] = 5;
  p.bpm = 133;
  templateStrip(p);
  TEST_ASSERT_FALSE(p.patterns[2].steps[1][3].hasNote());
  TEST_ASSERT_EQUAL(16, p.patterns[2].length);
  TEST_ASSERT_EQUAL(0, p.chainLen);
  TEST_ASSERT_FALSE(p.songMode);
  TEST_ASSERT_EQUAL_HEX16(kSceneEmpty, p.scenes[1]);
  TEST_ASSERT_EQUAL(133, p.bpm);  // project settings stay
  TEST_ASSERT_TRUE(p.instruments[0].type == InstrType::Kit);
  TEST_ASSERT_EQUAL_STRING("untitled", p.name);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_names_and_empty);
  RUN_TEST(test_every_template_fills_its_instruments);
  RUN_TEST(test_808_kit_lanes_play_drum_instruments);
  RUN_TEST(test_midi8);
  RUN_TEST(test_strip_keeps_sound_drops_notes);
  return UNITY_END();
}
