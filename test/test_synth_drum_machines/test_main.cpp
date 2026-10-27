#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>
#include "synth_drum_machines.h"
#include "synth_osc.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static bool sane(const DrumParams& p) {
  const float lim = kSynthRate * 0.45f;
  const float* f[] = {&p.toneHz[0], &p.toneHz[1], &p.toneMs, &p.pitchEnv, &p.pitchMs, &p.click,
                      &p.metalLvl, &p.metalMs, &p.metalBp1, &p.metalBp2, &p.metalHp, &p.noiseLvl,
                      &p.noiseMs, &p.noiseHz, &p.noiseQ, &p.burstMs, &p.tail, &p.drive};
  for (const float* x : f)
    if (!isfinite(*x) || *x < 0) return false;
  for (float h : p.metalHz)
    if (!isfinite(h) || h < 0) return false;
  return p.toneHz[0] < lim && p.noiseHz < lim && p.metalBp1 < lim && p.metalHp < lim && p.drive <= 1;
}

void test_all_machines_sane() {
  const float edges[] = {0, 64, 127};
  for (int mc = 0; mc < static_cast<int>(DrumMachine::Count); ++mc)
    for (float e : edges)
      for (int note = 24; note <= 108; note += 12) {
        float mac[kFmMacros];
        for (float& x : mac) x = e;
        DrumParams p;
        drumMachine(static_cast<uint8_t>(mc), mac, note, p);
        TEST_ASSERT_TRUE_MESSAGE(sane(p), drumMachineName(static_cast<uint8_t>(mc)));
      }
}

void test_bd8_base_pitch_c4() {
  float mac[kFmMacros] = {90, 0, 0, 0, 0};
  DrumParams p;
  drumMachine(static_cast<uint8_t>(DrumMachine::Bd8), mac, 60, p);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 45.f, p.toneHz[0]);
  drumMachine(static_cast<uint8_t>(DrumMachine::Bd8), mac, 72, p);
  TEST_ASSERT_FLOAT_WITHIN(1.f, 90.f, p.toneHz[0]);
}

void test_hh8_decay_monotonic() {
  float a[kFmMacros] = {10, 64, 30, 64, 64}, b[kFmMacros] = {120, 64, 30, 64, 64};
  DrumParams pa, pb;
  drumMachine(static_cast<uint8_t>(DrumMachine::Hh8), a, 60, pa);
  drumMachine(static_cast<uint8_t>(DrumMachine::Hh8), b, 60, pb);
  TEST_ASSERT_TRUE(pb.metalMs > pa.metalMs * 10);
}

void test_names() {
  TEST_ASSERT_EQUAL_STRING("BD8", drumMachineName(0));
  TEST_ASSERT_EQUAL_STRING("CY9", drumMachineName(15));
  TEST_ASSERT_EQUAL_STRING("BD8", drumMachineName(200));  // out of range = BD8
  TEST_ASSERT_EQUAL_STRING("SNAPPY", drumMacroName(static_cast<uint8_t>(DrumMachine::Sd8), kMacShp));
  TEST_ASSERT_EQUAL_STRING("", drumMacroName(static_cast<uint8_t>(DrumMachine::Rs8), kMacCon));
}

// Every machine at mid macros makes sound, stays bounded and ends (one-shot).
void test_every_machine_sounds_and_ends() {
  static float buf[32];
  for (int mc = 0; mc < static_cast<int>(DrumMachine::Count); ++mc) {
    float mac[kFmMacros] = {64, 64, 64, 64, 64};
    DrumParams p;
    drumMachine(static_cast<uint8_t>(mc), mac, 60, p);
    DrumVoice d;
    d.trigger(false);
    float peak = 0;
    int blk = 0;
    for (; blk < 4 * kSynthRate / 32 && !d.done(); ++blk) {
      memset(buf, 0, sizeof buf);
      d.control(p, 32);
      d.render(buf, 32, 1.f);
      for (float x : buf) {
        TEST_ASSERT_TRUE(isfinite(x));
        peak = fmaxf(peak, fabsf(x));
      }
    }
    TEST_ASSERT_TRUE_MESSAGE(peak > 0.05f && peak <= 1.f, drumMachineName(static_cast<uint8_t>(mc)));
    TEST_ASSERT_TRUE_MESSAGE(d.done(), drumMachineName(static_cast<uint8_t>(mc)));
  }
}

// DECAY follows a fractional macro (LFO), not just whole steps.
void test_decay_fractional() {
  float a[kFmMacros] = {64, 0, 0, 0, 0}, b[kFmMacros] = {64.5f, 0, 0, 0, 0};
  DrumParams pa, pb;
  drumMachine(static_cast<uint8_t>(DrumMachine::Bd8), a, 60, pa);
  drumMachine(static_cast<uint8_t>(DrumMachine::Bd8), b, 60, pb);
  TEST_ASSERT_TRUE(pb.toneMs > pa.toneMs);
}

static float clapRms(uint8_t machine, float freq, float q, float& peak) {
  static float b[kSynthRate / 10];
  float mac[kFmMacros] = {64, freq, 64, q, 64};
  DrumParams p;
  drumMachine(machine, mac, 60, p);
  DrumVoice d;
  d.trigger(false);
  memset(b, 0, sizeof b);
  for (int pos = 0; pos < kSynthRate / 10; pos += 32) {
    d.control(p, 32);
    d.render(b + pos, 32, 1.f);
  }
  float a = 0;
  for (float x : b) {
    a += x * x;
    peak = fmaxf(peak, fabsf(x));
  }
  return sqrtf(a / (kSynthRate / 10));
}

// The band-passed clap noise keeps its level across FREQ and Q, bounded.
void test_clap_level_even() {
  const uint8_t ms[] = {static_cast<uint8_t>(DrumMachine::Cp8), static_cast<uint8_t>(DrumMachine::Cp9)};
  for (uint8_t m : ms) {
    float peak = 0, lo = 1e9f, hi = 0;
    const float edge[] = {0, 127};
    for (float f : edge)
      for (float q : edge) {
        const float r = clapRms(m, f, q, peak);
        lo = fminf(lo, r);
        hi = fmaxf(hi, r);
      }
    TEST_ASSERT_TRUE(hi < lo * 1.6f);  // within ~4 dB
    TEST_ASSERT_TRUE(peak <= 1.f);     // a noise peak may reach the soft clip's ceiling
  }
}

void test_groups_cover_every_machine_once() {
  int seen[static_cast<int>(DrumMachine::Count)] = {};
  TEST_ASSERT_EQUAL(8, drumGroupCount());
  for (int g = 0; g < drumGroupCount(); ++g) {
    TEST_ASSERT_EQUAL(2, drumModelCount(g));
    TEST_ASSERT_TRUE(strlen(drumGroupName(g)) <= 5);
    for (int k = 0; k < drumModelCount(g); ++k) {
      const uint8_t m = drumMachineAt(g, k);
      ++seen[m];
      TEST_ASSERT_EQUAL(g, drumGroupOf(m));
      TEST_ASSERT_EQUAL(k, drumModelOf(m));
      TEST_ASSERT_TRUE(strlen(drumModelName(g, k)) <= 5);
    }
  }
  for (int m = 0; m < static_cast<int>(DrumMachine::Count); ++m)
    TEST_ASSERT_EQUAL_MESSAGE(1, seen[m], drumMachineName(static_cast<uint8_t>(m)));
}

void test_group_names() {
  TEST_ASSERT_EQUAL_STRING("KICK", drumGroupName(0));
  TEST_ASSERT_EQUAL_STRING("909", drumModelName(0, 1));
  TEST_ASSERT_EQUAL(static_cast<int>(DrumMachine::Bd9), drumMachineAt(0, 1));
  const uint8_t cb8 = static_cast<uint8_t>(DrumMachine::Cb8);
  TEST_ASSERT_EQUAL_STRING("PERC", drumGroupName(drumGroupOf(cb8)));
  TEST_ASSERT_EQUAL_STRING("BELL", drumModelName(drumGroupOf(cb8), drumModelOf(cb8)));
  // Out of range: model 0 of group 0; unknown machines as BD8.
  TEST_ASSERT_EQUAL(static_cast<int>(DrumMachine::Bd8), drumMachineAt(99, 99));
  TEST_ASSERT_EQUAL_STRING("KICK", drumGroupName(99));
  TEST_ASSERT_EQUAL(0, drumGroupOf(200));
  TEST_ASSERT_EQUAL(0, drumModelOf(200));
  TEST_ASSERT_EQUAL(0, drumGroupOf(static_cast<uint8_t>(DrumMachine::Count)));
}

// ---- Kicks against the real machines (docs/plans/2026-10-26-drum-calibration-design.md) ----

constexpr int kKickN = kSynthRate;  // 1 s
static float kb[kKickN];

// The machine's hit at amp 1, macros as given (-1 = the machine's default).
static void kickRender(DrumMachine mc, float dec, float col, float con) {
  Instrument in;
  instrSetType(in, InstrType::Drum);
  drumSetMachine(in, static_cast<uint8_t>(mc));
  float mac[kFmMacros];
  for (int k = 0; k < kFmMacros; ++k) mac[k] = in.macro[k];
  if (dec >= 0) mac[kMacDec] = dec;
  if (col >= 0) mac[kMacCol] = col;
  if (con >= 0) mac[kMacCon] = con;
  DrumParams p;
  drumMachine(static_cast<uint8_t>(mc), mac, 60, p);
  DrumVoice d;
  d.trigger(false);
  memset(kb, 0, sizeof kb);
  for (int pos = 0; pos < kKickN; pos += 32) {
    d.control(p, 32);
    d.render(kb + pos, 32, 1.f);
  }
}

static float peakOf() {
  float pk = 0;
  for (float x : kb) pk = fmaxf(pk, fabsf(x));
  return pk;
}

// Pitch at t ms: full periods between every other zero crossing (both ways, as the analysis
// scripts), each at its centre, linear between.
static float hzAt(float ms) {
  const float t = ms * kSynthRate / 1000.f;
  float z0 = -1, z1 = -1, pc = -1, pf = 0;
  for (int i = 1; i < kKickN; ++i) {
    if ((kb[i - 1] < 0) == (kb[i] < 0)) continue;
    const float z = i - 1 + kb[i - 1] / (kb[i - 1] - kb[i]);
    if (z0 >= 0) {
      const float c = 0.5f * (z0 + z), f = kSynthRate / (z - z0);
      if (c >= t) return pc < 0 ? f : pf + (f - pf) * (t - pc) / (c - pc);
      pc = c, pf = f;
    }
    z0 = z1, z1 = z;
  }
  return 0;
}

// Level (dB re the peak) of the largest |x| in a 20 ms window at ms.
static float dbAt(float ms) {
  const int a = static_cast<int>((ms - 10) * kSynthRate / 1000), b = static_cast<int>((ms + 10) * kSynthRate / 1000);
  float m = 0;
  for (int i = a; i < b; ++i) m = fmaxf(m, fabsf(kb[i]));
  return 20.f * log10f(m / peakOf());
}

// Decay time constant, ms, from the 20 ms window peaks between -6 and -40 dB re the peak.
static float tauMs() {
  float st = 0, sd = 0, stt = 0, std2 = 0;
  int n = 0;
  bool after = false;
  for (int ms = 10; ms < 990; ms += 5) {
    const float d = dbAt(static_cast<float>(ms));
    if (d < -6) after = true;
    if (!after || d < -40) continue;
    if (d > -6) continue;
    st += ms, sd += d, stt += ms * ms, std2 += ms * d, ++n;
  }
  const float slope = (n * std2 - st * sd) / (n * stt - st * st);  // dB / ms
  return -8.6859f / slope;
}

// Level of harmonic h re the fundamental at hz, Hann window of 4 periods centred at ms.
static float harmDb(float ms, float hz, int h) {
  const int n = static_cast<int>(4 * kSynthRate / hz), c = static_cast<int>(ms * kSynthRate / 1000);
  auto mag = [&](float f) {
    double re = 0, im = 0;
    for (int i = 0; i < n; ++i) {
      const double w = 0.5 - 0.5 * cos(2 * M_PI * i / (n - 1)), x = kb[c - n / 2 + i] * w;
      re += x * cos(2 * M_PI * f * i / kSynthRate);
      im -= x * sin(2 * M_PI * f * i / kSynthRate);
    }
    return sqrt(re * re + im * im);
  };
  return static_cast<float>(20 * log10(mag(h * hz) / mag(hz)));
}

static void within(float want, float got, float rel, const char* what) {
  char m[96];
  snprintf(m, sizeof m, "%s: want %.1f got %.1f", what, want, got);
  TEST_ASSERT_TRUE_MESSAGE(fabsf(got - want) <= rel * fabsf(want), m);
}
static void withinDb(float want, float got, const char* what) {
  char m[96];
  snprintf(m, sizeof m, "%s: want %.1f dB got %.1f", what, want, got);
  TEST_ASSERT_TRUE_MESSAGE(fabsf(got - want) <= 1.5f, m);
}

// BD9 sweep: one exponential from ~250 Hz, TIME = Tune 0 / 3 / 7 / 10 (sample medians at 10 / 20 / 40 ms).
void test_bd9_sweep() {
  const float want[4][3] = {{117, 71, 51}, {139, 86, 55}, {175, 121, 74}, {200, 157, 104}};
  const int tune[4] = {0, 3, 7, 10};
  for (int k = 0; k < 4; ++k) {
    kickRender(DrumMachine::Bd9, 127, 0, tune[k] * 12.7f);
    within(want[k][0], hzAt(10), 0.1f, "BD9 hz@10");
    within(want[k][1], hzAt(20), 0.1f, "BD9 hz@20");
    within(want[k][2], hzAt(40), 0.1f, "BD9 hz@40");
    within(49.5f, hzAt(300), 0.05f, "BD9 base");
  }
}

// BD9 decay tau 15 / 30 / 58 / 96 ms at DECAY 0 / 3 / 7 / 10; level after the step; h3 / h5; peak.
void test_bd9_decay_level_harmonics() {
  const float want[4] = {15, 30, 58, 96};
  const int dec[4] = {0, 3, 7, 10};
  for (int k = 0; k < 4; ++k) {
    kickRender(DrumMachine::Bd9, dec[k] * 12.7f, -1, 0);
    within(want[k], tauMs(), 0.1f, "BD9 tau");
  }
  kickRender(DrumMachine::Bd9, 127, 0, 0);  // Tune 0, Decay 10
  withinDb(-5.4f, dbAt(50), "BD9 T0 D10 @50");
  withinDb(-8.5f, dbAt(100), "BD9 T0 D10 @100");
  withinDb(-17.f, dbAt(200), "BD9 T0 D10 @200");
  withinDb(-20.f, harmDb(150, hzAt(150), 3), "BD9 h3");
  withinDb(-34.f, harmDb(150, hzAt(150), 5), "BD9 h5");
  kickRender(DrumMachine::Bd9, -1, -1, -1);
  withinDb(-1.f, 20.f * log10f(peakOf()), "BD9 peak");
  // The click step ahead of the body: -16 dB, ~1.3 ms (ATTACK 0: little noise on it).
  kickRender(DrumMachine::Bd9, -1, 0, -1);
  withinDb(-16.f, 20.f * log10f(fabsf(kb[20]) / peakOf()), "BD9 click step");
  TEST_ASSERT_FLOAT_WITHIN(0.02f, kb[20], kb[40]);  // flat but for the noise
}

// BD8 at default: pitch 45 + 215 e^-t/5 + 22 e^-t/90 Hz (sample medians), long decay, near-sine, peak.
void test_bd8_targets() {
  kickRender(DrumMachine::Bd8, -1, -1, -1);
  within(92, hzAt(10), 0.1f, "BD8 hz@10");
  within(67, hzAt(20), 0.1f, "BD8 hz@20");
  within(58, hzAt(40), 0.1f, "BD8 hz@40");
  within(55, hzAt(80), 0.1f, "BD8 hz@80");
  within(45.5f, hzAt(400), 0.05f, "BD8 base");
  const float t = tauMs();
  TEST_ASSERT_TRUE_MESSAGE(t > 130 && t < 280, "BD8 default tau in the long range");
  TEST_ASSERT_TRUE_MESSAGE(harmDb(150, hzAt(150), 3) < -20.f, "BD8 near-sine");
  withinDb(-1.f, 20.f * log10f(peakOf()), "BD8 peak");
  // Hold: within 1 dB of the peak for ~25 .. 60 ms.
  TEST_ASSERT_TRUE(dbAt(40) > -1.5f);
}

// BD8 DECAY: short tau 20 .. 40 ms, long 130 .. 280 ms; both to -60 dB within 0.15 .. 1.8 s.
void test_bd8_decay_range() {
  kickRender(DrumMachine::Bd8, 0, -1, -1);
  float t = tauMs();
  TEST_ASSERT_TRUE_MESSAGE(t > 20 && t < 40, "BD8 short tau");
  kickRender(DrumMachine::Bd8, 127, -1, -1);
  t = tauMs();
  TEST_ASSERT_TRUE_MESSAGE(t > 130 && t < 280, "BD8 long tau");
}

// ---- Snares against the real machines (same design doc) ----

// The machine's hit into kb at amp 1; mac[k] < 0 = the machine's default for that macro.
// f1Only: F1 alone (no other tones, click or noise), to read its sweep. The noise as after `seed`
// triggers; the note sets the pitch.
static void drumRender(DrumMachine mc, const float (&over)[kFmMacros], bool f1Only = false, int seed = 0,
                       float note = 60) {
  Instrument in;
  instrSetType(in, InstrType::Drum);
  drumSetMachine(in, static_cast<uint8_t>(mc));
  float mac[kFmMacros];
  for (int k = 0; k < kFmMacros; ++k) mac[k] = over[k] >= 0 ? over[k] : in.macro[k];
  DrumParams p;
  drumMachine(static_cast<uint8_t>(mc), mac, note, p);
  if (f1Only) p.toneLvl[1] = p.toneLvl[2] = p.click = p.noiseLvl = 0;
  drumResetNoise();
  for (int k = 0; k < seed; ++k) DrumVoice().trigger(false);
  DrumVoice d;
  d.trigger(false);
  memset(kb, 0, sizeof kb);
  for (int pos = 0; pos < kKickN; pos += 32) {
    d.control(p, 32);
    d.render(kb + pos, 32, 1.f);
  }
}

// Butterworth biquad (RBJ, q 0.707), in place over x.
static void biquad(float* x, int n, float hz, bool hp) {
  const double w = 2 * M_PI * hz / kSynthRate, al = sin(w) / (2 * 0.70710678), c = cos(w), a0 = 1 + al;
  const double b0 = (hp ? (1 + c) : (1 - c)) / 2 / a0, b1 = (hp ? -(1 + c) : (1 - c)) / a0, b2 = b0;
  const double a1 = -2 * c / a0, a2 = (1 - al) / a0;
  double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  for (int i = 0; i < n; ++i) {
    const double y = b0 * x[i] + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1, x1 = x[i], y2 = y1, y1 = y;
    x[i] = static_cast<float>(y);
  }
}
static void filt(float* x, int n, float hz, bool hp, int stages) {
  for (int k = 0; k < stages; ++k) biquad(x, n, hz, hp);
}

// Power at hz (Hann window) over ms0 .. ms1 of x.
static double powerAt(const float* x, float ms0, float ms1, float hz) {
  const int a = static_cast<int>(ms0 * kSynthRate / 1000), n = static_cast<int>((ms1 - ms0) * kSynthRate / 1000);
  double re = 0, im = 0;
  for (int i = 0; i < n; ++i) {
    const double w = 0.5 - 0.5 * cos(2 * M_PI * i / (n - 1)), v = x[a + i] * w;
    re += v * cos(2 * M_PI * hz * i / kSynthRate);
    im -= v * sin(2 * M_PI * hz * i / kSynthRate);
  }
  return re * re + im * im;
}

// The strongest frequency in lo .. hi (1 Hz steps) over ms0 .. ms1.
static float peakHz(float ms0, float ms1, float lo, float hi) {
  float best = lo;
  double bp = -1;
  for (float f = lo; f <= hi; f += 1) {
    const double p = powerAt(kb, ms0, ms1, f);
    if (p > bp) bp = p, best = f;
  }
  return best;
}

static float db10(double p) { return static_cast<float>(10 * log10(p > 1e-30 ? p : 1e-30)); }

// Noise band levels (dB) at hz[0 .. n), n <= 8: mean power over 17 points across a third of an octave, 5 ..
// 80 ms in 10 ms frames, 8 hits (each its own noise).
static void bandsDb(DrumMachine mc, const float (&over)[kFmMacros], const float* hz, float* out, int n) {
  double p[8] = {};
  for (int hit = 0; hit < 8; ++hit) {
    drumRender(mc, over, false, hit);
    for (int b = 0; b < n && b < 8; ++b)
      for (float ms = 5; ms < 80; ms += 10)
        for (int k = -8; k <= 8; ++k) p[b] += powerAt(kb, ms, ms + 10, hz[b] * powf(2.f, k / 108.f));
  }
  for (int b = 0; b < n && b < 8; ++b) out[b] = db10(p[b]);
}

// Tone (below 1 kHz) re noise (above) in energy over the first 50 ms, dB (as the analysis' bal1k).
static float toneOverNoiseDb() {
  constexpr int n = kSynthRate / 20;
  static float lo[n], hi[n];
  memcpy(lo, kb, sizeof lo);
  memcpy(hi, kb, sizeof hi);
  filt(lo, n, 1000, false, 4);
  filt(hi, n, 1000, true, 4);
  double el = 0, eh = 0;
  for (int i = 0; i < n; ++i) el += lo[i] * lo[i], eh += hi[i] * hi[i];
  return db10(el / eh);
}

// Noise above 1 kHz: ms where it stays `below` dB under its 10 .. 40 ms plateau (5 ms frames of the
// power over 8 hits, each its own noise).
static float noiseDropMs(DrumMachine mc, const float (&over)[kFmMacros], float below) {
  constexpr int w = kSynthRate / 200, frames = kKickN / w;
  static float h[kKickN];
  static float e[frames];
  static double acc[frames];
  memset(acc, 0, sizeof acc);
  for (int hit = 0; hit < 8; ++hit) {
    drumRender(mc, over, false, hit);
    memcpy(h, kb, sizeof h);
    filt(h, kKickN, 1000, true, 4);
    for (int f = 0; f < frames; ++f)
      for (int i = 0; i < w; ++i) acc[f] += h[f * w + i] * h[f * w + i];
  }
  for (int f = 0; f < frames; ++f) e[f] = db10(acc[f]);
  float pl[6];
  memcpy(pl, e + 2, sizeof pl);
  for (int i = 0; i < 6; ++i)
    for (int j = i + 1; j < 6; ++j)
      if (pl[j] < pl[i]) {
        const float t = pl[i];
        pl[i] = pl[j], pl[j] = t;
      }
  const float plateau = 0.5f * (pl[2] + pl[3]);
  for (int f = 2; f + 2 < frames; ++f)
    if (e[f] < plateau - below && e[f + 1] < plateau - below && e[f + 2] < plateau - below) return f * 5.f;
  return 0;
}

// Cycles of kb from 0 to ms: zero crossings (either way) are half cycles, linear between.
static float cyclesAt(float ms) {
  const float t = ms * kSynthRate / 1000.f;
  int count = 0;
  float prev = 0;
  for (int i = 1; i < kKickN; ++i) {
    if ((kb[i - 1] < 0) == (kb[i] < 0)) continue;
    const float z = i - 1 + kb[i - 1] / (kb[i - 1] - kb[i]);
    if (z >= t) return 0.5f * (count - 1 + (t - prev) / (z - prev));
    prev = z, ++count;
  }
  return 0;
}
static float meanHz(float ms0, float ms1) { return (cyclesAt(ms1) - cyclesAt(ms0)) * 1000.f / (ms1 - ms0); }

// SD8 tones: F1 165 Hz tau 30 ms, F2 ~1.95 x F1 tau 9 ms, no sweep; TONE: F2 -20 .. 0 dB.
void test_sd8_tones() {
  const float n = -1;
  drumRender(DrumMachine::Sd8, {n, 0, 0, n, n});  // TONE 0, SNAPPY 0
  static float t[kKickN];
  memcpy(t, kb, sizeof t);
  filt(kb, kKickN, 250, false, 2);
  within(165, hzAt(15), 0.1f, "SD8 F1 @15");
  within(165, hzAt(80), 0.1f, "SD8 F1 @80");
  within(30, tauMs(), 0.1f, "SD8 F1 tau");
  memcpy(kb, t, sizeof t);
  within(322, peakHz(3, 43, 260, 400), 0.1f, "SD8 F2");
  const float tone[3] = {0, 50, 127}, want[3] = {-20, -7.5f, 0};
  for (int k = 0; k < 3; ++k) {
    drumRender(DrumMachine::Sd8, {n, tone[k], 0, n, n});
    const float f1 = peakHz(3, 43, 140, 190), f2 = peakHz(3, 43, 280, 380);
    withinDb(want[k], db10(powerAt(kb, 3, 43, f2) / powerAt(kb, 3, 43, f1)), "SD8 F2 re F1");
  }
  drumRender(DrumMachine::Sd8, {n, 127, 0, n, n});  // F2 loud: its decay above 280 Hz
  filt(kb, kKickN, 280, true, 3);
  // tauMs() reads -6 .. -40 dB re the peak; F1 is ~30 dB under F2 here, so its tail stays below.
  within(9, tauMs(), 0.15f, "SD8 F2 tau");
}

// SD8 noise: band ~2 .. 4 kHz around 2.8 kHz, tau ~43 ms; tone ~ noise in energy; peak -1 dBFS.
void test_sd8_noise_balance_peak() {
  const float n = -1;
  drumRender(DrumMachine::Sd8, {n, n, n, n, n});
  withinDb(-1.f, 20.f * log10f(peakOf()), "SD8 peak");
  withinDb(1.8f, toneOverNoiseDb(), "SD8 tone re noise");
  static const float hz8[] = {1000, 2000, 2800, 4000, 8000};
  float b8[5];
  bandsDb(DrumMachine::Sd8, {n, 0, 127, n, n}, hz8, b8, 5);  // noise to the fore
  TEST_ASSERT_TRUE_MESSAGE(b8[1] > b8[2] - 4.5f && b8[3] > b8[2] - 4.5f, "SD8 noise -3 dB band");
  TEST_ASSERT_TRUE_MESSAGE(b8[0] < b8[2] - 6 && b8[4] < b8[2] - 6, "SD8 noise band edges");
  drumRender(DrumMachine::Sd8, {n, 0, 127, 0, n});  // and the tone short
  filt(kb, kKickN, 1500, true, 4);
  within(43, tauMs(), 0.1f, "SD8 noise tau");
}

// SD9 tones: TUNE 0 / 3 / 7 / 10 = F1 120 / 146 / 205 / 233 Hz; F2 = 1.455 x F1 at -7 .. -10 dB;
// one tau ~28 ms; the sweep +260 Hz, tau 7 ms.
void test_sd9_tones() {
  const float n = -1;
  const float tune[4] = {120, 146, 205, 233}, f2db[4] = {-7.2f, -7.3f, -9.7f, -10.4f};
  const int knob[4] = {0, 3, 7, 10};
  for (int k = 0; k < 4; ++k) {
    drumRender(DrumMachine::Sd9, {n, knob[k] * 12.7f, 0, n, n});
    const float f1 = peakHz(25, 75, 0.8f * tune[k], 1.2f * tune[k]);
    const float f2 = peakHz(25, 75, 1.3f * f1, 1.6f * f1);
    within(tune[k], f1, 0.1f, "SD9 F1");
    within(1.455f, f2 / f1, 0.03f, "SD9 F2 / F1");
    withinDb(f2db[k], db10(powerAt(kb, 25, 75, f2) / powerAt(kb, 25, 75, f1)), "SD9 F2 re F1");
    within(28, tauMs(), 0.1f, "SD9 tone tau");
    // The sweep as mean pitch over windows (cycles counted between zero crossings).
    drumRender(DrumMachine::Sd9, {n, knob[k] * 12.7f, 0, n, n}, true);
    const float w[4] = {2, 6, 12, 24};
    for (int j = 0; j < 3; ++j) {
      const float want = tune[k] + 260 * 7 * (expf(-w[j] / 7) - expf(-w[j + 1] / 7)) / (w[j + 1] - w[j]);
      within(want, meanHz(w[j], w[j + 1]), 0.1f, "SD9 sweep");
    }
  }
}

// SD9 noise: SNAPPY 3 / 7 / 10 = tone re noise 15.9 / 7.8 / 5.8 dB (the samples' medians); N.DEC (the
// 909's Tone) 0 / 3 / 7 / 10 = -3 dB at 65 / 90 / 122 / 130 ms after a plateau; tilted band; peak.
void test_sd9_noise() {
  const float n = -1;
  const float snap[3] = {3, 7, 10}, bal[3] = {15.9f, 7.8f, 5.8f};
  for (int k = 0; k < 3; ++k) {
    drumRender(DrumMachine::Sd9, {n, 89, snap[k] * 12.7f, n, n});
    withinDb(bal[k], toneOverNoiseDb(), "SD9 tone re noise");
  }
  const float t3[4] = {65, 90, 122, 130};
  const int knob[4] = {0, 3, 7, 10};
  for (int k = 0; k < 4; ++k) {
    const float m[kFmMacros] = {n, 89, 89, n, knob[k] * 12.7f};
    const float a = noiseDropMs(DrumMachine::Sd9, m, 3), b = noiseDropMs(DrumMachine::Sd9, m, 10);
    within(t3[k], a, 0.1f, "SD9 noise -3 dB");
    TEST_ASSERT_TRUE_MESSAGE(b - a < a - 10, "SD9 noise: a plateau, then a fall that speeds up");
  }
  static const float hz9[] = {1000, 2000, 4000, 6400, 8000, 10000, 14000};
  float b9[7];
  bandsDb(DrumMachine::Sd9, {n, 89, 127, n, n}, hz9, b9, 7);
  const float pk = fmaxf(b9[3], b9[4]);
  TEST_ASSERT_TRUE_MESSAGE(b9[2] > pk - 4.5f && b9[5] > pk - 4.5f, "SD9 noise -3 dB band");
  TEST_ASSERT_TRUE_MESSAGE(b9[1] < pk - 3 && b9[0] < pk - 6 && b9[6] < pk - 6, "SD9 noise tilt");
  drumRender(DrumMachine::Sd9, {n, n, n, n, n});
  withinDb(-1.f, 20.f * log10f(peakOf()), "SD9 peak");
}

// ---- Toms against the real machines (same design doc) ----

// The note of a drum at hz, re 60 at hz0.
static float noteOf(float hz, float hz0) { return 60.f + 12.f * log2f(hz / hz0); }

// Time of the n-th zero crossing (1 = the first after the start), ms.
static float crossingMs(int n) {
  for (int i = 1, c = 0; i < kKickN; ++i) {
    if ((kb[i - 1] < 0) == (kb[i] < 0)) continue;
    if (++c == n) return (i - 1 + kb[i - 1] / (kb[i - 1] - kb[i])) * 1000.f / kSynthRate;
  }
  return 0;
}

// When a sine from phase 0 at hz (1 + a e^(-t / tau)) has made `cycles` cycles, ms.
static float blipCrossMs(float hz, float a, float tau, float cycles) {
  float t = 0;
  for (int k = 0; k < 60; ++k) {  // Newton
    const float e = expf(-t / tau), c = hz * (t + a * tau * (1 - e)) / 1000.f - cycles;
    t -= c / (hz * (1 + a * e) / 1000.f);
  }
  return t;
}

// TOM8: LT 86 / MT 127 / HT 171, congas 191 / 264 / 367 Hz; a sine with a first-cycle blip of +9.5
// semitones, tau ~0.29 cycles (LT 3.4 .. HC 0.8 ms); decay tau LT 83, MT 62, HT 59, MC 45, HC 42 ms (LC
// 84 on the samples: one curve over the note can't have it, ours ~53). No noise; peak -1 dBFS.
void test_tom8_targets() {
  const float n = -1;
  const float hz[6] = {86, 127, 171, 191, 264, 367}, tau[6] = {83, 62, 59, 0, 45, 42};
  for (int k = 0; k < 6; ++k) {
    drumRender(DrumMachine::Tom8, {n, n, n, n, n}, false, 0, noteOf(hz[k], 127));
    within(hz[k], meanHz(150, 300), 0.02f, "TOM8 hz");
    const float a = powf(2.f, 9.5f / 12) - 1, tb = 290.f / hz[k];
    within(blipCrossMs(hz[k], a, tb, 0.5f), crossingMs(1), 0.1f, "TOM8 blip: first crossing");
    within(blipCrossMs(hz[k], a, tb, 1.f), crossingMs(2), 0.1f, "TOM8 blip: second crossing");
    TEST_ASSERT_TRUE_MESSAGE(crossingMs(1) < 0.8f * 500.f / hz[k], "TOM8 blip: shorter first half-cycle");
    if (tau[k] > 0) within(tau[k], tauMs(), 0.1f, "TOM8 tau");
    TEST_ASSERT_TRUE_MESSAGE(harmDb(100, hz[k], 3) < -30.f, "TOM8 sine");
  }
  drumRender(DrumMachine::Tom8, {n, n, n, n, n});
  withinDb(-1.f, 20.f * log10f(peakOf()), "TOM8 peak");
  drumRender(DrumMachine::Tom8, {n, n, n, 0, n});
  TEST_ASSERT_TRUE_MESSAGE(crossingMs(1) > 0.97f * 500.f / 127, "TOM8 SWEEP 0: no blip");
}

// kb band-passed around the main tone of TOM9 at hz: its partials (x 0.64, x 1.78) out.
static void tom9Main(float hz) {
  filt(kb, kKickN, 1.6f * hz, false, 2);
  filt(kb, kKickN, 0.8f * hz, true, 4);
}

// One tone of TOM9 alone (the others, click and noise off) into kb, macros as given (-1 = default).
static void tom9Tone(int tone, const float (&over)[kFmMacros]) {
  Instrument in;
  instrSetType(in, InstrType::Drum);
  drumSetMachine(in, static_cast<uint8_t>(DrumMachine::Tom9));
  float mac[kFmMacros];
  for (int k = 0; k < kFmMacros; ++k) mac[k] = over[k] >= 0 ? over[k] : in.macro[k];
  DrumParams p;
  drumMachine(static_cast<uint8_t>(DrumMachine::Tom9), mac, 60, p);
  for (int k = 0; k < kDrumTones; ++k)
    if (k != tone) p.toneLvl[k] = 0;
  p.click = p.noiseLvl = 0;
  DrumVoice d;
  d.trigger(false);
  memset(kb, 0, sizeof kb);
  for (int pos = 0; pos < kKickN; pos += 32) {
    d.control(p, 32);
    d.render(kb + pos, 32, 1.f);
  }
}

// The -60 dB time re the peak of the 20 ms window peaks, ms.
static float t60Ms() {
  for (int ms = 20; ms < 990; ms += 5)
    if (dbAt(static_cast<float>(ms)) < -60) return static_cast<float>(ms);
  return 1000;
}

// TOM9 base and sweep: TUNE 0 / 3 / 7 / 10 = MT 81 / 97 / 131 / 152 Hz (LT 63 .. 122, HT 94 .. 173 by the
// note); +5 semitones decaying in tau ~40 ms, both partials along.
void test_tom9_pitch() {
  const float n = -1;
  const float tune[4] = {81, 97, 131, 152};
  const int knob[4] = {0, 3, 7, 10};
  for (int k = 0; k < 4; ++k) {
    drumRender(DrumMachine::Tom9, {n, knob[k] * 12.7f, n, n, n});
    within(tune[k], peakHz(250, 450, 0.8f * tune[k], 1.2f * tune[k]), 0.03f, "TOM9 MT base");
  }
  drumRender(DrumMachine::Tom9, {n, 0, n, n, n}, false, 0, noteOf(63, 81));
  within(63, peakHz(250, 450, 50, 80), 0.05f, "TOM9 LT tune 0");
  drumRender(DrumMachine::Tom9, {n, 127, n, n, n}, false, 0, noteOf(94, 81));
  within(173, peakHz(250, 450, 140, 210), 0.05f, "TOM9 HT tune 10");
  // The sweep as mean pitch over windows, against +5 semitones, tau 40 ms.
  for (int k = 0; k < 4; ++k) {
    drumRender(DrumMachine::Tom9, {n, knob[k] * 12.7f, n, n, n}, true);
    const float w[5] = {5, 15, 30, 60, 120}, a = powf(2.f, 5.f / 12) - 1;
    for (int j = 0; j < 4; ++j) {
      const float want = tune[k] * (1 + a * 40 * (expf(-w[j] / 40) - expf(-w[j + 1] / 40)) / (w[j + 1] - w[j]));
      within(want, meanHz(w[j], w[j + 1]), 0.03f, "TOM9 sweep");
    }
  }
}

// TOM9 body: tau 47 / 58 / 76 / 82 ms at DECAY 0 / 3 / 7 / 10 (MT / HT), LT 75 .. 140; t60 MT / HT
// 357 / 428 / 541 / 568 ms (the samples' medians); h3 -14, h5 -28 dB; peak -1 dBFS.
void test_tom9_body() {
  const float n = -1;
  const float tau[4] = {47, 58, 76, 82}, t60[4] = {357, 428, 541, 568}, ltTau[4] = {75, 88, 123, 138};
  const int knob[4] = {0, 3, 7, 10};
  for (int k = 0; k < 4; ++k) {
    drumRender(DrumMachine::Tom9, {knob[k] * 12.7f, 38, n, n, n});
    const float f = peakHz(150, 300, 80, 140);
    tom9Main(f);
    within(tau[k], tauMs(), 0.1f, "TOM9 MT tau");
    within(t60[k], t60Ms(), 0.1f, "TOM9 MT t60");
    drumRender(DrumMachine::Tom9, {knob[k] * 12.7f, 38, n, n, n}, false, 0, noteOf(63, 81));
    tom9Main(peakHz(150, 300, 60, 110));
    within(ltTau[k], tauMs(), 0.1f, "TOM9 LT tau");
  }
  drumRender(DrumMachine::Tom9, {n, n, n, n, n}, true);
  const float f = hzAt(150);
  withinDb(-14.f, harmDb(150, f, 3), "TOM9 h3");
  withinDb(-28.f, harmDb(150, f, 5), "TOM9 h5");
  drumRender(DrumMachine::Tom9, {n, n, n, n, n});
  withinDb(-1.f, 20.f * log10f(peakOf()), "TOM9 peak");
}

// TOM9 partials: lower x 0.60 .. 0.69 at -8 .. -6 dB, held at high DECAY, fading at low (tau ~25 ms);
// upper x 1.75 .. 1.8 at -3 .. -9 dB early, tau 20 .. 25 ms; both ride the sweep.
void test_tom9_partials() {
  const float n = -1, d7 = 7 * 12.7f;
  static float main0[kKickN];
  tom9Tone(0, {d7, n, n, n, n});
  memcpy(main0, kb, sizeof main0);
  const float m0 = meanHz(5, 15), m1 = meanHz(150, 250);
  double e0 = 0;
  for (int i = kSynthRate * 5 / 1000; i < kSynthRate * 35 / 1000; ++i) e0 += kb[i] * kb[i];
  // Lower: the ratio, along the sweep; its level re the main tone over 100 .. 200 ms.
  tom9Tone(1, {d7, n, n, n, n});
  within(0.645f, meanHz(150, 250) / m1, 0.07f, "TOM9 lower ratio");
  within(meanHz(150, 250) / m1, meanHz(5, 15) / m0, 0.01f, "TOM9 lower sweeps along");
  memcpy(kb, main0, sizeof kb);
  // Upper: the ratio, along the sweep; level over 5 .. 35 ms; its tau.
  tom9Tone(2, {d7, n, n, n, n});
  within(1.775f, meanHz(150, 250) / m1, 0.03f, "TOM9 upper ratio");
  within(meanHz(150, 250) / m1, meanHz(5, 15) / m0, 0.01f, "TOM9 upper sweeps along");
  double e2 = 0;
  for (int i = kSynthRate * 5 / 1000; i < kSynthRate * 35 / 1000; ++i) e2 += kb[i] * kb[i];
  const float hdb = db10(e2 / e0);
  TEST_ASSERT_TRUE_MESSAGE(hdb > -9 && hdb < -3, "TOM9 upper level");
  within(22.5f, tauMs(), 0.12f, "TOM9 upper tau");
  // In the full hit: the lower partial's level at DECAY 7 (held) and 0 (fading, tau ~25 ms).
  drumRender(DrumMachine::Tom9, {d7, n, n, n, n});
  float f = peakHz(100, 200, 90, 160), lo = peakHz(100, 200, 0.5f * f, 0.8f * f);
  withinDb(-6.5f, db10(powerAt(kb, 100, 200, lo) / powerAt(kb, 100, 200, f)), "TOM9 lower level D7");
  tom9Tone(1, {0, n, n, n, n});
  within(25, tauMs(), 0.15f, "TOM9 lower tau D0");
}

// TOM9 noise: a burst at ~-27 dB re the hit above 2 kHz over 5 .. 15 ms (8 hits), tau ~17 ms (the
// samples' 16 .. 18), low-passed at ~4 .. 6 kHz.
void test_tom9_noise() {
  const float n = -1;
  constexpr int a = kSynthRate * 5 / 1000, b = kSynthRate * 45 / 1000, w = kSynthRate * 5 / 1000;
  static float h[kKickN];
  double tot = 0, hf = 0, fr[8] = {};
  for (int hit = 0; hit < 8; ++hit) {
    drumRender(DrumMachine::Tom9, {n, n, n, n, n}, false, hit);
    memcpy(h, kb, sizeof h);
    filt(h, b + w, 2000, true, 4);
    for (int i = a; i < a + 2 * w; ++i) {  // Hann-weighted, as the analysis' spectrum
      const double hw = 0.5 - 0.5 * cos(2 * M_PI * (i - a) / (2 * w - 1));
      tot += hw * kb[i] * kb[i], hf += hw * h[i] * h[i];
    }
    for (int f = 0; f < 8; ++f)
      for (int i = a + f * w; i < a + (f + 1) * w; ++i) fr[f] += h[i] * h[i];
  }
  withinDb(-27.f, db10(hf / tot), "TOM9 noise level");
  // Amplitude tau from the 5 ms frames' power, 5 .. 45 ms (least squares on dB).
  double st = 0, sd = 0, stt = 0, std2 = 0;
  for (int f = 0; f < 8; ++f) {
    const double t = 7.5 + 5 * f, d = db10(fr[f]);
    st += t, sd += d, stt += t * t, std2 += t * d;
  }
  const double slope = (8 * std2 - st * sd) / (8 * stt - st * st);
  within(17, static_cast<float>(-4.3429 / slope), 0.1f, "TOM9 noise tau");
  static const float hz[] = {2000, 4000, 10000};
  float bd[3];
  bandsDb(DrumMachine::Tom9, {n, n, n, n, n}, hz, bd, 3);
  TEST_ASSERT_TRUE_MESSAGE(bd[1] > bd[0] - 4 && bd[2] < bd[1] - 8, "TOM9 noise low-passed at ~4 .. 6 kHz");
}


// ---- Claps, rims, claves and cowbell against the real machines (same design doc) ----

constexpr int kLongN = 2 * kSynthRate;  // 2 s
static float lb[kLongN];

// The machine's hit into lb (2 s) at amp 1; mac[k] < 0 = the machine's default; the noise as after
// `seed` triggers.
static void longRender(DrumMachine mc, const float (&over)[kFmMacros], int seed = 0) {
  Instrument in;
  instrSetType(in, InstrType::Drum);
  drumSetMachine(in, static_cast<uint8_t>(mc));
  float mac[kFmMacros];
  for (int k = 0; k < kFmMacros; ++k) mac[k] = over[k] >= 0 ? over[k] : in.macro[k];
  DrumParams p;
  drumMachine(static_cast<uint8_t>(mc), mac, 60, p);
  drumResetNoise();
  for (int k = 0; k < seed; ++k) DrumVoice().trigger(false);
  DrumVoice d;
  d.trigger(false);
  memset(lb, 0, sizeof lb);
  for (int pos = 0; pos < kLongN; pos += 32) {
    d.control(p, 32);
    d.render(lb + pos, 32, 1.f);
  }
}

// Amplitude tau, ms, from the least-squares slope of 10 log10(power) of frames (frameMs each) whose
// centres lie in a .. b ms.
static float fitTau(const double* fr, float frameMs, float a, float b) {
  double st = 0, sd = 0, stt = 0, std2 = 0;
  int n = 0;
  for (int k = static_cast<int>(a / frameMs); (k + 0.5f) * frameMs < b; ++k) {
    const double t = (k + 0.5) * frameMs, d = db10(fr[k]);
    st += t, sd += d, stt += t * t, std2 += t * d, ++n;
  }
  return static_cast<float>(-8.6859 * (n * stt - st * st) / (n * std2 - st * sd));
}

// Power of x in frames of frameMs from 0, added to fr[0 .. n).
static void addFrames(const float* x, float frameMs, double* fr, int n) {
  const int w = static_cast<int>(frameMs * kSynthRate / 1000);
  for (int f = 0; f < n; ++f)
    for (int i = 0; i < w; ++i) fr[f] += x[f * w + i] * x[f * w + i];
}

// A clap's measures, the noise averaged over 8 hits (mean power, as the samples' analysis).
struct ClapWant {
  DrumMachine mc;
  float hit[3];       // hit onsets, ms; the tail starts at t0
  float t0;
  float hitTau;       // the hits' mean decay tau, ms (each from its peak to the next hit, 0.5 ms frames)
  float hz;           // the hits' band-pass peak, Hz
  float hf;           // power above 3 kHz re all in the hits, dB (broadband)
  float tailLvl;      // the tail's first 10 ms re the hits' mean power, dB
  float early, late;  // tail tau over t0 + 10 .. 60 and t0 + 60 .. 400 ms (5 ms frames; 0 = not checked)
  float all;          // tail tau from t0 + 10 ms to -40 dB re its first 10 ms
  float at1s;         // the tail at 1 s re the hits' mean power, dB (0 = not checked)
};

static void clapCheck(const ClapWant& w) {
  const float n = -1;
  constexpr int k05 = 2 * 80, k5 = kLongN / (kSynthRate / 200);
  static double f05[k05], f5[k5], spec[201];
  static float h[kSynthRate / 20];
  memset(f05, 0, sizeof f05);
  memset(f5, 0, sizeof f5);
  memset(spec, 0, sizeof spec);
  double all = 0, hf = 0;
  float pk[8];
  const int nh = static_cast<int>(w.t0 * kSynthRate / 1000);
  for (int hit = 0; hit < 8; ++hit) {
    longRender(w.mc, {n, n, n, n, n}, hit);
    addFrames(lb, 0.5f, f05, k05);
    addFrames(lb, 5, f5, k5);
    for (int k = 0; k <= 200; ++k) spec[k] += powerAt(lb, 0, w.t0, 500 + 10.f * k);
    memcpy(h, lb, sizeof h);
    filt(h, kSynthRate / 20, 3000, true, 2);
    for (int i = 0; i < nh; ++i) all += lb[i] * lb[i], hf += h[i] * h[i];
    pk[hit] = 0;
    for (float x : lb) pk[hit] = fmaxf(pk[hit], fabsf(x));
  }
  const char* nm = drumMachineName(static_cast<uint8_t>(w.mc));
  char what[48];
  // Hits: a rise of > 10 dB at each onset (two 0.5 ms frames after it re two 1 .. 1.5 ms before).
  for (int k = 1; k < 3; ++k) {
    const int f = static_cast<int>(w.hit[k] / 0.5f);
    snprintf(what, sizeof what, "%s hit %d onset", nm, k + 1);
    TEST_ASSERT_TRUE_MESSAGE(db10((f05[f] + f05[f + 1]) / (f05[f - 3] + f05[f - 2])) > 10, what);
  }
  // Each hit's tau from its peak frame to the frame before the next onset; their mean.
  float tau = 0;
  for (int k = 0; k < 3; ++k) {
    const float end = k < 2 ? w.hit[k + 1] : w.t0;
    int p = static_cast<int>(w.hit[k] / 0.5f);
    for (int f = p; f < p + 6; ++f)
      if (f05[f] > f05[p]) p = f;
    tau += fitTau(f05, 0.5f, p * 0.5f, end - 0.5f) * (1.f / 3);
  }
  snprintf(what, sizeof what, "%s hit tau", nm);
  within(w.hitTau, tau, 0.1f, what);
  int best = 0;
  for (int k = 0; k <= 200; ++k)
    if (spec[k] > spec[best]) best = k;
  snprintf(what, sizeof what, "%s hits band-pass", nm);
  within(w.hz, 500 + 10.f * best, 0.1f, what);
  snprintf(what, sizeof what, "%s hits broadband", nm);
  withinDb(w.hf, db10(hf / all), what);
  // Tail.
  double hits = 0;
  for (int f = 0; f < static_cast<int>(w.t0 / 0.5f); ++f) hits += f05[f];
  hits /= static_cast<int>(w.t0 / 0.5f) * (kSynthRate / 2000);  // mean power per sample
  const int t0 = static_cast<int>(w.t0 / 5);
  const double per5 = kSynthRate / 200;
  snprintf(what, sizeof what, "%s tail level", nm);
  withinDb(w.tailLvl, db10((f5[t0] + f5[t0 + 1]) / (2 * per5) / hits), what);
  if (w.early != 0) {
    snprintf(what, sizeof what, "%s tail early tau", nm);
    within(w.early, fitTau(f5, 5, w.t0 + 10, w.t0 + 60), 0.1f, what);
    snprintf(what, sizeof what, "%s tail late tau", nm);
    within(w.late, fitTau(f5, 5, w.t0 + 60, w.t0 + 400), 0.1f, what);
  }
  int end = t0 + 2;
  while (end < k5 - 1 && f5[end] > 0.5 * (f5[t0] + f5[t0 + 1]) * 1e-4) ++end;
  snprintf(what, sizeof what, "%s tail tau", nm);
  within(w.all, fitTau(f5, 5, w.t0 + 10, end * 5.f), 0.1f, what);
  if (w.at1s != 0) {
    snprintf(what, sizeof what, "%s tail at 1 s", nm);
    withinDb(w.at1s, db10(f5[static_cast<int>((w.t0 + 1000) / 5)] / per5 / hits), what);
  }
  // Peak: the median hit at -1 dBFS.
  for (int i = 0; i < 8; ++i)
    for (int j = i + 1; j < 8; ++j)
      if (pk[j] < pk[i]) {
        const float t = pk[i];
        pk[i] = pk[j], pk[j] = t;
      }
  snprintf(what, sizeof what, "%s peak", nm);
  withinDb(-1.f, 20.f * log10f(0.5f * (pk[3] + pk[4])), what);
}

// CP8: hits at 0 / 8.5 / 19 ms (the samples' onsets), tau 3.5 .. 4 ms, band-passed at ~1.1 kHz with
// broadband alongside (-6.4 dB above 3 kHz, the samples'); the tail from 28.5 ms, +4.7 dB re the hits'
// mean power, tau ~50 ms early, ~140 later, 182 to -40 dB (the samples'), -44.5 dB at 1 s (the samples
// are gated at ~1.2 s, so their t60 of 1.2 s isn't the machine's).
// At 32 kHz the same params' hits read tau 4.25 (3.75 at 44.1 kHz: 1 ms control blocks, rate).
void test_cp8_targets() {
  clapCheck({DrumMachine::Cp8, {0, 8.5f, 19}, 28.5f, 4.25f, 1100, -6.4f, 4.7f, 50, 140, 182, -44.5f});
}

// CP9: hits at 0 / 11 / 23.5 ms, tau ~3.3 ms, ~1.0 kHz; the tail from 32.5 ms, +1.7 dB re the hits'
// mean power (the samples'), tau ~90 ms to -40 dB (the samples end at ~0.4 s).
void test_cp9_targets() {
  clapCheck({DrumMachine::Cp9, {0, 11, 23.5f}, 32.5f, 3.3f, 1000, -6.4f, 1.7f, 0, 0, 90, 0});
}

// Level (dB) at hz over a Hann window of ms0 .. ms1 of kb re that at hz0 over ms2 .. ms3.
static float relDb(float hz, float ms0, float ms1, float hz0, float ms2, float ms3) {
  return db10(powerAt(kb, ms0, ms1, hz) / powerAt(kb, ms2, ms3, hz0));
}

// Amplitude tau of the component at hz: 2 ms Hann windows every 0.5 ms from ms0 to ms1.
static float trackTau(float hz, float ms0, float ms1) {
  double st = 0, sd = 0, stt = 0, std2 = 0;
  int n = 0;
  for (float t = ms0; t < ms1; t += 0.5f) {
    const double c = t + 1, d = db10(powerAt(kb, t, t + 2, hz));
    st += c, sd += d, stt += c * c, std2 += c * d, ++n;
  }
  return static_cast<float>(-8.6859 * (n * stt - st * st) / (n * std2 - st * sd));
}

// Peak of a 0.25 ms rms envelope above hz (4th-order high-pass) re that of kb, dB (the analysis' click).
static float clickDb(float hz) {
  constexpr int n = kSynthRate / 10, w = kSynthRate / 4000;
  static float h[n];
  memcpy(h, kb, sizeof h);
  filt(h, n, hz, true, 2);
  double ph = 0, pt = 0;
  for (int f = 0; f + w <= n; f += w) {
    double a = 0, b = 0;
    for (int i = f; i < f + w; ++i) a += h[i] * h[i], b += kb[i] * kb[i];
    ph = a > ph ? a : ph;
    pt = b > pt ? b : pt;
  }
  return db10(ph / pt);
}

// RS8: 448 Hz (454 in the spectrum) from ~1.5 ms, tau ~4.2 ms, its pitch up at the start (6 ms windows
// at 0.5 / 3 / 8 ms: 467 / 458 / 448 Hz, the samples' medians); 900 Hz at -12 dB; 1.75 kHz +6 dB over
// the sine's peak at the hit, tau ~2 ms; the click above 3.5 kHz at -10 dB; peak -1 dBFS.
void test_rs8_targets() {
  const float n = -1;
  drumRender(DrumMachine::Rs8, {n, n, n, n, n});
  withinDb(-1.f, 20.f * log10f(peakOf()), "RS8 peak");
  within(454, peakHz(1.5f, 40, 400, 500), 0.02f, "RS8 f1");
  within(467, peakHz(0.5f, 6.5f, 400, 540), 0.012f, "RS8 pitch @0.5");
  within(458, peakHz(3, 9, 400, 540), 0.012f, "RS8 pitch @3");
  within(448, peakHz(8, 14, 400, 540), 0.012f, "RS8 pitch @8");
  within(4.2f, trackTau(450, 5, 15), 0.1f, "RS8 f1 tau");
  withinDb(-12.f, relDb(900, 1.5f, 40, 454, 1.5f, 40), "RS8 900 Hz");
  float pk450 = 0;
  for (float t = 0; t < 10; t += 0.5f) pk450 = fmaxf(pk450, db10(powerAt(kb, t, t + 2, 450)));
  withinDb(6.f, db10(powerAt(kb, 0, 2, 1750)) - pk450, "RS8 1.75 kHz at the hit");
  within(2, trackTau(1750, 0, 6), 0.1f, "RS8 1.75 kHz tau");
  withinDb(-11.5f, clickDb(3500), "RS8 click");  // -10 at 44.1 kHz, same params (32 kHz: a lower Nyquist)
}

// RS9 (the 909's 40 ms sample): 220 Hz, tau 10 ms; 460 Hz at -7 dB, 900 Hz at -22 dB (Hann over the
// first 40 ms); the click above 3.5 kHz at -9 dB; peak -1 dBFS.
void test_rs9_targets() {
  const float n = -1;
  drumRender(DrumMachine::Rs9, {n, n, n, n, n});
  withinDb(-1.f, 20.f * log10f(peakOf()), "RS9 peak");
  const float f1 = peakHz(0, 40, 180, 260);
  within(220, f1, 0.02f, "RS9 f1");
  within(10, trackTau(220, 3, 25), 0.1f, "RS9 f1 tau");
  const float f2 = peakHz(0, 40, 400, 520);
  within(460, f2, 0.02f, "RS9 f2");
  withinDb(-7.f, relDb(f2, 0, 40, f1, 0, 40), "RS9 f2 level");
  withinDb(-22.f, relDb(peakHz(0, 40, 850, 950), 0, 40, f1, 0, 40), "RS9 900 Hz level");
  withinDb(-9.2f, clickDb(3500), "RS9 click");
}

// CL8: 2614 Hz, tau 5 ms, no pitch envelope; the click above 5.2 kHz at -15 dB; peak -1 dBFS.
void test_cl8_targets() {
  const float n = -1;
  drumRender(DrumMachine::Cl8, {n, n, n, n, n});
  withinDb(-1.f, 20.f * log10f(peakOf()), "CL8 peak");
  within(2614, peakHz(0.5f, 6.5f, 2400, 2800), 0.003f, "CL8 pitch @0.5");
  within(2614, peakHz(8, 14, 2400, 2800), 0.003f, "CL8 pitch @8");
  within(5, trackTau(2614, 1, 15), 0.1f, "CL8 tau");
  withinDb(-15.f, clickDb(5228), "CL8 click");
}

// CB8: 422 / 632 Hz; in the spectrum (3 .. 300 ms) the lower -15.4 dB re the upper (the samples' median)
// and 843 Hz at -20 dB; level 0.75 tau 20 ms + 0.25 tau 125 ms (5 ms power frames), -60 dB at ~0.8 s;
// peak -1 dBFS.
void test_cb8_targets() {
  const float n = -1;
  char what[32];
  drumRender(DrumMachine::Cb8, {n, n, n, n, n});
  withinDb(-1.f, 20.f * log10f(peakOf()), "CB8 peak");
  const float f1 = peakHz(3, 300, 400, 450), f2 = peakHz(3, 300, 600, 660);
  within(422, f1, 0.01f, "CB8 f1");
  within(632, f2, 0.01f, "CB8 f2");
  withinDb(-15.4f, relDb(f1, 3, 300, f2, 3, 300), "CB8 f1 re f2");
  withinDb(-20.f, relDb(843, 3, 300, f2, 3, 300), "CB8 843 Hz");
  constexpr int fr = kKickN / (kSynthRate / 200);
  static double e[fr];
  memset(e, 0, sizeof e);
  addFrames(kb, 5, e, fr);
  within(125, fitTau(e, 5, 150, 500), 0.1f, "CB8 slow tau");
  // The level against 0.75 e^(-t / 20) + 0.25 e^(-t / 125), re 10 .. 15 ms (the first 10 ms beat and
  // ring in).
  auto model = [](float t) { return 20 * log10f(0.75f * expf(-t / 20) + 0.25f * expf(-t / 125)); };
  const float at[3] = {22.5f, 62.5f, 152.5f};
  for (float t : at) {
    snprintf(what, sizeof what, "CB8 level @%.0f", t);
    withinDb(model(t) - model(12.5f), db10(e[static_cast<int>(t / 5)] / e[2]), what);
  }
  float t60 = 0;
  for (int k = fr - 1; k > 0; --k)
    if (db10(e[k] / e[0]) > -60) {
      t60 = k * 5.f;
      break;
    }
  within(800, t60, 0.1f, "CB8 t60");
}

// ---- Hats and cymbals against the real machines (same design doc) ----

constexpr int kHatN = 5 * kSynthRate;  // 5 s
static float hb[kHatN];

// The machine's hit into hb at amp 1; mac[k] < 0 = the machine's default; the noise as after `seed`
// triggers; noNoise: the metal alone (its share of the energy).
static void hatRender(DrumMachine mc, const float (&over)[kFmMacros], int seed = 0, bool noNoise = false) {
  Instrument in;
  instrSetType(in, InstrType::Drum);
  drumSetMachine(in, static_cast<uint8_t>(mc));
  float mac[kFmMacros];
  for (int k = 0; k < kFmMacros; ++k) mac[k] = over[k] >= 0 ? over[k] : in.macro[k];
  DrumParams p;
  drumMachine(static_cast<uint8_t>(mc), mac, 60, p);
  if (noNoise) p.noiseLvl = 0;
  drumResetNoise();
  for (int k = 0; k < seed; ++k) DrumVoice().trigger(false);
  DrumVoice d;
  d.trigger(false);
  memset(hb, 0, sizeof hb);
  for (int pos = 0; pos < kHatN; pos += 32) {
    d.control(p, 32);
    d.render(hb + pos, 32, 1.f);
  }
}

// Measures of a hat or cymbal, the noise averaged over 8 hits (mean power, as the samples' analysis):
// 2 ms power frames, the energy of bands (4th-order Butterworth, 0 = no edge) over a time span, the
// median peak.
struct HatMeas {
  static constexpr int kFr = kHatN / (2 * kSynthRate / 1000);
  double fr[kFr];
  float peakDb;
};
struct HatBand {
  float lo, hi, ms0, ms1;
  double e;
};
static void hatMeasure(DrumMachine mc, const float (&over)[kFmMacros], HatMeas& m, HatBand* b = nullptr,
                       int nb = 0, bool noNoise = false) {
  static float h[kHatN];
  memset(m.fr, 0, sizeof m.fr);
  for (int k = 0; k < nb; ++k) b[k].e = 0;
  float pk[8];
  for (int hit = 0; hit < 8; ++hit) {
    hatRender(mc, over, hit, noNoise);
    addFrames(hb, 2, m.fr, HatMeas::kFr);
    pk[hit] = 0;
    for (float x : hb) pk[hit] = fmaxf(pk[hit], fabsf(x));
    for (int k = 0; k < nb; ++k) {
      const int a = static_cast<int>(b[k].ms0 * kSynthRate / 1000), n = static_cast<int>(b[k].ms1 * kSynthRate / 1000);
      memcpy(h, hb, n * sizeof(float));
      if (b[k].lo > 0) filt(h, n, b[k].lo, true, 2);
      if (b[k].hi > 0) filt(h, n, b[k].hi, false, 2);
      for (int i = a; i < n; ++i) b[k].e += h[i] * h[i];
    }
  }
  for (int i = 0; i < 8; ++i)
    for (int j = i + 1; j < 8; ++j)
      if (pk[j] < pk[i]) {
        const float t = pk[i];
        pk[i] = pk[j], pk[j] = t;
      }
  m.peakDb = 20.f * log10f(0.5f * (pk[3] + pk[4]));
}

// The analysis' tau: least squares over the 2 ms frames from the loudest one, those between -3 and
// -20 dB re it, up to the first below -20.
static float hatTau(const HatMeas& m) {
  int p = 0;
  for (int k = 0; k < HatMeas::kFr; ++k)
    if (m.fr[k] > m.fr[p]) p = k;
  double st = 0, sd = 0, stt = 0, std2 = 0;
  int n = 0;
  for (int k = p; k < HatMeas::kFr; ++k) {
    const double d = db10(m.fr[k] / m.fr[p]), t = (k + 0.5) * 2;
    if (d < -20) break;
    if (d > -3) continue;
    st += t, sd += d, stt += t * t, std2 += t * d, ++n;
  }
  return static_cast<float>(-8.6859 * (n * stt - st * st) / (n * std2 - st * sd));
}

// Level of the 2 ms frame at ms re the loudest, dB; the last ms above `db` re the loudest.
static float hatDbAt(const HatMeas& m, float ms) {
  double pk = 0;
  for (double f : m.fr) pk = f > pk ? f : pk;
  return db10(m.fr[static_cast<int>(ms / 2)] / pk);
}
static float hatLastAbove(const HatMeas& m, float db) {
  double pk = 0;
  for (double f : m.fr) pk = f > pk ? f : pk;
  for (int k = HatMeas::kFr - 1; k > 0; --k)
    if (db10(m.fr[k] / pk) > db) return (k + 1) * 2.f;
  return 0;
}

// The strongest of lo .. hi (step hz, each the mean power over a third of an octave, as the analysis) over ms0 .. ms1,
// summed over 4 hits.
static float hatPeakHz(DrumMachine mc, float ms0, float ms1, float lo, float hi, float step) {
  const float n = -1;
  constexpr int kMax = 128;
  double p[kMax] = {};
  const int nf = static_cast<int>((hi - lo) / step) + 1;
  for (int hit = 0; hit < 4; ++hit) {
    hatRender(mc, {n, n, n, n, n}, hit);
    for (int k = 0; k < nf && k < kMax; ++k)
      for (int j = -9; j <= 9; ++j) p[k] += powerAt(hb, ms0, ms1, (lo + k * step) * powf(2.f, j / 54.f));
  }
  int best = 0;
  for (int k = 0; k < nf && k < kMax; ++k)
    if (p[k] > p[best]) best = k;
  return lo + best * step;
}

// HH8 (808 metal set: 348 / 458.5 / 564 / 831 / 845 / 982 Hz): closed tau 18 ms at the default, 5 ..
// 40 over DECAY 0 .. 51, open 160 .. 660 over DECAY 76 .. 127; band-pass at 7.1 kHz with an air band
// (8 .. 16 kHz re 4 .. 8 kHz over 60 / 300 ms: +4.3 dB closed, -1.2 open, the samples' medians); nearly all metal
// (noise <= 10 % of the energy); the open hat cut between 0.6 and 0.8 s (DECAY 104: tau ~435 ms);
// peak -1 dBFS.
void test_hh8_targets() {
  const float n = -1;
  const float set[kDrumMetal] = {348.2f, 458.5f, 564.f, 831.f, 845.2f, 982.2f};
  float mac[kFmMacros] = {38, 64, 30, 64, 64};
  DrumParams p;
  drumMachine(static_cast<uint8_t>(DrumMachine::Hh8), mac, 60, p);
  for (int k = 0; k < kDrumMetal; ++k) within(set[k], p.metalHz[k], 0.001f, "HH8 metal set");
  within(5000, p.metalHp, 0.1f, "HH8 HP");
  static HatMeas m;
  HatBand b[3] = {{4000, 8000, 0, 60, 0}, {8000, 16000, 0, 60, 0}, {0, 0, 0, 2000, 0}};
  hatMeasure(DrumMachine::Hh8, {n, n, n, n, n}, m, b, 3);
  withinDb(-1.f, m.peakDb, "HH8 peak");
  within(18, hatTau(m), 0.1f, "HH8 closed tau");
  withinDb(4.3f, db10(b[1].e / b[0].e), "HH8 closed air");
  const double all = b[2].e;
  hatMeasure(DrumMachine::Hh8, {n, n, n, n, n}, m, b + 2, 1, true);
  char what[64];
  snprintf(what, sizeof what, "HH8 noise share %.1f %%", 100 * (1 - b[2].e / all));
  TEST_ASSERT_TRUE_MESSAGE(b[2].e / all > 0.9 && b[2].e / all <= 1.0001, what);
  within(7100, hatPeakHz(DrumMachine::Hh8, 0, 30, 5000, 9000, 50), 0.1f, "HH8 band");
  const float dec[4] = {0, 51, 76, 127}, tau[4] = {5, 40, 160, 660};
  for (int k = 0; k < 4; ++k) {
    hatMeasure(DrumMachine::Hh8, {dec[k], n, n, n, n}, m);
    snprintf(what, sizeof what, "HH8 tau @DECAY %.0f", dec[k]);
    within(tau[k], hatTau(m), 0.1f, what);
  }
  HatBand o[2] = {{4000, 8000, 0, 300, 0}, {8000, 16000, 0, 300, 0}};
  hatMeasure(DrumMachine::Hh8, {104, n, n, n, n}, m, o, 2);
  withinDb(-1.2f, db10(o[1].e / o[0].e), "HH8 open air");
  withinDb(-1.f, m.peakDb, "HH8 open peak");
  const float cut = hatLastAbove(m, -40);
  snprintf(what, sizeof what, "HH8 open cut: -40 dB at %.0f ms", cut);
  TEST_ASSERT_TRUE_MESSAGE(cut > 600 && cut < 800, what);
  TEST_ASSERT_TRUE_MESSAGE(hatDbAt(m, 560) > -20, "HH8 open before the cut");
}

// CY8: a fast band at 7.3 kHz and a slow one at 2.9 kHz (peaks of the 1/3-octave spectrum), above 5.5 kHz
// tau 634 ms, 2 .. 4 kHz 1133 ms (fits over 0.3 .. 2.5 s, the samples' medians this way; the analysis'
// STFT band taus read 508 / 1310); -60 dB at 3.5 .. 4.3 s; peak -1 dBFS.
void test_cy8_targets() {
  const float n = -1;
  static HatMeas m;
  hatMeasure(DrumMachine::Cy8, {n, n, n, n, n}, m);
  withinDb(-1.f, m.peakDb, "CY8 peak");
  within(3900, hatLastAbove(m, -60), 0.1f, "CY8 length");
  within(7300, hatPeakHz(DrumMachine::Cy8, 0, 100, 5500, 9000, 50), 0.1f, "CY8 fast band");
  within(2900, hatPeakHz(DrumMachine::Cy8, 500, 1500, 2000, 4000, 25), 0.1f, "CY8 slow band");
  // Band taus: 2 ms frames of each band (4th-order) of one hit (the noise is small), fit 0.3 .. 2.5 s.
  static float h[kHatN];
  static double f[2][HatMeas::kFr];
  const float lo[2] = {5500, 2000}, hi[2] = {0, 4000};
  memset(f, 0, sizeof f);
  hatRender(DrumMachine::Cy8, {n, n, n, n, n});
  for (int k = 0; k < 2; ++k) {
    memcpy(h, hb, sizeof h);
    filt(h, kHatN, lo[k], true, 2);
    if (hi[k] > 0) filt(h, kHatN, hi[k], false, 2);
    addFrames(h, 2, f[k], HatMeas::kFr);
  }
  const float fast = fitTau(f[0], 2, 300, 2500), slow = fitTau(f[1], 2, 300, 2500);
  within(567, fast, 0.1f, "CY8 fast band tau");  // 634 at 44.1 kHz, same params (the band ends at Nyquist)
  within(1133, slow, 0.1f, "CY8 slow band tau");
}

// HH9: closed tau 9.4 ms at DECAY 0, ~19 at the default (the 909's CH Decay 2), open 151 at 127; mostly
// noise (65 % of the energy); 4 .. 8 kHz re 8 .. 16 kHz -0.5 dB, 1 .. 4 kHz re 4 .. 8 -4.8 (the
// closed samples); peak -1 dBFS.
void test_hh9_targets() {
  const float n = -1;
  static HatMeas m;
  HatBand b[4] = {{1000, 4000, 0, 60, 0}, {4000, 8000, 0, 60, 0}, {8000, 16000, 0, 60, 0}, {0, 0, 0, 1000, 0}};
  hatMeasure(DrumMachine::Hh9, {n, n, n, n, n}, m, b, 4);
  withinDb(-1.f, m.peakDb, "HH9 peak");
  within(18.7f, hatTau(m), 0.1f, "HH9 closed tau");
  // 32 kHz: 8 .. 16 kHz ends at Nyquist and the filters warp toward it (-0.5 / -4.8 dB at 44.1 kHz).
  withinDb(1.4f, db10(b[1].e / b[2].e), "HH9 4-8k re 8-16k");
  withinDb(-7.1f, db10(b[0].e / b[1].e), "HH9 1-4k re 4-8k");
  const double all = b[3].e;
  hatMeasure(DrumMachine::Hh9, {n, n, n, n, n}, m, b + 3, 1, true);
  within(65, static_cast<float>(100 * (1 - b[3].e / all)), 0.1f, "HH9 noise share %");
  hatMeasure(DrumMachine::Hh9, {0, n, n, n, n}, m);
  within(9.4f, hatTau(m), 0.1f, "HH9 tau @DECAY 0");
  hatMeasure(DrumMachine::Hh9, {127, n, n, n, n}, m);
  within(151, hatTau(m), 0.1f, "HH9 tau @DECAY 127");
}

// CY9 (crash at the default RIDE/CR 127, ride at 0): TUNE 0 / 127: crash tau 433 / 204 ms, ride 337
// / 183; TUNE moves the band up (ride centroid x 1.72 over the first 25 ms, the samples'); the crash
// swells (5 .. 9.5 kHz: +6 dB at 45 .. 55 ms re 0 .. 5) and clangs (0.4 .. 1 kHz re 2 .. 8 kHz over 20
// ms: -1.2 dB at TUNE 4 of 10); peaks -1 dBFS.
void test_cy9_targets() {
  const float n = -1;
  static HatMeas m;
  char what[48];
  const float tune[2] = {0, 127}, crash[2] = {433, 204}, ride[2] = {337, 183};
  // The ride at TUNE 127 peaks lower at 32 kHz (its band near Nyquist; -1 dB at 44.1 kHz).
  const float ridePk[2] = {-1.f, -3.4f};
  for (int k = 0; k < 2; ++k) {
    hatMeasure(DrumMachine::Cy9, {n, tune[k], 127, n, n}, m);
    snprintf(what, sizeof what, "CY9 crash tau @TUNE %.0f", tune[k]);
    within(crash[k], hatTau(m), 0.1f, what);
    snprintf(what, sizeof what, "CY9 crash peak @TUNE %.0f", tune[k]);
    withinDb(-1.f, m.peakDb, what);
    hatMeasure(DrumMachine::Cy9, {n, tune[k], 0, n, n}, m);
    snprintf(what, sizeof what, "CY9 ride tau @TUNE %.0f", tune[k]);
    within(ride[k], hatTau(m), 0.1f, what);
    snprintf(what, sizeof what, "CY9 ride peak @TUNE %.0f", tune[k]);
    withinDb(ridePk[k], m.peakDb, what);
  }
  hatMeasure(DrumMachine::Cy9, {n, n, n, n, n}, m);
  withinDb(-1.f, m.peakDb, "CY9 peak");
  // Ride centroid (100 Hz steps, 1 kHz .. Nyquist) over 0 .. 25 ms, TUNE 127 re 0, 4 hits.
  float cen[2];
  for (int k = 0; k < 2; ++k) {
    double s = 0, w = 0;
    for (int hit = 0; hit < 4; ++hit) {
      hatRender(DrumMachine::Cy9, {n, tune[k], 0, n, n}, hit);
      for (float f = 1000; f < kSynthRate / 2; f += 100) {  // to Nyquist (above it a DFT aliases)
        const double p = powerAt(hb, 0, 25, f);
        s += p * f, w += p;
      }
    }
    cen[k] = static_cast<float>(s / w);
  }
  within(1.72f, cen[1] / cen[0], 0.1f, "CY9 ride TUNE band");
  HatBand b[4] = {{5000, 9500, 0, 5, 0}, {5000, 9500, 45, 55, 0}, {400, 1000, 0, 20, 0}, {2000, 8000, 0, 20, 0}};
  hatMeasure(DrumMachine::Cy9, {n, 4 * 12.7f, 127, n, n}, m, b, 4);
  withinDb(6.f, db10(b[1].e / b[0].e * 0.5), "CY9 crash swell");
  withinDb(-1.2f, db10(b[2].e / b[3].e), "CY9 crash clang");
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_all_machines_sane);
  RUN_TEST(test_bd8_base_pitch_c4);
  RUN_TEST(test_hh8_decay_monotonic);
  RUN_TEST(test_names);
  RUN_TEST(test_every_machine_sounds_and_ends);
  RUN_TEST(test_decay_fractional);
  RUN_TEST(test_clap_level_even);
  RUN_TEST(test_groups_cover_every_machine_once);
  RUN_TEST(test_group_names);
  RUN_TEST(test_bd9_sweep);
  RUN_TEST(test_bd9_decay_level_harmonics);
  RUN_TEST(test_bd8_targets);
  RUN_TEST(test_bd8_decay_range);
  RUN_TEST(test_sd8_tones);
  RUN_TEST(test_sd8_noise_balance_peak);
  RUN_TEST(test_sd9_tones);
  RUN_TEST(test_sd9_noise);
  RUN_TEST(test_tom8_targets);
  RUN_TEST(test_tom9_pitch);
  RUN_TEST(test_tom9_body);
  RUN_TEST(test_tom9_partials);
  RUN_TEST(test_tom9_noise);
  RUN_TEST(test_cp8_targets);
  RUN_TEST(test_cp9_targets);
  RUN_TEST(test_rs8_targets);
  RUN_TEST(test_rs9_targets);
  RUN_TEST(test_cl8_targets);
  RUN_TEST(test_cb8_targets);
  RUN_TEST(test_hh8_targets);
  RUN_TEST(test_cy8_targets);
  RUN_TEST(test_hh9_targets);
  RUN_TEST(test_cy9_targets);
  return UNITY_END();
}
