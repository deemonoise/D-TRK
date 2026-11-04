#include <math.h>
#include <string.h>
#include <complex>
#include <unity.h>
#include "synth_osc.h"
#include "synth_reverb.h"

using namespace mt;

static float buf[Reverb::kBufLen];
static constexpr int kN = 128;
static constexpr int kRate = 44100;

void setUp() {}
void tearDown() {}

void test_silent_without_buffer() {
  Reverb r;
  float in[kN] = {1}, mix[kN] = {0}, mr[kN] = {0};
  r.process(in, mix, mr, kN, 60, 70, 127, false);
  for (int i = 0; i < kN; ++i) TEST_ASSERT_TRUE(mix[i] == 0.f && mr[i] == 0.f);
  r.setBuffer(buf, Reverb::kBufLen - 1);  // too short: off
  r.process(in, mix, mr, kN, 60, 70, 127, false);
  for (int i = 0; i < kN; ++i) TEST_ASSERT_TRUE(mix[i] == 0.f && mr[i] == 0.f);
}

// Peak of the output per 100 ms window after an impulse, for `seconds`.
static int tailWindows(uint8_t size, float* peaks, int maxWin) {
  Reverb r;
  r.setBuffer(buf, Reverb::kBufLen);
  const int win = kRate / 10;
  int w = 0, inWin = 0;
  float peak = 0;
  for (int b = 0; w < maxWin; ++b) {
    float in[kN] = {0}, mix[kN] = {0}, mr[kN] = {0};
    if (b == 0) in[0] = 1.f;
    r.process(in, mix, mr, kN, size, 70, 127, false);  // L: the mono path
    for (int i = 0; i < kN && w < maxWin; ++i) {
      if (fabsf(mix[i]) > peak) peak = fabsf(mix[i]);
      if (++inWin == win) {
        peaks[w++] = peak;
        peak = 0;
        inWin = 0;
      }
    }
  }
  return w;
}

void test_impulse_tail_decays() {
  float peaks[30];
  tailWindows(60, peaks, 30);  // 3 s
  float top = 0;
  for (float p : peaks) top = p > top ? p : top;
  TEST_ASSERT_TRUE(top > 0);
  TEST_ASSERT_TRUE(peaks[1] > 0.01f * top);   // still ringing at 100..200 ms
  TEST_ASSERT_TRUE(peaks[29] < 1e-3f * top);  // below -60 dB by 3 s
}

static int decayWindow(uint8_t size) {
  float peaks[60];
  tailWindows(size, peaks, 60);
  float top = 0;
  for (float p : peaks) top = p > top ? p : top;
  for (int w = 1; w < 60; ++w)
    if (peaks[w] < 1e-3f * top) return w;
  return 60;
}

void test_size_lengthens_tail() { TEST_ASSERT_TRUE(decayWindow(0) < decayWindow(127)); }

// Stereo: an impulse rings on both sides, and the sides differ (the extra allpass on R).
void test_stereo_impulse_both_sides_differ() {
  Reverb r;
  r.setBuffer(buf, Reverb::kBufLen);
  bool anyL = false, anyR = false, diff = false;
  for (int b = 0; b < 40; ++b) {
    float in[kN] = {0}, l[kN] = {0}, rr[kN] = {0};
    if (b == 0) in[0] = 1.f;
    r.process(in, l, rr, kN, 60, 70, 127, false);
    for (int i = 0; i < kN; ++i) {
      anyL |= l[i] != 0;
      anyR |= rr[i] != 0;
      diff |= l[i] != rr[i];
    }
  }
  TEST_ASSERT_TRUE(anyL);
  TEST_ASSERT_TRUE(anyR);
  TEST_ASSERT_TRUE(diff);
}

// The corrected diffusion network is allpass: it does not add gain to the comb sum.
static double steadyGain(float hz) {
  Reverb rv;
  rv.setBuffer(buf, Reverb::kBufLen);
  double in2 = 0, out2 = 0;
  for (int i = 0; i < kRate * 2; ++i) {
    const float input = .1f * (hz == 0 ? 1.f : sinf(6.28318530718f * hz * i / kRate));
    float l = 0, r = 0;
    rv.process(&input, &l, &r, 1, 0, 0, 127, false);
    if (i >= kRate) in2 += input * input, out2 += l * l;
  }
  return sqrt(out2 / in2);
}
void test_diffusion_has_unit_magnitude() {
  TEST_ASSERT_FLOAT_WITHIN(.001f, .5f / .3f, steadyGain(0));
  for (float hz : {43.f, 151.f, 997.f, 5003.f}) {
    const double w = 6.283185307179586 * hz / kRate;
    std::complex<double> comb(0, 0);
    for (int len : Reverb::kCombLen) {
      const auto z = std::polar(1., -w * static_cast<int>(len * .5f));
      comb += z / (1. - .7 * z);
    }
    TEST_ASSERT_FLOAT_WITHIN(.002f, abs(comb) * .125, steadyGain(hz));
  }
}

void test_level_change_starts_smoothly() {
  Reverb rv;
  rv.setBuffer(buf, Reverb::kBufLen);
  float last = 0;
  for (int i = 0; i < kRate; ++i) {
    const float x = .1f; float l = 0, r = 0;
    rv.process(&x, &l, &r, 1, 64, 64, 127, false); last = l;
  }
  const float x = .1f; float l = 0, r = 0;
  rv.process(&x, &l, &r, 1, 64, 64, 1, false);
  TEST_ASSERT_TRUE(fabsf(l - last) < fabsf(last) * .01f);
}

void test_size_change_crossfades_old_read() {
  Reverb a, b;
  static float ba[Reverb::kBufLen], bb[Reverb::kBufLen];
  a.setBuffer(ba, Reverb::kBufLen); b.setBuffer(bb, Reverb::kBufLen);
  uint32_t state = 1;
  auto next = [&] { state = state * 1664525u + 1013904223u;
                    return static_cast<int32_t>(state) * (.25f / 2147483648.f); };
  for (int i = 0; i < 10000; ++i) {
    const float x = next(); float al = 0, ar = 0, bl = 0, br = 0;
    a.process(&x, &al, &ar, 1, 127, 0, 127, false);
    b.process(&x, &bl, &br, 1, 127, 0, 127, false);
  }
  const float x = next(); float al = 0, ar = 0, bl = 0, br = 0;
  a.process(&x, &al, &ar, 1, 0, 0, 127, false);
  b.process(&x, &bl, &br, 1, 127, 0, 127, false);
  TEST_ASSERT_FLOAT_WITHIN(.001f, bl, al);
  TEST_ASSERT_FLOAT_WITHIN(.001f, br, ar);
}

void test_level_zero_fades_then_is_exact() {
  Reverb rv;
  rv.setBuffer(buf, Reverb::kBufLen);
  for (int i = 0; i < kRate; ++i) {
    const float x = .1f; float l = 0, r = 0;
    rv.process(&x, &l, &r, 1, 64, 64, 127, false);
  }
  const float x = .1f; float l = 0, r = 0;
  rv.process(&x, &l, &r, 1, 64, 64, 0, false);
  TEST_ASSERT_TRUE(l > .01f);
  for (int i = 0; i < 256; ++i) {
    l = r = 0; rv.process(&x, &l, &r, 1, 64, 64, 0, false);
  }
  TEST_ASSERT_EQUAL_FLOAT(0, l); TEST_ASSERT_EQUAL_FLOAT(0, r);
}

void test_stereo_level_zero_untouched() {
  Reverb r;
  r.setBuffer(buf, Reverb::kBufLen);
  float in[kN], l[kN], rr[kN], ref[kN];
  for (int i = 0; i < kN; ++i) {
    in[i] = 0.5f;
    l[i] = rr[i] = ref[i] = i * 0.001f;
  }
  r.process(in, l, rr, kN, 60, 70, 0, false);
  TEST_ASSERT_EQUAL_MEMORY(ref, l, sizeof(ref));
  TEST_ASSERT_EQUAL_MEMORY(ref, rr, sizeof(ref));
}

// The idle check covers both outputs: it rests only once both tails are gone, then wakes on input.
void test_stereo_idle_after_tail() {
  Reverb r;
  r.setBuffer(buf, Reverb::kBufLen);
  float in[kN] = {1}, l[kN] = {0}, rr[kN] = {0};
  r.process(in, l, rr, kN, 127, 0, 127, false);
  in[0] = 0;
  for (int b = 0; b < kSynthRate * 30 / kN; ++b) r.process(in, l, rr, kN, 127, 0, 127, false);  // 30 s
  float ref[kN];
  for (int i = 0; i < kN; ++i) l[i] = rr[i] = ref[i] = 0.25f;
  r.process(in, l, rr, kN, 127, 0, 127, false);
  TEST_ASSERT_EQUAL_MEMORY(ref, l, sizeof(ref));
  TEST_ASSERT_EQUAL_MEMORY(ref, rr, sizeof(ref));
  in[5] = 1;
  r.process(in, l, rr, kN, 127, 0, 127, false);
  in[5] = 0;
  bool anyL = false, anyR = false;
  for (int b = 0; b < 20; ++b) {
    float tl[kN] = {0}, tr[kN] = {0};
    r.process(in, tl, tr, kN, 127, 0, 127, false);
    for (int i = 0; i < kN; ++i) {
      anyL |= tl[i] != 0;
      anyR |= tr[i] != 0;
    }
  }
  TEST_ASSERT_TRUE(anyL);
  TEST_ASSERT_TRUE(anyR);
}

// FREEZE: the lines loop without loss or input; 3 s later the tail is as loud as right after the
// burst (within 1 dB).
void test_freeze_holds_tail() {
  Reverb r;
  r.setBuffer(buf, Reverb::kBufLen);
  uint32_t seed = 1;
  for (int b = 0; b < 32; ++b) {  // 128 ms of noise
    float in[kN], l[kN] = {0}, rr[kN] = {0};
    for (int i = 0; i < kN; ++i) {
      seed = seed * 1664525u + 1013904223u;
      in[i] = static_cast<int32_t>(seed) * (0.5f / 2147483648.f);
    }
    r.process(in, l, rr, kN, 80, 60, 127, false);
  }
  const int win = 4096;
  auto rms = [&](bool burstIn) {
    double sum = 0;
    for (int b = 0; b < win / kN; ++b) {
      float in[kN], l[kN] = {0}, rr[kN] = {0};
      for (int i = 0; i < kN; ++i) in[i] = burstIn ? 1.f : 0.f;  // frozen: the input is ignored
      r.process(in, l, rr, kN, 80, 60, 127, true);
      for (int i = 0; i < kN; ++i) sum += static_cast<double>(l[i]) * l[i] + static_cast<double>(rr[i]) * rr[i];
    }
    return sqrt(sum / (2.0 * win));
  };
  const double first = rms(false);
  TEST_ASSERT_TRUE(first > 1e-3);
  for (int b = 0; b < kRate * 3 / kN; ++b) {
    float in[kN] = {0}, l[kN] = {0}, rr[kN] = {0};
    if (b % 50 == 0) in[3] = 1.f;  // ignored while frozen
    r.process(in, l, rr, kN, 80, 60, 127, true);
  }
  const double later = rms(true);
  const double db = 20.0 * log10(later / first);
  TEST_ASSERT_TRUE(db > -1.0 && db < 1.0);
  // Released: the tail decays again.
  for (int b = 0; b < kRate * 3 / kN; ++b) {
    float in[kN] = {0}, l[kN] = {0}, rr[kN] = {0};
    r.process(in, l, rr, kN, 80, 60, 127, false);
  }
  float in[kN] = {0}, l[kN] = {0}, rr[kN] = {0};
  r.process(in, l, rr, kN, 80, 60, 127, false);
  for (int i = 0; i < kN; ++i) TEST_ASSERT_TRUE(fabsf(l[i]) < first * 0.01);
}

void test_parameter_transitions_partition_exact() {
  constexpr int count = 4096;
  static float in[count], al[count], ar[count], bl[count], br[count];
  static float ba[Reverb::kBufLen], bb[Reverb::kBufLen];
  uint32_t rng = 9;
  for (int i = 0; i < count; ++i) {
    rng = rng * 1664525u + 1013904223u;
    in[i] = static_cast<int32_t>(rng) * (.25f / 2147483648.f);
    al[i] = ar[i] = bl[i] = br[i] = 0;
  }
  const int boundaries[] = {0, 1200, 1240, 1270, 1790, 2200, 2500, count};
  const uint8_t sizes[] = {40, 127, 0, 83, 16, 123, 50};
  const uint8_t levels[] = {127, 27, 127, 94, 0, 50, 127};
  auto render = [&](Reverb& r, float* l, float* rr, bool split) {
    for (int phase = 0; phase < 7; ++phase) {
      for (int pos = boundaries[phase]; pos < boundaries[phase + 1];) {
        const int room = boundaries[phase + 1] - pos;
        const int n = split && room > 17 ? 17 : room;
        r.process(in + pos, l + pos, rr + pos, n, sizes[phase], phase * 17,
                  levels[phase], phase == 3);
        pos += n;
      }
    }
  };
  Reverb a, b; a.setBuffer(ba, Reverb::kBufLen); b.setBuffer(bb, Reverb::kBufLen);
  render(a, al, ar, false); render(b, bl, br, true);
  TEST_ASSERT_EQUAL_MEMORY(al, bl, sizeof(al));
  TEST_ASSERT_EQUAL_MEMORY(ar, br, sizeof(ar));
}

void test_completed_mute_cannot_resurrect_tail() {
  Reverb r; r.setBuffer(buf, Reverb::kBufLen);
  for (int i = 0; i < 10000; ++i) {
    const float x = sinf(i * .37f); float l = 0, rr = 0;
    r.process(&x, &l, &rr, 1, 127, 0, 127, false);
  }
  for (int i = 0; i < 300; ++i) {
    const float x = 1; float l = 0, rr = 0;
    r.process(&x, &l, &rr, 1, 0, 0, 0, false);
  }
  TEST_ASSERT_TRUE(r.muted());
  for (int i = 0; i < 6000; ++i) {
    const float x = 0; float l = .125f, rr = -.25f;
    r.process(&x, &l, &rr, 1, 127, 0, 127, false);
    TEST_ASSERT_EQUAL_FLOAT(.125f, l); TEST_ASSERT_EQUAL_FLOAT(-.25f, rr);
  }
}

void test_freeze_ignores_input_and_size_edits() {
  Reverb a, b;
  static float ba[Reverb::kBufLen], bb[Reverb::kBufLen];
  a.setBuffer(ba, Reverb::kBufLen); b.setBuffer(bb, Reverb::kBufLen);
  for (int i = 0; i < 10000; ++i) {
    const float x = sinf(i * .37f); float al = 0, ar = 0, bl = 0, br = 0;
    a.process(&x, &al, &ar, 1, 60, 63, 127, false);
    b.process(&x, &bl, &br, 1, 60, 63, 127, false);
  }
  for (int i = 0; i < 10000; ++i) {
    const float x = 0, ignored = 1; float al = 0, ar = 0, bl = 0, br = 0;
    a.process(&x, &al, &ar, 1, 60, 63, 127, true);
    b.process(&ignored, &bl, &br, 1, (i / 53) % 128, 0, 127, true);
    TEST_ASSERT_EQUAL_FLOAT(al, bl); TEST_ASSERT_EQUAL_FLOAT(ar, br);
  }
}

void test_clamp_clear_and_detach() {
  Reverb a, b;
  static float ba[Reverb::kBufLen], bb[Reverb::kBufLen];
  a.setBuffer(ba, Reverb::kBufLen); b.setBuffer(bb, Reverb::kBufLen);
  for (int i = 0; i < 5000; ++i) {
    const float x = sinf(i * .37f); float al = 0, ar = 0, bl = 0, br = 0;
    a.process(&x, &al, &ar, 1, 200, 200, 200, false);
    b.process(&x, &bl, &br, 1, 127, 127, 127, false);
    TEST_ASSERT_EQUAL_FLOAT(al, bl); TEST_ASSERT_EQUAL_FLOAT(ar, br);
  }
  a.clear(); b.setBuffer(bb, Reverb::kBufLen);
  for (int i = 0; i < 3000; ++i) {
    const float x = i == 0 ? 1 : 0; float al = 0, ar = 0, bl = 0, br = 0;
    a.process(&x, &al, &ar, 1, 30, 30, 100, false);
    b.process(&x, &bl, &br, 1, 30, 30, 100, false);
    TEST_ASSERT_EQUAL_FLOAT(al, bl); TEST_ASSERT_EQUAL_FLOAT(ar, br);
  }
  a.setBuffer(nullptr, 0); TEST_ASSERT_TRUE(a.muted());
  const float x = 1; float l = .25f, rr = -.25f;
  a.process(&x, &l, &rr, 1, 127, 127, 127, false);
  TEST_ASSERT_EQUAL_FLOAT(.25f, l); TEST_ASSERT_EQUAL_FLOAT(-.25f, rr);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_silent_without_buffer);
  RUN_TEST(test_impulse_tail_decays);
  RUN_TEST(test_size_lengthens_tail);
  RUN_TEST(test_stereo_impulse_both_sides_differ);
  RUN_TEST(test_diffusion_has_unit_magnitude);
  RUN_TEST(test_level_change_starts_smoothly);
  RUN_TEST(test_size_change_crossfades_old_read);
  RUN_TEST(test_level_zero_fades_then_is_exact);
  RUN_TEST(test_stereo_level_zero_untouched);
  RUN_TEST(test_stereo_idle_after_tail);
  RUN_TEST(test_freeze_holds_tail);
  RUN_TEST(test_parameter_transitions_partition_exact);
  RUN_TEST(test_completed_mute_cannot_resurrect_tail);
  RUN_TEST(test_freeze_ignores_input_and_size_edits);
  RUN_TEST(test_clamp_clear_and_detach);
  return UNITY_END();
}
