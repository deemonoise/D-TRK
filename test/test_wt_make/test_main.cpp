#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>
#include <vector>
#include "project_io.h"
#include "sample_set.h"
#include "wav.h"
#include "wt_file.h"
#include "wt_make.h"

using namespace mt;

void setUp() {}
void tearDown() {}

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int N = kWtFrameLen;

float peakOf(const float* f, int n) {
  float p = 0;
  for (int i = 0; i < n; ++i) p = fabsf(f[i]) > p ? fabsf(f[i]) : p;
  return p;
}

double rmsOf(const int16_t* f, int n) {
  double m = 0, s = 0;
  for (int i = 0; i < n; ++i) m += f[i];
  m /= n;
  for (int i = 0; i < n; ++i) s += (f[i] - m) * (f[i] - m);
  return sqrt(s / n);
}

int peak16(const int16_t* f, int n) {
  int p = 0;
  for (int i = 0; i < n; ++i) p = abs(f[i]) > p ? abs(f[i]) : p;
  return p;
}

// Periodic test signals at hz, amplitude a.
enum class Sig { Sin, Saw, Sqr };
std::vector<int16_t> tone(Sig w, double hz, double rate, uint32_t n, double a = 20000, double decay = 0,
                          double dc = 0) {
  std::vector<int16_t> x(n);
  for (uint32_t i = 0; i < n; ++i) {
    const double ph = fmod(i * hz / rate, 1.0);
    double v = w == Sig::Sin ? sin(2 * kPi * ph) : (w == Sig::Saw ? 2 * ph - 1 : (ph < 0.5 ? 1 : -1));
    v = v * a * exp(-decay * i / rate) + dc;
    x[i] = static_cast<int16_t>(lrint(v < -32767 ? -32767 : (v > 32767 ? 32767 : v)));
  }
  return x;
}

std::vector<int16_t> noise(uint32_t n, uint32_t seed = 1) {
  std::vector<int16_t> x(n);
  for (uint32_t i = 0; i < n; ++i) {
    seed = seed * 1664525u + 1013904223u;
    x[i] = static_cast<int16_t>((seed >> 16) & 0x7FFF) - 16384;
  }
  return x;
}

float seamErr(const int16_t* f) {  // |extrapolated next point - f[0]|
  return static_cast<float>(abs(f[N - 1] + (f[N - 1] - f[N - 2]) - f[0]));
}

struct VecSink : ByteSink {
  std::vector<uint8_t> b;
  bool write(const void* d, size_t n) override {
    const uint8_t* p = static_cast<const uint8_t*>(d);
    b.insert(b.end(), p, p + n);
    return true;
  }
};
struct VecSource : ByteSource {
  const std::vector<uint8_t>& b;
  size_t at = 0;
  explicit VecSource(const std::vector<uint8_t>& v) : b(v) {}
  bool read(void* d, size_t n) override {
    if (at + n > b.size()) return false;
    memcpy(d, b.data() + at, n);
    at += n;
    return true;
  }
  bool skip(size_t n) override {
    if (at + n > b.size()) return false;
    at += n;
    return true;
  }
};

}  // namespace

// --- resample, shapes, keys

void test_resample_constant() {
  std::vector<float> x(3000, 0.25f);
  float out[N];
  wtResample(x.data(), 3000, 17.3, 2111.7, out);
  for (float v : out) TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.25f, v);
  wtResample(x.data(), 3000, 5.5, 100.2, out);
  for (float v : out) TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.25f, v);
}

void test_resample_identity_256() {
  float x[N], out[N];
  for (int i = 0; i < N; ++i) x[i] = static_cast<float>(sin(2 * kPi * i / N));
  wtResample(x, N, 0, N, out);
  for (int i = 0; i < N; ++i) TEST_ASSERT_FLOAT_WITHIN(1e-6f, x[i], out[i]);
}

void test_resample_long_sine() {
  std::vector<float> x(1024);
  for (int i = 0; i < 1024; ++i) x[i] = static_cast<float>(20000 * sin(2 * kPi * i / 1024));
  float out[N];
  wtResample(x.data(), 1024, 0, 1024, out);
  for (int i = 0; i < N; ++i) TEST_ASSERT_FLOAT_WITHIN(4.0f, static_cast<float>(20000 * sin(2 * kPi * i / N)), out[i]);
}

void test_resample_area_no_alias() {
  // 300 cycles over 4096 samples: far above the 128-harmonic Nyquist of the target.
  std::vector<float> x(4096);
  for (int i = 0; i < 4096; ++i) x[i] = static_cast<float>(sin(2 * kPi * 300.0 * i / 4096 + 0.3));
  float out[N];
  wtResample(x.data(), 4096, 0, 4096, out);
  TEST_ASSERT_TRUE(peakOf(out, N) < 0.15f);
}

void test_resample_cubic_hits_samples() {
  float x[128], out[N];
  for (int i = 0; i < 128; ++i) x[i] = static_cast<float>(sin(2 * kPi * i / 128) + 0.1 * i);
  wtResample(x, 128, 0, 128, out);
  for (int i = 0; i < 128; ++i) TEST_ASSERT_FLOAT_WITHIN(1e-5f, x[i], out[2 * i]);
}

void test_shapes() {
  float f[N];
  for (int s = 0; s < static_cast<int>(WtShape::Count); ++s) {
    wtShape(static_cast<WtShape>(s), f);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.0f, peakOf(f, N));
  }
  wtShape(WtShape::Sin, f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, f[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, f[64]);
  wtShape(WtShape::Sqr, f);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, f[10]);
  TEST_ASSERT_EQUAL_FLOAT(-1.0f, f[200]);
}

void test_smooth() {
  float f[N];
  for (float& v : f) v = 0.5f;
  wtSmooth(f);
  for (float v : f) TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, v);
  for (float& v : f) v = 0;
  f[0] = 1;
  wtSmooth(f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, f[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.25f, f[1]);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.25f, f[N - 1]);
}

void test_from_keys() {
  static WtKey keys[3];
  keys[0].frame = 63;
  keys[1].frame = 0;
  keys[2].frame = 21;
  for (int i = 0; i < N; ++i) {
    keys[0].pts[i] = -0.5f;
    keys[1].pts[i] = 0.5f;
    keys[2].pts[i] = 1.0f;
  }
  static int16_t src[kWtSrcSamples];
  TEST_ASSERT_TRUE(wtFromKeys(keys, 3, src));
  TEST_ASSERT_EQUAL(0, keys[0].frame);  // sorted
  TEST_ASSERT_EQUAL(21, keys[1].frame);
  TEST_ASSERT_EQUAL(63, keys[2].frame);
  TEST_ASSERT_EQUAL(kWtPeak / 2, src[0]);
  TEST_ASSERT_EQUAL(kWtPeak, src[21 * N + 5]);
  TEST_ASSERT_EQUAL(-kWtPeak / 2, src[63 * N + 7]);
  // Midpoint of 21 (1.0) and 63 (-0.5) at 42: 0.25.
  TEST_ASSERT_INT_WITHIN(1, kWtPeak / 4, src[42 * N]);
  float f[N];
  wtKeyAt(keys, 3, 10, f);  // 0.5 + 10/21 * 0.5
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.5f + 10.0f / 21 * 0.5f, f[3]);
}

void test_from_keys_needs_ends() {
  static WtKey keys[2];
  static int16_t src[kWtSrcSamples];
  keys[0].frame = 0;
  keys[1].frame = 62;
  TEST_ASSERT_FALSE(wtFromKeys(keys, 2, src));
  keys[1].frame = 63;
  TEST_ASSERT_TRUE(wtFromKeys(keys, 2, src));
}

// --- pitch

void test_yin_periods() {
  const Sig waves[] = {Sig::Sin, Sig::Saw, Sig::Sqr};
  const double hzs[] = {55, 110, 440, 1760};
  const double rates[] = {44100, 22050};
  static float x[4000], d[1200], dn[1200];
  for (double rate : rates)
    for (Sig w : waves)
      for (double hz : hzs) {
        // A saw / square of ~12 samples a period is all aliases: YIN locks to two periods there.
        if (w != Sig::Sin && rate / hz < 20) continue;
        const int tauMax = static_cast<int>(rate / kWtMinHz);
        const int tauMin = static_cast<int>(rate / kWtMaxHz);
        const int win = static_cast<int>(2.5 * tauMax) + 2 - tauMax - 1;
        const std::vector<int16_t> s = tone(w, hz, rate, win + tauMax + 1);
        for (int i = 0; i < win + tauMax + 1; ++i) x[i] = s[i];
        float p = 0, c = 0;
        TEST_ASSERT_TRUE(wtYin(x, win, tauMin, tauMax, d, dn, p, c));
        // Saw / square: d is V-shaped at the period (jumps), the parabola is good to a fraction of a sample.
        TEST_ASSERT_FLOAT_WITHIN(w == Sig::Sin ? 0.1f : 0.3f, static_cast<float>(rate / hz), p);
        TEST_ASSERT_TRUE(c > 0.85f);
      }
}

void test_yin_noise_no_pitch() {
  static float x[4000], d[1200], dn[1200];
  const std::vector<int16_t> s = noise(4000, 7);
  for (int i = 0; i < 4000; ++i) x[i] = s[i];
  float p = 0, c = 0;
  TEST_ASSERT_FALSE(wtYin(x, 1650, 22, 1102, d, dn, p, c));
}

void test_cum_norm() {
  float d[5] = {0, 2, 2, 2, 2}, dn[5];
  wtCumNorm(d, dn, 4);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, dn[0]);
  for (int t = 1; t < 5; ++t) TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, dn[t]);
}

// --- maker

void test_too_short() {
  const std::vector<int16_t> x = tone(Sig::Sin, 220, 44100, 5000);
  static int16_t out[kWtSrcSamples];
  WtMakeParams p;
  TEST_ASSERT_EQUAL(static_cast<int>(WtMakeErr::TooShort),
                    static_cast<int>(wtMakeArray(x.data(), 5000, 100, 100 + 2047, 44100, p, out)));
  p.mode = WtMode::Window;
  TEST_ASSERT_EQUAL(static_cast<int>(WtMakeErr::TooShort),
                    static_cast<int>(wtMakeArray(x.data(), 5000, 0, 1000, 44100, p, out)));
  TEST_ASSERT_EQUAL(static_cast<int>(WtMakeErr::Ok), static_cast<int>(wtMakeArray(x.data(), 5000, 0, 2048, 44100, p, out)));
}

void test_cycle_sine_starts_at_rising_zero() {
  const std::vector<int16_t> x = tone(Sig::Sin, 220, 44100, 44100);
  static int16_t out[kWtSrcSamples];
  WtMakeParams p;
  int pitched = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(WtMakeErr::Ok),
                    static_cast<int>(wtMakeArray(x.data(), 44100, 1000, 40000, 44100, p, out, &pitched)));
  TEST_ASSERT_EQUAL(64, pitched);
  TEST_ASSERT_INT_WITHIN(1, kWtMakePeak, peak16(out, kWtSrcSamples));
  for (int k = 0; k < kWtFrames; k += 9) {
    const int16_t* f = out + k * N;
    TEST_ASSERT_TRUE(abs(f[0]) < kWtMakePeak / 100);
    TEST_ASSERT_TRUE(f[1] > f[0]);
    double num = 0, a = 0, b = 0;
    for (int i = 0; i < N; ++i) {
      const double s = sin(2 * kPi * i / N);
      num += f[i] * s;
      a += f[i] * f[i];
      b += s * s;
    }
    TEST_ASSERT_TRUE(num / sqrt(a * b) > 0.999);
  }
}

void test_cycle_glissando() {
  // 220 -> 440 Hz over 2 s: the frames stay one cycle each (one rising zero per frame).
  const uint32_t n = 88200;
  std::vector<int16_t> x(n);
  double ph = 0;
  for (uint32_t i = 0; i < n; ++i) {
    x[i] = static_cast<int16_t>(lrint(20000 * sin(2 * kPi * ph)));
    ph += (220.0 + 220.0 * i / n) / 44100;
  }
  static int16_t out[kWtSrcSamples];
  WtMakeParams p;
  int pitched = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(WtMakeErr::Ok),
                    static_cast<int>(wtMakeArray(x.data(), n, 0, n, 44100, p, out, &pitched)));
  TEST_ASSERT_TRUE(pitched >= 60);
  for (int k = 0; k < kWtFrames; ++k) {
    const int16_t* f = out + k * N;
    int rises = 0;
    for (int i = 0; i < N; ++i) rises += f[i] < 0 && f[(i + 1) % N] >= 0;
    TEST_ASSERT_EQUAL(1, rises);
  }
}

void test_cycle_noise_no_pitch() {
  const std::vector<int16_t> x = noise(40000, 3);
  static int16_t out[kWtSrcSamples];
  WtMakeParams p;
  TEST_ASSERT_EQUAL(static_cast<int>(WtMakeErr::NoPitch),
                    static_cast<int>(wtMakeArray(x.data(), 40000, 0, 40000, 44100, p, out)));
}

void test_seam_cycle_decaying() {
  const std::vector<int16_t> x = tone(Sig::Saw, 137, 44100, 44100, 20000, 3.0);
  static int16_t out[kWtSrcSamples];
  WtMakeParams p;
  p.norm = WtNorm::Table;
  TEST_ASSERT_EQUAL(static_cast<int>(WtMakeErr::Ok),
                    static_cast<int>(wtMakeArray(x.data(), 44100, 0, 44100, 44100, p, out)));
  // A saw jumps inside the cycle, not at the seam: the cycle starts at its steep rising zero.
  const std::vector<int16_t> s = tone(Sig::Sin, 137, 44100, 44100, 20000, 3.0);
  TEST_ASSERT_EQUAL(static_cast<int>(WtMakeErr::Ok),
                    static_cast<int>(wtMakeArray(s.data(), 44100, 0, 44100, 44100, p, out)));
  for (int k = 0; k < kWtFrames; ++k) TEST_ASSERT_TRUE(seamErr(out + k * N) < 0.02f * peak16(out + k * N, N) + 2);
}

void test_seam_window() {
  // 3.33 cycles per window: without the seam every frame ends far from its start.
  const uint32_t n = 64 * 1000;
  const std::vector<int16_t> x = tone(Sig::Sin, 44100.0 / 300, 44100, n);
  static int16_t on[kWtSrcSamples], off[kWtSrcSamples];
  WtMakeParams p;
  p.mode = WtMode::Window;
  TEST_ASSERT_EQUAL(static_cast<int>(WtMakeErr::Ok), static_cast<int>(wtMakeArray(x.data(), n, 0, n, 44100, p, on)));
  p.seam = false;
  TEST_ASSERT_EQUAL(static_cast<int>(WtMakeErr::Ok), static_cast<int>(wtMakeArray(x.data(), n, 0, n, 44100, p, off)));
  int bad = 0;
  for (int k = 0; k < kWtFrames; ++k) {
    TEST_ASSERT_TRUE(seamErr(on + k * N) < 0.02f * kWtMakePeak);
    bad += seamErr(off + k * N) > 0.1f * kWtMakePeak;
  }
  TEST_ASSERT_TRUE(bad > 30);
}

void test_window_short_region_cubic() {
  // 4096 samples: 64 points a window, upsampled 4x; a sine of 64 samples per window is one cycle.
  const std::vector<int16_t> x = tone(Sig::Sin, 44100.0 / 64, 44100, 4096);
  static int16_t out[kWtSrcSamples];
  WtMakeParams p;
  p.mode = WtMode::Window;
  p.seam = false;
  TEST_ASSERT_EQUAL(static_cast<int>(WtMakeErr::Ok), static_cast<int>(wtMakeArray(x.data(), 4096, 0, 4096, 44100, p, out)));
  for (int i = 0; i < N; i += 16)
    TEST_ASSERT_INT_WITHIN(300, static_cast<int>(kWtMakePeak * sin(2 * kPi * i / N)), out[20 * N + i]);
}

void test_dc_removed_by_import() {
  const std::vector<int16_t> x = tone(Sig::Sin, 220, 44100, 30000, 10000, 0, 8000);
  static int16_t out[kWtSrcSamples], table[kWtTableSamples];
  WtMakeParams p;
  TEST_ASSERT_EQUAL(static_cast<int>(WtMakeErr::Ok), static_cast<int>(wtMakeArray(x.data(), 30000, 0, 30000, 44100, p, out)));
  TEST_ASSERT_EQUAL(static_cast<int>(WtErr::Ok), static_cast<int>(wtImport(out, kWtSrcSamples, N, table)));
  double m = 0;
  for (int i = 0; i < N; ++i) m += table[i];
  TEST_ASSERT_TRUE(fabs(m / N) < 50);
}

void test_norm_frame_and_table() {
  const std::vector<int16_t> x = tone(Sig::Sin, 220, 44100, 88200, 30000, 1.2);  // -21 dB over 2 s (the gain ceiling is 24 dB)
  static int16_t out[kWtSrcSamples];
  WtMakeParams p;
  TEST_ASSERT_EQUAL(static_cast<int>(WtMakeErr::Ok), static_cast<int>(wtMakeArray(x.data(), 88200, 0, 88200, 44100, p, out)));
  const double r0 = rmsOf(out, N), r63 = rmsOf(out + 63 * N, N);
  TEST_ASSERT_TRUE(fabs(20 * log10(r0 / r63)) < 1.0);
  p.norm = WtNorm::Table;
  TEST_ASSERT_EQUAL(static_cast<int>(WtMakeErr::Ok), static_cast<int>(wtMakeArray(x.data(), 88200, 0, 88200, 44100, p, out)));
  TEST_ASSERT_TRUE(rmsOf(out, N) > 10 * rmsOf(out + 63 * N, N));
  TEST_ASSERT_INT_WITHIN(1, kWtMakePeak, peak16(out, kWtSrcSamples));
}

void test_norm_frame_silence_copies() {
  std::vector<int16_t> x = tone(Sig::Sin, 44100.0 / 300, 44100, 64000);
  for (uint32_t i = 32000; i < 64000; ++i) x[i] = (i & 1) ? 3 : -3;  // below -60 dBFS
  static int16_t out[kWtSrcSamples];
  WtMakeParams p;
  p.mode = WtMode::Window;
  TEST_ASSERT_EQUAL(static_cast<int>(WtMakeErr::Ok), static_cast<int>(wtMakeArray(x.data(), 64000, 0, 64000, 44100, p, out)));
  for (int k = 32; k < kWtFrames; ++k) TEST_ASSERT_EQUAL_INT16_ARRAY(out + 31 * N, out + k * N, N);
  for (uint32_t i = 0; i < 64000; ++i) x[i] = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(WtMakeErr::Silent), static_cast<int>(wtMakeArray(x.data(), 64000, 0, 64000, 44100, p, out)));
}

void test_pieces_same_result() {
  const std::vector<int16_t> x = tone(Sig::Saw, 180, 44100, 60000, 15000, 1.0);
  static int16_t a[kWtSrcSamples], b[kWtSrcSamples];
  for (int m = 0; m < 2; ++m) {
    WtMakeParams p;
    p.mode = static_cast<WtMode>(m);
    TEST_ASSERT_EQUAL(0, static_cast<int>(wtMakeArray(x.data(), 60000, 500, 59000, 44100, p, a)));
    TEST_ASSERT_EQUAL(0, static_cast<int>(wtMakeArray(x.data(), 60000, 500, 59000, 44100, p, b, nullptr, 1000)));
    TEST_ASSERT_EQUAL_INT16_ARRAY(a, b, kWtSrcSamples);
    TEST_ASSERT_EQUAL(0, static_cast<int>(wtMakeArray(x.data(), 60000, 500, 59000, 44100, p, b, nullptr, 333)));
    TEST_ASSERT_EQUAL_INT16_ARRAY(a, b, kWtSrcSamples);
  }
}

void test_progress() {
  const std::vector<int16_t> x = tone(Sig::Sin, 220, 44100, 30000);
  WtMaker m;
  WtMakeParams p;
  TEST_ASSERT_EQUAL(0, static_cast<int>(m.begin(p, 0, 30000, 44100)));
  TEST_ASSERT_EQUAL(0, m.progress());
  int last = 0;
  while (!m.done()) {
    uint32_t at, n;
    if (m.nextRead(at, n)) m.feed(x.data() + at, n);
    else TEST_ASSERT_TRUE(m.step());
    TEST_ASSERT_TRUE(m.progress() >= last);
    last = m.progress();
  }
  TEST_ASSERT_EQUAL(kWtFrames, m.progress());
  TEST_ASSERT_NOT_NULL(m.result());
  TEST_ASSERT_FALSE(m.step());
}

// --- WAV

void test_wav_write_round_trip() {
  static int16_t src[kWtSrcSamples], table[kWtTableSamples];
  for (int i = 0; i < kWtSrcSamples; ++i) src[i] = static_cast<int16_t>(lrint(20000 * sin(2 * kPi * i / N)));
  VecSink s;
  TEST_ASSERT_TRUE(wtWavWrite(src, s));
  TEST_ASSERT_EQUAL(kWtWavBytes, s.b.size());
  VecSource in(s.b);
  WavInfo w;
  TEST_ASSERT_EQUAL(static_cast<int>(WavErr::Ok), static_cast<int>(wavParse(in, w)));
  TEST_ASSERT_EQUAL(1, w.channels);
  TEST_ASSERT_EQUAL(16, w.bits);
  TEST_ASSERT_EQUAL(static_cast<uint32_t>(kWtSrcSamples), w.frames());
  TEST_ASSERT_EQUAL(N, w.clmFrame);
  TEST_ASSERT_TRUE(w.hasCrc);
  TEST_ASSERT_EQUAL_HEX32(sampleCrc(src, kWtSrcSamples), w.crc);
  TEST_ASSERT_EQUAL(kWtWavBytes - kWtSrcSamples * 2, w.dataOffset);
  TEST_ASSERT_EQUAL_MEMORY(src, s.b.data() + w.dataOffset, kWtSrcSamples * 2);
  uint32_t riff;
  memcpy(&riff, s.b.data() + 4, 4);
  TEST_ASSERT_EQUAL(kWtWavBytes - 8, riff);
  WtFormat f;
  TEST_ASSERT_EQUAL(static_cast<int>(WtErr::Ok), static_cast<int>(wtDetect(w.frames(), w.clmFrame, f)));
  TEST_ASSERT_EQUAL(N, f.frameLen);
  TEST_ASSERT_EQUAL(64, f.frames);
  TEST_ASSERT_EQUAL(static_cast<int>(WtErr::Ok), static_cast<int>(wtImport(src, kWtSrcSamples, w.clmFrame, table)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_resample_constant);
  RUN_TEST(test_resample_identity_256);
  RUN_TEST(test_resample_long_sine);
  RUN_TEST(test_resample_area_no_alias);
  RUN_TEST(test_resample_cubic_hits_samples);
  RUN_TEST(test_shapes);
  RUN_TEST(test_smooth);
  RUN_TEST(test_from_keys);
  RUN_TEST(test_from_keys_needs_ends);
  RUN_TEST(test_yin_periods);
  RUN_TEST(test_yin_noise_no_pitch);
  RUN_TEST(test_cum_norm);
  RUN_TEST(test_too_short);
  RUN_TEST(test_cycle_sine_starts_at_rising_zero);
  RUN_TEST(test_cycle_glissando);
  RUN_TEST(test_cycle_noise_no_pitch);
  RUN_TEST(test_seam_cycle_decaying);
  RUN_TEST(test_seam_window);
  RUN_TEST(test_window_short_region_cubic);
  RUN_TEST(test_dc_removed_by_import);
  RUN_TEST(test_norm_frame_and_table);
  RUN_TEST(test_norm_frame_silence_copies);
  RUN_TEST(test_pieces_same_result);
  RUN_TEST(test_progress);
  RUN_TEST(test_wav_write_round_trip);
  return UNITY_END();
}
