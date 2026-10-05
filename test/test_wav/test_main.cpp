#include <math.h>
#include <string.h>
#include <unity.h>
#include <vector>
#include "wav.h"

using namespace mt;

void setUp() {}
void tearDown() {}

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

static void put16(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(x & 0xFF);
  v.push_back((x >> 8) & 0xFF);
}
static void put32(std::vector<uint8_t>& v, uint32_t x) {
  put16(v, x & 0xFFFF);
  put16(v, x >> 16);
}
static void putId(std::vector<uint8_t>& v, const char* id) { v.insert(v.end(), id, id + 4); }

static void chunk(std::vector<uint8_t>& v, const char* id, const std::vector<uint8_t>& body) {
  putId(v, id);
  put32(v, static_cast<uint32_t>(body.size()));
  v.insert(v.end(), body.begin(), body.end());
  if (body.size() & 1) v.push_back(0);
}

static std::vector<uint8_t> fmtBody(uint16_t format, uint16_t ch, uint32_t rate, uint16_t bits, bool extensible = false) {
  std::vector<uint8_t> f;
  put16(f, extensible ? 0xFFFE : format);
  put16(f, ch);
  put32(f, rate);
  put32(f, rate * ch * bits / 8);
  put16(f, ch * bits / 8);
  put16(f, bits);
  if (extensible) {
    put16(f, 22);
    put16(f, bits);
    put32(f, 0);
    put16(f, format);  // sub-format GUID starts with the format code
    static const uint8_t rest[14] = {0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71};
    f.insert(f.end(), rest, rest + 14);
  }
  return f;
}

// RIFF file: "fmt " chunk, extra chunks before "data", data, extra chunks after.
static std::vector<uint8_t> wav(const std::vector<uint8_t>& fmt, const std::vector<uint8_t>& data,
                                const std::vector<uint8_t>& before = {}, const std::vector<uint8_t>& after = {}) {
  std::vector<uint8_t> body;
  putId(body, "WAVE");
  chunk(body, "fmt ", fmt);
  body.insert(body.end(), before.begin(), before.end());
  chunk(body, "data", data);
  body.insert(body.end(), after.begin(), after.end());
  std::vector<uint8_t> v;
  putId(v, "RIFF");
  put32(v, static_cast<uint32_t>(body.size()));
  v.insert(v.end(), body.begin(), body.end());
  return v;
}

static std::vector<uint8_t> pcm16(const std::vector<int16_t>& s) {
  std::vector<uint8_t> d;
  for (int16_t x : s) put16(d, static_cast<uint16_t>(x));
  return d;
}

void test_mono16() {
  const std::vector<uint8_t> f = wav(fmtBody(1, 1, 32000, 16), pcm16({0, 1000, -1000, 32767, -32768}));
  VecSource src(f);
  WavInfo w;
  TEST_ASSERT_EQUAL(static_cast<int>(WavErr::Ok), static_cast<int>(wavParse(src, w)));
  TEST_ASSERT_EQUAL(1, w.channels);
  TEST_ASSERT_EQUAL(16, w.bits);
  TEST_ASSERT_EQUAL_UINT32(32000, w.rate);
  TEST_ASSERT_EQUAL_UINT32(5, w.frames());
  TEST_ASSERT_EQUAL_UINT32(10, w.dataBytes);
  TEST_ASSERT_EQUAL_UINT32(44, w.dataOffset);
  TEST_ASSERT_FALSE(w.hasRoot);
  TEST_ASSERT_EQUAL(60, w.root);
  int16_t out[5];
  wavToMono(f.data() + w.dataOffset, 5, w, out);
  const int16_t want[5] = {0, 1000, -1000, 32767, -32768};
  TEST_ASSERT_EQUAL_INT16_ARRAY(want, out, 5);
}

void test_8bit_unsigned() {
  const std::vector<uint8_t> f = wav(fmtBody(1, 1, 8000, 8), {128, 255, 0, 192});
  VecSource src(f);
  WavInfo w;
  TEST_ASSERT_EQUAL(static_cast<int>(WavErr::Ok), static_cast<int>(wavParse(src, w)));
  TEST_ASSERT_EQUAL_UINT32(4, w.frames());
  int16_t out[4];
  wavToMono(f.data() + w.dataOffset, 4, w, out);
  TEST_ASSERT_EQUAL_INT16(0, out[0]);
  TEST_ASSERT_EQUAL_INT16(127 * 256, out[1]);
  TEST_ASSERT_EQUAL_INT16(-32768, out[2]);
  TEST_ASSERT_EQUAL_INT16(64 * 256, out[3]);
}

void test_24bit() {
  // 0x123456, -2 (0xFFFFFE), 0x7FFFFF
  const std::vector<uint8_t> f = wav(fmtBody(1, 1, 44100, 24), {0x56, 0x34, 0x12, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F});
  VecSource src(f);
  WavInfo w;
  TEST_ASSERT_EQUAL(static_cast<int>(WavErr::Ok), static_cast<int>(wavParse(src, w)));
  TEST_ASSERT_EQUAL_UINT32(3, w.frames());
  int16_t out[3];
  wavToMono(f.data() + w.dataOffset, 3, w, out);
  TEST_ASSERT_EQUAL_INT16(0x1234, out[0]);
  TEST_ASSERT_EQUAL_INT16(-1, out[1]);
  TEST_ASSERT_EQUAL_INT16(0x7FFF, out[2]);
}

void test_stereo_averaged() {
  const std::vector<uint8_t> f = wav(fmtBody(1, 2, 32000, 16), pcm16({1000, 3000, -32768, -32768, 32767, -32767}));
  VecSource src(f);
  WavInfo w;
  TEST_ASSERT_EQUAL(static_cast<int>(WavErr::Ok), static_cast<int>(wavParse(src, w)));
  TEST_ASSERT_EQUAL(2, w.channels);
  TEST_ASSERT_EQUAL_UINT32(3, w.frames());
  int16_t out[3];
  wavToMono(f.data() + w.dataOffset, 3, w, out);
  TEST_ASSERT_EQUAL_INT16(2000, out[0]);
  TEST_ASSERT_EQUAL_INT16(-32768, out[1]);
  TEST_ASSERT_EQUAL_INT16(0, out[2]);
}

void test_list_before_data_skipped() {
  std::vector<uint8_t> extra;
  chunk(extra, "LIST", {'I', 'N', 'F', 'O', 'a', 'b', 'c'});  // odd size: padded
  chunk(extra, "fact", {1, 0, 0, 0});
  const std::vector<uint8_t> f = wav(fmtBody(1, 1, 22050, 16), pcm16({7, 8}), extra);
  VecSource src(f);
  WavInfo w;
  TEST_ASSERT_EQUAL(static_cast<int>(WavErr::Ok), static_cast<int>(wavParse(src, w)));
  TEST_ASSERT_EQUAL_UINT32(2, w.frames());
  TEST_ASSERT_EQUAL_UINT32(22050, w.rate);
  int16_t out[2];
  wavToMono(f.data() + w.dataOffset, 2, w, out);
  TEST_ASSERT_EQUAL_INT16(7, out[0]);
  TEST_ASSERT_EQUAL_INT16(8, out[1]);
}

void test_float_unsupported() {
  const std::vector<uint8_t> f = wav(fmtBody(3, 1, 32000, 32), {0, 0, 0, 0});
  VecSource src(f);
  WavInfo w;
  TEST_ASSERT_EQUAL(static_cast<int>(WavErr::Unsupported), static_cast<int>(wavParse(src, w)));
}

void test_32bit_pcm_unsupported() {
  const std::vector<uint8_t> f = wav(fmtBody(1, 1, 32000, 32), {0, 0, 0, 0});
  VecSource src(f);
  WavInfo w;
  TEST_ASSERT_EQUAL(static_cast<int>(WavErr::Unsupported), static_cast<int>(wavParse(src, w)));
}

void test_extensible_pcm() {
  const std::vector<uint8_t> f = wav(fmtBody(1, 2, 48000, 16, true), pcm16({10, 20}));
  VecSource src(f);
  WavInfo w;
  TEST_ASSERT_EQUAL(static_cast<int>(WavErr::Ok), static_cast<int>(wavParse(src, w)));
  TEST_ASSERT_EQUAL(2, w.channels);
  TEST_ASSERT_EQUAL_UINT32(48000, w.rate);
  TEST_ASSERT_EQUAL_UINT32(1, w.frames());
}

void test_extensible_float_unsupported() {
  const std::vector<uint8_t> f = wav(fmtBody(3, 1, 48000, 32, true), {0, 0, 0, 0});
  VecSource src(f);
  WavInfo w;
  TEST_ASSERT_EQUAL(static_cast<int>(WavErr::Unsupported), static_cast<int>(wavParse(src, w)));
}

void test_not_wav() {
  std::vector<uint8_t> f = wav(fmtBody(1, 1, 32000, 16), pcm16({1}));
  f[8] = 'X';  // "WAVE" -> "XAVE"
  VecSource src(f);
  WavInfo w;
  TEST_ASSERT_EQUAL(static_cast<int>(WavErr::NotWav), static_cast<int>(wavParse(src, w)));
  const std::vector<uint8_t> tiny = {'R', 'I'};
  VecSource src2(tiny);
  TEST_ASSERT_EQUAL(static_cast<int>(WavErr::NotWav), static_cast<int>(wavParse(src2, w)));
}

void test_truncated() {
  std::vector<uint8_t> f = wav(fmtBody(1, 1, 32000, 16), pcm16({1, 2, 3, 4}));
  f.resize(f.size() - 3);  // cut inside the data
  VecSource src(f);
  WavInfo w;
  TEST_ASSERT_EQUAL(static_cast<int>(WavErr::Truncated), static_cast<int>(wavParse(src, w)));
  std::vector<uint8_t> g = wav(fmtBody(1, 1, 32000, 16), pcm16({1}));
  g.resize(30);  // cut inside "fmt "
  VecSource src2(g);
  TEST_ASSERT_EQUAL(static_cast<int>(WavErr::Truncated), static_cast<int>(wavParse(src2, w)));
}

void test_smpl_root_after_data() {
  std::vector<uint8_t> body;
  for (int i = 0; i < 3; ++i) put32(body, 0);  // manufacturer, product, sample period
  put32(body, 48);                             // MIDI unity note
  for (int i = 0; i < 5; ++i) put32(body, 0);
  std::vector<uint8_t> after;
  chunk(after, "smpl", body);
  const std::vector<uint8_t> f = wav(fmtBody(1, 1, 32000, 16), pcm16({1, 2, 3}), {}, after);
  VecSource src(f);
  WavInfo w;
  TEST_ASSERT_EQUAL(static_cast<int>(WavErr::Ok), static_cast<int>(wavParse(src, w)));
  TEST_ASSERT_TRUE(w.hasRoot);
  TEST_ASSERT_EQUAL(48, w.root);
  TEST_ASSERT_EQUAL_UINT32(3, w.frames());
}

void test_short_smpl_before_data_skipped() {
  std::vector<uint8_t> before;
  chunk(before, "smpl", {1, 2, 3, 4, 5});  // too short for a root note; odd size: padded
  const std::vector<uint8_t> f = wav(fmtBody(1, 1, 32000, 16), pcm16({7, 8}), before);
  VecSource src(f);
  WavInfo w;
  TEST_ASSERT_EQUAL(static_cast<int>(WavErr::Ok), static_cast<int>(wavParse(src, w)));
  TEST_ASSERT_FALSE(w.hasRoot);
  TEST_ASSERT_EQUAL_UINT32(2, w.frames());
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(f.size() - 4), w.dataOffset);
}

void test_downsampler_passthrough() {
  Downsampler d(22050);
  TEST_ASSERT_EQUAL_UINT32(22050, d.outRate());
  const int16_t in[4] = {1, 2, 3, 4};
  int16_t out[8];
  TEST_ASSERT_EQUAL(4, d.push(in, 4, out));
  TEST_ASSERT_EQUAL_INT16_ARRAY(in, out, 4);
}

// 1 kHz sine at 48 kHz in uneven chunks: about 2/3 as many frames, still 1 kHz at 32 kHz.
void test_downsampler_48k() {
  constexpr int kIn = 4800;
  static int16_t in[kIn];
  for (int i = 0; i < kIn; ++i) in[i] = static_cast<int16_t>(16000 * sin(2 * M_PI * 1000.0 * i / 48000.0));
  Downsampler d(48000);
  TEST_ASSERT_EQUAL_UINT32(32000, d.outRate());
  static int16_t out[kIn];
  int n = 0, pos = 0;
  const int chunks[] = {1, 7, 333, 1000, 59};
  for (int c = 0; pos < kIn; ++c) {
    int k = chunks[c % 5];
    if (k > kIn - pos) k = kIn - pos;
    const int got = d.push(in + pos, k, out + n);
    TEST_ASSERT_TRUE(static_cast<uint32_t>(got) <= Downsampler::outFrames(k, 48000));
    n += got;
    pos += k;
  }
  TEST_ASSERT_INT_WITHIN(1, 3200, n);
  TEST_ASSERT_TRUE(static_cast<uint32_t>(n) <= Downsampler::outFrames(kIn, 48000));
  // Rising zero crossings: 100 periods.
  int cross = 0;
  for (int i = 1; i < n; ++i)
    if (out[i - 1] < 0 && out[i] >= 0) ++cross;
  TEST_ASSERT_INT_WITHIN(1, 100, cross);
  // Amplitude kept (2-tap average loses ~1 % at 1 kHz).
  int peak = 0;
  for (int i = 0; i < n; ++i) peak = out[i] > peak ? out[i] : peak;
  TEST_ASSERT_INT_WITHIN(600, 16000, peak);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_mono16);
  RUN_TEST(test_8bit_unsigned);
  RUN_TEST(test_24bit);
  RUN_TEST(test_stereo_averaged);
  RUN_TEST(test_list_before_data_skipped);
  RUN_TEST(test_float_unsupported);
  RUN_TEST(test_32bit_pcm_unsupported);
  RUN_TEST(test_extensible_pcm);
  RUN_TEST(test_extensible_float_unsupported);
  RUN_TEST(test_not_wav);
  RUN_TEST(test_truncated);
  RUN_TEST(test_smpl_root_after_data);
  RUN_TEST(test_short_smpl_before_data_skipped);
  RUN_TEST(test_downsampler_passthrough);
  RUN_TEST(test_downsampler_48k);
  return UNITY_END();
}
