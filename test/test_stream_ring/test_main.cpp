#include <unity.h>
#include "stream_ring.h"

using namespace mt;

static StreamRing ring;
static int16_t in[20000], out[40000];

void setUp() {
  ring.stop();
  ring.reset();
}
void tearDown() {}

void test_resampler_same_rate_passes_through() {
  LinearResampler rs(44100, 44100);
  for (int i = 0; i < 100; ++i) in[i] = static_cast<int16_t>(i * 3);
  int k = rs.push(in, 60, out);
  k += rs.push(in + 60, 40, out + k);
  // The last input frame waits for its successor.
  TEST_ASSERT_EQUAL(99, k);
  for (int i = 0; i < k; ++i) TEST_ASSERT_EQUAL_INT16(i * 3, out[i]);
}

void test_resampler_doubles_and_interpolates() {
  LinearResampler rs(22050, 44100);
  for (int i = 0; i < 1000; ++i) in[i] = static_cast<int16_t>(i * 30);
  int k = 0;
  for (int i = 0; i < 1000; i += 7) k += rs.push(in + i, i + 7 <= 1000 ? 7 : 1000 - i, out + k);
  TEST_ASSERT_EQUAL(2 * 999, k);
  for (int j = 0; j < k; ++j) TEST_ASSERT_EQUAL_INT16(j * 15, out[j]);
}

void test_resampler_48k_length_and_bound() {
  LinearResampler rs(48000, 44100);
  for (int i = 0; i < 20000; ++i) in[i] = 1000;
  int k = 0;
  for (int i = 0; i < 20000; i += 512) {
    const int n = i + 512 <= 20000 ? 512 : 20000 - i;
    const int got = rs.push(in + i, n, out + k);
    TEST_ASSERT_TRUE(static_cast<uint32_t>(got) <= rs.maxOut(n));
    k += got;
  }
  TEST_ASSERT_INT_WITHIN(2, 20000 * 44100 / 48000, k);
  for (int j = 0; j < k; ++j) TEST_ASSERT_EQUAL_INT16(1000, out[j]);
  TEST_ASSERT_TRUE(rs.maxOut(rs.inFor(100)) <= 100);
}

void test_ring_plays_then_stops_when_finished() {
  for (int i = 0; i < 300; ++i) in[i] = 1000;
  ring.write(in, 300);
  TEST_ASSERT_EQUAL_UINT32(StreamRing::kLen - 300, ring.space());
  ring.finish();
  ring.start();
  int16_t blk[128];
  for (auto& v : blk) v = 5;
  ring.mix(blk, 128, 0.5f);
  TEST_ASSERT_EQUAL_INT16(505, blk[0]);
  TEST_ASSERT_EQUAL_INT16(505, blk[127]);
  ring.mix(blk, 128, 1.f);
  TEST_ASSERT_TRUE(ring.playing());
  for (auto& v : blk) v = 0;
  ring.mix(blk, 128, 1.f);  // 44 left, then the end
  TEST_ASSERT_EQUAL_INT16(1000, blk[43]);
  TEST_ASSERT_EQUAL_INT16(0, blk[44]);
  TEST_ASSERT_FALSE(ring.playing());
  TEST_ASSERT_EQUAL_UINT32(0, ring.takeUnderruns());
}

void test_ring_underrun_is_silence_and_counted() {
  for (int i = 0; i < 100; ++i) in[i] = -2000;
  ring.write(in, 100);
  ring.start();
  int16_t blk[128] = {};
  ring.mix(blk, 128, 1.f);
  TEST_ASSERT_EQUAL_INT16(-2000, blk[99]);
  TEST_ASSERT_EQUAL_INT16(0, blk[100]);
  TEST_ASSERT_TRUE(ring.playing());
  TEST_ASSERT_EQUAL_UINT32(1, ring.takeUnderruns());
  TEST_ASSERT_EQUAL_UINT32(0, ring.takeUnderruns());
}

void test_ring_wraps_and_saturates() {
  ring.start();
  int16_t blk[1000];
  for (int round = 0; round < 40; ++round) {
    for (int i = 0; i < 1000; ++i) in[i] = static_cast<int16_t>(round * 1000 + i - 20000);
    TEST_ASSERT_TRUE(ring.space() >= 1000);
    ring.write(in, 1000);
    for (auto& v : blk) v = 0;
    ring.mix(blk, 1000, 1.f);
    for (int i = 0; i < 1000; ++i) TEST_ASSERT_EQUAL_INT16(in[i], blk[i]);
  }
  for (int i = 0; i < 10; ++i) in[i] = 30000;
  ring.write(in, 10);
  for (auto& v : blk) v = 30000;
  ring.mix(blk, 10, 1.f);
  TEST_ASSERT_EQUAL_INT16(32767, blk[0]);
}

void test_stopped_ring_adds_nothing() {
  ring.write(in, 10);
  int16_t blk[4] = {1, 2, 3, 4};
  ring.mix(blk, 4, 1.f);
  TEST_ASSERT_EQUAL_INT16(1, blk[0]);
  TEST_ASSERT_EQUAL_UINT32(10, ring.avail());
}

void test_ring_mixes_into_both_channels() {
  for (int i = 0; i < 128; ++i) in[i] = static_cast<int16_t>(i * 10);
  ring.write(in, 128);
  ring.start();
  int16_t l[128], r[128];
  for (int i = 0; i < 128; ++i) {
    l[i] = 1;
    r[i] = 2;
  }
  ring.mix(l, r, 128, 1.f);
  for (int i = 0; i < 128; ++i) {
    TEST_ASSERT_EQUAL_INT16(i * 10 + 1, l[i]);
    TEST_ASSERT_EQUAL_INT16(i * 10 + 2, r[i]);
  }
  TEST_ASSERT_EQUAL_UINT32(0, ring.avail());  // one read for both
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_resampler_same_rate_passes_through);
  RUN_TEST(test_resampler_doubles_and_interpolates);
  RUN_TEST(test_resampler_48k_length_and_bound);
  RUN_TEST(test_ring_plays_then_stops_when_finished);
  RUN_TEST(test_ring_underrun_is_silence_and_counted);
  RUN_TEST(test_ring_wraps_and_saturates);
  RUN_TEST(test_stopped_ring_adds_nothing);
  RUN_TEST(test_ring_mixes_into_both_channels);
  return UNITY_END();
}
