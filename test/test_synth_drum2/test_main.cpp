// Drum2Voice: the eight newer DRUM machines (BDA .. HHR), DSP level (no Synth, no model).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <type_traits>
#include <unity.h>
#include <vector>
#include "synth_drum2.h"
#include "synth_osc.h"

using namespace mt;

void setUp() {}
void tearDown() {}

namespace {

constexpr int kFs = kSynthRate;
constexpr int kCtl = 32;  // control period, as in Synth

int ms(float t) { return static_cast<int>(t * kFs / 1000.f); }

// One hit of machine k: trigger, control every 32 samples, render in 32-sample blocks. *doneAtEnd: done()
// after the last block.
std::vector<float> hit(Drum2Kind k, const float mac[5], float note, float vel, int frames, bool* doneAtEnd = nullptr,
                       const float* macAfter = nullptr) {
  Drum2Voice v{};
  v.trigger(k, vel / 127.f, false);
  Drum2Params p;
  p.hz = drum2BaseHz(k) * powf(2.f, (note - 60.f) / 12.f);
  for (int i = 0; i < 5; ++i) p.mac[i] = mac[i];
  std::vector<float> out(static_cast<size_t>(frames), 0.f);
  for (int i = 0; i < frames; i += kCtl) {
    if (macAfter && i == kCtl)
      for (int j = 0; j < 5; ++j) p.mac[j] = macAfter[j];
    const int n = frames - i < kCtl ? frames - i : kCtl;
    v.control(p, n);
    v.render(out.data() + i, n, 1.f);
  }
  if (doneAtEnd) *doneAtEnd = v.done();
  return out;
}

bool finite(const std::vector<float>& x) {
  for (float s : x)
    if (!(s == s) || s > 1e6f || s < -1e6f) return false;
  return true;
}

float peak(const std::vector<float>& x, int from = 0, int to = -1) {
  if (to < 0) to = static_cast<int>(x.size());
  float m = 0;
  for (int i = from; i < to; ++i) m = fabsf(x[i]) > m ? fabsf(x[i]) : m;
  return m;
}

float rmsDb(const std::vector<float>& x, int from, int to) {
  double e = 0;
  for (int i = from; i < to; ++i) e += static_cast<double>(x[i]) * x[i];
  return 10.f * static_cast<float>(log10(e / (to - from) + 1e-24));
}

float zeroCrossHz(const std::vector<float>& x, int from, int to) {
  int n = 0;
  for (int i = from + 1; i < to; ++i)
    if ((x[i - 1] < 0) != (x[i] < 0)) ++n;
  return n * 0.5f * kFs / (to - from);
}

// Magnitude spectrum of 1024 samples from `from` (Hann), bins 0..511.
std::vector<float> spectrum(const std::vector<float>& x, int from) {
  constexpr int N = 1024;
  std::vector<float> w(N), m(N / 2);
  for (int i = 0; i < N; ++i) {
    const int j = from + i;
    const float s = j < static_cast<int>(x.size()) ? x[j] : 0.f;
    w[i] = s * (0.5f - 0.5f * cosf(6.2831853f * i / N));
  }
  for (int b = 0; b < N / 2; ++b) {
    double re = 0, im = 0;
    for (int i = 0; i < N; ++i) {
      const double a = 6.283185307179586 * b * i / N;
      re += w[i] * cos(a);
      im -= w[i] * sin(a);
    }
    m[b] = static_cast<float>(sqrt(re * re + im * im));
  }
  return m;
}

float centroidHz(const std::vector<float>& x, int from) {
  const std::vector<float> m = spectrum(x, from);
  double a = 0, b = 0;
  for (size_t i = 1; i < m.size(); ++i) {
    a += i * static_cast<double>(m[i]) * m[i];  // power-weighted
    b += static_cast<double>(m[i]) * m[i];
  }
  return b > 0 ? static_cast<float>(a / b) * kFs / 1024.f : 0.f;
}

// Energy (power) above hz in the 1024 samples from `from`.
float energyAbove(const std::vector<float>& x, int from, float hz) {
  const std::vector<float> m = spectrum(x, from);
  double e = 0;
  for (size_t i = static_cast<size_t>(hz * 1024 / kFs); i < m.size(); ++i) e += static_cast<double>(m[i]) * m[i];
  return static_cast<float>(e);
}

void defaults(const uint8_t d[5], float out[5]) {
  for (int i = 0; i < 5; ++i) out[i] = d[i] / 127.f;
}

struct Spec {
  Drum2Kind k;
  uint8_t def[5];      // knob defaults, 0..127
  bool tonal;          // pitch test by zero crossings, else by spectral centroid
  float pitchFrom, pitchTo;  // ms, tonal window
  int quiet;           // knob zeroed for the tonal pitch test (noise), -1 none
};

void checkMachine(const Spec& s) {
  char msg[64];
  float d[5];
  defaults(s.def, d);
  // All knobs up, full velocity: finite, within +-1.
  const float max[5] = {1, 1, 1, 1, 1};
  std::vector<float> a = hit(s.k, max, 60, 127, kFs);
  TEST_ASSERT_TRUE(finite(a));
  TEST_ASSERT_TRUE(peak(a) <= 1.f);
  // Also at the extremes of the pitch range.
  a = hit(s.k, max, 96, 127, kFs / 2);
  TEST_ASSERT_TRUE(finite(a));
  a = hit(s.k, max, 24, 127, kFs / 2);
  TEST_ASSERT_TRUE(finite(a));
  const float zero[5] = {0, 0, 0, 0, 0};
  a = hit(s.k, zero, 60, 127, kFs / 2);
  TEST_ASSERT_TRUE(finite(a));
  // Defaults: audible.
  a = hit(s.k, d, 60, 127, kFs);
  TEST_ASSERT_TRUE(finite(a));
  snprintf(msg, sizeof(msg), "default peak %f", peak(a));
  TEST_ASSERT_TRUE_MESSAGE(peak(a) > 0.05f, msg);
  TEST_ASSERT_TRUE_MESSAGE(peak(a) <= 1.f, msg);
  // Determinism.
  const std::vector<float> b = hit(s.k, d, 60, 127, kFs);
  TEST_ASSERT_TRUE(memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
  // Short decay: below -60 dB by 1 s, done.
  float dd[5];
  memcpy(dd, d, sizeof(dd));
  dd[0] = 0.3f;
  bool done = false;
  a = hit(s.k, dd, 60, 127, kFs, &done);
  snprintf(msg, sizeof(msg), "decay 0.3: %f dB at 1 s", rmsDb(a, kFs - 2048, kFs));
  TEST_ASSERT_TRUE_MESSAGE(rmsDb(a, kFs - 2048, kFs) < -60.f, msg);
  TEST_ASSERT_TRUE_MESSAGE(done, "decay 0.3: done at 1 s");
  // Longest decay: silent and done by 4 s.
  dd[0] = 1.f;
  a = hit(s.k, dd, 60, 127, 4 * kFs, &done);
  snprintf(msg, sizeof(msg), "decay 1: %f dB at 4 s", rmsDb(a, 4 * kFs - 2048, 4 * kFs));
  TEST_ASSERT_TRUE_MESSAGE(rmsDb(a, 4 * kFs - 2048, 4 * kFs) < -75.f, msg);
  TEST_ASSERT_TRUE_MESSAGE(done, "decay 1: done at 4 s");
  // ... but still sounding at 100 ms.
  TEST_ASSERT_TRUE(rmsDb(a, ms(100), ms(100) + 1024) > -60.f);
  // Velocity is an accent.
  const float loud = peak(hit(s.k, d, 60, 127, kFs / 2)), soft = peak(hit(s.k, d, 60, 40, kFs / 2));
  snprintf(msg, sizeof(msg), "vel 127 %f vel 40 %f", loud, soft);
  TEST_ASSERT_TRUE_MESSAGE(loud > 1.3f * soft, msg);
  // Pitch follows the note.
  if (s.tonal) {
    float q[5];
    memcpy(q, d, sizeof(q));
    if (s.quiet >= 0) q[s.quiet] = 0;
    const std::vector<float> lo = hit(s.k, q, 60, 127, kFs), hi = hit(s.k, q, 72, 127, kFs);
    const float r = zeroCrossHz(hi, ms(s.pitchFrom), ms(s.pitchTo)) / zeroCrossHz(lo, ms(s.pitchFrom), ms(s.pitchTo));
    snprintf(msg, sizeof(msg), "octave ratio %f (%f Hz)", r, zeroCrossHz(lo, ms(s.pitchFrom), ms(s.pitchTo)));
    TEST_ASSERT_TRUE_MESSAGE(fabsf(r - 2.f) < 0.1f, msg);
  } else {
    const std::vector<float> lo = hit(s.k, d, 60, 127, kFs / 2), hi = hit(s.k, d, 72, 127, kFs / 2);
    const float r = centroidHz(hi, 0) / centroidHz(lo, 0);
    snprintf(msg, sizeof(msg), "centroid ratio %f", r);
    TEST_ASSERT_TRUE_MESSAGE(r > 1.5f, msg);
  }
}

const Spec kBda = {Drum2Kind::Bda, {80, 40, 50, 40, 0}, true, 200, 500, -1};
const Spec kSda = {Drum2Kind::Sda, {55, 64, 70, 0, 0}, true, 80, 200, 2};  // after the 2nd mode
const Spec kBds = {Drum2Kind::Bds, {75, 50, 30, 60, 40}, true, 200, 500, -1};
const Spec kSds = {Drum2Kind::Sds, {55, 60, 70, 40, 0}, true, 20, 80, 2};
const Spec kCpx = {Drum2Kind::Cpx, {60, 64, 64, 64, 0}, false, 0, 0, -1};
const Spec kRsx = {Drum2Kind::Rsx, {40, 64, 40, 0, 0}, true, 20, 80, 2};
const Spec kBdf = {Drum2Kind::Bdf, {75, 50, 40, 50, 16}, true, 200, 500, -1};
const Spec kHhr = {Drum2Kind::Hhr, {30, 64, 30, 0, 0}, false, 0, 0, -1};

}  // namespace

// ---- Task 5: skeleton ----

void test_stub_silent_done() {
  for (int k = 0; k < static_cast<int>(Drum2Kind::Count); ++k) {
    Drum2Voice v{};
    TEST_ASSERT_TRUE(v.done());
  }
  Drum2Voice v{};
  v.trigger(Drum2Kind::Bda, 1.f, false);
  TEST_ASSERT_FALSE(v.done());  // fresh hit, not rendered yet
}

void test_struct_trivial() {
  static_assert(std::is_trivially_copyable<Drum2Voice>::value, "Drum2Voice in the voice union");
  TEST_ASSERT_TRUE(sizeof(Drum2Voice) <= 512);
}

void test_base_hz() {
  TEST_ASSERT_EQUAL_FLOAT(55.f, drum2BaseHz(Drum2Kind::Bda));
  TEST_ASSERT_EQUAL_FLOAT(1.f, drum2BaseHz(Drum2Kind::Cpx));
  TEST_ASSERT_EQUAL_FLOAT(470.f, drum2BaseHz(Drum2Kind::Rsx));
  TEST_ASSERT_EQUAL_FLOAT(1.f, drum2BaseHz(Drum2Kind::Hhr));
}

// Choke: the new hit keeps the old one's last output, fading in ~2 ms.
void test_choke_tail() {
  float d[5];
  defaults(kBds.def, d);
  Drum2Params p;
  p.hz = drum2BaseHz(Drum2Kind::Bds);
  memcpy(p.mac, d, sizeof(d));
  Drum2Voice v{};
  v.trigger(Drum2Kind::Bds, 1.f, false);
  float buf[32];
  for (int i = 0; i < 40; ++i) {
    memset(buf, 0, sizeof(buf));
    v.control(p, 32);
    v.render(buf, 32, 1.f);
  }
  const float last = buf[31];
  TEST_ASSERT_TRUE(fabsf(last) > 1e-3f);
  Drum2Voice w = v;
  w.trigger(Drum2Kind::Hhr, 0.f, true);
  Drum2Params ph;
  ph.hz = 1;
  ph.mac[0] = 0.3f;
  w.control(ph, 32);
  memset(buf, 0, sizeof(buf));
  w.render(buf, 1, 1.f);
  TEST_ASSERT_TRUE(fabsf(buf[0] - last) < 0.3f);  // continuous
  // Without keepTail the old sound is gone at once.
  Drum2Voice x = v;
  x.trigger(Drum2Kind::Bda, 0.f, false);
  const float zero[5] = {0, 0, 0, 0, 0};
  Drum2Params pz;
  pz.hz = 55;
  memcpy(pz.mac, zero, sizeof(zero));
  x.control(pz, 32);
  float y[1] = {0};
  x.render(y, 1, 1.f);
  TEST_ASSERT_TRUE(fabsf(y[0]) < fabsf(last) || fabsf(last) < 0.05f);
}

void test_render_adds_scaled() {
  float d[5];
  defaults(kSds.def, d);
  const std::vector<float> a = hit(Drum2Kind::Sds, d, 60, 127, 2048);
  Drum2Voice v{};
  v.trigger(Drum2Kind::Sds, 1.f, false);
  Drum2Params p;
  p.hz = drum2BaseHz(Drum2Kind::Sds);
  memcpy(p.mac, d, sizeof(d));
  std::vector<float> b(2048, 0.25f);
  for (int i = 0; i < 2048; i += 32) {
    v.control(p, 32);
    v.render(b.data() + i, 32, 0.5f);
  }
  for (int i = 0; i < 2048; ++i) TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.25f + 0.5f * a[i], b[i]);
}

// ---- Task 6: BDA, SDA ----

void test_bda() { checkMachine(kBda); }
void test_sda() { checkMachine(kSda); }

void test_bda_attack_brightens() {
  float d[5];
  defaults(kBda.def, d);
  d[2] = 0;
  const float c0 = centroidHz(hit(Drum2Kind::Bda, d, 60, 127, 2048), 0);
  d[2] = 1;
  const float c1 = centroidHz(hit(Drum2Kind::Bda, d, 60, 127, 2048), 0);
  char msg[64];
  snprintf(msg, sizeof(msg), "centroid %f -> %f", c0, c1);
  TEST_ASSERT_TRUE_MESSAGE(c1 > c0 * 1.05f, msg);
}

void test_sda_snappy_noise() {
  float d[5];
  defaults(kSda.def, d);
  d[2] = 0;
  const float e0 = energyAbove(hit(Drum2Kind::Sda, d, 60, 127, 4096), 0, 3000);
  d[2] = 1;
  const float e1 = energyAbove(hit(Drum2Kind::Sda, d, 60, 127, 4096), 0, 3000);
  char msg[64];
  snprintf(msg, sizeof(msg), "hf energy %g -> %g", e0, e1);
  TEST_ASSERT_TRUE_MESSAGE(e1 > 4.f * e0, msg);
}

// ---- Task 7: BDS, SDS ----

void test_bds() { checkMachine(kBds); }
void test_sds() { checkMachine(kSds); }

void test_bds_sweep_falls() {
  float d[5];
  defaults(kBds.def, d);
  d[3] = 1;
  const std::vector<float> a = hit(Drum2Kind::Bds, d, 60, 127, kFs / 2);
  const float early = zeroCrossHz(a, 0, ms(10)), late = zeroCrossHz(a, ms(200), ms(400));
  char msg[64];
  snprintf(msg, sizeof(msg), "%f Hz -> %f Hz", early, late);
  TEST_ASSERT_TRUE_MESSAGE(early > 1.5f * late, msg);
}

void test_bds_dirt_harmonics() {
  float d[5];
  defaults(kBds.def, d);
  const float hz = drum2BaseHz(Drum2Kind::Bds);
  float share[2];
  for (int j = 0; j < 2; ++j) {
    d[2] = static_cast<float>(j);
    const std::vector<float> a = hit(Drum2Kind::Bds, d, 60, 127, kFs / 2);
    share[j] = energyAbove(a, ms(100), 3 * hz) / energyAbove(a, ms(100), 0);
  }
  char msg[64];
  snprintf(msg, sizeof(msg), "share %g -> %g", share[0], share[1]);
  TEST_ASSERT_TRUE_MESSAGE(share[1] > share[0], msg);
}

// ---- Task 8: CPX, RSX ----

void test_cpx() { checkMachine(kCpx); }
void test_rsx() { checkMachine(kRsx); }

// The 4 claps of a pattern start at drum2ClapBurstMs (envelope rises there).
void test_cpx_bursts() {
  for (int pat : {0, 15}) {
    float d[5];
    defaults(kCpx.def, d);
    d[4] = pat / 15.f;
    const std::vector<float> a = hit(Drum2Kind::Cpx, d, 60, 127, kFs / 2);
    std::vector<float> env(a.size());
    float e = 0;
    const float g = 1.f - expf(-1.f / (0.0005f * kFs));
    for (size_t i = 0; i < a.size(); ++i) env[i] = e += g * (fabsf(a[i]) - e);
    for (int i = 0; i < 4; ++i) {
      const float t = drum2ClapBurstMs(pat, d[3], i);
      const int at = ms(t);
      const float before = at > ms(0.3f) ? env[at - ms(0.3f)] : 0.f;
      const float after = peak(env, at, at + ms(2));
      char msg[64];
      snprintf(msg, sizeof(msg), "pattern %d burst %d at %f ms: %f -> %f", pat, i, t, before, after);
      TEST_ASSERT_TRUE_MESSAGE(after > 1.5f * before + 1e-3f, msg);
    }
  }
}

void test_cpx_pattern_latched() {
  float d[5], e[5];
  defaults(kCpx.def, d);
  memcpy(e, d, sizeof(e));
  e[4] = 1;
  const std::vector<float> a = hit(Drum2Kind::Cpx, d, 60, 127, kFs / 4);
  const std::vector<float> b = hit(Drum2Kind::Cpx, d, 60, 127, kFs / 4, nullptr, e);
  TEST_ASSERT_TRUE(memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}

// ---- Task 9: BDF, HHR ----

void test_bdf() { checkMachine(kBdf); }
void test_hhr() { checkMachine(kHhr); }

void test_bdf_ratio_changes() {
  float d[5];
  defaults(kBdf.def, d);
  d[3] = 1;  // index up: the modulator audible
  d[4] = 0;
  const float c0 = centroidHz(hit(Drum2Kind::Bdf, d, 60, 127, 2048), 0);
  d[4] = 1;
  const float c1 = centroidHz(hit(Drum2Kind::Bdf, d, 60, 127, 2048), 0);
  char msg[64];
  snprintf(msg, sizeof(msg), "centroid %f -> %f", c0, c1);
  TEST_ASSERT_TRUE_MESSAGE(c1 > 1.3f * c0 || c0 > 1.3f * c1, msg);
}

void test_hhr_decay_open_closed() {
  float d[5];
  defaults(kHhr.def, d);
  d[0] = 0.1f;
  std::vector<float> a = hit(Drum2Kind::Hhr, d, 60, 127, kFs / 2);
  TEST_ASSERT_TRUE(rmsDb(a, ms(300), ms(300) + 1024) < -60.f);
  d[0] = 1.f;
  a = hit(Drum2Kind::Hhr, d, 60, 127, kFs / 2);
  TEST_ASSERT_TRUE(rmsDb(a, ms(300), ms(300) + 1024) > -60.f);
}

// Renders `sec` seconds of a hit; false on NaN / Inf or |x| > 1. peak: the largest |x|.
static bool runHit(Drum2Kind k, float hz, float accent, const float mac[5], float sec, float& peak,
                   bool& ended) {
  Drum2Voice v;
  v.trigger(k, accent, false);
  Drum2Params p;
  p.hz = hz;
  for (int i = 0; i < 5; ++i) p.mac[i] = mac[i];
  float buf[kCtl];
  peak = 0;
  ended = false;
  for (int b = 0; b < static_cast<int>(sec * kFs) / kCtl; ++b) {
    memset(buf, 0, sizeof buf);
    v.control(p, kCtl);
    v.render(buf, kCtl, 1.f);
    for (float x : buf) {
      if (!isfinite(x) || fabsf(x) > 1.f) return false;
      peak = fmaxf(peak, fabsf(x));
    }
    if (v.done()) {
      ended = true;
      break;
    }
  }
  return true;
}

// CPX: every one of the 16 patterns sounds, stays bounded and ends.
void test_cpx_all_patterns() {
  for (int pat = 0; pat < 16; ++pat) {
    const float mac[5] = {0.5f, 0.5f, 0.5f, 0.5f, (pat + 0.5f) / 16.f};
    float peak;
    bool ended;
    char msg[16];
    snprintf(msg, sizeof msg, "pattern %d", pat);
    TEST_ASSERT_TRUE_MESSAGE(runHit(Drum2Kind::Cpx, 1.f, 1.f, mac, 8.f, peak, ended), msg);
    TEST_ASSERT_TRUE_MESSAGE(peak > 0.01f, msg);
    TEST_ASSERT_TRUE_MESSAGE(ended, msg);
  }
}

// Every machine at the edges: notes 0 / 127, velocity 0 / 1 / 127, every knob 0 or 1: no NaN / Inf,
// peak <= 1.
void test_edges_bounded() {
  const float notes[] = {0, 60, 127};
  const float accents[] = {0.f, 1.f / 127.f, 1.f};
  const float knobs[] = {0.f, 1.f};
  for (int k = 0; k < static_cast<int>(Drum2Kind::Count); ++k)
    for (float note : notes)
      for (float a : accents)
        for (float kn : knobs) {
          const float mac[5] = {kn, kn, kn, kn, kn};
          const float hz = drum2BaseHz(static_cast<Drum2Kind>(k)) * exp2f((note - 60.f) / 12.f);
          float peak;
          bool ended;
          char msg[48];
          snprintf(msg, sizeof msg, "kind %d note %.0f acc %.3f knobs %.0f", k, note, a, kn);
          TEST_ASSERT_TRUE_MESSAGE(runHit(static_cast<Drum2Kind>(k), hz, a, mac, 2.f, peak, ended), msg);
        }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_stub_silent_done);
  RUN_TEST(test_struct_trivial);
  RUN_TEST(test_base_hz);
  RUN_TEST(test_choke_tail);
  RUN_TEST(test_render_adds_scaled);
  RUN_TEST(test_bda);
  RUN_TEST(test_sda);
  RUN_TEST(test_bda_attack_brightens);
  RUN_TEST(test_sda_snappy_noise);
  RUN_TEST(test_bds);
  RUN_TEST(test_sds);
  RUN_TEST(test_bds_sweep_falls);
  RUN_TEST(test_bds_dirt_harmonics);
  RUN_TEST(test_cpx);
  RUN_TEST(test_rsx);
  RUN_TEST(test_cpx_bursts);
  RUN_TEST(test_cpx_pattern_latched);
  RUN_TEST(test_bdf);
  RUN_TEST(test_hhr);
  RUN_TEST(test_bdf_ratio_changes);
  RUN_TEST(test_hhr_decay_open_closed);
  RUN_TEST(test_cpx_all_patterns);
  RUN_TEST(test_edges_bounded);
  return UNITY_END();
}
