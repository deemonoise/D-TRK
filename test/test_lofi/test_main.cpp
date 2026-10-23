#include <math.h>
#include <string.h>
#include <type_traits>
#include <unity.h>
#include "bytebeat_compile.h"
#include "bytebeat_lib.h"
#include "synth_lofi.h"
#include "synth_osc.h"

using namespace mt;

void setUp() {}
void tearDown() {}

namespace {

constexpr int kSec = kSynthRate;
constexpr int kCtl = 32;  // Synth::kControl

ByteCode compile(const char* src, uint8_t mode = kByteUnsigned) {
  ByteCode bc;
  TEST_ASSERT_TRUE(byteCompile(src, mode, bc).err == ByteErr::Ok);
  return bc;
}

// BYTE params: formula code, note, macros 0..1 (A B C TONE LOOP).
LofiParams byteParams(const ByteCode* code, float note, float a = 0, float b = 0, float c = 0, float tone = 0,
                      float loop = 0) {
  LofiParams p;
  p.machine = static_cast<uint8_t>(LofiMachine::Byte);
  p.code = code;
  p.hz = noteHz(note);
  const float m[kFmMacros] = {a, b, c, tone, loop};
  for (int k = 0; k < kFmMacros; ++k) p.mac[k] = m[k];
  return p;
}

// APC params: hz, macros 0..1 (WIDTH TONE DRIFT SMOOTH).
LofiParams apcParams(float hz, float width, float tone = 1, float drift = 0, float smooth = 0) {
  LofiParams p;
  p.machine = static_cast<uint8_t>(LofiMachine::Apc);
  p.hz = hz;
  const float m[kFmMacros] = {width, tone, drift, smooth, 0};
  for (int k = 0; k < kFmMacros; ++k) p.mac[k] = m[k];
  return p;
}

// WIDTH macro for a pulse of `periods` periods (0.1 .. 8, exponential).
float widthFor(float periods) { return logf(periods / 0.1f) / logf(80.f); }

// Renders n samples (control every kCtl) into out (zeroed first).
void run(LofiVoice& v, const LofiParams& p, float* out, int n) {
  for (int i = 0; i < n; ++i) out[i] = 0;
  for (int i = 0; i < n; i += kCtl) {
    const int m = n - i < kCtl ? n - i : kCtl;
    v.control(p, m);
    v.render(out + i, m, 1.f);
  }
}

float out1[2 * kSec], out2[2 * kSec];

// Sum of squared first differences: the high-frequency energy.
double hfEnergy(const float* x, int from, int n) {
  double e = 0;
  for (int i = from + 1; i < n; ++i) e += double(x[i] - x[i - 1]) * (x[i] - x[i - 1]);
  return e;
}

// Rising steps (a pulse start) in x[from, n).
int risingEdges(const float* x, int from, int n, float th = 0.3f) {
  int c = 0;
  for (int i = from + 1; i < n; ++i)
    if (x[i] - x[i - 1] > th) ++c;
  return c;
}

}  // namespace

void test_voice_is_trivially_copyable() {
  TEST_ASSERT_TRUE(std::is_trivially_copyable<LofiVoice>::value);
  TEST_ASSERT_TRUE(std::is_trivially_copyable<LofiParams>::value);
}

void test_byte_c4_evaluates_8000_per_second() {
  const ByteCode bc = compile("t");
  LofiVoice v;
  v.trigger(1);
  run(v, byteParams(&bc, 60), out1, kSec);
  TEST_ASSERT_INT_WITHIN(4, 8000, static_cast<int>(v.evals()));
  TEST_ASSERT_UINT32_WITHIN(4, 8000, v.tick());
}

void test_byte_octave_up_doubles_rate() {
  const ByteCode bc = compile("t");
  LofiVoice v;
  v.trigger(1);
  run(v, byteParams(&bc, 72), out1, kSec);
  TEST_ASSERT_INT_WITHIN(8, 16000, static_cast<int>(v.evals()));
}

void test_byte_evaluates_at_most_once_per_sample() {
  const ByteCode bc = compile("t");
  LofiVoice v;
  v.trigger(1);
  run(v, byteParams(&bc, 96), out1, kSec);  // 64 kHz of t
  TEST_ASSERT_TRUE(v.evals() <= static_cast<uint32_t>(kSec));
  TEST_ASSERT_UINT32_WITHIN(64, 64000, v.tick());
}

void test_byte_trigger_resets_t() {
  const ByteCode bc = compile("t*(t>>5|t>>8)");
  LofiVoice v;
  v.trigger(1);
  run(v, byteParams(&bc, 60), out1, 4000);
  TEST_ASSERT_TRUE(v.tick() > 900);
  v.trigger(1);
  TEST_ASSERT_EQUAL_UINT32(0, v.tick());
  run(v, byteParams(&bc, 60), out1, 400);
  TEST_ASSERT_UINT32_WITHIN(1, 100, v.tick());
}

void test_byte_loop_wraps_t() {
  const ByteCode bc = compile("t*(t>>5|t>>8)");
  LofiVoice v;
  v.trigger(1);
  // LOOP at its lowest step: t & 1023, a period of 4096 samples at C-4.
  run(v, byteParams(&bc, 60, 0, 0, 0, 0, 1.f / 127.f), out1, kSec);
  TEST_ASSERT_TRUE(v.lastTick() < 1024);
  for (int i = kSec / 2; i < kSec / 2 + 4096; ++i) TEST_ASSERT_FLOAT_WITHIN(1e-3f, out1[i], out1[i + 4096]);
  // Off: t runs on.
  LofiVoice w;
  w.trigger(1);
  run(w, byteParams(&bc, 60), out2, kSec);
  TEST_ASSERT_TRUE(w.lastTick() > 7000);
  // Top: 2^20.
  LofiVoice x;
  x.trigger(1);
  run(x, byteParams(&bc, 60, 0, 0, 0, 0, 1.f), out2, 100);
  TEST_ASSERT_EQUAL_UINT32((1u << 20) - 1, x.loopMask());
}

void test_loop_steps() {
  TEST_ASSERT_EQUAL_INT(0, lofiLoopBits(0));
  TEST_ASSERT_EQUAL_INT(0, lofiLoopSteps(0));
  TEST_ASSERT_EQUAL_INT(10, lofiLoopBits(1));
  TEST_ASSERT_EQUAL_INT(1, lofiLoopSteps(1));
  TEST_ASSERT_EQUAL_INT(20, lofiLoopBits(127));
  TEST_ASSERT_EQUAL_INT(1024, lofiLoopSteps(127));
  // Monotonic, powers of two, every length 1..1024 reachable.
  int seen = 0;
  for (int v = 1; v <= 127; ++v) {
    const int s = lofiLoopSteps(v);
    TEST_ASSERT_TRUE(s >= lofiLoopSteps(v - 1));
    TEST_ASSERT_EQUAL_INT(1 << (lofiLoopBits(v) - 10), s);
    seen |= s;
  }
  TEST_ASSERT_EQUAL_INT(2047, seen);
}

void test_byte_tone_reduces_hf() {
  const ByteCode bc = compile("t*(t>>5|t>>8)");
  LofiVoice a, b, c;
  a.trigger(1);
  b.trigger(1);
  c.trigger(1);
  run(a, byteParams(&bc, 60, 0, 0, 0, 0), out1, kSec);
  const double raw = hfEnergy(out1, 0, kSec);
  run(b, byteParams(&bc, 60, 0, 0, 0, 0.3f), out2, kSec);
  const double mid = hfEnergy(out2, 0, kSec);
  run(c, byteParams(&bc, 60, 0, 0, 0, 1.f), out2, kSec);
  const double high = hfEnergy(out2, 0, kSec);
  TEST_ASSERT_TRUE(raw > 0);
  TEST_ASSERT_TRUE(mid < raw * 0.8);
  TEST_ASSERT_TRUE(high < mid * 0.5);
}

void test_byte_tone_zero_holds_steps() {
  const ByteCode bc = compile("t*7");
  LofiVoice v;
  v.trigger(1);
  run(v, byteParams(&bc, 60), out1, 4000);
  // C-4: one value per 4 samples; the DC blocker alone moves within a step by < 1 % (a few steps
  // vanish where the blocker's overshoot of the saw's wrap is clipped).
  int steps = 0;
  for (int i = 1000; i < 4000; ++i)
    if (fabsf(out1[i] - out1[i - 1]) > 0.02f) ++steps;
  TEST_ASSERT_TRUE(steps >= 700 && steps <= 750);
}

void test_byte_abc_change_output() {
  const char* src[3] = {"t*(a>>3|1)", "t*(b>>3|1)", "t*(c>>3|1)"};
  for (int k = 0; k < 3; ++k) {
    const ByteCode bc = compile(src[k]);
    float m0[3] = {0, 0, 0}, m1[3] = {0, 0, 0};
    m1[k] = 1.f;
    LofiVoice v, w;
    v.trigger(1);
    w.trigger(1);
    run(v, byteParams(&bc, 60, m0[0], m0[1], m0[2]), out1, 8000);
    run(w, byteParams(&bc, 60, m1[0], m1[1], m1[2]), out2, 8000);
    double d = 0;
    for (int i = 0; i < 8000; ++i) d += fabsf(out1[i] - out2[i]);
    TEST_ASSERT_TRUE(d > 100);
  }
}

void test_byte_bounded_for_every_formula() {
  uint32_t r = 7;
  for (int f = 0; f < byteFormulaCount(); ++f) {
    for (int note = 0; note <= 127; note += 21) {
      float m[kFmMacros];
      for (float& x : m) {
        r = r * 1664525u + 1013904223u;
        x = (r >> 8) * (1.f / 16777216.f);
      }
      LofiVoice v;
      v.trigger(r);
      run(v, byteParams(&byteCode(f), static_cast<float>(note), m[0], m[1], m[2], m[3], m[4]), out1, 8000);
      for (int i = 0; i < 8000; ++i) {
        TEST_ASSERT_FALSE(isnan(out1[i]));
        TEST_ASSERT_TRUE(fabsf(out1[i]) <= 1.f);
      }
    }
  }
}

void test_byte_no_code_is_silent() {
  LofiVoice v;
  v.trigger(1);
  run(v, byteParams(nullptr, 60), out1, 1000);
  for (int i = 0; i < 1000; ++i) TEST_ASSERT_EQUAL_FLOAT(0.f, out1[i]);
}

void test_byte_dc_blocked() {
  const ByteCode bc = compile("t>>4&1?255:160");  // all above the midpoint: heavy DC
  LofiVoice v;
  v.trigger(1);
  run(v, byteParams(&bc, 60), out1, 2 * kSec);
  double s = 0;
  for (int i = kSec; i < 2 * kSec; ++i) s += out1[i];
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.f, static_cast<float>(s / kSec));
}

// Unsigned "t" starts at raw 0 = -1: the note-on must not step (the DC blocker starts from the
// first value), a C-4 ramp moves 1/128 per 4 samples.
void test_byte_note_on_has_no_step() {
  const ByteCode bc = compile("t");
  LofiVoice v;
  v.trigger(1);
  run(v, byteParams(&bc, 60), out1, 256);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.f, out1[0]);
  for (int i = 1; i < 256; ++i) TEST_ASSERT_FLOAT_WITHIN(0.05f, out1[i - 1], out1[i]);
}

// A retrigger mid-ramp (value ~ +0.76 -> -1) continues from the current output.
void test_byte_retrigger_has_no_step() {
  const ByteCode bc = compile("t");
  LofiVoice v;
  v.trigger(1);
  run(v, byteParams(&bc, 60), out1, 900);
  const float before = out1[899];
  v.trigger(2, true);
  run(v, byteParams(&bc, 60), out2, 256);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, before, out2[0]);
  for (int i = 1; i < 256; ++i) TEST_ASSERT_FLOAT_WITHIN(0.05f, out2[i - 1], out2[i]);
}

// A fresh note on a voice that played before (its blocker state is stale) starts from silence.
void test_byte_fresh_trigger_starts_from_zero() {
  const ByteCode bc = compile("t");
  LofiVoice v;
  v.trigger(1);
  run(v, byteParams(&bc, 60), out1, 900);
  v.trigger(2, false);
  run(v, byteParams(&bc, 60), out2, 256);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.f, out2[0]);
  for (int i = 1; i < 256; ++i) TEST_ASSERT_FLOAT_WITHIN(0.05f, out2[i - 1], out2[i]);
}

// A step to a constant (silence at TONE > 0): the DC blocker and TONE decay to exactly 0 at a
// control block's end, no output sample is a denormal.
void test_byte_decay_has_no_denormals() {
  const ByteCode bc = compile("t<4000?255:128");  // C-4: 0.5 s high, then the midpoint (0)
  LofiVoice v;
  v.trigger(1);
  run(v, byteParams(&bc, 60, 0, 0, 0, 0.5f), out1, 2 * kSec);
  for (int i = 0; i < 2 * kSec; ++i) TEST_ASSERT_TRUE(fpclassify(out1[i]) != FP_SUBNORMAL);
  TEST_ASSERT_EQUAL_FLOAT(0.f, out1[2 * kSec - 1]);
}

void test_apc_small_width_plays_the_note() {
  LofiVoice v;
  v.trigger(1);
  run(v, apcParams(220.f, widthFor(0.1f)), out1, kSec);
  TEST_ASSERT_INT_WITHIN(2, 220, risingEdges(out1, 0, kSec));
}

void test_apc_wide_pulse_swallows_edges() {
  LofiVoice v;
  v.trigger(1);
  run(v, apcParams(220.f, widthFor(1.5f)), out1, kSec);
  TEST_ASSERT_INT_WITHIN(2, 110, risingEdges(out1, 0, kSec));
  LofiVoice w;
  w.trigger(1);
  run(w, apcParams(220.f, widthFor(2.5f)), out1, kSec);
  TEST_ASSERT_INT_WITHIN(2, 73, risingEdges(out1, 0, kSec));
}

void test_apc_smooth_reduces_hf() {
  LofiVoice a, b;
  a.trigger(1);
  b.trigger(1);
  run(a, apcParams(1234.5f, widthFor(0.4f), 1, 0, 0), out1, kSec);
  run(b, apcParams(1234.5f, widthFor(0.4f), 1, 0, 1), out2, kSec);
  TEST_ASSERT_TRUE(hfEnergy(out2, 100, kSec) < hfEnergy(out1, 100, kSec) * 0.8);
}

void test_apc_tone_reduces_hf() {
  LofiVoice a, b;
  a.trigger(1);
  b.trigger(1);
  run(a, apcParams(440.f, widthFor(0.4f), 1), out1, kSec);
  run(b, apcParams(440.f, widthFor(0.4f), 0.3f), out2, kSec);
  TEST_ASSERT_TRUE(hfEnergy(out2, 100, kSec) < hfEnergy(out1, 100, kSec) * 0.5);
}

void test_apc_drift_zero_is_stable() {
  LofiVoice v;
  v.trigger(1);
  run(v, apcParams(440.f, widthFor(0.3f)), out1, 2 * kSec);
  // Every pulse start the same distance apart (to a sample).
  int last = -1, minD = 1 << 30, maxD = 0;
  for (int i = 1; i < 2 * kSec; ++i)
    if (out1[i] - out1[i - 1] > 0.3f) {
      if (last >= 0) {
        minD = i - last < minD ? i - last : minD;
        maxD = i - last > maxD ? i - last : maxD;
      }
      last = i;
    }
  TEST_ASSERT_TRUE(maxD - minD <= 1);
}

void test_apc_drift_wanders_within_50_cents() {
  LofiVoice v;
  v.trigger(5);
  int lo = 1 << 30, hi = 0;
  for (int w = 0; w < 8; ++w) {  // 8 x 0.5 s windows
    run(v, apcParams(440.f, widthFor(0.3f), 1, 1.f), out1, kSec / 2);
    const int e = risingEdges(out1, 0, kSec / 2);
    lo = e < lo ? e : lo;
    hi = e > hi ? e : hi;
  }
  TEST_ASSERT_TRUE(hi > lo);  // it moves
  // +-50 cents of 220 edges per window: 207.6 .. 233.1.
  TEST_ASSERT_TRUE(lo >= 206 && hi <= 235);
}

void test_apc_deterministic_per_seed() {
  LofiVoice a, b;
  a.trigger(9);
  b.trigger(9);
  run(a, apcParams(330.f, widthFor(0.7f), 0.8f, 1.f, 0.5f), out1, kSec);
  run(b, apcParams(330.f, widthFor(0.7f), 0.8f, 1.f, 0.5f), out2, kSec);
  TEST_ASSERT_EQUAL_MEMORY(out1, out2, sizeof(float) * kSec);
}

void test_apc_bounded() {
  uint32_t r = 3;
  for (int k = 0; k < 40; ++k) {
    float m[4];
    for (float& x : m) {
      r = r * 1664525u + 1013904223u;
      x = (r >> 8) * (1.f / 16777216.f);
    }
    const float hz = 8.f * powf(2.f, (k % 12));  // 8 Hz .. 16 kHz
    LofiVoice v;
    v.trigger(r);
    run(v, apcParams(hz, m[0], m[1], m[2], m[3]), out1, 8000);
    for (int i = 0; i < 8000; ++i) {
      TEST_ASSERT_FALSE(isnan(out1[i]));
      TEST_ASSERT_TRUE(fabsf(out1[i]) <= 1.f);
    }
  }
}

void test_apc_dc_blocked() {
  LofiVoice v;
  v.trigger(1);
  run(v, apcParams(220.f, widthFor(0.5f)), out1, 2 * kSec);
  double s = 0;
  for (int i = kSec; i < 2 * kSec; ++i) s += out1[i];
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.f, static_cast<float>(s / kSec));
}

// A full-scale APC square (WIDTH half a period) is as loud as a full-scale BYTE square (0 / 255).
void test_apc_level_matches_byte() {
  const ByteCode sq = compile("t<<3&128?255:0");  // 250 Hz at C-4
  LofiVoice b, a;
  b.trigger(1);
  a.trigger(1);
  run(b, byteParams(&sq, 60), out1, kSec);
  run(a, apcParams(250.f, widthFor(0.5f)), out2, kSec);
  double eb = 0, ea = 0;
  for (int i = kSec / 2; i < kSec; ++i) eb += double(out1[i]) * out1[i], ea += double(out2[i]) * out2[i];
  const double r = sqrt(ea / eb);
  TEST_ASSERT_TRUE(r > 0.8 && r < 1.25);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_voice_is_trivially_copyable);
  RUN_TEST(test_byte_c4_evaluates_8000_per_second);
  RUN_TEST(test_byte_octave_up_doubles_rate);
  RUN_TEST(test_byte_evaluates_at_most_once_per_sample);
  RUN_TEST(test_byte_trigger_resets_t);
  RUN_TEST(test_byte_loop_wraps_t);
  RUN_TEST(test_loop_steps);
  RUN_TEST(test_byte_tone_reduces_hf);
  RUN_TEST(test_byte_tone_zero_holds_steps);
  RUN_TEST(test_byte_abc_change_output);
  RUN_TEST(test_byte_bounded_for_every_formula);
  RUN_TEST(test_byte_no_code_is_silent);
  RUN_TEST(test_byte_dc_blocked);
  RUN_TEST(test_byte_note_on_has_no_step);
  RUN_TEST(test_byte_retrigger_has_no_step);
  RUN_TEST(test_byte_fresh_trigger_starts_from_zero);
  RUN_TEST(test_byte_decay_has_no_denormals);
  RUN_TEST(test_apc_small_width_plays_the_note);
  RUN_TEST(test_apc_wide_pulse_swallows_edges);
  RUN_TEST(test_apc_smooth_reduces_hf);
  RUN_TEST(test_apc_tone_reduces_hf);
  RUN_TEST(test_apc_drift_zero_is_stable);
  RUN_TEST(test_apc_drift_wanders_within_50_cents);
  RUN_TEST(test_apc_deterministic_per_seed);
  RUN_TEST(test_apc_bounded);
  RUN_TEST(test_apc_dc_blocked);
  RUN_TEST(test_apc_level_matches_byte);
  return UNITY_END();
}
