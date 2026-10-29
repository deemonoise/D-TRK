#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include <vector>
#include "follow.h"
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

// A wakeup that is due both to send events and to plan a step sends first: planning a step takes
// up to ~2 ms on the device, and an event waiting behind it reaches the synth late.
void test_due_events_go_out_before_planning() {
  struct LoopSink : FakeSink {
    std::vector<uint32_t> offLoop;  // loopCount() when each NoteOff went out
    void send(const uint8_t* b, uint8_t len) override {
      FakeSink::send(b, len);
      if ((b[0] & 0xF0) == 0x80) offLoop.push_back(seq->loopCount());
    }
  } ls;
  p->patterns[0].length = 1;  // every planned step starts a pass: loopCount() counts the planning
  p->patterns[0].steps[0][0].note = 60;
  seq->start(0, ls);
  seq->process(0, ls);  // plans steps 0..2 (two steps ahead)
  const uint32_t planned = seq->loopCount();
  ls.now = 125000;
  seq->process(125000, ls);  // step 0's NoteOff (62.5 ms) is due and step 3 is to be planned
  TEST_ASSERT_FALSE(ls.offLoop.empty());
  TEST_ASSERT_EQUAL_UINT32(planned, ls.offLoop[0]);
  TEST_ASSERT_TRUE(seq->loopCount() > planned);  // and the step was planned in the same call
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

void test_track_15_plays() {
  p->patterns[0].steps[kTracks - 1][0].note = 60;
  seq->start(0, *sink);
  run(0, 1000);
  const uint8_t on = static_cast<uint8_t>(0x90 | (p->tracks[kTracks - 1].channel & 0x0F));
  TEST_ASSERT_EQUAL(1, sink->times(on, 60).size());
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

// Patterns 0..4: 4 steps each, step 0 plays 60 + index. Song on, from slot 0.
static void songSetup(std::initializer_list<uint8_t> slots) {
  for (int i = 0; i < 5; ++i) {
    p->patterns[i].length = 4;
    p->patterns[i].steps[0][0].note = static_cast<uint8_t>(60 + i);
  }
  p->songLen = 0;
  for (uint8_t c : slots) p->song[p->songLen++] = c;
  seq->setSong(0, *sink, true, 0);
}

static void setSongAt(uint64_t t, bool on, int slot) {
  sink->now = t;
  seq->setSong(t, *sink, on, slot);
}

// Step-0 notes (60..64) in the order they went out.
static std::vector<int> heardPatterns() {
  std::vector<int> out;
  for (const auto& r : sink->log)
    if (r.b[0] == 0x90 && r.b[1] >= 60 && r.b[1] < 65) out.push_back(r.b[1] - 60);
  return out;
}

static void fol(int slot, FollowAct a, int chance = 100, int target = 0) {
  p->songFol[slot].act = static_cast<uint8_t>(a);
  p->songFol[slot].chance = static_cast<uint8_t>(chance);
  p->songFol[slot].target = static_cast<uint8_t>(target);
}

void test_follow_stay_repeats_slot() {
  songSetup({1, 2});
  fol(0, FollowAct::Stay);
  seq->start(0, *sink);
  run(0, 1900000);
  const std::vector<int> want = {1, 1, 1, 1};
  TEST_ASSERT_TRUE(heardPatterns() == want);
  TEST_ASSERT_EQUAL(0, seq->heardSongPos());
}

void test_follow_chance_zero_plays_next() {
  songSetup({1, 2});
  fol(0, FollowAct::Stay, 0);
  seq->start(0, *sink);
  run(0, 1900000);
  const std::vector<int> want = {1, 2, 1, 2};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_follow_prev_from_first_and_jump() {
  songSetup({1, 2, 3});
  fol(0, FollowAct::Prev);  // 0 -> the last
  seq->start(0, *sink);
  run(0, 1900000);
  std::vector<int> want = {1, 3, 1, 3};
  TEST_ASSERT_TRUE(heardPatterns() == want);
  tearDown();
  setUp();
  songSetup({1, 2, 3, 4});
  fol(1, FollowAct::Jump, 100, 3);
  seq->start(0, *sink);
  run(0, 2900000);
  want = {1, 2, 4, 1, 2, 4};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

// Slot i = pattern i: the heard patterns are the slots. Live (any salt) follows the walk of that salt,
// wrapping to slot 0 where the walk would end.
static std::vector<int> liveWalk(uint32_t salt, int passes) {
  std::vector<int> v;
  const uint32_t rs = followRunSeed(p->seed, salt);
  int slot = 0;
  for (int n = 0; n < passes; ++n) {
    v.push_back(slot);
    const int nx = followNext(*p, slot, rs, static_cast<uint32_t>(n));
    slot = nx < 0 ? 0 : nx;
  }
  return v;
}

void test_follow_random_matches_walk_and_salt() {
  songSetup({0, 1, 2, 3, 4});
  for (int i = 0; i < 5; ++i) fol(i, FollowAct::Random, 60);
  seq->setFollowSalt(1234);
  seq->start(0, *sink);
  run(0, 5900000);  // 12 passes of 0.5 s
  const std::vector<int> a = heardPatterns();
  TEST_ASSERT_TRUE(a == liveWalk(1234, 12));
  // Restart (Shift+Play): the counter starts over, same salt -> same path.
  seq->stop(6000000, *sink);
  sink->log.clear();
  seq->start(7000000, *sink);
  run(7000000, 12900000);
  TEST_ASSERT_TRUE(heardPatterns() == a);
  // Another salt (the next Play live): another path.
  seq->stop(13000000, *sink);
  sink->log.clear();
  seq->setFollowSalt(99);
  seq->start(14000000, *sink);
  run(14000000, 19900000);
  TEST_ASSERT_TRUE(heardPatterns() == liveWalk(99, 12));
  TEST_ASSERT_FALSE(heardPatterns() == a);
}

// Replanning (tempo, pause) undoes the unheard decision and takes it again with the same count:
// the path stays the walk's.
void test_follow_fuzz_tempo_pause_keeps_path() {
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    tearDown();
    setUp();
    songSetup({0, 1, 2, 3, 4});
    for (int i = 0; i < 5; ++i) fol(i, static_cast<FollowAct>(1 + (i + seed) % 4), 50 + 10 * (i % 3), (i + 2) % 5);
    Rng r(seed * 2654435761u + 11);
    seq->setFollowSalt(seed);
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
    }
    const std::vector<int> got = heardPatterns();
    TEST_ASSERT_TRUE(got.size() > 8);
    const std::vector<int> want = liveWalk(seed, static_cast<int>(got.size()));
    TEST_ASSERT_TRUE(got == want);
  }
}

// A follow edit before the pass end (the advance already planned in the lookahead) is taken.
void test_follow_edit_redecides_unheard_advance() {
  songSetup({1, 2});
  seq->start(0, *sink);
  run(0, 300000);  // slot 0's last step (375 ms) planned, not heard: the advance to slot 1 decided
  fol(0, FollowAct::Stay);
  sink->now = 300000;
  seq->songEdited(300000, *sink, 0, SongOp::Edit);
  run(300000, 1400000);
  const std::vector<int> want = {1, 1, 1};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_plays_slots_and_wraps() {
  songSetup({2, 1, 3});
  TEST_ASSERT_TRUE(seq->song());
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

void test_song_starts_at_set_slot() {
  songSetup({1, 2, 3});
  seq->setSong(0, *sink, true, 2);
  seq->start(0, *sink);
  TEST_ASSERT_EQUAL(3, seq->pattern());
  TEST_ASSERT_EQUAL(2, seq->heardSongPos());
  run(0, 1100000);
  const std::vector<int> want = {3, 1, 2};  // the last slot wraps to slot 0
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_start_slot_clamped() {
  songSetup({1, 2});
  seq->setSong(0, *sink, true, 50);
  seq->start(0, *sink);
  TEST_ASSERT_EQUAL(1, seq->heardSongPos());
  TEST_ASSERT_EQUAL(2, seq->pattern());
}

void test_song_repeated_slot_keeps_pattern() {
  songSetup({1, 1, 2});
  seq->start(0, *sink);
  run(0, 2100000);
  const std::vector<int> want = {1, 1, 2, 1, 1};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_ignores_queue_while_playing() {
  songSetup({1});
  seq->start(0, *sink);
  seq->queuePattern(2);
  TEST_ASSERT_EQUAL(-1, seq->queued());
  TEST_ASSERT_EQUAL(-1, seq->pendingPattern());
  run(0, 1100000);
  const std::vector<int> want = {1, 1, 1};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_stopped_queue_works() {
  songSetup({1, 2});
  seq->queuePattern(3);
  TEST_ASSERT_EQUAL(3, seq->pattern());
  TEST_ASSERT_EQUAL(3, seq->heardPattern());
  seq->start(0, *sink);  // the song plays from its slot
  TEST_ASSERT_EQUAL(1, seq->pattern());
}

void test_song_tempo_change_after_boundary_decided() {
  songSetup({1, 2});
  seq->start(0, *sink);
  run(0, 480000);  // step 3 heard, step 0 of the next slot scheduled
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
      TEST_ASSERT_EQUAL(p->song[seq->heardSongPos()], seq->heardPattern());
    }
    const std::vector<int> got = heardPatterns();
    TEST_ASSERT_TRUE(got.size() > 8);
    const int cyc[4] = {1, 2, 3, 2};
    for (size_t i = 0; i < got.size(); ++i) TEST_ASSERT_EQUAL(cyc[i % 4], got[i]);
  }
}

void test_song_pause_resume_continues_slot() {
  songSetup({1, 2});
  seq->start(0, *sink);
  run(0, 700000);  // in slot 1 (pattern 2), step 1 heard
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

void test_song_stop_then_start_from_set_slot() {
  songSetup({1, 2});
  seq->start(0, *sink);
  run(0, 700000);
  seq->stop(700000, *sink);
  TEST_ASSERT_EQUAL(-1, seq->heardSongPos());  // stopped: no slot shown
  sink->log.clear();
  seq->start(1000000, *sink);
  run(1000000, 1000000);
  TEST_ASSERT_EQUAL(1000000, sink->times(0x90, 61)[0]);
  TEST_ASSERT_EQUAL(0, seq->heardSongPos());
}

void test_song_shrink_while_playing() {
  songSetup({1, 2, 3});
  seq->start(0, *sink);
  run(0, 1100000);  // slot 2 heard
  p->songLen = 2;
  run(1100000, 2100000);
  const std::vector<int> want = {1, 2, 3, 1, 2};
  TEST_ASSERT_TRUE(heardPatterns() == want);
  TEST_ASSERT_EQUAL(1, seq->heardSongPos());
}

void test_song_empty_keeps_pattern() {
  songSetup({1, 2, 3});
  seq->start(0, *sink);
  run(0, 600000);  // slot 1 heard
  p->songLen = 0;
  run(600000, 1600000);
  const std::vector<int> want = {1, 2, 2, 2};
  TEST_ASSERT_TRUE(heardPatterns() == want);
  TEST_ASSERT_EQUAL(-1, seq->heardSongPos());
  seq->queuePattern(4);  // no song without slots: queue works
  TEST_ASSERT_EQUAL(4, seq->queued());
}

void test_song_empty_at_start_plays_current() {
  songSetup({});
  seq->selectPattern(3);
  seq->start(0, *sink);
  TEST_ASSERT_EQUAL(-1, seq->heardSongPos());
  run(0, 600000);
  const std::vector<int> want = {3, 3};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_slots_clamped() {
  songSetup({99});
  p->songLen = 200;
  for (int i = 1; i < kSongMax; ++i) p->song[i] = 1;
  seq->start(0, *sink);
  TEST_ASSERT_EQUAL(kPatterns - 1, seq->pattern());
  run(0, 2100000);  // pattern 15 keeps 16 steps
  TEST_ASSERT_EQUAL(1, seq->heardSongPos());
  TEST_ASSERT_EQUAL(1, seq->heardPattern());
}

void test_song_enable_while_playing_starts_slot_at_boundary() {
  songSetup({2, 3});
  seq->setSong(0, *sink, false, 0);
  TEST_ASSERT_FALSE(seq->song());
  seq->start(0, *sink);
  seq->queuePattern(1);
  run(0, 100000);
  setSongAt(100000, true, 0);  // the song wins over the queued pattern
  TEST_ASSERT_EQUAL(-1, seq->heardSongPos());
  run(100000, 1100000);
  const std::vector<int> want = {0, 2, 3};
  TEST_ASSERT_TRUE(heardPatterns() == want);
  TEST_ASSERT_EQUAL(1, seq->heardSongPos());
}

void test_song_enable_while_looping_goes_to_slot() {
  songSetup({2, 3, 4});
  seq->setSong(0, *sink, false, 0);
  seq->start(0, *sink);
  run(0, 100000);
  setSongAt(100000, true, 2);
  run(100000, 1100000);
  const std::vector<int> want = {0, 4, 2};
  TEST_ASSERT_TRUE(heardPatterns() == want);
  TEST_ASSERT_EQUAL(0, seq->heardSongPos());
}

// A slot past the end enters the last slot, as on start().
void test_song_enable_while_looping_slot_clamped() {
  songSetup({2, 3, 4});
  seq->setSong(0, *sink, false, 0);
  seq->start(0, *sink);
  run(0, 100000);
  setSongAt(100000, true, 50);
  run(100000, 600000);
  const std::vector<int> want = {0, 4};
  TEST_ASSERT_TRUE(heardPatterns() == want);
  TEST_ASSERT_EQUAL(2, seq->heardSongPos());
}

void test_song_disable_while_playing_keeps_pattern() {
  songSetup({2, 3});
  seq->start(0, *sink);
  run(0, 600000);  // slot 1 (pattern 3) heard
  setSongAt(600000, false, 0);
  TEST_ASSERT_EQUAL(1, seq->heardSongPos());  // until the boundary
  run(600000, 1600000);
  const std::vector<int> want = {2, 3, 3, 3};
  TEST_ASSERT_TRUE(heardPatterns() == want);
  TEST_ASSERT_EQUAL(-1, seq->heardSongPos());
}

// Two equal adjacent slots go on like a loop: the tie holds over the boundary.
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

void test_song_select_then_song_goes_on() {
  songSetup({1, 2, 3});
  seq->start(0, *sink);
  run(0, 200000);  // steps up to the 1 -> 2 advance are scheduled
  seq->selectPattern(4);  // from the next step: replaces slot 1's pattern
  run(200000, 600000);
  TEST_ASSERT_EQUAL(4, seq->heardPattern());
  TEST_ASSERT_EQUAL(1, seq->heardSongPos());
  run(600000, 1100000);
  const std::vector<int> want = {1, 4, 3};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_slots_with_own_length_and_resolution() {
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

// Edits the song the way the UI does: the write, then songEdited() at the same time.
static void songInsert(uint64_t t, int at, uint8_t pat) {
  memmove(p->song + at + 1, p->song + at, p->songLen - at);
  memmove(p->songMute + at + 1, p->songMute + at, (p->songLen - at) * sizeof(uint16_t));
  p->song[at] = pat;
  p->songMute[at] = 0;
  ++p->songLen;
  sink->now = t;
  seq->songEdited(t, *sink, at, SongOp::Insert);
}
static void songDelete(uint64_t t, int at) {
  memmove(p->song + at, p->song + at + 1, p->songLen - at - 1);
  memmove(p->songMute + at, p->songMute + at + 1, (p->songLen - at - 1) * sizeof(uint16_t));
  --p->songLen;
  sink->now = t;
  seq->songEdited(t, *sink, at, SongOp::Delete);
}

void test_song_insert_before_playing_keeps_slot() {
  songSetup({1, 2, 3});
  seq->start(0, *sink);
  run(0, 600000);  // slot 1 heard
  songInsert(600000, 0, 4);
  TEST_ASSERT_EQUAL(2, seq->heardSongPos());
  run(600000, 2100000);
  const std::vector<int> want = {1, 2, 3, 4, 1};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_insert_after_boundary_decided() {
  songSetup({1, 2});
  seq->start(0, *sink);
  run(0, 300000);  // 1 -> 2 decided, not heard
  songInsert(300000, 1, 4);
  run(300000, 1100000);
  const std::vector<int> want = {1, 4, 2};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_delete_before_playing_keeps_slot() {
  songSetup({1, 2, 3});
  seq->start(0, *sink);
  run(0, 600000);
  songDelete(600000, 0);
  TEST_ASSERT_EQUAL(0, seq->heardSongPos());
  run(600000, 1600000);
  const std::vector<int> want = {1, 2, 3, 2};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_delete_playing_slot_goes_on_with_next() {
  songSetup({1, 2, 3});
  seq->start(0, *sink);
  run(0, 600000);
  songDelete(600000, 1);
  run(600000, 1600000);
  const std::vector<int> want = {1, 2, 3, 1};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_delete_first_playing_slot() {
  songSetup({1, 2, 3});
  seq->start(0, *sink);
  run(0, 100000);
  songDelete(100000, 0);
  run(100000, 1100000);
  const std::vector<int> want = {1, 2, 3};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

// Deleting the playing slot 0 goes on with the new slot 0 (the next one), not the setSong slot.
void test_song_delete_playing_slot_0_goes_on_with_next() {
  songSetup({1, 2, 3, 4});
  seq->setSong(0, *sink, true, 2);
  seq->start(0, *sink);
  run(0, 1100000);  // slot 2, slot 3, then slot 0 heard
  TEST_ASSERT_EQUAL(0, seq->heardSongPos());
  songDelete(1100000, 0);
  run(1100000, 1600000);
  const std::vector<int> want = {3, 4, 1, 2};
  TEST_ASSERT_TRUE(heardPatterns() == want);
  TEST_ASSERT_EQUAL(0, seq->heardSongPos());
}

void test_song_edit_next_slot_in_lookahead() {
  songSetup({1, 2});
  seq->start(0, *sink);
  run(0, 300000);  // 1 -> 2 decided, not heard
  p->song[1] = 3;
  seq->songEdited(300000, *sink, 1, SongOp::Edit);
  run(300000, 600000);
  const std::vector<int> want = {1, 3};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_enable_in_lookahead() {
  songSetup({2});
  seq->setSong(0, *sink, false, 0);
  seq->start(0, *sink);
  run(0, 300000);  // 0 -> 0 decided, not heard
  setSongAt(300000, true, 0);
  run(300000, 600000);
  const std::vector<int> want = {0, 2};
  TEST_ASSERT_TRUE(heardPatterns() == want);
}

void test_song_edit_while_stopped_keeps_pattern() {
  songSetup({2, 3});
  seq->songEdited(0, *sink, 0, SongOp::Edit);
  TEST_ASSERT_EQUAL(-1, seq->heardSongPos());
  TEST_ASSERT_EQUAL(0, seq->heardPattern());
  TEST_ASSERT_EQUAL(0, seq->pattern());
}

// Every slot start writes all mutes from its mask: slot 0 mutes track 3, slot 1 unmutes it.
void test_song_slot_mask_sets_mutes() {
  songSetup({0, 0});
  p->songMute[0] = 1u << 3;
  p->songMute[1] = 0;
  p->patterns[0].steps[3][0].note = 60;
  p->patterns[0].steps[2][0].note = 60;
  seq->start(0, *sink);
  run(0, 1010000);  // slots at 0, 500 000, 1 000 000
  TEST_ASSERT_TRUE(p->tracks[3].mute);
  auto on = sink->times(0x93, 60);
  TEST_ASSERT_EQUAL(1, on.size());
  TEST_ASSERT_EQUAL(500000, on[0]);
  TEST_ASSERT_EQUAL(3, sink->times(0x92, 60).size());
}

// A sounding (tied) note of a track the next slot mutes ends at the slot start.
void test_song_slot_mask_ends_sounding_note() {
  songSetup({1, 1});
  p->songMute[1] = 1u << 1;
  p->patterns[1].steps[1][3].note = 70;
  p->patterns[1].steps[1][3].fx[0] = {Fx::TIE, 0};
  seq->start(0, *sink);
  run(0, 600000);
  auto off = sink->times(0x81, 70);
  TEST_ASSERT_EQUAL(1, off.size());
  TEST_ASSERT_EQUAL(500000, off[0]);
  TEST_ASSERT_TRUE(p->tracks[1].mute);
}

// A live mute mid-slot holds until the next slot start, which writes the slot's mask.
void test_song_live_mute_overwritten_at_slot_start() {
  songSetup({0, 0});
  p->patterns[0].steps[2][3].note = 61;
  seq->start(0, *sink);
  run(0, 510000);
  p->tracks[2].mute = true;  // slot 1: its step 3 (875 000) is planned after this
  run(510000, 1400000);
  const std::vector<uint64_t> want = {375000, 1375000};
  TEST_ASSERT_TRUE(sink->times(0x92, 61) == want);
  TEST_ASSERT_FALSE(p->tracks[2].mute);
}

// Pre-song mutes: track 5 muted. Slot 0 mutes track 3, slot 1 track 4.
static void songMuteSetup() {
  songSetup({0, 0});
  p->tracks[5].mute = true;
  p->songMute[0] = 1u << 3;
  p->songMute[1] = 1u << 4;
}
static bool muted(int t) { return p->tracks[t].mute; }

void test_song_stop_restores_pre_song_mutes() {
  songMuteSetup();
  seq->start(0, *sink);
  run(0, 600000);  // slot 1 heard
  TEST_ASSERT_TRUE(muted(4));
  TEST_ASSERT_FALSE(muted(5));
  uint16_t held = 0;
  TEST_ASSERT_TRUE(seq->savedMutes(held));
  TEST_ASSERT_EQUAL_HEX16(1u << 5, held);
  seq->stop(600000, *sink);
  TEST_ASSERT_FALSE(muted(3));
  TEST_ASSERT_FALSE(muted(4));
  TEST_ASSERT_TRUE(muted(5));
  TEST_ASSERT_FALSE(seq->savedMutes(held));
}

// SONG -> LOOP: the slot mutes hold until the pass end, then the pre-song mutes return.
void test_song_to_loop_restores_mutes_at_pass_end() {
  songMuteSetup();
  p->patterns[0].steps[3][0].note = 60;
  seq->start(0, *sink);
  run(0, 100000);
  setSongAt(100000, false, 0);
  run(100000, 400000);
  run(400000, 1100000);
  TEST_ASSERT_FALSE(muted(3));
  TEST_ASSERT_TRUE(muted(5));
  const std::vector<uint64_t> want = {500000, 1000000};
  TEST_ASSERT_TRUE(sink->times(0x93, 60) == want);
  seq->stop(1100000, *sink);
  TEST_ASSERT_FALSE(muted(3));
  TEST_ASSERT_TRUE(muted(5));
}

// Slot to slot: the mutes saved at the song start stay (no new snapshot).
void test_song_slots_do_not_resnapshot_mutes() {
  songMuteSetup();
  seq->start(0, *sink);
  run(0, 1100000);  // slots 0, 1, 0
  TEST_ASSERT_TRUE(muted(3));
  seq->stop(1100000, *sink);
  TEST_ASSERT_FALSE(muted(3));
  TEST_ASSERT_FALSE(muted(4));
  TEST_ASSERT_TRUE(muted(5));
}

void test_song_pause_resume_keeps_slot_mutes() {
  songMuteSetup();
  seq->start(0, *sink);
  run(0, 200000);
  sink->now = 200000;
  seq->pause(200000, *sink);
  TEST_ASSERT_TRUE(muted(3));
  TEST_ASSERT_FALSE(muted(5));
  sink->now = 300000;
  seq->resume(300000, *sink);
  run(300000, 600000);  // slot 1 (500 000); the next slot start is not planned yet
  TEST_ASSERT_TRUE(muted(4));
  TEST_ASSERT_FALSE(muted(5));
  seq->stop(600000, *sink);
  TEST_ASSERT_TRUE(muted(5));
  TEST_ASSERT_FALSE(muted(3));
  TEST_ASSERT_FALSE(muted(4));
}

void test_loop_play_never_touches_mutes() {
  songMuteSetup();
  seq->setSong(0, *sink, false, 0);
  seq->start(0, *sink);
  run(0, 600000);
  TEST_ASSERT_FALSE(muted(3));
  TEST_ASSERT_TRUE(muted(5));
  p->tracks[6].mute = true;  // live
  run(600000, 1100000);
  uint16_t held = 0;
  TEST_ASSERT_FALSE(seq->savedMutes(held));
  seq->stop(1100000, *sink);
  TEST_ASSERT_TRUE(muted(5));
  TEST_ASSERT_TRUE(muted(6));
  TEST_ASSERT_FALSE(muted(3));
}

// Clear song while playing: the pattern loops on and the pre-song mutes return at the pass end.
void test_song_clear_during_play_restores_mutes() {
  songMuteSetup();
  seq->start(0, *sink);
  run(0, 200000);
  p->songLen = 0;
  sink->now = 200000;
  seq->songEdited(200000, *sink, 0, SongOp::Edit);
  run(200000, 600000);
  TEST_ASSERT_FALSE(muted(3));
  TEST_ASSERT_TRUE(muted(5));
  seq->stop(600000, *sink);
  TEST_ASSERT_FALSE(muted(3));
  TEST_ASSERT_TRUE(muted(5));
}

// A restore planned ahead (SONG -> LOOP) is undone with its steps by a rewind (pause), and stop still restores.
void test_song_mute_restore_undone_by_rewind() {
  songMuteSetup();
  seq->start(0, *sink);
  run(0, 100000);
  setSongAt(100000, false, 0);
  run(100000, 300000);  // the pass end (step 3, 375 000) is planned, not heard
  sink->now = 300000;
  seq->pause(300000, *sink);
  TEST_ASSERT_TRUE(muted(3));
  TEST_ASSERT_FALSE(muted(5));
  seq->stop(300000, *sink);
  TEST_ASSERT_FALSE(muted(3));
  TEST_ASSERT_TRUE(muted(5));
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

// Transport start reaches the synth once (TEMPO LFOs with Retrig OFF), at the start time; not
// without INT tracks.
void test_start_sends_transport_start_to_synth() {
  p->tracks[3].out = TrackOut::Int;
  p->tracks[5].out = TrackOut::Int;
  seq->start(1234, *sink);
  auto fa = sink->synKind(0xFA);
  TEST_ASSERT_EQUAL(1, fa.size());
  TEST_ASSERT_EQUAL(1, fa[0].len);
  TEST_ASSERT_EQUAL(1234, fa[0].t);
  TEST_ASSERT_EQUAL(1, sink->times(0xFA).size());  // MIDI out: its own 0xFA only
}

static std::vector<SynRec> perfSteps() {
  std::vector<SynRec> out;
  for (const auto& r : sink->synKind(0xF6))
    if (r.b[1] == 4) out.push_back(r);
  return out;
}

// The PERF step clock: 0xF6 4 (1/16 index in the pass, mod 64) on every 1/16 boundary, on the first INT track,
// at the unshifted step time (swing ignored); never on MIDI out.
void test_perf_step_clock_to_synth() {
  p->tracks[2].out = TrackOut::Int;
  p->tracks[5].out = TrackOut::Int;
  p->patterns[0].swing = 75;
  seq->start(0, *sink);
  run(0, 1000000);
  const auto v = perfSteps();
  TEST_ASSERT_TRUE(v.size() >= 6);
  const uint64_t su = 625000ull * 24 / p->bpm;
  for (size_t i = 0; i < v.size(); ++i) {
    TEST_ASSERT_EQUAL(2, v[i].track);
    TEST_ASSERT_EQUAL(3, v[i].len);
    TEST_ASSERT_EQUAL(static_cast<int>(i % p->patterns[0].length), v[i].b[2]);
    TEST_ASSERT_TRUE(v[i].t + 1 >= i * su && v[i].t <= i * su + 1);
  }
  for (const auto& r : sink->log) TEST_ASSERT_NOT_EQUAL(0xF6, r.b[0]);
}

// 1/32 resolution: one 1/16 boundary every 2 steps; no INT track: none.
void test_perf_step_clock_resolution_and_midi_only() {
  p->tracks[0].out = TrackOut::Int;
  p->patterns[0].res = Resolution::ThirtySecond;
  seq->start(0, *sink);
  run(0, 1000000);
  const auto v = perfSteps();
  TEST_ASSERT_TRUE(v.size() >= 6);
  const uint64_t su = 625000ull * 24 / p->bpm;
  const size_t pass = p->patterns[0].length / 2;  // sixteenths per pass
  for (size_t i = 0; i < v.size(); ++i) {
    TEST_ASSERT_EQUAL(static_cast<int>(i % pass), v[i].b[2]);
    TEST_ASSERT_TRUE(v[i].t + 1 >= i * su && v[i].t <= i * su + 1);
  }
  tearDown();
  setUp();
  seq->start(0, *sink);
  run(0, 1000000);
  TEST_ASSERT_EQUAL(0, perfSteps().size());
  for (const auto& r : sink->log) TEST_ASSERT_NOT_EQUAL(0xF6, r.b[0]);
}

// The clock is pass-relative: a 1/32 pattern of 13 steps (6.5 sixteenths) restarts it at 0 on
// every pass (pattern steps 0, 2, .. 12 -> 0 .. 6); it never stops after a pass.
void test_perf_step_clock_odd_pass() {
  p->tracks[0].out = TrackOut::Int;
  p->patterns[0].res = Resolution::ThirtySecond;
  p->patterns[0].length = 13;
  seq->start(0, *sink);
  const uint64_t su = 625000ull * 12 / p->bpm;  // one 1/32 step
  run(0, 3 * 13 * su - su / 2);
  const auto v = perfSteps();
  TEST_ASSERT_EQUAL(3 * 7, v.size());
  for (size_t i = 0; i < v.size(); ++i) {
    const uint64_t pass = i / 7, k = i % 7;
    TEST_ASSERT_EQUAL(static_cast<int>(k), v[i].b[2]);
    const uint64_t want = (pass * 13 + 2 * k) * su;
    TEST_ASSERT_TRUE(v[i].t + 1 >= want && v[i].t <= want + 1);
  }
}

static std::vector<SynRec> songClocks() {
  std::vector<SynRec> out;
  for (const auto& r : sink->synKind(0xF6))
    if (r.b[1] == 5) out.push_back(r);
  return out;
}

// The song clock (0xF6 5, trance gates): the 1/16 count since start, mod 64, not restarted per pass
// (a 12-step pattern: 0..35 over 3 passes), on the first INT track at the unshifted time.
void test_song_clock_absolute_across_passes() {
  p->tracks[1].out = TrackOut::Int;
  p->tracks[4].out = TrackOut::Int;
  p->patterns[0].length = 12;
  p->patterns[0].swing = 70;
  seq->start(0, *sink);
  const uint64_t su = 625000ull * 24 / p->bpm;
  run(0, 6 * 12 * su - su / 2);
  const auto v = songClocks();
  TEST_ASSERT_EQUAL(72, v.size());
  for (size_t i = 0; i < v.size(); ++i) {
    TEST_ASSERT_EQUAL(1, v[i].track);
    TEST_ASSERT_EQUAL(static_cast<int>(i % 64), v[i].b[2]);
    TEST_ASSERT_TRUE(v[i].t + 1 >= i * su && v[i].t <= i * su + 1);
  }
}

// 1/32 and 13 steps (6.5 sixteenths): a clock every other step counted from the start, so passes 2,
// 4 .. begin between two clocks.
void test_song_clock_odd_pass() {
  p->tracks[0].out = TrackOut::Int;
  p->patterns[0].res = Resolution::ThirtySecond;
  p->patterns[0].length = 13;
  seq->start(0, *sink);
  const uint64_t su = 625000ull * 12 / p->bpm;
  run(0, 4 * 13 * su - su / 2);
  const auto v = songClocks();
  TEST_ASSERT_EQUAL(26, v.size());
  for (size_t i = 0; i < v.size(); ++i) {
    TEST_ASSERT_EQUAL(static_cast<int>(i), v[i].b[2]);
    TEST_ASSERT_TRUE(v[i].t + 1 >= 2 * i * su && v[i].t <= 2 * i * su + 1);
  }
}

// Pause rewinds the unheard steps with the count; resume goes on from there (no gap, no repeat).
// Stop tells the synth (0xFD: gates bypassed); start begins at 0 again.
void test_song_clock_pause_resume_stop() {
  p->tracks[2].out = TrackOut::Int;
  const uint64_t su = 625000ull * 24 / p->bpm;
  seq->start(0, *sink);
  run(0, 5 * su + su / 2);
  sink->now = 5 * su + su / 2;
  seq->pause(sink->now, *sink);
  sink->now = 10 * su;
  seq->resume(sink->now, *sink);
  run(10 * su, 14 * su);
  auto v = songClocks();
  TEST_ASSERT_TRUE(v.size() >= 9);
  for (size_t i = 0; i < v.size(); ++i) TEST_ASSERT_EQUAL(static_cast<int>(i), v[i].b[2]);
  TEST_ASSERT_EQUAL(0, sink->synKind(0xFD).size());
  sink->now = 14 * su;
  seq->stop(sink->now, *sink);
  const auto fd = sink->synKind(0xFD);
  TEST_ASSERT_EQUAL(1, fd.size());
  TEST_ASSERT_EQUAL(2, fd[0].track);
  TEST_ASSERT_EQUAL(1, fd[0].len);
  sink->log.clear();
  sink->syn.clear();
  seq->start(20 * su, *sink);
  run(20 * su, 21 * su);
  v = songClocks();
  TEST_ASSERT_TRUE(v.size() >= 1);
  TEST_ASSERT_EQUAL(0, v[0].b[2]);
}

// Pause and resume reach the synth too (TEMPO LFOs with Retrig OFF hold while paused).
void test_pause_resume_sent_to_synth() {
  p->tracks[2].out = TrackOut::Int;
  seq->start(0, *sink);
  run(0, 100000);
  sink->now = 100000;
  seq->pause(100000, *sink);
  auto fc = sink->synKind(0xFC);
  TEST_ASSERT_EQUAL(1, fc.size());
  TEST_ASSERT_EQUAL(2, fc[0].track);
  TEST_ASSERT_EQUAL(1, fc[0].len);
  TEST_ASSERT_EQUAL(100000, fc[0].t);
  TEST_ASSERT_EQUAL(0, sink->synKind(0xFB).size());
  sink->now = 200000;
  seq->resume(200000, *sink);
  auto fb = sink->synKind(0xFB);
  TEST_ASSERT_EQUAL(1, fb.size());
  TEST_ASSERT_EQUAL(1, fb[0].len);
  TEST_ASSERT_EQUAL(200000, fb[0].t);
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

// ---- polymeter ----

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

// 1:2 on step 0 and PRE on step 1: step 1 plays in the passes where step 0 played; NEI on track 2
// follows track 1.
void test_cnd_pre_follows_previous_condition() {
  p->patterns[0].length = 4;
  Step& a = p->patterns[0].steps[0][0];
  a.note = 60;
  a.fx[0] = {Fx::CND, 0x12};
  Step& b = p->patterns[0].steps[0][1];
  b.note = 62;
  b.fx[0] = {Fx::CND, kCndPre};
  Step& n = p->patterns[0].steps[1][2];
  n.note = 64;
  n.fx[0] = {Fx::CND, kCndNei};
  seq->start(0, *sink);
  run(0, 125000 * 16 - 1);  // 4 passes
  TEST_ASSERT_EQUAL(2, sink->times(0x90, 60).size());
  TEST_ASSERT_EQUAL(2, sink->times(0x90, 62).size());
  TEST_ASSERT_EQUAL(2, sink->times(0x91, 64).size());  // track 2 = MIDI channel 2
  TEST_ASSERT_EQUAL(sink->times(0x90, 60)[1] + 125000, sink->times(0x90, 62)[1]);
}

// A groove replaces the swing: per-step shift (% of a step) and velocity accent.
void test_groove_shifts_and_accents() {
  p->patterns[0].length = 4;
  p->patterns[0].swing = 75;  // ignored under a groove
  p->patterns[0].groove = 5;  // SHUFFLE: odd steps +33 %, velocities 100 / 70 / 95 / 70
  for (int i = 0; i < 4; ++i) {
    p->patterns[0].steps[0][i].note = 60;
    p->patterns[0].steps[0][i].vel = 100;
  }
  seq->start(0, *sink);
  run(0, 499999);
  const auto on = sink->times(0x90, 60);
  TEST_ASSERT_EQUAL(4, on.size());
  TEST_ASSERT_EQUAL(0, on[0]);
  TEST_ASSERT_EQUAL(125000 + 125000 * 33 / 100, on[1]);
  TEST_ASSERT_EQUAL(250000, on[2]);
  std::vector<int> vel;
  for (const auto& r : sink->log)
    if (r.b[0] == 0x90) vel.push_back(r.b[2]);
  TEST_ASSERT_EQUAL(100, vel[0]);
  TEST_ASSERT_EQUAL(70, vel[1]);
  TEST_ASSERT_EQUAL(95, vel[2]);
}

// Humanize moves a step at most 10 % of a step off the grid.
void test_humanize_stays_within_range() {
  p->tracks[0].humanize = 100;
  for (int i = 0; i < 16; ++i) p->patterns[0].steps[0][i].note = 60;
  seq->start(0, *sink);
  run(0, 125000 * 16 - 12501);  // the next pass's step 0 may come up to 10 % early
  const auto on = sink->times(0x90, 60);
  TEST_ASSERT_EQUAL(16, on.size());
  bool moved = false;
  for (size_t i = 1; i < on.size(); ++i) {  // step 0 cannot go early (start)
    const int64_t d = static_cast<int64_t>(on[i]) - static_cast<int64_t>(i * 125000);
    TEST_ASSERT_TRUE(d >= -12500 && d <= 12500);
    moved |= d != 0;
  }
  TEST_ASSERT_TRUE(moved);
}

void test_speed_x2_plays_two_steps_per_step() {
  Pattern& pt = p->patterns[0];
  pt.trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::X2);
  pt.steps[0][0].note = 60;
  pt.steps[0][1].note = 62;
  pt.steps[0][2].note = 64;
  seq->start(0, *sink);
  run(0, 130000);
  TEST_ASSERT_EQUAL(0, sink->times(0x90, 60)[0]);
  TEST_ASSERT_EQUAL(62500, sink->times(0x90, 62)[0]);
  TEST_ASSERT_EQUAL(125000, sink->times(0x90, 64)[0]);
  TEST_ASSERT_EQUAL(31250, sink->times(0x80, 60)[0]);  // default gate: half of the track's own step
}

void test_speed_x2_with_x1_track_in_sync() {
  Pattern& pt = p->patterns[0];
  pt.length = 4;
  pt.trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::X2);
  for (int i = 0; i < 4; ++i) pt.steps[0][i].note = 60;  // x2: 4 steps loop twice per pass
  pt.steps[1][0].note = 36;                              // x1: once per pass
  seq->start(0, *sink);
  run(0, 999999);  // two passes of 500000
  TEST_ASSERT_EQUAL(16, sink->times(0x90, 60).size());  // 2 passes x 8
  auto kick = sink->times(0x91, 36);  // track 1 is on channel 1
  TEST_ASSERT_EQUAL(2, kick.size());
  TEST_ASSERT_EQUAL(500000, kick[1]);
}

// A full load at x4 loses nothing: every track plays every sub-step, tracks 0-3 also with RAT 4.
void test_speed_x4_heap_holds() {
  Pattern& pt = p->patterns[0];
  for (int tr = 0; tr < kTracks; ++tr) {
    pt.trackSpeed[tr] = static_cast<uint8_t>(TrackSpeed::X4);
    for (int i = 0; i < pt.length; ++i) {
      pt.steps[tr][i].note = static_cast<uint8_t>(40 + tr);
      if (tr < 4) pt.steps[tr][i].fx[0] = {Fx::RAT, 4};
    }
  }
  seq->start(0, *sink);
  run(0, 1999999);  // one pass: the next starts at 2000000
  for (int tr = 0; tr < kTracks; ++tr) {  // track tr is on MIDI channel tr
    const size_t want = 16u * 4u * (tr < 4 ? 4u : 1u);
    TEST_ASSERT_EQUAL(want, sink->times(static_cast<uint8_t>(0x90 + tr), 40 + tr).size());
    TEST_ASSERT_EQUAL(want, sink->times(static_cast<uint8_t>(0x80 + tr), 40 + tr).size());
  }
}

// test_speed_x4_heap_holds at finer resolutions: the lookahead is 48 ticks, so it plans more pattern
// steps ahead (16th: 2-3, 16th triplet: 3-4, 32nd: 4-5) and fewer RAT tracks fit. Peak heap (NoteOns
// are pushed below kCap - kOffReserve = 768): 16th + RAT 4 on 4 tracks 671; 16T 703 with 2 (3: 799);
// 32nd 759 with 1 (2: 879, 4: 1119).
static void x4Load(Resolution res, int ratTracks) {
  Pattern& pt = p->patterns[0];
  pt.res = res;
  for (int tr = 0; tr < kTracks; ++tr) {
    pt.trackSpeed[tr] = static_cast<uint8_t>(TrackSpeed::X4);
    for (int i = 0; i < pt.length; ++i) {
      pt.steps[tr][i].note = static_cast<uint8_t>(40 + tr);
      if (tr < ratTracks) pt.steps[tr][i].fx[0] = {Fx::RAT, 4};
    }
  }
}

static void checkX4Load(int ratTracks) {
  for (int tr = 0; tr < kTracks; ++tr) {  // track tr is on MIDI channel tr
    const size_t want = 16u * 4u * (tr < ratTracks ? 4u : 1u);
    TEST_ASSERT_EQUAL(want, sink->times(static_cast<uint8_t>(0x90 + tr), 40 + tr).size());
    TEST_ASSERT_EQUAL(want, sink->times(static_cast<uint8_t>(0x80 + tr), 40 + tr).size());
  }
}

void test_speed_x4_heap_holds_at_16th_triplet() {
  x4Load(Resolution::SixteenthTriplet, 2);
  seq->start(0, *sink);
  run(0, 1333332);  // one pass of 16 x 83333
  checkX4Load(2);
}

void test_speed_x4_heap_holds_at_32nd() {
  x4Load(Resolution::ThirtySecond, 1);
  seq->start(0, *sink);
  run(0, 999999);  // one pass of 16 x 62500
  checkX4Load(1);
}

void test_speed_x2_replans_after_edit() {
  Pattern& pt = p->patterns[0];
  pt.trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::X2);
  pt.steps[0][3].note = 60;  // sub-step 1 of pattern step 1: 187500
  seq->start(0, *sink);
  run(0, 10000);             // planned ahead, not heard
  pt.steps[0][3].note = 62;
  seq->releaseTies(10000, *sink);  // replans the unheard steps
  run(10000, 200000);
  TEST_ASSERT_EQUAL(0, sink->times(0x90, 60).size());
  TEST_ASSERT_EQUAL(187500, sink->times(0x90, 62)[0]);
}

void test_speed_half_plays_every_other_step() {
  Pattern& pt = p->patterns[0];
  pt.length = 4;
  pt.trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::Half);
  pt.steps[0][0].note = 60;
  pt.steps[0][1].note = 62;
  pt.steps[0][2].note = 64;
  pt.steps[0][3].note = 65;
  seq->start(0, *sink);
  run(0, 999999);  // two passes of 4 steps
  TEST_ASSERT_EQUAL(0, sink->times(0x90, 60)[0]);
  TEST_ASSERT_EQUAL(250000, sink->times(0x90, 62)[0]);
  TEST_ASSERT_EQUAL(500000, sink->times(0x90, 64)[0]);  // the second pass goes on, not from 0
  TEST_ASSERT_EQUAL(750000, sink->times(0x90, 65)[0]);
  TEST_ASSERT_EQUAL(125000, sink->times(0x80, 60)[0]);  // gate: half of a two-step-long step
}

void test_speed_half_tie_holds_over_skipped_step() {
  // As test_tie_holds_until_next_note_with_overlap, on a 1/2 track: track step 1 = pattern step 2.
  Pattern& pt = p->patterns[0];
  pt.trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::Half);
  pt.steps[0][0].note = 60;
  pt.steps[0][0].fx[0] = {Fx::TIE, 0};
  pt.steps[0][1].note = 64;
  seq->start(0, *sink);
  run(0, 300000);
  auto off60 = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(1, off60.size());
  TEST_ASSERT_EQUAL(251000, off60[0]);  // held through the skipped pattern step 1
  TEST_ASSERT_EQUAL(250000, sink->times(0x90, 64)[0]);
}

void test_speed_half_restarts_on_new_pattern() {
  // Pattern 0 has 3 steps: its 1/2 track would rest on the next pass's step 0, a new pattern starts it at 0.
  p->patterns[0].length = 3;
  p->patterns[0].trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::Half);
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][1].note = 61;
  p->patterns[1].length = 4;
  p->patterns[1].trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::Half);
  p->patterns[1].steps[0][0].note = 62;
  p->patterns[1].steps[0][1].note = 64;
  seq->start(0, *sink);
  seq->queuePattern(1);
  run(0, 700000);
  TEST_ASSERT_EQUAL(250000, sink->times(0x90, 61)[0]);
  auto on62 = sink->times(0x90, 62);
  TEST_ASSERT_EQUAL(1, on62.size());
  TEST_ASSERT_EQUAL(375000, on62[0]);
  TEST_ASSERT_EQUAL(625000, sink->times(0x90, 64)[0]);
}

void test_speed_half_continues_over_own_loop() {
  // Same pattern, 3 steps: the 1/2 track counts through the pass end; its 3 steps take two passes.
  p->patterns[0].length = 3;
  p->patterns[0].trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::Half);
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][1].note = 61;
  p->patterns[0].steps[0][2].note = 62;
  seq->start(0, *sink);
  run(0, 800000);
  auto on60 = sink->times(0x90, 60);
  TEST_ASSERT_EQUAL(2, on60.size());
  TEST_ASSERT_EQUAL(0, on60[0]);
  TEST_ASSERT_EQUAL(750000, on60[1]);  // pass 2, step 0
  TEST_ASSERT_EQUAL(250000, sink->times(0x90, 61)[0]);
  TEST_ASSERT_EQUAL(500000, sink->times(0x90, 62)[0]);  // pass 1, pattern step 1
}

void test_speed_x2_squeezed_into_swung_slot() {
  Pattern& pt = p->patterns[0];
  pt.swing = 75;  // step 1 at 187500
  pt.trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::X2);
  pt.steps[0][0].note = 60;
  pt.steps[0][1].note = 62;
  pt.steps[0][2].note = 64;
  pt.steps[0][3].note = 65;
  seq->start(0, *sink);
  run(0, 240000);
  TEST_ASSERT_EQUAL(0, sink->times(0x90, 60)[0]);
  TEST_ASSERT_EQUAL(93750, sink->times(0x90, 62)[0]);   // slot [0, 187500)
  TEST_ASSERT_EQUAL(187500, sink->times(0x90, 64)[0]);  // slot [187500, 250000)
  TEST_ASSERT_EQUAL(218750, sink->times(0x90, 65)[0]);
  TEST_ASSERT_EQUAL(31250, sink->times(0x80, 60)[0]);  // the gate still counts from the track's step
}

void test_speed_x2_groove_accent_on_both_sub_steps() {
  Pattern& pt = p->patterns[0];
  pt.groove = 5;  // SHUFFLE: odd steps +33 %, velocities 100 / 70 / 95 / 70
  pt.trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::X2);
  for (int i = 0; i < 4; ++i) {
    pt.steps[0][i].note = static_cast<uint8_t>(60 + i);
    pt.steps[0][i].vel = 100;
  }
  seq->start(0, *sink);
  run(0, 240000);
  int vel[4] = {0, 0, 0, 0};
  for (const auto& r : sink->log)
    if (r.b[0] == 0x90 && r.b[1] >= 60 && r.b[1] < 64) vel[r.b[1] - 60] = r.b[2];
  TEST_ASSERT_EQUAL(100, vel[0]);
  TEST_ASSERT_EQUAL(100, vel[1]);
  TEST_ASSERT_EQUAL(70, vel[2]);  // pattern step 1: both sub-steps accented
  TEST_ASSERT_EQUAL(70, vel[3]);
  TEST_ASSERT_EQUAL(166250, sink->times(0x90, 62)[0]);  // 125000 + 33 %: step 1's sub-step 0
}

// A stall ending inside a pattern step: the sub-steps already past are missed, not played together.
void test_speed_x4_stall_skips_past_sub_steps() {
  Pattern& pt = p->patterns[0];
  pt.trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::X4);
  fillSteps(0, 60);
  seq->start(0, *sink);
  run(0, 100000);
  sink->now = 2100000;  // stalled; the step at 2000000 has sub-steps 2000000 .. 2093750
  seq->process(2100000, *sink);
  run(2100000, 2124999);
  TEST_ASSERT_EQUAL(1, between(sink->times(0x90, 60), 2000000, 2125000).size());  // only the last
}

void test_speed_heard_loop_follows_heard_step() {
  p->patterns[0].length = 4;
  seq->start(0, *sink);
  run(0, 400000);  // heard: step 3 of pass 0; planned into pass 1
  TEST_ASSERT_EQUAL(3, seq->playPos());
  TEST_ASSERT_EQUAL(0, seq->heardLoop());
  TEST_ASSERT_EQUAL(1, seq->loopCount());
  run(400001, 520000);
  TEST_ASSERT_EQUAL(0, seq->playPos());
  TEST_ASSERT_EQUAL(1, seq->heardLoop());
}

void test_speed_x4_timing() {
  Pattern& pt = p->patterns[0];
  pt.trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::X4);
  pt.steps[0][0].note = 60;
  pt.steps[0][1].note = 62;
  pt.steps[0][2].note = 64;
  pt.steps[0][3].note = 65;
  seq->start(0, *sink);
  run(0, 120000);
  TEST_ASSERT_EQUAL(0, sink->times(0x90, 60)[0]);
  TEST_ASSERT_EQUAL(31250, sink->times(0x90, 62)[0]);
  TEST_ASSERT_EQUAL(62500, sink->times(0x90, 64)[0]);
  TEST_ASSERT_EQUAL(93750, sink->times(0x90, 65)[0]);
}

void test_speed_quarter_plays_every_fourth_step() {
  Pattern& pt = p->patterns[0];
  pt.trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::Quarter);
  for (int i = 0; i < 4; ++i) pt.steps[0][i].note = static_cast<uint8_t>(60 + i);
  seq->start(0, *sink);
  run(0, 1999999);  // one pass: track steps 0..3
  for (int i = 0; i < 4; ++i) {
    const auto on = sink->times(0x90, 60 + i);
    TEST_ASSERT_EQUAL(1, on.size());
    TEST_ASSERT_EQUAL(500000u * i, on[0]);
  }
  TEST_ASSERT_EQUAL(250000, sink->times(0x80, 60)[0]);  // gate: half of a four-step-long step
}

void test_speed_x2_int_step_marker_ticks() {
  p->tracks[1].out = TrackOut::Int;
  p->patterns[0].res = Resolution::Eighth;  // 48 ticks per pattern step
  p->patterns[0].trackSpeed[1] = static_cast<uint8_t>(TrackSpeed::X2);
  p->patterns[0].steps[1][0].note = 60;
  p->patterns[0].steps[1][1].note = 62;
  seq->start(0, *sink);
  run(0, 240000);
  std::vector<SynRec> mk;
  for (const SynRec& r : sink->synKind(0xF5))
    if (r.b[1] == kSynthStep) mk.push_back(r);
  TEST_ASSERT_EQUAL(2, mk.size());
  TEST_ASSERT_EQUAL_HEX8(0x80 | 24, mk[0].b[2]);
  TEST_ASSERT_EQUAL(0, mk[0].t);
  TEST_ASSERT_EQUAL_HEX8(0x80 | 24, mk[1].b[2]);
  TEST_ASSERT_EQUAL(125000, mk[1].t);
}

void test_speed_x2_step_arp_on_sub_steps() {
  Pattern& pt = p->patterns[0];
  pt.trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::X2);
  Step& s = pt.steps[0][0];
  s.note = 60;
  s.fx[0] = {Fx::CHD, kChordTriad};
  s.fx[1] = {Fx::ARS, kArsDefault};
  pt.steps[0][5].note = kNoteOff;  // track step 5: 312500
  seq->start(0, *sink);
  run(0, 62500 * 7);
  const std::vector<int> on = notesOn(0x90);
  const int want[] = {60, 64, 67, 60, 64};
  TEST_ASSERT_EQUAL(5, on.size());
  for (int i = 0; i < 5; ++i) TEST_ASSERT_EQUAL(want[i], on[i]);
  TEST_ASSERT_EQUAL(62500, sink->times(0x90, 64)[0]);
  TEST_ASSERT_EQUAL(125000, sink->times(0x90, 67)[0]);
  TEST_ASSERT_TRUE(sink->times(0x80, 64)[0] < 125000);  // the arp gate counts from the track's step
}

// A replan reaching back over a pass end keeps a 1/2 track's count (3 steps: it spans two passes).
void test_speed_half_rewind_across_pass_end() {
  Pattern& pt = p->patterns[0];
  pt.length = 3;
  pt.trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::Half);
  pt.steps[0][0].note = 60;
  pt.steps[0][1].note = 61;
  pt.steps[0][2].note = 62;
  seq->start(0, *sink);
  run(0, 240000);  // planned past the pass end at 375000
  seq->releaseTies(240000, *sink);  // replans from step 2
  run(240000, 800000);
  TEST_ASSERT_EQUAL(1, sink->times(0x90, 61).size());
  TEST_ASSERT_EQUAL(250000, sink->times(0x90, 61)[0]);
  TEST_ASSERT_EQUAL(1, sink->times(0x90, 62).size());
  TEST_ASSERT_EQUAL(500000, sink->times(0x90, 62)[0]);  // pass 1, pattern step 1
  const auto on60 = sink->times(0x90, 60);
  TEST_ASSERT_EQUAL(2, on60.size());
  TEST_ASSERT_EQUAL(750000, on60[1]);
}

// Paused after an odd number of passes of a 3-step pattern: a pattern queued now starts its pass
// count at 0, so its 1/2 track plays its step 0 on the first step after resume.
void test_speed_half_queue_while_paused_starts_at_step0() {
  p->patterns[0].length = 3;
  p->patterns[1].length = 3;
  p->patterns[1].trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::Half);
  p->patterns[1].steps[0][0].note = 62;
  seq->start(0, *sink);
  run(0, 450000);  // pass 1 of pattern 0 (375000..750000)
  sink->now = 450000;
  seq->pause(450000, *sink);
  seq->queuePattern(1);
  TEST_ASSERT_EQUAL(0, seq->loopCount());
  TEST_ASSERT_EQUAL(0, seq->heardLoop());
  sink->now = 1000000;
  seq->resume(1000000, *sink);
  run(1000000, 1100000);
  const auto on62 = sink->times(0x90, 62);
  TEST_ASSERT_EQUAL(1, on62.size());
  TEST_ASSERT_EQUAL(1000000, on62[0]);
}

// Pause / resume mid-pattern: a 1/2 track over a 3-step pattern goes on with its count.
void test_speed_half_pause_resume_continues() {
  p->patterns[0].length = 3;
  p->patterns[0].trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::Half);
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][1].note = 61;
  p->patterns[0].steps[0][2].note = 62;
  seq->start(0, *sink);
  run(0, 300000);  // steps up to 250000 heard; 375000 (pass 1, pos 0) planned, not heard
  sink->now = 300000;
  seq->pause(300000, *sink);
  sink->now = 1000000;
  seq->resume(1000000, *sink);  // from pass 1, pos 0 (count 3: rests)
  run(1000000, 1400000);
  const auto on60 = sink->times(0x90, 60), on61 = sink->times(0x90, 61), on62 = sink->times(0x90, 62);
  TEST_ASSERT_EQUAL(2, on60.size());
  TEST_ASSERT_EQUAL(0, on60[0]);
  TEST_ASSERT_EQUAL(1375000, on60[1]);  // pass 2, pos 0 (count 6)
  TEST_ASSERT_EQUAL(1, on61.size());
  TEST_ASSERT_EQUAL(250000, on61[0]);
  TEST_ASSERT_EQUAL(1, on62.size());
  TEST_ASSERT_EQUAL(1125000, on62[0]);  // pass 1, pos 1 (count 4)
}

void test_hint_time_one_step_ahead() {
  TEST_ASSERT_EQUAL_UINT64(1000000 - 125000, hintTime(1000000, 125000, 0));
  TEST_ASSERT_EQUAL_UINT64(1000000 - kHintLeadUs, hintTime(1000000, 500000, 0));  // slow: capped
  TEST_ASSERT_EQUAL_UINT64(950000, hintTime(1000000, 125000, 950000));            // never in the past
  TEST_ASSERT_EQUAL_UINT64(0, hintTime(100000, 125000, 0));                      // no wrap below zero
}

void test_int_sample_steps_send_a_hint_one_step_ahead() {
  p->tracks[2].out = TrackOut::Int;
  p->tracks[2].instr = 0;
  instrSetType(p->instruments[0], InstrType::Sample);
  p->patterns[0].steps[2][0].note = 60;
  Step& s2 = p->patterns[0].steps[2][2];
  s2.note = 62;
  s2.fx[0].cmd = Fx::OFS;
  s2.fx[0].val = 0x40;
  p->tracks[4].out = TrackOut::Int;  // a CHIP instrument: no hint
  p->tracks[4].instr = 1;
  instrSetType(p->instruments[1], InstrType::Chip);
  p->patterns[0].steps[4][2].note = 64;
  p->patterns[0].steps[3][2].note = 65;  // a MIDI track: never a hint
  seq->start(0, *sink);
  run(0, 400000);
  const auto h = sink->synKind(kSynthHint);
  TEST_ASSERT_EQUAL(2, h.size());
  const uint64_t stepUs = 60000000ull / p->bpm / 4;
  const auto on = sink->synKind(0x90);
  const SynRec* h62 = nullptr;
  const SynRec* on62 = nullptr;
  for (const auto& r : h)
    if (r.b[1] == 62) h62 = &r;
  for (const auto& r : on)
    if (r.b[1] == 62) on62 = &r;
  TEST_ASSERT_NOT_NULL(h62);
  TEST_ASSERT_NOT_NULL(on62);
  TEST_ASSERT_EQUAL(2, h62->track);
  TEST_ASSERT_EQUAL(3, h62->len);
  TEST_ASSERT_EQUAL_UINT8(0x40, h62->b[2]);
  TEST_ASSERT_EQUAL_UINT64(on62->t - (stepUs < kHintLeadUs ? stepUs : kHintLeadUs), h62->t);
  for (const auto& r : h)
    if (r.b[1] == 60) TEST_ASSERT_EQUAL_UINT8(0, r.b[2]);  // no OFS: the start
  for (const auto& r : sink->log) TEST_ASSERT_NOT_EQUAL(kSynthHint, r.b[0]);
}

// ---- song slot TRN ----

void test_song_trn_transposes_its_slot() {
  songSetup({1, 2});
  p->songTrn[1] = 2;
  seq->start(0, *sink);
  run(0, 990000);
  const std::vector<int> want = {61, 64};  // slot 0 as written, slot 1 (P3: 62) + 2
  TEST_ASSERT_TRUE(notesOn(0x90) == want);
}

// The scale root moves with TRN: a CHD triad of C major from C, 2 up, is D major (D F# A), not D F A.
void test_song_trn_moves_scale_root_for_chd() {
  songSetup({1});
  p->scaleType = static_cast<uint8_t>(ScaleType::Major);
  Step& st = p->patterns[1].steps[0][0];
  st.note = 60;
  st.fx[0] = {Fx::CHD, kChordTriad};
  p->songTrn[0] = 2;
  seq->start(0, *sink);
  run(0, 400000);
  const std::vector<int> want = {62, 66, 69};
  TEST_ASSERT_TRUE(notesOn(0x90) == want);
}

// KIT tracks never follow TRN, nor tracks with their Song trn bit off.
void test_song_trn_skips_kit_and_masked_tracks() {
  songSetup({1});
  p->tracks[1].instr = 5;
  instrSetType(p->instruments[5], InstrType::Kit);
  Step& d = p->patterns[1].steps[1][0];
  d.note = 0;
  d.vel = 0b00000001;  // lane 0: note 60
  p->patterns[1].steps[2][0].note = 50;
  p->songTrnMask = static_cast<uint16_t>(~(1u << 2));
  p->songTrn[0] = 5;
  seq->start(0, *sink);
  run(0, 400000);
  TEST_ASSERT_TRUE(notesOn(0x90) == std::vector<int>{66});
  TEST_ASSERT_TRUE(notesOn(0x91) == std::vector<int>{60});
  TEST_ASSERT_TRUE(notesOn(0x92) == std::vector<int>{50});
}

// TRACK -> MAIN Song trn off the way the UI does it (the mask, then songEdited(0, Edit)), playing on a
// slot with TRN and BPM and then stopped: the unheard steps follow the new mask, nothing hangs.
void test_song_trn_mask_toggle_replans() {
  songSetup({1});
  p->patterns[1].steps[2][0].note = 50;
  p->songTrn[0] = 5;
  p->songBpm[0] = 140;
  seq->start(0, *sink);
  run(0, 300000);
  p->songTrnMask = static_cast<uint16_t>(~(1u << 2));
  sink->now = 300000;
  seq->songEdited(300000, *sink, 0, SongOp::Edit);
  run(300000, 700000);
  TEST_ASSERT_TRUE(notesOn(0x92) == (std::vector<int>{55, 50}));
  seq->stop(700000, *sink);
  p->songTrnMask = 0xFFFF;
  seq->songEdited(700000, *sink, 0, SongOp::Edit);
  sink->log.clear();
  seq->start(800000, *sink);
  run(800000, 1000000);
  TEST_ASSERT_TRUE(notesOn(0x92) == std::vector<int>{55});
}

// A transposed note past 0..127 is dropped.
void test_song_trn_drops_notes_out_of_range() {
  songSetup({1, 2});
  p->patterns[1].steps[0][0].note = 120;
  p->patterns[2].steps[0][0].note = 10;
  p->songTrn[0] = 24;
  p->songTrn[1] = -24;
  seq->start(0, *sink);
  run(0, 990000);
  TEST_ASSERT_EQUAL(0, notesOn(0x90).size());
}

// LOOP plays the pattern as written.
void test_song_trn_not_in_loop() {
  songSetup({0});
  p->songTrn[0] = 7;
  seq->setSong(0, *sink, false, 0);
  seq->start(0, *sink);
  run(0, 400000);
  TEST_ASSERT_TRUE(notesOn(0x90) == std::vector<int>{60});
}

// ---- song slot BPM ----

static void near(uint64_t want, uint64_t got) {
  const int64_t d = static_cast<int64_t>(got) - static_cast<int64_t>(want);
  TEST_ASSERT_TRUE_MESSAGE(d >= -1 && d <= 1, "time off by more than 1 us");
}

// Slot 1 (P3, BPM 60) starts on the beat of 120: its steps and clocks at 60; the clocks before it at 120;
// p->bpm (the live tempo the synth gets) turns 60 only once the slot is heard. Slot 0 "-" keeps 60.
void test_song_slot_bpm_sets_tempo_at_its_start() {
  songSetup({1, 2});
  fillSteps(2, 62);
  p->songBpm[1] = 60;
  seq->start(0, *sink);
  run(0, 450000);
  TEST_ASSERT_EQUAL(120, p->bpm);  // slot 1 is planned, not heard
  run(450000, 1800000);
  const auto on = sink->times(0x90, 62);
  TEST_ASSERT_EQUAL(4, on.size());
  for (int i = 0; i < 4; ++i) near(500000 + 250000ull * i, on[i]);
  near(1500000, sink->times(0x90, 61)[1]);  // slot 0 again, still at 60
  const auto clk = sink->times(0xF8);
  const auto before = between(clk, 400000, 500000), after = between(clk, 500000, 600000);
  TEST_ASSERT_EQUAL(4, before.size());  // 416666 437500 458333 479166
  near(479166, before.back());
  TEST_ASSERT_EQUAL(500000, after[0]);
  near(541666, after[1]);
  TEST_ASSERT_EQUAL(60, p->bpm);
  uint16_t b = 0;
  TEST_ASSERT_TRUE(seq->savedBpm(b));
  TEST_ASSERT_EQUAL(120, b);
}

void test_song_stop_restores_project_bpm() {
  songSetup({1});
  p->songBpm[0] = 90;
  seq->start(0, *sink);
  run(0, 700000);
  TEST_ASSERT_EQUAL(90, p->bpm);  // the entry slot's tempo from the start
  const auto on = sink->times(0x90, 61);
  TEST_ASSERT_EQUAL(2, on.size());
  near(4 * 625000ull * 24 / 90, on[1]);  // pass of 4 steps at 90
  seq->stop(700000, *sink);
  TEST_ASSERT_EQUAL(120, p->bpm);
  uint16_t b = 0;
  TEST_ASSERT_FALSE(seq->savedBpm(b));
  sink->log.clear();
  seq->setSong(700000, *sink, false, 0);
  sink->now = 1000000;
  seq->start(1000000, *sink);  // LOOP: the project tempo
  run(1000000, 1600000);
  const auto loop = sink->times(0x90, 61);  // P2 (still the pattern) step 0, a pass of 500 ms at 120
  TEST_ASSERT_EQUAL(2, loop.size());
  TEST_ASSERT_EQUAL(1000000, loop[0]);
  near(1500000, loop[1]);
}

// A BPM edit while a slot's tempo holds: the project BPM and the live tempo until a slot with its own BPM.
void test_song_bpm_edit_while_slot_tempo_holds() {
  songSetup({1, 2, 3});
  p->songBpm[1] = 60;
  p->songBpm[2] = 80;
  seq->start(0, *sink);
  run(0, 600000);
  TEST_ASSERT_EQUAL(60, p->bpm);
  seq->setBpm(100);
  run(600000, 900000);
  TEST_ASSERT_EQUAL(100, p->bpm);
  uint16_t b = 0;
  TEST_ASSERT_TRUE(seq->savedBpm(b));
  TEST_ASSERT_EQUAL(100, b);
  run(900000, 1300000);
  TEST_ASSERT_EQUAL(80, p->bpm);
  seq->stop(1300000, *sink);
  TEST_ASSERT_EQUAL(100, p->bpm);
}

// The header BPM while a slot's BPM holds: the live tempo only, until the next slot with its own BPM; the
// project BPM stays. With no slot BPM holding it is a project BPM edit.
void test_song_live_bpm_while_slot_tempo_holds() {
  songSetup({1, 2, 3});
  p->bpm = 120;
  p->songBpm[1] = 60;
  p->songBpm[2] = 80;
  seq->start(0, *sink);
  run(0, 600000);
  TEST_ASSERT_EQUAL(60, p->bpm);
  seq->setLiveBpm(100);
  run(600000, 900000);
  TEST_ASSERT_EQUAL(100, p->bpm);
  uint16_t b = 0;
  TEST_ASSERT_TRUE(seq->savedBpm(b));
  TEST_ASSERT_EQUAL(120, b);
  run(900000, 1300000);
  TEST_ASSERT_EQUAL(80, p->bpm);
  seq->stop(1300000, *sink);
  TEST_ASSERT_EQUAL(120, p->bpm);
  seq->setLiveBpm(90);  // nothing holds: the project BPM
  TEST_ASSERT_EQUAL(90, p->bpm);
  TEST_ASSERT_FALSE(seq->savedBpm(b));
  seq->start(2000000, *sink);
  seq->stop(2100000, *sink);
  TEST_ASSERT_EQUAL(90, p->bpm);
}

// Leaving the song (LOOP) goes back to the project tempo at the pass end.
void test_song_off_restores_bpm() {
  songSetup({1, 2});
  fillSteps(1, 61);
  p->songBpm[0] = 60;
  seq->start(0, *sink);
  setSongAt(300000, false, 0);
  run(300000, 1300000);
  const auto on = between(sink->times(0x90, 61), 1000000, 1300000);
  TEST_ASSERT_EQUAL(3, on.size());
  TEST_ASSERT_EQUAL(1000000, on[0]);
  near(1125000, on[1]);
  TEST_ASSERT_EQUAL(120, p->bpm);
  uint16_t b = 0;
  TEST_ASSERT_FALSE(seq->savedBpm(b));
}

// A slot's tempo planned but unheard is undone with its steps: a replan (edit) or a pause decides again.
void test_song_slot_bpm_replanned_and_resumed() {
  songSetup({1, 2});
  fillSteps(2, 62);
  p->songBpm[1] = 60;
  seq->start(0, *sink);
  run(0, 200000);
  p->songBpm[1] = 150;  // slot 1 already decided (at the plan of step 3) at 60
  sink->now = 200000;
  seq->songEdited(200000, *sink, 1, SongOp::Edit);
  run(200000, 700000);
  auto on = sink->times(0x90, 62);
  near(500000, on[0]);
  near(600000, on[1]);  // a step at 150 is 100 ms
  TEST_ASSERT_EQUAL(150, p->bpm);
  tearDown();
  setUp();
  songSetup({1, 2});
  fillSteps(2, 62);
  p->songBpm[1] = 60;
  seq->start(0, *sink);
  run(0, 450000);
  sink->now = 450000;
  seq->pause(450000, *sink);
  TEST_ASSERT_EQUAL(120, p->bpm);
  sink->now = 2000000;
  seq->resume(2000000, *sink);
  run(2000000, 2600000);
  on = sink->times(0x90, 62);
  TEST_ASSERT_EQUAL(3, on.size());
  TEST_ASSERT_EQUAL(2000000, on[0]);
  near(2250000, on[1]);
  TEST_ASSERT_EQUAL(60, p->bpm);
}

// Short passes, any resolution, a tempo per slot, BPM edits and pauses at random: the clocks never go back in
// time, every NoteOn gets its NoteOff, and the slot tempo is the live one once heard.
void test_song_slot_bpm_fuzz() {
  for (uint32_t seed = 1; seed <= 40; ++seed) {
    tearDown();
    setUp();
    Rng r(seed * 2654435761u + 7);
    p->songLen = 6;
    for (int i = 0; i < 6; ++i) {
      Pattern& pt = p->patterns[i];
      pt.length = static_cast<uint8_t>(4 + r.below(5));
      pt.res = static_cast<Resolution>(r.below(static_cast<uint32_t>(Resolution::Count)));
      for (int k = 0; k < pt.length; ++k) pt.steps[0][k].note = static_cast<uint8_t>(40 + r.below(40));
      p->song[i] = static_cast<uint8_t>(i);
      p->songBpm[i] = r.below(3) ? static_cast<uint16_t>(20 + r.below(281)) : 0;
      p->songTrn[i] = static_cast<int8_t>(static_cast<int>(r.below(49)) - 24);
    }
    seq->setSong(0, *sink, true, 0);
    seq->start(0, *sink);
    uint64_t t = 0;
    uint16_t proj = p->bpm;
    for (int k = 0; k < 30; ++k) {
      const uint64_t to = t + 50000 + r.below(400000);
      run(t, to);
      t = to;
      sink->now = t;
      const uint32_t a = r.below(8);
      if (a == 0) {
        proj = static_cast<uint16_t>(20 + r.below(281));
        seq->setBpm(proj);
      } else if (a == 1) {
        seq->pause(t, *sink);
        t += 100000;
        sink->now = t;
        seq->resume(t, *sink);
      } else if (a == 2) {
        p->songBpm[r.below(6)] = static_cast<uint16_t>(r.below(2) ? 20 + r.below(281) : 0);
        seq->songEdited(t, *sink, 0, SongOp::Edit);
      }
    }
    seq->stop(t, *sink);
    const auto clk = sink->times(0xF8);
    for (size_t i = 1; i < clk.size(); ++i) TEST_ASSERT_TRUE_MESSAGE(clk[i] >= clk[i - 1], "clock back in time");
    int open[128] = {0};
    for (const auto& e : sink->log) {
      if ((e.b[0] & 0xF0) == 0x90 && e.b[2] > 0) ++open[e.b[1]];
      else if ((e.b[0] & 0xF0) == 0x80 || (e.b[0] & 0xF0) == 0x90) open[e.b[1]] = 0;
    }
    for (int n = 0; n < 128; ++n) TEST_ASSERT_EQUAL_MESSAGE(0, open[n], "stuck note");
    uint16_t b = 0;
    TEST_ASSERT_FALSE(seq->savedBpm(b));
    TEST_ASSERT_EQUAL(proj, p->bpm);
  }
}

// Passes of 48 ticks (the lookahead) with a tempo per slot: two tempo changes can wait for their clocks
// at once; every clock still lands at its tick on the slots' tempo map.
static void shortPassesClocksExact(int len) {
  const uint16_t bpms[4] = {60, 240, 90, 300};
  const uint64_t pass = 12u * len;
  p->songLen = 4;
  for (int i = 0; i < 4; ++i) {
    p->patterns[i].length = static_cast<uint8_t>(len);
    p->patterns[i].res = Resolution::ThirtySecond;
    p->song[i] = static_cast<uint8_t>(i);
    p->songBpm[i] = bpms[i];
  }
  seq->setSong(0, *sink, true, 0);
  seq->start(0, *sink);
  run(0, 3000000);
  const auto clk = sink->times(0xF8);
  TEST_ASSERT_TRUE(clk.size() > 60);
  double slotT = 0;  // us
  for (size_t n = 0; n < clk.size(); ++n) {
    const uint64_t tick = 4 * n;
    const int k = static_cast<int>(tick / pass);
    if (n > 0 && tick % pass == 0) slotT += pass * 625000.0 / bpms[(k - 1) % 4];
    const double want = slotT + (tick - static_cast<double>(pass) * k) * 625000.0 / bpms[k % 4];
    const double d = static_cast<double>(clk[n]) - want;
    if (d < -1.5 || d > 1.5) {
      char m[64];
      snprintf(m, sizeof m, "clock %u at %llu, want %.0f", static_cast<unsigned>(n),
               static_cast<unsigned long long>(clk[n]), want);
      TEST_FAIL_MESSAGE(m);
    }
  }
}

void test_song_slot_bpm_short_passes_clocks_exact() { shortPassesClocksExact(4); }
// Passes of 24 and 12 ticks: three and more tempo changes wait inside the lookahead.
void test_song_slot_bpm_shorter_passes_clocks_exact() {
  shortPassesClocksExact(2);
}
void test_song_slot_bpm_one_step_passes_clocks_exact() { shortPassesClocksExact(1); }

// A BPM edit after the clock passed the grid of a swung, still unheard step: the step stays on the old
// tempo's grid (125 ms), only its swing follows the new step length.
void test_set_bpm_keeps_swung_step_grid() {
  p->patterns[0].swing = 75;
  fillSteps(0, 60);
  seq->start(0, *sink);
  run(0, 150000);  // clocks up to tick 28 sent; step 1 (tick 24) sounds at 187500
  seq->setBpm(60);
  run(150000, 400000);
  const auto on = sink->times(0x90, 60);
  TEST_ASSERT_TRUE(on.size() >= 2);
  near(250000, on[1]);
}

// A tied note followed by a step whose note TRN drops (its CC still goes out): the tie ends there.
void test_song_trn_dropped_note_ends_tie() {
  songSetup({1});
  Step& a = p->patterns[1].steps[0][0];
  a.note = 60;
  a.fx[0] = {Fx::TIE, 0};
  Step& b = p->patterns[1].steps[0][1];
  b.note = 120;
  b.fx[0] = {Fx::CCA, 77};
  p->songTrn[0] = 24;
  seq->start(0, *sink);
  run(0, 300000);
  const auto on = sink->times(0x90, 84), off = sink->times(0x80, 84);
  TEST_ASSERT_EQUAL(1, on.size());
  TEST_ASSERT_EQUAL(1, off.size());
  TEST_ASSERT_TRUE(off[0] <= 125000 + kTieOverlapUs);
  TEST_ASSERT_EQUAL(1, sink->times(0xB0).size());  // the CC
}

// --- liveNote: a controller pad plays a note now, held until its off ---

void test_live_note_int_track_on_and_off() {
  p->tracks[3].out = TrackOut::Int;
  p->tracks[3].channel = 9;  // ignored on INT: the nibble is the track
  seq->liveNote(1000, 3, 60, 100, *sink);
  TEST_ASSERT_EQUAL(1, sink->syn.size());
  TEST_ASSERT_EQUAL(3, sink->syn[0].track);
  TEST_ASSERT_EQUAL(1000, sink->syn[0].t);
  TEST_ASSERT_EQUAL_HEX8(0x93, sink->syn[0].b[0]);
  TEST_ASSERT_EQUAL(60, sink->syn[0].b[1]);
  TEST_ASSERT_EQUAL(100, sink->syn[0].b[2]);
  seq->liveNote(2000, 3, 60, 0, *sink);
  TEST_ASSERT_EQUAL(2, sink->syn.size());
  TEST_ASSERT_EQUAL_HEX8(0x83, sink->syn[1].b[0]);
  TEST_ASSERT_EQUAL(60, sink->syn[1].b[1]);
  TEST_ASSERT_EQUAL(0, sink->syn[1].b[2]);
  TEST_ASSERT_EQUAL(0, sink->log.size());
}

void test_live_note_midi_track_uses_its_channel() {
  p->tracks[2].channel = 4;
  seq->liveNote(0, 2, 64, 90, *sink);
  seq->liveNote(10, 2, 64, 0, *sink);
  TEST_ASSERT_EQUAL(2, sink->log.size());
  TEST_ASSERT_EQUAL_HEX8(0x94, sink->log[0].b[0]);
  TEST_ASSERT_EQUAL(64, sink->log[0].b[1]);
  TEST_ASSERT_EQUAL(90, sink->log[0].b[2]);
  TEST_ASSERT_EQUAL_HEX8(0x84, sink->log[1].b[0]);
  TEST_ASSERT_EQUAL(64, sink->log[1].b[1]);
  TEST_ASSERT_EQUAL(0, sink->syn.size());
}

void test_live_note_velocity_clamped_to_127() {
  seq->liveNote(0, 2, 64, 200, *sink);
  TEST_ASSERT_EQUAL(1, sink->log.size());
  TEST_ASSERT_EQUAL(127, sink->log[0].b[2]);
}

void test_live_note_off_without_on_sends_nothing() {
  seq->liveNote(0, 1, 60, 0, *sink);
  p->tracks[2].out = TrackOut::Int;
  seq->liveNote(0, 2, 60, 0, *sink);
  TEST_ASSERT_EQUAL(0, sink->log.size());
  TEST_ASSERT_EQUAL(0, sink->syn.size());
}

void test_live_all_off_ends_every_held_note() {
  seq->liveNote(0, 0, 60, 100, *sink);
  seq->liveNote(0, 1, 62, 100, *sink);
  seq->liveNote(0, 1, 63, 100, *sink);
  seq->liveNote(5, 1, 63, 0, *sink);  // already let go
  sink->log.clear();
  seq->liveAllOff(10, *sink);
  TEST_ASSERT_EQUAL(2, sink->log.size());
  TEST_ASSERT_EQUAL_HEX8(0x80, sink->log[0].b[0] & 0xF0);
  TEST_ASSERT_EQUAL_HEX8(0x80, sink->log[1].b[0] & 0xF0);
  const int a = sink->log[0].b[1], b = sink->log[1].b[1];
  TEST_ASSERT_TRUE((a == 60 && b == 62) || (a == 62 && b == 60));
  sink->log.clear();
  seq->liveAllOff(20, *sink);
  TEST_ASSERT_EQUAL(0, sink->log.size());
}

void test_live_note_plays_while_stopped() {
  TEST_ASSERT_FALSE(seq->playing());
  seq->liveNote(0, 0, 48, 127, *sink);
  TEST_ASSERT_EQUAL(1, sink->log.size());
  TEST_ASSERT_EQUAL_HEX8(0x90, sink->log[0].b[0]);
  TEST_ASSERT_FALSE(seq->playing());
}

void test_live_note_same_note_twice_lets_first_go() {
  seq->liveNote(0, 0, 60, 100, *sink);
  seq->liveNote(5, 0, 60, 80, *sink);
  TEST_ASSERT_EQUAL(3, sink->log.size());
  TEST_ASSERT_EQUAL_HEX8(0x80, sink->log[1].b[0]);
  TEST_ASSERT_EQUAL(60, sink->log[1].b[1]);
  TEST_ASSERT_EQUAL_HEX8(0x90, sink->log[2].b[0]);
  TEST_ASSERT_EQUAL(80, sink->log[2].b[2]);
  seq->liveNote(10, 0, 60, 0, *sink);  // the second one ends
  TEST_ASSERT_EQUAL(4, sink->log.size());
  TEST_ASSERT_EQUAL_HEX8(0x80, sink->log[3].b[0]);
  seq->liveAllOff(20, *sink);
  TEST_ASSERT_EQUAL(4, sink->log.size());
}

void test_live_note_table_full_lets_oldest_go() {
  for (int i = 0; i < Sequencer::kLiveHeld; ++i) seq->liveNote(i, 0, static_cast<uint8_t>(40 + i), 100, *sink);
  TEST_ASSERT_EQUAL(Sequencer::kLiveHeld, sink->log.size());
  seq->liveNote(100, 0, 90, 100, *sink);
  TEST_ASSERT_EQUAL(Sequencer::kLiveHeld + 2, sink->log.size());
  TEST_ASSERT_EQUAL_HEX8(0x80, sink->log[Sequencer::kLiveHeld].b[0]);
  TEST_ASSERT_EQUAL(40, sink->log[Sequencer::kLiveHeld].b[1]);
  TEST_ASSERT_EQUAL_HEX8(0x90, sink->log[Sequencer::kLiveHeld + 1].b[0]);
  TEST_ASSERT_EQUAL(90, sink->log[Sequencer::kLiveHeld + 1].b[1]);
  sink->log.clear();
  seq->liveNote(200, 0, 40, 0, *sink);  // the oldest is no longer held
  TEST_ASSERT_EQUAL(0, sink->log.size());
}

void test_live_note_out_of_range_ignored() {
  seq->liveNote(0, -1, 60, 100, *sink);
  seq->liveNote(0, kTracks, 60, 100, *sink);
  seq->liveNote(0, 0, 128, 100, *sink);
  TEST_ASSERT_EQUAL(0, sink->log.size());
  TEST_ASSERT_EQUAL(0, sink->syn.size());
}

void test_live_note_dropped_on_track_out_change() {
  seq->liveNote(0, 1, 60, 100, *sink);
  p->tracks[1].out = TrackOut::Int;
  seq->trackOutChanged(10, 1, *sink);
  sink->log.clear();
  sink->syn.clear();
  seq->liveNote(20, 1, 60, 0, *sink);
  seq->liveAllOff(30, *sink);
  TEST_ASSERT_EQUAL(0, sink->log.size());
  TEST_ASSERT_EQUAL(0, sink->syn.size());
}

void test_live_note_dropped_on_stop_silence() {
  seq->start(0, *sink);
  seq->liveNote(0, 0, 60, 100, *sink);
  seq->stop(10, *sink);  // playing: silence() ends every note
  sink->log.clear();
  seq->liveAllOff(20, *sink);
  TEST_ASSERT_EQUAL(0, sink->log.size());
}

// MIDI panic (a new BLE OUT link): All Notes Off once per channel a MIDI track uses; INT tracks none.
void test_midi_panic_each_midi_channel_once() {
  for (TrackCfg& c : p->tracks) c.out = TrackOut::Int;
  p->tracks[0].out = TrackOut::Midi;
  p->tracks[0].channel = 0;  // CH 1
  p->tracks[3].out = TrackOut::Midi;
  p->tracks[3].channel = 4;  // CH 5
  p->tracks[1].channel = 7;  // INT: ignored
  seq->midiPanic(0, *sink);
  TEST_ASSERT_EQUAL(2, sink->log.size());
  TEST_ASSERT_EQUAL(0, sink->syn.size());
  const uint8_t a[3] = {0xB0, 0x7B, 0x00}, b[3] = {0xB4, 0x7B, 0x00};
  TEST_ASSERT_EQUAL(3, sink->log[0].len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(a, sink->log[0].b, 3);
  TEST_ASSERT_EQUAL(3, sink->log[1].len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(b, sink->log[1].b, 3);
}

void test_midi_panic_shared_channel_sent_once() {
  for (TrackCfg& c : p->tracks) c.out = TrackOut::Int;
  p->tracks[2].out = TrackOut::Midi;
  p->tracks[2].channel = 9;
  p->tracks[6].out = TrackOut::Midi;
  p->tracks[6].channel = 9;
  seq->midiPanic(0, *sink);
  TEST_ASSERT_EQUAL(1, sink->log.size());
  const uint8_t a[3] = {0xB9, 0x7B, 0x00};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(a, sink->log[0].b, 3);
}

// A pattern note retaking the live note's voice owns it: the live off sends nothing, the pattern's
// own off still ends it.
void test_live_note_voice_retaken_by_pattern_note() {
  p->patterns[0].steps[0][0].note = 60;
  seq->liveNote(0, 0, 60, 100, *sink);
  seq->start(0, *sink);
  run(0, 1000);
  // live on, then the pattern's on retakes the voice: its off first
  TEST_ASSERT_EQUAL(1, sink->times(0x80, 60).size());
  TEST_ASSERT_EQUAL(2, sink->times(0x90, 60).size());
  sink->now = 2000;
  seq->liveNote(2000, 0, 60, 0, *sink);
  TEST_ASSERT_EQUAL(1, sink->times(0x80, 60).size());  // the live off: nothing
  run(2000, 100000);
  auto off = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(2, off.size());
  TEST_ASSERT_EQUAL(62500, off[1]);  // the pattern note's own off
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_start_sends_start_then_clock);
  RUN_TEST(test_clock_24ppqn_at_120);
  RUN_TEST(test_sixteenths_at_120);
  RUN_TEST(test_due_events_go_out_before_planning);
  RUN_TEST(test_swing_delays_odd_steps);
  RUN_TEST(test_queued_pattern_switches_at_loop_end);
  RUN_TEST(test_pause_silences_and_stops_output);
  RUN_TEST(test_resume_sends_song_position_and_continue);
  RUN_TEST(test_retrigger_ignores_stale_note_off);
  RUN_TEST(test_muted_track_is_silent);
  RUN_TEST(test_drum_track_plays_lane_notes);
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
  RUN_TEST(test_song_plays_slots_and_wraps);
  RUN_TEST(test_song_starts_at_set_slot);
  RUN_TEST(test_song_start_slot_clamped);
  RUN_TEST(test_song_repeated_slot_keeps_pattern);
  RUN_TEST(test_song_ignores_queue_while_playing);
  RUN_TEST(test_song_stopped_queue_works);
  RUN_TEST(test_song_tempo_change_after_boundary_decided);
  RUN_TEST(test_song_tempo_change_before_boundary_heard);
  RUN_TEST(test_song_fuzz_tempo_pause_keeps_order);
  RUN_TEST(test_song_pause_resume_continues_slot);
  RUN_TEST(test_song_stop_then_start_from_set_slot);
  RUN_TEST(test_song_shrink_while_playing);
  RUN_TEST(test_song_empty_keeps_pattern);
  RUN_TEST(test_song_empty_at_start_plays_current);
  RUN_TEST(test_song_slots_clamped);
  RUN_TEST(test_follow_stay_repeats_slot);
  RUN_TEST(test_follow_chance_zero_plays_next);
  RUN_TEST(test_follow_prev_from_first_and_jump);
  RUN_TEST(test_follow_random_matches_walk_and_salt);
  RUN_TEST(test_follow_fuzz_tempo_pause_keeps_path);
  RUN_TEST(test_follow_edit_redecides_unheard_advance);
  RUN_TEST(test_song_enable_while_playing_starts_slot_at_boundary);
  RUN_TEST(test_song_enable_while_looping_goes_to_slot);
  RUN_TEST(test_song_enable_while_looping_slot_clamped);
  RUN_TEST(test_song_disable_while_playing_keeps_pattern);
  RUN_TEST(test_song_tie_released_on_change_held_on_repeat);
  RUN_TEST(test_song_select_then_song_goes_on);
  RUN_TEST(test_song_slots_with_own_length_and_resolution);
  RUN_TEST(test_song_release_ties_across_boundary);
  RUN_TEST(test_song_stall_skips_across_boundaries);
  RUN_TEST(test_song_insert_before_playing_keeps_slot);
  RUN_TEST(test_song_insert_after_boundary_decided);
  RUN_TEST(test_song_delete_before_playing_keeps_slot);
  RUN_TEST(test_song_delete_playing_slot_goes_on_with_next);
  RUN_TEST(test_song_delete_first_playing_slot);
  RUN_TEST(test_song_delete_playing_slot_0_goes_on_with_next);
  RUN_TEST(test_song_edit_next_slot_in_lookahead);
  RUN_TEST(test_song_enable_in_lookahead);
  RUN_TEST(test_song_edit_while_stopped_keeps_pattern);
  RUN_TEST(test_song_slot_mask_sets_mutes);
  RUN_TEST(test_song_slot_mask_ends_sounding_note);
  RUN_TEST(test_song_live_mute_overwritten_at_slot_start);
  RUN_TEST(test_song_stop_restores_pre_song_mutes);
  RUN_TEST(test_song_to_loop_restores_mutes_at_pass_end);
  RUN_TEST(test_song_slots_do_not_resnapshot_mutes);
  RUN_TEST(test_song_pause_resume_keeps_slot_mutes);
  RUN_TEST(test_loop_play_never_touches_mutes);
  RUN_TEST(test_song_clear_during_play_restores_mutes);
  RUN_TEST(test_song_mute_restore_undone_by_rewind);
  RUN_TEST(test_midi_track_does_not_reach_synth);
  RUN_TEST(test_int_track_notes_go_to_synth_only);
  RUN_TEST(test_int_track_events_carry_scheduled_time);
  RUN_TEST(test_int_and_midi_same_channel_and_note_both_sound);
  RUN_TEST(test_int_track_muted_sends_nothing_to_synth);
  RUN_TEST(test_int_track_stop_sends_off_and_all_off);
  RUN_TEST(test_int_track_pgm_goes_to_synth);
  RUN_TEST(test_send_program_int_track_uses_instrument);
  RUN_TEST(test_start_resets_int_tracks);
  RUN_TEST(test_start_sends_transport_start_to_synth);
  RUN_TEST(test_perf_step_clock_to_synth);
  RUN_TEST(test_perf_step_clock_resolution_and_midi_only);
  RUN_TEST(test_perf_step_clock_odd_pass);
  RUN_TEST(test_song_clock_absolute_across_passes);
  RUN_TEST(test_song_clock_odd_pass);
  RUN_TEST(test_song_clock_pause_resume_stop);
  RUN_TEST(test_pause_resume_sent_to_synth);
  RUN_TEST(test_track_out_change_releases_held_notes);
  RUN_TEST(test_int_track_synth_fx_before_note);
  RUN_TEST(test_midi_track_synth_fx_go_nowhere);
  RUN_TEST(test_int_off_note_releases_track);
  RUN_TEST(test_midi_off_never_sends_reset);
  RUN_TEST(test_int_fx_off_releases_track_after_ticks);
  RUN_TEST(test_int_fx_off_on_note_step);
  RUN_TEST(test_fx_off_releases_tie);
  RUN_TEST(test_int_track_step_markers);
  RUN_TEST(test_track_length_polymeter);
  RUN_TEST(test_fill_gates_fil_steps);
  RUN_TEST(test_phase256_within_step);
  RUN_TEST(test_track_mask_silences_others);
  RUN_TEST(test_step_arp_plays_on_following_steps);
  RUN_TEST(test_step_arp_every_two_steps_until_next_note);
  RUN_TEST(test_step_arp_continues_over_loop_and_stops_on_pattern_change);
  RUN_TEST(test_step_arp_updown_over_two_octaves);
  RUN_TEST(test_step_arp_survives_a_stall);
  RUN_TEST(test_step_arp_on_int_track_uses_track_voices);
  RUN_TEST(test_cnd_pre_follows_previous_condition);
  RUN_TEST(test_groove_shifts_and_accents);
  RUN_TEST(test_humanize_stays_within_range);
  RUN_TEST(test_speed_x2_plays_two_steps_per_step);
  RUN_TEST(test_speed_x2_with_x1_track_in_sync);
  RUN_TEST(test_speed_x4_heap_holds);
  RUN_TEST(test_speed_x4_heap_holds_at_16th_triplet);
  RUN_TEST(test_speed_x4_heap_holds_at_32nd);
  RUN_TEST(test_speed_half_queue_while_paused_starts_at_step0);
  RUN_TEST(test_speed_half_pause_resume_continues);
  RUN_TEST(test_speed_x2_replans_after_edit);
  RUN_TEST(test_speed_half_plays_every_other_step);
  RUN_TEST(test_speed_half_tie_holds_over_skipped_step);
  RUN_TEST(test_speed_half_restarts_on_new_pattern);
  RUN_TEST(test_speed_half_continues_over_own_loop);
  RUN_TEST(test_speed_x2_squeezed_into_swung_slot);
  RUN_TEST(test_speed_x2_groove_accent_on_both_sub_steps);
  RUN_TEST(test_speed_x4_stall_skips_past_sub_steps);
  RUN_TEST(test_speed_heard_loop_follows_heard_step);
  RUN_TEST(test_speed_x4_timing);
  RUN_TEST(test_speed_quarter_plays_every_fourth_step);
  RUN_TEST(test_speed_x2_int_step_marker_ticks);
  RUN_TEST(test_speed_x2_step_arp_on_sub_steps);
  RUN_TEST(test_speed_half_rewind_across_pass_end);
  RUN_TEST(test_hint_time_one_step_ahead);
  RUN_TEST(test_int_sample_steps_send_a_hint_one_step_ahead);
  RUN_TEST(test_song_trn_transposes_its_slot);
  RUN_TEST(test_song_trn_moves_scale_root_for_chd);
  RUN_TEST(test_song_trn_skips_kit_and_masked_tracks);
  RUN_TEST(test_song_trn_mask_toggle_replans);
  RUN_TEST(test_song_trn_drops_notes_out_of_range);
  RUN_TEST(test_song_trn_not_in_loop);
  RUN_TEST(test_song_slot_bpm_sets_tempo_at_its_start);
  RUN_TEST(test_song_stop_restores_project_bpm);
  RUN_TEST(test_song_bpm_edit_while_slot_tempo_holds);
  RUN_TEST(test_song_live_bpm_while_slot_tempo_holds);
  RUN_TEST(test_song_off_restores_bpm);
  RUN_TEST(test_song_slot_bpm_replanned_and_resumed);
  RUN_TEST(test_song_slot_bpm_fuzz);
  RUN_TEST(test_song_slot_bpm_short_passes_clocks_exact);
  RUN_TEST(test_song_slot_bpm_shorter_passes_clocks_exact);
  RUN_TEST(test_song_slot_bpm_one_step_passes_clocks_exact);
  RUN_TEST(test_set_bpm_keeps_swung_step_grid);
  RUN_TEST(test_song_trn_dropped_note_ends_tie);
  RUN_TEST(test_live_note_int_track_on_and_off);
  RUN_TEST(test_live_note_midi_track_uses_its_channel);
  RUN_TEST(test_live_note_velocity_clamped_to_127);
  RUN_TEST(test_live_note_off_without_on_sends_nothing);
  RUN_TEST(test_live_all_off_ends_every_held_note);
  RUN_TEST(test_live_note_plays_while_stopped);
  RUN_TEST(test_live_note_same_note_twice_lets_first_go);
  RUN_TEST(test_live_note_table_full_lets_oldest_go);
  RUN_TEST(test_live_note_out_of_range_ignored);
  RUN_TEST(test_live_note_dropped_on_track_out_change);
  RUN_TEST(test_live_note_dropped_on_stop_silence);
  RUN_TEST(test_live_note_voice_retaken_by_pattern_note);
  RUN_TEST(test_midi_panic_each_midi_channel_once);
  RUN_TEST(test_midi_panic_shared_channel_sent_once);
  return UNITY_END();
}
