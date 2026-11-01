#include <cmath>
#include <unity.h>
#include "synth_syn.h"
#include "fastmath.h"
using namespace mt;
void setUp() {}
void tearDown() {}

static BlOut pulse(BlOsc& o, float dt, float pw, float dpw) {
  const float rel = dt - dpw;
  return o.squarePwm(dt, 1.f / dt, pw, dpw, rel != 0 ? 1.f / rel : 0.f, -1.f);
}

void test_pwm_has_no_dc() {
  BlOsc o;
  double mean = 0;
  for (int i = 0; i < 88200; ++i) {
    const float y = pulse(o, 100.f / kSynthRate, 0.95f, 0).v;
    if (i >= 44100) mean += y;
  }
  TEST_ASSERT_FLOAT_WITHIN(0.002f, 0.f, static_cast<float>(mean / 44100));
}

void test_moving_pwm_edge_is_corrected() {
  BlOsc o;
  constexpr float dt = .001f, dpw = .45f / 32;
  o.t = .6f;
  o.prev = -1.f;
  float last = pulse(o, dt, .5f, 0).v, jump = 0;
  for (int i = 1; i <= 32; ++i) {
    const float y = pulse(o, dt, .5f + dpw * i, dpw).v;
    jump = fmaxf(jump, fabsf(y - last));
    last = y;
  }
  TEST_ASSERT_LESS_THAN_FLOAT(1.8f, jump);
}

void test_triangle_sync_changes_slave_pitch() {
  SynVoice free, synced;
  SynParams p;
  p.mode[0] = p.mode[1] = static_cast<uint8_t>(SynOsc::Tri);
  p.hz[0] = 300; p.hz[1] = 711; p.mix = 1;
  free.trigger(); synced.trigger();
  double difference = 0;
  for (int b = 0; b < 200; ++b) {
    float a[32] = {}, c[32] = {};
    p.sync = false; free.control(p, 32); free.render(a, 32, 1);
    p.sync = true; synced.control(p, 32); synced.render(c, 32, 1);
    if (b > 20) for (int i = 0; i < 32; ++i) difference += fabsf(a[i] - c[i]);
  }
  TEST_ASSERT_GREATER_THAN_FLOAT(.1f, difference / (179 * 32));
}
// The pulse was low at phase .7; reset half-way through the interval raises it.
// A half-sample BLEP distributes the two-unit step equally over its two neighbouring samples.
void test_pwm_sync_corrects_both_sides_of_reset() {
  BlOsc o;
  o.t = .7f; o.prev = -1.f; o.triv = .5f;
  const float before = o.squarePwm(.02f, 50.f, .5f, 0.f, 50.f, .5f).v;
  const float after = o.squarePwm(.02f, 50.f, .5f, 0.f, 50.f, -1.f).v;
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -.75f, before);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, .75f, after);
}

// Falling at sample -.5, then resetting at -.25, gives a quarter-sample low pulse:
// its two BLEPs reduce adjacent samples by .1875/.3125 rather than making a full-size notch.
void test_pwm_sync_composes_with_own_falling_edge() {
  BlOsc o;
  o.t = .49f; o.prev = 1.f; o.triv = .5f;
  const float before = o.squarePwm(.02f, 50.f, .5f, 0.f, 50.f, .25f).v;
  const float after = o.squarePwm(.02f, 50.f, .5f, 0.f, 50.f, -1.f).v;
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, .8125f, before);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, .6875f, after);
}

// Width overtakes the oscillator (negative relative edge velocity): this is a rising edge,
// the opposite of the oscillator passing a stationary width. The DC values before/after are .1/.3.
void test_reverse_pwm_crossing_has_correct_polarity_and_time() {
  BlOsc o;
  o.t = .6f; o.prev = -1.f; o.triv = .55f;
  const float before = pulse(o, .01f, .65f, .1f).v;
  const float after = pulse(o, .01f, .65f, 0.f).v;
  TEST_ASSERT_FLOAT_WITHIN(2e-6f, -.90246914f, before);
  TEST_ASSERT_FLOAT_WITHIN(2e-6f, .39135802f, after);
}

// At phase .5 the native +4 -> -4 triangle cusp and the reset's -4 -> +4 slope change
// coincide and cancel. Only the two-unit level reset remains, with its half-sample BLEP.
void test_triangle_sync_at_own_peak_cancels_opposite_cusps() {
  BlOsc o;
  o.t = .49f; o.prev = .96f;
  const float before = o.tri(.02f, 50.f, .5f).v;
  const float after = o.tri(.02f, 50.f, -1.f).v;
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, .71f, before);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -.71f, after);
}

// Removing DC preserves the audible AC gain, rather than normalising narrow pulses and
// silently lowering their output: at 95% PW the plateaus are +.1/-1.9, peak-to-peak 2.
void test_static_pwm_preserves_ac_gain_and_documented_peak() {
  BlOsc o;
  float lo = 0, hi = 0;
  double energy = 0;
  for (int i = 0; i < 44100; ++i) {
    const float y = pulse(o, 100.f / kSynthRate, .95f, 0.f).v;
    if (i > 4410) { lo = fminf(lo, y); hi = fmaxf(hi, y); energy += y * y; }
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, .1f, hi);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, -1.9f, lo);
  TEST_ASSERT_FLOAT_WITHIN(.005f, .4312f, sqrtf(static_cast<float>(energy / (44100 - 4411))));
}

// Compare the production block specialisations with the generic edge renderer across both
// PWM directions, held widths, control-slope changes and oscillator wraps at low/high pitch.
void test_fast_pwm_blocks_match_generic_edge_rendering() {
  constexpr float targets[] = {0.f, .8f, .8f, 0.f, .1f, .9f, .9f, .2f};
  constexpr float pitches[] = {20.f, 220.f, 19000.f};
  for (float hz : pitches) {
    SynVoice voice;
    SynParams p;
    p.mode[0] = static_cast<uint8_t>(SynOsc::Square);
    p.hz[0] = hz;
    BlOsc ref;
    voice.trigger();
    float shape = targets[0];
    const float dt = hz / kSynthRate, inv = fastRcp(dt);
    for (int block = 0; block < 64; ++block) {
      p.shape[0] = targets[block % 8];
      const float step = block ? (p.shape[0] - shape) / 32.f : 0.f;
      const float dpw = .45f * step;
      const float relative = dt - dpw;
      const float ei = relative > 0.f ? fastRcp(relative) : (relative < 0.f ? -fastRcp(-relative) : 0.f);
      float out[32] = {};
      voice.control(p, 32);
      voice.render(out, 32, 1.f);
      float sh = shape;
      for (int i = 0; i < 32; ++i, sh += step) {
        const float expected = ref.squarePwm(dt, inv, .5f + .45f * sh, dpw, ei, -1.f).v;
        TEST_ASSERT_FLOAT_WITHIN(2e-5f, expected, out[i]);
      }
      shape += step * 32;
    }
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fast_pwm_blocks_match_generic_edge_rendering);
  RUN_TEST(test_pwm_sync_corrects_both_sides_of_reset);
  RUN_TEST(test_pwm_sync_composes_with_own_falling_edge);
  RUN_TEST(test_reverse_pwm_crossing_has_correct_polarity_and_time);
  RUN_TEST(test_triangle_sync_at_own_peak_cancels_opposite_cusps);
  RUN_TEST(test_static_pwm_preserves_ac_gain_and_documented_peak);
  RUN_TEST(test_pwm_has_no_dc);
  RUN_TEST(test_moving_pwm_edge_is_corrected);
  RUN_TEST(test_triangle_sync_changes_slave_pitch);
  return UNITY_END();
}
