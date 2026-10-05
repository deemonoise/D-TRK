#include <math.h>
#include <unity.h>
#include "synth_fm_machines.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static float buf[4000];

static void macros(float* m, float v) {
  for (int k = 0; k < kFmMacros; ++k) m[k] = v;
}

void test_all_machines_finite() {
  const float vals[] = {0, 64, 127};
  const float notes[] = {24, 60, 108};
  for (int mc = 0; mc < static_cast<int>(FmMachine::Count); ++mc)
    for (float v : vals)
      for (float note : notes) {
        float m[kFmMacros];
        macros(m, v);
        FmParams p;
        fmMachine(static_cast<uint8_t>(mc), m, note, p);
        FmVoice fv;
        fv.trigger(false);
        for (int i = 0; i < 4000; ++i) buf[i] = 0;
        for (int pos = 0; pos < 4000; pos += 32) {
          fmMachine(static_cast<uint8_t>(mc), m, note, p);
          fv.control(p, 32);
          fv.render(buf + pos, 32, 1.f);
        }
        for (int i = 0; i < 4000; ++i) {
          TEST_ASSERT_FALSE(isnan(buf[i]) || isinf(buf[i]));
          TEST_ASSERT_TRUE(fabsf(buf[i]) < 4.f);
        }
      }
}

void test_one_shot_vs_gated() {
  float m[kFmMacros];
  macros(m, 64);
  FmParams p;
  for (int mc = 0; mc < static_cast<int>(FmMachine::Count); ++mc) {
    fmMachine(static_cast<uint8_t>(mc), m, 60, p);
    TEST_ASSERT_EQUAL(!fmGated(static_cast<uint8_t>(mc)), p.oneShot);
  }
}

void test_kick_color_raises_index() {
  float m[kFmMacros];
  macros(m, 64);
  FmParams a, b, c;
  m[kMacCol] = 0;
  fmMachine(static_cast<uint8_t>(FmMachine::Kick), m, 60, a);
  m[kMacCol] = 64;
  fmMachine(static_cast<uint8_t>(FmMachine::Kick), m, 60, b);
  m[kMacCol] = 127;
  fmMachine(static_cast<uint8_t>(FmMachine::Kick), m, 60, c);
  TEST_ASSERT_TRUE(a.op[1].level < b.op[1].level);
  TEST_ASSERT_TRUE(b.op[1].level < c.op[1].level);
}

void test_kick_pitch_follows_note() {
  float m[kFmMacros];
  macros(m, 64);
  FmParams p;
  fmMachine(static_cast<uint8_t>(FmMachine::Kick), m, 60, p);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 55.f, p.hz);
  fmMachine(static_cast<uint8_t>(FmMachine::Kick), m, 72, p);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 110.f, p.hz);
}

void test_chord_maj_ratios() {
  float m[kFmMacros];
  macros(m, 64);
  m[kMacShp] = 0;
  FmParams p;
  fmMachine(static_cast<uint8_t>(FmMachine::Chord), m, 60, p);
  TEST_ASSERT_TRUE(p.alg == FmAlg::Additive);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.f, p.op[0].ratio);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.2599f, p.op[1].ratio);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.4983f, p.op[2].ratio);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.f, p.op[3].ratio);
  TEST_ASSERT_EQUAL_STRING("MAJ", fmChordName(0));
  TEST_ASSERT_EQUAL_STRING("MI9", fmChordName(127));
  TEST_ASSERT_EQUAL_STRING("MI7", fmChordName(5 * 127 / 12 + 1));
}

void test_hat_shape_mixes_metal_and_noise() {
  float m[kFmMacros];
  macros(m, 64);
  FmParams p;
  m[kMacShp] = 0;
  fmMachine(static_cast<uint8_t>(FmMachine::Hat), m, 60, p);
  TEST_ASSERT_EQUAL_FLOAT(0, p.noise);
  TEST_ASSERT_TRUE(p.op[0].level > 0);
  m[kMacShp] = 127;
  fmMachine(static_cast<uint8_t>(FmMachine::Hat), m, 60, p);
  TEST_ASSERT_EQUAL_FLOAT(1, p.noise);
  TEST_ASSERT_EQUAL_FLOAT(0, p.op[0].level);
  TEST_ASSERT_TRUE(p.filterAll);
}

void test_decay_macro_lengthens() {
  float m[kFmMacros];
  macros(m, 64);
  FmParams a, b;
  m[kMacDec] = 10;
  fmMachine(static_cast<uint8_t>(FmMachine::Snare), m, 60, a);
  m[kMacDec] = 110;
  fmMachine(static_cast<uint8_t>(FmMachine::Snare), m, 60, b);
  TEST_ASSERT_TRUE(b.ampMs > a.ampMs * 10);
}

// Renders a machine's first n samples into out (zeroed first).
static void render(uint8_t mc, const float* m, float note, float* out, int n) {
  FmParams p;
  FmVoice fv;
  fv.trigger(false);
  for (int i = 0; i < n; ++i) out[i] = 0;
  for (int pos = 0; pos < n; pos += 32) {
    fmMachine(mc, m, note, p);
    fv.control(p, 32);
    fv.render(out + pos, 32, 1.f);
  }
}

void test_out_of_range_machine_is_kick() {
  float m[kFmMacros];
  macros(m, 64);
  FmParams a, b;
  fmMachine(200, m, 60, a);
  fmMachine(static_cast<uint8_t>(FmMachine::Kick), m, 60, b);
  TEST_ASSERT_EQUAL_FLOAT(b.hz, a.hz);
  TEST_ASSERT_TRUE(a.alg == b.alg);
  TEST_ASSERT_EQUAL_FLOAT(b.ampMs, a.ampMs);
  TEST_ASSERT_EQUAL_FLOAT(b.pitchEnv, a.pitchEnv);
  for (int i = 0; i < kFmOps; ++i) {
    TEST_ASSERT_EQUAL_FLOAT(b.op[i].ratio, a.op[i].ratio);
    TEST_ASSERT_EQUAL_FLOAT(b.op[i].level, a.op[i].level);
  }
  static float other[4000];
  render(200, m, 60, buf, 4000);
  render(static_cast<uint8_t>(FmMachine::Kick), m, 60, other, 4000);
  for (int i = 0; i < 4000; ++i) TEST_ASSERT_EQUAL_FLOAT(other[i], buf[i]);
}

void test_every_machine_sounds() {
  const float m[kFmMacros] = {85, 40, 32, 70, 50};  // Instrument defaults
  for (int mc = 0; mc < static_cast<int>(FmMachine::Count); ++mc) {
    render(static_cast<uint8_t>(mc), m, 60, buf, 4000);
    double sum = 0;
    for (int i = 0; i < 4000; ++i) sum += static_cast<double>(buf[i]) * buf[i];
    TEST_ASSERT_TRUE_MESSAGE(sqrt(sum / 4000) > 0.01, "silent machine");
  }
}

// Carrier feedback peak (fb x (1 + fbEnv)) stays below the chaotic range at any COLOR/SWEEP/note.
void test_feedback_peak_bounded() {
  const FmMachine mcs[] = {FmMachine::Tone, FmMachine::Chord, FmMachine::Hat, FmMachine::Kick};
  const float notes[] = {24, 60, 108};
  float m[kFmMacros];
  for (FmMachine mc : mcs)
    for (float note : notes)
      for (int shp = 0; shp <= 127; shp += 9) {
        macros(m, 127);
        m[kMacShp] = static_cast<float>(shp);
        FmParams p;
        fmMachine(static_cast<uint8_t>(mc), m, note, p);
        for (int i = 0; i < kFmOps; ++i) TEST_ASSERT_TRUE(p.op[i].fb * (1 + p.fbEnv) <= 1.8f);
      }
}

// SWEEP/CONTOUR shape the TONE saw even at COLOR 0.
void test_tone_sweep_at_zero_color() {
  float m[kFmMacros];
  macros(m, 64);
  m[kMacShp] = 127 * 1.5f / 5;  // zone 1: feedback saw
  m[kMacCol] = 0;
  m[kMacSwp] = 127;
  FmParams p;
  fmMachine(static_cast<uint8_t>(FmMachine::Tone), m, 60, p);
  TEST_ASSERT_TRUE(p.op[0].fb > 0);
  TEST_ASSERT_TRUE(p.op[0].fb * (1 + p.fbEnv) > 1.f);
  m[kMacSwp] = 0;
  fmMachine(static_cast<uint8_t>(FmMachine::Tone), m, 60, p);
  TEST_ASSERT_EQUAL_FLOAT(0, p.op[0].fb * (1 + p.fbEnv));
}

// One-shot parts multiply with the amp decay: the total length follows DECAY, not about half of it.
void test_decay_not_doubled() {
  float m[kFmMacros];
  macros(m, 64);
  m[kMacCon] = 127;  // HAT/METAL: parts as long as the amp
  const FmMachine mcs[] = {FmMachine::Metal, FmMachine::Hat};
  for (FmMachine mc : mcs) {
    FmParams p;
    fmMachine(static_cast<uint8_t>(mc), m, 60, p);
    TEST_ASSERT_EQUAL_FLOAT(0, p.op[0].decayMs);
  }
  FmParams p;
  fmMachine(static_cast<uint8_t>(FmMachine::Clap), m, 60, p);
  TEST_ASSERT_EQUAL_FLOAT(0, p.noiseMs);  // CLAP: the amp decay (after the bursts) alone
  TEST_ASSERT_EQUAL_FLOAT(5.f * powf(800.f, 64.f / 127.f), p.ampMs);
  fmMachine(static_cast<uint8_t>(FmMachine::Snare), m, 60, p);  // CONTOUR 127: noise rings the longest
  TEST_ASSERT_EQUAL_FLOAT(0, p.noiseMs);
}

// CLAP at low DECAY, loosest SHAPE: the amp decay leaves the last burst within 6 dB (RMS) of the first.
void test_clap_bursts_even() {
  float m[kFmMacros];
  macros(m, 64);
  m[kMacDec] = 0;
  m[kMacShp] = 127;
  FmParams p;
  fmMachine(static_cast<uint8_t>(FmMachine::Clap), m, 60, p);
  const int burst = static_cast<int>(p.burstMs * kSynthRate / 1000.f);
  const int n = burst * p.bursts;
  static float out[8192];
  TEST_ASSERT_TRUE(n <= 8192 - 32);
  render(static_cast<uint8_t>(FmMachine::Clap), m, 60, out, (n + 31) / 32 * 32);
  double first = 0, last = 0;  // energy of each burst (noise peaks vary too much)
  for (int i = 0; i < burst; ++i) {
    first += static_cast<double>(out[i]) * out[i];
    last += static_cast<double>(out[n - burst + i]) * out[n - burst + i];
  }
  TEST_ASSERT_TRUE(first > 0);
  TEST_ASSERT_TRUE(last > first * 0.25);  // RMS within 6 dB
}

// CLAP voice frees right after its tail: bursts + DECAY (to -80 dB: x 4/3) + a little.
void test_clap_done_after_tail() {
  float m[kFmMacros];
  macros(m, 64);
  const float decs[] = {0, 64, 100};
  for (float d : decs) {
    m[kMacDec] = d;
    FmParams p;
    fmMachine(static_cast<uint8_t>(FmMachine::Clap), m, 60, p);
    const float limitMs = p.bursts * p.burstMs + p.ampMs * 4.f / 3.f + 5.f;
    const int limit = static_cast<int>(limitMs * kSynthRate / 1000.f);
    FmVoice fv;
    fv.trigger(false);
    int t = 0;
    while (!fv.done() && t < limit + 64) {
      fmMachine(static_cast<uint8_t>(FmMachine::Clap), m, 60, p);
      fv.control(p, 32);
      fv.render(buf, 32, 1.f);
      t += 32;
    }
    TEST_ASSERT_TRUE(fv.done());
    TEST_ASSERT_TRUE(t <= limit);
    TEST_ASSERT_TRUE(t >= static_cast<int>(p.bursts * p.burstMs * kSynthRate / 1000.f));
  }
}

// High notes: all HAT partials and the KICK sweep peak stay below ~13 kHz.
void test_high_pitch_partials_audible() {
  float m[kFmMacros];
  macros(m, 127);
  FmParams p;
  for (float note = 60; note <= 127; note += 6) {
    fmMachine(static_cast<uint8_t>(FmMachine::Hat), m, note, p);
    for (int i = 0; i < kFmOps; ++i) TEST_ASSERT_TRUE(p.hz * p.op[i].ratio <= 13001.f);
    fmMachine(static_cast<uint8_t>(FmMachine::Kick), m, note, p);
    TEST_ASSERT_TRUE(p.hz * p.op[1].ratio * exp2f(p.pitchEnv / 12) <= 13001.f);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_all_machines_finite);
  RUN_TEST(test_one_shot_vs_gated);
  RUN_TEST(test_kick_color_raises_index);
  RUN_TEST(test_kick_pitch_follows_note);
  RUN_TEST(test_chord_maj_ratios);
  RUN_TEST(test_hat_shape_mixes_metal_and_noise);
  RUN_TEST(test_decay_macro_lengthens);
  RUN_TEST(test_out_of_range_machine_is_kick);
  RUN_TEST(test_every_machine_sounds);
  RUN_TEST(test_feedback_peak_bounded);
  RUN_TEST(test_tone_sweep_at_zero_color);
  RUN_TEST(test_decay_not_doubled);
  RUN_TEST(test_clap_bursts_even);
  RUN_TEST(test_clap_done_after_tail);
  RUN_TEST(test_high_pitch_partials_audible);
  return UNITY_END();
}
