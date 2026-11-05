#include <unity.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include "synth.h"
#include "inst_codec.h"
#include "synth_params.h"
#include "synth_preview.h"

using namespace mt;
constexpr uint8_t kHyper = 5;  // appended persistent oscillator ID
static float a[16384], b[16384];
static SynthModel model;
static Synth* engine;
void setUp() { engine = nullptr; model = SynthModel(); }
void tearDown() { delete engine; }

static SynParams hyper(float density = 1.f, float spread = 1.f) {
  SynParams p;
  p.mode[0] = kHyper;
  p.hz[0] = 440;
  p.shape[0] = density;
  p.shape[1] = spread;
  return p;
}
static void render(SynVoice& voice, const SynParams& p, float* out, int n = 16384) {
  std::fill(out, out + n, 0.f);
  voice.trigger();
  voice.control(p, 32);
  voice.render(out, n, 1.f);
}
static double tone(const float* out, float hz) {
  double re = 0, im = 0;
  for (int i = 0; i < 16384; ++i) {
    const double angle = 6.283185307179586 * hz * i / kSynthRate;
    const double win = .5 - .5 * std::cos(6.283185307179586 * i / 16383);
    re += out[i] * win * std::cos(angle);
    im += out[i] * win * std::sin(angle);
  }
  return re * re + im * im;
}
void test_hyper_id_roundtrip_and_no_second_group_in_files() {
  Instrument m, decoded;
  m.synOsc[0] = m.synOsc[1] = kHyper;
  uint8_t record[kSynRecSize];
  packSyn(m, record);
  unpackSyn(record, decoded);
  TEST_ASSERT_EQUAL(kHyper, decoded.synOsc[0]);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(SynOsc::Saw), decoded.synOsc[1]);
}
void test_one_saw_density_matches_legacy_saw() {
  SynVoice h, saw;
  SynParams p = hyper(0.f);
  render(h, p, a);
  p.mode[0] = static_cast<uint8_t>(SynOsc::Saw);
  render(saw, p, b);
  for (int i = 0; i < 16384; ++i) TEST_ASSERT_FLOAT_WITHIN(2e-5f, b[i], a[i]);
}
void test_nine_saws_detune_has_side_fundamentals() {
  SynVoice h;
  render(h, hyper(), a);
  const double center = tone(a, 440);
  const double lower = tone(a, 440.f * std::exp2(-137.5f / 1200.f));
  const double upper = tone(a, 440.f * std::exp2(137.5f / 1200.f));
  TEST_ASSERT_TRUE(center > 100);
  TEST_ASSERT_TRUE(lower > center * .08);
  TEST_ASSERT_TRUE(upper > center * .08);
}
void test_zero_detune_retrigger_keeps_coherent_phase() {
  SynVoice h, saw;
  SynParams p = hyper(1.f, 0.f);
  render(h, p, a);
  p.mode[0] = static_cast<uint8_t>(SynOsc::Saw);
  render(saw, p, b);
  for (int i = 0; i < 16384; ++i) TEST_ASSERT_FLOAT_WITHIN(5e-5f, 3.f * b[i], a[i]);
}
void test_hyper_ignores_osc2_mix_ring_and_sync() {
  SynVoice first, second;
  SynParams p = hyper();
  render(first, p, a);
  p.mode[1] = kHyper;
  p.hz[1] = 9300;
  p.mix = p.ring = 1;
  p.sync = true;
  render(second, p, b);
  for (int i = 0; i < 16384; ++i) TEST_ASSERT_FLOAT_WITHIN(1e-6f, a[i], b[i]);
}
void test_hyper_phase_retrigger_random_and_free() {
  SynVoice h;
  const SynParams p = hyper();
  render(h, p, a);
  render(h, p, b);
  TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof a);
  h.trigger(static_cast<uint8_t>(SynPhaseMode::Random), 0, 0, 123);
  h.control(p, 32);
  std::fill(b, b + 16384, 0.f); h.render(b, 16384, 1);
  TEST_ASSERT_TRUE(std::memcmp(a, b, sizeof a) != 0);
  SynVoice same = h;
  h.trigger(static_cast<uint8_t>(SynPhaseMode::Free), 0, 0, 0, true);
  h.control(p, 32); same.control(p, 32);
  std::fill(a, a + 16384, 0.f); std::fill(b, b + 16384, 0.f);
  h.render(a, 16384, 1); same.render(b, 16384, 1);
  TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof a);
}
void test_hyper_density_automation_is_finite_bounded_and_split_stable() {
  SynVoice first, second;
  SynParams p = hyper(0);
  first.trigger(); second.trigger(); first.control(p, 32); second.control(p, 32);
  first.render(a, 32, 1); second.render(b, 32, 1);
  p.shape[0] = 1;
  first.control(p, 32); second.control(p, 32);
  std::fill(a, a + 128, 0.f); std::fill(b, b + 128, 0.f);
  first.render(a, 32, 1);
  int at = 0;
  for (int n : {3, 1, 7, 21}) { second.render(b + at, n, 1); at += n; }
  for (int i = 0; i < 32; ++i) {
    TEST_ASSERT_TRUE(std::isfinite(a[i]) && std::fabs(a[i]) < 3.1f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, a[i], b[i]);
  }
}
void test_hyper_high_pitch_stays_finite() {
  for (float hz : {10.f, 7040.f, 19000.f, 100000.f}) {
    SynVoice h; SynParams p = hyper(); p.hz[0] = hz;
    render(h, p, a);
    for (float sample : a) TEST_ASSERT_TRUE(std::isfinite(sample) && std::fabs(sample) < 3.1f);
  }
}
void test_shape_envelope_changes_density_without_changing_spread() {
  SynParams p = hyper(.2f, .7f);
  synApplyEnv(p, static_cast<uint8_t>(SynEnvDest::Shape), .3f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, .5f, p.shape[0]);
  TEST_ASSERT_EQUAL_FLOAT(.7f, p.shape[1]);
}
static void setupEngine(bool hs) {
  Instrument& m = model.instruments[0]; instrSetType(m, InstrType::Synth);
  m.synOsc[0] = hs ? kHyper : 0;
  m.attack = m.decay = 0; m.sustain = 127; m.release = 96; m.fltMode = 0;
  m.macro[kMacShp1] = 127;
  for (auto& track : model.tracks) { track.out = TrackOut::Int; track.instr = 0; }
  engine = new Synth(model); engine->setVoiceBudget(kVoiceBudgetOff);
}
static void block() { int16_t l[Synth::kBlock], r[Synth::kBlock]; engine->render(l, r); }
static void note(int i) {
  const uint8_t bytes[] = {0x90, static_cast<uint8_t>(48 + i % 4), 100};
  TEST_ASSERT_TRUE(engine->event(0, i / 4, bytes, 3)); block();
}
static int liveHeavy() {
  int count = 0; for (int i = 0; i < kVoices; ++i) count += heavyLoad(engine->voice(i)); return count;
}
void test_hyper_uses_shared_heavy_voice_cap() {
  setupEngine(true);
  for (int i = 0; i < 16; ++i) note(i);
  TEST_ASSERT_EQUAL(kFmVoiceMax, liveHeavy());
}
void test_switching_live_synth_to_hyper_enforces_heavy_cap() {
  setupEngine(false);
  for (int i = 0; i < 16; ++i) note(i);
  TEST_ASSERT_EQUAL(0, liveHeavy());
  model.instruments[0].synOsc[0] = kHyper; block();
  TEST_ASSERT_EQUAL(kFmVoiceMax, liveHeavy());
}
void test_hyper_reserves_cpu_budget_and_ignores_ring_cost() {
  Instrument m; instrSetType(m, InstrType::Synth); m.synOsc[0] = kHyper;
  const int cost = voiceCost(m, false);
  TEST_ASSERT_TRUE(cost >= 300 && cost <= 400);
  m.synOsc[1] = static_cast<uint8_t>(SynOsc::Wt); m.synRing = 127;
  TEST_ASSERT_EQUAL(cost, voiceCost(m, false));
}
void test_hyper_preview_hides_inactive_second_oscillator() {
  SynParams p = hyper(); p.mode[1] = static_cast<uint8_t>(SynOsc::Wt); p.mix = p.ring = 1; p.sync = true;
  SynPreviewModel preview; SynPreviewFrame frame; SynPreviewPhase phase;
  preview.render(p, phase, false, frame);
  TEST_ASSERT_FALSE(frame.missing[1]);
  for (float x : frame.osc[1]) TEST_ASSERT_EQUAL_FLOAT(0.f, x);
}
void test_budget_allows_sixteen_plain_but_reserves_measured_heavy_modulation() {
  Instrument m; instrSetType(m, InstrType::Synth); m.synOsc[0] = kHyper;
  TEST_ASSERT_TRUE(16 * voiceCost(m, false) <= kVoiceBudgetTeensy);
  m.synFold = m.synSub = 32; m.synNoise = 8;
  m.lfoDest = static_cast<uint8_t>(LfoDest::Dec); m.lfoDepth = m.lfoRate = 127;
  // The full16voice modulated fold/FX stress required the runtime guard: reserve it upfront.
  TEST_ASSERT_TRUE(16 * voiceCost(m, false) > kVoiceBudgetTeensy);
}
void test_detuned_density_preserves_measured_rms_level() {
  SynVoice h;
  render(h, hyper(0.f), a);
  double reference = 0, group = 0;
  for (int i = 0; i < 16384; ++i) reference += a[i] * a[i];
  render(h, hyper(), a);
  for (int i = 0; i < 16384; ++i) group += a[i] * a[i];
  TEST_ASSERT_FLOAT_WITHIN(.08f, 1.f, std::sqrt(group / reference));
}
void test_density_midpoint_adds_three_measured_saws() {
  SynVoice h;
  render(h, hyper(64.f / 127.f), a);
  // Independent raw64 measurement: three full saws near -62.7 / -0.4 / +61.9 cents.
  const double low = tone(a, 440.f * std::exp2(-62.7f / 1200.f));
  const double middle = tone(a, 440.f);
  const double high = tone(a, 440.f * std::exp2(61.9f / 1200.f));
  TEST_ASSERT_TRUE(low > middle * .5 && high > middle * .5);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_detuned_density_preserves_measured_rms_level);
  RUN_TEST(test_density_midpoint_adds_three_measured_saws);
  RUN_TEST(test_hyper_id_roundtrip_and_no_second_group_in_files);
  RUN_TEST(test_one_saw_density_matches_legacy_saw);
  RUN_TEST(test_nine_saws_detune_has_side_fundamentals);
  RUN_TEST(test_zero_detune_retrigger_keeps_coherent_phase);
  RUN_TEST(test_hyper_ignores_osc2_mix_ring_and_sync);
  RUN_TEST(test_hyper_phase_retrigger_random_and_free);
  RUN_TEST(test_hyper_density_automation_is_finite_bounded_and_split_stable);
  RUN_TEST(test_hyper_high_pitch_stays_finite);
  RUN_TEST(test_shape_envelope_changes_density_without_changing_spread);
  RUN_TEST(test_hyper_uses_shared_heavy_voice_cap);
  RUN_TEST(test_switching_live_synth_to_hyper_enforces_heavy_cap);
  RUN_TEST(test_hyper_reserves_cpu_budget_and_ignores_ring_cost);
  RUN_TEST(test_hyper_preview_hides_inactive_second_oscillator);
  RUN_TEST(test_budget_allows_sixteen_plain_but_reserves_measured_heavy_modulation);
  return UNITY_END();
}
