#include <stdio.h>
#include <string.h>
#include <unity.h>
#include <vector>
#include "rec.h"
#include "sample_set.h"
#include "slices.h"
#include "wav.h"

using namespace mt;

void setUp() {}
void tearDown() {}

// Stereo test signal: frame i is (i * 3 + seed, -(i * 5) + seed), wrapping in int16.
static void signal(std::vector<int16_t>& l, std::vector<int16_t>& r, uint32_t from, uint32_t n, int seed = 0) {
  l.resize(n);
  r.resize(n);
  for (uint32_t i = 0; i < n; ++i) {
    l[i] = static_cast<int16_t>((from + i) * 3 + seed);
    r[i] = static_cast<int16_t>(-static_cast<int32_t>((from + i) * 5) + seed);
  }
}

static void checkPopped(const std::vector<int16_t>& lr, uint32_t from, uint32_t n, int seed = 0) {
  std::vector<int16_t> l, r;
  signal(l, r, from, n, seed);
  for (uint32_t i = 0; i < n; ++i) {
    TEST_ASSERT_EQUAL_INT16(l[i], lr[i * 2]);
    TEST_ASSERT_EQUAL_INT16(r[i], lr[i * 2 + 1]);
  }
}

// --- RecRing ---

static void test_ring_push_pop() {
  std::vector<int16_t> mem(1024 * 2);
  RecRing ring;
  ring.init(mem.data(), 1024);
  std::vector<int16_t> l, r;
  signal(l, r, 0, 300);
  TEST_ASSERT_TRUE(ring.push(l.data(), r.data(), 300));
  TEST_ASSERT_EQUAL_UINT32(300, ring.size());
  std::vector<int16_t> out(300 * 2);
  TEST_ASSERT_EQUAL_UINT32(128, ring.pop(out.data(), 128));
  checkPopped(out, 0, 128);
  TEST_ASSERT_EQUAL_UINT32(172, ring.pop(out.data(), 1000));
  checkPopped(out, 128, 172);
  TEST_ASSERT_EQUAL_UINT32(0, ring.size());
  TEST_ASSERT_EQUAL_UINT32(0, ring.pop(out.data(), 10));
}

static void test_ring_wrap() {
  std::vector<int16_t> mem(1024 * 2);
  RecRing ring;
  ring.init(mem.data(), 1024);
  std::vector<int16_t> l, r, out(700 * 2);
  for (int c = 0; c < 10; ++c) {
    signal(l, r, 0, 700, c * 11);
    TEST_ASSERT_TRUE(ring.push(l.data(), r.data(), 700));
    TEST_ASSERT_EQUAL_UINT32(700, ring.pop(out.data(), 700));
    checkPopped(out, 0, 700, c * 11);
  }
  TEST_ASSERT_FALSE(ring.overflowed());
}

static void test_ring_overflow() {
  std::vector<int16_t> mem(1024 * 2);
  RecRing ring;
  ring.init(mem.data(), 1024);
  std::vector<int16_t> l, r;
  signal(l, r, 0, 1000);
  TEST_ASSERT_TRUE(ring.push(l.data(), r.data(), 1000));
  TEST_ASSERT_FALSE(ring.overflowed());
  std::vector<int16_t> l2(100, 7), r2(100, 7);
  TEST_ASSERT_FALSE(ring.push(l2.data(), r2.data(), 100));
  TEST_ASSERT_TRUE(ring.overflowed());
  TEST_ASSERT_EQUAL_UINT32(1000, ring.size());
  std::vector<int16_t> out(1024 * 2);
  TEST_ASSERT_EQUAL_UINT32(1000, ring.pop(out.data(), 1024));
  checkPopped(out, 0, 1000);
  ring.reset();
  TEST_ASSERT_FALSE(ring.overflowed());
  TEST_ASSERT_EQUAL_UINT32(0, ring.size());
}

static void test_ring_keep_last() {
  std::vector<int16_t> mem(4096 * 2);
  RecRing ring;
  ring.init(mem.data(), 4096);
  std::vector<int16_t> l, r;
  signal(l, r, 0, 3000);
  TEST_ASSERT_TRUE(ring.push(l.data(), r.data(), 3000));
  ring.keepLast(882);
  TEST_ASSERT_EQUAL_UINT32(882, ring.size());
  std::vector<int16_t> out(882 * 2);
  TEST_ASSERT_EQUAL_UINT32(882, ring.pop(out.data(), 4096));
  checkPopped(out, 3000 - 882, 882);
  ring.keepLast(100);  // fewer than n kept: nothing changes
  TEST_ASSERT_EQUAL_UINT32(0, ring.size());
}

// --- meter, threshold, gain ---

static void test_meter_peak_clips() {
  RecMeter m;
  const int16_t a[] = {10, -32768, 500, 32767, -200};
  m.feed(a, 5);
  uint16_t p = 0;
  uint32_t c = 0;
  m.take(p, c);
  TEST_ASSERT_EQUAL_UINT16(32767, p);
  TEST_ASSERT_EQUAL_UINT32(2, c);
  m.take(p, c);
  TEST_ASSERT_EQUAL_UINT16(0, p);
  TEST_ASSERT_EQUAL_UINT32(0, c);
  const int16_t b[] = {-1200, 300};
  m.feed(b, 2);
  m.take(p, c);
  TEST_ASSERT_EQUAL_UINT16(1200, p);
  TEST_ASSERT_EQUAL_UINT32(0, c);
}

static void test_thresh_db() {
  TEST_ASSERT_INT_WITHIN(1, 327, recThreshLevel(-40));
  TEST_ASSERT_EQUAL_INT16(32767, recThreshLevel(0));
  TEST_ASSERT_EQUAL_INT16(0, recThreshLevel(kRecThreshOff));
}

static void test_gain_ranges() {
  TEST_ASSERT_EQUAL_INT(63, recGainClamp(RecSource::Mic, 70));
  TEST_ASSERT_EQUAL_INT(0, recGainClamp(RecSource::Mic, -5));
  TEST_ASSERT_EQUAL_INT(32, recGainClamp(RecSource::Mic, 32));
  TEST_ASSERT_EQUAL_INT(0, recGainClamp(RecSource::Line, -3));
  TEST_ASSERT_EQUAL_INT(15, recGainClamp(RecSource::Line, 20));
  TEST_ASSERT_EQUAL_INT(0, recGainClamp(RecSource::Resample, 10));
}

// --- RecCapture ---

static void feedBlock(RecCapture& c, int16_t v, int16_t vr) {
  int16_t l[128], r[128];
  for (int i = 0; i < 128; ++i) {
    l[i] = v;
    r[i] = vr;
  }
  c.feed(l, r, 128);
}

static void test_capture_off_thresh_starts_now() {
  static std::vector<int16_t> mem(4096 * 2);
  RecCapture c;
  c.init(mem.data(), 4096);
  TEST_ASSERT_EQUAL(RecState::Idle, c.state());
  c.arm(RecSource::Line, kRecThreshOff, kRecMaxFrames);
  TEST_ASSERT_EQUAL(RecState::Rec, c.state());
  TEST_ASSERT_EQUAL_UINT8(2, c.channels());
  for (int b = 0; b < 3; ++b) feedBlock(c, 5, 6);
  TEST_ASSERT_EQUAL_UINT32(384, c.frames());
  TEST_ASSERT_EQUAL_UINT32(384, c.ring().size());
  int16_t out[2];
  c.ring().pop(out, 1);
  TEST_ASSERT_EQUAL_INT16(5, out[0]);
  TEST_ASSERT_EQUAL_INT16(6, out[1]);
}

static void test_capture_armed_keeps_preroll() {
  static std::vector<int16_t> mem(4096 * 2);
  RecCapture c;
  c.init(mem.data(), 4096);
  c.arm(RecSource::Line, -40, kRecMaxFrames);
  TEST_ASSERT_EQUAL(RecState::Armed, c.state());
  for (int b = 0; b < 20; ++b) feedBlock(c, 10, -10);
  TEST_ASSERT_EQUAL(RecState::Armed, c.state());
  TEST_ASSERT_TRUE(c.ring().size() <= kRecPreRollFrames);
  TEST_ASSERT_EQUAL_UINT32(0, c.frames());
  feedBlock(c, 0, 1000);  // the right channel trips it
  TEST_ASSERT_EQUAL(RecState::Rec, c.state());
  TEST_ASSERT_EQUAL_UINT32(kRecPreRollFrames + 128, c.ring().size());
  TEST_ASSERT_EQUAL_UINT32(kRecPreRollFrames + 128, c.frames());
  feedBlock(c, 0, 0);  // quiet again: still recording
  TEST_ASSERT_EQUAL_UINT32(kRecPreRollFrames + 256, c.frames());
}

static void test_capture_mono_mic() {
  static std::vector<int16_t> mem(1024 * 2);
  RecCapture c;
  c.init(mem.data(), 1024);
  c.arm(RecSource::Mic, kRecThreshOff, kRecMaxFrames);
  TEST_ASSERT_EQUAL_UINT8(1, c.channels());
  feedBlock(c, 123, -999);
  int16_t out[2];
  TEST_ASSERT_EQUAL_UINT32(1, c.ring().pop(out, 1));
  TEST_ASSERT_EQUAL_INT16(123, out[0]);
  TEST_ASSERT_EQUAL_INT16(123, out[1]);
  uint16_t p;
  uint32_t cl;
  c.meter().take(p, cl);
  TEST_ASSERT_EQUAL_UINT16(123, p);  // the unused right input is not metered
}

static void test_capture_max_len() {
  static std::vector<int16_t> mem(4096 * 2);
  RecCapture c;
  c.init(mem.data(), 4096);
  c.arm(RecSource::Resample, kRecThreshOff, 1000);
  for (int b = 0; b < 8; ++b) feedBlock(c, 1, 1);
  TEST_ASSERT_TRUE(c.autoStopped());
  TEST_ASSERT_EQUAL(RecState::Idle, c.state());
  TEST_ASSERT_EQUAL_UINT32(1000, c.frames());
  TEST_ASSERT_EQUAL_UINT32(1000, c.ring().size());
  feedBlock(c, 1, 1);
  TEST_ASSERT_EQUAL_UINT32(1000, c.ring().size());
}

static void test_capture_overflow_stops() {
  static std::vector<int16_t> mem(512 * 2);
  RecCapture c;
  c.init(mem.data(), 512);
  c.arm(RecSource::Line, kRecThreshOff, kRecMaxFrames);
  for (int b = 0; b < 4; ++b) feedBlock(c, 1, 1);
  TEST_ASSERT_FALSE(c.overflow());
  feedBlock(c, 1, 1);
  TEST_ASSERT_TRUE(c.overflow());
  TEST_ASSERT_FALSE(c.autoStopped());
  TEST_ASSERT_EQUAL(RecState::Idle, c.state());
  TEST_ASSERT_EQUAL_UINT32(512, c.frames());
  int16_t out[2 * 512];
  c.ring().pop(out, 512);
  feedBlock(c, 1, 1);  // room again, but the take is over
  TEST_ASSERT_EQUAL_UINT32(0, c.ring().size());
  TEST_ASSERT_EQUAL_UINT32(512, c.frames());
}

static void test_capture_stop_idle() {
  static std::vector<int16_t> mem(1024 * 2);
  RecCapture c;
  c.init(mem.data(), 1024);
  c.arm(RecSource::Line, kRecThreshOff, kRecMaxFrames);
  feedBlock(c, 1, 1);
  c.stop();
  TEST_ASSERT_EQUAL(RecState::Idle, c.state());
  feedBlock(c, 1, 1);
  TEST_ASSERT_EQUAL_UINT32(128, c.frames());
  TEST_ASSERT_EQUAL_UINT32(128, c.ring().size());
  uint16_t p;
  uint32_t cl;
  c.meter().take(p, cl);
  TEST_ASSERT_EQUAL_UINT16(1, p);  // the meter keeps running
}

// Before a take (the window open): the meter follows the watched source, nothing is kept.
static void test_capture_idle_meter_follows_source() {
  static std::vector<int16_t> mem(1024 * 2);
  RecCapture c;
  c.init(mem.data(), 1024);
  c.watch(RecSource::Mic);
  feedBlock(c, 10, 20000);  // MIC: the left input only
  uint16_t p;
  uint32_t cl;
  c.meter().take(p, cl);
  TEST_ASSERT_EQUAL_UINT16(10, p);
  c.watch(RecSource::Line);
  feedBlock(c, 10, 20000);
  c.meter().take(p, cl);
  TEST_ASSERT_EQUAL_UINT16(20000, p);
  TEST_ASSERT_EQUAL(RecState::Idle, c.state());
  TEST_ASSERT_EQUAL_UINT32(0, c.ring().size());
}

// --- mono mix, writer ---

struct VecSource : ByteSource {
  const std::vector<uint8_t>& buf;
  size_t pos = 0;
  explicit VecSource(const std::vector<uint8_t>& b) : buf(b) {}
  bool read(void* d, size_t n) override {
    if (pos + n > buf.size()) return false;
    memcpy(d, buf.data() + pos, n);
    pos += n;
    return true;
  }
  bool skip(size_t n) override {
    if (pos + n > buf.size()) return false;
    pos += n;
    return true;
  }
};

struct MemSink {
  std::vector<uint8_t> buf;
  int failAfter = -1;  // >= 0: calls left before the sink fails
  static bool put(const void* d, uint32_t n, void* ctx) {
    MemSink& s = *static_cast<MemSink*>(ctx);
    if (s.failAfter == 0) return false;
    if (s.failAfter > 0) --s.failAfter;
    const uint8_t* b = static_cast<const uint8_t*>(d);
    s.buf.insert(s.buf.end(), b, b + n);
    return true;
  }
};

static void test_mono_mix() {
  const int16_t lr[] = {100, 300, -32768, -32768, 32767, 32767, -3, 0, 5, -6};
  int16_t out[5];
  recMonoMix(lr, 5, out);
  TEST_ASSERT_EQUAL_INT16(200, out[0]);
  TEST_ASSERT_EQUAL_INT16(-32768, out[1]);
  TEST_ASSERT_EQUAL_INT16(32767, out[2]);
  TEST_ASSERT_EQUAL_INT16(-1, out[3]);  // as wavToMono: the sum / 2 toward zero
  TEST_ASSERT_EQUAL_INT16(0, out[4]);
}

// Writes 1000 + 37 frames of the test signal through a RecWriter with ch channels into a file image.
static std::vector<uint8_t> writeTake(uint16_t ch, RecWriter& w, std::vector<int16_t>& lr) {
  std::vector<int16_t> l, r;
  signal(l, r, 0, 1037);
  lr.resize(1037 * 2);
  for (int i = 0; i < 1037; ++i) {
    lr[i * 2] = l[i];
    lr[i * 2 + 1] = r[i];
  }
  MemSink sink;
  sink.buf.resize(kWavHeaderBytes, 0);
  w.begin(ch, &MemSink::put, &sink);
  TEST_ASSERT_TRUE(w.write(lr.data(), 1000));
  TEST_ASSERT_TRUE(w.write(lr.data() + 2000, 37));
  uint8_t hdr[kWavHeaderBytes];
  TEST_ASSERT_TRUE(w.finish(hdr));
  memcpy(sink.buf.data(), hdr, kWavHeaderBytes);
  return sink.buf;
}

static void test_writer_stereo_file() {
  RecWriter w;
  std::vector<int16_t> lr;
  const std::vector<uint8_t> file = writeTake(2, w, lr);
  TEST_ASSERT_EQUAL_UINT32(kWavHeaderBytes + 1037 * 4, file.size());
  VecSource src(file);
  WavInfo info;
  TEST_ASSERT_EQUAL(WavErr::Ok, wavParse(src, info));
  TEST_ASSERT_EQUAL_UINT16(2, info.channels);
  TEST_ASSERT_EQUAL_UINT32(44100, info.rate);
  TEST_ASSERT_EQUAL_UINT32(1037, info.frames());
  TEST_ASSERT_EQUAL_UINT8(60, info.root);
  std::vector<int16_t> mono(1037);
  recMonoMix(lr.data(), 1037, mono.data());
  TEST_ASSERT_TRUE(info.hasCrc);
  TEST_ASSERT_EQUAL_HEX32(sampleCrc(mono.data(), 1037), info.crc);
  TEST_ASSERT_EQUAL_HEX32(info.crc, w.crc());
  TEST_ASSERT_EQUAL_UINT32(1037, w.frames());
  TEST_ASSERT_EQUAL_MEMORY(lr.data(), file.data() + info.dataOffset, 1037 * 4);
}

static void test_writer_mono_file() {
  RecWriter w;
  std::vector<int16_t> lr;
  const std::vector<uint8_t> file = writeTake(1, w, lr);
  TEST_ASSERT_EQUAL_UINT32(kWavHeaderBytes + 1037 * 2, file.size());
  VecSource src(file);
  WavInfo info;
  TEST_ASSERT_EQUAL(WavErr::Ok, wavParse(src, info));
  TEST_ASSERT_EQUAL_UINT16(1, info.channels);
  TEST_ASSERT_EQUAL_UINT32(1037, info.frames());
  std::vector<int16_t> l(1037);
  for (int i = 0; i < 1037; ++i) l[i] = lr[i * 2];
  TEST_ASSERT_EQUAL_HEX32(sampleCrc(l.data(), 1037), info.crc);
  TEST_ASSERT_EQUAL_MEMORY(l.data(), file.data() + info.dataOffset, 1037 * 2);
}

static void test_writer_sink_fail() {
  MemSink sink;
  sink.failAfter = 1;
  RecWriter w;
  w.begin(1, &MemSink::put, &sink);
  std::vector<int16_t> lr(600 * 2, 3);
  TEST_ASSERT_FALSE(w.write(lr.data(), 600));  // 3 chunks of 256: the second fails
  TEST_ASSERT_TRUE(w.failed());
  uint8_t hdr[kWavHeaderBytes];
  TEST_ASSERT_FALSE(w.finish(hdr));
  TEST_ASSERT_FALSE(w.write(lr.data(), 1));
}

// --- names, binding ---

static void test_rec_name() {
  char name[kSampleNameMax + 1];
  recName(3, name);
  TEST_ASSERT_EQUAL_STRING("REC3", name);
  char path[kRecPathMax];
  recPath(3, path);
  TEST_ASSERT_EQUAL_STRING("/samples/rec/REC3.wav", path);
  recTmpPath(3, path);
  TEST_ASSERT_EQUAL_STRING("/samples/rec/REC3.wav.tmp", path);
  recTmpPath(65535, path);
  TEST_ASSERT_EQUAL_STRING("/samples/rec/REC65535.wav.tmp", path);

  static Project p;
  p = Project();
  TEST_ASSERT_EQUAL_INT(1, nextRecNumber(p, 1));
  TEST_ASSERT_TRUE(projSampleSet(p, "REC1", 1, 10) >= 0);
  TEST_ASSERT_TRUE(projSampleSet(p, "rec2", 2, 10) >= 0);  // names ignore case
  TEST_ASSERT_TRUE(projSampleSet(p, "REC4", 4, 10) >= 0);
  TEST_ASSERT_EQUAL_INT(3, nextRecNumber(p, 1));
  TEST_ASSERT_EQUAL_INT(5, nextRecNumber(p, 4));
  TEST_ASSERT_EQUAL_INT(7, nextRecNumber(p, 7));
  TEST_ASSERT_EQUAL_INT(3, nextRecNumber(p, 0));  // numbers start at 1
  TEST_ASSERT_EQUAL_INT(3, nextRecNumber(p, kRecNumberMax + 1));  // out of range: from 1
  TEST_ASSERT_TRUE(projSampleSet(p, "REC9999", 9, 10) >= 0);
  TEST_ASSERT_EQUAL_INT(3, nextRecNumber(p, kRecNumberMax));  // bounded: wraps to 1
  TEST_ASSERT_EQUAL_INT(9998, nextRecNumber(p, 9998));
}

static void test_rec_bind() {
  static Project p;
  p = Project();
  Instrument& m = p.instruments[2];
  m.type = InstrType::Sample;
  TEST_ASSERT_TRUE(projSampleSet(p, "KICK", 0x1234, 500) >= 0);
  snprintf(m.sample, sizeof(m.sample), "KICK");
  m.root = 48;
  sliceInsert(m, 1000);
  sliceInsert(m, 2000);
  TEST_ASSERT_EQUAL_UINT8(2, m.sliceCount);
  const int i = recBind(p, 2, "REC1", 0xABCD, 4410, 60);
  TEST_ASSERT_TRUE(i >= 0);
  TEST_ASSERT_EQUAL_INT(i, projSampleFind(p, "REC1"));
  TEST_ASSERT_EQUAL_HEX32(0xABCD, p.samples[i].crc);
  TEST_ASSERT_EQUAL_UINT32(4410, p.samples[i].frames);
  TEST_ASSERT_EQUAL_STRING("REC1", m.sample);
  TEST_ASSERT_EQUAL_UINT8(0, m.sliceCount);
  TEST_ASSERT_EQUAL_UINT8(60, m.root);
  TEST_ASSERT_TRUE(projSampleFind(p, "KICK") >= 0);  // the old sample stays in the list

  // Full list: nothing changes.
  static Project q;
  q = Project();
  char name[kSampleNameMax + 1];
  for (int k = 0; k < kProjSamples; ++k) {
    snprintf(name, sizeof(name), "S%d", k);
    TEST_ASSERT_TRUE(projSampleSet(q, name, k + 1, 10) >= 0);
  }
  Instrument& n = q.instruments[2];
  snprintf(n.sample, sizeof(n.sample), "S0");
  sliceInsert(n, 1000);
  TEST_ASSERT_EQUAL_INT(-1, recBind(q, 2, "REC1", 0xABCD, 4410, 60));
  TEST_ASSERT_EQUAL_STRING("S0", n.sample);
  TEST_ASSERT_EQUAL_UINT8(1, n.sliceCount);
  TEST_ASSERT_EQUAL_INT(-1, recBind(q, kInstruments, "S1", 1, 10, 60));  // bad instrument
}

static uint32_t rd32le(const uint8_t* p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// A take straight to the card (REC -> SD): the free space less kRecSdReserveBytes, at most the WAV's
// 4 GB (RIFF / FAT32 sizes are 32 bit), in whole frames past the header.
static void test_sd_limit_frames() {
  TEST_ASSERT_EQUAL_UINT32(0, recSdLimitFrames(0, 2));
  TEST_ASSERT_EQUAL_UINT32(0, recSdLimitFrames(kRecSdReserveBytes, 2));
  TEST_ASSERT_EQUAL_UINT32(0, recSdLimitFrames(kRecSdReserveBytes + kWavHeaderBytes, 2));
  TEST_ASSERT_EQUAL_UINT32(1, recSdLimitFrames(kRecSdReserveBytes + kWavHeaderBytes + 4, 2));
  TEST_ASSERT_EQUAL_UINT32(0, recSdLimitFrames(kRecSdReserveBytes + kWavHeaderBytes + 3, 2));
  TEST_ASSERT_EQUAL_UINT32(2, recSdLimitFrames(kRecSdReserveBytes + kWavHeaderBytes + 4, 1));
  const uint64_t mb100 = 100ull << 20;
  TEST_ASSERT_EQUAL_UINT32((mb100 - kRecSdReserveBytes - kWavHeaderBytes) / 4, recSdLimitFrames(mb100, 2));
  TEST_ASSERT_EQUAL_UINT32((mb100 - kRecSdReserveBytes - kWavHeaderBytes) / 2, recSdLimitFrames(mb100, 1));
  // A big card: the 4 GB file ends it, with the margin; the same past any size.
  const uint64_t big = 1ull << 40;
  for (uint8_t ch = 1; ch <= 2; ++ch) {
    const uint32_t f = recSdLimitFrames(big, ch);
    const uint64_t file = kWavHeaderBytes + static_cast<uint64_t>(f) * ch * 2;
    TEST_ASSERT_TRUE(file <= kRecSdMaxFileBytes);
    TEST_ASSERT_TRUE(file > kRecSdMaxFileBytes - 2u * ch);
    TEST_ASSERT_TRUE(kRecSdMaxFileBytes < 0xFFFFFFFFull);
    TEST_ASSERT_EQUAL_UINT32(f, recSdLimitFrames(~0ull, ch));
  }
  // Hours of stereo fit: more than 6 h at 44.1 kHz.
  TEST_ASSERT_TRUE(recSdLimitFrames(big, 2) > 6u * 3600u * kRecRate);
  TEST_ASSERT_TRUE(recSdLimitFrames(big, 2) > kRecMaxFrames);
}

// The header of a take over 2 GB: unsigned 32-bit sizes, no wrap.
static void test_header_over_2gb() {
  for (uint16_t ch = 1; ch <= 2; ++ch) {
    const uint32_t f = recSdLimitFrames(1ull << 40, static_cast<uint8_t>(ch));
    uint8_t h[kWavHeaderBytes];
    wavHeader(h, f, kRecRate, 60, 0x1234, ch);
    const uint64_t data = static_cast<uint64_t>(f) * ch * 2;
    TEST_ASSERT_TRUE(data > 0x80000000ull);
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(kWavHeaderBytes - 8 + data), rd32le(h + 4));
    TEST_ASSERT_EQUAL_UINT64(kWavHeaderBytes - 8 + data, rd32le(h + 4));
    TEST_ASSERT_EQUAL_UINT64(data, rd32le(h + kWavHeaderBytes - 4));
    TEST_ASSERT_EQUAL_UINT32(f, rd32le(h + kWavHeaderBytes - 12));  // mtcr frames
  }
}

// The capture runs to a limit far past the 5 minutes when asked (SD takes).
static void test_capture_long_limit() {
  static std::vector<int16_t> mem(4096 * 2);
  RecCapture c;
  c.init(mem.data(), 4096);
  const uint32_t lim = recSdLimitFrames(1ull << 40, 2);
  c.arm(RecSource::Line, kRecThreshOff, lim);
  std::vector<int16_t> lr(2048 * 2);
  for (int b = 0; b < 8; ++b) {
    feedBlock(c, 1, 1);
    c.ring().pop(lr.data(), 2048);
  }
  TEST_ASSERT_FALSE(c.autoStopped());
  TEST_ASSERT_EQUAL(RecState::Rec, c.state());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_ring_push_pop);
  RUN_TEST(test_ring_wrap);
  RUN_TEST(test_ring_overflow);
  RUN_TEST(test_ring_keep_last);
  RUN_TEST(test_meter_peak_clips);
  RUN_TEST(test_thresh_db);
  RUN_TEST(test_gain_ranges);
  RUN_TEST(test_capture_off_thresh_starts_now);
  RUN_TEST(test_capture_armed_keeps_preroll);
  RUN_TEST(test_capture_mono_mic);
  RUN_TEST(test_capture_max_len);
  RUN_TEST(test_capture_overflow_stops);
  RUN_TEST(test_capture_stop_idle);
  RUN_TEST(test_capture_idle_meter_follows_source);
  RUN_TEST(test_mono_mix);
  RUN_TEST(test_writer_stereo_file);
  RUN_TEST(test_writer_mono_file);
  RUN_TEST(test_writer_sink_fail);
  RUN_TEST(test_rec_name);
  RUN_TEST(test_rec_bind);
  RUN_TEST(test_sd_limit_frames);
  RUN_TEST(test_header_over_2gb);
  RUN_TEST(test_capture_long_limit);
  return UNITY_END();
}
