#include <unity.h>
#include <string.h>
#include <initializer_list>
#include "smf.h"

using namespace mt;

void setUp() {}
void tearDown() {}

// ---- SMF byte builder ----
struct Buf {
  uint8_t d[4096];
  size_t n = 0;
  void b(uint8_t v) { d[n++] = v; }
  void bytes(std::initializer_list<int> l) {
    for (int v : l) b(static_cast<uint8_t>(v));
  }
  void u16(uint16_t v) { b(v >> 8); b(v & 0xFF); }
  void u32(uint32_t v) { b(v >> 24); b((v >> 16) & 0xFF); b((v >> 8) & 0xFF); b(v & 0xFF); }
  void vlq(uint32_t v) {
    uint8_t tmp[5];
    int k = 0;
    tmp[k++] = v & 0x7F;
    while (v >>= 7) tmp[k++] = 0x80 | (v & 0x7F);
    while (k) b(tmp[--k]);
  }
  void tag(const char* t) { for (int i = 0; i < 4; ++i) b(t[i]); }
};

static Buf F;   // file
static Buf T;   // current track body

static void header(uint16_t fmt, uint16_t ntrk, uint16_t div) {
  F.n = 0;
  F.tag("MThd");
  F.u32(6);
  F.u16(fmt);
  F.u16(ntrk);
  F.u16(div);
}
static void trackBegin() { T.n = 0; }
static void ev(uint32_t delta, std::initializer_list<int> l) { T.vlq(delta); T.bytes(l); }
static void meta(uint32_t delta, uint8_t type, const char* s) {
  T.vlq(delta);
  T.b(0xFF);
  T.b(type);
  T.vlq(strlen(s));
  for (const char* p = s; *p; ++p) T.b(*p);
}
static void trackEnd(bool eot = true) {
  if (eot) ev(0, {0xFF, 0x2F, 0x00});
  F.tag("MTrk");
  F.u32(T.n);
  memcpy(F.d + F.n, T.d, T.n);
  F.n += T.n;
}

static SmfInfo info;
static SmfNote notes[64];

void test_vlq_running_status_format0() {
  header(0, 1, 96);
  trackBegin();
  ev(0, {0x90, 60, 100});
  ev(96, {62, 90});        // running status: note on 62
  ev(0, {60, 0});          // vel 0 = off for 60 (running)
  ev(200, {62, 0});        // 62 off at 296 (delta needs 2-byte VLQ)
  ev(0, {0x80, 64, 0});    // stray off: ignored
  trackEnd();
  TEST_ASSERT_EQUAL(SmfErr::Ok, parseSmf(F.d, F.n, info, notes, 64));
  TEST_ASSERT_EQUAL(96, info.ppq);
  TEST_ASSERT_EQUAL_UINT32(500000, info.firstTempoUsPerQ);
  TEST_ASSERT_EQUAL_UINT32(2, info.noteCount);
  TEST_ASSERT_EQUAL(1, info.sourceCount);
  TEST_ASSERT_EQUAL(0, info.src[0].track);
  TEST_ASSERT_EQUAL(0, info.src[0].channel);
  TEST_ASSERT_EQUAL(2, info.src[0].count);
  TEST_ASSERT_EQUAL(60, info.src[0].lo);
  TEST_ASSERT_EQUAL(62, info.src[0].hi);
  TEST_ASSERT_EQUAL_UINT32(0, notes[0].tick);
  TEST_ASSERT_EQUAL_UINT32(96, notes[0].len);
  TEST_ASSERT_EQUAL(60, notes[0].note);
  TEST_ASSERT_EQUAL(100, notes[0].vel);
  TEST_ASSERT_EQUAL_UINT32(96, notes[1].tick);
  TEST_ASSERT_EQUAL_UINT32(200, notes[1].len);
  TEST_ASSERT_EQUAL(62, notes[1].note);
  TEST_ASSERT_EQUAL_UINT32(296, info.lastTick);
}

void test_format1_two_tracks_names_sorted() {
  header(1, 3, 480);
  trackBegin();  // conductor
  meta(0, 0x03, "Song");
  ev(0, {0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20});  // 500000
  ev(960, {0xFF, 0x51, 0x03, 0x03, 0xD0, 0x90});  // later tempo, ignored
  trackEnd();
  trackBegin();
  meta(0, 0x03, "Bass");
  ev(480, {0x91, 36, 100});
  ev(240, {0x81, 36, 0});
  ev(0, {0x91, 38, 100});
  ev(240, {0x81, 38, 0});
  trackEnd();
  trackBegin();
  meta(0, 0x03, "A very long track name here");
  ev(0, {0x99, 42, 80});
  ev(120, {0x89, 42, 0});
  ev(480, {0x99, 42, 80});  // tick 600
  ev(120, {0x89, 42, 0});
  trackEnd();
  TEST_ASSERT_EQUAL(SmfErr::Ok, parseSmf(F.d, F.n, info, notes, 64));
  TEST_ASSERT_EQUAL(480, info.ppq);
  TEST_ASSERT_EQUAL_UINT32(500000, info.firstTempoUsPerQ);
  TEST_ASSERT_EQUAL(2, info.sourceCount);
  TEST_ASSERT_EQUAL(1, info.src[0].track);
  TEST_ASSERT_EQUAL(1, info.src[0].channel);
  TEST_ASSERT_EQUAL_STRING("Bass", info.src[0].name);
  TEST_ASSERT_EQUAL(2, info.src[1].track);
  TEST_ASSERT_EQUAL(9, info.src[1].channel);
  TEST_ASSERT_EQUAL_STRING("A very long trac", info.src[1].name);
  TEST_ASSERT_EQUAL_UINT32(4, info.noteCount);
  // Sorted by tick: 0 (hat), 480 (bass), 600 (hat), 720 (bass)
  const uint32_t ticks[] = {0, 480, 600, 720};
  const uint8_t srcs[] = {1, 0, 1, 0};
  for (int i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL_UINT32(ticks[i], notes[i].tick);
    TEST_ASSERT_EQUAL(srcs[i], notes[i].src);
  }
}

void test_tempo_read() {
  header(0, 1, 96);
  trackBegin();
  ev(0, {0xFF, 0x51, 0x03, 0x09, 0x27, 0xC0});  // 600000 us = 100 bpm
  ev(0, {0x90, 60, 100});
  ev(10, {0x80, 60, 0});
  trackEnd();
  TEST_ASSERT_EQUAL(SmfErr::Ok, parseSmf(F.d, F.n, info, notes, 64));
  TEST_ASSERT_EQUAL_UINT32(600000, info.firstTempoUsPerQ);
}

void test_overlap_fifo_and_unclosed() {
  header(0, 1, 96);
  trackBegin();
  ev(0, {0x90, 60, 100});
  ev(10, {0x90, 60, 90});   // second 60 at 10
  ev(10, {0x80, 60, 0});    // closes the first (len 20)
  ev(10, {0x80, 60, 0});    // closes the second (len 20)
  ev(0, {0x90, 64, 70});    // never closed
  ev(50, {0xFF, 0x2F, 0x00});  // end at 80
  trackEnd(false);
  TEST_ASSERT_EQUAL(SmfErr::Ok, parseSmf(F.d, F.n, info, notes, 64));
  TEST_ASSERT_EQUAL_UINT32(3, info.noteCount);
  TEST_ASSERT_EQUAL_UINT32(0, notes[0].tick);
  TEST_ASSERT_EQUAL_UINT32(20, notes[0].len);
  TEST_ASSERT_EQUAL(100, notes[0].vel);
  TEST_ASSERT_EQUAL_UINT32(10, notes[1].tick);
  TEST_ASSERT_EQUAL_UINT32(20, notes[1].len);
  TEST_ASSERT_EQUAL(64, notes[2].note);
  TEST_ASSERT_EQUAL_UINT32(30, notes[2].tick);
  TEST_ASSERT_EQUAL_UINT32(50, notes[2].len);
}

void test_sysex_and_meta_skipped_cancel_running_status() {
  header(0, 1, 96);
  trackBegin();
  ev(0, {0xF0, 0x03, 0x43, 0x12, 0xF7});     // sysex
  ev(0, {0xB0, 7, 100});                      // CC
  ev(0, {0xC0, 5});                           // program (1 data byte)
  ev(0, {0xE0, 0, 64});                       // pitch bend
  ev(0, {0x90, 60, 100});
  ev(5, {0xF7, 0x02, 0x01, 0x02});            // escape sysex
  ev(5, {0x80, 60, 0});
  meta(0, 0x01, "text");
  ev(0, {0x90, 61, 100});
  ev(5, {0x80, 61, 0});
  trackEnd();
  TEST_ASSERT_EQUAL(SmfErr::Ok, parseSmf(F.d, F.n, info, notes, 64));
  TEST_ASSERT_EQUAL_UINT32(2, info.noteCount);
  TEST_ASSERT_EQUAL_UINT32(10, notes[0].len);
  TEST_ASSERT_EQUAL(61, notes[1].note);
}

void test_running_status_survives_meta_and_sysex() {
  header(0, 1, 96);
  trackBegin();
  ev(0, {0x90, 60, 100});
  meta(0, 0x01, "x");
  ev(5, {60, 0});  // running status continues after meta
  ev(0, {62, 100});
  ev(0, {0xF0, 0x01, 0xF7});  // sysex
  ev(7, {62, 0});  // still running
  trackEnd();
  TEST_ASSERT_EQUAL(SmfErr::Ok, parseSmf(F.d, F.n, info, notes, 64));
  TEST_ASSERT_EQUAL_UINT32(2, info.noteCount);
  TEST_ASSERT_EQUAL_UINT32(5, notes[0].len);
  TEST_ASSERT_EQUAL(62, notes[1].note);
  TEST_ASSERT_EQUAL_UINT32(7, notes[1].len);
}

void test_realtime_bytes_skipped() {
  header(0, 1, 96);
  trackBegin();
  ev(0, {0x90, 60, 100});
  ev(3, {0xF8});         // clock: single byte, running status kept
  ev(2, {0xFE});         // active sensing
  ev(1, {60, 0});
  trackEnd();
  TEST_ASSERT_EQUAL(SmfErr::Ok, parseSmf(F.d, F.n, info, notes, 64));
  TEST_ASSERT_EQUAL_UINT32(1, info.noteCount);
  TEST_ASSERT_EQUAL_UINT32(6, notes[0].len);
}

void test_bad_track_without_notes_is_error() {
  header(0, 1, 96);
  trackBegin();
  ev(0, {60, 0});  // data byte, no running status
  trackEnd();
  TEST_ASSERT_EQUAL(SmfErr::NotMidi, parseSmf(F.d, F.n, info, notes, 64));
}

void test_bad_track_keeps_parsed_notes() {
  header(1, 3, 96);
  trackBegin();
  ev(0, {0x90, 60, 100});
  ev(10, {0x80, 60, 0});
  trackEnd();
  trackBegin();
  ev(0, {0x91, 62, 100});
  ev(4, {0x81, 62, 0});
  ev(0, {0xF1, 0x00});  // unsupported system common: bad from here
  ev(0, {0x91, 63, 100});
  trackEnd();
  trackBegin();  // not parsed
  ev(0, {0x92, 64, 100});
  ev(5, {0x82, 64, 0});
  trackEnd();
  TEST_ASSERT_EQUAL(SmfErr::Truncated, parseSmf(F.d, F.n, info, notes, 64));
  TEST_ASSERT_EQUAL_UINT32(2, info.noteCount);
  TEST_ASSERT_EQUAL(60, notes[0].note);
  TEST_ASSERT_EQUAL(62, notes[1].note);
  TEST_ASSERT_EQUAL(2, info.sourceCount);
  TEST_ASSERT_EQUAL_UINT32(10, info.lastTick);
}

void test_tick_overflow_is_bad_track() {
  header(0, 1, 96);
  trackBegin();
  ev(0, {0x90, 60, 100});
  ev(16, {0x80, 60, 0});
  for (int i = 0; i < 17; ++i) ev(0x0FFFFFFF, {0xB0, 7, 100});  // 17 * (2^28 - 1) > 2^32
  ev(0, {0x90, 61, 100});
  trackEnd();
  TEST_ASSERT_EQUAL(SmfErr::Truncated, parseSmf(F.d, F.n, info, notes, 64));
  TEST_ASSERT_EQUAL_UINT32(1, info.noteCount);
  TEST_ASSERT_EQUAL_UINT32(16, notes[0].len);
  TEST_ASSERT_EQUAL_UINT32(16u + 15u * 0x0FFFFFFFu, info.lastTick);  // last tick before the wrap

  header(0, 1, 96);
  trackBegin();
  for (int i = 0; i < 17; ++i) ev(0x0FFFFFFF, {0xB0, 7, 100});
  trackEnd();
  TEST_ASSERT_EQUAL(SmfErr::NotMidi, parseSmf(F.d, F.n, info, notes, 64));
}

void test_smpte_error() {
  header(0, 1, 0xE728);  // -25 fps, 40 ticks/frame
  trackBegin();
  trackEnd();
  TEST_ASSERT_EQUAL(SmfErr::Smpte, parseSmf(F.d, F.n, info, notes, 64));
}

void test_format2_unsupported() {
  header(2, 1, 96);
  trackBegin();
  trackEnd();
  TEST_ASSERT_EQUAL(SmfErr::Unsupported, parseSmf(F.d, F.n, info, notes, 64));
}

void test_not_midi() {
  const uint8_t junk[] = "RIFF....WAVEfmt ";
  TEST_ASSERT_EQUAL(SmfErr::NotMidi, parseSmf(junk, sizeof(junk), info, notes, 64));
}

void test_truncated() {
  header(0, 1, 96);
  trackBegin();
  ev(0, {0x90, 60, 100});
  ev(10, {0x80, 60, 0});
  trackEnd();
  TEST_ASSERT_EQUAL(SmfErr::Truncated, parseSmf(F.d, F.n - 5, info, notes, 64));
  TEST_ASSERT_EQUAL(SmfErr::Truncated, parseSmf(F.d, 10, info, notes, 64));
}

void test_too_many_notes() {
  header(0, 1, 96);
  trackBegin();
  for (int i = 0; i < 5; ++i) {
    ev(0, {0x90, 60 + i, 100});
    ev(10, {0x80, 60 + i, 0});
  }
  trackEnd();
  TEST_ASSERT_EQUAL(SmfErr::TooManyNotes, parseSmf(F.d, F.n, info, notes, 3));
  TEST_ASSERT_EQUAL_UINT32(3, info.noteCount);
  TEST_ASSERT_EQUAL_UINT32(10, notes[2].len);
  TEST_ASSERT_EQUAL(62, notes[2].note);
}

void test_source_limit_32() {
  header(1, 3, 96);
  for (int t = 0; t < 3; ++t) {
    trackBegin();
    for (int c = 0; c < 16; ++c) {
      ev(0, {0x90 | c, 60, 100});
      ev(1, {0x80 | c, 60, 0});
    }
    trackEnd();
  }
  TEST_ASSERT_EQUAL(SmfErr::Ok, parseSmf(F.d, F.n, info, notes, 64));
  TEST_ASSERT_EQUAL(32, info.sourceCount);
  TEST_ASSERT_EQUAL_UINT32(32, info.noteCount);
  TEST_ASSERT_EQUAL(1, info.src[31].track);
  TEST_ASSERT_EQUAL(15, info.src[31].channel);
}

static SmfNote big[400];

void test_merge_many_tracks_sorted_stable() {
  header(1, 8, 96);
  uint32_t seed = 7;
  for (int t = 0; t < 8; ++t) {
    trackBegin();
    for (int i = 0; i < 40; ++i) {
      seed = seed * 1103515245u + 12345u;
      const uint32_t d = (seed >> 16) % 3 == 0 ? 0 : (seed >> 16) % 7;  // many ties
      ev(d, {0x90, 40 + t, 100});
      ev(1, {0x80, 40 + t, 0});
    }
    trackEnd();
  }
  TEST_ASSERT_EQUAL(SmfErr::Ok, parseSmf(F.d, F.n, info, big, 400));
  TEST_ASSERT_EQUAL_UINT32(320, info.noteCount);
  int perTrack[8] = {0};
  for (uint32_t i = 0; i < info.noteCount; ++i) {
    perTrack[big[i].src]++;
    if (i == 0) continue;
    TEST_ASSERT_TRUE(big[i - 1].tick <= big[i].tick);
    if (big[i - 1].tick == big[i].tick) TEST_ASSERT_TRUE(big[i - 1].src <= big[i].src);
  }
  for (int t = 0; t < 8; ++t) TEST_ASSERT_EQUAL(40, perTrack[t]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_vlq_running_status_format0);
  RUN_TEST(test_format1_two_tracks_names_sorted);
  RUN_TEST(test_tempo_read);
  RUN_TEST(test_overlap_fifo_and_unclosed);
  RUN_TEST(test_sysex_and_meta_skipped_cancel_running_status);
  RUN_TEST(test_running_status_survives_meta_and_sysex);
  RUN_TEST(test_realtime_bytes_skipped);
  RUN_TEST(test_bad_track_without_notes_is_error);
  RUN_TEST(test_bad_track_keeps_parsed_notes);
  RUN_TEST(test_tick_overflow_is_bad_track);
  RUN_TEST(test_smpte_error);
  RUN_TEST(test_format2_unsupported);
  RUN_TEST(test_not_midi);
  RUN_TEST(test_truncated);
  RUN_TEST(test_too_many_notes);
  RUN_TEST(test_source_limit_32);
  RUN_TEST(test_merge_many_tracks_sorted_stable);
  return UNITY_END();
}
