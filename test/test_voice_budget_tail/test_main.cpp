#include <cmath>
#include <cstring>
#include <unity.h>
#define private public
#include "synth.h"
#undef private
#include "bytebeat_lib.h"
using namespace mt;
static SynthModel model;
static Synth *synth;
void setUp() {
  model = SynthModel();
  auto &m = model.instruments[0];
  instrSetType(m, InstrType::Chip);
  m.wave = uint8_t(Wave::Pulse);
  m.mono = true;
  m.vol = m.sustain = 127;
  m.attack = m.decay = m.release = 0;
  m.fltMode = uint8_t(FltMode::Lp);
  m.cutoff = 64;
  for (auto &t : model.tracks) {
    t.out = TrackOut::Int;
    t.instr = 0;
  }
  synth = new Synth(model);
  synth->setVoiceBudget(kVoiceBudgetTeensy);
}
void tearDown() { delete synth; }
static Voice &note() {
  synth->noteOn(0, 60, 127);
  for (auto &v : synth->voices_)
    if (v.on)
      return v;
  TEST_FAIL_MESSAGE("missing voice");
  return synth->voices_[0];
}
void test_cache_keeps_postfilter_individual_last_sample() {
  auto &v = note();
  synth->voices_[1] = v;
  auto &ref = synth->voices_[1];
  ref.on = false;
  float expected = 0;
  for (int b = 0; b < 4; b++) {
    float out[32] = {};
    synth->control(ref, 32);
    synth->renderVoice(ref, out, 32);
    expected = out[31];
  }
  synth->renderCore();
  TEST_ASSERT_TRUE(fabsf(expected) > .01f);
  TEST_ASSERT_FLOAT_WITHIN(.000001f, expected, v.hold);
}
void test_tail_renders_cached_sample_without_refiltering() {
  auto &v = note();
  v.hold = .5f;
  v.stolen = true;
  collapseStealTail(v);
  float out[32] = {};
  Env reference = v.env;
  synth->renderVoice(v, out, 32);
  for (int i = 0; i < 32; i++)
    TEST_ASSERT_FLOAT_WITHIN(.000001f, .5f * reference.next(), out[i]);
  TEST_ASSERT_FALSE(v.fltOn);
}
void test_tail_control_does_not_reactivate_instrument() {
  auto &v = note();
  v.hold = .5f;
  v.stolen = true;
  collapseStealTail(v);
  synth->control(v, 32);
  TEST_ASSERT_TRUE(isStealTail(v));
  TEST_ASSERT_FALSE(v.fltOn);
  TEST_ASSERT_FALSE(v.sample || v.fm || v.drum || v.syn || v.str || v.lofi);
}
void test_reused_tail_is_fresh_note_and_clears_stale_cache() {
  auto &v = note();
  v.hold = .5f;
  v.stolen = true;
  collapseStealTail(v);
  float down[32] = {};
  synth->renderVoice(v, down, 32);
  model.instruments[0].vol = 32;
  synth->noteOn(0, 62, 127);
  TEST_ASSERT_EQUAL(62, v.note);
  TEST_ASSERT_FALSE(v.stolen);
  TEST_ASSERT_FLOAT_WITHIN(.000001f, 32.f / 127, v.ampNow);
  TEST_ASSERT_EQUAL_FLOAT(0, v.ampStep);
  TEST_ASSERT_EQUAL_FLOAT(0, v.hold);
}
void test_fresh_non_sample_reuse_does_not_keep_pcm_hold() {
  auto &v = note();
  v.sample = true;
  v.hold = 32767;
  v.env.kill();
  model.instruments[0].vol = 127;
  synth->noteOn(0, 62, 127);
  TEST_ASSERT_FALSE(v.sample);
  TEST_ASSERT_EQUAL_FLOAT(0, v.hold);
}
void test_dense_byte_notes_bound_actual_full_dsp_cost() {
  delete synth;
  model = SynthModel();
  auto &m = model.instruments[0];
  instrSetType(m, InstrType::Lofi);
  m.machine = uint8_t(LofiMachine::Byte);
  m.mono = false;
  m.vol = m.sustain = 127;
  m.attack = m.decay = m.release = 0;
  int best = 0;
  for (int f = 0; f < byteFormulaCount(); f++)
    if (byteWorkScore(f) > byteWorkScore(best))
      best = f;
  m.lofiFormula = best;
  for (auto &t : model.tracks) {
    t.out = TrackOut::Int;
    t.instr = 0;
  }
  synth = new Synth(model);
  const int budget = 3 * voiceCost(m, false, 0, noteHz(127));
  synth->setVoiceBudget(budget);
  for (int n = 0; n < 12; n++) {
    synth->noteOn(n % 4, 127 - (n % 3), 127);
    int actual = 0;
    for (const auto &v : synth->voices_)
      if (v.on && !v.env.idle() && !isStealTail(v))
        actual += v.cost;
    TEST_ASSERT_TRUE(actual <= budget);
  }
}

void test_tail_linear_duration_and_reuse_attack() {
  auto &v = note();
  v.hold = .5f;
  collapseStealTail(v);
  Env reference = v.env;
  for (int at = 0; at < 177;) {
    int n = 177 - at < 29 ? 177 - at : 29;
    float out[29] = {};
    synth->control(v, 32);
    synth->renderVoice(v, out, n);
    for (int i = 0; i < n; i++)
      TEST_ASSERT_FLOAT_WITHIN(.000001f, .5f * reference.next(), out[i]);
    at += n;
  }
  TEST_ASSERT_TRUE(v.env.idle());
  v.hold = .5f;
  collapseStealTail(v);
  float first[32] = {};
  synth->renderVoice(v, first, 32);
  model.instruments[0].attack = 100;
  synth->noteOn(0, 62, 127);
  TEST_ASSERT_EQUAL_FLOAT(0, v.env.level());
  TEST_ASSERT_TRUE(v.env.stage() == Env::Stage::Attack);
}
void test_budget_enable_invalidates_unknown_cache_only() {
  auto &v = note();
  v.hold = .5f;
  synth->voices_[1] = v;
  auto &sample = synth->voices_[1];
  sample.sample = true;
  sample.hold = 32767;
  synth->voices_[2] = v;
  auto &tail = synth->voices_[2];
  tail.hold = .25f;
  collapseStealTail(tail);
  synth->setVoiceBudget(kVoiceBudgetOff);
  synth->setVoiceBudget(kVoiceBudgetTeensy);
  TEST_ASSERT_EQUAL_FLOAT(0, v.hold);
  TEST_ASSERT_EQUAL_FLOAT(32767, sample.hold);
  TEST_ASSERT_EQUAL_FLOAT(.25f, tail.hold);
}
struct Bank final : SampleSource {
  int16_t data[8] = {1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000};
  bool find(const char *, SampleData &out) const override {
    out = SampleData();
    out.whole = data;
    out.frames = 8;
    out.rate = kSynthRate;
    return true;
  }
};
static Bank bank;
void test_sample_playback_keeps_underrun_hold_units() {
  auto &m = model.instruments[0];
  m.type = InstrType::Sample;
  m.loop = uint8_t(LoopMode::Forward);
  m.root = 60;
  strcpy(m.sample, "s");
  synth->setBank(&bank);
  auto &v = note();
  v.hold = 12345;
  synth->renderCore();
  TEST_ASSERT_EQUAL_FLOAT(12345, v.hold);
}
void test_budget_off_fixed_output_is_bit_exact() {
  int16_t off[4 * Synth::kBlock], on[4 * Synth::kBlock], right[Synth::kBlock];
  for (int enabled = 0; enabled < 2; enabled++) {
    delete synth;
    synth = new Synth(model);
    synth->setVoiceBudget(enabled ? kVoiceBudgetTeensy : kVoiceBudgetOff);
    note();
    for (int b = 0; b < 4; b++)
      synth->render((enabled ? on : off) + b * Synth::kBlock, right);
  }
  TEST_ASSERT_EQUAL_MEMORY(off, on, sizeof off);
}
void test_collapsed_byte_engine_stops_work() {
  delete synth;
  model = SynthModel();
  instrSetType(model.instruments[0], InstrType::Lofi);
  model.instruments[0].machine = uint8_t(LofiMachine::Byte);
  model.instruments[0].vol = model.instruments[0].sustain = 127;
  model.instruments[0].attack = model.instruments[0].decay = 0;
  model.tracks[0].out = TrackOut::Int;
  model.tracks[0].instr = 0;
  synth = new Synth(model);
  synth->setVoiceBudget(kVoiceBudgetTeensy);
  auto &v = note();
  synth->renderCore();
  const auto evals = v.lv().evals();
  collapseStealTail(v);
  synth->renderCore();
  TEST_ASSERT_EQUAL(evals, v.eng.lofi.lv.evals());
  synth->renderCore();
  TEST_ASSERT_FALSE(v.on);
}

void test_full_dsp_fading_byte_cost_tracks_pitch_modulation() {
  delete synth;
  model = SynthModel();
  int best = 0;
  for (int f = 0; f < byteFormulaCount(); ++f)
    if (byteWorkScore(f) > byteWorkScore(best))
      best = f;
  for (int i = 0; i < 2; ++i) {
    auto &m = model.instruments[i];
    instrSetType(m, InstrType::Lofi);
    m.machine = uint8_t(LofiMachine::Byte);
    m.lofiFormula = best;
    m.mono = false;
    m.vol = m.sustain = 127;
    m.attack = m.decay = 0;
    m.release = 80;
  }
  model.instruments[0].transpose = 24;
  for (auto &t : model.tracks) {
    t.out = TrackOut::Int;
    t.instr = 0;
  }
  synth = new Synth(model);
  synth->setVoiceBudget(kVoiceBudgetTeensy);
  for (int t = 0; t < 12; ++t)
    synth->noteOn(t, 24, 127);
  synth->renderCore();
  for (int t = 0; t < 6; ++t)
    synth->noteOff(t, 24);
  for (int t = 0; t < 16; ++t) {
    model.tracks[t].instr = 1;
    synth->noteOn(t, 0, 127);
  }
  for (int i = 0; i < kLfos; ++i) {
    auto cfg = lfoRef(model.instruments[0], i);
    cfg.depth = 63;
    cfg.wave = uint8_t(LfoWave::Square);
    cfg.dest = uint8_t(LfoDest::Pitch);
    cfg.rate = 100;
  }
  synth->cacheInstrument(0);
  for (auto &v : synth->voices_)
    if (v.on && !v.env.idle())
      synth->control(v, 32);
  int actual = 0;
  for (const auto &v : synth->voices_) {
    if (!v.on || v.env.idle() || isStealTail(v))
      continue;
    if (v.lofi) {
      const int cost = voiceCost(model.instruments[v.instr], false, 0,
                                 v.eng.lofi.lp.hz, InstrType::Lofi, v.machine);
      TEST_ASSERT_EQUAL(cost, v.cost);
      actual += cost;
    } else
      actual += v.cost;
  }
  TEST_ASSERT_TRUE(actual <= synth->budget_);
}
void test_sample_tail_uses_postfilter_cache_and_normal_release_keeps_sample() {
  auto& m = model.instruments[0];
  instrSetType(m, InstrType::Sample);
  strcpy(m.sample, "s"); m.root = 60;
  m.loop = uint8_t(LoopMode::Forward);
  synth->setBank(&bank);
  auto& v = note();
  synth->renderCore();
  TEST_ASSERT_TRUE(fabsf(v.sampleTailHold) > .00001f);
  const float last = v.sampleTailHold;
  v.env.fade(kStealMs);
  trimFullDspTails(synth->voices_, 0);
  TEST_ASSERT_TRUE(v.sample); // ordinary releases retain their full sample DSP
  v.stolen = true;
  trimFullDspTails(synth->voices_, 0);
  TEST_ASSERT_TRUE(isStealTail(v));
  TEST_ASSERT_EQUAL_FLOAT(last, v.hold);
  TEST_ASSERT_FALSE(v.fltOn);
  Env expected = v.env;
  float out[32] = {};
  synth->renderVoice(v, out, 32);
  for (int i = 0; i < 32; ++i)
    TEST_ASSERT_FLOAT_WITHIN(.000001f, last * expected.next(), out[i]);
  for (int i = 0; i < 8; ++i) synth->renderCore();
  TEST_ASSERT_FALSE(v.on);
}

void test_warm_sample_pool_pitch_jump_bounds_full_dsp_cost() {
  auto& m = model.instruments[0];
  instrSetType(m, InstrType::Sample);
  strcpy(m.sample, "s"); m.root = 60; m.mono = false;
  m.loop = uint8_t(LoopMode::Forward);
  synth->setBank(&bank);
  for (int t = 0; t < 8; ++t)
    for (int n = 0; n < 4; ++n) synth->noteOn(t, 60, 127);
  synth->renderCore();
  m.root = 0;
  synth->renderCore();
  int actual = 0, tails = 0;
  for (const auto& v : synth->voices_) {
    if (v.on && !v.env.idle() && !isStealTail(v)) actual += v.cost;
    tails += isStealTail(v);
  }
  TEST_ASSERT_TRUE(actual <= kVoiceBudgetTeensy);
  TEST_ASSERT_TRUE(tails > 0);
}

void test_dense_fir_sample_notes_bound_actual_full_dsp_cost() {
  delete synth;
  model = SynthModel();
  auto& m = model.instruments[0];
  instrSetType(m, InstrType::Sample);
  strcpy(m.sample, "s");
  m.root = 60;
  m.loop = uint8_t(LoopMode::Forward);
  m.mono = false;
  m.vol = m.sustain = 127;
  m.attack = m.decay = m.release = 0;
  for (auto& track : model.tracks) {
    track.out = TrackOut::Int;
    track.instr = 0;
  }
  synth = new Synth(model);
  synth->setBank(&bank);
  const int budget = 2 * voiceCost(m, true, 0, 0, InstrType::Count, 255, 4.f);
  synth->setVoiceBudget(budget);
  for (int t = 0; t < 8; ++t) {
    synth->noteOn(t, 84, 127);
    synth->renderCore();
    int actual = 0;
    for (const auto& voice : synth->voices_)
      if (voice.on && !voice.env.idle() && !isStealTail(voice)) actual += voice.cost;
    TEST_ASSERT_TRUE(actual <= budget);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_dense_fir_sample_notes_bound_actual_full_dsp_cost);
  RUN_TEST(test_warm_sample_pool_pitch_jump_bounds_full_dsp_cost);
  RUN_TEST(test_sample_tail_uses_postfilter_cache_and_normal_release_keeps_sample);
  RUN_TEST(test_cache_keeps_postfilter_individual_last_sample);
  RUN_TEST(test_tail_renders_cached_sample_without_refiltering);
  RUN_TEST(test_tail_control_does_not_reactivate_instrument);
  RUN_TEST(test_reused_tail_is_fresh_note_and_clears_stale_cache);
  RUN_TEST(test_fresh_non_sample_reuse_does_not_keep_pcm_hold);
  RUN_TEST(test_dense_byte_notes_bound_actual_full_dsp_cost);
  RUN_TEST(test_tail_linear_duration_and_reuse_attack);
  RUN_TEST(test_budget_enable_invalidates_unknown_cache_only);
  RUN_TEST(test_sample_playback_keeps_underrun_hold_units);
  RUN_TEST(test_budget_off_fixed_output_is_bit_exact);
  RUN_TEST(test_collapsed_byte_engine_stops_work);
  RUN_TEST(test_full_dsp_fading_byte_cost_tracks_pitch_modulation);
  return UNITY_END();
}
