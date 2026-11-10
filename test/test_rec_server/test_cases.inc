// Host-only integration: compile through scripts/test_rec_server_native.py. The real server and
// portable capture/writer run here; SD, codec, link transport and interrupt exclusion are fakes.
#include <unity.h>
#include <stdarg.h>
#include <new>
#include <vector>
#include "sample_set.h"
#include "../../src/teensy/rec_server.cpp"

namespace rec_test {
uint32_t nowMs = 1;
int audioGuards = 0;
bool failWrite = false, failSeal = false;
unsigned dataWrites = 0, importStarts = 0;
bool healthy = true;
bool codecPresent = false;
int micGain = -1, lineGain = -1, gainWrites = 0;
std::vector<int16_t> memory(8192 * 2);
std::vector<uint8_t> reply;
}
RecTestSd SD;
void SynthStream::update() {}
namespace codec {
bool present() { return rec_test::codecPresent; }
void input(Input) {}
void micGain(int g) { TEST_ASSERT_EQUAL_INT(0, rec_test::audioGuards); rec_test::micGain = g; ++rec_test::gainWrites; }
void lineLevel(int g) { TEST_ASSERT_EQUAL_INT(0, rec_test::audioGuards); rec_test::lineGain = g; ++rec_test::gainWrites; }
}
namespace bank {
mt::BankResult writable() { return mt::BankResult::Ok; }
mt::BankResult importFile(const char*) { ++rec_test::importStarts; return mt::BankResult::Running; }
mt::BankResult importPoll(mt::ImportOut&, uint32_t&, uint32_t&) { return mt::BankResult::Running; }
}
namespace card { void spaceChanged() {} }
namespace render { bool active() { return false; } }
namespace psram {
int16_t* recRing() { return rec_test::healthy ? rec_test::memory.data() : nullptr; }
uint32_t recRingFrames() { return rec_test::healthy ? 8192 : 0; }
}
namespace link {
void log(const char*, ...) {}
void reply(mt::link::Msg, uint8_t, const uint8_t* p, int n) { rec_test::reply.assign(p, p + n); }
}
using namespace mt;
using namespace mt::link;
static SynthStream output;
static uint16_t rd16le(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
static uint32_t rd32le(const uint8_t* p) { return rd16le(p) | (uint32_t(rd16le(p + 2)) << 16); }

void setUp() {
  rec_test::nowMs = 1;
  rec_test::audioGuards = 0;
  rec_test::healthy = true;
  rec_test::codecPresent = false;
  rec_test::micGain = rec_test::lineGain = -1;
  rec_test::gainWrites = 0;
  rec_test::memory.resize(8192 * 2);
  rec_test::failWrite = rec_test::failSeal = false;
  rec_test::dataWrites = rec_test::importStarts = 0;
  rec_test::reply.clear();
  SD.sdfs.nodes.clear();
  rec::file = FsFile{};
  rec::cap.~RecCapture();
  new (&rec::cap) RecCapture;
  rec::phase = rec::Phase::None;
  rec::ringOk = rec::watching = rec::stopPending = rec::haveStopRep = false;
  rec::memoryLost = false;
  rec::begin(output);
}
void tearDown() { TEST_ASSERT_EQUAL_INT(0, rec_test::audioGuards); }

static void beginTake(bool toSd = true, int8_t thresh = kRecThreshOff) {
  uint8_t p[kMaxPayload] = {}, o[kMaxPayload];
  RecStartReq q;
  q.source = uint8_t(RecSource::Resample);
  q.thresh = thresh;
  q.number = 9900;
  q.toSd = toSd ? 1 : 0;
  Writer w(p, sizeof p);
  encode(q, w);
  const int n = rec::handle(Msg::RecStart, 11, p, w.size(), o);
  RecStartRep rep;
  Reader r(o, n);
  TEST_ASSERT_TRUE(decode(r, rep));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RecResult::Ok), rep.result);
}
static void feed(uint32_t frames) {
  std::vector<int16_t> l(frames, 8000), r(frames, -2000);
  rec::cap.feed(l.data(), r.data(), frames);
}
static void failMemory() {
  // The integration order required from psram.cpp: detach capture first, then publish failure.
  rec::psramFailed();
  rec_test::healthy = false;
}
static RecStopRep stop() {
  uint8_t o[kMaxPayload];
  TEST_ASSERT_EQUAL_INT(-1, rec::handle(Msg::RecStop, 12, nullptr, 0, o));
  for (int i = 0; i < 5 && rec_test::reply.empty(); ++i) rec::step();
  RecStopRep rep;
  Reader r(rec_test::reply.data(), static_cast<int>(rec_test::reply.size()));
  TEST_ASSERT_TRUE(decode(r, rep));
  return rep;
}
static void assertPrefix(uint32_t frames) {
  auto found = SD.sdfs.nodes.find("/samples/rec/REC9900.wav");
  TEST_ASSERT_TRUE(found != SD.sdfs.nodes.end());
  const auto& b = found->second->bytes;
  TEST_ASSERT_EQUAL_UINT32(kWavHeaderBytes + frames * 4, b.size());
  TEST_ASSERT_EQUAL_UINT32(b.size() - 8, rd32le(b.data() + 4));
  TEST_ASSERT_EQUAL_UINT32(frames * 4, rd32le(b.data() + kWavHeaderBytes - 4));
  TEST_ASSERT_EQUAL_UINT32(frames, rd32le(b.data() + kWavHeaderBytes - 12));
  const std::vector<int16_t> mono(frames, 3000);  // (8000 + -2000) / 2
  TEST_ASSERT_EQUAL_UINT32(sampleCrc(mono.data(), frames), rd32le(b.data() + kWavHeaderBytes - 16));
  for (uint32_t i = 0; i < frames; ++i) {
    TEST_ASSERT_EQUAL_INT16(8000, int16_t(rd16le(b.data() + kWavHeaderBytes + i * 4)));
    TEST_ASSERT_EQUAL_INT16(-2000, int16_t(rd16le(b.data() + kWavHeaderBytes + i * 4 + 2)));
  }
}
static void test_live_fault_seals_only_written_sd_prefix() {
  beginTake();
  feed(4096);
  rec::step();
  feed(1024);  // pending in failed PSRAM, never safe to append after notification
  failMemory();
  const unsigned writes = rec_test::dataWrites;
  rec::step();
  const RecStopRep rep = stop();
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RecResult::NoPsram), rep.result);
  TEST_ASSERT_EQUAL_UINT32(4096, rep.frames);
  TEST_ASSERT_EQUAL_UINT(writes, rec_test::dataWrites);
  assertPrefix(4096);
  TEST_ASSERT_EQUAL_UINT(0, rec_test::importStarts);
}
static void test_live_fault_suppresses_bank_import() {
  beginTake(false);
  feed(4096);
  rec::step();
  feed(256);
  failMemory();
  const RecStopRep rep = stop();
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RecResult::NoPsram), rep.result);
  TEST_ASSERT_EQUAL_UINT32(4096, rep.frames);
  TEST_ASSERT_EQUAL_UINT(0, rec_test::importStarts);
  assertPrefix(4096);
}
static void test_fault_before_stop_reports_level_and_disables_future_start() {
  beginTake();
  feed(4096);
  rec::step();
  failMemory();
  uint8_t o[kMaxPayload];
  int n = rec::handle(Msg::RecLevel, 13, nullptr, 0, o);
  RecLevelRep level;
  Reader r(o, n);
  TEST_ASSERT_TRUE(decode(r, level));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RecResult::NoPsram), level.result);
  stop();
  uint8_t p[kMaxPayload];
  RecStartReq q;
  q.source = uint8_t(RecSource::Resample);
  q.thresh = kRecThreshOff;
  Writer w(p, sizeof p);
  encode(q, w);
  n = rec::handle(Msg::RecStart, 14, p, w.size(), o);
  RecStartRep rep;
  Reader rr(o, n);
  TEST_ASSERT_TRUE(decode(rr, rep));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RecResult::NoPsram), rep.result);
}

static void test_fault_after_stop_before_sealing_discards_remaining_ring() {
  beginTake();
  feed(4096);
  rec::step();
  feed(256);
  uint8_t o[kMaxPayload];
  TEST_ASSERT_EQUAL_INT(-1, rec::handle(Msg::RecStop, 12, nullptr, 0, o));
  failMemory();
  rec::step();
  RecStopRep rep;
  Reader r(rec_test::reply.data(), static_cast<int>(rec_test::reply.size()));
  TEST_ASSERT_TRUE(decode(r, rep));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RecResult::NoPsram), rep.result);
  TEST_ASSERT_EQUAL_UINT32(4096, rep.frames);
  assertPrefix(4096);
}

static void test_fault_cannot_feed_or_drain_freed_psram() {
  beginTake();
  feed(4096);
  rec::step();
  feed(1024);
  failMemory();
  rec_test::memory.clear();
  rec_test::memory.shrink_to_fit();
  // Even a stale caller or a re-arm cannot access the freed allocation under ASan.
  feed(128);
  rec::cap.arm(RecSource::Resample, kRecThreshOff, kRecMaxFrames);
  feed(128);
  rec::step();
  const RecStopRep rep = stop();
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RecResult::NoPsram), rep.result);
  TEST_ASSERT_EQUAL_UINT32(4096, rep.frames);
  assertPrefix(4096);
}

static void test_fault_keepalive_seals_prefix_without_bank_import() {
  beginTake(false);
  feed(4096);
  rec::step();
  feed(1024);
  failMemory();
  rec_test::nowMs += rec::kKeepaliveMs + 1;
  rec::step();
  TEST_ASSERT_FALSE(rec::busy());
  TEST_ASSERT_EQUAL_UINT(0, rec_test::importStarts);
  assertPrefix(4096);
}

static void test_fault_hello_reset_seals_prefix_without_ring_access() {
  beginTake();
  feed(4096);
  rec::step();
  feed(1024);
  failMemory();
  rec::reset();
  TEST_ASSERT_FALSE(rec::busy());
  TEST_ASSERT_TRUE(rec_test::reply.empty());
  assertPrefix(4096);
}

static void test_fault_retains_short_and_empty_prefix_as_no_psram() {
  for (uint32_t frames : {0u, 128u}) {
    setUp();
    beginTake();
    feed(frames);
    rec::step();
    failMemory();
    const RecStopRep rep = stop();
    TEST_ASSERT_EQUAL_UINT8(uint8_t(RecResult::NoPsram), rep.result);
    TEST_ASSERT_EQUAL_UINT32(frames, rep.frames);
    assertPrefix(frames);
  }
}

static void test_fault_does_not_hide_sd_seal_failure() {
  beginTake();
  feed(4096);
  rec::step();
  failMemory();
  rec_test::failSeal = true;
  const RecStopRep rep = stop();
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RecResult::WriteFail), rep.result);
  TEST_ASSERT_FALSE(SD.sdfs.exists("/samples/rec/REC9900.wav"));
  TEST_ASSERT_TRUE(SD.sdfs.exists("/samples/rec/REC9900.wav.tmp"));
}

static void test_fault_does_not_hide_prior_sd_write_failure() {
  beginTake();
  feed(4096);
  rec_test::failWrite = true;
  rec::step();
  failMemory();
  uint8_t o[kMaxPayload];
  const int n = rec::handle(Msg::RecLevel, 13, nullptr, 0, o);
  RecLevelRep level;
  Reader r(o, n);
  TEST_ASSERT_TRUE(decode(r, level));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RecResult::WriteFail), level.result);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RecResult::WriteFail), stop().result);
}

static void test_fault_during_import_does_not_invalidate_sealed_file() {
  beginTake(false);
  feed(4096);
  rec::step();
  uint8_t o[kMaxPayload];
  TEST_ASSERT_EQUAL_INT(-1, rec::handle(Msg::RecStop, 12, nullptr, 0, o));
  rec::step();
  TEST_ASSERT_EQUAL_UINT(1, rec_test::importStarts);
  failMemory();
  TEST_ASSERT_FALSE(rec::memoryLost);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RecResult::Ok), rec::stopRes.result);
  TEST_ASSERT_TRUE(rec::busy());
  assertPrefix(4096);
}

static void test_healthy_resample_recording_still_seals_and_retries_stop() {
  beginTake();
  feed(4096);
  rec::step();
  const RecStopRep rep = stop();
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RecResult::Ok), rep.result);
  TEST_ASSERT_EQUAL_UINT32(4096, rep.frames);
  assertPrefix(4096);
  uint8_t o[kMaxPayload];
  const int n = rec::handle(Msg::RecStop, 12, nullptr, 0, o);
  TEST_ASSERT_EQUAL_UINT(rec_test::reply.size(), n);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(rec_test::reply.data(), o, n);
}

static void test_armed_capture_keeps_only_preroll_until_trigger() {
  beginTake(true, 0);
  for (int i = 0; i < 12; ++i) {
    feed(128);  // below the full-scale threshold, long enough to replace old pre-roll
    rec::step();
    TEST_ASSERT_EQUAL(RecState::Armed, rec::cap.state());
    TEST_ASSERT_EQUAL_UINT32(0, rec::writer.frames());
  }
  TEST_ASSERT_EQUAL_UINT32(kRecPreRollFrames, rec::cap.ring().size());
  const std::vector<int16_t> left(128, 32767), right(128, -2000);
  rec::cap.feed(left.data(), right.data(), 128);
  rec::step();
  feed(2048);  // enough post-trigger audio for an ordinary, non-short take
  rec::step();
  const RecStopRep rep = stop();
  const uint32_t frames = kRecPreRollFrames + 128 + 2048;
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RecResult::Ok), rep.result);
  TEST_ASSERT_EQUAL_UINT32(frames, rep.frames);
  const auto& b = SD.sdfs.nodes.at("/samples/rec/REC9900.wav")->bytes;
  TEST_ASSERT_EQUAL_UINT32(kWavHeaderBytes + frames * 4, b.size());
  std::vector<int16_t> mono(frames);
  for (uint32_t i = 0; i < frames; ++i) {
    const bool trigger = i >= kRecPreRollFrames && i < kRecPreRollFrames + 128;
    const int16_t l = trigger ? 32767 : 8000;
    TEST_ASSERT_EQUAL_INT16(l, int16_t(rd16le(b.data() + kWavHeaderBytes + i * 4)));
    TEST_ASSERT_EQUAL_INT16(-2000, int16_t(rd16le(b.data() + kWavHeaderBytes + i * 4 + 2)));
    mono[i] = (int32_t(l) - 2000) / 2;
  }
  TEST_ASSERT_EQUAL_UINT32(sampleCrc(mono.data(), frames), rd32le(b.data() + kWavHeaderBytes - 16));
}
static RecLevelRep setInput(RecSource src, int gain, bool automatic, bool mon = false) {
  RecSetReq q;
  q.source = uint8_t(src); q.gain = gain; q.autoGain = automatic; q.mon = mon;
  uint8_t p[kMaxPayload], o[kMaxPayload];
  Writer w(p, sizeof p); encode(q, w);
  const int n = rec::handle(Msg::RecSet, 12, p, w.size(), o);
  Reader r(o, n); RecLevelRep rep;
  TEST_ASSERT_TRUE(decode(r, rep));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RecResult::Ok), rep.result);
  return rep;
}
static void inputWindow(int16_t left, int16_t right) {
  rec::cap.feed(&left, &right, 1);
  rec_test::nowMs += RecAutoGain::kWindowMs;
  rec::step();
}
static void test_auto_gain_survives_start_monitor_and_background_take() {
  rec_test::codecPresent = true;
  setInput(RecSource::Mic, 32, true);
  inputWindow(32767, 1);
  TEST_ASSERT_EQUAL_INT(26, rec_test::micGain);
  auto rep = setInput(RecSource::Mic, 32, true, true);
  TEST_ASSERT_EQUAL_INT(26, rep.gain);  // MON must not restore the manual gain
  TEST_ASSERT_EQUAL_INT(26, rec_test::micGain);
  RecStartReq q;
  q.source = uint8_t(RecSource::Mic); q.gain = 32; q.autoGain = 1; q.toSd = 1;
  uint8_t p[kMaxPayload], o[kMaxPayload];
  Writer w(p, sizeof p); encode(q, w);
  const int n = rec::handle(Msg::RecStart, 13, p, w.size(), o);
  Reader r(o, n); RecStartRep start;
  TEST_ASSERT_TRUE(decode(r, start));
  TEST_ASSERT_EQUAL_UINT8(0, start.result);
  TEST_ASSERT_EQUAL_INT(26, rec_test::micGain);
  // No UI meter polls: control still runs and clips still reach the take's report.
  inputWindow(-32768, 1);
  TEST_ASSERT_EQUAL_INT(20, rec_test::micGain);
  const int len = rec::handle(Msg::RecLevel, 14, nullptr, 0, o);
  Reader rl(o, len);
  TEST_ASSERT_TRUE(decode(rl, rep));
  TEST_ASSERT_EQUAL_INT(20, rep.gain);
  TEST_ASSERT_EQUAL_UINT32(1, rep.clips);
  TEST_ASSERT_EQUAL_UINT16(32767, rep.peak);
}
static void test_auto_gain_line_switch_off_and_resample() {
  rec_test::codecPresent = true;
  setInput(RecSource::Line, 9, true);
  inputWindow(100, -32768);  // right channel also protects the linked pair
  TEST_ASSERT_EQUAL_INT(5, rec_test::lineGain);
  setInput(RecSource::Line, 9, false);
  inputWindow(32767, 32767);
  TEST_ASSERT_EQUAL_INT(9, rec_test::lineGain);
  setInput(RecSource::Mic, 32, true);
  inputWindow(100, -32768);  // MIC ignores the unused right channel
  TEST_ASSERT_EQUAL_INT(32, rec_test::micGain);
  const int writes = rec_test::gainWrites;
  setInput(RecSource::Resample, 63, true);
  inputWindow(32767, 32767);
  TEST_ASSERT_EQUAL_INT(writes, rec_test::gainWrites);
}
static void test_auto_gain_stops_after_close_and_source_discards_old_peak() {
  rec_test::codecPresent = true;
  setInput(RecSource::Mic, 32, true);
  const int16_t loud = 32767;
  rec::cap.feed(&loud, &loud, 1);
  setInput(RecSource::Line, 5, true);
  inputWindow(1000, 1000);
  TEST_ASSERT_EQUAL_INT(5, rec_test::lineGain);
  uint8_t o[kMaxPayload];
  rec::handle(Msg::RecCancel, 15, nullptr, 0, o);
  const int writes = rec_test::gainWrites;
  inputWindow(32767, 32767);
  TEST_ASSERT_EQUAL_INT(writes, rec_test::gainWrites);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_auto_gain_survives_start_monitor_and_background_take);
  RUN_TEST(test_auto_gain_line_switch_off_and_resample);
  RUN_TEST(test_auto_gain_stops_after_close_and_source_discards_old_peak);
  RUN_TEST(test_live_fault_seals_only_written_sd_prefix);
  RUN_TEST(test_live_fault_suppresses_bank_import);
  RUN_TEST(test_fault_before_stop_reports_level_and_disables_future_start);
  RUN_TEST(test_fault_after_stop_before_sealing_discards_remaining_ring);
  RUN_TEST(test_fault_cannot_feed_or_drain_freed_psram);
  RUN_TEST(test_fault_keepalive_seals_prefix_without_bank_import);
  RUN_TEST(test_fault_hello_reset_seals_prefix_without_ring_access);
  RUN_TEST(test_fault_retains_short_and_empty_prefix_as_no_psram);
  RUN_TEST(test_fault_does_not_hide_sd_seal_failure);
  RUN_TEST(test_fault_does_not_hide_prior_sd_write_failure);
  RUN_TEST(test_fault_during_import_does_not_invalidate_sealed_file);
  RUN_TEST(test_healthy_resample_recording_still_seals_and_retries_stop);
  RUN_TEST(test_armed_capture_keeps_only_preroll_until_trigger);
  return UNITY_END();
}
