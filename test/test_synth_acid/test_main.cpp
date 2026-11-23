#include <unity.h>
#include "synth.h"
#include "synth_syn_calibration.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_accent_strengthens_the_note_and_shortens_filter_decay() {
  AcidAccent plain, accent;
  plain.note(109, false);
  accent.note(110, false);
  plain.advance(882, 0, 48);
  accent.advance(882, 0, 48);
  TEST_ASSERT_EQUAL_FLOAT(1.f, plain.gain());
  TEST_ASSERT_EQUAL_FLOAT(0.f, plain.cutoffOctaves());
  TEST_ASSERT_TRUE(accent.gain() > 1.1f);
  TEST_ASSERT_TRUE(accent.cutoffOctaves() > .5f);
  TEST_ASSERT_FLOAT_WITHIN(.001f, plain.envelope() * plain.envelope(), accent.envelope());
}

void test_slide_keeps_filter_state_and_updates_accent() {
  AcidAccent voice;
  voice.note(120, false);
  voice.advance(1323, 0, 48);
  const float held = voice.envelope();
  const float heldCut = voice.cutoffOctaves();
  voice.note(90, true);
  voice.advance(0, 0, 48);
  TEST_ASSERT_EQUAL_FLOAT(held, voice.envelope());
  TEST_ASSERT_EQUAL_FLOAT(heldCut, voice.cutoffOctaves());
  voice.advance(441, 0, 48);
  TEST_ASSERT_FLOAT_WITHIN(.001f, held * synModEnv(441, 0, 48), voice.envelope());
  TEST_ASSERT_TRUE(voice.cutoffOctaves() < heldCut);
  voice.note(120, false);
  voice.advance(0, 0, 48);
  TEST_ASSERT_EQUAL_FLOAT(1.f, voice.envelope());
}

void test_filter_attack_and_hold_follow_synth_settings() {
  AcidAccent voice;
  voice.note(110, false);
  voice.advance(0, 16, 0);
  TEST_ASSERT_EQUAL_FLOAT(0.f, voice.envelope());
  const auto samples = synModAttackMs(16) * kSynthRate / 2000;
  voice.advance(samples, 16, 0);
  TEST_ASSERT_FLOAT_WITHIN(.001f, .5f, voice.envelope());
  voice.advance(kSynthRate, 16, 0);
  TEST_ASSERT_EQUAL_FLOAT(1.f, voice.envelope());
}

void test_reselecting_profile_starts_with_a_neutral_accent() {
  AcidAccent voice;
  TEST_ASSERT_FALSE(voice.armed());
  voice.note(127, false);
  voice.advance(441, 0, 48);
  TEST_ASSERT_TRUE(voice.armed());
  voice.reset();
  TEST_ASSERT_FALSE(voice.armed());
  TEST_ASSERT_EQUAL_FLOAT(0.f, voice.envelope());
  TEST_ASSERT_EQUAL_FLOAT(0.f, voice.cutoffOctaves());
  TEST_ASSERT_EQUAL_FLOAT(1.f, voice.gain());
  voice.advance(128, 0, 48);
  TEST_ASSERT_EQUAL_FLOAT(0.f, voice.envelope());
  voice.note(80, true);
  voice.advance(0, 0, 48);
  TEST_ASSERT_EQUAL_FLOAT(1.f, voice.envelope());
  TEST_ASSERT_EQUAL_FLOAT(0.f, voice.cutoffOctaves());
}

static SynthModel acidModel;

static void setupInstrument(bool enabled, FltMode filter) {
  acidModel = SynthModel();
  acidModel.masterVol = 100;
  acidModel.tracks[0].out = TrackOut::Int;
  acidModel.tracks[0].instr = 0;
  acidModel.tracks[0].vol = 100;
  auto& inst = acidModel.instruments[0];
  instrSetType(inst, InstrType::Synth);
  inst.synAcid = enabled;
  inst.vol = 36;
  inst.attack = inst.decay = 0;
  inst.sustain = 127;
  inst.mono = !enabled;  // Acid itself chooses mono.
  inst.fltMode = static_cast<uint8_t>(filter);
  inst.cutoff = 45;
  inst.reso = 45;
  inst.fenv = 0;
  inst.fAtk = 0;
  inst.fDec = 48;
}

static void event(Synth& synth, uint8_t cmd, uint8_t a, uint8_t b) {
  const uint8_t message[] = {cmd, a, b};
  TEST_ASSERT_TRUE(synth.event(0, 0, message, 3));
}

static double render(Synth& synth, int blocks) {
  double energy = 0;
  int16_t left[Synth::kBlock], right[Synth::kBlock];
  for (int i = 0; i < blocks; ++i) {
    synth.render(left, right);
    for (auto x : left) energy += double(x) * x;
  }
  return energy;
}

struct AccentRender {
  double energy;
  float octaves;
};

static AccentRender playAccent(bool profile, FltMode filter) {
  setupInstrument(profile, filter);
  Synth synth(acidModel);
  event(synth, 0x90, 60, 120);
  const double energy = render(synth, 12);
  const int voice = synth.trackVoice(0);
  return {energy, voice >= 0 ? synth.voice(voice).octKey : -1.f};
}

void test_profile_reaches_both_audio_gain_and_filter() {
  // The two runs use the same velocity. Bypass the filter to isolate the gain change.
  const auto dry = playAccent(false, FltMode::Off);
  const auto accented = playAccent(true, FltMode::Off);
  TEST_ASSERT_TRUE(dry.energy > 0);
  TEST_ASSERT_TRUE(accented.energy > dry.energy * 1.2);
  const auto filtered = playAccent(false, FltMode::Ladder);
  const auto opened = playAccent(true, FltMode::Ladder);
  TEST_ASSERT_TRUE(filtered.octaves > 0);
  TEST_ASSERT_TRUE(opened.octaves > filtered.octaves + .5f);
}

void test_profile_toggle_on_a_held_note_keeps_the_oscillator_running() {
  setupInstrument(false, FltMode::Ladder);
  Synth synth(acidModel);
  event(synth, 0x90, 60, 120);
  render(synth, 1);
  const int vi = synth.trackVoice(0);
  TEST_ASSERT_TRUE(vi >= 0);
  const auto& voice = synth.voice(vi);
  const float oldCut = voice.octKey;
  const auto oldOscAge = voice.senvT;
  TEST_ASSERT_FALSE(voice.sv().acid().armed());

  acidModel.instruments[0].synAcid = true;
  render(synth, 12);
  TEST_ASSERT_TRUE(voice.sv().acid().armed());
  TEST_ASSERT_TRUE(voice.octKey > oldCut + .5f);

  acidModel.instruments[0].synAcid = false;
  render(synth, 1);
  TEST_ASSERT_EQUAL(vi, synth.trackVoice(0));
  TEST_ASSERT_TRUE(voice.senvT > oldOscAge);
  TEST_ASSERT_FALSE(voice.sv().acid().armed());
  TEST_ASSERT_EQUAL_FLOAT(oldCut, voice.octKey);
}

struct AcidSlide {
  float firstPitch;
  float lastPitch;
};

static AcidSlide slide(bool enabled) {
  setupInstrument(enabled, FltMode::Ladder);
  Synth synth(acidModel);
  event(synth, 0x90, 60, 120);
  render(synth, 10);
  const int vi = synth.trackVoice(0);
  if (vi < 0) { TEST_FAIL_MESSAGE("First note did not allocate a voice"); return {}; }
  const auto& voice = synth.voice(vi);
  const auto previousAge = voice.senvT;
  const float previousEnv = voice.eng.syn.acid().envelope();
  event(synth, 0xF5, kSynthStep, 0x80 | 24);
  event(synth, 0xF5, static_cast<uint8_t>(Fx::SLD), 25);  // 100 ms
  event(synth, 0x90, 72, 80);
  render(synth, 1);
  if (synth.trackVoice(0) != vi || synth.activeVoices() != 1 || voice.senvT <= previousAge) {
    TEST_FAIL_MESSAGE("Slide did not keep the original oscillator voice");
    return {};
  }
  if (enabled && !(voice.eng.syn.acid().envelope() < previousEnv &&
                   voice.eng.syn.acid().envelope() > previousEnv * .9f)) {
    TEST_FAIL_MESSAGE("Slide restarted the Acid filter envelope");
    return {};
  }
  const float pitch = voice.pitch;
  render(synth, 40);
  const float last = voice.pitch;
  event(synth, 0x90, 67, 80);
  render(synth, 1);
  if (synth.trackVoice(0) != vi || synth.activeVoices() != 1) {
    TEST_FAIL_MESSAGE("The instrument did not stay monophonic after the slide");
    return {};
  }
  return {pitch, last};
}

void test_profile_keeps_sld_timing_and_a_single_continuous_voice() {
  const auto plain = slide(false);
  const auto acid = slide(true);
  TEST_ASSERT_TRUE(acid.firstPitch > 60.f && acid.firstPitch < 62.f);
  TEST_ASSERT_EQUAL_FLOAT(plain.firstPitch, acid.firstPitch);
  TEST_ASSERT_EQUAL_FLOAT(72.f, acid.lastPitch);
  TEST_ASSERT_EQUAL_FLOAT(plain.lastPitch, acid.lastPitch);
}

void test_selecting_acid_fades_older_poly_notes_and_continues_the_latest() {
  setupInstrument(false, FltMode::Off);
  auto& inst = acidModel.instruments[0];
  inst.mono = false;
  Synth synth(acidModel);
  event(synth, 0x90, 60, 120);
  event(synth, 0x90, 64, 120);
  event(synth, 0x90, 67, 120);
  render(synth, 1);
  TEST_ASSERT_EQUAL(3, synth.activeVoices());
  const int newest = synth.trackVoice(0);
  TEST_ASSERT_TRUE(newest >= 0);
  TEST_ASSERT_EQUAL(67, synth.voice(newest).note);
  const uint32_t phaseAge = synth.voice(newest).senvT;

  inst.synAcid = true;
  event(synth, 0x90, 72, 120);
  TEST_ASSERT_TRUE(render(synth, 2) > 0);  // ~5.8 ms, past the 4 ms fade
  TEST_ASSERT_EQUAL(1, synth.activeVoices());
  TEST_ASSERT_EQUAL(newest, synth.trackVoice(0));
  TEST_ASSERT_EQUAL(72, synth.voice(newest).note);
  TEST_ASSERT_TRUE(synth.voice(newest).senvT > phaseAge);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_accent_strengthens_the_note_and_shortens_filter_decay);
  RUN_TEST(test_slide_keeps_filter_state_and_updates_accent);
  RUN_TEST(test_filter_attack_and_hold_follow_synth_settings);
  RUN_TEST(test_reselecting_profile_starts_with_a_neutral_accent);
  RUN_TEST(test_profile_reaches_both_audio_gain_and_filter);
  RUN_TEST(test_profile_toggle_on_a_held_note_keeps_the_oscillator_running);
  RUN_TEST(test_profile_keeps_sld_timing_and_a_single_continuous_voice);
  RUN_TEST(test_selecting_acid_fades_older_poly_notes_and_continues_the_latest);
  return UNITY_END();
}
