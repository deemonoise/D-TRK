#include <unity.h>
#include "midi_router.h"

using namespace mt;

namespace {

Project* p = nullptr;  // heap: Project is ~480 KB
PadLearn pads;
MidiRouter r;

Action on(int ch, int note, int vel = 100) {
  return r.route(MidiMsg{static_cast<uint8_t>(0x90 | ch), static_cast<uint8_t>(note), static_cast<uint8_t>(vel)},
                 pads, *p);
}
Action off(int ch, int note) {
  return r.route(MidiMsg{static_cast<uint8_t>(0x80 | ch), static_cast<uint8_t>(note), 0}, pads, *p);
}
Action cc(int ch, int num, int val) {
  return r.route(MidiMsg{static_cast<uint8_t>(0xB0 | ch), static_cast<uint8_t>(num), static_cast<uint8_t>(val)},
                 pads, *p);
}

void learnBank(PadMode m, int ch, int base) {
  pads.ch[static_cast<int>(m)] = static_cast<uint8_t>(ch);
  pads.base[static_cast<int>(m)] = static_cast<uint8_t>(base);
}

void bind(int i, int ch, int num, int rel) {
  p->knobs[i].screen = 1;
  p->knobs[i].ch = static_cast<uint8_t>(ch);
  p->knobs[i].cc = static_cast<uint8_t>(num);
  p->knobs[i].rel = static_cast<uint8_t>(rel);
}

void assertAct(const Action& a, ActKind k, int ax, int bx) {
  TEST_ASSERT_EQUAL(static_cast<int>(k), static_cast<int>(a.kind));
  TEST_ASSERT_EQUAL(ax, a.a);
  TEST_ASSERT_EQUAL(bx, a.b);
}

void assertNone(const Action& a) { TEST_ASSERT_EQUAL(static_cast<int>(ActKind::None), static_cast<int>(a.kind)); }

}  // namespace

void setUp() {
  p = new Project();
  pads.clear();
  r.reset();
}

void tearDown() {
  delete p;
  p = nullptr;
}

void test_learn_sets_bank() {
  r.armLearn(PadMode::Pat);
  TEST_ASSERT_EQUAL(1, r.learning());
  assertAct(on(9, 52), ActKind::LearnPad, 1, 0);
  TEST_ASSERT_EQUAL(9, pads.ch[1]);
  TEST_ASSERT_EQUAL(52, pads.base[1]);
  TEST_ASSERT_EQUAL(-1, r.learning());
  assertAct(on(9, 55), ActKind::Pat, 3, 0);  // learned bank works right away
}

void test_learn_routes_releases_and_cc_stays_armed() {
  learnBank(PadMode::Perf, 9, 68);
  r.armLearn(PadMode::Mute);
  assertAct(off(9, 39), ActKind::NoteOff, 39, 0);
  assertAct(on(9, 39, 0), ActKind::NoteOff, 39, 0);
  assertAct(off(9, 70), ActKind::PerfOff, 2, 0);
  assertAct(cc(0, 21, 64), ActKind::KnobUnbound, 21, 0);
  TEST_ASSERT_EQUAL(static_cast<int>(PadMode::Mute), r.learning());
}

void test_disarm_and_invalid_mode() {
  r.armLearn(PadMode::Pat);
  r.disarm();
  assertAct(on(9, 40, 90), ActKind::NoteOn, 40, 90);
  r.armLearn(PadMode::Count);
  TEST_ASSERT_EQUAL(-1, r.learning());
  r.armLearn(PadMode::Note);  // legacy NOTE is no longer a learn target
  TEST_ASSERT_EQUAL(-1, r.learning());
}

void test_reset_disarms_learn() {
  r.armLearn(PadMode::Pat);
  r.reset();
  TEST_ASSERT_EQUAL(-1, r.learning());
  assertAct(on(9, 36), ActKind::NoteOn, 36, 100);
}

void test_unlearned_notes_pass_through() {
  assertAct(on(9, 36, 77), ActKind::NoteOn, 36, 77);
  assertAct(off(9, 36), ActKind::NoteOff, 36, 0);
  assertAct(on(0, 0, 1), ActKind::NoteOn, 0, 1);
  assertAct(on(15, 127, 127), ActKind::NoteOn, 127, 127);
  assertAct(on(15, 127, 0), ActKind::NoteOff, 127, 0);
}

void test_legacy_note_bank_does_not_remap_notes() {
  learnBank(PadMode::Note, 9, 36);
  assertAct(on(9, 39, 77), ActKind::NoteOn, 39, 77);
  assertAct(off(9, 39), ActKind::NoteOff, 39, 0);
  assertAct(on(9, 52), ActKind::NoteOn, 52, 100);
  assertAct(on(9, 35), ActKind::NoteOn, 35, 100);
}

void test_unassigned_channel_passes_through() {
  learnBank(PadMode::Mute, 9, 36);
  assertAct(on(0, 39), ActKind::NoteOn, 39, 100);
  assertAct(off(0, 39), ActKind::NoteOff, 39, 0);
  assertAct(on(9, 39), ActKind::Mute, 3, 0);
}

void test_pat_and_mute_press_only() {
  learnBank(PadMode::Pat, 9, 52);
  assertAct(on(9, 67), ActKind::Pat, 15, 0);
  assertNone(off(9, 67));
  learnBank(PadMode::Mute, 9, 84);
  assertAct(on(9, 84), ActKind::Mute, 0, 0);
  assertNone(on(9, 84, 0));
}

void test_perf_first_eight_pads() {
  learnBank(PadMode::Perf, 9, 68);
  assertAct(on(9, 75), ActKind::PerfOn, 7, 0);
  assertAct(off(9, 75), ActKind::PerfOff, 7, 0);
  assertAct(on(9, 76), ActKind::NoteOn, 76, 100);
  assertAct(off(9, 76), ActKind::NoteOff, 76, 0);
}

void test_overlap_first_mode_wins() {
  learnBank(PadMode::Note, 9, 36);
  learnBank(PadMode::Mute, 9, 40);
  assertAct(on(9, 41, 90), ActKind::Mute, 1, 0);  // legacy NOTE cannot shadow MUTE
  assertAct(on(9, 52), ActKind::Mute, 12, 0);         // past Note's bank: Mute's
}

void test_base_near_127() {
  learnBank(PadMode::Note, 9, 120);
  assertAct(on(9, 127, 64), ActKind::NoteOn, 127, 64);
  assertAct(off(9, 127), ActKind::NoteOff, 127, 0);
  assertAct(on(9, 119), ActKind::NoteOn, 119, 100);
}

void test_cc_unbound_carries_cc_and_channel() {
  assertAct(cc(5, 74, 10), ActKind::KnobUnbound, 74, 5);
  p->knobs[0].ch = 5;  // matching ch / cc but screen 0: an unused slot
  p->knobs[0].cc = 74;
  assertAct(cc(5, 74, 10), ActKind::KnobUnbound, 74, 5);
}

void test_cc_absolute_delta() {
  bind(4, 0, 21, 0);
  assertNone(cc(0, 21, 50));  // first value: the reference only
  assertAct(cc(0, 21, 53), ActKind::Knob, 4, 3);
  assertAct(cc(0, 21, 43), ActKind::Knob, 4, -10);
  assertNone(cc(0, 21, 43));  // no change
  assertAct(cc(0, 21, 127), ActKind::Knob, 4, 84);
  assertAct(cc(0, 21, 0), ActKind::Knob, 4, -127);
}

void test_cc_relative_twos_complement() {
  bind(2, 0, 30, 1);
  assertAct(cc(0, 30, 1), ActKind::Knob, 2, 1);
  assertAct(cc(0, 30, 127), ActKind::Knob, 2, -1);
  assertAct(cc(0, 30, 5), ActKind::Knob, 2, 5);
  assertAct(cc(0, 30, 120), ActKind::Knob, 2, -8);
  assertNone(cc(0, 30, 0));   // no move
  assertNone(cc(0, 30, 64));  // -64 is no step a knob sends: no move
}

void test_cc_relative_offset64() {
  bind(3, 0, 31, 2);
  assertAct(cc(0, 31, 65), ActKind::Knob, 3, 1);
  assertAct(cc(0, 31, 63), ActKind::Knob, 3, -1);
  assertAct(cc(0, 31, 70), ActKind::Knob, 3, 6);
  assertNone(cc(0, 31, 64));
  assertAct(cc(0, 31, 0), ActKind::Knob, 3, -64);
}

void test_cc_unknown_rel_is_absolute() {
  bind(5, 0, 40, 3);
  assertNone(cc(0, 40, 65));
  assertAct(cc(0, 40, 70), ActKind::Knob, 5, 5);
}

void test_rebind_forgets_absolute() {
  bind(0, 0, 21, 0);
  assertNone(cc(0, 21, 10));
  bind(0, 0, 30, 0);  // the slot now listens to another CC
  assertNone(cc(0, 30, 100));  // no jump from the old CC's 10
  assertAct(cc(0, 30, 98), ActKind::Knob, 0, -2);
  p->reset();  // a project load: the slot's next bind starts fresh too
  bind(0, 1, 30, 0);
  assertNone(cc(1, 30, 5));
  assertAct(cc(1, 30, 6), ActKind::Knob, 0, 1);
}

void test_cc_matches_channel() {
  bind(0, 2, 21, 2);
  assertAct(cc(0, 21, 65), ActKind::KnobUnbound, 21, 0);
  assertAct(cc(2, 21, 65), ActKind::Knob, 0, 1);
}

void test_reset_forgets_absolute() {
  bind(1, 0, 22, 0);
  assertNone(cc(0, 22, 10));
  assertAct(cc(0, 22, 20), ActKind::Knob, 1, 10);
  r.reset();
  assertNone(cc(0, 22, 90));  // a jump after reconnect is not a turn
  assertAct(cc(0, 22, 91), ActKind::Knob, 1, 1);
}

void test_forget_drops_absolute() {
  bind(1, 0, 22, 0);
  assertNone(cc(0, 22, 10));
  p->knobs[1].screen = 0;  // unbound, then learned again into the same slot
  bind(1, 0, 22, 0);
  r.forget(1);
  assertNone(cc(0, 22, 90));  // no jump from the old 10
  assertAct(cc(0, 22, 92), ActKind::Knob, 1, 2);
  r.forget(-1);  // out of range: nothing
  r.forget(kKnobBinds);
}

void test_other_messages_none() {
  learnBank(PadMode::Note, 0, 0);
  bind(0, 0, 0, 0);
  assertNone(r.route(MidiMsg{0xE0, 0, 64}, pads, *p));   // pitch bend
  assertNone(r.route(MidiMsg{0xC0, 5, 0}, pads, *p));    // program change
  assertNone(r.route(MidiMsg{0xA0, 3, 40}, pads, *p));   // poly aftertouch
  assertNone(r.route(MidiMsg{0xD0, 40, 0}, pads, *p));   // channel pressure
  assertNone(r.route(MidiMsg{0xF8, 0, 0}, pads, *p));    // clock
  r.armLearn(PadMode::Perf);
  assertNone(r.route(MidiMsg{0xE0, 0, 64}, pads, *p));
  TEST_ASSERT_EQUAL(static_cast<int>(PadMode::Perf), r.learning());
}

void test_project_reset_clears_knobs() {
  bind(0, 3, 21, 1);
  p->knobs[0].track = 5;
  p->knobs[0].page = 2;
  p->knobs[0].label[0] = 'X';
  p->reset();
  TEST_ASSERT_EQUAL(0, p->knobs[0].screen);
  TEST_ASSERT_EQUAL(0, p->knobs[0].ch);
  TEST_ASSERT_EQUAL(0, p->knobs[0].cc);
  TEST_ASSERT_EQUAL(0, p->knobs[0].track);
  TEST_ASSERT_EQUAL(0, p->knobs[0].rel);
  TEST_ASSERT_EQUAL(0, p->knobs[0].label[0]);
}

// KIT: pads 1-16 are its lanes 1-16; anything else plays nothing.
void test_kit_pad_lane() {
  TEST_ASSERT_EQUAL(0, mt::kitPadLane(0));
  TEST_ASSERT_EQUAL(8, mt::kitPadLane(8));
  TEST_ASSERT_EQUAL(15, mt::kitPadLane(15));
  TEST_ASSERT_EQUAL(-1, mt::kitPadLane(16));
  TEST_ASSERT_EQUAL(-1, mt::kitPadLane(-1));
}

void test_kit_midi_base_maps_lanes_and_preserves_melody() {
  Instrument kit;
  kitSetDefaults(kit);
  kit.kit[0].note = 80;
  for (int i = 0; i < kKitLanes; ++i) {
    auto target = midiNoteTarget(36 + i, &kit, 36);
    TEST_ASSERT_EQUAL(i, target.lane);
    TEST_ASSERT_EQUAL(kit.kit[i].note, target.note);
  }
  TEST_ASSERT_EQUAL(-1, midiNoteTarget(35, &kit, 36).note);
  TEST_ASSERT_EQUAL(-1, midiNoteTarget(52, &kit, 36).note);
  TEST_ASSERT_EQUAL(36, midiNoteTarget(36, nullptr, 36).note);
  TEST_ASSERT_EQUAL(-1, midiNoteTarget(36, nullptr, 36).lane);
  TEST_ASSERT_EQUAL(80, midiNoteTarget(80, &kit, 255).note);
  TEST_ASSERT_EQUAL(0, midiNoteTarget(80, &kit, 255).lane);
  TEST_ASSERT_EQUAL(-1, midiNoteTarget(36, &kit, 255).note);
  TEST_ASSERT_EQUAL(15, midiNoteTarget(127, &kit, 112).lane);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_kit_midi_base_maps_lanes_and_preserves_melody);
  RUN_TEST(test_learn_sets_bank);
  RUN_TEST(test_learn_routes_releases_and_cc_stays_armed);
  RUN_TEST(test_disarm_and_invalid_mode);
  RUN_TEST(test_reset_disarms_learn);
  RUN_TEST(test_unlearned_notes_pass_through);
  RUN_TEST(test_legacy_note_bank_does_not_remap_notes);
  RUN_TEST(test_unassigned_channel_passes_through);
  RUN_TEST(test_pat_and_mute_press_only);
  RUN_TEST(test_perf_first_eight_pads);
  RUN_TEST(test_overlap_first_mode_wins);
  RUN_TEST(test_base_near_127);
  RUN_TEST(test_cc_unbound_carries_cc_and_channel);
  RUN_TEST(test_cc_absolute_delta);
  RUN_TEST(test_cc_relative_twos_complement);
  RUN_TEST(test_cc_relative_offset64);
  RUN_TEST(test_cc_unknown_rel_is_absolute);
  RUN_TEST(test_rebind_forgets_absolute);
  RUN_TEST(test_cc_matches_channel);
  RUN_TEST(test_reset_forgets_absolute);
  RUN_TEST(test_forget_drops_absolute);
  RUN_TEST(test_other_messages_none);
  RUN_TEST(test_project_reset_clears_knobs);
  RUN_TEST(test_kit_pad_lane);
  return UNITY_END();
}
