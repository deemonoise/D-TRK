#include <math.h>
#include <string.h>
#include <unity.h>
#include "synth_osc.h"
#include "synth_syn.h"
#include "synth_shape.h"
#include "wt_builtin.h"

using namespace mt;

void setUp() {}
void tearDown() {}

// Goertzel power at hz over x[0..n).
static double power(const float* x, int n, float hz) {
  const double w = 6.283185307179586 * hz / kSynthRate, c = 2 * cos(w);
  double s1 = 0, s2 = 0;
  for (int i = 0; i < n; ++i) {
    const double s = x[i] + c * s1 - s2;
    s2 = s1;
    s1 = s;
  }
  return s1 * s1 + s2 * s2 - c * s1 * s2;
}

static float out[8192];

// f0 = 5 kHz: harmonics 5/10/15 kHz, aliases of 20/25/30 kHz land on 12/7/2 kHz.
void test_bl_saw_alias_below_naive() {
  BlOsc o;
  const float dt = 5000.f / kSynthRate;
  for (int i = 0; i < 8192; ++i) out[i] = o.saw(dt, -1).v;
  const double bl = power(out, 8192, 2000) + power(out, 8192, 7000);
  ChipOsc c;
  for (int i = 0; i < 8192; ++i) out[i] = c.next(Wave::Saw, dt, 0.5f);
  const double naive = power(out, 8192, 2000) + power(out, 8192, 7000);
  TEST_ASSERT_TRUE(bl < naive * 0.05);  // > 13 dB less
}

void test_bl_square_pw_duty() {
  BlOsc o;
  const float dt = 100.f / kSynthRate;
  double mean = 0;
  for (int i = 0; i < kSynthRate; ++i) mean += o.square(dt, 0.25f, -1).v;
  // 25 % high: mean = 0.25 - 0.75 = -0.5.
  TEST_ASSERT_FLOAT_WITHIN(0.02f, -0.5f, static_cast<float>(mean / kSynthRate));
}

void test_bl_tri_bounded() {
  BlOsc o;
  const float dt = 440.f / kSynthRate;
  float lo = 0, hi = 0;
  for (int i = 0; i < kSynthRate; ++i) {
    const float v = o.tri(dt).v;
    if (i > kSynthRate / 2) lo = v < lo ? v : lo, hi = v > hi ? v : hi;
  }
  TEST_ASSERT_FLOAT_WITHIN(0.15f, 1.f, hi);
  TEST_ASSERT_FLOAT_WITHIN(0.15f, -1.f, lo);
}

void test_master_reports_wrap() {
  BlOsc o;
  const float dt = 1000.f / kSynthRate;  // 1 kHz
  int wraps = 0;
  for (int i = 0; i < kSynthRate / 10; ++i) wraps += o.saw(dt, -1).wrap >= 0;  // 100 ms
  TEST_ASSERT_INT_WITHIN(1, 100, wraps);
}

void test_sync_no_jumps() {
  // Slave at 2.37 x master, hard synced: no sample-to-sample jump near the full step size.
  BlOsc m, s;
  const float d1 = 300.f / kSynthRate, d2 = d1 * 2.37f;
  float prev = 0, maxStep = 0;
  for (int i = 0; i < 4000; ++i) {
    const float w = m.saw(d1, -1).wrap;
    const float v = s.saw(d2, w).v;
    if (i > 10) maxStep = fabsf(v - prev) > maxStep ? fabsf(v - prev) : maxStep;
    prev = v;
  }
  TEST_ASSERT_TRUE(maxStep < 1.6f);  // naive resets jump by up to 2
}

void test_wt_reads_level_and_frame() {
  static int16_t t[kWtTableSamples];
  WtBuiltinSrc src(0);
  wtBuild(src, t);
  WtOsc o;
  o.setLevel(220.f);
  float mx = 0;
  for (int i = 0; i < 2000; ++i) {
    const float v = o.next(t, 220.f / kSynthRate, 0.f, -1);
    TEST_ASSERT_FALSE(isnan(v));
    mx = fabsf(v) > mx ? fabsf(v) : mx;
  }
  TEST_ASSERT_TRUE(mx > 0.5f && mx <= 1.05f);
  o.next(nullptr, 0.01f, 0.f, -1);  // one sample late: flushes the last table sample
  TEST_ASSERT_EQUAL_FLOAT(0.f, o.next(nullptr, 0.01f, 0.f, -1));  // missing table = silence
}

void test_wt_level_choice() {
  TEST_ASSERT_EQUAL_FLOAT(0.f, wtLevelPos(50.f));       // low notes: level 0
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.f, wtLevelPos(125.f));  // 125 * 128 = 16 kHz -> 1 octave above 8 kHz
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.f, wtLevelPos(20000.f));  // clamped
}

void test_voice_mix_and_silence() {
  SynVoice v;
  SynParams p;
  p.mode[0] = p.mode[1] = static_cast<uint8_t>(SynOsc::Saw);
  p.hz[0] = p.hz[1] = 220;
  p.mix = 0;
  v.trigger();
  v.control(p, 32);
  float buf[32] = {0};
  v.render(buf, 32, 1.f);
  float e = 0;
  for (float x : buf) e += fabsf(x);
  TEST_ASSERT_TRUE(e > 1.f);
  // Both oscillators on missing wavetables, no sub / noise: silent.
  p.mode[0] = p.mode[1] = static_cast<uint8_t>(SynOsc::Wt);
  p.wt[0] = p.wt[1] = nullptr;
  v.trigger();
  v.control(p, 32);
  memset(buf, 0, sizeof(buf));
  for (int k = 0; k < 4; ++k) v.render(buf, 32, 1.f);
  for (float x : buf) TEST_ASSERT_EQUAL_FLOAT(0.f, x);
}

// Above the sample rate (high note + transpose + osc semitones) the BL oscillators stay bounded.
void test_voice_bounded_above_sample_rate() {
  const uint8_t modes[] = {static_cast<uint8_t>(SynOsc::Saw), static_cast<uint8_t>(SynOsc::Square),
                           static_cast<uint8_t>(SynOsc::Tri)};
  for (uint8_t m : modes) {
    SynVoice v;
    SynParams p;
    p.mode[0] = p.mode[1] = m;
    p.hz[0] = p.hz[1] = 50000;
    p.mix = 64;
    p.subOct = 2;
    v.trigger();
    float buf[32];
    float peak = 0;
    for (int k = 0; k < 1000; ++k) {  // 1 s
      v.control(p, 32);
      memset(buf, 0, sizeof(buf));
      v.render(buf, 32, 1.f);
      for (float x : buf) peak = fmaxf(peak, fabsf(x));
    }
    TEST_ASSERT_TRUE(peak < 80.f);  // a 220 Hz saw at this gain peaks near 64 (kSynGain 1)
  }
}

static uint32_t fnv(const float* x, int n) {
  uint32_t h = 2166136261u;
  const uint8_t* b = reinterpret_cast<const uint8_t*>(x);
  for (int i = 0; i < n * 4; ++i) h = (h ^ b[i]) * 16777619u;
  return h;
}

// n samples of a voice, a control period of 32 each (out[] zeroed first).
static void renderVoice(SynVoice& v, const SynParams& p, int n) {
  memset(out, 0, sizeof(out));
  v.trigger();
  for (int k = 0; k < n; k += 32) {
    v.control(p, 32);
    v.render(out + k, 32, 1.f);
  }
}

static SynParams sawSqr() {
  SynParams p;
  p.mode[0] = static_cast<uint8_t>(SynOsc::Saw);
  p.mode[1] = static_cast<uint8_t>(SynOsc::Square);
  p.hz[0] = p.hz[1] = 220;
  p.mix = 0.5f;
  return p;
}

// Fold and ring off: bit-exact to the voice before they existed (hash of its output).
void test_fold_ring_off_bit_exact() {
  SynVoice v;
  SynParams p = sawSqr();
  p.fold = 0, p.ring = 0;
  renderVoice(v, p, 4096);
  TEST_ASSERT_EQUAL_HEX32(0xa7aa15b5u, fnv(out, 4096));
}

static SynParams tri(float hz) {
  SynParams p;
  p.mode[0] = p.mode[1] = static_cast<uint8_t>(SynOsc::Tri);
  p.hz[0] = p.hz[1] = hz;
  return p;
}

static double db(double a, double b) { return 10 * log10(a / b); }
static float rmsOut(int from, int n) {
  double s = 0;
  for (int i = from; i < from + n; ++i) s += static_cast<double>(out[i]) * out[i];
  return static_cast<float>(sqrt(s / n));
}

// Energy outside the fundamental over the fundamental's (tri / sine: the harmonics' share).
// 220.5 Hz: a period of 200 samples, 4000 samples = 20 periods (no leakage).
static double thd(const float* x) {
  double e = 0;
  for (int i = 0; i < 4000; ++i) e += static_cast<double>(x[i]) * x[i];
  const double f = 2 * power(x, 4000, 220.5f) / 4000;
  return (e - f) / f;
}

// Fold moves the energy of a 220 Hz tri into its harmonics; the output stays bounded.
void test_fold_adds_harmonics() {
  SynVoice v;
  SynParams p = tri(220.5f);
  renderVoice(v, p, 8192);
  const double h0 = thd(out + 4000);
  p.fold = 1;
  renderVoice(v, p, 8192);
  const double h1 = thd(out + 4000);
  TEST_ASSERT_TRUE(db(h1, h0) >= 20);
  for (int i = 0; i < 8192; ++i) TEST_ASSERT_TRUE(fabsf(out[i]) <= 1.5f);
}

void test_fold_finite() {
  const float hzs[] = {20, 220, 2000, 10000};
  for (float hz : hzs)
    for (int f = 0; f <= 4; ++f) {
      SynVoice v;
      SynParams p = sawSqr();
      p.hz[0] = p.hz[1] = hz;
      p.fold = f * 0.25f;
      renderVoice(v, p, 4096);
      for (int i = 0; i < 4096; ++i) TEST_ASSERT_TRUE(isfinite(out[i]));
    }
}

// Pitch limit: osc 1 at 5 kHz plays as if the fold were off.
void test_fold_pitch_limit() {
  static float ref[4096];
  SynVoice v;
  SynParams p = tri(5000);
  renderVoice(v, p, 4096);
  memcpy(ref, out, sizeof(ref));
  p.fold = 1;
  renderVoice(v, p, 4096);
  for (int i = 0; i < 4096; ++i) TEST_ASSERT_FLOAT_WITHIN(1e-6f, ref[i], out[i]);
}

// Between 1 and 5 kHz the pitch limit scales the fold's gain: at 3 kHz fold 1 is half way, the plain
// mix x 4.5 (1 + 7 x 0.5) through the curve, neither dry nor the full fold (x 8). Above 5 kHz: dry.
void test_fold_pitch_limit_scales_gain() {
  static float ref[4096];
  SynVoice v;
  SynParams p = tri(3000);
  renderVoice(v, p, 4096);
  memcpy(ref, out, sizeof(ref));
  p.fold = 1;
  renderVoice(v, p, 4096);
  double dDry = 0, dFull = 0;
  for (int i = 512; i < 4096; ++i) {  // past the fold's ramp from 0
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, foldCurve(4.5f * ref[i]), out[i]);
    dDry += fabsf(out[i] - ref[i]);
    dFull += fabsf(out[i] - foldCurve(8.f * ref[i]));
  }
  TEST_ASSERT_TRUE(dDry > 100);
  TEST_ASSERT_TRUE(dFull > 100);
  p = tri(7000);
  renderVoice(v, p, 4096);
  memcpy(ref, out, sizeof(ref));
  p.fold = 1;
  renderVoice(v, p, 4096);
  for (int i = 0; i < 4096; ++i) TEST_ASSERT_FLOAT_WITHIN(1e-6f, ref[i], out[i]);
}

// A NaN never reaches foldTri's int cast (UB): fclamp sends it to hi on every target, as VMINNM does.
void test_fold_tri_nan_is_hi() {
  TEST_ASSERT_EQUAL_FLOAT(60.f, fclamp(NAN, -60.f, 60.f));
  TEST_ASSERT_EQUAL_FLOAT(foldTri(60.f), foldTri(NAN));
  TEST_ASSERT_EQUAL_FLOAT(-60.f, fclamp(-1e30f, -60.f, 60.f));
  TEST_ASSERT_EQUAL_FLOAT(60.f, fclamp(1e30f, -60.f, 60.f));
  TEST_ASSERT_EQUAL_FLOAT(0.25f, fclamp(0.25f, -60.f, 60.f));
}

// Fold 1 at a low pitch is the plain fold curve per sample (naive, no ADAA: the CPU cut-off; the
// pitch limit keeps its aliasing in check): the plain mix x 8 through foldCurve.
void test_fold_is_naive_curve() {
  static float ref[4096];
  SynVoice v;
  SynParams p = sawSqr();
  renderVoice(v, p, 4096);
  memcpy(ref, out, sizeof(ref));
  p.fold = 1;
  renderVoice(v, p, 4096);
  for (int i = 0; i < 4096; ++i) TEST_ASSERT_FLOAT_WITHIN(1e-4f, foldCurve(8.f * ref[i]), out[i]);
}

// Ring 1, two 220 Hz tris: the product, its 2nd harmonic far above the fundamental.
void test_ring_product() {
  SynVoice v;
  SynParams p = tri(220);
  p.ring = 1;
  renderVoice(v, p, 8192);
  TEST_ASSERT_TRUE(db(power(out + 4096, 4096, 440), power(out + 4096, 4096, 220)) >= 20);
}

// Ring > 0 renders osc 2 even with MIX at osc 1 (Semi2 +7: 329.6 Hz): the sum 549.6 Hz appears.
void test_ring_forces_osc2() {
  SynVoice v;
  SynParams p = tri(220);
  p.hz[1] = 220 * powf(2.f, 7.f / 12.f);
  renderVoice(v, p, 8192);
  const float sum = p.hz[0] + p.hz[1];
  const double off = power(out + 4096, 4096, sum);
  p.ring = 0.5f;
  renderVoice(v, p, 8192);
  TEST_ASSERT_TRUE(db(power(out + 4096, 4096, sum), off) >= 20);
}

// Fold 1 and ring 1 at 220 Hz within +-3 dB RMS of the plain oscillator (kFoldGain, kRingGain):
// saws and tris, MIX 0 (ring: both oscillators the same).
void test_fold_ring_loudness() {
  SynParams ps[] = {tri(220), tri(220)};
  ps[0].mode[0] = ps[0].mode[1] = static_cast<uint8_t>(SynOsc::Saw);  // MIX 0: osc 1 alone
  for (SynParams p : ps) {
    SynVoice v;
    renderVoice(v, p, 8192);
    const float r0 = rmsOut(4096, 4096);
    p.fold = 1;
    renderVoice(v, p, 8192);
    const float rf = rmsOut(4096, 4096);
    p.fold = 0, p.ring = 1;
    renderVoice(v, p, 8192);
    const float rr = rmsOut(4096, 4096);
    TEST_ASSERT_FLOAT_WITHIN(3.f, 0.f, 20 * log10f(rf / r0));
    TEST_ASSERT_FLOAT_WITHIN(3.f, 0.f, 20 * log10f(rr / r0));
  }
}

// Max |x[i] - x[i-1]| of 1 s of ring 1 on two 220 Hz tris, fold per control period from fold(t).
static float maxJump(float (*fold)(int)) {
  static float buf[44128];
  SynVoice v;
  SynParams p = tri(220);
  p.ring = 1;
  memset(buf, 0, sizeof(buf));
  v.trigger();
  for (int k = 0; k + 32 <= 44128; k += 32) {
    p.fold = fold(k);
    v.control(p, 32);
    v.render(buf + k, 32, 1.f);
  }
  float mx = 0;
  for (int i = 2048; i < 44128; ++i) mx = fmaxf(mx, fabsf(buf[i] - buf[i - 1]));
  return mx;
}
static float foldZero(int) { return 0; }
static float gDepth = 0.3f;
static float foldLow(int) { return gDepth; }
// A 2 Hz LFO of depth 0.3 clamped at 0: triangles 0..0.3..0 with stretches held at 0.
static float foldLfo(int k) {
  const float ph = (k % 22050) * (1.f / 22050);
  const float t = ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph;  // -1..1
  return t > 0 ? gDepth * t : 0.f;
}

// Fold switching between 0 and the depth every few control periods (7 periods = 224 samples
// against the tri's 200.5: the crossings sweep every phase of the wave, its peaks included).
static float foldGate(int k) { return (k / 32) % 7 < 3 ? 0.f : gDepth; }

// Fold crossing 0 (an LFO, the pitch limit) does not switch the timbre abruptly: with ring 1 the
// mix peaks above 1, where a sudden fold would step the wave by up to ~0.6. The steps stay near
// those of the same sound with the fold held (it fades in over its first steps).
void test_fold_crossing_zero_is_smooth() {
  const float j0 = maxJump(foldZero);
  gDepth = 0.3f;  // a 2 Hz LFO of depth 0.3 clamped at 0
  TEST_ASSERT_TRUE(maxJump(foldLfo) <= 1.2f * fmaxf(j0, maxJump(foldLow)));
  const float ds[] = {1.f / 127, 0.03f};  // gated on and off, crossings at every phase
  for (float d : ds) {
    gDepth = d;
    TEST_ASSERT_TRUE(maxJump(foldGate) <= 1.2f * fmaxf(j0, maxJump(foldLow)));
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_bl_saw_alias_below_naive);
  RUN_TEST(test_bl_square_pw_duty);
  RUN_TEST(test_bl_tri_bounded);
  RUN_TEST(test_master_reports_wrap);
  RUN_TEST(test_sync_no_jumps);
  RUN_TEST(test_wt_reads_level_and_frame);
  RUN_TEST(test_wt_level_choice);
  RUN_TEST(test_voice_mix_and_silence);
  RUN_TEST(test_voice_bounded_above_sample_rate);
  RUN_TEST(test_fold_ring_off_bit_exact);
  RUN_TEST(test_fold_adds_harmonics);
  RUN_TEST(test_fold_finite);
  RUN_TEST(test_fold_pitch_limit);
  RUN_TEST(test_fold_pitch_limit_scales_gain);
  RUN_TEST(test_fold_tri_nan_is_hi);
  RUN_TEST(test_fold_is_naive_curve);
  RUN_TEST(test_ring_product);
  RUN_TEST(test_ring_forces_osc2);
  RUN_TEST(test_fold_ring_loudness);
  RUN_TEST(test_fold_crossing_zero_is_smooth);
  return UNITY_END();
}
