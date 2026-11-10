#include <math.h>
#include <string.h>
#include <unity.h>
#include <vector>
#include "onset.h"
#include "slices.h"

using namespace mt;
static_assert(sizeof(OnsetScan) <= 3628 + 1024, "onset analysis memory budget");

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t kRate = 32000;
constexpr uint32_t kLen = 64000;
int32_t acc[kLen];
int16_t data[kLen];
Onset o[kMaxOnsets];
uint32_t seed = 1;

int32_t rnd(int32_t amp) {  // LCG, -amp..amp
  seed = seed * 1664525u + 1013904223u;
  return static_cast<int32_t>((seed >> 8) % (2 * amp + 1)) - amp;
}

void clear(bool noise) {
  seed = 1;
  for (uint32_t i = 0; i < kLen; ++i) acc[i] = noise ? rnd(20) : 0;
}

// Decaying burst: |x| = amp * e^(-t/800), random sign.
void burst(uint32_t start, float amp) {
  for (uint32_t i = start; i < kLen; ++i) {
    const float a = amp * expf(-static_cast<float>(i - start) / 800.f);
    if (a < 1.f) break;
    acc[i] += static_cast<int32_t>(rnd(1) >= 0 ? a : -a);
  }
}

int detect(int max = kMaxOnsets) {
  for (uint32_t i = 0; i < kLen; ++i)
    data[i] = static_cast<int16_t>(acc[i] > 32767 ? 32767 : acc[i] < -32768 ? -32768 : acc[i]);
  return detectOnsets(data, kLen, kRate, o, max);
}

}  // namespace

void test_silence_has_no_onsets() {
  clear(false);
  TEST_ASSERT_EQUAL(0, detect());
}

void test_noise_floor_has_no_onsets() {
  clear(true);
  TEST_ASSERT_EQUAL(0, detect());
}

void test_clicks_found_at_their_positions() {
  clear(true);
  const uint32_t starts[3] = {4000, 16000, 40000};
  for (uint32_t s : starts) burst(s, 20000.f);
  TEST_ASSERT_EQUAL(3, detect());
  for (int i = 0; i < 3; ++i) {
    TEST_ASSERT_UINT32_WITHIN(64, starts[i], o[i].pos);
    TEST_ASSERT_TRUE(o[i].pos <= starts[i] + 32);
  }
}

void test_unaligned_click_refined_back() {
  clear(true);
  burst(10017, 15000.f);
  TEST_ASSERT_EQUAL(1, detect());
  TEST_ASSERT_TRUE(o[0].pos <= 10017 && o[0].pos + 32 > 10017);
}

void test_strengths_normalized() {
  clear(true);
  burst(4000, 20000.f);
  burst(30000, 4000.f);
  TEST_ASSERT_EQUAL(2, detect());
  TEST_ASSERT_EQUAL(1000, o[0].strength);
  TEST_ASSERT_TRUE(o[1].strength < 1000);
  TEST_ASSERT_TRUE(o[1].strength > 0);
}

void test_close_peaks_merged() {
  clear(true);
  burst(8000, 20000.f);
  burst(8400, 20000.f);
  TEST_ASSERT_EQUAL(1, detect());
  TEST_ASSERT_UINT32_WITHIN(64, 8000, o[0].pos);
}

void test_max_keeps_strongest() {
  clear(true);
  uint32_t starts[10];
  for (int i = 0; i < 10; ++i) {
    starts[i] = 2048 + static_cast<uint32_t>(i) * 6144;
    burst(starts[i], 20000.f / powf(1.4f, static_cast<float>(9 - i)));
  }
  TEST_ASSERT_EQUAL(10, detect());
  TEST_ASSERT_EQUAL(4, detect(4));
  for (int i = 0; i < 4; ++i) TEST_ASSERT_UINT32_WITHIN(64, starts[6 + i], o[i].pos);
}

void test_nearest_and_step() {
  const Onset h[3] = {{100, 1}, {500, 1}, {900, 1}};
  TEST_ASSERT_EQUAL(1, nearestOnset(h, 3, 480));
  TEST_ASSERT_EQUAL(0, nearestOnset(h, 3, 0));
  TEST_ASSERT_EQUAL(2, nearestOnset(h, 3, 5000));
  TEST_ASSERT_EQUAL(-1, nearestOnset(h, 0, 480));
  TEST_ASSERT_EQUAL(2, stepOnset(h, 3, 500, +1));
  TEST_ASSERT_EQUAL(0, stepOnset(h, 3, 500, -1));
  TEST_ASSERT_EQUAL(-1, stepOnset(h, 3, 900, +1));
  TEST_ASSERT_EQUAL(-1, stepOnset(h, 3, 100, -1));
  TEST_ASSERT_EQUAL(1, stepOnset(h, 3, 300, +1));
}

void test_chop_transients_threshold() {
  const Onset h[3] = {{8000, 1000}, {16000, 300}, {24000, 800}};
  const int expect[3][2] = {{100, 4}, {50, 3}, {0, 2}};
  for (const auto& e : expect) {
    Instrument m;
    m.start = 0;
    m.end = 0xFFFF;
    m.chopThresh = static_cast<uint8_t>(e[0]);
    chopTransients(m, h, 3, 32000, 32000);
    TEST_ASSERT_EQUAL(e[1], m.sliceCount);
    TEST_ASSERT_EQUAL(0, m.slices[0]);
    for (int i = 1; i < m.sliceCount; ++i) TEST_ASSERT_TRUE(m.slices[i - 1] < m.slices[i]);
  }
}

void test_chop_transients_inside_region_only() {
  const Onset h[4] = {{4000, 1000}, {8100, 1000}, {16000, 1000}, {24000, 1000}};
  Instrument m;
  m.start = 0x4000;  // frame 8000
  m.end = 0xC000;    // frame 24000
  m.chopThresh = 100;
  chopTransients(m, h, 4, 32000, 32000);
  TEST_ASSERT_EQUAL(2, m.sliceCount);
  TEST_ASSERT_EQUAL_HEX16(m.start, m.slices[0]);
  TEST_ASSERT_UINT32_WITHIN(1, 16000, fracToFrame(m.slices[1], 32000));
}

void test_chop_transients_keeps_strongest() {
  Onset h[kMaxOnsets];
  for (int i = 0; i < kMaxOnsets; ++i)
    h[i] = {static_cast<uint32_t>(1000 + i * 1000), static_cast<uint16_t>(i + 1)};
  Instrument m;
  m.chopThresh = 100;
  m.slices[5] = 123;
  constexpr uint32_t length = 2 * 0xFFFFu;  // fractions are exact for these even frame positions
  chopTransients(m, h, kMaxOnsets, length, 32000);
  TEST_ASSERT_EQUAL(kMaxSlices, m.sliceCount);
  TEST_ASSERT_EQUAL(0, m.slices[0]);
  const uint32_t firstKept = (kMaxOnsets - kMaxSlices + 2) * 1000;
  TEST_ASSERT_UINT32_WITHIN(1, firstKept, fracToFrame(m.slices[1], length));
  TEST_ASSERT_UINT32_WITHIN(1, kMaxOnsets * 1000, fracToFrame(m.slices[kMaxSlices - 1], length));
  for (int i = 1; i < m.sliceCount; ++i) TEST_ASSERT_TRUE(m.slices[i - 1] < m.slices[i]);
}

static std::vector<int16_t> clicks(uint32_t n, uint32_t every) {
  std::vector<int16_t> v(n, 0);
  uint32_t x = 3;
  for (uint32_t i = 0; i < n; ++i) {
    x = x * 1664525u + 1013904223u;
    const uint32_t ph = i % every;
    const int amp = ph < 2000 ? 20000 - static_cast<int>(ph) * 10 : 30;
    v[i] = static_cast<int16_t>((static_cast<int>(x >> 16) - 32768) * amp / 32768);
  }
  return v;
}

void test_scan_in_pieces_equals_detect() {
  const auto d = clicks(200000, 7919);
  Onset a[kMaxOnsets], b[kMaxOnsets];
  const int na = detectOnsets(d.data(), 200000, 44100, a, kMaxOnsets);
  TEST_ASSERT_TRUE(na > 10);
  OnsetScan s(200000, 44100, kMaxOnsets);
  uint32_t at = 0, piece = 1;
  while (at < 200000) {  // ragged pieces: 1, 2, 3, ... frames up to 997
    const uint32_t n = at + piece <= 200000 ? piece : 200000 - at;
    s.feed(d.data() + at, n);
    at += n;
    piece = piece % 997 + 1;
  }
  TEST_ASSERT_EQUAL(na, s.finish(b));
  for (int i = 0; i < na; ++i) {
    TEST_ASSERT_EQUAL_UINT32(a[i].pos, b[i].pos);
    TEST_ASSERT_EQUAL_UINT16(a[i].strength, b[i].strength);
  }
}

void test_scan_short_input_finds_nothing() {
  OnsetScan s(100, 44100, kMaxOnsets);
  int16_t z[100] = {};
  s.feed(z, 100);
  Onset o[1];
  TEST_ASSERT_EQUAL(0, s.finish(o));
}

// Time-based fixtures: the same acoustic signal at every supported sample rate.
static std::vector<int16_t> acoustic(uint32_t rate, int kind) {
  std::vector<double> sum(rate * 3, 0);
  if (kind == 0) for (double& v : sum) v = 12000;  // constant DC
  if (kind == 1 || kind == 2 || kind == 4) {
    const double hz = kind == 1 ? 20 : kind == 2 ? 40 : 220;
    for (size_t i = 0; i < sum.size(); ++i) sum[i] = 10000 * sin(6.283185307179586 * hz * i / rate);
  }
  auto hit = [&](double when, double amp, double tau, bool tonal) {
    uint32_t random = 7;
    for (size_t i = static_cast<size_t>(when * rate); i < sum.size(); ++i) {
      const double time = double(i) / rate - when;
      const double envelope = amp * exp(-time / tau);
      if (envelope < .2) break;
      random = random * 1664525u + 1013904223u;
      const double carrier = tonal ? sin(6.283185307179586 * 80 * time) :
          (static_cast<int32_t>(random >> 16) - 32768) / 32768.;
      sum[i] += envelope * carrier;
    }
  };
  if (kind == 3) { hit(.25, 26000, .25, true); hit(.55, 3000, .012, false); }
  if (kind == 4 || kind == 5)
    for (double when : {.25, .75, 1.25, 1.75, 2.25, 2.75}) hit(when, kind == 4 ? 14000 : 22000, kind == 4 ? .004 : .025, false);
  std::vector<int16_t> data(sum.size());
  for (size_t i = 0; i < sum.size(); ++i)
    data[i] = static_cast<int16_t>(sum[i] > 32767 ? 32767 : sum[i] < -32768 ? -32768 : sum[i]);
  return data;
}
static void test_steady_low_tones_never_explode_into_slices() {
  for (uint32_t rate : {22050u, 32000u, 44100u})
    for (int kind : {1, 2}) {
      const auto data = acoustic(rate, kind);
      TEST_ASSERT_LESS_OR_EQUAL(1, detectOnsets(data.data(), data.size(), rate, o, kMaxOnsets));
    }
}
static void test_constant_dc_has_no_attacks() {
  for (uint32_t rate : {22050u, 32000u, 44100u}) {
    const auto data = acoustic(rate, 0);
    TEST_ASSERT_EQUAL(0, detectOnsets(data.data(), data.size(), rate, o, kMaxOnsets));
  }
}
static void checkAcousticHits(int kind, int hits) {
  for (uint32_t rate : {22050u, 32000u, 44100u}) {
    const auto data = acoustic(rate, kind);
    const int n = detectOnsets(data.data(), data.size(), rate, o, kMaxOnsets);
    for (int hit = 0; hit < hits; ++hit) {
      const uint32_t expected = static_cast<uint32_t>(rate * (.25 + hit * (kind == 3 ? .3 : .5)));
      const int near = nearestOnset(o, n, expected);
      TEST_ASSERT_GREATER_OR_EQUAL(0, near);
      TEST_ASSERT_UINT32_WITHIN(rate * 3 / 1000, expected, o[near].pos);
    }
    TEST_ASSERT_LESS_OR_EQUAL(hits + 1, n);  // a tone's initial attack is allowed, no repeated false attacks
  }
}
static void test_quiet_hat_after_loud_kick_is_separate_and_refined() { checkAcousticHits(3, 2); }
static void test_attacks_on_a_steady_tone_are_not_masked() { checkAcousticHits(4, 6); }
static void test_drum_timing_at_all_sample_rates() { checkAcousticHits(5, 6); }

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_silence_has_no_onsets);
  RUN_TEST(test_noise_floor_has_no_onsets);
  RUN_TEST(test_clicks_found_at_their_positions);
  RUN_TEST(test_unaligned_click_refined_back);
  RUN_TEST(test_strengths_normalized);
  RUN_TEST(test_close_peaks_merged);
  RUN_TEST(test_max_keeps_strongest);
  RUN_TEST(test_nearest_and_step);
  RUN_TEST(test_chop_transients_threshold);
  RUN_TEST(test_chop_transients_inside_region_only);
  RUN_TEST(test_chop_transients_keeps_strongest);
  RUN_TEST(test_scan_in_pieces_equals_detect);
  RUN_TEST(test_scan_short_input_finds_nothing);
  RUN_TEST(test_steady_low_tones_never_explode_into_slices);
  RUN_TEST(test_constant_dc_has_no_attacks);
  RUN_TEST(test_quiet_hat_after_loud_kick_is_separate_and_refined);
  RUN_TEST(test_attacks_on_a_steady_tone_are_not_masked);
  RUN_TEST(test_drum_timing_at_all_sample_rates);
  return UNITY_END();
}
