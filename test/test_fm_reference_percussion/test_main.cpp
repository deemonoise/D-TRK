#include <cmath>
#include <initializer_list>
#include <vector>
#include <algorithm>
#include <complex>
#include <unity.h>
#include "synth_fm.h"

namespace mt {
bool fmPercussionMachine(uint8_t machine, const float raw[8], float note, FmParams& out);
}
using namespace mt;

namespace {
// Wire IDs of the replacement family; these deliberately do not depend on UI ordering.
constexpr uint8_t kMachines[] = {0, 1, 2, 3, 4, 5, 9};
constexpr float kDefaults[][8] = {
    {64, 0, 0, 80, 48, 32, 64, 0}, {64, 0, 0, 80, 32, 64, 64, 0},
    {64, 0, 0, 80, 32, 64, 64, 0}, {64, 0, 0, 80, 32, 64, 64, 0},
    {64, 32, 0, 80, 64, 48, 64, 0}, {64, 64, 0, 80, 64, 64, 64, 0},
    {64, 64, 32, 80, 0, 0, 127, 0}};
void rawFor(int machine, float (&raw)[8]) {
  const int row = machine == 9 ? 6 : machine;
  for (int k = 0; k < 8; ++k) raw[k] = kDefaults[row][k];
}
FmParams mapped(int machine, const float* raw, float note = 60) {
  FmParams p;
  TEST_ASSERT_TRUE(fmPercussionMachine(static_cast<uint8_t>(machine), raw, note, p));
  return p;
}
void handles_exactly_seven_percussion_machines() {
  for (uint8_t machine : kMachines) {
    float raw[8]; rawFor(machine, raw);
    const FmParams p = mapped(machine, raw);
    TEST_ASSERT_TRUE(std::isfinite(p.hz));
    TEST_ASSERT_GREATER_THAN_FLOAT(0, p.hz);
    TEST_ASSERT_TRUE(p.oneShot);
  }
  float raw[8]; rawFor(0, raw);
  for (uint8_t machine : {uint8_t(6), uint8_t(7), uint8_t(8), uint8_t(10), uint8_t(255)}) {
    FmParams p; p.hz = 123.f;
    TEST_ASSERT_FALSE(fmPercussionMachine(machine, raw, 60, p));
    TEST_ASSERT_EQUAL_FLOAT(123.f, p.hz);
  }
}
void tune_tracks_notes_and_has_measured_edge_plateaus() {
  for (uint8_t machine : {uint8_t(0), uint8_t(3)}) {
    float raw[8]; rawFor(machine, raw);
    const float base = mapped(machine, raw).hz;
    TEST_ASSERT_FLOAT_WITHIN(.1f, machine == 0 ? 65.46f : 261.55f, base);
    TEST_ASSERT_FLOAT_WITHIN(.1f, 2 * base, mapped(machine, raw, 72).hz);
    raw[0] = 0; const float low = mapped(machine, raw).hz;
    raw[0] = 32; TEST_ASSERT_EQUAL_FLOAT(low, mapped(machine, raw).hz);
    raw[0] = 96; const float high = mapped(machine, raw).hz;
    raw[0] = 127; TEST_ASSERT_EQUAL_FLOAT(high, mapped(machine, raw).hz);
    TEST_ASSERT_FLOAT_WITHIN(.1f, 16 * low, high);
  }
}
void useful_decay_controls_change_time_without_changing_pitch() {
  for (uint8_t machine : {uint8_t(0), uint8_t(1), uint8_t(3), uint8_t(9)}) {
    float raw[8]; rawFor(machine, raw); raw[3] = 0;
    const FmParams shortP = mapped(machine, raw);
    raw[3] = 127; const FmParams longP = mapped(machine, raw);
    TEST_ASSERT_GREATER_THAN_FLOAT(shortP.ampMs * 10, longP.ampMs);
    TEST_ASSERT_EQUAL_FLOAT(shortP.hz, longP.hz);
  }
}
void vintage_inharmonicity_extreme_shortens_the_tail() {
  float raw[8]; rawFor(2, raw); raw[4] = 0;
  const FmParams normal = mapped(2, raw);
  raw[4] = 127; const FmParams extreme = mapped(2, raw);
  TEST_ASSERT_LESS_THAN_FLOAT(normal.ampMs * .25f, extreme.ampMs);
}
void over_is_independent_of_pitch_and_decay() {
  for (uint8_t machine : kMachines) {
    float raw[8]; rawFor(machine, raw);
    const FmParams clean = mapped(machine, raw);
    raw[7] = 127; const FmParams driven = mapped(machine, raw);
    TEST_ASSERT_EQUAL_FLOAT(clean.hz, driven.hz);
    TEST_ASSERT_EQUAL_FLOAT(clean.ampMs, driven.ampMs);
    TEST_ASSERT_EQUAL_FLOAT(0, clean.sat);
    TEST_ASSERT_EQUAL_FLOAT(1, driven.sat);
  }
}
void bass_drum_timbre_modulation_is_not_a_linear_brightness_knob() {
  float raw[8]; rawFor(0, raw); raw[5] = 32;
  const FmParams quiet = mapped(0, raw);
  raw[5] = 96; const FmParams bright = mapped(0, raw);
  raw[5] = 127; const FmParams edge = mapped(0, raw);
  TEST_ASSERT_GREATER_THAN_FLOAT(quiet.op[4].level * 10 + .01f, bright.op[4].level);
  TEST_ASSERT_GREATER_THAN_FLOAT(edge.op[4].level * 4, bright.op[4].level);
  TEST_ASSERT_EQUAL_INT(4, bright.op[0].mod);
  TEST_ASSERT_FALSE(bright.op[4].output);
}
void carbon_modulation_requires_inharmonicity() {
  float raw[8]; rawFor(3, raw); raw[4] = 0; raw[5] = 0;
  const FmParams a = mapped(3, raw);
  raw[5] = 127; const FmParams b = mapped(3, raw);
  TEST_ASSERT_EQUAL_FLOAT(a.op[4].level, b.op[4].level);
  raw[4] = 127; const FmParams c = mapped(3, raw);
  TEST_ASSERT_GREATER_THAN_FLOAT(b.op[4].level + 1, c.op[4].level);
}
void clap_body_has_an_independent_branch_and_envelope() {
  float raw[8]; rawFor(4, raw); raw[4] = 0; raw[5] = 0;
  const FmParams emptyBody = mapped(4, raw);
  raw[5] = 127; const FmParams stillEmpty = mapped(4, raw);
  TEST_ASSERT_EQUAL_FLOAT(0, emptyBody.noise2);
  TEST_ASSERT_EQUAL_FLOAT(0, stillEmpty.noise2);
  TEST_ASSERT_EQUAL_FLOAT(emptyBody.filterHz, stillEmpty.filterHz);
  raw[4] = 127; const FmParams high = mapped(4, raw);
  raw[5] = 0; const FmParams low = mapped(4, raw);
  TEST_ASSERT_GREATER_THAN_FLOAT(low.noise2FilterHz * 10, high.noise2FilterHz);
  TEST_ASSERT_GREATER_THAN_FLOAT(0, high.noise2);
  raw[6] = 127; const FmParams longBody = mapped(4, raw);
  TEST_ASSERT_GREATER_THAN_FLOAT(low.noise2Ms, longBody.noise2Ms);
}
void basic_feedback_and_modulation_envelope_are_separate() {
  float raw[8]; rawFor(1, raw); raw[5] = 0;
  const FmParams clean = mapped(1, raw);
  raw[5] = 127; const FmParams feedback = mapped(1, raw);
  TEST_ASSERT_GREATER_THAN_FLOAT(clean.op[4].fb + .5f, feedback.op[4].fb);
  raw[6] = 127; const FmParams held = mapped(1, raw);
  TEST_ASSERT_GREATER_THAN_FLOAT(feedback.op[4].sustain, held.op[4].sustain);
}
void hat_empty_slots_do_not_change_sound_and_reset_is_boolean() {
  float raw[8]; rawFor(9, raw); raw[6] = 0;
  const FmParams free = mapped(9, raw);
  raw[4] = 127; raw[5] = 127;
  const FmParams unused = mapped(9, raw);
  TEST_ASSERT_EQUAL_FLOAT(free.hz, unused.hz);
  TEST_ASSERT_EQUAL_FLOAT(free.filterHz, unused.filterHz);
  TEST_ASSERT_EQUAL_FLOAT(free.noise, unused.noise);
  TEST_ASSERT_EQUAL_FLOAT(free.noise2, unused.noise2);
  TEST_ASSERT_FALSE(free.resetPhase);
  raw[6] = 1; TEST_ASSERT_TRUE(mapped(9, raw).resetPhase);
  raw[6] = 64; TEST_ASSERT_TRUE(mapped(9, raw).resetPhase);
  raw[6] = 127; TEST_ASSERT_TRUE(mapped(9, raw).resetPhase);
}
void hat_transient_and_tone_controls_are_independent() {
  float raw[8]; rawFor(9, raw); raw[2] = 0;
  const FmParams calm = mapped(9, raw);
  raw[2] = 127; const FmParams transient = mapped(9, raw);
  TEST_ASSERT_EQUAL_FLOAT(calm.filterHz, transient.filterHz);
  TEST_ASSERT_EQUAL_FLOAT(calm.ampMs, transient.ampMs);
  TEST_ASSERT_GREATER_THAN_FLOAT(calm.noise2, transient.noise2);
  raw[1] = 127; const FmParams bright = mapped(9, raw);
  TEST_ASSERT_GREATER_THAN_FLOAT(transient.filterHz, bright.filterHz);
}
void mapped_extremes_are_finite_and_modulation_graph_is_acyclic() {
  for (uint8_t machine : kMachines) {
    for (float value : {-100.f, 0.f, 32.f, 64.f, 96.f, 127.f, 300.f}) {
      float raw[8]; for (float& v : raw) v = value;
      const FmParams p = mapped(machine, raw, 127);
      TEST_ASSERT_TRUE(std::isfinite(p.hz));
      TEST_ASSERT_TRUE(std::isfinite(p.ampMs));
      TEST_ASSERT_TRUE(std::isfinite(p.noise));
      TEST_ASSERT_TRUE(std::isfinite(p.noise2));
      TEST_ASSERT_TRUE(std::isfinite(p.filterHz));
      TEST_ASSERT_TRUE(std::isfinite(p.noise2FilterHz));
      TEST_ASSERT_TRUE(p.sat >= 0 && p.sat <= 1);
      for (int k = 0; k < kFmOps; ++k) {
        TEST_ASSERT_TRUE(std::isfinite(p.op[k].ratio));
        TEST_ASSERT_TRUE(std::isfinite(p.op[k].level));
        TEST_ASSERT_TRUE(std::isfinite(p.op[k].fb));
        TEST_ASSERT_TRUE(p.op[k].mod < 0 || p.op[k].mod > k);
        TEST_ASSERT_LESS_THAN_INT(kFmOps, p.op[k].mod);
      }
    }
  }
}

std::vector<float> render(const FmParams& p, float seconds) {
  std::vector<float> result(static_cast<size_t>(seconds * kSynthRate));
  FmVoice voice;
  voice.trigger(false);
  for (size_t off = 0; off < result.size(); off += 64) {
    const int n = static_cast<int>(std::min(size_t(64), result.size() - off));
    voice.control(p, n);
    voice.render(result.data() + off, n, 1);
  }
  return result;
}
float rms(const std::vector<float>& x, float from, float to) {
  const int a = static_cast<int>(from * kSynthRate), b = static_cast<int>(to * kSynthRate);
  double energy = 0;
  for (int k = a; k < b; ++k) energy += x[k] * x[k];
  return static_cast<float>(std::sqrt(energy / (b - a)));
}
float brightness(const std::vector<float>& x) {
  double energy = 0, delta = 0;
  for (size_t k = 1; k < x.size(); ++k) {
    energy += x[k] * x[k];
    const double d = x[k] - x[k - 1]; delta += d * d;
  }
  return static_cast<float>(delta / std::max(energy, 1e-20));
}
void bass_drum_renders_the_measured_baseline_pitch() {
  float raw[8]; rawFor(0, raw);
  const auto audio = render(mapped(0, raw), .7f);
  int crossings = 0;
  for (int k = kSynthRate / 10 + 1; k < kSynthRate * 6 / 10; ++k)
    if (audio[k - 1] <= 0 && audio[k] > 0) ++crossings;
  TEST_ASSERT_FLOAT_WITHIN(2.1f, 65.46f, crossings * 2.f);
  TEST_ASSERT_GREATER_THAN_FLOAT(.01f, rms(audio, .1f, .3f));
}
void carbon_interaction_is_audible_in_rendered_spectral_brightness() {
  float raw[8]; rawFor(3, raw); raw[4] = 0; raw[5] = 0;
  const auto unmodulated = render(mapped(3, raw), .3f);
  raw[5] = 127; const auto inharmZero = render(mapped(3, raw), .3f);
  TEST_ASSERT_EQUAL_MEMORY(unmodulated.data(), inharmZero.data(), unmodulated.size() * sizeof(float));
  raw[4] = 127; const auto bright = render(mapped(3, raw), .3f);
  TEST_ASSERT_GREATER_THAN_FLOAT(brightness(unmodulated) * 2, brightness(bright));
}
void clap_body_extremes_change_rendered_bandwidth_only_when_balanced_in() {
  float raw[8]; rawFor(4, raw); raw[4] = 0; raw[5] = 0;
  const auto noBody = render(mapped(4, raw), .3f);
  raw[5] = 127; const auto stillNoBody = render(mapped(4, raw), .3f);
  TEST_ASSERT_EQUAL_MEMORY(noBody.data(), stillNoBody.data(), noBody.size() * sizeof(float));
  raw[4] = 127; const auto bright = render(mapped(4, raw), .3f);
  raw[5] = 0; const auto dark = render(mapped(4, raw), .3f);
  TEST_ASSERT_GREATER_THAN_FLOAT(brightness(dark) * 2, brightness(bright));
}
void long_decay_has_an_audible_tail_and_all_models_render_finite() {
  float raw[8]; rawFor(0, raw); raw[3] = 32;
  const auto shortHit = render(mapped(0, raw), 1.2f);
  raw[3] = 127; const auto longHit = render(mapped(0, raw), 1.2f);
  TEST_ASSERT_GREATER_THAN_FLOAT(rms(shortHit, 1, 1.2f) * 100, rms(longHit, 1, 1.2f));
  for (uint8_t machine : kMachines) {
    rawFor(machine, raw);
    const auto audio = render(mapped(machine, raw), .15f);
    double energy = 0;
    for (float sample : audio) { TEST_ASSERT_TRUE(std::isfinite(sample)); energy += sample * sample; }
    TEST_ASSERT_GREATER_THAN_FLOAT(1e-8f, static_cast<float>(energy));
  }
}

float centroid(const std::vector<float>& audio, float start = .01f) {
  constexpr int n = 8192;
  std::vector<std::complex<double>> x(n);
  for (int k = 0; k < n; ++k)
    x[k] = audio[k + static_cast<int>(start * kSynthRate)] * (.5 - .5 * std::cos(6.28318530718 * k / (n - 1)));
  for (int i = 1, j = 0; i < n; ++i) {
    int b = n >> 1; for (; j & b; b >>= 1) j ^= b; j ^= b;
    if (i < j) std::swap(x[i], x[j]);
  }
  for (int length = 2; length <= n; length <<= 1) {
    const auto increment = std::polar(1.0, -6.28318530718 / length);
    for (int k = 0; k < n; k += length) {
      std::complex<double> w(1, 0);
      for (int j = 0; j < length / 2; ++j) {
        const auto a = x[k + j], b = w * x[k + j + length / 2];
        x[k + j] = a + b; x[k + j + length / 2] = a - b; w *= increment;
      }
    }
  }
  double energy = 0, weighted = 0;
  for (int k = 0; k <= n / 2; ++k) {
    const double e = std::norm(x[k]); energy += e; weighted += e * (k * double(kSynthRate) / n);
  }
  return static_cast<float>(weighted / std::max(energy, 1e-20));
}
void snare_and_cymbal_baselines_have_the_measured_broad_spectrum() {
  float raw[8]; rawFor(1, raw);
  TEST_ASSERT_GREATER_THAN_FLOAT(1500.f, centroid(render(mapped(1, raw), .31f)));
  rawFor(2, raw);
  TEST_ASSERT_GREATER_THAN_FLOAT(1600.f, centroid(render(mapped(2, raw), .31f)));
  rawFor(5, raw);
  TEST_ASSERT_GREATER_THAN_FLOAT(5800.f, centroid(render(mapped(5, raw), .31f)));
}
void carbon_baseline_retains_the_measured_upper_component() {
  float raw[8]; rawFor(3, raw);
  TEST_ASSERT_GREATER_THAN_FLOAT(270.f, centroid(render(mapped(3, raw), .31f)));
}

float sustainedT20(const std::vector<float>& audio) {
  const int block = kSynthRate / 200;
  std::vector<float> envelope;
  for (size_t k = 0; k + block <= audio.size(); k += block) {
    double e = 0; for (int j = 0; j < block; ++j) e += audio[k + j] * audio[k + j];
    envelope.push_back(static_cast<float>(std::sqrt(e / block)));
  }
  const size_t peak = std::max_element(envelope.begin(), envelope.end()) - envelope.begin();
  for (size_t k = peak + 1; k + 10 <= envelope.size(); ++k) {
    bool below = true;
    for (int j = 0; j < 10; ++j) below = below && envelope[k + j] < envelope[peak] * .1f;
    if (below) return (k - peak) * (block / float(kSynthRate));
  }
  return -1;
}
void clap_baseline_tail_is_not_cut_to_half_the_reference_duration() {
  float raw[8]; rawFor(4, raw);
  const float seconds = sustainedT20(render(mapped(4, raw), 1.2f));
  TEST_ASSERT_FLOAT_WITHIN(.1f, .41f, seconds);
}

void basic_menv_changes_late_broadband_energy_separately_from_decay() {
  float raw[8]; rawFor(1, raw); raw[5] = 127; raw[6] = 0;
  const FmParams low = mapped(1, raw);
  const auto darker = render(low, .6f);
  raw[6] = 127; const FmParams high = mapped(1, raw);
  TEST_ASSERT_EQUAL_FLOAT(low.ampMs, high.ampMs);
  const auto brighter = render(high, .6f);
  TEST_ASSERT_GREATER_THAN_FLOAT(centroid(darker, .25f) * 1.5f, centroid(brighter, .25f));
}

void hat_baseline_preserves_the_measured_sharp_transient() {
  float raw[8]; rawFor(9, raw);
  const auto audio = render(mapped(9, raw), .4f);
  float peak = 0; for (float sample : audio) peak = std::max(peak, std::fabs(sample));
  // Captured baseline crest is about21dB; allow3dB for a stochastic approximation.
  TEST_ASSERT_GREATER_THAN_FLOAT(7.94f, peak / rms(audio, .01f, .31f));
  TEST_ASSERT_LESS_THAN_FLOAT(16.6f, peak / rms(audio, .01f, .31f));
  TEST_ASSERT_FLOAT_WITHIN(.2f, .675f, sustainedT20(render(mapped(9, raw), 1.2f)));
}

void baseline_machine_levels_preserve_reference_balance_with_one_shared_gain() {
  // Mean captured baseline10..310ms levels, relative to BD. One global gain cancels here.
  constexpr float expectedDb[] = {0, -16.3f, -11.1f, -4.1f, -1.7f, -5.9f, -22.5f};
  float raw[8]; rawFor(0, raw);
  const float bass = rms(render(mapped(0, raw), .4f), .01f, .31f);
  for (int row = 0; row < 7; ++row) {
    rawFor(kMachines[row], raw);
    const float level = rms(render(mapped(kMachines[row], raw), .4f), .01f, .31f);
    const float relativeDb = 20 * std::log10(level / bass);
    TEST_ASSERT_FLOAT_WITHIN(1.f, expectedDb[row], relativeDb);
  }
}

void clap_spacing_matches_measured_signed_rate_knots() {
  constexpr float rawValues[] = {0, 32, 64, 96, 127};
  constexpr float rates[] = {42, 21, 0, 22, 42};
  float raw[8]; rawFor(4, raw);
  for (int k = 0; k < 5; ++k) {
    raw[1] = rawValues[k]; const FmParams p = mapped(4, raw);
    if (rates[k] == 0) { TEST_ASSERT_EQUAL_INT(0, p.bursts); }
    else { TEST_ASSERT_FLOAT_WITHIN(.05f, 1000 / rates[k], p.burstMs); }
    TEST_ASSERT_LESS_OR_EQUAL_INT(8, p.bursts);
  }
}
void compact_clap_matches_reference_energy_peak_and_duration_together() {
  float raw[8]; rawFor(4, raw);
  auto baseline = render(mapped(4, raw), 1.2f);
  raw[1] = 64; auto compact = render(mapped(4, raw), 1.2f);
  const size_t latency = static_cast<size_t>(.0035f * kSynthRate);
  baseline.insert(baseline.begin(), latency, 0);
  compact.insert(compact.begin(), latency, 0);
  const float deltaRms = 20 * std::log10(rms(compact, .01f, .31f) / rms(baseline, .01f, .31f));
  float peakBase = 0, peakCompact = 0;
  for (float y : baseline) peakBase = std::max(peakBase, std::fabs(y));
  for (float y : compact) peakCompact = std::max(peakCompact, std::fabs(y));
  TEST_ASSERT_FLOAT_WITHIN(2.5f, -13.7f, deltaRms);
  TEST_ASSERT_FLOAT_WITHIN(3.f, -4.7f, 20 * std::log10(peakCompact / peakBase));
  TEST_ASSERT_FLOAT_WITHIN(.03f, .08f, sustainedT20(compact));
}

struct HatSpectrum { float low, line6, peakHz; };
HatSpectrum hatSpectrum(const std::vector<float>& audio, float start = .04f) {
  constexpr int n = 8192;
  std::vector<std::complex<double>> x(n);
  for (int k = 0; k < n; ++k)
    x[k] = audio[k + static_cast<int>(start * kSynthRate)] * (.5 - .5 * std::cos(6.28318530718 * k / (n - 1)));
  for (int i = 1, j = 0; i < n; ++i) {
    int b = n >> 1; for (; j & b; b >>= 1) j ^= b; j ^= b;
    if (i < j) std::swap(x[i], x[j]);
  }
  for (int length = 2; length <= n; length <<= 1) {
    const auto increment = std::polar(1.0, -6.28318530718 / length);
    for (int k = 0; k < n; k += length) {
      std::complex<double> w(1, 0);
      for (int j = 0; j < length / 2; ++j) {
        const auto a = x[k + j], b = w * x[k + j + length / 2];
        x[k + j] = a + b; x[k + j + length / 2] = a - b; w *= increment;
      }
    }
  }
  double energy = 0, weighted = 0;
  for (int k = 0; k <= n / 2; ++k) {
    const double e = std::norm(x[k]); energy += e; weighted += e * (k * double(kSynthRate) / n);
  }
  double low = 0; std::vector<int> peaks;
  for (int k = 1; k < n / 2; ++k) {
    if (k * float(kSynthRate) / n < 6000) low += std::norm(x[k]);
    if (std::norm(x[k]) > std::norm(x[k-1]) && std::norm(x[k]) > std::norm(x[k+1])) peaks.push_back(k);
  }
  std::sort(peaks.begin(), peaks.end(), [&](int a,int b){return std::norm(x[a])>std::norm(x[b]);});
  std::vector<int> selected;
  for (int k : peaks) {
    bool separated = k * float(kSynthRate) / n > 100;
    for (int j : selected) separated = separated && std::abs(k-j) * float(kSynthRate) / n >= 80;
    if (separated) selected.push_back(k);
    if (selected.size() == 6) break;
  }
  double lines = 0;
  for (int k = 1; k < n / 2; ++k) {
    bool near = false; for (int j : selected) near = near || std::abs(k-j) * float(kSynthRate) / n <= 20;
    if (near) lines += std::norm(x[k]);
  }
  return {float(low/energy), float(lines/energy), selected[0] * float(kSynthRate) / n};
}
void hat_tone_restores_the_measured_mid_band_instead_of_only_opening_highpass() {
  float raw[8]; rawFor(9, raw); raw[1] = 0;
  const auto dark = hatSpectrum(render(mapped(9, raw), .6f));
  raw[1] = 32; const auto medium = hatSpectrum(render(mapped(9, raw), .6f));
  TEST_ASSERT_GREATER_THAN_FLOAT(.15f, dark.low);
  TEST_ASSERT_GREATER_THAN_FLOAT(.20f, medium.low);
}
void hat_baseline_contains_broadband_energy_without_upper_octave_whistles() {
  float raw[8]; rawFor(9, raw);
  const auto spectrum = hatSpectrum(render(mapped(9, raw), .6f));
  TEST_ASSERT_LESS_THAN_FLOAT(.50f, spectrum.line6);
  TEST_ASSERT_LESS_THAN_FLOAT(9000.f, spectrum.peakHz);
}

void hat_tdec_changes_excitation_without_changing_sustained_colored_tail() {
  float raw[8]; rawFor(9, raw); raw[1] = 32; raw[2] = 0;
  const auto low = render(mapped(9, raw), .6f);
  raw[2] = 127; const auto high = render(mapped(9, raw), .6f);
  TEST_ASSERT_FLOAT_WITHIN(.2f, 0, 20 * std::log10(rms(high, .2f, .5f) / rms(low, .2f, .5f)));
  float a = 0, b = 0;
  for (float x : low) a = std::max(a, std::fabs(x));
  for (float x : high) b = std::max(b, std::fabs(x));
  TEST_ASSERT_GREATER_THAN_FLOAT(a, b);
}

}
void setUp() {
  for (int k = 0; k <= kSineLen; ++k) gSine[k] = std::sin(6.28318530718f * k / kSineLen);
}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(handles_exactly_seven_percussion_machines);
  RUN_TEST(tune_tracks_notes_and_has_measured_edge_plateaus);
  RUN_TEST(useful_decay_controls_change_time_without_changing_pitch);
  RUN_TEST(vintage_inharmonicity_extreme_shortens_the_tail);
  RUN_TEST(over_is_independent_of_pitch_and_decay);
  RUN_TEST(bass_drum_timbre_modulation_is_not_a_linear_brightness_knob);
  RUN_TEST(carbon_modulation_requires_inharmonicity);
  RUN_TEST(clap_body_has_an_independent_branch_and_envelope);
  RUN_TEST(basic_feedback_and_modulation_envelope_are_separate);
  RUN_TEST(hat_empty_slots_do_not_change_sound_and_reset_is_boolean);
  RUN_TEST(hat_transient_and_tone_controls_are_independent);
  RUN_TEST(mapped_extremes_are_finite_and_modulation_graph_is_acyclic);
  RUN_TEST(bass_drum_renders_the_measured_baseline_pitch);
  RUN_TEST(carbon_interaction_is_audible_in_rendered_spectral_brightness);
  RUN_TEST(clap_body_extremes_change_rendered_bandwidth_only_when_balanced_in);
  RUN_TEST(long_decay_has_an_audible_tail_and_all_models_render_finite);
  RUN_TEST(snare_and_cymbal_baselines_have_the_measured_broad_spectrum);
  RUN_TEST(carbon_baseline_retains_the_measured_upper_component);
  RUN_TEST(clap_baseline_tail_is_not_cut_to_half_the_reference_duration);
  RUN_TEST(basic_menv_changes_late_broadband_energy_separately_from_decay);
  RUN_TEST(hat_baseline_preserves_the_measured_sharp_transient);
  RUN_TEST(baseline_machine_levels_preserve_reference_balance_with_one_shared_gain);
  RUN_TEST(clap_spacing_matches_measured_signed_rate_knots);
  RUN_TEST(compact_clap_matches_reference_energy_peak_and_duration_together);
  RUN_TEST(hat_tone_restores_the_measured_mid_band_instead_of_only_opening_highpass);
  RUN_TEST(hat_baseline_contains_broadband_energy_without_upper_octave_whistles);
  RUN_TEST(hat_tdec_changes_excitation_without_changing_sustained_colored_tail);
  return UNITY_END();
}
