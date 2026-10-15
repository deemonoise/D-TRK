#include <math.h>
#include <stdint.h>
#include <initializer_list>
#include <unity.h>
#include "model.h"
#include "synth_filter.h"
#include "synth_lines.h"
#include "synth_vfilter.h"

using namespace mt;

void setUp() {}
void tearDown() {}

namespace {

constexpr int kBlock = 32;  // Synth's control period

// Deterministic white noise, -amp..amp.
struct Noise {
  uint32_t s = 0x12345678u;
  float next(float amp = 1.f) {
    s = s * 1664525u + 1013904223u;
    return amp * (static_cast<float>(s >> 8) * (2.f / 16777216.f) - 1.f);
  }
};

// RMS of a sine at hz (amplitude amp) through the filter, after the transient (half a second).
float rmsSine(FltKind k, float fc, float reso127, float hz, float amp = 0.1f) {
  VoiceFilter f;
  float out[kBlock], in[kBlock];
  double acc = 0;
  int t = 0;
  const int n = kSynthRate / 2;
  for (int blk = 0; blk < n / kBlock; ++blk) {
    f.set(k, fc, reso127, resoQ(reso127));
    for (int i = 0; i < kBlock; ++i, ++t) {
      in[i] = amp * sinf(6.2831853f * hz * t / kSynthRate);
      out[i] = 0;
    }
    f.processAdd(in, out, kBlock, nullptr);
    if (t > n / 2)
      for (int i = 0; i < kBlock; ++i) acc += out[i] * out[i];
  }
  return sqrtf(static_cast<float>(acc / (n / 2 - kBlock)));
}

float dB(float a, float b) { return 20.f * log10f(a / b); }

// A cutoff sweep 20 Hz .. 20 kHz over a second, full-scale noise: no NaN, |y| < bound.
void sweepBounded(FltKind k, float reso127, float bound) {
  VoiceFilter f;
  Noise nz;
  float out[kBlock], in[kBlock];
  const int blocks = kSynthRate / kBlock;
  for (int blk = 0; blk < blocks; ++blk) {
    f.set(k, 20.f * powf(1000.f, static_cast<float>(blk) / blocks), reso127, resoQ(reso127));
    for (int i = 0; i < kBlock; ++i) {
      in[i] = nz.next();
      out[i] = 0;
    }
    f.processAdd(in, out, kBlock, nullptr);
    for (int i = 0; i < kBlock; ++i) {
      TEST_ASSERT_FALSE(isnan(out[i]) || isinf(out[i]));
      TEST_ASSERT_TRUE(fabsf(out[i]) < bound);
    }
  }
}

// RMS over samples 20000..22050 after a single impulse at t 0 (self-oscillation); ringing() after.
float impulseRms(FltKind k, float fc, float reso127, bool* ring) {
  VoiceFilter f;
  float out[kBlock], in[kBlock] = {};
  double acc = 0;
  int t = 0;
  for (int blk = 0; blk < 22050 / kBlock + 1; ++blk) {
    f.set(k, fc, reso127, resoQ(reso127));
    in[0] = blk == 0 ? 1.f : 0.f;
    for (int i = 0; i < kBlock; ++i) out[i] = 0;
    f.processAdd(in, out, kBlock, nullptr);
    for (int i = 0; i < kBlock; ++i, ++t)
      if (t >= 20000 && t < 22050) acc += out[i] * out[i];
  }
  if (ring) *ring = f.ringing();
  return sqrtf(static_cast<float>(acc / 2050));
}

// Noise for half a second, then a tail (in = nullptr): samples until ringing() goes false (-1: not
// within maxSamples); *stepIn / *stepTail: the largest per-sample step of the output before / after.
int tailEnd(VoiceFilter& f, FltKind k, float fc, float reso127, int maxSamples, float* stepIn = nullptr,
            float* stepTail = nullptr) {
  Noise nz;
  float out[kBlock], in[kBlock];
  float prev = 0, sIn = 0, sTail = 0;
  for (int blk = 0; blk < kSynthRate / 2 / kBlock; ++blk) {
    f.set(k, fc, reso127, resoQ(reso127));
    for (int i = 0; i < kBlock; ++i) {
      in[i] = nz.next();
      out[i] = 0;
    }
    f.processAdd(in, out, kBlock, nullptr);
    for (int i = 0; i < kBlock; ++i) {
      if (blk > 100) sIn = fmaxf(sIn, fabsf(out[i] - prev));
      prev = out[i];
    }
  }
  int end = -1;
  for (int t = 0; t < maxSamples; t += kBlock) {
    for (int i = 0; i < kBlock; ++i) out[i] = 0;
    f.processAdd(nullptr, out, kBlock, nullptr);
    for (int i = 0; i < kBlock; ++i) {
      sTail = fmaxf(sTail, fabsf(out[i] - prev));
      prev = out[i];
    }
    if (!f.ringing()) {
      end = t + kBlock;
      break;
    }
  }
  if (stepIn) *stepIn = sIn;
  if (stepTail) *stepTail = sTail;
  return end;
}
int tailEnd(FltKind k, float fc, float reso127, int maxSamples, float* stepIn = nullptr, float* stepTail = nullptr) {
  VoiceFilter f;
  return tailEnd(f, k, fc, reso127, maxSamples, stepIn, stepTail);
}

// Power at hz of the filter's response to white noise (amp), set every block: Hann-windowed
// Goertzel averaged over 10 windows of 4096 (after a 0.25 s settle).
struct Spectrum {
  static constexpr int kWin = 4096, kWins = 10;
  float y[kWin * kWins];
  void run(VoiceFilter& f, FltKind k, float hz, float reso127, int16_t* line, float amp = 0.5f) {
    Noise nz;
    float out[kBlock], in[kBlock];
    int w = 0;
    const int settle = kSynthRate / 4;
    for (int t = 0; w < kWin * kWins; t += kBlock) {
      f.set(k, hz, reso127, resoQ(reso127));
      for (int i = 0; i < kBlock; ++i) {
        in[i] = nz.next(amp);
        out[i] = 0;
      }
      f.processAdd(in, out, kBlock, line);
      if (t >= settle)
        for (int i = 0; i < kBlock && w < kWin * kWins; ++i) y[w++] = out[i];
    }
  }
  float power(float hz) const {
    double acc = 0;
    const double c = 2.0 * cos(6.283185307 * hz / kSynthRate);
    for (int win = 0; win < kWins; ++win) {
      double s1 = 0, s2 = 0;
      for (int i = 0; i < kWin; ++i) {
        const double hann = 0.5 - 0.5 * cos(6.283185307 * i / (kWin - 1));
        const double s0 = y[win * kWin + i] * hann + c * s1 - s2;
        s2 = s1;
        s1 = s0;
      }
      acc += s1 * s1 + s2 * s2 - c * s1 * s2;
    }
    return static_cast<float>(acc / kWins);
  }
  float dBAt(float a, float b) const { return 10.f * log10f(power(a) / power(b)); }
};
Spectrum gSpec;

// The cutoff that puts FORMANT at vowel position p (0 A, 1 E, 2 I, 3 O, 4 U): p = oct / 9.45 x 4.
float vowelHz(float p) { return 20.f * powf(2.f, p * 0.25f * 9.451211f); }

}  // namespace

// LP / BP / HP through VoiceFilter: bit-exact to the Svf Synth used before, tails and ringing() too.
void test_svf_modes_bit_exact() {
  const FltKind kinds[] = {FltKind::Lp, FltKind::Bp, FltKind::Hp};
  const Svf::Mode modes[] = {Svf::Mode::Lp, Svf::Mode::Bp, Svf::Mode::Hp};
  const float hzs[] = {20.f, 330.f, 2500.f, 19000.f};
  const float resos[] = {0.f, 64.f, 127.f};
  for (int m = 0; m < 3; ++m)
    for (float hz : hzs)
      for (float res : resos) {
        VoiceFilter vf;
        Svf svf;
        Noise nz;
        float a[kBlock], b[kBlock], in[kBlock];
        for (int blk = 0; blk < 4096 / kBlock; ++blk) {
          // A cutoff move per block (an envelope), the q Synth caches.
          const float h = hz * (1.f + 0.002f * blk), q = resoQ(res);
          vf.set(kinds[m], h, res, q);
          svf.set(modes[m], h, q);
          const bool tail = blk >= 96;  // the last 32 blocks: in = nullptr, a ringing tail
          for (int i = 0; i < kBlock; ++i) {
            in[i] = nz.next();
            a[i] = b[i] = 0.25f;
          }
          vf.processAdd(tail ? nullptr : in, a, kBlock, nullptr);
          svf.processAdd(tail ? nullptr : in, b, kBlock);
          for (int i = 0; i < kBlock; ++i) TEST_ASSERT_TRUE(a[i] == b[i]);
          TEST_ASSERT_EQUAL(svf.ringing(), vf.ringing());
        }
        vf.reset();
        svf.reset();
        TEST_ASSERT_FALSE(vf.ringing());
      }
}

// LP -> BP -> HP keeps the Svf state (as Svf::set does today): still bit-exact over the switches.
void test_svf_mode_switch_keeps_state() {
  VoiceFilter vf;
  Svf svf;
  Noise nz;
  const FltKind kinds[] = {FltKind::Lp, FltKind::Bp, FltKind::Hp};
  const Svf::Mode modes[] = {Svf::Mode::Lp, Svf::Mode::Bp, Svf::Mode::Hp};
  float a[kBlock], b[kBlock], in[kBlock];
  for (int blk = 0; blk < 60; ++blk) {
    const int m = (blk / 7) % 3;
    vf.set(kinds[m], 800.f, 90.f, resoQ(90.f));
    svf.set(modes[m], 800.f, resoQ(90.f));
    for (int i = 0; i < kBlock; ++i) {
      in[i] = nz.next();
      a[i] = b[i] = 0;
    }
    vf.processAdd(in, a, kBlock, nullptr);
    svf.processAdd(in, b, kBlock);
    for (int i = 0; i < kBlock; ++i) TEST_ASSERT_TRUE(a[i] == b[i]);
  }
}

// LADDER: 24 dB/oct, two octaves above the cutoff >= 20 dB under two octaves below.
void test_ladder_slope() {
  const float lo = rmsSine(FltKind::Ladder, 1000.f, 0.f, 250.f), hi = rmsSine(FltKind::Ladder, 1000.f, 0.f, 4000.f);
  TEST_ASSERT_TRUE(dB(lo, hi) >= 20.f);
  TEST_ASSERT_TRUE(rmsSine(FltKind::Ladder, 1000.f, 0.f, 8000.f) < hi);
}

// Resonance 1 self-oscillates: a single impulse still rings half a second later; ringing() keeps it.
void test_ladder_self_oscillates() {
  VoiceFilter f;
  float out[kBlock], in[kBlock] = {};
  double acc = 0;
  int t = 0;
  for (int blk = 0; blk < 22050 / kBlock + 1; ++blk) {
    f.set(FltKind::Ladder, 1000.f, 127.f, resoQ(127.f));
    in[0] = blk == 0 ? 1.f : 0.f;
    for (int i = 0; i < kBlock; ++i) out[i] = 0;
    f.processAdd(in, out, kBlock, nullptr);
    for (int i = 0; i < kBlock; ++i, ++t)
      if (t >= 20000 && t < 22050) acc += out[i] * out[i];
  }
  TEST_ASSERT_TRUE(sqrtf(static_cast<float>(acc / 2050)) > 0.01f);
  TEST_ASSERT_TRUE(f.ringing());
}

// Below self-oscillation a tail dies: ringing() false a second after the input stops.
void test_ladder_tail_ends() {
  VoiceFilter f;
  Noise nz;
  float out[kBlock], in[kBlock];
  for (int blk = 0; blk < 200; ++blk) {
    f.set(FltKind::Ladder, 500.f, 100.f, resoQ(100.f));
    for (int i = 0; i < kBlock; ++i) {
      in[i] = nz.next();
      out[i] = 0;
    }
    f.processAdd(in, out, kBlock, nullptr);
  }
  TEST_ASSERT_TRUE(f.ringing());
  for (int blk = 0; blk < kSynthRate / kBlock; ++blk) f.processAdd(nullptr, out, kBlock, nullptr);
  TEST_ASSERT_FALSE(f.ringing());
}

void test_ladder_bounded_sweep() {
  sweepBounded(FltKind::Ladder, 0.f, 4.f);
  sweepBounded(FltKind::Ladder, 63.5f, 4.f);
  sweepBounded(FltKind::Ladder, 127.f, 4.f);
}

// The compensation keeps the passband: resonance 0.8 within 6 dB of resonance 0.
void test_ladder_passband_compensated() {
  const float r0 = rmsSine(FltKind::Ladder, 2000.f, 0.f, 100.f), r8 = rmsSine(FltKind::Ladder, 2000.f, 0.8f * 127.f, 100.f);
  TEST_ASSERT_TRUE(fabsf(dB(r8, r0)) <= 6.f);
  TEST_ASSERT_TRUE(r0 > 0.05f);  // near unity at r 0 (0.1 in)
}

// A switch from the Svf family to LADDER resets: no stale state, same output as a fresh filter.
void test_ladder_switch_resets() {
  VoiceFilter a, b;
  Noise nz;
  float out[kBlock], in[kBlock];
  for (int blk = 0; blk < 20; ++blk) {
    a.set(FltKind::Lp, 300.f, 120.f, resoQ(120.f));
    for (int i = 0; i < kBlock; ++i) {
      in[i] = nz.next();
      out[i] = 0;
    }
    a.processAdd(in, out, kBlock, nullptr);
  }
  float ya[kBlock] = {}, yb[kBlock] = {};
  a.set(FltKind::Ladder, 300.f, 60.f, resoQ(60.f));
  b.set(FltKind::Ladder, 300.f, 60.f, resoQ(60.f));
  a.processAdd(in, ya, kBlock, nullptr);
  b.processAdd(in, yb, kBlock, nullptr);
  for (int i = 0; i < kBlock; ++i) TEST_ASSERT_TRUE(ya[i] == yb[i]);
}

// MS20: 12 dB/oct, two octaves above the cutoff >= 10 dB under the passband.
void test_ms20_slope() {
  const float pass = rmsSine(FltKind::Ms20, 1000.f, 0.f, 250.f), hi = rmsSine(FltKind::Ms20, 1000.f, 0.f, 4000.f);
  TEST_ASSERT_TRUE(dB(pass, hi) >= 10.f);
  TEST_ASSERT_TRUE(pass > 0.05f);  // near unity in the passband (0.1 in)
}

// Resonance 0.8 peaks at the cutoff.
void test_ms20_resonance_peak() {
  TEST_ASSERT_TRUE(rmsSine(FltKind::Ms20, 1000.f, 0.8f * 127.f, 1000.f) > rmsSine(FltKind::Ms20, 1000.f, 0.8f * 127.f, 500.f));
  TEST_ASSERT_TRUE(rmsSine(FltKind::Ms20, 1000.f, 0.8f * 127.f, 1000.f) > rmsSine(FltKind::Ms20, 1000.f, 0.f, 1000.f));
}

// Screams but stays bounded: a full-scale saw at resonance 1.
void test_ms20_bounded_saw() {
  VoiceFilter f;
  float out[kBlock], in[kBlock];
  int t = 0;
  for (const float fc : {80.f, 700.f, 5000.f, 19000.f})
    for (int blk = 0; blk < kSynthRate / 2 / kBlock; ++blk) {
      f.set(FltKind::Ms20, fc, 127.f, resoQ(127.f));
      for (int i = 0; i < kBlock; ++i, ++t) {
        float ph = t * (110.f / kSynthRate);
        ph -= static_cast<float>(static_cast<int>(ph));
        in[i] = 2.f * ph - 1.f;
        out[i] = 0;
      }
      f.processAdd(in, out, kBlock, nullptr);
      for (int i = 0; i < kBlock; ++i) {
        TEST_ASSERT_FALSE(isnan(out[i]));
        TEST_ASSERT_TRUE(fabsf(out[i]) < 2.f);
      }
    }
}

void test_ms20_bounded_sweep() {
  sweepBounded(FltKind::Ms20, 0.f, 2.f);
  sweepBounded(FltKind::Ms20, 63.5f, 2.f);
  sweepBounded(FltKind::Ms20, 127.f, 2.f);
}

// A tail below the scream dies: ringing() false a second after the input stops.
void test_ms20_tail_ends() {
  VoiceFilter f;
  Noise nz;
  float out[kBlock], in[kBlock];
  for (int blk = 0; blk < 200; ++blk) {
    f.set(FltKind::Ms20, 500.f, 100.f, resoQ(100.f));
    for (int i = 0; i < kBlock; ++i) {
      in[i] = nz.next();
      out[i] = 0;
    }
    f.processAdd(in, out, kBlock, nullptr);
  }
  TEST_ASSERT_TRUE(f.ringing());
  for (int blk = 0; blk < kSynthRate / kBlock; ++blk) f.processAdd(nullptr, out, kBlock, nullptr);
  TEST_ASSERT_FALSE(f.ringing());
  f.set(FltKind::Ms20, 500.f, 100.f, resoQ(100.f));
  f.processAdd(in, out, kBlock, nullptr);
  f.reset();
  TEST_ASSERT_FALSE(f.ringing());
}

// LADDER -> MS20 resets: same output as a fresh filter.
void test_ms20_switch_resets() {
  VoiceFilter a, b;
  Noise nz;
  float out[kBlock], in[kBlock];
  for (int blk = 0; blk < 20; ++blk) {
    a.set(FltKind::Ladder, 300.f, 120.f, resoQ(120.f));
    for (int i = 0; i < kBlock; ++i) {
      in[i] = nz.next();
      out[i] = 0;
    }
    a.processAdd(in, out, kBlock, nullptr);
  }
  float ya[kBlock] = {}, yb[kBlock] = {};
  a.set(FltKind::Ms20, 300.f, 60.f, resoQ(60.f));
  b.set(FltKind::Ms20, 300.f, 60.f, resoQ(60.f));
  a.processAdd(in, ya, kBlock, nullptr);
  b.processAdd(in, yb, kBlock, nullptr);
  for (int i = 0; i < kBlock; ++i) TEST_ASSERT_TRUE(ya[i] == yb[i]);
}

// MS20 at resonance 1 screams on its own: an impulse still rings half a second later.
void test_ms20_self_oscillates() {
  for (const float fc : {100.f, 1000.f, 8000.f}) {
    bool ring = false;
    TEST_ASSERT_TRUE(impulseRms(FltKind::Ms20, fc, 127.f, &ring) > 0.01f);
    TEST_ASSERT_TRUE(ring);
  }
}

// A self-oscillating tail fades out (Synth frees the voice): ringing() false within 0.2 s, no click.
void test_self_oscillation_tail_fades() {
  for (const FltKind k : {FltKind::Ladder, FltKind::Ms20})
    for (const float fc : {200.f, 2000.f}) {
      float stepIn = 0, stepTail = 0;
      const int end = tailEnd(k, fc, 127.f, kSynthRate / 5, &stepIn, &stepTail);
      TEST_ASSERT_TRUE(end > 0);
      TEST_ASSERT_TRUE(stepTail <= stepIn * 1.05f);
    }
}

// Just under self-oscillation the decay is slow: the 2 s cap fades it, done within 2.5 s.
void test_near_threshold_tail_capped() {
  for (const float res : {119.f, 120.f, 120.9f}) {
    TEST_ASSERT_TRUE(tailEnd(FltKind::Ladder, 300.f, res, kSynthRate * 5 / 2) > 0);
    TEST_ASSERT_TRUE(tailEnd(FltKind::Ms20, 300.f, res, kSynthRate * 5 / 2) > 0);
  }
  // 120.9: k, K a hair under the threshold, a decay of minutes: only the cap ends it.
  TEST_ASSERT_TRUE(tailEnd(FltKind::Ladder, 300.f, 120.9f, kSynthRate * 5 / 2) >= 2 * kSynthRate);
}

// New input after a faded tail plays at full level again.
void test_tail_fade_resets_on_input() {
  VoiceFilter f;
  TEST_ASSERT_TRUE(tailEnd(f, FltKind::Ladder, 1000.f, 127.f, kSynthRate / 5) > 0);
  f.set(FltKind::Ladder, 1000.f, 0.f, resoQ(0.f));  // a new note, no resonance
  float out[kBlock], in[kBlock];
  double acc = 0;
  for (int t = 0; t < kSynthRate / 2; t += kBlock) {
    for (int i = 0; i < kBlock; ++i) {
      in[i] = 0.1f * sinf(6.2831853f * 100.f * (t + i) / kSynthRate);
      out[i] = 0;
    }
    f.processAdd(in, out, kBlock, nullptr);
    if (t >= kSynthRate / 4)
      for (int i = 0; i < kBlock; ++i) acc += out[i] * out[i];
  }
  TEST_ASSERT_TRUE(f.ringing());
  TEST_ASSERT_TRUE(sqrtf(static_cast<float>(acc / (kSynthRate / 4))) > 0.05f);
}

// COMB: tuned to the cutoff: the fundamental and its octave stand over the valley between them.
void test_comb_peaks() {
  VoiceFilter f;
  f.reset();
  gSpec.run(f, FltKind::Comb, 220.f, 0.9f * 127.f, voiceLine(0));
  TEST_ASSERT_TRUE(gSpec.dBAt(220.f, 330.f) >= 10.f);
  TEST_ASSERT_TRUE(gSpec.dBAt(440.f, 330.f) >= 10.f);
}

// Below 86 Hz the line is too short: the delay holds at kLineLen - 1 samples.
void test_comb_lowest_pitch() {
  VoiceFilter f;
  const float low = static_cast<float>(kSynthRate) / (kLineLen - 1);  // 86.3 Hz
  gSpec.run(f, FltKind::Comb, 50.f, 0.9f * 127.f, voiceLine(0));
  TEST_ASSERT_TRUE(gSpec.dBAt(low, 1.5f * low) >= 10.f);
  TEST_ASSERT_TRUE(gSpec.dBAt(low, 50.f) >= 10.f);
}

void test_comb_bounded() {
  VoiceFilter f;
  Noise nz;
  float out[kBlock], in[kBlock];
  const int blocks = kSynthRate / kBlock;
  for (int blk = 0; blk < 2 * blocks; ++blk) {
    f.set(FltKind::Comb, 20.f * powf(1000.f, static_cast<float>(blk % blocks) / blocks), 127.f, 1.f);
    for (int i = 0; i < kBlock; ++i) {
      in[i] = nz.next() > 0 ? 1.f : -1.f;  // full scale
      out[i] = 0;
    }
    f.processAdd(in, out, kBlock, voiceLine(0));
    for (int i = 0; i < kBlock; ++i) {
      TEST_ASSERT_FALSE(isnan(out[i]) || isinf(out[i]));
      TEST_ASSERT_TRUE(fabsf(out[i]) < 4.f);
    }
  }
}

// ringing(): true right after input, false 2 s into the silence at r 0.9.
void test_comb_tail() {
  VoiceFilter f;
  Noise nz;
  float out[kBlock], in[kBlock];
  for (int blk = 0; blk < 100; ++blk) {
    f.set(FltKind::Comb, 220.f, 0.9f * 127.f, 1.f);
    for (int i = 0; i < kBlock; ++i) {
      in[i] = nz.next();
      out[i] = 0;
    }
    f.processAdd(in, out, kBlock, voiceLine(0));
  }
  TEST_ASSERT_TRUE(f.ringing());
  // An impulse then silence: still ringing between the echoes (D = 200 > a block).
  in[0] = 1.f;
  for (int i = 1; i < kBlock; ++i) in[i] = 0;
  f.processAdd(in, out, kBlock, voiceLine(0));
  for (int blk = 0; blk < 12; ++blk) {
    f.processAdd(nullptr, out, kBlock, voiceLine(0));
    TEST_ASSERT_TRUE(f.ringing());
  }
  for (int blk = 0; blk < 2 * kSynthRate / kBlock; ++blk) f.processAdd(nullptr, out, kBlock, voiceLine(0));
  TEST_ASSERT_FALSE(f.ringing());
}

// Two voices, two lines: each comb hears only its own input.
void test_comb_lines_separate() {
  VoiceFilter a, b, solo;
  Noise na, nb, ns;
  float oa[kBlock], ob[kBlock], os[kBlock], ia[kBlock], ib[kBlock], is[kBlock];
  for (int blk = 0; blk < 200; ++blk) {
    a.set(FltKind::Comb, 220.f, 110.f, 1.f);
    b.set(FltKind::Comb, 150.f, 110.f, 1.f);
    solo.set(FltKind::Comb, 220.f, 110.f, 1.f);
    for (int i = 0; i < kBlock; ++i) {
      ia[i] = na.next();
      ib[i] = nb.next() * 0.3f + 0.5f;
      is[i] = ns.next();
      oa[i] = ob[i] = os[i] = 0;
    }
    a.processAdd(ia, oa, kBlock, voiceLine(1));
    b.processAdd(ib, ob, kBlock, voiceLine(2));
    solo.processAdd(is, os, kBlock, voiceLine(3));
    for (int i = 0; i < kBlock; ++i) TEST_ASSERT_TRUE(oa[i] == os[i]);
  }
}

// A new note does not hear the line's last owner: reset() (and a switch to COMB) clears it.
void test_comb_reset_clears_line() {
  int16_t* line = voiceLine(4);
  for (int i = 0; i < kLineLen; ++i) line[i] = static_cast<int16_t>((i * 7919) & 0x3FFF);  // stale
  float out[kBlock] = {};
  VoiceFilter f;
  f.set(FltKind::Comb, 100.f, 120.f, 1.f);  // a switch from LP
  for (int blk = 0; blk < 40; ++blk) f.processAdd(nullptr, out, kBlock, line);
  for (int i = 0; i < kBlock; ++i) TEST_ASSERT_TRUE(out[i] == 0);
  TEST_ASSERT_FALSE(f.ringing());
  for (int i = 0; i < kLineLen; ++i) line[i] = static_cast<int16_t>((i * 7919) & 0x3FFF);
  f.reset();
  for (int blk = 0; blk < 40; ++blk) f.processAdd(nullptr, out, kBlock, line);
  for (int i = 0; i < kBlock; ++i) TEST_ASSERT_TRUE(out[i] == 0);
}

// FORMANT at A (cutoff 0): F1 730, F2 1090 stand over the gap to F3 (2440).
void test_formant_a() {
  VoiceFilter f;
  gSpec.run(f, FltKind::Formant, vowelHz(0), 64.f, nullptr);
  TEST_ASSERT_TRUE(gSpec.dBAt(730.f, 1600.f) >= 6.f);
  TEST_ASSERT_TRUE(gSpec.dBAt(1090.f, 1600.f) >= 6.f);
}

// At U (cutoff 127): F1 300 over 600.
void test_formant_u() {
  VoiceFilter f;
  gSpec.run(f, FltKind::Formant, vowelHz(4), 64.f, nullptr);
  TEST_ASSERT_TRUE(gSpec.dBAt(300.f, 600.f) > 0.f);
  VoiceFilter g;  // the top of the cutoff range holds at U
  gSpec.run(g, FltKind::Formant, 19000.f, 64.f, nullptr);
  TEST_ASSERT_TRUE(gSpec.dBAt(300.f, 600.f) > 0.f);
}

// Between E and I (1.5) F2 moves over 1800 Hz (sqrt(1840 x 2290) = 2053).
void test_formant_moves() {
  VoiceFilter f;
  gSpec.run(f, FltKind::Formant, vowelHz(1.5f), 64.f, nullptr);
  TEST_ASSERT_TRUE(gSpec.dBAt(2053.f, 1600.f) >= 6.f);
  const float e = gSpec.power(2053.f);
  VoiceFilter g;
  gSpec.run(g, FltKind::Formant, vowelHz(0), 64.f, nullptr);
  TEST_ASSERT_TRUE(e > 2.f * gSpec.power(2053.f));
}

// Resonance narrows the formants: a deeper gap at r 1 than at r 0.
void test_formant_q_from_reso() {
  VoiceFilter f, g;
  gSpec.run(f, FltKind::Formant, vowelHz(0), 0.f, nullptr);
  const float wide = gSpec.dBAt(730.f, 1600.f);
  gSpec.run(g, FltKind::Formant, vowelHz(0), 127.f, nullptr);
  TEST_ASSERT_TRUE(gSpec.dBAt(730.f, 1600.f) >= wide + 3.f);
}

// set() with unchanged inputs does not recompute (Synth sets every control tick); a change does.
void test_formant_set_cached() {
  VoiceFilter f;
  f.set(FltKind::Formant, 500.f, 64.f, 1.f);
  const uint32_t n0 = VoiceFilter::formantComputes;
  for (int i = 0; i < 10; ++i) f.set(FltKind::Formant, 500.f, 64.f, resoQ(64.f));
  TEST_ASSERT_EQUAL_UINT32(n0, VoiceFilter::formantComputes);
  f.set(FltKind::Formant, 510.f, 64.f, 1.f);
  f.set(FltKind::Formant, 510.f, 70.f, 1.f);
  TEST_ASSERT_EQUAL_UINT32(n0 + 2, VoiceFilter::formantComputes);
}

void test_formant_tail_and_bounds() {
  VoiceFilter f;
  sweepBounded(FltKind::Formant, 127.f, 4.f);
  Noise nz;
  float out[kBlock], in[kBlock];
  for (int blk = 0; blk < 100; ++blk) {
    f.set(FltKind::Formant, vowelHz(2), 127.f, 1.f);
    for (int i = 0; i < kBlock; ++i) {
      in[i] = nz.next();
      out[i] = 0;
    }
    f.processAdd(in, out, kBlock, nullptr);
  }
  TEST_ASSERT_TRUE(f.ringing());
  for (int blk = 0; blk < kSynthRate / kBlock; ++blk) f.processAdd(nullptr, out, kBlock, nullptr);
  TEST_ASSERT_FALSE(f.ringing());
}

// A tuned COMB (key track 100 %: the note's harmonics on the comb's peaks) stays in level: a saw 0.8
// at 220 Hz peaks <= 1.3 at any resonance, near unity at r 0, and the line never hard-clips.
void test_comb_tuned_gain() {
  for (const float r : {0.f, 0.5f, 0.9f, 1.f}) {
    VoiceFilter f;
    int16_t* line = voiceLine(5);
    float out[kBlock], in[kBlock];
    float peak = 0;
    int clipped = 0, written = 0;
    int t = 0;
    for (int blk = 0; blk < kSynthRate / kBlock; ++blk) {
      f.set(FltKind::Comb, 220.f, r * 127.f, 1.f);
      for (int i = 0; i < kBlock; ++i, ++t) {
        float ph = t * (220.f / kSynthRate);
        ph -= static_cast<float>(static_cast<int>(ph));
        in[i] = 0.8f * (2.f * ph - 1.f);
        out[i] = 0;
      }
      f.processAdd(in, out, kBlock, line);
      for (int i = 0; i < kBlock; ++i) peak = fmaxf(peak, fabsf(out[i]));
      if (blk % 16 == 0)
        for (int i = 0; i < kLineLen; ++i, ++written) clipped += line[i] >= 32767 || line[i] <= -32767;
    }
    TEST_ASSERT_TRUE(peak <= 1.3f);
    if (r == 0) TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.8f, peak);
    TEST_ASSERT_TRUE(clipped * 100 < written);
  }
}

// A cutoff sweep moves the delay smoothly (ramped over the block): no zipper. Second differences of a
// 220 Hz sine through the sweep 200 -> 2000 Hz stay within 2x of the static renders' at either end.
static float maxJerk(float hz0, float hz1) {
  VoiceFilter f;
  float out[kBlock], in[kBlock];
  float y1 = 0, y2 = 0, jerk = 0;
  const int blocks = kSynthRate / 2 / kBlock;
  int t = 0;
  for (int blk = 0; blk < blocks; ++blk) {
    f.set(FltKind::Comb, hz0 * powf(hz1 / hz0, static_cast<float>(blk) / blocks), 64.f, 1.f);
    for (int i = 0; i < kBlock; ++i, ++t) {
      in[i] = 0.5f * sinf(6.2831853f * 220.f * t / kSynthRate);
      out[i] = 0;
    }
    f.processAdd(in, out, kBlock, voiceLine(6));
    for (int i = 0; i < kBlock; ++i) {
      if (blk > 20) jerk = fmaxf(jerk, fabsf(out[i] - 2.f * y1 + y2));
      y2 = y1;
      y1 = out[i];
    }
  }
  return jerk;
}

void test_comb_sweep_no_zipper() {
  const float ref = fmaxf(maxJerk(200.f, 200.f), maxJerk(2000.f, 2000.f));
  TEST_ASSERT_TRUE(maxJerk(200.f, 2000.f) <= 2.f * ref);
}

// The longest delay (D = 511, 16 blocks between echoes): an impulse into a cleared line keeps the
// voice ringing across the gaps while the echoes last.
void test_comb_ringing_across_echo_gaps() {
  VoiceFilter f;
  float out[kBlock], in[kBlock] = {};
  f.set(FltKind::Comb, 50.f, 0.9f * 127.f, 1.f);
  in[0] = 1.f;
  f.processAdd(in, out, kBlock, voiceLine(7));
  for (int blk = 0; blk < 25 * kLineLen / kBlock; ++blk) {  // 25 echoes: 0.873^25 = 3 %
    f.processAdd(nullptr, out, kBlock, voiceLine(7));
    TEST_ASSERT_TRUE(f.ringing());
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_svf_modes_bit_exact);
  RUN_TEST(test_svf_mode_switch_keeps_state);
  RUN_TEST(test_ladder_slope);
  RUN_TEST(test_ladder_self_oscillates);
  RUN_TEST(test_ladder_tail_ends);
  RUN_TEST(test_ladder_bounded_sweep);
  RUN_TEST(test_ladder_passband_compensated);
  RUN_TEST(test_ladder_switch_resets);
  RUN_TEST(test_ms20_slope);
  RUN_TEST(test_ms20_resonance_peak);
  RUN_TEST(test_ms20_bounded_saw);
  RUN_TEST(test_ms20_bounded_sweep);
  RUN_TEST(test_ms20_tail_ends);
  RUN_TEST(test_ms20_switch_resets);
  RUN_TEST(test_ms20_self_oscillates);
  RUN_TEST(test_self_oscillation_tail_fades);
  RUN_TEST(test_near_threshold_tail_capped);
  RUN_TEST(test_tail_fade_resets_on_input);
  RUN_TEST(test_comb_peaks);
  RUN_TEST(test_comb_lowest_pitch);
  RUN_TEST(test_comb_bounded);
  RUN_TEST(test_comb_tail);
  RUN_TEST(test_comb_lines_separate);
  RUN_TEST(test_comb_reset_clears_line);
  RUN_TEST(test_formant_a);
  RUN_TEST(test_formant_u);
  RUN_TEST(test_formant_moves);
  RUN_TEST(test_formant_q_from_reso);
  RUN_TEST(test_formant_set_cached);
  RUN_TEST(test_formant_tail_and_bounds);
  RUN_TEST(test_comb_tuned_gain);
  RUN_TEST(test_comb_sweep_no_zipper);
  RUN_TEST(test_comb_ringing_across_echo_gaps);
  return UNITY_END();
}
