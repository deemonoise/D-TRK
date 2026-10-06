#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include <vector>
#include "sequencer.h"

using namespace mt;

struct Rec {
  uint64_t t;
  uint8_t b[3];
  uint8_t len;
};

struct SynRec {
  uint64_t t;   // scheduled time passed to synth()
  uint64_t at;  // when it was sent (sink->now)
  uint8_t track;
  uint8_t b[3];
  uint8_t len;
};

struct FakeSink : MidiSink {
  std::vector<Rec> log;
  std::vector<SynRec> syn;  // messages for INT tracks
  uint64_t now = 0;
  void send(const uint8_t* b, uint8_t len) override {
    Rec r{now, {0, 0, 0}, len};
    memcpy(r.b, b, len);
    log.push_back(r);
  }
  void synth(uint64_t t, uint8_t track, const uint8_t* b, uint8_t len) override {
    SynRec r{t, now, track, {0, 0, 0}, len};
    memcpy(r.b, b, len);
    syn.push_back(r);
  }
  // Synth messages whose high nibble (or whole status for 0xF_) matches.
  std::vector<SynRec> synKind(uint8_t kind) const {
    std::vector<SynRec> out;
    for (const auto& r : syn)
      if ((kind >= 0xF0 ? r.b[0] : (r.b[0] & 0xF0)) == kind) out.push_back(r);
    return out;
  }
  size_t sendNotes() const {
    size_t n = 0;
    for (const auto& r : log)
      if ((r.b[0] & 0xF0) == 0x80 || (r.b[0] & 0xF0) == 0x90) ++n;
    return n;
  }
  std::vector<uint64_t> times(uint8_t status, int note = -1) const {
    std::vector<uint64_t> out;
    for (const auto& r : log)
      if (r.b[0] == status && (note < 0 || r.b[1] == note)) out.push_back(r.t);
    return out;
  }
};

static Project* p;
static Sequencer* seq;
static FakeSink* sink;

// Event-driven loop: jumps to the time the sequencer asks for.
static void run(uint64_t from, uint64_t to) {
  uint64_t t = from;
  while (t <= to) {
    sink->now = t;
    const uint64_t next = seq->process(t, *sink);
    if (next == kNever) break;
    t = next > t ? next : t + 1;
  }
}

static void fillSteps(int pat, uint8_t note) {
  for (int i = 0; i < p->patterns[pat].length; ++i) p->patterns[pat].steps[0][i].note = note;
}

static std::vector<uint64_t> between(const std::vector<uint64_t>& v, uint64_t a, uint64_t b) {
  std::vector<uint64_t> out;
  for (uint64_t x : v)
    if (x >= a && x < b) out.push_back(x);
  return out;
}

// Tests here check MIDI output: every track on MIDI (new projects default to INT).
static Project* midiProject() {
  Project* q = new Project();
  for (TrackCfg& c : q->tracks) c.out = TrackOut::Midi;
  return q;
}

void setUp() {
  p = midiProject();
  seq = new Sequencer(*p);
  seq->seed(1);
  sink = new FakeSink();
}
void tearDown() {
  delete sink;
  delete seq;
  delete p;
}

void test_start_sends_start_then_clock() {
  sink->now = 1000;
  seq->start(1000, *sink);
  run(1000, 1000);
  TEST_ASSERT_EQUAL_HEX8(0xFA, sink->log[0].b[0]);
  TEST_ASSERT_EQUAL_HEX8(0xF8, sink->log[1].b[0]);
  TEST_ASSERT_EQUAL(1000, sink->log[1].t);
}

void test_clock_24ppqn_at_120() {
  seq->start(0, *sink);
  run(0, 500000);
  TEST_ASSERT_EQUAL(25, sink->times(0xF8).size());
}

void test_sixteenths_at_120() {
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][1].note = 60;
  seq->start(0, *sink);
  run(0, 200000);
  auto on = sink->times(0x90, 60);
  TEST_ASSERT_EQUAL(2, on.size());
  TEST_ASSERT_EQUAL(0, on[0]);
  TEST_ASSERT_EQUAL(125000, on[1]);
  auto off = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(62500, off[0]);
}

void test_swing_delays_odd_steps() {
  p->patterns[0].swing = 75;
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][1].note = 60;
  seq->start(0, *sink);
  run(0, 200000);
  TEST_ASSERT_EQUAL(187500, sink->times(0x90, 60)[1]);
}

void test_queued_pattern_switches_at_loop_end() {
  p->patterns[0].length = 4;
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[1].length = 4;
  p->patterns[1].steps[0][0].note = 62;
  seq->start(0, *sink);
  seq->queuePattern(1);
  run(0, 510000);
  TEST_ASSERT_EQUAL(1, sink->times(0x90, 60).size());
  auto on62 = sink->times(0x90, 62);
  TEST_ASSERT_EQUAL(1, on62.size());
  TEST_ASSERT_EQUAL(500000, on62[0]);
  TEST_ASSERT_EQUAL(1, seq->pattern());
}

void test_pause_silences_and_stops_output() {
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][0].fx[0] = {Fx::GAT, 200};
  seq->start(0, *sink);
  run(0, 100000);
  sink->now = 100000;
  seq->pause(100000, *sink);
  const size_t n = sink->log.size();
  TEST_ASSERT_EQUAL_HEX8(0xFC, sink->log[n - 1].b[0]);
  TEST_ASSERT_EQUAL_HEX8(0x80, sink->log[n - 2].b[0]);
  TEST_ASSERT_EQUAL(60, sink->log[n - 2].b[1]);
  run(100000, 3000000);
  TEST_ASSERT_EQUAL(n, sink->log.size());
}

void test_resume_sends_song_position_and_continue() {
  seq->start(0, *sink);
  run(0, 300000);
  sink->now = 300000;
  seq->pause(300000, *sink);  // step 3 was scheduled for 375000 but not reached
  sink->now = 400000;
  seq->resume(400000, *sink);
  const size_t n = sink->log.size();
  TEST_ASSERT_EQUAL_HEX8(0xF2, sink->log[n - 2].b[0]);
  TEST_ASSERT_EQUAL(3, sink->log[n - 2].b[1]);
  TEST_ASSERT_EQUAL(0, sink->log[n - 2].b[2]);
  TEST_ASSERT_EQUAL_HEX8(0xFB, sink->log[n - 1].b[0]);
}

void test_retrigger_ignores_stale_note_off() {
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][0].fx[0] = {Fx::GAT, 200};  // off would land at 1 000 000
  p->patterns[0].steps[0][1].note = 60;
  seq->start(0, *sink);
  run(0, 1100000);
  auto off = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(2, off.size());
  TEST_ASSERT_EQUAL(125000, off[0]);  // retrigger
  TEST_ASSERT_EQUAL(187500, off[1]);  // second note's own gate
}

void test_muted_track_is_silent() {
  p->tracks[0].mute = true;
  p->patterns[0].steps[0][0].note = 60;
  seq->start(0, *sink);
  run(0, 200000);
  TEST_ASSERT_EQUAL(0, sink->times(0x90).size());
}

void test_drum_track_plays_lane_notes() {
  // Track 0 on a KIT: the step's vel is a lane mask, lanes 0 and 2 -> notes 60 and 62.
  instrSetType(p->instruments[p->tracks[0].instr], InstrType::Kit);
  Step& s = p->patterns[0].steps[0][0];
  s.note = 0;
  s.vel = 0b00000101;
  seq->start(0, *sink);
  run(0, 200000);
  const uint8_t on = static_cast<uint8_t>(0x90 | (p->tracks[0].channel & 0x0F));
  TEST_ASSERT_EQUAL(1, sink->times(on, 60).size());
  TEST_ASSERT_EQUAL(1, sink->times(on, 62).size());
  TEST_ASSERT_EQUAL(0, sink->times(on, 61).size());
  TEST_ASSERT_EQUAL(2, sink->times(on).size());
}

void test_activity_marks_sounding_tracks() {
  p->patterns[0].steps[2][0].note = 60;
  p->patterns[0].steps[5][0].note = 62;
  p->tracks[5].mute = true;
  seq->start(0, *sink);
  TEST_ASSERT_EQUAL_HEX16(0, seq->takeActivity());
  run(0, 1000);
  TEST_ASSERT_EQUAL_HEX16(1 << 2, seq->takeActivity());
  TEST_ASSERT_EQUAL_HEX16(0, seq->takeActivity());  // cleared by take
}

void test_track_15_plays() {
  p->patterns[0].steps[kTracks - 1][0].note = 60;
  seq->start(0, *sink);
  run(0, 1000);
  const uint8_t on = static_cast<uint8_t>(0x90 | (p->tracks[kTracks - 1].channel & 0x0F));
  TEST_ASSERT_EQUAL(1, sink->times(on, 60).size());
  TEST_ASSERT_EQUAL_HEX16(1u << (kTracks - 1), seq->takeActivity());
}

void test_tie_holds_until_next_note_with_overlap() {
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][0].fx[0] = {Fx::TIE, 0};
  p->patterns[0].steps[0][2].note = 64;
  seq->start(0, *sink);
  run(0, 300000);
  auto off60 = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(1, off60.size());
  TEST_ASSERT_EQUAL(251000, off60[0]);
  TEST_ASSERT_EQUAL(250000, sink->times(0x90, 64)[0]);
}

void test_set_bpm_while_playing() {
  fillSteps(0, 60);
  seq->start(0, *sink);
  run(0, 250000);
  seq->setBpm(240);
  run(250000, 700000);
  auto on = sink->times(0x90, 60);
  TEST_ASSERT_TRUE(on.size() >= 7);
  TEST_ASSERT_EQUAL(250000, on[2]);
  TEST_ASSERT_TRUE(on[3] > 250000 && on[3] < 375000);  // step 3 moved to the new tempo
  for (size_t i = 4; i < on.size(); ++i) {
    const int64_t d = static_cast<int64_t>(on[i] - on[i - 1]) - 62500;
    TEST_ASSERT_TRUE(d >= -1 && d <= 1);
  }
  TEST_ASSERT_EQUAL(240, p->bpm);
}

void test_stop_rewinds() {
  seq->start(0, *sink);
  run(0, 300000);
  seq->stop(300000, *sink);
  TEST_ASSERT_FALSE(seq->playing());
  TEST_ASSERT_EQUAL_HEX8(0xFC, sink->log.back().b[0]);
  sink->log.clear();
  p->patterns[0].steps[0][0].note = 60;
  seq->start(400000, *sink);
  run(400000, 400000);
  TEST_ASSERT_EQUAL(400000, sink->times(0x90, 60)[0]);
}


void test_resolution_change_mid_play_keeps_time() {
  fillSteps(0, 60);
  seq->start(0, *sink);
  run(0, 60000000);
  p->patterns[0].res = Resolution::ThirtySecond;
  run(60000000, 61000000);
  auto on = between(sink->times(0x90, 60), 60000000, 61000000);
  TEST_ASSERT_TRUE(between(on, 60000000, 60100000).size() <= 2);
  TEST_ASSERT_TRUE(on.size() > 10);
  for (size_t i = 1; i < on.size(); ++i) {
    TEST_ASSERT_TRUE(on[i] - on[i - 1] >= 62500 && on[i] - on[i - 1] <= 125000);
    if (on[i - 1] >= 60500000) TEST_ASSERT_EQUAL(62500, on[i] - on[i - 1]);
  }
}

void test_set_bpm_keeps_steps_on_clock_grid() {
  fillSteps(0, 60);
  seq->start(0, *sink);
  run(0, 1000000);
  seq->setBpm(60);
  run(1000000, 6000000);
  const auto clk = sink->times(0xF8);
  const auto on = between(sink->times(0x90, 60), 1000001, 6000000);
  TEST_ASSERT_TRUE(on.size() > 10);
  TEST_ASSERT_EQUAL(1229166, on[0]);  // step 9, rescheduled on the new grid
  for (size_t i = 1; i < on.size(); ++i) TEST_ASSERT_EQUAL(250000, on[i] - on[i - 1]);
  for (uint64_t t : on) {
    size_t idx = clk.size();
    for (size_t i = 0; i < clk.size(); ++i)
      if (clk[i] == t) idx = i;
    TEST_ASSERT_TRUE_MESSAGE(idx < clk.size(), "step NoteOn without a clock");
    TEST_ASSERT_EQUAL(0, idx % 6);
  }
}

void test_direct_bpm_write_mid_play() {
  fillSteps(0, 60);
  seq->start(0, *sink);
  run(0, 10000000);
  p->bpm = 140;
  run(10000000, 12000000);
  auto on = between(sink->times(0x90, 60), 10000000, 12000000);
  TEST_ASSERT_TRUE(between(on, 10000000, 10250000).size() <= 3);
  for (size_t i = 1; i < on.size(); ++i) {
    TEST_ASSERT_TRUE(on[i] - on[i - 1] >= 90000);
    if (on[i - 1] > 10000000) {
      const int64_t d = static_cast<int64_t>(on[i] - on[i - 1]) - 625000 * 24 / 140;
      TEST_ASSERT_TRUE(d >= -1 && d <= 1);
    }
  }
  TEST_ASSERT_EQUAL(140, p->bpm);
}

void test_tie_released_when_track_muted() {
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][0].fx[0] = {Fx::TIE, 0};
  p->patterns[0].steps[0][4].note = 64;
  seq->start(0, *sink);
  run(0, 50000);
  p->tracks[0].mute = true;
  run(50000, 600000);
  auto off60 = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(1, off60.size());
  TEST_ASSERT_EQUAL(501000, off60[0]);
  TEST_ASSERT_EQUAL(0, sink->times(0x90, 64).size());
}

void test_tie_to_same_pitch_extends() {
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][0].fx[0] = {Fx::TIE, 0};
  p->patterns[0].steps[0][2].note = 60;
  seq->start(0, *sink);
  run(0, 370000);
  TEST_ASSERT_EQUAL(1, sink->times(0x90, 60).size());
  auto off = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(1, off.size());
  TEST_ASSERT_EQUAL(312500, off[0]);
}

void test_tie_to_same_pitch_tied_again_keeps_holding() {
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][0].fx[0] = {Fx::TIE, 0};
  p->patterns[0].steps[0][2].note = 60;
  p->patterns[0].steps[0][2].fx[0] = {Fx::TIE, 0};
  p->patterns[0].steps[0][4].note = 64;
  seq->start(0, *sink);
  run(0, 600000);
  TEST_ASSERT_EQUAL(1, sink->times(0x90, 60).size());
  auto off = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(1, off.size());
  TEST_ASSERT_EQUAL(501000, off[0]);
}

void test_play_pos_with_swing() {
  p->patterns[0].swing = 75;
  seq->start(0, *sink);
  run(0, 380000);
  sink->now = 380000;
  seq->process(380000, *sink);
  TEST_ASSERT_EQUAL(2, seq->playPos());
}

void test_pause_after_queued_switch_in_lookahead() {
  p->patterns[0].steps[0][15].note = 50;
  p->patterns[1].steps[0][0].note = 72;
  p->patterns[1].steps[0][15].note = 70;
  seq->start(0, *sink);
  seq->queuePattern(1);
  run(0, 1800000);  // step 15 is scheduled at 1750000 for 1875000
  sink->now = 1800000;
  seq->pause(1800000, *sink);
  TEST_ASSERT_EQUAL(0, sink->times(0x90, 50).size());
  TEST_ASSERT_EQUAL(0, seq->pattern());
  TEST_ASSERT_EQUAL(1, seq->queued());
  sink->now = 3000000;
  seq->resume(3000000, *sink);
  const size_t n = sink->log.size();
  TEST_ASSERT_EQUAL_HEX8(0xF2, sink->log[n - 2].b[0]);
  TEST_ASSERT_EQUAL(15, sink->log[n - 2].b[1]);
  run(3000000, 5100000);
  auto on50 = sink->times(0x90, 50);
  TEST_ASSERT_EQUAL(1, on50.size());
  TEST_ASSERT_EQUAL(3000000, on50[0]);
  TEST_ASSERT_EQUAL(3125000, sink->times(0x90, 72)[0]);
  TEST_ASSERT_EQUAL(5000000, sink->times(0x90, 70)[0]);
  TEST_ASSERT_EQUAL(1, seq->pattern());
}

void test_length_shrink_applies_queued_switch() {
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[1].steps[0][0].note = 62;
  seq->start(0, *sink);
  seq->queuePattern(1);
  run(0, 375000);  // step 5 scheduled, pos 6 next
  p->patterns[0].length = 4;
  run(500000, 800000);
  TEST_ASSERT_EQUAL(1, sink->times(0x90, 60).size());
  auto on62 = sink->times(0x90, 62);
  TEST_ASSERT_EQUAL(1, on62.size());
  TEST_ASSERT_EQUAL(750000, on62[0]);
  TEST_ASSERT_EQUAL(1, seq->pattern());
}

void test_negative_nudge_at_start_is_clamped() {
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][0].fx[0] = {Fx::NDG, static_cast<uint8_t>(-50)};
  sink->now = 1000000;
  seq->start(1000000, *sink);
  run(1000000, 1100000);
  auto on = sink->times(0x90, 60);
  auto off = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(1, on.size());
  TEST_ASSERT_EQUAL(1000000, on[0]);
  TEST_ASSERT_EQUAL(1, off.size());
  TEST_ASSERT_EQUAL(1062500, off[0]);
}

void test_select_pattern_switches_from_next_step() {
  p->patterns[0].steps[0][5].note = 60;
  p->patterns[1].res = Resolution::ThirtySecond;
  p->patterns[1].steps[0][5].note = 72;
  p->patterns[1].steps[0][6].note = 73;
  seq->start(0, *sink);
  run(0, 300000);  // steps up to 4 scheduled, step 5 next at 625000
  seq->selectPattern(1);
  run(300000, 800000);
  TEST_ASSERT_EQUAL(0, sink->times(0x90, 60).size());
  TEST_ASSERT_EQUAL(625000, sink->times(0x90, 72)[0]);
  TEST_ASSERT_EQUAL(687500, sink->times(0x90, 73)[0]);
}

void test_set_bpm_faster_keeps_step_order() {
  for (int i = 0; i < 16; ++i) p->patterns[0].steps[0][i].note = 40 + i;
  p->bpm = 40;
  seq->start(0, *sink);
  run(0, 1000000);
  seq->setBpm(300);
  run(1000000, 1500000);  // one pass at 300 bpm is 800 ms
  auto on43 = sink->times(0x90, 43), on44 = sink->times(0x90, 44);
  auto off43 = sink->times(0x80, 43);
  TEST_ASSERT_EQUAL(1, on43.size());
  TEST_ASSERT_EQUAL(1, on44.size());
  TEST_ASSERT_TRUE(on43[0] > 1000000 && on43[0] < on44[0]);
  TEST_ASSERT_TRUE(off43[0] <= on44[0]);
  const int64_t d = static_cast<int64_t>(on44[0] - on43[0]) - 50000;
  TEST_ASSERT_TRUE(d >= -1 && d <= 1);
}

void test_long_uptime_does_not_overflow() {
  const uint64_t t0 = 800ull * 86400 * 1000000;  // 800 days in us
  p->bpm = 300;
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][1].note = 60;
  sink->now = t0;
  seq->start(t0, *sink);
  run(t0, t0 + 60000);
  auto on = sink->times(0x90, 60);
  TEST_ASSERT_EQUAL(2, on.size());
  TEST_ASSERT_TRUE(on[0] == t0);
  TEST_ASSERT_TRUE(on[1] == t0 + 50000);
}

void test_resolution_growth_keeps_nudge_on_time() {
  p->patterns[0].res = Resolution::ThirtySecond;
  for (int i = 0; i < 16; ++i) {
    p->patterns[0].steps[0][i].note = 60;
    p->patterns[0].steps[0][i].fx[0] = {Fx::NDG, static_cast<uint8_t>(-50)};
  }
  seq->start(0, *sink);
  run(0, 100000);
  p->patterns[0].res = Resolution::Quarter;
  run(100000, 3000000);
  auto on = between(sink->times(0x90, 60), 300000, 3000000);
  TEST_ASSERT_TRUE(on.size() >= 4);
  for (uint64_t t : on) TEST_ASSERT_EQUAL(0, (t - 125000) % 500000);  // half a quarter early
}

void test_late_process_keeps_gate() {
  p->patterns[0].steps[0][2].note = 60;  // 250000, the latest step due at 300000
  seq->start(0, *sink);
  sink->now = 300000;
  seq->process(300000, *sink);
  run(300000, 400000);
  auto on = sink->times(0x90, 60);
  auto off = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(1, on.size());
  TEST_ASSERT_EQUAL(300000, on[0]);
  TEST_ASSERT_EQUAL(1, off.size());
  TEST_ASSERT_EQUAL(362500, off[0]);
}

void test_pause_keeps_pattern_queued_later() {
  p->patterns[0].steps[0][15].note = 50;
  p->patterns[1].steps[0][0].note = 71;
  p->patterns[2].steps[0][0].note = 72;
  seq->start(0, *sink);
  seq->queuePattern(1);
  run(0, 1800000);  // the switch to 1 is already scheduled
  seq->queuePattern(2);
  sink->now = 1800000;
  seq->pause(1800000, *sink);
  TEST_ASSERT_EQUAL(0, seq->pattern());
  TEST_ASSERT_EQUAL(2, seq->queued());
  sink->now = 3000000;
  seq->resume(3000000, *sink);
  run(3000000, 3200000);
  TEST_ASSERT_EQUAL(3000000, sink->times(0x90, 50)[0]);
  TEST_ASSERT_EQUAL(3125000, sink->times(0x90, 72)[0]);
  TEST_ASSERT_EQUAL(0, sink->times(0x90, 71).size());
}

void test_pause_keeps_select_immediate() {
  p->patterns[0].steps[0][3].note = 60;
  p->patterns[1].steps[0][3].note = 61;
  seq->start(0, *sink);
  run(0, 300000);  // step 3 (375000) scheduled, not heard
  seq->selectPattern(1);
  sink->now = 300000;
  seq->pause(300000, *sink);
  TEST_ASSERT_EQUAL(1, seq->pattern());
  TEST_ASSERT_EQUAL(-1, seq->queued());
  sink->now = 1000000;
  seq->resume(1000000, *sink);
  run(1000000, 1100000);
  TEST_ASSERT_EQUAL(0, sink->times(0x90, 60).size());
  TEST_ASSERT_EQUAL(1000000, sink->times(0x90, 61)[0]);
}

void test_tie_extension_after_other_track_took_the_voice() {
  p->tracks[1].channel = p->tracks[0].channel;
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][0].fx[0] = {Fx::TIE, 0};
  p->patterns[0].steps[1][1].note = 60;  // same channel, retriggers and ends it
  p->patterns[0].steps[0][2].note = 60;
  seq->start(0, *sink);
  run(0, 370000);
  auto on = sink->times(0x90, 60);
  TEST_ASSERT_EQUAL(3, on.size());
  TEST_ASSERT_EQUAL(250000, on[2]);
  TEST_ASSERT_EQUAL(312500, sink->times(0x80, 60).back());
}

static void checkResume(Resolution r, uint8_t pausePos, uint64_t pauseAt, uint8_t spp, uint64_t offset) {
  p->patterns[0].res = r;
  p->patterns[0].steps[0][pausePos].note = 60;
  seq->start(0, *sink);
  run(0, pauseAt);
  sink->now = pauseAt;
  seq->pause(pauseAt, *sink);
  sink->log.clear();
  sink->now = 1000000;
  seq->resume(1000000, *sink);
  TEST_ASSERT_EQUAL_HEX8(0xF2, sink->log[0].b[0]);
  TEST_ASSERT_EQUAL(spp, sink->log[0].b[1]);
  run(1000000, 1200000);
  TEST_ASSERT_EQUAL(1000000, sink->times(0xF8)[0]);
  auto on = sink->times(0x90, 60);
  TEST_ASSERT_EQUAL(1, on.size());
  TEST_ASSERT_EQUAL(1000000 + offset, on[0]);
}

void test_resume_thirty_second_position() {
  checkResume(Resolution::ThirtySecond, 3, 150000, 1, 62500);  // 12 ticks after the clock origin
}

void test_resume_eighth_triplet_position() {
  checkResume(Resolution::EighthTriplet, 2, 300000, 2, 83333);  // 16 ticks
}

void test_set_bpm_rewinds_step_with_pending_late_note() {
  for (int i = 0; i < 16; ++i) p->patterns[0].steps[0][i].note = 40 + i;
  p->patterns[0].steps[0][3].fx[0] = {Fx::NDG, 50};  // step 3 grid 1125000, NoteOn 1312500
  p->bpm = 40;
  seq->start(0, *sink);
  run(0, 1150000);
  seq->setBpm(300);
  sink->now = 1150000;
  seq->process(1150000, *sink);
  run(1150000, 1700000);
  auto on43 = sink->times(0x90, 43), on44 = sink->times(0x90, 44);
  TEST_ASSERT_EQUAL(1, on43.size());
  TEST_ASSERT_EQUAL(1, on44.size());
  TEST_ASSERT_TRUE(on43[0] < on44[0]);
}

static void assertNotStuck(uint8_t note) {
  auto on = sink->times(0x90, note), off = sink->times(0x80, note);
  TEST_ASSERT_TRUE(on.size() > 0);
  TEST_ASSERT_EQUAL(on.size(), off.size());
  TEST_ASSERT_TRUE(off.back() > on.back());
}

void test_tie_release_after_late_ratchet_with_swing() {
  p->patterns[0].swing = 75;
  p->patterns[0].steps[0][1].note = 60;  // odd: 187500, ratchets every 31250, tied one at 281250
  p->patterns[0].steps[0][1].fx[0] = {Fx::RAT, 4};
  p->patterns[0].steps[0][1].fx[1] = {Fx::TIE, 0};
  p->patterns[0].steps[0][2].note = 64;  // 250000, before the tied NoteOn
  seq->start(0, *sink);
  run(0, 600000);
  assertNotStuck(60);
  TEST_ASSERT_EQUAL(281250 + kMinGateUs, sink->times(0x80, 60).back());
}

void test_tie_release_after_tempo_jump() {
  p->bpm = 40;
  p->patterns[0].res = Resolution::Quarter;
  p->patterns[0].steps[0][0].note = 60;  // NoteOns at 0 and 750000, the second tied
  p->patterns[0].steps[0][0].fx[0] = {Fx::RAT, 2};
  p->patterns[0].steps[0][0].fx[1] = {Fx::TIE, 0};
  p->patterns[0].steps[0][1].note = 64;
  seq->start(0, *sink);
  run(0, 100000);
  seq->setBpm(300);
  run(100000, 1500000);
  assertNotStuck(60);
}

void test_pause_plays_events_due_before_it() {
  p->patterns[0].steps[0][2].note = 62;  // 250000
  seq->start(0, *sink);
  run(0, 200000);  // last process before the step
  sink->now = 260000;
  seq->pause(260000, *sink);
  TEST_ASSERT_EQUAL(1, sink->times(0x90, 62).size());
  sink->now = 1000000;
  seq->resume(1000000, *sink);
  const size_t n = sink->log.size();
  TEST_ASSERT_EQUAL_HEX8(0xF2, sink->log[n - 2].b[0]);
  TEST_ASSERT_EQUAL(3, sink->log[n - 2].b[1]);
}

void test_stall_skips_missed_steps() {
  fillSteps(0, 60);
  for (int i = 0; i < 16; ++i) p->patterns[0].steps[1][i].note = 61;
  seq->start(0, *sink);
  run(0, 100000);
  sink->now = 2100000;  // the engine stalled for 2 s
  seq->process(2100000, *sink);
  run(2100000, 2600000);
  std::vector<uint64_t> on;
  for (const auto& r : sink->log)
    if ((r.b[0] & 0xF0) == 0x90 && r.t >= 2100000) on.push_back(r.t);
  for (size_t i = 0; i < on.size(); ++i) {
    size_t same = 0;
    for (uint64_t t : on) same += t == on[i];
    TEST_ASSERT_TRUE(same <= 2);  // one step of two tracks
  }
  TEST_ASSERT_EQUAL(2125000, between(sink->times(0x90, 60), 2100001, 2600000)[0]);  // back on the grid
  TEST_ASSERT_EQUAL(2600000 / 20833 + 1, sink->times(0xF8).size());  // the slave keeps its position
}

void test_tied_again_continuation_is_released() {
  p->tracks[1].channel = p->tracks[0].channel;
  p->patterns[0].swing = 75;
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][0].fx[0] = {Fx::TIE, 0};
  p->patterns[0].steps[0][1].note = 60;  // 187500 + 62500 = 250000
  p->patterns[0].steps[0][1].fx[0] = {Fx::TIE, 0};
  p->patterns[0].steps[0][1].fx[1] = {Fx::NDG, 50};
  p->patterns[0].steps[0][2].note = kNoteOff;  // 250000, same time as the continuation
  p->patterns[0].steps[1][1].note = 60;        // steals the voice
  p->patterns[0].steps[1][1].fx[0] = {Fx::GAT, 10};
  seq->start(0, *sink);
  run(0, 600000);
  assertNotStuck(60);
}

// Like run(), checking what the UI would show after every process().
static void runShown(uint64_t from, uint64_t to) {
  uint64_t t = from;
  while (t <= to) {
    sink->now = t;
    const uint64_t next = seq->process(t, *sink);
    TEST_ASSERT_TRUE(seq->playPos() < p->patterns[seq->heardPattern()].length);
    if (next == kNever) break;
    t = next > t ? next : t + 1;
  }
}

void test_shown_position_matches_shown_pattern() {
  p->patterns[1].length = 4;
  p->patterns[2].length = 4;
  seq->start(0, *sink);
  seq->queuePattern(1);
  runShown(0, 1800000);  // the switch to 1 is scheduled, not heard
  TEST_ASSERT_EQUAL(0, seq->heardPattern());
  TEST_ASSERT_EQUAL(1, seq->pendingPattern());
  runShown(1800000, 2100000);
  TEST_ASSERT_EQUAL(1, seq->heardPattern());
  TEST_ASSERT_EQUAL(-1, seq->pendingPattern());
  seq->selectPattern(0);
  runShown(2100000, 2600000);
  seq->selectPattern(2);
  runShown(2600000, 4000000);
  TEST_ASSERT_EQUAL(2, seq->heardPattern());
}

void test_cca_sends_control_change() {
  p->patterns[0].steps[0][0].fx[0] = {Fx::CCA, 77};
  seq->start(0, *sink);
  run(0, 100000);
  int n = 0;
  for (const auto& r : sink->log) {
    if (r.b[0] != 0xB0) continue;
    ++n;
    TEST_ASSERT_EQUAL(3, r.len);
    TEST_ASSERT_EQUAL_HEX8(0x4A, r.b[1]);
    TEST_ASSERT_EQUAL(77, r.b[2]);
    TEST_ASSERT_EQUAL(0, r.t);
  }
  TEST_ASSERT_EQUAL(1, n);
}

void test_muted_track_sends_no_controls() {
  p->patterns[0].steps[0][0].fx[0] = {Fx::CCA, 77};
  p->tracks[0].mute = true;
  seq->start(0, *sink);
  run(0, 100000);
  TEST_ASSERT_EQUAL(0, sink->times(0xB0).size());
}

void test_off_step_releases_tie_then_sends_control() {
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][0].fx[0] = {Fx::TIE, 0};
  p->patterns[0].steps[0][2].note = kNoteOff;
  p->patterns[0].steps[0][2].fx[0] = {Fx::PBN, 0};
  seq->start(0, *sink);
  run(0, 300000);
  auto off = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(1, off.size());
  TEST_ASSERT_EQUAL(250000, off[0]);
  auto pb = sink->times(0xE0);
  TEST_ASSERT_EQUAL(1, pb.size());
  TEST_ASSERT_EQUAL(250000, pb[0]);
}

void test_cnd_one_of_two_across_passes() {
  p->patterns[0].length = 4;
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][0].fx[0] = {Fx::CND, 0x12};
  seq->start(0, *sink);
  run(0, 1900000);
  auto on = sink->times(0x90, 60);
  TEST_ASSERT_EQUAL(2, on.size());
  TEST_ASSERT_EQUAL(0, on[0]);
  TEST_ASSERT_EQUAL(1000000, on[1]);
}

void test_tie_extension_with_program_on_step() {
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][0].fx[0] = {Fx::TIE, 0};
  p->patterns[0].steps[0][2].note = 60;
  p->patterns[0].steps[0][2].fx[0] = {Fx::PGM, 5};
  seq->start(0, *sink);
  run(0, 370000);
  TEST_ASSERT_EQUAL(1, sink->times(0x90, 60).size());
  auto off = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(1, off.size());
  TEST_ASSERT_EQUAL(312500, off[0]);
  int n = 0;
  for (const auto& r : sink->log) {
    if (r.b[0] != 0xC0) continue;
    ++n;
    TEST_ASSERT_EQUAL(2, r.len);
    TEST_ASSERT_EQUAL(5, r.b[1]);
    TEST_ASSERT_EQUAL(250000, r.t);
  }
  TEST_ASSERT_EQUAL(1, n);
}

void test_controls_precede_same_time_notes_on_all_tracks() {
  const Fx ctl[] = {Fx::PGM, Fx::CCA, Fx::PBN};
  for (int tr = 0; tr < kTracks; ++tr) {
    for (int i = 0; i < p->patterns[0].length; ++i) {
      Step& s = p->patterns[0].steps[tr][i];
      s.note = static_cast<uint8_t>(48 + tr + i);
      s.fx[0] = {ctl[(tr + i) % 3], static_cast<uint8_t>(i)};
      s.fx[1] = (tr + i) & 1 ? FxSlot{Fx::RAT, 4} : FxSlot{Fx::CHD, 0};
    }
  }
  seq->start(0, *sink);
  run(0, 4000000);
  int checked = 0;
  const auto& log = sink->log;
  for (size_t i = 0; i < log.size(); ++i) {
    const uint8_t k = log[i].b[0] & 0xF0;
    if (k != 0xB0 && k != 0xC0 && k != 0xE0) continue;
    ++checked;
    for (size_t j = 0; j < i; ++j) {
      const bool sameOn = (log[j].b[0] & 0xF0) == 0x90 && (log[j].b[0] & 15) == (log[i].b[0] & 15) && log[j].t == log[i].t;
      TEST_ASSERT_FALSE(sameOn);
    }
  }
  TEST_ASSERT_TRUE(checked > 200);
}

static std::vector<uint8_t> programsFrom(uint64_t t0) {
  std::vector<uint8_t> out;
  for (const auto& r : sink->log)
    if (r.b[0] == 0xC0 && r.t >= t0) out.push_back(r.b[1]);
  return out;
}

void test_stall_keeps_controls_of_skipped_steps() {
  p->patterns[0].steps[0][4].fx[0] = {Fx::PGM, 5};
  p->patterns[0].steps[0][6].note = 60;
  p->patterns[0].steps[0][6].fx[0] = {Fx::PGM, 7};
  p->patterns[0].steps[1][8].fx[0] = {Fx::CND, 0x22};  // fails on pass 0: not sent
  p->patterns[0].steps[1][8].fx[1] = {Fx::CCA, 9};
  seq->start(0, *sink);
  run(0, 100000);
  sink->now = 1300000;  // the engine stalled
  seq->process(1300000, *sink);
  run(1300000, 1400000);
  auto prg = programsFrom(1300000);
  TEST_ASSERT_EQUAL(2, prg.size());
  TEST_ASSERT_EQUAL(5, prg[0]);
  TEST_ASSERT_EQUAL(7, prg[1]);
  TEST_ASSERT_EQUAL(0, sink->times(0x90, 60).size());
  TEST_ASSERT_EQUAL(0, sink->times(0xB1).size());
}

void test_send_program_now() {
  p->tracks[2].channel = 5;
  p->tracks[2].program = 10;
  seq->sendProgram(0, 2, *sink);
  TEST_ASSERT_EQUAL(1, sink->log.size());
  TEST_ASSERT_EQUAL(2, sink->log[0].len);
  TEST_ASSERT_EQUAL_HEX8(0xC5, sink->log[0].b[0]);
  TEST_ASSERT_EQUAL(10, sink->log[0].b[1]);
  p->tracks[2].program = kNoProgram;
  seq->sendProgram(0, 2, *sink);
  seq->sendProgram(0, kTracks, *sink);
  TEST_ASSERT_EQUAL(1, sink->log.size());
}

static void tieOnFirstStep() {
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][0].fx[0] = {Fx::TIE, 0};
}

void test_release_ties_after_clear() {
  tieOnFirstStep();
  seq->start(0, *sink);
  run(0, 50000);
  p->patterns[0].clear();
  sink->now = 50000;
  seq->releaseTies(50000, *sink);
  run(50000, 3000000);
  auto off60 = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(1, off60.size());
  TEST_ASSERT_EQUAL(50000, off60[0]);
}

void test_release_ties_keeps_min_gate() {
  tieOnFirstStep();
  seq->start(0, *sink);
  run(0, 0);
  p->patterns[0].clear();
  sink->now = 200;
  seq->releaseTies(200, *sink);
  run(200, 3000000);
  auto off60 = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(1, off60.size());
  TEST_ASSERT_EQUAL(kMinGateUs, off60[0]);
}

void test_release_ties_survives_rewind() {
  tieOnFirstStep();
  seq->start(0, *sink);
  run(0, 50000);
  p->patterns[0].clear();
  sink->now = 50000;
  seq->releaseTies(50000, *sink);
  seq->setBpm(140);  // rewinds the steps scheduled ahead
  run(50000, 3000000);
  auto off60 = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(1, off60.size());
  TEST_ASSERT_EQUAL(50000, off60[0]);
}

void test_deferred_tie_release_survives_rewind() {
  tieOnFirstStep();
  seq->start(0, *sink);
  run(0, 0);
  p->patterns[0].clear();
  sink->now = 200;
  seq->releaseTies(200, *sink);  // NoteOff waits for kMinGateUs
  seq->setBpm(140);
  run(200, 3000000);
  auto off60 = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(1, off60.size());
  TEST_ASSERT_EQUAL(kMinGateUs, off60[0]);
}

// Every NoteOn in the log is followed by a NoteOff for the same channel and note.
static void assertNotStuck() {
  bool on[16][128] = {};
  for (const auto& r : sink->log) {
    const uint8_t k = r.b[0] & 0xF0, ch = r.b[0] & 0x0F;
    if (k == 0x90) on[ch][r.b[1]] = true;
    else if (k == 0x80) on[ch][r.b[1]] = false;
  }
  for (int ch = 0; ch < 16; ++ch)
    for (int n = 0; n < 128; ++n)
      if (on[ch][n]) {
        char msg[32];
        snprintf(msg, sizeof(msg), "stuck ch %d note %d", ch + 1, n);
        TEST_FAIL_MESSAGE(msg);
      }
}

// Tie heard on step 0, its NoteOff held by unheard step 1; the tied pitch never plays again.
void test_release_ties_keeps_unheard_tie_release() {
  tieOnFirstStep();
  p->patterns[0].steps[0][1].note = 62;
  seq->start(0, *sink);
  run(0, 50000);
  p->patterns[0].steps[0][0] = Step();
  sink->now = 50000;
  seq->releaseTies(50000, *sink);
  seq->setBpm(140);
  run(50000, 3000000);
  assertNotStuck();
}

// A tie that starts in the lookahead is rescheduled and keeps its full length.
void test_release_ties_keeps_upcoming_tie_length() {
  p->patterns[0].steps[0][1].note = 60;
  p->patterns[0].steps[0][1].fx[0] = {Fx::TIE, 0};
  p->patterns[0].steps[0][3].note = 62;
  seq->start(0, *sink);
  run(0, 50000);
  sink->now = 50000;
  seq->releaseTies(50000, *sink);
  run(50000, 400000);
  auto on60 = sink->times(0x90, 60);
  auto off60 = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(1, on60.size());
  TEST_ASSERT_EQUAL(125000, on60[0]);
  TEST_ASSERT_EQUAL(1, off60.size());
  TEST_ASSERT_EQUAL(375000 + kTieOverlapUs, off60[0]);
}

static void randomStep(Rng& r, Step& st) {
  st = Step();
  if (r.below(10) < 5) st.note = static_cast<uint8_t>(60 + r.below(3));
  else if (r.below(10) == 0) st.note = kNoteOff;
  for (auto& f : st.fx) {
    switch (r.below(8)) {
      case 0:
      case 1: f = {Fx::TIE, 0}; break;
      case 2: f = {Fx::RAT, static_cast<uint8_t>(2 + r.below(3))}; break;
      case 3: f = {Fx::NDG, static_cast<uint8_t>(static_cast<int8_t>(static_cast<int>(r.below(101)) - 50))}; break;
      default: break;
    }
  }
}

// Random patterns with TIE / RAT / NDG / swing, random edits, releaseTies and tempo
// changes; then every step becomes OFF and no note may be left sounding.
static void fuzzReleaseTies(uint32_t seed) {
  delete sink;
  delete seq;
  delete p;
  p = midiProject();
  seq = new Sequencer(*p);
  seq->seed(seed);
  sink = new FakeSink();
  Rng r(seed * 2654435761u + 1);
  Pattern& pat = p->patterns[0];
  pat.length = static_cast<uint8_t>(4 + r.below(13));
  pat.swing = static_cast<uint8_t>(50 + r.below(26));
  pat.res = r.below(2) ? Resolution::Sixteenth : Resolution::ThirtySecond;
  for (int t = 0; t < 3; ++t)
    for (int i = 0; i < pat.length; ++i) randomStep(r, pat.steps[t][i]);

  seq->start(0, *sink);
  uint64_t t = 0;
  for (int k = 0; k < 40; ++k) {
    const uint64_t next = t + 1 + r.below(150000);
    run(t, next);
    t = next;
    sink->now = t;
    switch (r.below(4)) {
      case 0:
        randomStep(r, pat.steps[r.below(3)][r.below(pat.length)]);
        seq->releaseTies(t, *sink);
        break;
      case 1: seq->releaseTies(t, *sink); break;
      case 2: seq->setBpm(static_cast<uint16_t>(60 + r.below(200))); break;
      default: break;
    }
  }
  for (int tr = 0; tr < kTracks; ++tr)
    for (int i = 0; i < kMaxSteps; ++i) {
      pat.steps[tr][i] = Step();
      pat.steps[tr][i].note = kNoteOff;
    }
  run(t, t + 10000000);
  assertNotStuck();
}

void test_release_ties_fuzz() {
  for (uint32_t seed = 1; seed <= 60; ++seed) fuzzReleaseTies(seed);
}

// ---- song mode ----

// Patterns 0..4: 4 steps each, step 0 plays 60 + index.
static void songSetup(std::initializer_list<uint8_t> chain) {
  for (int i = 0; i < 5; ++i) {
    p->patterns[i].length = 4;
    p->patterns[i].steps[0][0].note = static_cast<uint8_t>(60 + i);
  }
  p->songMode = true;
  p->chainLen = 0;
  for (uint8_t c : chain) p->chain[p->chainLen++] = c;
}

// Step-0 notes (60..64) in the order they went out.
static std::vector<int> heardPatterns() {
  std::vector<int> out;
  for (const auto& r : sink->log)
    if (r.b[0] == 0x90 && r.b[1] >= 60 && r.b[1] < 65) out.push_back(r.b[1] - 60);
  return out;
}

void test_song_plays_chain_and_loops() {
  songSetup({2, 1, 3});
  seq->start(0, *sink);
  TEST_ASSERT_EQUAL(2, seq->pattern());
  TEST_ASSERT_EQUAL(0, seq->heardSongPos());
  run(0, 1100000);
  TEST_ASSERT_EQUAL(2, seq->heardSongPos());
  TEST_ASSERT_EQUAL(3, seq->heardPattern());
  run(1100000, 2600000);
  const std::vector<int> want = {2, 1, 3, 2, 1, 3};
  TEST_ASSERT_TRUE(heardPatterns() == want);
  TEST_ASSERT_EQUAL(1500000, sink->times(0x90, 62)[1]);
  TEST_ASSERT_EQUAL(2500000, sink->times(0x90, 63)[1]);
  TEST_ASSERT_EQUAL(-1, seq->queued());
}

void test_song_repeated_entry_keeps_pattern() {
  songSetup({1, 1, 2});
  seq->start(0, *sink);
  run(0, 2100000);
  const std::vector<int> want = {1, 1, 2, 1, 1};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_ignores_queue() {
  songSetup({1});
  seq->queuePattern(3);  // stopped: ignored too
  seq->start(0, *sink);
  seq->queuePattern(2);
  TEST_ASSERT_EQUAL(-1, seq->queued());
  TEST_ASSERT_EQUAL(-1, seq->pendingPattern());
  run(0, 1100000);
  const std::vector<int> want = {1, 1, 1};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_tempo_change_after_boundary_decided() {
  songSetup({1, 2});
  seq->start(0, *sink);
  run(0, 480000);  // step 3 heard, step 0 of the next entry scheduled
  seq->setBpm(240);
  run(480000, 1100000);  // 2 at 500000, 1 at 750000, 2 at 1000000
  const std::vector<int> want = {1, 2, 1, 2};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_tempo_change_before_boundary_heard() {
  songSetup({1, 2});
  seq->start(0, *sink);
  run(0, 300000);  // step 3 (375000) scheduled, the boundary decided but not heard
  seq->setBpm(60);
  TEST_ASSERT_EQUAL(0, seq->heardSongPos());
  run(300000, 2500000);  // 2 at 687500, 1 at 1687500
  const std::vector<int> want = {1, 2, 1};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_fuzz_tempo_pause_keeps_order() {
  for (uint32_t seed = 1; seed <= 30; ++seed) {
    tearDown();
    setUp();
    songSetup({1, 2, 3, 2});
    Rng r(seed * 2654435761u + 7);
    seq->start(0, *sink);
    uint64_t t = 0;
    for (int k = 0; k < 60; ++k) {
      const uint64_t next = t + 1 + r.below(200000);
      run(t, next);
      t = next;
      sink->now = t;
      switch (r.below(5)) {
        case 0:
        case 1: seq->setBpm(static_cast<uint16_t>(60 + r.below(240))); break;
        case 2:
          seq->pause(t, *sink);
          t += r.below(100000);
          sink->now = t;
          seq->resume(t, *sink);
          break;
        default: break;
      }
      TEST_ASSERT_TRUE(seq->heardSongPos() >= 0 && seq->heardSongPos() < 4);
      TEST_ASSERT_EQUAL(p->chain[seq->heardSongPos()], seq->heardPattern());
    }
    const std::vector<int> got = heardPatterns();
    TEST_ASSERT_TRUE(got.size() > 8);
    const int cyc[4] = {1, 2, 3, 2};
    for (size_t i = 0; i < got.size(); ++i) TEST_ASSERT_EQUAL(cyc[i % 4], got[i]);
  }
}

void test_song_pause_resume_continues_entry() {
  songSetup({1, 2});
  seq->start(0, *sink);
  run(0, 700000);  // in entry 2 (pattern 2), step 1 heard
  sink->now = 700000;
  seq->pause(700000, *sink);
  TEST_ASSERT_EQUAL(1, seq->heardSongPos());
  TEST_ASSERT_EQUAL(2, seq->heardPattern());
  sink->now = 2000000;
  seq->resume(2000000, *sink);
  run(2000000, 2300000);
  const std::vector<int> want = {1, 2, 1};
  TEST_ASSERT_TRUE(heardPatterns() == want);
  TEST_ASSERT_EQUAL(2250000, sink->times(0x90, 61)[1]);
  TEST_ASSERT_EQUAL(0, seq->heardSongPos());
}

void test_song_stop_then_start_from_first_entry() {
  songSetup({1, 2});
  seq->start(0, *sink);
  run(0, 700000);
  seq->stop(700000, *sink);
  TEST_ASSERT_EQUAL(0, seq->heardSongPos());  // stopped: shows what start() plays
  TEST_ASSERT_EQUAL(1, seq->heardPattern());
  TEST_ASSERT_EQUAL(1, seq->pattern());
  sink->log.clear();
  seq->start(1000000, *sink);
  run(1000000, 1000000);
  TEST_ASSERT_EQUAL(1000000, sink->times(0x90, 61)[0]);
  TEST_ASSERT_EQUAL(0, seq->heardSongPos());
}

void test_song_chain_shrink_while_playing() {
  songSetup({1, 2, 3});
  seq->start(0, *sink);
  run(0, 1100000);  // entry 3 heard
  p->chainLen = 2;
  run(1100000, 2100000);
  const std::vector<int> want = {1, 2, 3, 1, 2};
  TEST_ASSERT_TRUE(heardPatterns() == want);
  TEST_ASSERT_EQUAL(1, seq->heardSongPos());
}

void test_song_empty_chain_keeps_pattern() {
  songSetup({1, 2, 3});
  seq->start(0, *sink);
  run(0, 600000);  // entry 2 heard
  p->chainLen = 0;
  run(600000, 1600000);
  const std::vector<int> want = {1, 2, 2, 2};
  TEST_ASSERT_TRUE(heardPatterns() == want);
  TEST_ASSERT_EQUAL(-1, seq->heardSongPos());
  seq->queuePattern(4);  // no song without a chain: queue works
  TEST_ASSERT_EQUAL(4, seq->queued());
}

void test_song_empty_chain_at_start_plays_current() {
  songSetup({});
  seq->selectPattern(3);
  seq->start(0, *sink);
  TEST_ASSERT_EQUAL(-1, seq->heardSongPos());
  run(0, 600000);
  const std::vector<int> want = {3, 3};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_chain_entries_clamped() {
  songSetup({99});
  p->chainLen = 200;
  for (int i = 1; i < kChainMax; ++i) p->chain[i] = 1;
  seq->start(0, *sink);
  TEST_ASSERT_EQUAL(kPatterns - 1, seq->pattern());
  run(0, 2100000);  // pattern 15 keeps 16 steps
  TEST_ASSERT_EQUAL(1, seq->heardSongPos());
  TEST_ASSERT_EQUAL(1, seq->heardPattern());
}

void test_song_enable_while_playing_starts_chain_at_boundary() {
  songSetup({2, 3});
  p->songMode = false;
  seq->start(0, *sink);
  seq->queuePattern(1);
  run(0, 100000);
  p->songMode = true;  // the song wins over the queued pattern
  TEST_ASSERT_EQUAL(-1, seq->heardSongPos());
  run(100000, 1100000);
  const std::vector<int> want = {0, 2, 3};
  TEST_ASSERT_TRUE(heardPatterns() == want);
  TEST_ASSERT_EQUAL(1, seq->heardSongPos());
}

void test_song_disable_while_playing_keeps_pattern() {
  songSetup({2, 3});
  seq->start(0, *sink);
  run(0, 600000);  // entry 2 (pattern 3) heard
  p->songMode = false;
  TEST_ASSERT_EQUAL(1, seq->heardSongPos());  // until the boundary
  run(600000, 1600000);
  const std::vector<int> want = {2, 3, 3, 3};
  TEST_ASSERT_TRUE(heardPatterns() == want);
  TEST_ASSERT_EQUAL(-1, seq->heardSongPos());
}

void test_song_tie_released_on_change_held_on_repeat() {
  songSetup({1, 1, 2});
  p->patterns[1].steps[1][3].note = 70;
  p->patterns[1].steps[1][3].fx[0] = {Fx::TIE, 0};
  seq->start(0, *sink);
  run(0, 1100000);
  auto off = sink->times(0x81, 70);  // track 2 is on channel 2
  TEST_ASSERT_EQUAL(1, off.size());
  TEST_ASSERT_EQUAL(1000000, off[0]);  // held across 1 -> 1, released at 1 -> 2
}

void test_song_select_then_chain_goes_on() {
  songSetup({1, 2, 3});
  seq->start(0, *sink);
  run(0, 200000);  // steps up to the 1 -> 2 advance are scheduled
  seq->selectPattern(4);  // from the next step: replaces entry 2's pattern
  run(200000, 600000);
  TEST_ASSERT_EQUAL(4, seq->heardPattern());
  TEST_ASSERT_EQUAL(1, seq->heardSongPos());
  run(600000, 1100000);
  const std::vector<int> want = {1, 4, 3};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_entries_with_own_length_and_resolution() {
  songSetup({1, 2});
  p->patterns[2].length = 3;
  p->patterns[2].res = Resolution::Eighth;
  seq->start(0, *sink);
  run(0, 1800000);
  const std::vector<int> want = {1, 2, 1, 2};
  TEST_ASSERT_TRUE(heardPatterns() == want);
  TEST_ASSERT_EQUAL(500000, sink->times(0x90, 62)[0]);
  TEST_ASSERT_EQUAL(1250000, sink->times(0x90, 61)[1]);
  TEST_ASSERT_EQUAL(1750000, sink->times(0x90, 62)[1]);
}

void test_song_release_ties_across_boundary() {
  songSetup({1, 2});
  seq->start(0, *sink);
  run(0, 300000);  // boundary decided, not heard
  sink->now = 300000;
  seq->releaseTies(300000, *sink);
  run(300000, 400000);  // decided again and heard
  sink->now = 400000;
  seq->releaseTies(400000, *sink);
  run(400000, 1100000);
  const std::vector<int> want = {1, 2, 1};
  TEST_ASSERT_TRUE(heardPatterns() == want);
  TEST_ASSERT_EQUAL(500000, sink->times(0x90, 62)[0]);
}

void test_song_stall_skips_across_boundaries() {
  songSetup({1, 2, 3});
  seq->start(0, *sink);
  run(0, 300000);
  run(1300000, 1300000);  // stalled: the rest of 1, all of 2 and most of 3 are skipped
  TEST_ASSERT_EQUAL(2, seq->heardSongPos());
  TEST_ASSERT_EQUAL(3, seq->heardPattern());
  run(1300000, 1600000);
  TEST_ASSERT_EQUAL(0, seq->heardSongPos());
  TEST_ASSERT_EQUAL(1500000, sink->times(0x90, 61).back());
}

// Edits the chain the way the UI does: the write, then chainEdited() at the same time.
static void chainInsert(uint64_t t, int at, uint8_t pat) {
  memmove(p->chain + at + 1, p->chain + at, p->chainLen - at);
  p->chain[at] = pat;
  ++p->chainLen;
  sink->now = t;
  seq->chainEdited(t, *sink, at, ChainOp::Insert);
}
static void chainDelete(uint64_t t, int at) {
  memmove(p->chain + at, p->chain + at + 1, p->chainLen - at - 1);
  --p->chainLen;
  sink->now = t;
  seq->chainEdited(t, *sink, at, ChainOp::Delete);
}

void test_song_insert_before_playing_keeps_entry() {
  songSetup({1, 2, 3});
  seq->start(0, *sink);
  run(0, 600000);  // entry 2 heard
  chainInsert(600000, 0, 4);
  TEST_ASSERT_EQUAL(2, seq->heardSongPos());
  run(600000, 2100000);
  const std::vector<int> want = {1, 2, 3, 4, 1};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_insert_after_boundary_decided() {
  songSetup({1, 2});
  seq->start(0, *sink);
  run(0, 300000);  // 1 -> 2 decided, not heard
  chainInsert(300000, 1, 4);
  run(300000, 1100000);
  const std::vector<int> want = {1, 4, 2};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_delete_before_playing_keeps_entry() {
  songSetup({1, 2, 3});
  seq->start(0, *sink);
  run(0, 600000);
  chainDelete(600000, 0);
  TEST_ASSERT_EQUAL(0, seq->heardSongPos());
  run(600000, 1600000);
  const std::vector<int> want = {1, 2, 3, 2};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_delete_playing_entry_goes_on_with_next() {
  songSetup({1, 2, 3});
  seq->start(0, *sink);
  run(0, 600000);
  chainDelete(600000, 1);
  run(600000, 1600000);
  const std::vector<int> want = {1, 2, 3, 1};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_delete_first_playing_entry() {
  songSetup({1, 2, 3});
  seq->start(0, *sink);
  run(0, 100000);
  chainDelete(100000, 0);
  run(100000, 1100000);
  const std::vector<int> want = {1, 2, 3};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_edit_next_entry_in_lookahead() {
  songSetup({1, 2});
  seq->start(0, *sink);
  run(0, 300000);  // 1 -> 2 decided, not heard
  p->chain[1] = 3;
  seq->chainEdited(300000, *sink, 1, ChainOp::Edit);
  run(300000, 600000);
  const std::vector<int> want = {1, 3};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_enable_in_lookahead() {
  songSetup({2});
  p->songMode = false;
  seq->start(0, *sink);
  run(0, 300000);  // 0 -> 0 decided, not heard
  p->songMode = true;
  seq->chainEdited(300000, *sink, 0, ChainOp::Edit);
  run(300000, 600000);
  const std::vector<int> want = {0, 2};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_edit_while_stopped_shows_first_entry() {
  songSetup({2, 3});
  p->songMode = false;
  seq->chainEdited(0, *sink, 0, ChainOp::Edit);
  TEST_ASSERT_EQUAL(-1, seq->heardSongPos());
  TEST_ASSERT_EQUAL(0, seq->heardPattern());
  p->songMode = true;
  seq->chainEdited(0, *sink, 0, ChainOp::Edit);
  TEST_ASSERT_EQUAL(0, seq->heardSongPos());
  TEST_ASSERT_EQUAL(2, seq->heardPattern());
}


// --- INT tracks (internal synth) ---

void test_midi_track_does_not_reach_synth() {
  p->patterns[0].steps[0][0].note = 60;
  seq->start(0, *sink);
  run(0, 200000);
  TEST_ASSERT_EQUAL(1, sink->times(0x90, 60).size());
  TEST_ASSERT_EQUAL(0, sink->synKind(0x90).size());
  TEST_ASSERT_EQUAL(0, sink->synKind(0x80).size());
}

void test_int_track_notes_go_to_synth_only() {
  p->tracks[2].out = TrackOut::Int;
  p->patterns[0].steps[2][0].note = 60;
  seq->start(0, *sink);
  run(0, 200000);
  TEST_ASSERT_EQUAL(0, sink->sendNotes());
  TEST_ASSERT_TRUE(sink->times(0xF8).size() > 0);  // clock as before
  auto on = sink->synKind(0x90);
  auto off = sink->synKind(0x80);
  TEST_ASSERT_EQUAL(1, on.size());
  TEST_ASSERT_EQUAL(1, off.size());
  TEST_ASSERT_EQUAL(2, on[0].track);
  TEST_ASSERT_EQUAL(60, on[0].b[1]);
  TEST_ASSERT_EQUAL(0, on[0].t);
  TEST_ASSERT_EQUAL(2, off[0].track);
  TEST_ASSERT_EQUAL(62500, off[0].t);
}

// The synth gets the scheduled time, not the (later) time the engine got around to send it.
void test_int_track_events_carry_scheduled_time() {
  p->tracks[2].out = TrackOut::Int;
  p->patterns[0].steps[2][0].note = 60;
  p->patterns[0].steps[2][1].note = 62;
  seq->start(0, *sink);
  uint64_t t = 0;
  while (t <= 300000) {  // every wakeup 300 us late
    sink->now = t;
    const uint64_t next = seq->process(t, *sink);
    if (next == kNever) break;
    t = (next > t ? next : t + 1) + 300;
  }
  auto on = sink->synKind(0x90);
  TEST_ASSERT_EQUAL(2, on.size());
  for (const auto& r : on) TEST_ASSERT_TRUE(r.t <= r.at);
  TEST_ASSERT_EQUAL_UINT64(300, on[1].at - on[1].t);  // step 1: sent late, stamped on time
  const uint64_t stepUs = 60000000ull / p->bpm / 4;
  TEST_ASSERT_UINT64_WITHIN(1, stepUs, on[1].t - on[0].t);
}

void test_int_and_midi_same_channel_and_note_both_sound() {
  p->tracks[1].out = TrackOut::Int;
  p->tracks[1].channel = 0;  // same channel as track 0
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[1][0].note = 60;
  seq->start(0, *sink);
  run(0, 200000);
  TEST_ASSERT_EQUAL(1, sink->times(0x90, 60).size());
  TEST_ASSERT_EQUAL(1, sink->times(0x80, 60).size());  // no retrigger NoteOff
  TEST_ASSERT_EQUAL(1, sink->synKind(0x90).size());
  TEST_ASSERT_EQUAL(1, sink->synKind(0x80).size());
}

void test_int_track_muted_sends_nothing_to_synth() {
  p->tracks[3].out = TrackOut::Int;
  p->tracks[3].mute = true;
  p->patterns[0].steps[3][0].note = 60;
  seq->start(0, *sink);
  run(0, 200000);
  TEST_ASSERT_EQUAL(0, sink->synKind(0x90).size());
  TEST_ASSERT_EQUAL_HEX8(0, seq->takeActivity());
}

void test_int_track_marks_activity() {
  p->tracks[4].out = TrackOut::Int;
  p->patterns[0].steps[4][0].note = 60;
  seq->start(0, *sink);
  run(0, 1000);
  TEST_ASSERT_EQUAL_HEX8(1 << 4, seq->takeActivity());
}

void test_int_track_stop_sends_off_and_all_off() {
  p->tracks[2].out = TrackOut::Int;
  p->patterns[0].steps[2][0].note = 60;
  p->patterns[0].steps[2][0].fx[0] = {Fx::GAT, 200};
  seq->start(0, *sink);
  run(0, 50000);
  sink->now = 50000;
  sink->syn.clear();
  seq->stop(50000, *sink);
  auto off = sink->synKind(0x80);
  TEST_ASSERT_EQUAL(1, off.size());
  TEST_ASSERT_EQUAL(2, off[0].track);
  TEST_ASSERT_EQUAL(60, off[0].b[1]);
  auto all = sink->synKind(0xFF);
  TEST_ASSERT_EQUAL(1, all.size());
  TEST_ASSERT_EQUAL(2, all[0].track);
  TEST_ASSERT_EQUAL(0, sink->sendNotes());
}

void test_int_track_pgm_goes_to_synth() {
  p->tracks[2].out = TrackOut::Int;
  p->patterns[0].steps[2][0].fx[0] = {Fx::PGM, 5};
  p->patterns[0].steps[0][0].fx[0] = {Fx::PGM, 7};
  seq->start(0, *sink);
  run(0, 1000);
  auto pg = sink->synKind(0xC0);
  TEST_ASSERT_EQUAL(1, pg.size());
  TEST_ASSERT_EQUAL(2, pg[0].track);
  TEST_ASSERT_EQUAL(5, pg[0].b[1]);
  auto mid = sink->times(0xC0);
  TEST_ASSERT_EQUAL(1, mid.size());  // track 0 on channel 1 as before
}

void test_send_program_int_track_uses_instrument() {
  p->tracks[2].out = TrackOut::Int;
  p->tracks[2].instr = 9;
  seq->sendProgram(0, 2, *sink);
  TEST_ASSERT_EQUAL(0, sink->log.size());
  auto pg = sink->synKind(0xC0);
  TEST_ASSERT_EQUAL(1, pg.size());
  TEST_ASSERT_EQUAL(9, pg[0].b[1]);
}

void test_start_resets_int_tracks() {
  p->tracks[1].out = TrackOut::Int;
  p->tracks[6].out = TrackOut::Int;
  seq->start(0, *sink);
  auto fe = sink->synKind(0xFE);
  TEST_ASSERT_EQUAL(2, fe.size());
  TEST_ASSERT_EQUAL(1, fe[0].track);
  TEST_ASSERT_EQUAL(6, fe[1].track);
}

void test_track_out_change_releases_held_notes() {
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][0].fx[0] = {Fx::GAT, 200};
  seq->start(0, *sink);
  run(0, 50000);
  p->tracks[0].out = TrackOut::Int;
  sink->now = 50000;
  seq->trackOutChanged(50000, 0, *sink);
  TEST_ASSERT_EQUAL(1, sink->times(0x80, 60).size());  // MIDI note released at once
  run(50000, 400000);
  TEST_ASSERT_EQUAL(1, sink->times(0x80, 60).size());  // and only once
}

void test_int_track_synth_fx_before_note() {
  p->tracks[2].out = TrackOut::Int;
  Step& st = p->patterns[0].steps[2][0];
  st.note = 60;
  st.fx[0] = {Fx::ARP, 0x47};
  seq->start(0, *sink);
  run(0, 1000);
  int stepAt = -1, arpAt = -1, onAt = -1;
  for (size_t i = 0; i < sink->syn.size(); ++i) {
    const SynRec& r = sink->syn[i];
    if (r.b[0] == 0xF5 && r.b[1] == 0xF0) stepAt = static_cast<int>(i);
    if (r.b[0] == 0xF5 && r.b[1] == static_cast<uint8_t>(Fx::ARP)) arpAt = static_cast<int>(i);
    if ((r.b[0] & 0xF0) == 0x90) onAt = static_cast<int>(i);
  }
  TEST_ASSERT_TRUE(stepAt >= 0 && arpAt > stepAt && onAt > arpAt);
  const SynRec& sr = sink->syn[stepAt];
  TEST_ASSERT_EQUAL(2, sr.track);
  TEST_ASSERT_EQUAL(3, sr.len);
  TEST_ASSERT_EQUAL_HEX8(0x80 | 24, sr.b[2]);  // new step with a note, 24 ticks
  TEST_ASSERT_EQUAL_HEX8(0x47, sink->syn[arpAt].b[2]);
  TEST_ASSERT_EQUAL(0, sink->syn[arpAt].t);
}

void test_midi_track_synth_fx_go_nowhere() {
  Step& st = p->patterns[0].steps[0][0];
  st.note = 60;
  st.fx[0] = {Fx::VIB, 0x44};
  p->patterns[0].steps[0][1].fx[0] = {Fx::CUT, 2};  // fx-only step
  seq->start(0, *sink);
  run(0, 300000);
  TEST_ASSERT_EQUAL(0, sink->syn.size());
  for (const auto& r : sink->log) TEST_ASSERT_NOT_EQUAL(0xF5, r.b[0]);
  TEST_ASSERT_EQUAL(1, sink->times(0x90, 60).size());
}

void test_int_track_step_markers() {
  p->tracks[1].out = TrackOut::Int;
  p->patterns[0].res = Resolution::Eighth;
  p->patterns[0].steps[1][0].note = 60;                // plain note: marker with the note flag
  p->patterns[0].steps[1][1].fx[0] = {Fx::VSL, 0xF0};  // fx-only step: marker without it
  p->patterns[0].steps[1][2].fx[0] = {Fx::CCA, 10};    // no synth fx, no note: nothing
  seq->start(0, *sink);
  run(0, 3 * 250000 - 1);
  auto f5 = sink->synKind(0xF5);
  TEST_ASSERT_EQUAL(3, f5.size());
  TEST_ASSERT_EQUAL_HEX8(0xF0, f5[0].b[1]);
  TEST_ASSERT_EQUAL_HEX8(0x80 | 48, f5[0].b[2]);
  TEST_ASSERT_EQUAL(0, f5[0].t);
  TEST_ASSERT_EQUAL_HEX8(0xF0, f5[1].b[1]);
  TEST_ASSERT_EQUAL_HEX8(48, f5[1].b[2]);
  TEST_ASSERT_EQUAL(250000, f5[1].t);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(Fx::VSL), f5[2].b[1]);
  TEST_ASSERT_EQUAL_HEX8(0xF0, f5[2].b[2]);
  TEST_ASSERT_EQUAL(0, sink->synKind(0xB0).size());
}

// --- OFF (note column and FX) ---

void test_int_off_note_releases_track() {
  p->tracks[2].out = TrackOut::Int;
  p->patterns[0].steps[2][0].note = 60;
  p->patterns[0].steps[2][2].note = kNoteOff;
  seq->start(0, *sink);
  run(0, 3 * 125000 - 1);
  auto rel = sink->synKind(0xFF);
  TEST_ASSERT_EQUAL(1, rel.size());
  TEST_ASSERT_EQUAL(2, rel[0].track);
  TEST_ASSERT_EQUAL(1, rel[0].len);
  TEST_ASSERT_EQUAL(250000, rel[0].t);
}

void test_midi_off_never_sends_reset() {
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][1].note = kNoteOff;
  p->patterns[0].steps[0][2].fx[0] = {Fx::OFF, 0};
  seq->start(0, *sink);
  run(0, 3 * 125000 - 1);
  for (const auto& r : sink->log) TEST_ASSERT_NOT_EQUAL(0xFF, r.b[0]);
  TEST_ASSERT_EQUAL(0, sink->syn.size());
}

void test_int_fx_off_releases_track_after_ticks() {
  p->tracks[1].out = TrackOut::Int;
  p->patterns[0].steps[1][0].note = 60;
  p->patterns[0].steps[1][1].fx[4] = {Fx::OFF, 12};  // empty step, half a step in
  seq->start(0, *sink);
  run(0, 2 * 125000 - 1);
  auto rel = sink->synKind(0xFF);
  TEST_ASSERT_EQUAL(1, rel.size());
  TEST_ASSERT_EQUAL(1, rel[0].track);
  TEST_ASSERT_EQUAL(125000 + 62500, rel[0].t);
}

void test_int_fx_off_on_note_step() {
  p->tracks[1].out = TrackOut::Int;
  Step& st = p->patterns[0].steps[1][0];
  st.note = 60;
  st.fx[0] = {Fx::GAT, 200};
  st.fx[1] = {Fx::OFF, 6};
  seq->start(0, *sink);
  run(0, 125000 - 1);
  auto off = sink->synKind(0x80);
  TEST_ASSERT_EQUAL(1, off.size());
  TEST_ASSERT_EQUAL(31250, off[0].t);
  auto rel = sink->synKind(0xFF);
  TEST_ASSERT_EQUAL(1, rel.size());
  TEST_ASSERT_EQUAL(31250, rel[0].t);
}

void test_fx_off_releases_tie() {
  Step& a = p->patterns[0].steps[0][0];
  a.note = 60;
  a.fx[0] = {Fx::TIE, 0};
  p->patterns[0].steps[0][2].fx[0] = {Fx::OFF, 12};  // MIDI track: the tie ends there
  seq->start(0, *sink);
  run(0, 4 * 125000 - 1);
  auto offs = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(1, offs.size());
  TEST_ASSERT_EQUAL(250000 + 62500, offs[0]);
}

// ---- Song: chain transpose / repeat / scene, polymeter ----

void test_chain_transpose_melodic_not_drum() {
  p->songMode = true;
  p->chainLen = 1;
  p->chain[0] = 0;
  p->chainTr[0] = 5;
  p->patterns[0].length = 4;
  p->patterns[0].steps[0][0].note = 60;
  instrSetType(p->instruments[1], InstrType::Kit);  // track 1: a drum track (lanes 60..67)
  p->tracks[1].instr = 1;
  p->patterns[0].steps[1][0].note = 100;  // step velocity
  p->patterns[0].steps[1][0].vel = 1;     // lane 1: note 60
  seq->start(0, *sink);
  run(0, 10000);
  TEST_ASSERT_EQUAL(1, sink->times(0x90, 65).size());
  TEST_ASSERT_EQUAL(0, sink->times(0x90, 60).size());
  TEST_ASSERT_EQUAL(1, sink->times(0x91, 60).size());  // the lane note is not transposed
}

void test_chain_transpose_clamps_and_skips_off() {
  p->songMode = true;
  p->chainLen = 1;
  p->chainTr[0] = 24;
  p->patterns[0].length = 4;
  p->patterns[0].steps[0][0].note = 120;
  p->patterns[0].steps[0][1].note = kNoteOff;
  seq->start(0, *sink);
  run(0, 300000);
  TEST_ASSERT_EQUAL(1, sink->times(0x90, 127).size());
  TEST_ASSERT_EQUAL(kNoteOff, p->patterns[0].steps[0][1].note);  // the pattern itself is untouched
  TEST_ASSERT_EQUAL(120, p->patterns[0].steps[0][0].note);
}

void test_chain_repeat_advances_after_n_passes() {
  p->songMode = true;
  p->chainLen = 2;
  p->chain[0] = 0;
  p->chainRep[0] = 3;
  p->chain[1] = 1;
  p->patterns[0].length = 4;
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[1].length = 4;
  p->patterns[1].steps[0][0].note = 62;
  seq->start(0, *sink);
  run(0, 1510000);  // 3 passes of P0, then P1 starts at 1 500 000
  TEST_ASSERT_EQUAL(3, sink->times(0x90, 60).size());
  auto on62 = sink->times(0x90, 62);
  TEST_ASSERT_EQUAL(1, on62.size());
  TEST_ASSERT_EQUAL(1500000, on62[0]);
}

// Raising the repeat count while the advance is planned but not heard re-decides it (rewind restores
// the pass count).
void test_chain_repeat_edit_before_advance_heard() {
  p->songMode = true;
  p->chainLen = 2;
  p->chain[0] = 0;
  p->chainRep[0] = 2;
  p->chain[1] = 1;
  p->patterns[0].length = 4;
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[1].length = 4;
  p->patterns[1].steps[0][0].note = 62;
  seq->start(0, *sink);
  run(0, 800000);  // pass 2 playing; its last step (875 000) and the advance already planned
  p->chainRep[0] = 3;
  sink->now = 800000;
  seq->chainEdited(800000, *sink, 0, ChainOp::Edit);
  run(800000, 1510000);
  TEST_ASSERT_EQUAL(3, sink->times(0x90, 60).size());
  auto on62 = sink->times(0x90, 62);
  TEST_ASSERT_EQUAL(1, on62.size());
  TEST_ASSERT_EQUAL(1500000, on62[0]);
}

void test_chain_scene_sets_mutes() {
  p->songMode = true;
  p->chainLen = 2;
  p->chain[0] = 0;
  p->chainScene[0] = 2;
  p->chain[1] = 0;
  p->chainScene[1] = 0;  // no scene: mutes stay
  p->scenes[1] = 1u << 3;  // scene 2 mutes track 4
  p->patterns[0].length = 4;
  p->patterns[0].steps[3][0].note = 60;
  p->patterns[0].steps[2][0].note = 60;
  seq->start(0, *sink);
  run(0, 1010000);  // passes at 0, 500 000, 1 000 000
  TEST_ASSERT_TRUE(p->tracks[3].mute);
  TEST_ASSERT_EQUAL(0, sink->times(0x93, 60).size());
  TEST_ASSERT_EQUAL(3, sink->times(0x92, 60).size());
}

void test_chain_empty_scene_ignored() {
  p->songMode = true;
  p->chainLen = 1;
  p->chainScene[0] = 1;  // scene 1 is empty
  p->tracks[2].mute = true;
  seq->start(0, *sink);
  run(0, 10000);
  TEST_ASSERT_TRUE(p->tracks[2].mute);
  TEST_ASSERT_FALSE(p->tracks[0].mute);
}

// Track 0 has length 3 inside a 16-step pattern: its step 0 plays at positions 0, 3, 6 ... 15.
void test_track_length_polymeter() {
  p->patterns[0].length = 16;
  p->patterns[0].trackLen[0] = 3;
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[1][15].note = 62;  // the pattern still has 16 steps
  seq->start(0, *sink);
  run(0, 2010000);  // one pass = 2 000 000 us
  auto on = sink->times(0x90, 60);
  TEST_ASSERT_EQUAL(7, on.size());  // 0, 3, 6, 9, 12, 15, then the next pass's 0
  TEST_ASSERT_EQUAL(375000, on[1]);
  TEST_ASSERT_EQUAL(2000000, on[6]);
  TEST_ASSERT_EQUAL(1, sink->times(0x91, 62).size());
  TEST_ASSERT_EQUAL(2, seq->loopCount() + 1);  // the pass counter follows the pattern length
}

// ---- Live: fill, perf, phase ----

void test_fill_gates_fil_steps() {
  p->patterns[0].length = 4;
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][0].fx[0] = {Fx::CND, kCndFill};
  p->patterns[0].steps[1][0].note = 62;
  p->patterns[0].steps[1][0].fx[0] = {Fx::CND, kCndNoFill};
  seq->start(0, *sink);
  run(0, 260000);
  TEST_ASSERT_EQUAL(0, sink->times(0x90, 60).size());
  TEST_ASSERT_EQUAL(1, sink->times(0x91, 62).size());
  seq->setFill(true);  // the step at 500 000 is planned already (lookahead): the one at 1 000 000 fills
  TEST_ASSERT_TRUE(seq->fill());
  run(260001, 1010000);
  TEST_ASSERT_EQUAL(1, sink->times(0x90, 60).size());
  TEST_ASSERT_EQUAL(1000000, sink->times(0x90, 60)[0]);
  TEST_ASSERT_EQUAL(2, sink->times(0x91, 62).size());
}

void test_perf_rat2_doubles_and_release_restores() {
  p->patterns[0].length = 4;
  p->patterns[0].steps[0][0].note = 60;
  seq->start(0, *sink);
  seq->perfOn(0, PerfFx::Rat2);
  run(0, 260000);
  TEST_ASSERT_EQUAL(2, sink->times(0x90, 60).size());
  seq->perfOff(0);
  run(260001, 1010000);  // 500 000 was planned with it (lookahead), 1 000 000 without
  TEST_ASSERT_EQUAL(5, sink->times(0x90, 60).size());
}

void test_perf_keeps_step_fx_and_replaces_same_cmd() {
  Step s;
  s.note = 60;
  s.fx[0] = {Fx::RAT, 3};
  for (int k = 1; k < kFxSlots; ++k) s.fx[k] = {Fx::PRB, 100};
  p->patterns[0].length = 4;
  p->patterns[0].steps[0][0] = s;
  seq->perfOn(0, PerfFx::Rat4);
  seq->start(0, *sink);
  run(0, 120000);
  TEST_ASSERT_EQUAL(4, sink->times(0x90, 60).size());  // RAT 4 took the RAT slot
  TEST_ASSERT_EQUAL(3, p->patterns[0].steps[0][0].fx[0].val);
}

void test_perf_mute_silences_track() {
  p->patterns[0].length = 4;
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[1][0].note = 62;
  seq->perfOn(0, PerfFx::Mute);
  seq->start(0, *sink);
  run(0, 260000);
  TEST_ASSERT_EQUAL(0, sink->times(0x90, 60).size());
  TEST_ASSERT_EQUAL(1, sink->times(0x91, 62).size());
}

void test_perf_synth_fx_skipped_on_midi_track() {
  p->patterns[0].length = 4;
  p->patterns[0].steps[0][0].note = 60;
  seq->perfOn(0, PerfFx::FltLow);
  seq->start(0, *sink);
  run(0, 10000);
  TEST_ASSERT_EQUAL(1, sink->times(0x90, 60).size());
  TEST_ASSERT_EQUAL(0, sink->synKind(0xF5).size());
}

void test_perf_flt_on_int_track() {
  p->tracks[0].out = TrackOut::Int;
  p->patterns[0].length = 4;
  p->patterns[0].steps[0][0].note = 60;
  seq->perfOn(0, PerfFx::FltLow);
  seq->start(0, *sink);
  run(0, 10000);
  bool flt = false;
  for (const SynRec& r : sink->synKind(0xF5)) flt |= r.b[1] == static_cast<uint8_t>(Fx::FLT) && r.b[2] == 30;
  TEST_ASSERT_TRUE(flt);
}

void test_stop_clears_perf() {
  p->patterns[0].length = 4;
  p->patterns[0].steps[0][0].note = 60;
  seq->perfOn(0, PerfFx::Mute);
  seq->start(0, *sink);
  run(0, 10000);
  seq->stop(10000, *sink);
  seq->start(20000, *sink);
  run(20000, 30000);
  TEST_ASSERT_EQUAL(1, sink->times(0x90, 60).size());
}

void test_phase256_within_step() {
  p->patterns[0].length = 4;
  seq->start(0, *sink);
  run(0, 1000);
  TEST_ASSERT_EQUAL(0, seq->phase256(0));
  TEST_ASSERT_EQUAL(128, seq->phase256(62500));  // half a 16th at 120 BPM
  TEST_ASSERT_EQUAL(255, seq->phase256(200000));
  run(1001, 130000);
  TEST_ASSERT_EQUAL(1, seq->playPos());
  TEST_ASSERT_EQUAL(32, seq->phase256(140625));
}

void test_track_mask_silences_others() {
  p->patterns[0].length = 4;
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[3][0].note = 64;
  seq->setTrackMask(1u << 3);
  seq->start(0, *sink);
  run(0, 10000);
  TEST_ASSERT_EQUAL(0, sink->times(0x90, 60).size());
  TEST_ASSERT_EQUAL(1, sink->times(0x93, 64).size());
}

// ---- ARS (step arp) ----

static std::vector<int> notesOn(uint8_t status) {
  std::vector<int> out;
  for (const auto& r : sink->log)
    if (r.b[0] == status) out.push_back(r.b[1]);
  return out;
}

void test_step_arp_plays_on_following_steps() {
  Step& s = p->patterns[0].steps[0][0];
  s.note = 60;
  s.fx[0] = {Fx::CHD, kChordTriad};
  s.fx[1] = {Fx::ARS, kArsDefault};
  p->patterns[0].steps[0][5].note = kNoteOff;
  seq->start(0, *sink);
  run(0, 125000 * 7);
  const std::vector<int> on = notesOn(0x90);
  const int want[] = {60, 64, 67, 60, 64};  // steps 0..4, OFF at 5
  TEST_ASSERT_EQUAL(5, on.size());
  for (int i = 0; i < 5; ++i) TEST_ASSERT_EQUAL(want[i], on[i]);
  TEST_ASSERT_EQUAL(125000, sink->times(0x90, 64)[0]);
  TEST_ASSERT_EQUAL(250000, sink->times(0x90, 67)[0]);
  // Every arp note gets its NoteOff before the next one.
  TEST_ASSERT_EQUAL(5, sink->times(0x80).size());
  TEST_ASSERT_TRUE(sink->times(0x80, 64)[0] < 250000);
}

void test_step_arp_every_two_steps_until_next_note() {
  Step& s = p->patterns[0].steps[0][0];
  s.note = 60;
  s.fx[0] = {Fx::ARS, 0x02};  // UP, every 2 steps: 60, 72
  p->patterns[0].steps[0][6].note = 50;
  seq->start(0, *sink);
  run(0, 125000 * 9);
  const std::vector<int> on = notesOn(0x90);
  const int want[] = {60, 72, 60, 50};  // steps 0, 2, 4, then the note at 6 ends it
  TEST_ASSERT_EQUAL(4, on.size());
  for (int i = 0; i < 4; ++i) TEST_ASSERT_EQUAL(want[i], on[i]);
  TEST_ASSERT_EQUAL(250000, sink->times(0x90, 72)[0]);
}

void test_step_arp_continues_over_loop_and_stops_on_pattern_change() {
  p->patterns[0].length = 4;
  p->patterns[1].length = 4;
  Step& s = p->patterns[0].steps[0][2];
  s.note = 60;
  s.fx[0] = {Fx::ARS, kArsDefault};
  seq->start(0, *sink);
  run(0, 125000 * 3 + 1000);  // steps 2, 3 (planned up to step 5)
  TEST_ASSERT_EQUAL(2, notesOn(0x90).size());
  seq->queuePattern(1);
  run(125000 * 3 + 1001, 125000 * 14);
  // Pass 2: steps 0, 1 go on with the arp, the note at 2 restarts it, 3; pattern 1 stops it.
  const std::vector<int> on = notesOn(0x90);
  const int want[] = {60, 72, 60, 72, 60, 72};
  TEST_ASSERT_EQUAL(6, on.size());
  for (int i = 0; i < 6; ++i) TEST_ASSERT_EQUAL(want[i], on[i]);
  TEST_ASSERT_EQUAL(1, seq->pattern());
}

void test_step_arp_updown_over_two_octaves() {
  Step& s = p->patterns[0].steps[0][0];
  s.note = 60;
  s.fx[0] = {Fx::ARP, 0x47};      // 60 64 67
  s.fx[1] = {Fx::ARS, 0x61};      // 2 octaves, UPDOWN, every step
  p->patterns[0].length = 64;
  p->patterns[0].steps[0][12].note = kNoteOff;
  seq->start(0, *sink);
  run(0, 125000 * 13);
  const std::vector<int> on = notesOn(0x90);
  const int want[] = {60, 64, 67, 72, 76, 79, 76, 72, 67, 64, 60, 64};
  TEST_ASSERT_EQUAL(12, on.size());
  for (int i = 0; i < 12; ++i) TEST_ASSERT_EQUAL(want[i], on[i]);
}

// A stall skips the ARS step: the arp still runs on the steps after it, in step with the grid.
void test_step_arp_survives_a_stall() {
  Step& s = p->patterns[0].steps[0][0];
  s.note = 60;
  s.fx[0] = {Fx::ARS, kArsDefault};  // 60, 72 every step
  seq->start(0, *sink);
  sink->now = 1100000;  // steps 0..7 missed
  run(1100000, 1400000);
  const auto on72 = sink->times(0x90, 72);
  TEST_ASSERT_EQUAL(2, on72.size());
  TEST_ASSERT_EQUAL(1125000, on72[0]);  // steps 9 and 11: the odd arp notes
  TEST_ASSERT_EQUAL(1375000, on72[1]);
}

void test_step_arp_on_int_track_uses_track_voices() {
  p->tracks[0].out = TrackOut::Int;
  Step& s = p->patterns[0].steps[0][0];
  s.note = 60;
  s.fx[0] = {Fx::ARS, kArsDefault};
  s.fx[1] = {Fx::ARP, 0x37};
  p->patterns[0].steps[0][2].fx[0] = {Fx::FLT, 20};  // locks the arp note of its step
  seq->start(0, *sink);
  run(0, 125000 * 3 + 1000);
  const auto on = sink->synKind(0x90);
  TEST_ASSERT_EQUAL(4, on.size());
  TEST_ASSERT_EQUAL(63, on[1].b[1]);
  TEST_ASSERT_EQUAL(67, on[2].b[1]);
  TEST_ASSERT_EQUAL(60, on[3].b[1]);
  for (const auto& r : sink->synKind(0xF5))
    TEST_ASSERT_TRUE(r.b[1] != static_cast<uint8_t>(Fx::ARP));  // the synth's ARP stays out
  bool noteStart = false;  // step 2 starts as a note step
  for (const auto& r : sink->synKind(0xF5))
    if (r.b[1] == kSynthStep && r.t == 250000) noteStart = (r.b[2] & 0x80) != 0;
  TEST_ASSERT_TRUE(noteStart);
}

// A perf Mute's release is planned with the next step; a rewind (tempo change) replans it.
void test_perf_mute_release_survives_rewind() {
  Step& s = p->patterns[0].steps[0][0];
  s.note = 60;
  s.fx[0] = {Fx::TIE, 0};
  seq->start(0, *sink);
  run(0, 200000);
  seq->perfOn(0, PerfFx::Mute);
  run(200001, 260000);  // the step at 250 ms is planned with its release
  p->bpm = 121;         // the next process rewinds and replans it
  run(260001, 700000);
  const auto off = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(1, off.size());
  TEST_ASSERT_TRUE(off[0] < 400000);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_start_sends_start_then_clock);
  RUN_TEST(test_clock_24ppqn_at_120);
  RUN_TEST(test_sixteenths_at_120);
  RUN_TEST(test_swing_delays_odd_steps);
  RUN_TEST(test_queued_pattern_switches_at_loop_end);
  RUN_TEST(test_pause_silences_and_stops_output);
  RUN_TEST(test_resume_sends_song_position_and_continue);
  RUN_TEST(test_retrigger_ignores_stale_note_off);
  RUN_TEST(test_muted_track_is_silent);
  RUN_TEST(test_drum_track_plays_lane_notes);
  RUN_TEST(test_activity_marks_sounding_tracks);
  RUN_TEST(test_track_15_plays);
  RUN_TEST(test_tie_holds_until_next_note_with_overlap);
  RUN_TEST(test_set_bpm_while_playing);
  RUN_TEST(test_stop_rewinds);
  RUN_TEST(test_resolution_change_mid_play_keeps_time);
  RUN_TEST(test_set_bpm_keeps_steps_on_clock_grid);
  RUN_TEST(test_direct_bpm_write_mid_play);
  RUN_TEST(test_tie_released_when_track_muted);
  RUN_TEST(test_tie_to_same_pitch_extends);
  RUN_TEST(test_tie_to_same_pitch_tied_again_keeps_holding);
  RUN_TEST(test_play_pos_with_swing);
  RUN_TEST(test_pause_after_queued_switch_in_lookahead);
  RUN_TEST(test_length_shrink_applies_queued_switch);
  RUN_TEST(test_negative_nudge_at_start_is_clamped);
  RUN_TEST(test_select_pattern_switches_from_next_step);
  RUN_TEST(test_set_bpm_faster_keeps_step_order);
  RUN_TEST(test_long_uptime_does_not_overflow);
  RUN_TEST(test_resolution_growth_keeps_nudge_on_time);
  RUN_TEST(test_late_process_keeps_gate);
  RUN_TEST(test_pause_keeps_pattern_queued_later);
  RUN_TEST(test_pause_keeps_select_immediate);
  RUN_TEST(test_tie_extension_after_other_track_took_the_voice);
  RUN_TEST(test_resume_thirty_second_position);
  RUN_TEST(test_resume_eighth_triplet_position);
  RUN_TEST(test_set_bpm_rewinds_step_with_pending_late_note);
  RUN_TEST(test_tie_release_after_late_ratchet_with_swing);
  RUN_TEST(test_tie_release_after_tempo_jump);
  RUN_TEST(test_pause_plays_events_due_before_it);
  RUN_TEST(test_stall_skips_missed_steps);
  RUN_TEST(test_tied_again_continuation_is_released);
  RUN_TEST(test_shown_position_matches_shown_pattern);
  RUN_TEST(test_cca_sends_control_change);
  RUN_TEST(test_muted_track_sends_no_controls);
  RUN_TEST(test_off_step_releases_tie_then_sends_control);
  RUN_TEST(test_cnd_one_of_two_across_passes);
  RUN_TEST(test_tie_extension_with_program_on_step);
  RUN_TEST(test_controls_precede_same_time_notes_on_all_tracks);
  RUN_TEST(test_stall_keeps_controls_of_skipped_steps);
  RUN_TEST(test_send_program_now);
  RUN_TEST(test_release_ties_after_clear);
  RUN_TEST(test_release_ties_keeps_min_gate);
  RUN_TEST(test_release_ties_survives_rewind);
  RUN_TEST(test_deferred_tie_release_survives_rewind);
  RUN_TEST(test_release_ties_keeps_unheard_tie_release);
  RUN_TEST(test_release_ties_keeps_upcoming_tie_length);
  RUN_TEST(test_release_ties_fuzz);
  RUN_TEST(test_song_plays_chain_and_loops);
  RUN_TEST(test_song_repeated_entry_keeps_pattern);
  RUN_TEST(test_song_ignores_queue);
  RUN_TEST(test_song_tempo_change_after_boundary_decided);
  RUN_TEST(test_song_tempo_change_before_boundary_heard);
  RUN_TEST(test_song_fuzz_tempo_pause_keeps_order);
  RUN_TEST(test_song_pause_resume_continues_entry);
  RUN_TEST(test_song_stop_then_start_from_first_entry);
  RUN_TEST(test_song_chain_shrink_while_playing);
  RUN_TEST(test_song_empty_chain_keeps_pattern);
  RUN_TEST(test_song_empty_chain_at_start_plays_current);
  RUN_TEST(test_song_chain_entries_clamped);
  RUN_TEST(test_song_enable_while_playing_starts_chain_at_boundary);
  RUN_TEST(test_song_disable_while_playing_keeps_pattern);
  RUN_TEST(test_song_tie_released_on_change_held_on_repeat);
  RUN_TEST(test_song_select_then_chain_goes_on);
  RUN_TEST(test_song_entries_with_own_length_and_resolution);
  RUN_TEST(test_song_release_ties_across_boundary);
  RUN_TEST(test_song_stall_skips_across_boundaries);
  RUN_TEST(test_song_insert_before_playing_keeps_entry);
  RUN_TEST(test_song_insert_after_boundary_decided);
  RUN_TEST(test_song_delete_before_playing_keeps_entry);
  RUN_TEST(test_song_delete_playing_entry_goes_on_with_next);
  RUN_TEST(test_song_delete_first_playing_entry);
  RUN_TEST(test_song_edit_next_entry_in_lookahead);
  RUN_TEST(test_song_enable_in_lookahead);
  RUN_TEST(test_song_edit_while_stopped_shows_first_entry);
  RUN_TEST(test_midi_track_does_not_reach_synth);
  RUN_TEST(test_int_track_notes_go_to_synth_only);
  RUN_TEST(test_int_track_events_carry_scheduled_time);
  RUN_TEST(test_int_and_midi_same_channel_and_note_both_sound);
  RUN_TEST(test_int_track_muted_sends_nothing_to_synth);
  RUN_TEST(test_int_track_marks_activity);
  RUN_TEST(test_int_track_stop_sends_off_and_all_off);
  RUN_TEST(test_int_track_pgm_goes_to_synth);
  RUN_TEST(test_send_program_int_track_uses_instrument);
  RUN_TEST(test_start_resets_int_tracks);
  RUN_TEST(test_track_out_change_releases_held_notes);
  RUN_TEST(test_int_track_synth_fx_before_note);
  RUN_TEST(test_midi_track_synth_fx_go_nowhere);
  RUN_TEST(test_int_off_note_releases_track);
  RUN_TEST(test_midi_off_never_sends_reset);
  RUN_TEST(test_int_fx_off_releases_track_after_ticks);
  RUN_TEST(test_int_fx_off_on_note_step);
  RUN_TEST(test_fx_off_releases_tie);
  RUN_TEST(test_int_track_step_markers);
  RUN_TEST(test_chain_transpose_melodic_not_drum);
  RUN_TEST(test_chain_transpose_clamps_and_skips_off);
  RUN_TEST(test_chain_repeat_advances_after_n_passes);
  RUN_TEST(test_chain_repeat_edit_before_advance_heard);
  RUN_TEST(test_chain_scene_sets_mutes);
  RUN_TEST(test_chain_empty_scene_ignored);
  RUN_TEST(test_track_length_polymeter);
  RUN_TEST(test_fill_gates_fil_steps);
  RUN_TEST(test_perf_rat2_doubles_and_release_restores);
  RUN_TEST(test_perf_keeps_step_fx_and_replaces_same_cmd);
  RUN_TEST(test_perf_mute_silences_track);
  RUN_TEST(test_perf_synth_fx_skipped_on_midi_track);
  RUN_TEST(test_perf_flt_on_int_track);
  RUN_TEST(test_stop_clears_perf);
  RUN_TEST(test_phase256_within_step);
  RUN_TEST(test_track_mask_silences_others);
  RUN_TEST(test_step_arp_plays_on_following_steps);
  RUN_TEST(test_step_arp_every_two_steps_until_next_note);
  RUN_TEST(test_step_arp_continues_over_loop_and_stops_on_pattern_change);
  RUN_TEST(test_step_arp_updown_over_two_octaves);
  RUN_TEST(test_step_arp_survives_a_stall);
  RUN_TEST(test_step_arp_on_int_track_uses_track_voices);
  RUN_TEST(test_perf_mute_release_survives_rewind);
  return UNITY_END();
}
