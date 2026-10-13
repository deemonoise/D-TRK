#include <unity.h>
#include <string.h>
#include <math.h>
#include "perf_fx.h"
#include "synth_perf.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_names_and_short_names() {
  TEST_ASSERT_EQUAL_STRING("REPEAT", perfName(PerfFx::Repeat));
  TEST_ASSERT_EQUAL_STRING("FREEZE", perfName(PerfFx::Freeze));
  TEST_ASSERT_EQUAL_STRING("", perfName(PerfFx::None));
  TEST_ASSERT_EQUAL_STRING("", perfName(PerfFx::Count));
  TEST_ASSERT_EQUAL_STRING("RPT", perfShort(PerfFx::Repeat));
  TEST_ASSERT_EQUAL_STRING("FLT", perfShort(PerfFx::Filter));
}

void test_defaults_and_ranges() {
  TEST_ASSERT_EQUAL(0, perfRange(PerfFx::Filter).def);
  TEST_ASSERT_EQUAL(-64, perfRange(PerfFx::Filter).lo);
  TEST_ASSERT_EQUAL(63, perfRange(PerfFx::Filter).hi);
  TEST_ASSERT_EQUAL(6, perfRange(PerfFx::Crush).def);
  TEST_ASSERT_EQUAL(100, perfRange(PerfFx::DelayThrow).def);
  for (int f = 1; f < static_cast<int>(PerfFx::Count); ++f) {
    const PerfRange r = perfRange(static_cast<PerfFx>(f));
    TEST_ASSERT_TRUE(r.lo <= r.def && r.def <= r.hi);
  }
}

void test_values() {
  char b[16];
  perfValue(PerfFx::Repeat, perfRange(PerfFx::Repeat).def, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("1/16", b);
  perfValue(PerfFx::Filter, 0, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("OFF", b);
  perfValue(PerfFx::Filter, -20, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("LP 20", b);
  perfValue(PerfFx::Filter, 20, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("HP 20", b);
  perfValue(PerfFx::Crush, 6, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("6 BIT", b);
  perfValue(PerfFx::Reverse, 2, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("1 BAR", b);
  perfValue(PerfFx::Downsample, 1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("8 KHZ", b);
  perfValue(PerfFx::Freeze, 50, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("DRY 50", b);
}

void test_legacy_map() {
  // Old enum: None Rat2 Rat4 FltLow FltHigh DlyMax Crush Fade Mute DecShort Rat3 Rat8 RatUp RvbMax Srr Drive
  const PerfFx want[16] = {PerfFx::None,       PerfFx::Repeat, PerfFx::Repeat, PerfFx::Filter,
                           PerfFx::Filter,     PerfFx::DelayThrow, PerfFx::Crush, PerfFx::None,
                           PerfFx::None,       PerfFx::None,   PerfFx::Repeat, PerfFx::Repeat,
                           PerfFx::Roll,       PerfFx::ReverbThrow, PerfFx::Downsample, PerfFx::Drive};
  for (int i = 0; i < 16; ++i) TEST_ASSERT_EQUAL(static_cast<int>(want[i]), static_cast<int>(perfFromLegacy(i)));
  TEST_ASSERT_EQUAL(0, static_cast<int>(perfFromLegacy(200)));
}

void test_stack_press_turn_release() {
  PerfStack s;
  s.press(PerfFx::Filter);
  TEST_ASSERT_EQUAL(0, s.depth(PerfFx::Filter));
  s.turn(-20);
  TEST_ASSERT_EQUAL(-20, s.depth(PerfFx::Filter));
  s.turn(-100);
  TEST_ASSERT_EQUAL(-64, s.depth(PerfFx::Filter));  // clamped
  s.release(PerfFx::Filter);
  TEST_ASSERT_FALSE(s.held(PerfFx::Filter));
  s.press(PerfFx::Filter);
  TEST_ASSERT_EQUAL(0, s.depth(PerfFx::Filter));  // every press starts at the default
}

void test_stack_last_pressed_gets_the_encoder() {
  PerfStack s;
  s.press(PerfFx::Crush);
  s.press(PerfFx::Drive);
  s.turn(5);
  TEST_ASSERT_EQUAL(6, s.depth(PerfFx::Crush));
  TEST_ASSERT_EQUAL(65, s.depth(PerfFx::Drive));
  s.release(PerfFx::Drive);
  TEST_ASSERT_EQUAL(static_cast<int>(PerfFx::Crush), static_cast<int>(s.last()));
}

void test_stack_buffer_effects_one_at_a_time() {
  PerfStack s;
  s.press(PerfFx::Repeat);
  s.press(PerfFx::Gate);
  s.press(PerfFx::Reverse);
  TEST_ASSERT_EQUAL(static_cast<int>(PerfFx::Reverse), static_cast<int>(s.buffered()));
  s.release(PerfFx::Reverse);
  TEST_ASSERT_EQUAL(static_cast<int>(PerfFx::Repeat), static_cast<int>(s.buffered()));
  s.releaseAll();
  TEST_ASSERT_EQUAL(static_cast<int>(PerfFx::None), static_cast<int>(s.buffered()));
  TEST_ASSERT_EQUAL(0, s.count());
}

void test_stack_same_fx_twice_is_one_slot() {
  PerfStack s;  // two buttons mapped to the same effect
  s.press(PerfFx::Crush);
  s.press(PerfFx::Crush);
  TEST_ASSERT_EQUAL(1, s.count());
}

void test_stack_repress_moves_to_top_at_default() {
  PerfStack s;
  s.press(PerfFx::Crush);
  s.turn(-3);
  s.press(PerfFx::Drive);
  s.press(PerfFx::Crush);
  TEST_ASSERT_EQUAL(2, s.count());
  TEST_ASSERT_EQUAL(static_cast<int>(PerfFx::Drive), static_cast<int>(s.at(0)));
  TEST_ASSERT_EQUAL(static_cast<int>(PerfFx::Crush), static_cast<int>(s.last()));
  TEST_ASSERT_EQUAL(6, s.depth(PerfFx::Crush));
}

void test_stack_ignores_invalid_and_idle_turn() {
  PerfStack s;
  s.turn(10);  // nothing held: no-op
  TEST_ASSERT_EQUAL(0, s.count());
  s.press(PerfFx::None);
  s.press(PerfFx::Count);
  s.press(static_cast<PerfFx>(200));
  TEST_ASSERT_EQUAL(0, s.count());
  TEST_ASSERT_EQUAL(static_cast<int>(PerfFx::None), static_cast<int>(s.last()));
  s.release(PerfFx::Filter);  // not held: no-op
  TEST_ASSERT_EQUAL(0, s.count());
  TEST_ASSERT_EQUAL(100, s.depth(PerfFx::DelayThrow));  // default when not held
}

// ---- PerfBus ----

namespace {

constexpr int kN = 8192;  // samples per test signal
constexpr int kBlock = 128;
constexpr float kSps = 2000.f;  // samples per step
float inL[kN], inR[kN], outL[kN], outR[kN];
int16_t ring[2 * 4096];

void sine(float hz, float amp = 2.f) {
  for (int i = 0; i < kN; ++i) {
    inL[i] = amp * sinf(6.2831853f * hz * i / 32000.f);
    inR[i] = amp * sinf(6.2831853f * hz * i / 32000.f + 0.5f);
  }
}

void run(PerfBus& b, int from = 0, int to = kN) {
  for (int i = from; i < to; i += kBlock) {
    const int n = to - i < kBlock ? to - i : kBlock;
    memcpy(outL + i, inL + i, n * sizeof(float));
    memcpy(outR + i, inR + i, n * sizeof(float));
    b.process(outL + i, outR + i, n, kSps, true);
  }
}

float rms(const float* x, int from, int to) {
  double s = 0;
  for (int i = from; i < to; ++i) s += static_cast<double>(x[i]) * x[i];
  return static_cast<float>(sqrt(s / (to - from)));
}

float lossDb(int from = kN / 2, int to = kN) {
  return 20.f * log10f(rms(inL, from, to) / (rms(outL, from, to) + 1e-12f));
}

}  // namespace

void test_bus_idle_is_bit_exact() {
  sine(440.f);
  PerfBus a;
  a.transportStart();
  run(a);
  TEST_ASSERT_EQUAL_MEMORY(inL, outL, sizeof(inL));
  TEST_ASSERT_EQUAL_MEMORY(inR, outR, sizeof(inR));
  PerfBus b;
  b.setBuffer(ring, 4096);
  b.transportStart();
  run(b);
  TEST_ASSERT_EQUAL_MEMORY(inL, outL, sizeof(inL));
  TEST_ASSERT_EQUAL_MEMORY(inR, outR, sizeof(inR));
  // The ring holds the input: the last frame written is kN - 1 at index (kN - 1) % 4096.
  const int w = (kN - 1) % 4096;
  TEST_ASSERT_EQUAL(static_cast<int>(lroundf(inL[kN - 1] * PerfBus::kScale)), ring[2 * w]);
  TEST_ASSERT_EQUAL(static_cast<int>(lroundf(inR[kN - 1] * PerfBus::kScale)), ring[2 * w + 1]);
  // Clamped to +-4 and NaN -> -4.
  float x[3] = {9.f, NAN, -9.f}, y[3] = {0.5f, 0.f, 0.f};
  b.setBuffer(ring, 4096);
  b.process(x, y, 3, kSps, true);
  TEST_ASSERT_EQUAL(32764, ring[0]);
  TEST_ASSERT_EQUAL(-32764, ring[2]);
  TEST_ASSERT_EQUAL(-32764, ring[4]);
  TEST_ASSERT_EQUAL(4096, ring[1]);  // 0.5 x 8191 = 4095.5 -> 4096
}

void test_bus_filter_zero_is_passthrough() {
  sine(440.f);
  PerfBus b;
  b.transportStart();
  b.press(PerfFx::Filter);
  run(b);
  TEST_ASSERT_EQUAL_MEMORY(inL, outL, sizeof(inL));
  TEST_ASSERT_EQUAL_MEMORY(inR, outR, sizeof(inR));
}

void test_bus_filter_lp_cuts_highs() {
  sine(8000.f);
  PerfBus b;
  b.press(PerfFx::Filter);
  b.turn(-50);
  run(b);
  TEST_ASSERT_TRUE(lossDb() > 20.f);
  sine(100.f);
  PerfBus c;
  c.press(PerfFx::Filter);
  c.turn(-50);
  run(c);
  const float l = lossDb();
  TEST_ASSERT_TRUE(l < 3.f && l > -3.f);
}

void test_bus_filter_hp_cuts_lows() {
  sine(100.f);
  PerfBus b;
  b.press(PerfFx::Filter);
  b.turn(40);
  run(b);
  TEST_ASSERT_TRUE(lossDb() > 20.f);
}

float crush2(float x) {  // the 2-bit wet value: round(x / 4) x 4
  return x < -2.f ? -4.f : (x < 2.f ? 0.f : 4.f);
}

void test_bus_crush_quantizes() {
  sine(440.f, 3.f);  // x / 4 in +-0.75: never on a rounding edge's far side
  PerfBus b;
  b.press(PerfFx::Crush);
  b.turn(-4);  // 6 -> 2 bits
  TEST_ASSERT_EQUAL(2, b.stack().depth(PerfFx::Crush));
  run(b);
  bool seen[3] = {};
  for (int i = PerfBus::kXfade; i < kN; ++i) {
    TEST_ASSERT_TRUE(outL[i] == -4.f || outL[i] == 0.f || outL[i] == 4.f);
    TEST_ASSERT_TRUE(outR[i] == -4.f || outR[i] == 0.f || outR[i] == 4.f);
    seen[outL[i] < 0 ? 0 : (outL[i] == 0 ? 1 : 2)] = true;
  }
  TEST_ASSERT_TRUE(seen[0] && seen[1] && seen[2]);
}

void test_bus_fade_in_length() {
  sine(440.f, 3.f);
  PerfBus b;
  b.press(PerfFx::Crush);
  b.turn(-4);
  run(b, 0, kBlock);
  TEST_ASSERT_EQUAL_FLOAT(inL[0], outL[0]);  // sample 0: dry
  const int k = PerfBus::kXfade;
  TEST_ASSERT_TRUE(crush2(inL[k - 1]) != inL[k - 1]);
  TEST_ASSERT_TRUE(outL[k - 1] != inL[k - 1] && outL[k - 1] != crush2(inL[k - 1]));  // still mixed
  TEST_ASSERT_EQUAL_FLOAT(crush2(inL[k]), outL[k]);                                   // fully wet
}

void test_bus_fade_out_keeps_depth() {
  sine(440.f, 3.f);
  PerfBus b;
  b.press(PerfFx::Crush);
  b.turn(-4);  // 2 bits; released, depth() falls back to the default 6
  run(b, 0, 1024);
  b.release(PerfFx::Crush);
  run(b, 1024, 1024 + kBlock);
  for (int i = 1024; i < 1024 + PerfBus::kXfade; ++i) {
    const float d = inL[i], w = crush2(inL[i]);
    const float lo = d < w ? d : w, hi = d < w ? w : d;
    TEST_ASSERT_TRUE(outL[i] >= lo - 1e-5f && outL[i] <= hi + 1e-5f);
  }
}

void test_bus_drive() {
  sine(440.f, 4.f);
  PerfBus z;
  z.press(PerfFx::Drive);
  z.turn(-60);  // 60 -> 0
  TEST_ASSERT_EQUAL(0, z.stack().depth(PerfFx::Drive));
  run(z);
  TEST_ASSERT_EQUAL_MEMORY(inL, outL, sizeof(inL));
  PerfBus b;
  b.press(PerfFx::Drive);
  b.turn(40);  // 100
  run(b);
  float peak = 0;
  for (int i = 0; i < kN; ++i) peak = fabsf(outL[i]) > peak ? fabsf(outL[i]) : peak;
  TEST_ASSERT_TRUE(peak <= 4.f + 1e-3f);
  sine(440.f, 0.4f);
  PerfBus c;
  c.press(PerfFx::Drive);
  c.turn(40);
  run(c);
  TEST_ASSERT_TRUE(rms(outL, kN / 2, kN) > rms(inL, kN / 2, kN));
}

void test_bus_downsample_holds() {
  sine(300.f);
  PerfBus b;
  b.press(PerfFx::Downsample);
  b.turn(4);  // 8 kHz -> 500 Hz
  TEST_ASSERT_EQUAL(500, perfDownHz(b.stack().depth(PerfFx::Downsample)));
  run(b);
  const int hold = (kSynthRate + 250) / 500;  // samples per held value
  int first = -1, changes = 0;
  for (int i = 257; i < kN; ++i) {
    if (outL[i] == outL[i - 1]) continue;
    if (first < 0) first = i;
    TEST_ASSERT_EQUAL(0, (i - first) % hold);
    ++changes;
  }
  for (int i = first + 1; i < kN; ++i)  // both channels hold together
    if ((i - first) % hold) TEST_ASSERT_EQUAL_FLOAT(outR[i - 1], outR[i]);
  TEST_ASSERT_TRUE(changes > (kN - 257) / hold * 8 / 10);
}

void test_bus_gate_follows_grid() {
  sine(440.f);
  PerfBus b;
  b.transportStart();
  b.press(PerfFx::Gate);  // default 1/16: one step = 2000 samples
  TEST_ASSERT_EQUAL_FLOAT(1.f, perfGateSteps(b.stack().depth(PerfFx::Gate)));
  run(b);
  const float in = rms(inL, 2100, 2900);
  TEST_ASSERT_FLOAT_WITHIN(in * 0.02f, in, rms(outL, 2100, 2900));  // open half
  TEST_ASSERT_TRUE(rms(outL, 3100, 3900) < in * 0.001f);          // closed half
  TEST_ASSERT_FLOAT_WITHIN(in * 0.02f, in, rms(outL, 4100, 4900));  // open again
}

void test_bus_grid_does_not_drift() {
  PerfBus b;
  b.transportStart();
  const float sps = 1666.67f;
  float x[kBlock] = {}, y[kBlock] = {};
  const int blocks = 50000;
  for (int i = 0; i < blocks; ++i) b.process(x, y, kBlock, sps, true);
  const double want = fmod(static_cast<double>(blocks) * kBlock / sps, 64.0);
  TEST_ASSERT_TRUE(fabs(b.gridPos() - want) < 1e-3);
  b.process(x, y, kBlock, sps, false);  // stopped: stands still
  TEST_ASSERT_TRUE(fabs(b.gridPos() - want) < 1e-3);
  b.transportStart();
  TEST_ASSERT_TRUE(b.gridPos() == 0.0);
}

void test_bus_release_returns_live() {
  sine(440.f, 3.f);
  PerfBus b;
  b.press(PerfFx::Crush);
  run(b, 0, 1024);
  b.release(PerfFx::Crush);
  run(b, 1024, 1024 + 2 * kBlock);
  TEST_ASSERT_TRUE(memcmp(inL + 1024, outL + 1024, kBlock * sizeof(float)) != 0);  // fading out
  TEST_ASSERT_EQUAL_MEMORY(inL + 1024 + kBlock, outL + 1024 + kBlock, kBlock * sizeof(float));
  TEST_ASSERT_EQUAL_MEMORY(inR + 1024 + kBlock, outR + 1024 + kBlock, kBlock * sizeof(float));
}

void test_bus_stack_filter_crush_gate() {
  sine(1000.f);
  static float all[kN], one[kN];
  PerfBus b;
  b.transportStart();
  b.press(PerfFx::Filter);
  b.turn(-30);
  b.press(PerfFx::Crush);
  b.turn(-2);
  b.press(PerfFx::Gate);
  run(b);
  memcpy(all, outL, sizeof(all));
  for (int i = 0; i < kN; ++i) TEST_ASSERT_TRUE(isfinite(all[i]) && fabsf(all[i]) < 8.f);
  const PerfFx fx[] = {PerfFx::Filter, PerfFx::Crush, PerfFx::Gate};
  const int turns[] = {-30, -2, 0};
  for (int k = 0; k < 3; ++k) {
    PerfBus s;
    s.transportStart();
    s.press(fx[k]);
    s.turn(turns[k]);
    run(s);
    memcpy(one, outL, sizeof(one));
    TEST_ASSERT_TRUE(memcmp(all + kN / 2, one + kN / 2, sizeof(float) * kN / 2) != 0);
  }
}

void test_bus_throw_sends_and_tail() {
  sine(440.f);
  PerfBus b;
  TEST_ASSERT_EQUAL_FLOAT(0.f, b.delaySend());
  TEST_ASSERT_EQUAL_FLOAT(1.f, b.freezeDry());
  TEST_ASSERT_FALSE(b.sendTail());
  b.press(PerfFx::DelayThrow);
  TEST_ASSERT_EQUAL_FLOAT(100.f / 127.f, b.delaySend());
  TEST_ASSERT_EQUAL_FLOAT(0.f, b.reverbSend());
  run(b, 0, kBlock);
  TEST_ASSERT_TRUE(b.sendTail());
  TEST_ASSERT_TRUE(b.delayTail());
  TEST_ASSERT_FALSE(b.reverbTail());
  TEST_ASSERT_EQUAL_MEMORY(inL, outL, kBlock * sizeof(float));  // a throw leaves the mix alone
  b.release(PerfFx::DelayThrow);
  TEST_ASSERT_EQUAL_FLOAT(0.f, b.delaySend());
  float x[kBlock] = {}, y[kBlock] = {};
  for (int i = 0; i < 999; ++i) b.process(x, y, kBlock, kSps, true);  // 127872 samples
  TEST_ASSERT_TRUE(b.sendTail());
  b.process(x, y, kBlock, kSps, true);  // 128000 = 4 s
  TEST_ASSERT_FALSE(b.sendTail());
  b.press(PerfFx::Freeze);
  TEST_ASSERT_TRUE(b.freeze());
  TEST_ASSERT_EQUAL_FLOAT(0.5f, b.freezeDry());
  b.process(x, y, kBlock, kSps, true);
  TEST_ASSERT_TRUE(b.reverbTail());  // FREEZE: the reverb's tail only
  TEST_ASSERT_FALSE(b.delayTail());
}

// reset(): nothing held, no tails, the tone effects cut (no fade-out): the next block is bit-exact.
void test_bus_reset_to_rest() {
  sine(440.f);
  PerfBus b;
  b.press(PerfFx::Crush);
  b.press(PerfFx::ReverbThrow);
  run(b, 0, kBlock);
  TEST_ASSERT_TRUE(b.sendTail());
  b.reset();
  TEST_ASSERT_EQUAL(0, b.stack().count());
  TEST_ASSERT_FALSE(b.sendTail());
  run(b, 0, kBlock);
  TEST_ASSERT_EQUAL_MEMORY(inL, outL, kBlock * sizeof(float));
}

// ---- Buffer effects: REPEAT, ROLL ----

namespace {

constexpr float kSps1k = 1000.f;  // samples per step
constexpr int kBufFrames = 64000;
int16_t bigRing[2 * kBufFrames];
constexpr float kQ = 1.5f / PerfBus::kScale;  // int16 rounding

// A rising ramp -3..3 over kN samples: every sample unique (6 / kN > 1 / kScale), no clamp.
void ramp() {
  for (int i = 0; i < kN; ++i) {
    inL[i] = static_cast<float>(i) / kN * 6.f - 3.f;
    inR[i] = -inL[i];
  }
}

// Blocks of 100 samples: a press after 35 blocks lands at 3.5 steps.
void run100(PerfBus& b, int from, int to, float sps = kSps1k, bool running = true) {
  for (int i = from; i < to; i += 100) {
    const int n = to - i < 100 ? to - i : 100;
    memcpy(outL + i, inL + i, n * sizeof(float));
    memcpy(outR + i, inR + i, n * sizeof(float));
    b.process(outL + i, outR + i, n, sps, running);
  }
}

PerfBus& freshBus() {
  static PerfBus b;
  b = PerfBus();
  b.setBuffer(bigRing, kBufFrames);
  b.transportStart();
  return b;
}

// Seams on a ramp: the output falls (the crossfade back to the loop start) from seam + 1 on.
int seams(int from, int to, int* at, int max) {
  int k = 0;
  for (int i = from + 2; i < to && k < max; ++i)
    if (outL[i] < outL[i - 1] && outL[i - 1] >= outL[i - 2]) at[k++] = i - 1;
  return k;
}

}  // namespace

void test_repeat_loops_the_last_step() {
  ramp();
  PerfBus& b = freshBus();
  run100(b, 0, 3500);
  b.press(PerfFx::Repeat);  // 1/16 = 1 step: the step that started at sample 3000
  run100(b, 3500, 7500);
  for (int t = 3500 + PerfBus::kXfade; t < 7500; ++t) {
    const int k = (t - 3500) % 1000;
    if (t >= 4500 && k < PerfBus::kXfade) continue;  // seam crossfades
    TEST_ASSERT_FLOAT_WITHIN(kQ, inL[3000 + k], outL[t]);
    TEST_ASSERT_FLOAT_WITHIN(kQ, inR[3000 + k], outR[t]);
  }
  int at[8];
  TEST_ASSERT_EQUAL(3, seams(3500, 7500, at, 8));
  TEST_ASSERT_EQUAL(4500, at[0]);
  TEST_ASSERT_EQUAL(5500, at[1]);
  TEST_ASSERT_EQUAL(6500, at[2]);
}

void test_repeat_seam_is_smooth() {
  sine(440.f);  // period 72.7 samples: does not divide the 1000-sample loop
  PerfBus& b = freshBus();
  run100(b, 0, 3500);
  b.press(PerfFx::Repeat);
  run100(b, 3500, 7500);
  float slope = 0, worst = 0;
  for (int i = 1; i < 3500; ++i) slope = fmaxf(slope, fabsf(inL[i] - inL[i - 1]));
  for (int i = 3501; i < 7500; ++i) worst = fmaxf(worst, fabsf(outL[i] - outL[i - 1]));
  TEST_ASSERT_TRUE(worst < 3.f * slope);
}

void test_repeat_length_change_at_next_loop() {
  ramp();
  PerfBus& b = freshBus();
  run100(b, 0, 3500);
  b.press(PerfFx::Repeat);
  run100(b, 3500, 3800);
  b.turn(-2);  // 1/16 -> 1/8: the current loop still ends at 1000
  run100(b, 3800, 8100);
  int at[8];
  TEST_ASSERT_EQUAL(2, seams(3500, 8100, at, 8));
  TEST_ASSERT_EQUAL(4500, at[0]);
  TEST_ASSERT_EQUAL(6500, at[1]);
  for (int t = 6500 + PerfBus::kXfade; t < 8100; ++t)  // the next loop: 2 steps from 3000
    TEST_ASSERT_FLOAT_WITHIN(kQ, inL[3000 + t - 6500], outL[t]);
}

void test_roll_halves() {
  ramp();
  PerfBus& b = freshBus();
  run100(b, 0, 3500);
  b.press(PerfFx::Roll);
  b.turn(-1);  // N = 1: halves after every loop
  run100(b, 3500, 8100);
  // 2000, 1000, 500, 250, then 1/64 (0.25 step) stays.
  const int want[] = {5500, 6500, 7000, 7250, 7500, 7750, 8000};
  int at[16];
  TEST_ASSERT_EQUAL(7, seams(3500, 8100, at, 16));
  for (int i = 0; i < 7; ++i) TEST_ASSERT_EQUAL(want[i], at[i]);
  TEST_ASSERT_FLOAT_WITHIN(kQ, inL[3000 + 100], outL[7750 + 100]);  // still from the step start
}

void test_buffer_release_returns_live() {
  ramp();
  PerfBus& b = freshBus();
  run100(b, 0, 3500);
  b.press(PerfFx::Repeat);
  run100(b, 3500, 5000);
  b.release(PerfFx::Repeat);
  run100(b, 5000, 6000);
  TEST_ASSERT_TRUE(fabsf(outL[5010] - inL[5010]) > 0.01f);  // crossfading
  TEST_ASSERT_TRUE(fabsf(outR[5010] - inR[5010]) > 0.01f);
  const int k = 5000 + PerfBus::kXfade;
  TEST_ASSERT_EQUAL_MEMORY(inL + k, outL + k, (6000 - k) * sizeof(float));
  TEST_ASSERT_EQUAL_MEMORY(inR + k, outR + k, (6000 - k) * sizeof(float));
}

void test_repeat_hold_longer_than_the_ring() {
  ramp();
  PerfBus b;
  b.setBuffer(ring, 4096);  // the writer comes round to the loop after ~3600 samples
  b.transportStart();
  run100(b, 0, 3500);
  b.press(PerfFx::Repeat);
  run100(b, 3500, kN);
  for (int t = 7600; t < kN; ++t) {
    const int k = (t - 3500) % 1000;
    if (k < PerfBus::kXfade) continue;
    TEST_ASSERT_FLOAT_WITHIN(kQ, inL[3000 + k], outL[t]);
  }
}

void test_repeat_triplet_lengths_average() {
  ramp();
  PerfBus& b = freshBus();
  run100(b, 0, 3500);
  b.press(PerfFx::Repeat);
  b.turn(1);  // 1/16T = 2/3 step = 666.67 samples
  run100(b, 3500, kN);
  int at[16];
  const int k = seams(3500, kN, at, 16);
  TEST_ASSERT_TRUE(k >= 6);
  TEST_ASSERT_EQUAL(3500 + 667, at[0]);  // ceil of the first 666.67
  for (int i = 1; i < k; ++i) {
    const int len = at[i] - at[i - 1];
    TEST_ASSERT_TRUE(len == 666 || len == 667);
  }
  TEST_ASSERT_EQUAL(2000, at[3] - at[0]);  // three loops: 2000 exactly
  TEST_ASSERT_FLOAT_WITHIN(1.f, 666.67f * (k - 1), static_cast<float>(at[k - 1] - at[0]));
}

void test_roll_default_n2() {
  ramp();
  PerfBus& b = freshBus();
  const float sps = 500.f;
  run100(b, 0, 1750, sps);  // 3.5 steps
  b.press(PerfFx::Roll);    // N = 2: two loops per length
  run100(b, 1750, kN, sps);
  // 1000, 1000, 500, 500, 250, 250, 125 (1/64 floor) ...
  const int want[] = {2750, 3750, 4250, 4750, 5000, 5250, 5375, 5500, 5625};
  int at[64];
  TEST_ASSERT_TRUE(seams(1750, kN, at, 64) >= 9);
  for (int i = 0; i < 9; ++i) TEST_ASSERT_EQUAL(want[i], at[i]);
  TEST_ASSERT_FLOAT_WITHIN(kQ, inL[1500 + 100], outL[5375 + 100]);
}

void test_capture_after_parked_writer_is_fresh() {
  ramp();
  PerfBus b;
  b.setBuffer(ring, 4096);
  b.transportStart();
  run100(b, 0, 3500);
  b.press(PerfFx::Repeat);
  run100(b, 3500, 7500);  // the writer parked at the loop start around 7100
  b.press(PerfFx::Roll);  // grid at 7.5 steps: the step start (7000) was never recorded
  run100(b, 7500, kN);
  for (int t = 7500 + PerfBus::kXfade; t < kN; ++t) {  // 2000-sample loop from the press point
    TEST_ASSERT_FLOAT_WITHIN(kQ, inL[t], outL[t]);
    TEST_ASSERT_FLOAT_WITHIN(kQ, inR[t], outR[t]);
  }
}

void test_repeat_paused_starts_at_press() {
  ramp();
  PerfBus& b = freshBus();
  run100(b, 0, 3500);  // grid at 3.5 steps, then the transport pauses
  b.press(PerfFx::Repeat);
  run100(b, 3500, 6000, kSps1k, false);
  for (int t = 3500 + PerfBus::kXfade; t < 6000; ++t) {
    const int k = (t - 3500) % 1000;
    if (t >= 4500 && k < PerfBus::kXfade) continue;
    TEST_ASSERT_FLOAT_WITHIN(kQ, inL[3500 + k], outL[t]);
  }
}

// ---- Buffer effects: REVERSE, SCRATCH, TAPE STOP ----

namespace {

constexpr int kM = 16384;  // long signals: a press at sample 10000
float xL[kM], xR[kM], yL[kM], yR[kM];

float rampAt(float i) { return i / kM * 6.f - 3.f; }  // 3 int16 units a sample: unique

void longRamp() {
  for (int i = 0; i < kM; ++i) {
    xL[i] = rampAt(static_cast<float>(i));
    xR[i] = -xL[i];
  }
}

void runLong(PerfBus& b, int from, int to, float sps = kSps1k) {
  for (int i = from; i < to; i += 100) {
    const int n = to - i < 100 ? to - i : 100;
    memcpy(yL + i, xL + i, n * sizeof(float));
    memcpy(yR + i, xR + i, n * sizeof(float));
    b.process(yL + i, yR + i, n, sps, true);
  }
}

}  // namespace

void test_reverse_reads_backwards() {
  longRamp();
  PerfBus& b = freshBus();
  runLong(b, 0, 10000);
  b.press(PerfFx::Reverse);
  b.turn(-2);  // 1 bar -> 1/4 bar = 4000 samples
  runLong(b, 10000, kM);
  for (int k = PerfBus::kXfade; k < kM - 10000; ++k) {
    const int j = k % 4000;
    if (k >= 4000 && j < PerfBus::kXfade) continue;  // seam crossfade
    TEST_ASSERT_FLOAT_WITHIN(kQ, xL[9999 - j], yL[10000 + k]);
    TEST_ASSERT_FLOAT_WITHIN(kQ, xR[9999 - j], yR[10000 + k]);
  }
}

void test_scratch_stays_in_the_past() {
  ramp();
  PerfBus& b = freshBus();
  run100(b, 0, 3500);
  b.press(PerfFx::Scratch);  // 1/8: T = 2000, A = T / pi, around frame 3498
  run100(b, 3500, kN);
  const float a = 2000.f / 3.14159265f;
  auto at = [](float i) { return i / kN * 6.f - 3.f; };
  const float tol = 3.f * 6.f / kN;  // 3 frames of the ramp
  bool differs = false;
  for (int t = 3500; t < kN; ++t) {
    TEST_ASSERT_TRUE(isfinite(outL[t]) && isfinite(outR[t]));
    if (t < 3500 + PerfBus::kXfade) continue;
    TEST_ASSERT_TRUE(outL[t] <= at(3498.f) + kQ);  // never at or past the writer (frame t)
    TEST_ASSERT_TRUE(outL[t] >= at(3498.f - 2.f * a - 1.f) - kQ);
    differs = differs || fabsf(outL[t] - inL[t]) > 0.01f;
  }
  TEST_ASSERT_TRUE(differs);
  for (int p = 0; p < 2; ++p) {  // the swing: top at T/4, bottom at 3T/4, every period
    TEST_ASSERT_FLOAT_WITHIN(tol, at(3498.f), outL[3500 + 500 + 2000 * p]);
    TEST_ASSERT_FLOAT_WITHIN(tol, at(3498.f - 2.f * a), outL[3500 + 1500 + 2000 * p]);
    TEST_ASSERT_FLOAT_WITHIN(tol, at(3498.f - a), outL[3500 + 2000 + 2000 * p]);
  }
}

void test_tape_stop_slows_to_silence() {
  ramp();
  PerfBus& b = freshBus();
  run100(b, 0, 3500);
  b.press(PerfFx::TapeStop);  // 1/4 bar: 1 -> 0 over 4000 samples
  run100(b, 3500, 8000);
  auto at = [](double i) { return static_cast<float>(i / kN * 6.0 - 3.0); };
  for (int t = 0; t < 4000; t += 50) {
    const double pos = t - t * (t - 1) / 2.0 / 4000.0;  // sum of the speeds 1 - k / 4000
    if (t < PerfBus::kXfade) continue;
    TEST_ASSERT_FLOAT_WITHIN(3.f * kQ, at(3500 + pos), outL[3500 + t]);
  }
  // Slowing: the step between samples falls linearly.
  const float d1 = outL[3500 + 1001] - outL[3500 + 1000], d3 = outL[3500 + 3001] - outL[3500 + 3000];
  TEST_ASSERT_FLOAT_WITHIN(0.15f, 3.f, d1 / d3);  // speed 0.75 vs 0.25
  for (int t = 3500 + 4000 + PerfBus::kXfade + 1; t < 8000; ++t) {
    TEST_ASSERT_EQUAL_FLOAT(0.f, outL[t]);
    TEST_ASSERT_EQUAL_FLOAT(0.f, outR[t]);
  }
  b.release(PerfFx::TapeStop);  // crossfades back to live
  run100(b, 8000, kN);
  TEST_ASSERT_TRUE(outL[8010] != inL[8010]);
  const int k = 8000 + PerfBus::kXfade;
  TEST_ASSERT_EQUAL_MEMORY(inL + k, outL + k, (kN - k) * sizeof(float));
}

void test_two_buffer_fx_last_wins_and_returns() {
  ramp();
  PerfBus& b = freshBus();
  run100(b, 0, 3500);
  b.press(PerfFx::Repeat);
  run100(b, 3500, 4000);
  b.press(PerfFx::Reverse);  // the newest buffer effect plays
  run100(b, 4000, 5500);
  for (int k = PerfBus::kXfade; k < 1500; ++k)
    TEST_ASSERT_FLOAT_WITHIN(kQ, inL[3999 - k], outL[4000 + k]);
  b.release(PerfFx::Reverse);  // REPEAT again, captured now: the step from 5000
  run100(b, 5500, 7500);
  for (int k = PerfBus::kXfade; k < 2000; ++k) {
    const int j = k % 1000;
    if (k >= 1000 && j < PerfBus::kXfade) continue;
    TEST_ASSERT_FLOAT_WITHIN(kQ, inL[5000 + j], outL[5500 + k]);
  }
}

void test_reverse_hold_longer_than_the_ring() {
  ramp();
  PerfBus b;
  b.setBuffer(ring, 4096);  // the writer comes round to the window after ~2000 samples
  b.transportStart();
  const float sps = 500.f;
  run100(b, 0, 3500, sps);
  b.press(PerfFx::Reverse);
  b.turn(-2);  // 1/4 bar = 2000 samples
  run100(b, 3500, kN, sps);
  for (int k = PerfBus::kXfade; k < kN - 3500; ++k) {
    const int j = k % 2000;
    if (k >= 2000 && j < PerfBus::kXfade) continue;
    TEST_ASSERT_FLOAT_WITHIN(kQ, inL[3499 - j], outL[3500 + k]);
  }
}

void test_reverse_waits_for_fresh_audio() {
  longRamp();
  PerfBus b;
  b.setBuffer(ring, 4096);
  b.transportStart();
  runLong(b, 0, 3500);
  b.press(PerfFx::Repeat);
  runLong(b, 3500, 7500);  // the writer parks at the loop start (~7100): nothing fresh
  b.press(PerfFx::Reverse);
  runLong(b, 7500, kM);
  // No stale window: live until 580 frames (512 + the margin) are recorded again, from 8100 on.
  const int k = 7500 + PerfBus::kXfade;
  TEST_ASSERT_EQUAL_MEMORY(xL + k, yL + k, (8100 - k) * sizeof(float));
  // Then a proper reverse from the new press point over what was recorded (600 - 68 frames).
  for (int j = PerfBus::kXfade; j < 500; ++j) TEST_ASSERT_FLOAT_WITHIN(kQ, xL[8099 - j], yL[8100 + j]);
}

void test_reverse_turn_applies_at_the_seam() {
  longRamp();
  PerfBus& b = freshBus();
  const float sps = 500.f;
  runLong(b, 0, 10000, sps);
  b.press(PerfFx::Reverse);
  b.turn(-2);  // 1/4 bar = 2000
  runLong(b, 10000, 10500, sps);
  b.turn(1);  // 1/2 bar = 4000, from the next loop
  runLong(b, 10500, kM, sps);
  for (int k = PerfBus::kXfade; k < 2000; ++k) TEST_ASSERT_FLOAT_WITHIN(kQ, xL[9999 - k], yL[10000 + k]);
  for (int k = PerfBus::kXfade; k < 4000; ++k) TEST_ASSERT_FLOAT_WITHIN(kQ, xL[9999 - k], yL[12000 + k]);
  for (int k = PerfBus::kXfade; k < kM - 16000; ++k)
    TEST_ASSERT_FLOAT_WITHIN(kQ, xL[9999 - k], yL[16000 + k]);
}

void test_scratch_hold_longer_than_the_ring() {
  ramp();
  PerfBus b;
  b.setBuffer(ring, 4096);  // the writer comes round to the swing after ~3400 samples
  b.transportStart();
  const float sps = 500.f;
  run100(b, 0, 3500, sps);
  b.press(PerfFx::Scratch);  // 1/8 = 2 steps: T = 1000, A = T / pi
  run100(b, 3500, kN, sps);
  const float a = 1000.f / 3.14159265f;
  auto at = [](float i) { return i / kN * 6.f - 3.f; };
  const float tol = 3.f * 6.f / kN;
  for (int t = 3500 + PerfBus::kXfade; t < kN; ++t) {
    TEST_ASSERT_TRUE(outL[t] <= at(3498.f) + kQ);
    TEST_ASSERT_TRUE(outL[t] >= at(3498.f - 2.f * a - 1.f) - kQ);
  }
  for (int p = 3; p < 5; ++p) {  // periods after the writer came round
    TEST_ASSERT_FLOAT_WITHIN(tol, at(3498.f), outL[3500 + 250 + 1000 * p]);
    if (3500 + 750 + 1000 * p < kN)
      TEST_ASSERT_FLOAT_WITHIN(tol, at(3498.f - 2.f * a), outL[3500 + 750 + 1000 * p]);
  }
}

// First sample from which the output stays exactly 0 (-1: never).
int silentFrom(const float* x, int from, int to) {
  int z = -1;
  for (int i = from; i < to; ++i) {
    if (x[i] != 0.f) z = -1;
    else if (z < 0) z = i;
  }
  return z;
}

void test_tape_stop_turn_mid_stop() {
  ramp();
  PerfBus& b = freshBus();
  const float sps = 500.f;
  run100(b, 0, 3500, sps);
  b.press(PerfFx::TapeStop);  // 1/4 bar = 2000 samples
  run100(b, 3500, 4000, sps);
  b.turn(1);  // 1/2 bar: speed 0.75 falls on the 4000 slope, 3000 samples more
  run100(b, 4000, kN, sps);
  const float d0 = outL[3999] - outL[3998], d1 = outL[4001] - outL[4000];
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.f, d1 / d0);  // no speed jump at the turn
  const int z = silentFrom(outL, 3500, kN);
  TEST_ASSERT_INT_WITHIN(64, 7000 + PerfBus::kXfade, z);
}

void test_tape_stop_long_lands_on_time() {
  static int16_t tapeRing[2 * 100000];
  PerfBus b;
  b.setBuffer(tapeRing, 100000);
  b.transportStart();
  const float sps = 11875.f;  // 1 bar = 190000 samples
  float x[kBlock], y[kBlock];
  int t = 0, stop = -1;
  for (; t < 20000; t += kBlock) {
    for (int i = 0; i < kBlock; ++i) x[i] = y[i] = 1.f;
    b.process(x, y, kBlock, sps, true);
  }
  b.press(PerfFx::TapeStop);
  b.turn(2);  // 1 bar
  const int press = t;
  for (; t < press + 200000 && stop < 0; t += kBlock) {
    for (int i = 0; i < kBlock; ++i) x[i] = y[i] = 1.f;
    b.process(x, y, kBlock, sps, true);
    for (int i = 0; i < kBlock && stop < 0; ++i)
      if (x[i] < 0.999f) stop = t + i;  // the fade after the stop
  }
  TEST_ASSERT_INT_WITHIN(64, press + 190000, stop);
}

void test_repeat_without_buffer_is_passthrough() {
  ramp();
  PerfBus b;
  b.setBuffer(nullptr, 0);
  b.transportStart();
  run100(b, 0, 3500);
  b.press(PerfFx::Repeat);
  run100(b, 3500, 6000);
  b.release(PerfFx::Repeat);
  b.press(PerfFx::Roll);
  run100(b, 6000, kN);
  TEST_ASSERT_EQUAL_MEMORY(inL, outL, sizeof(inL));
  TEST_ASSERT_EQUAL_MEMORY(inR, outR, sizeof(inR));
}

// The sequencer's step clock re-anchors the grid: step k starting off samples into a block puts
// the block start at k - off / sps (modulo kGridWrap), whatever the grid had drifted to.
void test_bus_grid_reanchors_to_the_step_clock() {
  PerfBus b;
  b.transportStart();
  float x[kBlock] = {}, y[kBlock] = {};
  for (int i = 0; i < 10; ++i) b.process(x, y, kBlock, kSps, true);  // 0.64 steps
  b.stepAt(5, 32);
  b.process(x, y, kBlock, kSps, true);
  TEST_ASSERT_TRUE(fabs(b.gridPos() - (5.0 + (kBlock - 32) / kSps)) < 1e-6);
  b.stepAt(0, 64);  // block start before step 0: wraps to the end of the grid
  b.process(x, y, kBlock, kSps, true);
  TEST_ASSERT_TRUE(fabs(b.gridPos() - (kBlock - 64) / kSps) < 1e-6);
  b.stepAt(9, 0);
  b.process(x, y, kBlock, kSps, false);  // not running: ignored, the grid stands still
  TEST_ASSERT_TRUE(fabs(b.gridPos() - (kBlock - 64) / kSps) < 1e-6);
  b.process(x, y, kBlock, kSps, true);  // and not applied late
  TEST_ASSERT_TRUE(fabs(b.gridPos() - 2.0 * (kBlock - 32) / kSps) < 1e-6);
}

// GATE follows the re-anchored grid: step 0 placed at sample 1000 opens the gate there.
void test_bus_gate_follows_the_step_clock() {
  sine(440.f);
  PerfBus b;
  b.transportStart();
  b.press(PerfFx::Gate);
  run(b, 0, 896);
  b.stepAt(0, 1000 - 896);
  run(b, 896, kN);
  const float in = rms(inL, 1100, 1900);
  TEST_ASSERT_FLOAT_WITHIN(in * 0.02f, in, rms(outL, 1100, 1900));  // open half
  TEST_ASSERT_TRUE(rms(outL, 2100, 2900) < in * 0.001f);          // closed half
}

// FILTER from LP to HP while held (Shift x10 skips 0): no jump, the filter fades out, resets and
// fades back in.
void test_bus_filter_sign_change_no_click() {
  sine(100.f);
  PerfBus b;
  b.press(PerfFx::Filter);
  b.turn(-5);
  run(b, 0, 4096);
  b.turn(10);
  TEST_ASSERT_EQUAL(5, b.stack().depth(PerfFx::Filter));
  run(b, 4096, kN);
  float jump = 0;
  for (int i = 4000; i < kN; ++i) {
    const float d = fabsf(outL[i] - outL[i - 1]);
    if (d > jump) jump = d;
  }
  TEST_ASSERT_TRUE(jump < 0.1f);
  TEST_ASSERT_TRUE(lossDb(6144, kN) < 3.f);  // HP at the bottom: 100 Hz passes
}

// CRUSH: NaN / inf input never reaches the int conversion (UB); the output stays finite, +-4.
void test_bus_crush_nan_input() {
  sine(440.f, 3.f);
  PerfBus b;
  b.press(PerfFx::Crush);
  run(b, 0, 1024);  // fully wet
  float x[4] = {NAN, INFINITY, -INFINITY, 100.f}, y[4] = {-NAN, -INFINITY, INFINITY, -100.f};
  b.process(x, y, 4, kSps, true);
  for (int i = 0; i < 4; ++i) {
    TEST_ASSERT_TRUE(isfinite(x[i]) && fabsf(x[i]) <= 4.f);
    TEST_ASSERT_TRUE(isfinite(y[i]) && fabsf(y[i]) <= 4.f);
  }
  TEST_ASSERT_EQUAL_FLOAT(0.f, x[0]);
  TEST_ASSERT_EQUAL_FLOAT(4.f, x[1]);
  TEST_ASSERT_EQUAL_FLOAT(-4.f, x[2]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_names_and_short_names);
  RUN_TEST(test_defaults_and_ranges);
  RUN_TEST(test_values);
  RUN_TEST(test_legacy_map);
  RUN_TEST(test_stack_press_turn_release);
  RUN_TEST(test_stack_last_pressed_gets_the_encoder);
  RUN_TEST(test_stack_buffer_effects_one_at_a_time);
  RUN_TEST(test_stack_same_fx_twice_is_one_slot);
  RUN_TEST(test_stack_repress_moves_to_top_at_default);
  RUN_TEST(test_stack_ignores_invalid_and_idle_turn);
  RUN_TEST(test_bus_idle_is_bit_exact);
  RUN_TEST(test_bus_filter_zero_is_passthrough);
  RUN_TEST(test_bus_filter_lp_cuts_highs);
  RUN_TEST(test_bus_filter_hp_cuts_lows);
  RUN_TEST(test_bus_crush_quantizes);
  RUN_TEST(test_bus_fade_in_length);
  RUN_TEST(test_bus_fade_out_keeps_depth);
  RUN_TEST(test_bus_drive);
  RUN_TEST(test_bus_downsample_holds);
  RUN_TEST(test_bus_gate_follows_grid);
  RUN_TEST(test_bus_grid_does_not_drift);
  RUN_TEST(test_bus_release_returns_live);
  RUN_TEST(test_bus_stack_filter_crush_gate);
  RUN_TEST(test_bus_throw_sends_and_tail);
  RUN_TEST(test_bus_reset_to_rest);
  RUN_TEST(test_repeat_loops_the_last_step);
  RUN_TEST(test_repeat_seam_is_smooth);
  RUN_TEST(test_repeat_length_change_at_next_loop);
  RUN_TEST(test_roll_halves);
  RUN_TEST(test_buffer_release_returns_live);
  RUN_TEST(test_repeat_hold_longer_than_the_ring);
  RUN_TEST(test_repeat_triplet_lengths_average);
  RUN_TEST(test_roll_default_n2);
  RUN_TEST(test_capture_after_parked_writer_is_fresh);
  RUN_TEST(test_repeat_paused_starts_at_press);
  RUN_TEST(test_reverse_reads_backwards);
  RUN_TEST(test_scratch_stays_in_the_past);
  RUN_TEST(test_tape_stop_slows_to_silence);
  RUN_TEST(test_two_buffer_fx_last_wins_and_returns);
  RUN_TEST(test_reverse_hold_longer_than_the_ring);
  RUN_TEST(test_reverse_waits_for_fresh_audio);
  RUN_TEST(test_reverse_turn_applies_at_the_seam);
  RUN_TEST(test_scratch_hold_longer_than_the_ring);
  RUN_TEST(test_tape_stop_turn_mid_stop);
  RUN_TEST(test_tape_stop_long_lands_on_time);
  RUN_TEST(test_repeat_without_buffer_is_passthrough);
  RUN_TEST(test_bus_grid_reanchors_to_the_step_clock);
  RUN_TEST(test_bus_gate_follows_the_step_clock);
  RUN_TEST(test_bus_filter_sign_change_no_click);
  RUN_TEST(test_bus_crush_nan_input);
  return UNITY_END();
}
