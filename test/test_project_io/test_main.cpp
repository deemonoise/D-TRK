#include <stdio.h>
#include <string.h>
#include <unity.h>
#include <vector>
#include "project_io.h"
#include "scale.h"

using namespace mt;

struct VecSink : ByteSink {
  std::vector<uint8_t> buf;
  bool write(const void* d, size_t n) override {
    const uint8_t* b = static_cast<const uint8_t*>(d);
    buf.insert(buf.end(), b, b + n);
    return true;
  }
};

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

// Large objects live in static storage.
static Project a, b;

void setUp() {
  a.reset();
  b.reset();
}
void tearDown() {}

static void fillFull(Project& p) {
  strcpy(p.name, "my_song-1");
  p.bpm = 137;
  p.scaleRoot = 9;
  p.scaleType = 2;
  p.songMode = true;
  p.chainLen = 5;
  const uint8_t ch[] = {0, 3, 3, 15, 7};
  memcpy(p.chain, ch, sizeof(ch));
  for (int t = 0; t < kTracks; ++t) {
    TrackCfg& c = p.tracks[t];
    snprintf(c.name, sizeof(c.name), "T%d_x", t);
    c.channel = static_cast<uint8_t>(15 - t);
    c.defVel = static_cast<uint8_t>(90 + t);
    c.defGate = static_cast<uint8_t>(60 + t);
    c.ccA = static_cast<uint8_t>(10 + t);
    c.ccB = static_cast<uint8_t>(20 + t);
    c.program = t == 3 ? kNoProgram : static_cast<uint8_t>(t);
    c.mute = t == 1;
    c.solo = t == 2;
  }
  Pattern& p0 = p.patterns[0];
  p0.length = 4;
  p0.res = Resolution::Eighth;
  p0.swing = 60;
  p0.steps[0][0].note = 36;
  p0.steps[7][3].note = kNoteOff;
  Pattern& p5 = p.patterns[5];
  p5.length = 128;
  p5.res = Resolution::SixteenthTriplet;
  p5.swing = 75;
  for (int t = 0; t < kTracks; ++t)
    for (int s = 0; s < 128; s += 3) {
      Step& st = p5.steps[t][s];
      st.note = static_cast<uint8_t>((t * 7 + s) % 128);
      st.vel = static_cast<uint8_t>(s % 128);
      st.fx[0] = {Fx::RAT, 3};
      st.fx[1] = {Fx::NDG, static_cast<uint8_t>(-20)};
    }
  // Non-default params with no notes: still written.
  p.patterns[15].length = 32;
}

static void assertSame(const Project& x, const Project& y) {
  TEST_ASSERT_EQUAL_STRING(x.name, y.name);
  TEST_ASSERT_EQUAL(x.bpm, y.bpm);
  TEST_ASSERT_EQUAL(x.scaleRoot, y.scaleRoot);
  TEST_ASSERT_EQUAL(x.scaleType, y.scaleType);
  TEST_ASSERT_EQUAL(x.songMode, y.songMode);
  TEST_ASSERT_EQUAL(x.chainLen, y.chainLen);
  TEST_ASSERT_EQUAL_MEMORY(x.chain, y.chain, kChainMax);
  for (int t = 0; t < kTracks; ++t) {
    const TrackCfg &c = x.tracks[t], &d = y.tracks[t];
    TEST_ASSERT_EQUAL_STRING(c.name, d.name);
    TEST_ASSERT_EQUAL(c.channel, d.channel);
    TEST_ASSERT_EQUAL(c.defVel, d.defVel);
    TEST_ASSERT_EQUAL(c.defGate, d.defGate);
    TEST_ASSERT_EQUAL(c.ccA, d.ccA);
    TEST_ASSERT_EQUAL(c.ccB, d.ccB);
    TEST_ASSERT_EQUAL(c.program, d.program);
    TEST_ASSERT_EQUAL(c.mute, d.mute);
    TEST_ASSERT_EQUAL(c.solo, d.solo);
  }
  for (int i = 0; i < kPatterns; ++i) {
    const Pattern &p = x.patterns[i], &q = y.patterns[i];
    TEST_ASSERT_EQUAL(p.length, q.length);
    TEST_ASSERT_EQUAL(static_cast<int>(p.res), static_cast<int>(q.res));
    TEST_ASSERT_EQUAL(p.swing, q.swing);
    for (int t = 0; t < kTracks; ++t)
      TEST_ASSERT_EQUAL_MEMORY(p.steps[t], q.steps[t], sizeof(Step) * p.length);
  }
}

void test_crc32_reference() {
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32("123456789", 9));
  const uint32_t part = crc32("1234", 4);
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32("56789", 5, part));
}

void test_round_trip_full_project() {
  fillFull(a);
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  assertSame(a, b);
}

void test_load_resets_target_first() {
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  b.patterns[3].steps[0][0].note = 50;
  b.songMode = true;
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_TRUE(b.patterns[3].isEmpty());
  TEST_ASSERT_FALSE(b.songMode);
}

void test_empty_patterns_not_written() {
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_TRUE(out.buf.size() < 300);
  a.patterns[2].steps[1][1].note = 60;
  VecSink out2;
  TEST_ASSERT_TRUE(saveProject(a, out2));
  // One pattern of 16 steps: 8 + 4 + 8 * 16 * 6 bytes.
  TEST_ASSERT_EQUAL(out.buf.size() + 8 + 4 + 768, out2.buf.size());
}

// Rebuilds a file with an extra chunk inserted after the header and a fresh CRC.
static std::vector<uint8_t> withChunkAfterHeader(const std::vector<uint8_t>& f, const char id[4],
                                                 const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> r(f.begin(), f.begin() + 8);
  r.insert(r.end(), id, id + 4);
  const uint32_t n = payload.size();
  for (int i = 0; i < 4; ++i) r.push_back(static_cast<uint8_t>(n >> (8 * i)));
  r.insert(r.end(), payload.begin(), payload.end());
  r.insert(r.end(), f.begin() + 8, f.end() - 12);
  const uint32_t c = crc32(r.data(), r.size());
  const uint8_t tail[] = {'C', 'R', 'C', ' ', 4, 0, 0, 0,
                          static_cast<uint8_t>(c), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c >> 16),
                          static_cast<uint8_t>(c >> 24)};
  r.insert(r.end(), tail, tail + 12);
  return r;
}

void test_unknown_chunk_skipped() {
  fillFull(a);
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  std::vector<uint8_t> payload(37, 0xAB);
  const std::vector<uint8_t> f = withChunkAfterHeader(out.buf, "ZZZZ", payload);
  VecSource in(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  assertSame(a, b);
}

void test_corrupt_byte_bad_crc() {
  fillFull(a);
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  out.buf[out.buf.size() - 100] ^= 0x01;  // inside pattern step data
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadCrc), static_cast<int>(loadProject(in, b)));
}

void test_truncated_file() {
  fillFull(a);
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  std::vector<uint8_t> cut(out.buf.begin(), out.buf.begin() + out.buf.size() / 2);
  VecSource in(cut);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Truncated), static_cast<int>(loadProject(in, b)));
}

void test_missing_crc_is_truncated() {
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  std::vector<uint8_t> cut(out.buf.begin(), out.buf.end() - 12);
  VecSource in(cut);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Truncated), static_cast<int>(loadProject(in, b)));
}

void test_bad_magic() {
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  out.buf[0] = 'X';
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadMagic), static_cast<int>(loadProject(in, b)));
}

void test_newer_version_rejected() {
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  out.buf[4] = 2;
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadVersion), static_cast<int>(loadProject(in, b)));
}

static void put16(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back(static_cast<uint8_t>(x));
  v.push_back(static_cast<uint8_t>(x >> 8));
}
static void putChunk(std::vector<uint8_t>& f, const char id[4], const std::vector<uint8_t>& p) {
  f.insert(f.end(), id, id + 4);
  put16(f, static_cast<uint16_t>(p.size()));
  put16(f, 0);
  f.insert(f.end(), p.begin(), p.end());
}

void test_garbage_values_clamped() {
  std::vector<uint8_t> f = {'M', 'T', 'R', 'K', 1, 0, 0, 0};
  std::vector<uint8_t> pr(17, 0);
  memcpy(pr.data(), "ABCDEFGHIJKLMNOPQ", 17);  // no terminator
  put16(pr, 999);
  pr.push_back(50);   // scaleRoot
  pr.push_back(200);  // scaleType
  pr.push_back(7);    // songMode
  pr.push_back(200);  // chainLen
  for (int i = 0; i < kChainMax; ++i) pr.push_back(99);
  putChunk(f, "PROJ", pr);

  std::vector<uint8_t> tr = {8};
  for (int t = 0; t < kTracks; ++t) {
    for (int i = 0; i < 9; ++i) tr.push_back('Z');  // no terminator
    tr.push_back(40);   // channel
    tr.push_back(0);    // defVel
    tr.push_back(255);  // defGate
    tr.push_back(200);  // ccA
    tr.push_back(130);  // ccB
    tr.push_back(140);  // program
    tr.push_back(0xFF);  // flags
  }
  putChunk(f, "TRKS", tr);

  std::vector<uint8_t> pt = {3, 2, 99, 10};  // index 3, length 2, bad res, swing 10
  for (int t = 0; t < kTracks; ++t)
    for (int s = 0; s < 2; ++s) {
      const uint8_t st[] = {200, 250, 99, 5, static_cast<uint8_t>(Fx::GAT), 7};
      pt.insert(pt.end(), st, st + 6);
    }
  putChunk(f, "PATN", pt);
  std::vector<uint8_t> bad = {40, 16, 2, 50};  // pattern index out of range: ignored
  bad.resize(4 + 8 * 16 * 6, 0x24);
  putChunk(f, "PATN", bad);

  const uint32_t c = crc32(f.data(), f.size());
  const uint8_t tail[] = {'C', 'R', 'C', ' ', 4, 0, 0, 0,
                          static_cast<uint8_t>(c), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c >> 16),
                          static_cast<uint8_t>(c >> 24)};
  f.insert(f.end(), tail, tail + 12);

  VecSource in(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_EQUAL(16, strlen(b.name));
  TEST_ASSERT_EQUAL(300, b.bpm);
  TEST_ASSERT_TRUE(b.scaleRoot < 12);
  TEST_ASSERT_TRUE(b.scaleType < static_cast<int>(ScaleType::Count));
  TEST_ASSERT_TRUE(b.songMode);
  TEST_ASSERT_EQUAL(kChainMax, b.chainLen);
  for (int i = 0; i < kChainMax; ++i) TEST_ASSERT_TRUE(b.chain[i] < kPatterns);
  for (int t = 0; t < kTracks; ++t) {
    const TrackCfg& c2 = b.tracks[t];
    TEST_ASSERT_EQUAL(8, strlen(c2.name));
    TEST_ASSERT_TRUE(c2.channel <= 15);
    TEST_ASSERT_TRUE(c2.defVel >= 1 && c2.defVel <= 127);
    TEST_ASSERT_TRUE(c2.defGate >= 1 && c2.defGate <= 200);
    TEST_ASSERT_TRUE(c2.ccA <= 127 && c2.ccB <= 127);
    TEST_ASSERT_TRUE(c2.program <= 127 || c2.program == kNoProgram);
    TEST_ASSERT_TRUE(c2.mute);
    TEST_ASSERT_TRUE(c2.solo);
  }
  const Pattern& p3 = b.patterns[3];
  TEST_ASSERT_EQUAL(kMinSteps, p3.length);
  TEST_ASSERT_TRUE(static_cast<int>(p3.res) < static_cast<int>(Resolution::Count));
  TEST_ASSERT_EQUAL(50, p3.swing);
  const Step& s0 = p3.steps[0][0];
  TEST_ASSERT_EQUAL(kNoteEmpty, s0.note);
  TEST_ASSERT_TRUE(s0.vel <= 127);
  TEST_ASSERT_EQUAL(static_cast<int>(Fx::None), static_cast<int>(s0.fx[0].cmd));
  TEST_ASSERT_EQUAL(static_cast<int>(Fx::GAT), static_cast<int>(s0.fx[1].cmd));
  TEST_ASSERT_TRUE(p3.steps[0][2].isEmpty());  // beyond the stored length
}

// ---- Malformed / forward-compatible chunks ----

static std::vector<uint8_t> fileHeader() { return {'M', 'T', 'R', 'K', 1, 0, 0, 0}; }

static void putChunkSized(std::vector<uint8_t>& f, const char id[4], uint32_t size, const std::vector<uint8_t>& p) {
  f.insert(f.end(), id, id + 4);
  for (int i = 0; i < 4; ++i) f.push_back(static_cast<uint8_t>(size >> (8 * i)));
  f.insert(f.end(), p.begin(), p.end());
}

static void finish(std::vector<uint8_t>& f) {
  const uint32_t c = crc32(f.data(), f.size());
  const uint8_t tail[] = {'C', 'R', 'C', ' ', 4, 0, 0, 0,
                          static_cast<uint8_t>(c), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c >> 16),
                          static_cast<uint8_t>(c >> 24)};
  f.insert(f.end(), tail, tail + 12);
}

static std::vector<uint8_t> patn(uint8_t idx, uint8_t len, int extraBytes = 0) {
  std::vector<uint8_t> p = {idx, len, static_cast<uint8_t>(Resolution::Eighth), 60};
  for (int t = 0; t < kTracks; ++t)
    for (int s = 0; s < len; ++s) {
      const uint8_t st[] = {static_cast<uint8_t>((t + s) % 128), 100, 0, 0, 0, 0};
      p.insert(p.end(), st, st + 6);
    }
  if (extraBytes > 0) p.resize(p.size() + extraBytes, 0);
  if (extraBytes < 0) p.resize(p.size() + extraBytes);
  return p;
}

static LoadErr loadBytes(const std::vector<uint8_t>& f) {
  VecSource in(f);
  return loadProject(in, b);
}

void test_patn_length_zero() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "PATN", patn(2, 0));
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(kMinSteps, b.patterns[2].length);
  TEST_ASSERT_TRUE(b.patterns[2].isEmpty());
}

void test_patn_length_over_max() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "PATN", patn(1, 200));
  putChunk(f, "PATN", patn(4, 8));  // still parsed after the oversized one
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  const Pattern& p1 = b.patterns[1];
  TEST_ASSERT_EQUAL(kMaxSteps, p1.length);
  TEST_ASSERT_EQUAL((7 + 127) % 128, p1.steps[7][127].note);
  TEST_ASSERT_EQUAL(8, b.patterns[4].length);
  TEST_ASSERT_EQUAL((3 + 5) % 128, b.patterns[4].steps[3][5].note);
}

void test_patn_size_mismatch() {
  for (int extra : {-1, 6, 1}) {
    std::vector<uint8_t> f = fileHeader();
    putChunk(f, "PATN", patn(1, 16, extra));
    finish(f);
    TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
  }
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "PATN", {1, 16});  // shorter than the header
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
}

static std::vector<uint8_t> trks(uint8_t count, int stored) {
  std::vector<uint8_t> tr = {count};
  for (int t = 0; t < stored; ++t) {
    char nm[9] = {0};
    snprintf(nm, sizeof(nm), "K%d", t);
    tr.insert(tr.end(), nm, nm + 9);
    const uint8_t rest[] = {static_cast<uint8_t>(t), 90, 70, 1, 2, 5, 0};
    tr.insert(tr.end(), rest, rest + 7);
  }
  return tr;
}

void test_trks_count_not_eight() {
  {
    std::vector<uint8_t> f = fileHeader();
    putChunk(f, "TRKS", trks(3, 3));
    finish(f);
    TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
    TEST_ASSERT_EQUAL_STRING("K2", b.tracks[2].name);
    TEST_ASSERT_EQUAL(2, b.tracks[2].channel);
    const TrackCfg& def = a.tracks[3];  // a is freshly reset
    TEST_ASSERT_EQUAL_STRING(def.name, b.tracks[3].name);
    TEST_ASSERT_EQUAL(def.defVel, b.tracks[3].defVel);
  }
  {
    std::vector<uint8_t> f = fileHeader();
    putChunk(f, "TRKS", trks(10, 10));  // a future build with more tracks
    putChunk(f, "PATN", patn(0, 4));
    finish(f);
    TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
    TEST_ASSERT_EQUAL_STRING("K7", b.tracks[7].name);
    TEST_ASSERT_EQUAL(4, b.patterns[0].length);
  }
  {
    std::vector<uint8_t> f = fileHeader();
    putChunk(f, "TRKS", trks(10, 8));  // count says more than the chunk holds
    finish(f);
    TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
  }
}

void test_unknown_chunk_huge_size() {
  std::vector<uint8_t> f = fileHeader();
  putChunkSized(f, "ZZZZ", 0xFFFFFFF0u, std::vector<uint8_t>(100, 0x55));
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Truncated), static_cast<int>(loadBytes(f)));
}

void test_proj_chunk_larger_is_skipped() {
  fillFull(a);
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  // Grow the PROJ chunk (right after the header) by 20 bytes, as a newer minor version might.
  std::vector<uint8_t> f(out.buf.begin(), out.buf.end() - 12);
  const uint32_t size = f[12] | (f[13] << 8) | (f[14] << 16) | (static_cast<uint32_t>(f[15]) << 24);
  TEST_ASSERT_EQUAL(0, memcmp(f.data() + 8, "PROJ", 4));
  const uint32_t grown = size + 20;
  for (int i = 0; i < 4; ++i) f[12 + i] = static_cast<uint8_t>(grown >> (8 * i));
  f.insert(f.begin() + 16 + size, 20, 0xEE);
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  assertSame(a, b);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_crc32_reference);
  RUN_TEST(test_round_trip_full_project);
  RUN_TEST(test_load_resets_target_first);
  RUN_TEST(test_empty_patterns_not_written);
  RUN_TEST(test_unknown_chunk_skipped);
  RUN_TEST(test_corrupt_byte_bad_crc);
  RUN_TEST(test_truncated_file);
  RUN_TEST(test_missing_crc_is_truncated);
  RUN_TEST(test_bad_magic);
  RUN_TEST(test_newer_version_rejected);
  RUN_TEST(test_garbage_values_clamped);
  RUN_TEST(test_patn_length_zero);
  RUN_TEST(test_patn_length_over_max);
  RUN_TEST(test_patn_size_mismatch);
  RUN_TEST(test_trks_count_not_eight);
  RUN_TEST(test_unknown_chunk_huge_size);
  RUN_TEST(test_proj_chunk_larger_is_skipped);
  return UNITY_END();
}
