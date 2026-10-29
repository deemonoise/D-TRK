#include <unity.h>
#include <vector>
#include "ble_midi_parse.h"

using namespace mt;

void setUp() {}
void tearDown() {}

namespace {

struct Collected {
  std::vector<MidiMsg> m;
};

void sink(const MidiMsg& m, void* ctx) { static_cast<Collected*>(ctx)->m.push_back(m); }

void assertMsg(const MidiMsg& m, uint8_t status, uint8_t d1, uint8_t d2) {
  TEST_ASSERT_EQUAL_HEX8(status, m.status);
  TEST_ASSERT_EQUAL(d1, m.d1);
  TEST_ASSERT_EQUAL(d2, m.d2);
}

}  // namespace

void test_one_note_on() {
  BleMidiParser p; Collected g;
  const uint8_t pkt[] = {0x80, 0x80, 0x99, 36, 100};
  p.feed(pkt, sizeof(pkt), sink, &g);
  TEST_ASSERT_EQUAL(1, g.m.size());
  assertMsg(g.m[0], 0x99, 36, 100);
}

void test_two_messages_with_timestamps() {
  BleMidiParser p; Collected g;
  const uint8_t pkt[] = {0x80, 0x81, 0x90, 60, 90, 0x82, 0xB0, 70, 5};
  p.feed(pkt, sizeof(pkt), sink, &g);
  TEST_ASSERT_EQUAL(2, g.m.size());
  assertMsg(g.m[0], 0x90, 60, 90);
  assertMsg(g.m[1], 0xB0, 70, 5);
}

void test_running_status_without_timestamp() {
  BleMidiParser p; Collected g;
  const uint8_t pkt[] = {0x80, 0x80, 0xB0, 70, 1, 71, 2};
  p.feed(pkt, sizeof(pkt), sink, &g);
  TEST_ASSERT_EQUAL(2, g.m.size());
  assertMsg(g.m[1], 0xB0, 71, 2);
}

void test_running_status_across_packets() {
  BleMidiParser p; Collected g;
  const uint8_t a[] = {0x80, 0x80, 0x90, 60, 90};
  const uint8_t b[] = {0x80, 0x80, 62, 91};  // timestamp then data: running status
  p.feed(a, sizeof(a), sink, &g);
  p.feed(b, sizeof(b), sink, &g);
  TEST_ASSERT_EQUAL(2, g.m.size());
  assertMsg(g.m[1], 0x90, 62, 91);
}

void test_program_change_has_one_data_byte() {
  BleMidiParser p; Collected g;
  const uint8_t pkt[] = {0x80, 0x80, 0xC0, 5, 0x80, 0x90, 60, 1};
  p.feed(pkt, sizeof(pkt), sink, &g);
  TEST_ASSERT_EQUAL(2, g.m.size());
  assertMsg(g.m[0], 0xC0, 5, 0);
  assertMsg(g.m[1], 0x90, 60, 1);
}

void test_channel_pressure_has_one_data_byte() {
  BleMidiParser p; Collected g;
  const uint8_t pkt[] = {0x80, 0x80, 0xD3, 77, 78};  // second byte: running status
  p.feed(pkt, sizeof(pkt), sink, &g);
  TEST_ASSERT_EQUAL(2, g.m.size());
  assertMsg(g.m[0], 0xD3, 77, 0);
  assertMsg(g.m[1], 0xD3, 78, 0);
}

void test_data_len_table() {
  TEST_ASSERT_EQUAL(2, midiDataLen(0x80));
  TEST_ASSERT_EQUAL(2, midiDataLen(0x9F));
  TEST_ASSERT_EQUAL(2, midiDataLen(0xA0));
  TEST_ASSERT_EQUAL(2, midiDataLen(0xB0));
  TEST_ASSERT_EQUAL(1, midiDataLen(0xC5));
  TEST_ASSERT_EQUAL(1, midiDataLen(0xD0));
  TEST_ASSERT_EQUAL(2, midiDataLen(0xE0));
}

void test_sysex_skipped_across_packets() {
  BleMidiParser p; Collected g;
  const uint8_t a[] = {0x80, 0x80, 0xF0, 1, 2, 3};
  const uint8_t b[] = {0x80, 4, 5, 0x80, 0xF7, 0x80, 0x90, 60, 1};
  p.feed(a, sizeof(a), sink, &g);
  p.feed(b, sizeof(b), sink, &g);
  TEST_ASSERT_EQUAL(1, g.m.size());
  assertMsg(g.m[0], 0x90, 60, 1);
}

void test_bad_header_dropped() {
  BleMidiParser p; Collected g;
  const uint8_t pkt[] = {0x10, 0x80, 0x90, 60, 1};
  p.feed(pkt, sizeof(pkt), sink, &g);
  TEST_ASSERT_EQUAL(0, g.m.size());
}

void test_realtime_inside_message_passes_through() {
  BleMidiParser p; Collected g;
  // Note-on split by a clock with its own timestamp, then running-status data after a clock.
  const uint8_t pkt[] = {0x80, 0x80, 0x90, 60, 0x81, 0xF8, 90, 0x82, 0xF8, 61, 91};
  p.feed(pkt, sizeof(pkt), sink, &g);
  TEST_ASSERT_EQUAL(4, g.m.size());
  assertMsg(g.m[0], 0xF8, 0, 0);
  assertMsg(g.m[1], 0x90, 60, 90);
  assertMsg(g.m[2], 0xF8, 0, 0);
  assertMsg(g.m[3], 0x90, 61, 91);
}

void test_realtime_without_running_status() {
  BleMidiParser p; Collected g;
  const uint8_t pkt[] = {0x80, 0x80, 0xFA, 0x81, 0xF8, 5};  // stray data: no status, ignored
  p.feed(pkt, sizeof(pkt), sink, &g);
  TEST_ASSERT_EQUAL(2, g.m.size());
  assertMsg(g.m[0], 0xFA, 0, 0);
  assertMsg(g.m[1], 0xF8, 0, 0);
}

void test_realtime_inside_sysex_passes_through() {
  BleMidiParser p; Collected g;
  const uint8_t pkt[] = {0x80, 0x80, 0xF0, 1, 2, 0x80, 0xFA, 3, 0x80, 0xF7};
  p.feed(pkt, sizeof(pkt), sink, &g);
  TEST_ASSERT_EQUAL(1, g.m.size());
  assertMsg(g.m[0], 0xFA, 0, 0);
}

void test_note_on_velocity_zero_kept() {
  BleMidiParser p; Collected g;
  const uint8_t pkt[] = {0x80, 0x80, 0x90, 60, 0};
  p.feed(pkt, sizeof(pkt), sink, &g);
  TEST_ASSERT_EQUAL(1, g.m.size());
  assertMsg(g.m[0], 0x90, 60, 0);
}

void test_cut_message_dropped_at_packet_end() {
  BleMidiParser p; Collected g;
  const uint8_t a[] = {0x80, 0x80, 0x90, 60};  // note-on cut after the note number
  const uint8_t b[] = {0x80, 0x80, 90};         // one data byte: not enough alone
  p.feed(a, sizeof(a), sink, &g);
  p.feed(b, sizeof(b), sink, &g);
  TEST_ASSERT_EQUAL(0, g.m.size());
  const uint8_t c[] = {0x80, 0x80, 62, 91};  // running status survived: own data still works
  p.feed(c, sizeof(c), sink, &g);
  TEST_ASSERT_EQUAL(1, g.m.size());
  assertMsg(g.m[0], 0x90, 62, 91);
}

void test_new_status_drops_partial() {
  BleMidiParser p; Collected g;
  const uint8_t pkt[] = {0x80, 0x80, 0x90, 60, 0x80, 0xB0, 7, 100};
  p.feed(pkt, sizeof(pkt), sink, &g);
  TEST_ASSERT_EQUAL(1, g.m.size());
  assertMsg(g.m[0], 0xB0, 7, 100);
}

void test_sysex_cancels_running_status() {
  BleMidiParser p; Collected g;
  const uint8_t pkt[] = {0x80, 0x80, 0x90, 60, 90, 0x80, 0xF0, 1, 0x80, 0xF7, 0x80, 61, 91};
  p.feed(pkt, sizeof(pkt), sink, &g);
  TEST_ASSERT_EQUAL(1, g.m.size());
  assertMsg(g.m[0], 0x90, 60, 90);
}

void test_system_common_cancels_running_status() {
  BleMidiParser p; Collected g;
  const uint8_t pkt[] = {0x80, 0x80, 0x90, 60, 90, 0x80, 0xF6, 0x80, 61, 91};
  p.feed(pkt, sizeof(pkt), sink, &g);
  TEST_ASSERT_EQUAL(1, g.m.size());  // F6 not emitted, 61 91 has no status
  assertMsg(g.m[0], 0x90, 60, 90);
}

void test_reset_clears_state() {
  BleMidiParser p; Collected g;
  const uint8_t a[] = {0x80, 0x80, 0x90, 60, 90};
  p.feed(a, sizeof(a), sink, &g);
  p.reset();
  const uint8_t b[] = {0x80, 0x80, 62, 92};  // no running status after reset
  p.feed(b, sizeof(b), sink, &g);
  TEST_ASSERT_EQUAL(1, g.m.size());
  assertMsg(g.m[0], 0x90, 60, 90);

  const uint8_t c[] = {0x80, 0x80, 0xF0, 1, 2};  // open SysEx
  p.feed(c, sizeof(c), sink, &g);
  p.reset();
  const uint8_t d[] = {0x80, 0x80, 0x90, 63, 93};  // parsed, not swallowed as SysEx
  p.feed(d, sizeof(d), sink, &g);
  TEST_ASSERT_EQUAL(2, g.m.size());
  assertMsg(g.m[1], 0x90, 63, 93);
}

void test_short_or_null_packet_ignored() {
  BleMidiParser p; Collected g;
  const uint8_t pkt[] = {0x80};
  p.feed(pkt, 1, sink, &g);
  p.feed(nullptr, 0, sink, &g);
  p.feed(nullptr, 5, sink, &g);
  TEST_ASSERT_EQUAL(0, g.m.size());
}

// --- packer ---

namespace {

TimedMsg msgAt(uint32_t ms, uint8_t s, uint8_t d1 = 0, uint8_t d2 = 0, int len = -1) {
  TimedMsg t{ms, {s, d1, d2}, 0};
  t.len = static_cast<uint8_t>(len >= 0 ? len : (s >= 0xF8 ? 1 : 1 + midiDataLen(s)));
  return t;
}

void assertBytes(const std::vector<uint8_t>& want, const uint8_t* got, int n) {
  TEST_ASSERT_EQUAL(static_cast<int>(want.size()), n);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want.data(), got, n);
}

}  // namespace

void test_pack_one_note_on() {
  const uint32_t ms = 1234567;
  const TimedMsg in[] = {msgAt(ms, 0x90, 60, 100)};
  uint8_t out[32]; int taken = -1;
  const int n = blePack(in, 1, out, sizeof(out), taken);
  TEST_ASSERT_EQUAL(1, taken);
  assertBytes({static_cast<uint8_t>(0x80 | ((ms >> 7) & 0x3F)), static_cast<uint8_t>(0x80 | (ms & 0x7F)),
               0x90, 60, 100}, out, n);
}

void test_pack_running_status_same_time() {
  const TimedMsg in[] = {msgAt(5, 0x90, 60, 100), msgAt(5, 0x90, 64, 101)};
  uint8_t out[32]; int taken = 0;
  const int n = blePack(in, 2, out, sizeof(out), taken);
  TEST_ASSERT_EQUAL(2, taken);
  assertBytes({0x80, 0x85, 0x90, 60, 100, 0x85, 64, 101}, out, n);
}

void test_pack_new_time_keeps_running_status() {
  const TimedMsg in[] = {msgAt(5, 0x90, 60, 100), msgAt(6, 0x90, 64, 101), msgAt(6, 0xB0, 7, 1)};
  uint8_t out[32]; int taken = 0;
  const int n = blePack(in, 3, out, sizeof(out), taken);
  TEST_ASSERT_EQUAL(3, taken);
  assertBytes({0x80, 0x85, 0x90, 60, 100, 0x86, 64, 101, 0x86, 0xB0, 7, 1}, out, n);
}

void test_pack_clock_between_notes_rewrites_status() {
  const TimedMsg in[] = {msgAt(5, 0x90, 60, 100), msgAt(5, 0xF8), msgAt(5, 0x90, 64, 101)};
  uint8_t out[32]; int taken = 0;
  const int n = blePack(in, 3, out, sizeof(out), taken);
  TEST_ASSERT_EQUAL(3, taken);
  assertBytes({0x80, 0x85, 0x90, 60, 100, 0x85, 0xF8, 0x85, 0x90, 64, 101}, out, n);
}

void test_pack_cap_never_splits() {
  uint8_t out[32]; int taken = 0;
  const TimedMsg diff[] = {msgAt(1, 0x90, 60, 1), msgAt(2, 0xB0, 61, 2), msgAt(3, 0x90, 62, 3)};
  int n = blePack(diff, 3, out, 8, taken);  // 5 + 4 > 8
  TEST_ASSERT_EQUAL(1, taken);
  TEST_ASSERT_EQUAL(5, n);
  const TimedMsg same[] = {msgAt(1, 0x90, 60, 1), msgAt(2, 0x90, 61, 2), msgAt(3, 0x90, 62, 3)};
  n = blePack(same, 3, out, 8, taken);  // 5 + 3 = 8, + 3 > 8
  TEST_ASSERT_EQUAL(2, taken);
  assertBytes({0x80, 0x81, 0x90, 60, 1, 0x82, 61, 2}, out, n);
}

void test_pack_timestamp_wraps_inside_packet() {
  const TimedMsg in[] = {msgAt(8190, 0x90, 60, 1), msgAt(8193, 0x80, 60, 0)};
  uint8_t out[32]; int taken = 0;
  const int n = blePack(in, 2, out, sizeof(out), taken);
  TEST_ASSERT_EQUAL(2, taken);
  assertBytes({0x80 | 0x3F, 0x80 | 0x7E, 0x90, 60, 1, 0x80 | 0x01, 0x80, 60, 0}, out, n);
}

void test_pack_empty_or_small_cap() {
  const TimedMsg in[] = {msgAt(0, 0x90, 60, 1)};
  uint8_t out[32]; int taken = -1;
  TEST_ASSERT_EQUAL(0, blePack(in, 0, out, sizeof(out), taken));
  TEST_ASSERT_EQUAL(0, taken);
  taken = -1;
  TEST_ASSERT_EQUAL(0, blePack(in, 1, out, 2, taken));
  TEST_ASSERT_EQUAL(0, taken);
  taken = -1;
  TEST_ASSERT_EQUAL(0, blePack(in, 1, out, 4, taken));  // note-on needs 5
  TEST_ASSERT_EQUAL(0, taken);
  const TimedMsg clk[] = {msgAt(0, 0xF8)};
  TEST_ASSERT_EQUAL(3, blePack(clk, 1, out, 3, taken));  // clock fits exactly
  TEST_ASSERT_EQUAL(1, taken);
}

void test_pack_bad_messages_skipped() {
  const TimedMsg in[] = {
      msgAt(1, 0x90, 60, 1, 0),     // len 0
      msgAt(1, 0x90, 60, 1, 4),     // len > 3
      msgAt(1, 0x90, 60, 1, 2),     // note-on with one data byte
      msgAt(1, 0xC0, 5, 0, 3),      // program change with two
      msgAt(1, 0xF0, 1, 2),         // SysEx / system common
      msgAt(1, 0xF2, 1, 2, 3),
      msgAt(1, 0x40, 1, 2, 3),      // no status bit
      msgAt(1, 0x90, 0x80, 2, 3),   // data with the high bit
      msgAt(1, 0xF8, 0, 0, 2),      // real-time is one byte
      msgAt(2, 0xB0, 7, 100),
  };
  uint8_t out[32]; int taken = 0;
  const int n = blePack(in, 10, out, sizeof(out), taken);
  TEST_ASSERT_EQUAL(10, taken);
  assertBytes({0x80, 0x82, 0xB0, 7, 100}, out, n);  // header from the first message written

  const TimedMsg bad[] = {msgAt(1, 0x90, 60, 1, 0), msgAt(1, 0xF0), msgAt(1, 0x90, 60, 1, 4)};
  TEST_ASSERT_EQUAL(0, blePack(bad, 3, out, sizeof(out), taken));
  TEST_ASSERT_EQUAL(3, taken);  // all consumed, so the caller moves on

  const TimedMsg badThenBig[] = {msgAt(1, 0x90, 60, 1, 0), msgAt(1, 0x90, 60, 1)};
  TEST_ASSERT_EQUAL(0, blePack(badThenBig, 2, out, 4, taken));  // note-on needs 5
  TEST_ASSERT_EQUAL(1, taken);
}

void test_pack_breaks_on_time_gap_or_reorder() {
  uint8_t out[32]; int taken = 0;
  const TimedMsg gap[] = {msgAt(1000, 0x90, 60, 1), msgAt(1128, 0x90, 61, 1)};
  TEST_ASSERT_EQUAL(5, blePack(gap, 2, out, sizeof(out), taken));
  TEST_ASSERT_EQUAL(1, taken);
  const TimedMsg near[] = {msgAt(1000, 0x90, 60, 1), msgAt(1127, 0x90, 61, 1)};
  blePack(near, 2, out, sizeof(out), taken);
  TEST_ASSERT_EQUAL(2, taken);
  const TimedMsg back[] = {msgAt(1000, 0x90, 60, 1), msgAt(999, 0x90, 61, 1)};
  TEST_ASSERT_EQUAL(5, blePack(back, 2, out, sizeof(out), taken));
  TEST_ASSERT_EQUAL(1, taken);
  // The gap counts from the last written message, not the first.
  const TimedMsg chain[] = {msgAt(0, 0xF8), msgAt(100, 0xF8), msgAt(200, 0xF8)};
  TEST_ASSERT_EQUAL(7, blePack(chain, 3, out, sizeof(out), taken));
  TEST_ASSERT_EQUAL(3, taken);
}

void test_pack_round_trip() {
  const TimedMsg in[] = {msgAt(100, 0x90, 60, 100), msgAt(100, 0x90, 64, 90), msgAt(100, 0xF8),
                         msgAt(100, 0x90, 67, 80), msgAt(101, 0xC3, 9), msgAt(101, 0xC3, 10),
                         msgAt(102, 0xD1, 33), msgAt(103, 0xE0, 0, 64), msgAt(103, 0xFA),
                         msgAt(200, 0x80, 60, 0), msgAt(200, 0xB2, 74, 127)};
  const int total = sizeof(in) / sizeof(in[0]);
  BleMidiParser p; Collected g;
  uint8_t out[16];
  int pos = 0, packets = 0;
  while (pos < total) {
    int taken = 0;
    const int n = blePack(in + pos, total - pos, out, sizeof(out), taken);
    TEST_ASSERT_TRUE(taken > 0);
    TEST_ASSERT_TRUE(n <= static_cast<int>(sizeof(out)));
    p.feed(out, n, sink, &g);
    pos += taken;
    ++packets;
  }
  TEST_ASSERT_TRUE(packets > 1);
  TEST_ASSERT_EQUAL(total, g.m.size());
  for (int i = 0; i < total; ++i)
    assertMsg(g.m[i], in[i].b[0], in[i].len > 1 ? in[i].b[1] : 0, in[i].len > 2 ? in[i].b[2] : 0);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_one_note_on);
  RUN_TEST(test_two_messages_with_timestamps);
  RUN_TEST(test_running_status_without_timestamp);
  RUN_TEST(test_running_status_across_packets);
  RUN_TEST(test_program_change_has_one_data_byte);
  RUN_TEST(test_channel_pressure_has_one_data_byte);
  RUN_TEST(test_data_len_table);
  RUN_TEST(test_sysex_skipped_across_packets);
  RUN_TEST(test_bad_header_dropped);
  RUN_TEST(test_realtime_inside_message_passes_through);
  RUN_TEST(test_realtime_without_running_status);
  RUN_TEST(test_realtime_inside_sysex_passes_through);
  RUN_TEST(test_note_on_velocity_zero_kept);
  RUN_TEST(test_cut_message_dropped_at_packet_end);
  RUN_TEST(test_new_status_drops_partial);
  RUN_TEST(test_sysex_cancels_running_status);
  RUN_TEST(test_system_common_cancels_running_status);
  RUN_TEST(test_reset_clears_state);
  RUN_TEST(test_short_or_null_packet_ignored);
  RUN_TEST(test_pack_one_note_on);
  RUN_TEST(test_pack_running_status_same_time);
  RUN_TEST(test_pack_new_time_keeps_running_status);
  RUN_TEST(test_pack_clock_between_notes_rewrites_status);
  RUN_TEST(test_pack_cap_never_splits);
  RUN_TEST(test_pack_timestamp_wraps_inside_packet);
  RUN_TEST(test_pack_empty_or_small_cap);
  RUN_TEST(test_pack_bad_messages_skipped);
  RUN_TEST(test_pack_breaks_on_time_gap_or_reorder);
  RUN_TEST(test_pack_round_trip);
  return UNITY_END();
}
