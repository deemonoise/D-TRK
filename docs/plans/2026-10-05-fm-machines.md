# FM-машины — план реализации

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** третий тип инструмента FM — 8 машин в стиле Model:Cycles (KICK, SNARE, METAL, PERC, TONE, CHORD, CLAP, HAT) с макросами DECAY / COLOR / SHAPE / SWEEP / CONTOUR, p-lock через fx и LFO.

**Architecture:** общее 4-op FM-ядро (`synth_fm`) + SVF (`synth_filter`) + чистые функции-машины `macros → FmParams` (`synth_fm_machines`), считаются на control-rate (1 мс) внутри `Synth::control`. Модель: `InstrType::Fm`, поля в `Instrument`, чанк `FMIN`. Локи — `TrackRt`/`Voice` по образцу существующих synth fx.

**Tech Stack:** C++17, PlatformIO, Unity (native-тесты), ESP32-S3 Arduino, LovyanGFX.

**Дизайн:** [2026-10-05-fm-machines-design.md](2026-10-05-fm-machines-design.md).

**Правила проекта:** не коммитить (шаги Commit пропущены — пользователь коммитит сам). Все этапы подряд, без остановок на проверку железа; сводка в конце.

**Команды:**
- один тест: `pio test -e native -f test_synth_fm`
- все тесты: `pio test -e native`
- прошивка: `pio run -e wt32`

---

### Task 1: SVF-фильтр

**Files:**
- Create: `lib/core/src/synth_filter.h`
- Test: `test/test_synth_filter/test_main.cpp`

**Step 1: тест**

```cpp
#include <math.h>
#include <unity.h>
#include "synth_filter.h"

using namespace mt;

void setUp() {}
void tearDown() {}

// RMS of a sine at hz through the filter, after the transient.
static float rmsThrough(Svf::Mode m, float cutoff, float q, float hz) {
  Svf f;
  f.set(m, cutoff, q);
  double acc = 0;
  const int n = kSynthRate / 4;
  for (int i = 0; i < n; ++i) {
    const float y = f.process(sinf(6.2831853f * hz * i / kSynthRate));
    if (i >= n / 2) acc += y * y;
  }
  return sqrtf(static_cast<float>(acc / (n / 2)));
}

void test_lp_passes_low_cuts_high() {
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.707f, rmsThrough(Svf::Mode::Lp, 1000, 0.707f, 100));
  TEST_ASSERT_TRUE(rmsThrough(Svf::Mode::Lp, 1000, 0.707f, 8000) < 0.05f);
}

void test_hp_passes_high_cuts_low() {
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.707f, rmsThrough(Svf::Mode::Hp, 1000, 0.707f, 8000));
  TEST_ASSERT_TRUE(rmsThrough(Svf::Mode::Hp, 1000, 0.707f, 100) < 0.05f);
}

void test_bp_peaks_at_cutoff() {
  const float at = rmsThrough(Svf::Mode::Bp, 1000, 2, 1000);
  TEST_ASSERT_TRUE(at > rmsThrough(Svf::Mode::Bp, 1000, 2, 200));
  TEST_ASSERT_TRUE(at > rmsThrough(Svf::Mode::Bp, 1000, 2, 5000));
}

void test_stable_at_extremes() {
  Svf f;
  f.set(Svf::Mode::Lp, 1e6f, 100);  // clamped below Nyquist
  float y = 0;
  for (int i = 0; i < kSynthRate; ++i) y = f.process((i & 1) ? 1.f : -1.f);
  TEST_ASSERT_FALSE(isnan(y) || isinf(y));
  TEST_ASSERT_TRUE(fabsf(y) < 200);
  f.set(Svf::Mode::Lp, 0, 0);  // clamped to 20 Hz, q 0.5
  for (int i = 0; i < 1000; ++i) y = f.process(1);
  TEST_ASSERT_FALSE(isnan(y));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_lp_passes_low_cuts_high);
  RUN_TEST(test_hp_passes_high_cuts_low);
  RUN_TEST(test_bp_peaks_at_cutoff);
  RUN_TEST(test_stable_at_extremes);
  return UNITY_END();
}
```

**Step 2:** `pio test -e native -f test_synth_filter` — FAIL (нет `synth_filter.h`).

**Step 3: реализация**

```cpp
#pragma once
#include <math.h>
#include <stdint.h>
#include "synth_osc.h"

namespace mt {

// State-variable filter (Simper, trapezoidal integration): stable under fast cutoff changes.
// set() at control rate (tanf), process() per sample.
struct Svf {
  enum class Mode : uint8_t { Lp, Bp, Hp };

  // hz is clamped to 20 Hz .. 0.45 x the sample rate, q to >= 0.5.
  void set(Mode m, float hz, float q) {
    mode_ = m;
    const float maxHz = kSynthRate * 0.45f;
    hz = hz < 20.f ? 20.f : (hz > maxHz ? maxHz : hz);
    q = q < 0.5f ? 0.5f : q;
    const float g = tanf(3.14159265f * hz / kSynthRate);
    k_ = 1.f / q;
    a1_ = 1.f / (1.f + g * (g + k_));
    a2_ = g * a1_;
    a3_ = g * a2_;
  }
  void reset() { ic1_ = ic2_ = 0; }
  float process(float v0) {
    const float v3 = v0 - ic2_;
    const float v1 = a1_ * ic1_ + a2_ * v3;
    const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
    ic1_ = 2.f * v1 - ic1_;
    ic2_ = 2.f * v2 - ic2_;
    switch (mode_) {
      case Mode::Lp: return v2;
      case Mode::Bp: return v1;
      default: return v0 - k_ * v1 - v2;
    }
  }

 private:
  Mode mode_ = Mode::Lp;
  float ic1_ = 0, ic2_ = 0;
  float a1_ = 0, a2_ = 0, a3_ = 0, k_ = 2;
};

}  // namespace mt
```

**Step 4:** `pio test -e native -f test_synth_filter` — PASS.

---

### Task 2: модель — тип FM, машины, макросы, LFO

**Files:**
- Modify: `lib/core/src/model.h` (enum `InstrType`, `struct Instrument`, объявления после `envTimeMs`)
- Modify: `lib/core/src/model.cpp` (реализация рядом с `envTimeMs`)
- Test: `test/test_model/test_main.cpp`

**Step 1: тесты** (добавить в `test/test_model/test_main.cpp` и в `main()`)

```cpp
void test_fm_decay_ms_range() {
  TEST_ASSERT_EQUAL(5, fmDecayMs(0));
  TEST_ASSERT_EQUAL(4000, fmDecayMs(127));
  for (int v = 1; v < 128; ++v) TEST_ASSERT_TRUE(fmDecayMs(v) >= fmDecayMs(v - 1));
}

void test_lfo_hz_range() {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.05f, lfoHz(0));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 30.f, lfoHz(127));
}

void test_fm_gated_machines() {
  TEST_ASSERT_TRUE(fmGated(static_cast<uint8_t>(FmMachine::Tone)));
  TEST_ASSERT_TRUE(fmGated(static_cast<uint8_t>(FmMachine::Chord)));
  TEST_ASSERT_FALSE(fmGated(static_cast<uint8_t>(FmMachine::Kick)));
  TEST_ASSERT_FALSE(fmGated(static_cast<uint8_t>(FmMachine::Hat)));
  TEST_ASSERT_FALSE(fmGated(200));  // out of range = Kick
}

void test_fm_set_machine_defaults() {
  Instrument m;
  TEST_ASSERT_EQUAL(static_cast<int>(FmMachine::Kick), m.machine);
  m.macro[kMacCol] = 1;
  fmSetMachine(m, static_cast<uint8_t>(FmMachine::Chord));
  TEST_ASSERT_EQUAL(static_cast<int>(FmMachine::Chord), m.machine);
  TEST_ASSERT_EQUAL(0, m.macro[kMacShp]);  // chord type maj
  TEST_ASSERT_TRUE(m.macro[kMacCol] != 1);
  fmSetMachine(m, 99);
  TEST_ASSERT_EQUAL(static_cast<int>(FmMachine::Count) - 1, m.machine);
}
```

**Step 2:** `pio test -e native -f test_model` — FAIL (нет символов).

**Step 3: реализация**

`model.h` — тип и перечисления (после `enum class LoopMode`):

```cpp
enum class InstrType : uint8_t { Chip, Sample, Fm, Count };
// FM machines (Model:Cycles style). Stored in files: new machines go before Count only.
enum class FmMachine : uint8_t { Kick, Snare, Metal, Perc, Tone, Chord, Clap, Hat, Count };
// FM macros: Instrument::macro index, fx DEC..CON = Fx::DCY + index.
enum FmMacro : uint8_t { kMacDec, kMacCol, kMacShp, kMacSwp, kMacCon, kFmMacros };
enum class LfoWave : uint8_t { Sine, Tri, Saw, Square, Random, Count };
// Dec..Con = macro index + 1.
enum class LfoDest : uint8_t { Pitch, Dec, Col, Shp, Swp, Con, Vol, Count };
```

(старую строку `enum class InstrType` заменить). В конец `struct Instrument`:

```cpp
  // FM: machine and its macros (see fmSetMachine), LFO.
  uint8_t machine = 0;                                // FmMachine
  uint8_t macro[kFmMacros] = {85, 40, 32, 70, 50};    // DECAY..CONTOUR 0..127, Kick defaults
  uint8_t lfoWave = 0;   // LfoWave
  uint8_t lfoRate = 64;  // 0..127, see lfoHz
  int8_t lfoDepth = 0;   // -64..63, 0 = off
  uint8_t lfoDest = 0;   // LfoDest
```

После `uint16_t envTimeMs(uint8_t v);`:

```cpp
// FM DECAY: 0..127 -> 5..4000 ms exponentially (time to -60 dB).
uint16_t fmDecayMs(uint8_t v);
// LFO rate: 0..127 -> 0.05..30 Hz exponentially.
float lfoHz(uint8_t v);
// TONE, CHORD: held while the note is, with the instrument's attack / sustain / release.
// The other machines are one-shot drums. Out of range = Kick.
bool fmGated(uint8_t machine);
// Sets the machine (clamped) and its default macros.
void fmSetMachine(Instrument& m, uint8_t machine);
```

`model.cpp` (добавить `#include <math.h>` если нет):

```cpp
uint16_t fmDecayMs(uint8_t v) {
  if (v > 127) v = 127;
  return static_cast<uint16_t>(5.f * powf(800.f, v / 127.f) + 0.5f);
}

float lfoHz(uint8_t v) {
  if (v > 127) v = 127;
  return 0.05f * powf(600.f, v / 127.f);
}

bool fmGated(uint8_t machine) {
  return machine == static_cast<uint8_t>(FmMachine::Tone) || machine == static_cast<uint8_t>(FmMachine::Chord);
}

void fmSetMachine(Instrument& m, uint8_t machine) {
  // DECAY, COLOR, SHAPE, SWEEP, CONTOUR per machine: sounds right away.
  static const uint8_t kDefaults[static_cast<int>(FmMachine::Count)][kFmMacros] = {
      {85, 40, 32, 70, 50},  // Kick
      {70, 64, 40, 30, 64},  // Snare
      {90, 64, 0, 0, 64},    // Metal
      {65, 50, 32, 40, 40},  // Perc
      {64, 40, 0, 0, 64},    // Tone: sine
      {64, 30, 0, 0, 64},    // Chord: maj
      {70, 64, 40, 40, 64},  // Clap
      {40, 64, 90, 64, 64},  // Hat: closed
  };
  constexpr int kLast = static_cast<int>(FmMachine::Count) - 1;
  m.machine = static_cast<uint8_t>(machine > kLast ? kLast : machine);
  for (int k = 0; k < kFmMacros; ++k) m.macro[k] = kDefaults[m.machine][k];
}
```

**Step 4:** `pio test -e native -f test_model` — PASS. `pio test -e native` — все зелёные (тип с `Count` = 3: `test_audio_garbage_clamped` с `type = 0xFF` по-прежнему даёт Chip).

---

### Task 3: fx DEC, COL, SHP, SWP, CON

**Files:**
- Modify: `lib/core/src/model.h` (`enum class Fx`)
- Modify: `lib/core/src/fx_info.cpp` (`kInfo`), `lib/core/src/fx_info.h` (комментарий `fxNextCmd`)
- Test: `test/test_fx_info/test_main.cpp`, `test/test_expand/test_main.cpp`

**Step 1: тесты**

В `test_fx_info`: в `test_cmd_cycle` заменить две проверки на

```cpp
  TEST_ASSERT_TRUE(fxNextCmd(Fx::None, -1) == Fx::CON);
  TEST_ASSERT_TRUE(fxNextCmd(Fx::CUT, 1) == Fx::DCY);
  TEST_ASSERT_TRUE(fxNextCmd(Fx::CON, 1) == Fx::None);
```

и добавить

```cpp
void test_fm_lock_fx() {
  TEST_ASSERT_EQUAL(22, static_cast<int>(Fx::DCY));  // file values follow CUT
  TEST_ASSERT_EQUAL(static_cast<int>(Fx::DCY) + kMacCon, static_cast<int>(Fx::CON));
  TEST_ASSERT_EQUAL_STRING("DEC", fxName(Fx::DCY));
  TEST_ASSERT_EQUAL_STRING("COL", fxName(Fx::COL));
  TEST_ASSERT_EQUAL_STRING("SHP", fxName(Fx::SHP));
  TEST_ASSERT_EQUAL_STRING("SWP", fxName(Fx::SWP));
  TEST_ASSERT_EQUAL_STRING("CON", fxName(Fx::CON));
  TEST_ASSERT_EQUAL(64, fxDefault(Fx::COL));
  TEST_ASSERT_EQUAL(127, fxStep(Fx::SHP, 120, 100));
  TEST_ASSERT_EQUAL(0, fxStep(Fx::SHP, 3, -10));
  TEST_ASSERT_EQUAL_STRING(" 99", fmt(Fx::DCY, 99));
  TEST_ASSERT_TRUE(fxSynthOnly(Fx::DCY));
  TEST_ASSERT_TRUE(fxSynthOnly(Fx::CON));
}
```

В `test_expand` добавить

```cpp
void test_fm_lock_fx_on_int_track() {
  track.out = TrackOut::Int;
  Step s = note(60);
  s.fx[0] = {Fx::COL, 99};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, ctx, rng, out));
  TEST_ASSERT_TRUE(out.ev[0].kind == EvKind::SynthFx);
  TEST_ASSERT_EQUAL(static_cast<int>(Fx::COL), out.ev[0].note);
  TEST_ASSERT_EQUAL(99, out.ev[0].vel);
}
```

**Step 2:** `pio test -e native -f test_fx_info` — FAIL (нет `Fx::DCY`).

**Step 3: реализация**

`model.h`:

```cpp
// SLD..CON act on INT tracks only (synth fx, see fxSynthOnly). DEC..CON lock FM macros
// (Fx::DCY + FmMacro).
enum class Fx : uint8_t {
  None = 0, CHN, RAT, PRB, GAT, TIE, NDG, CHD, STR, CND, VRN, NRN, CCA, CCB, PBN, PGM,
  SLD, VIB, ARP, VSL, OFS, CUT, DCY, COL, SHP, SWP, CON, Count
};
```

`fx_info.cpp`, после строки `CUT`:

```cpp
    {"DEC", 0, 127, 64, false},  // DEC..CON: FM macro locks
    {"COL", 0, 127, 64, false},
    {"SHP", 0, 127, 64, false},
    {"SWP", 0, 127, 64, false},
    {"CON", 0, 127, 64, false},
```

`fx_info.h`: комментарии `// cycles None..CON`, `// SLD..CON: INT tracks only, ignored on MIDI`.

**Step 4:** `pio test -e native -f test_fx_info` и `-f test_expand` — PASS.

---

### Task 4: FM-ядро

**Files:**
- Create: `lib/core/src/synth_fm.h`, `lib/core/src/synth_fm.cpp`
- Test: `test/test_synth_fm/test_main.cpp`

**Step 1: тест**

```cpp
#include <math.h>
#include <unity.h>
#include "synth_fm.h"

using namespace mt;

void setUp() {}
void tearDown() {}

constexpr int kSpan = 32;  // control period, as Synth::kControl

// Renders n samples, calling control() every kSpan samples like the synth does.
static void run(FmVoice& v, const FmParams& p, float* out, int n) {
  for (int i = 0; i < n; ++i) out[i] = 0;
  for (int pos = 0; pos < n; pos += kSpan) {
    v.control(p, kSpan);
    v.render(out + pos, n - pos < kSpan ? n - pos : kSpan, 1.f);
  }
}

static FmParams sine(float hz) {
  FmParams p;
  p.alg = FmAlg::Stack;
  p.hz = hz;
  p.op[0].level = 1;
  p.oneShot = false;
  return p;
}

static int crossings(const float* b, int n) {
  int c = 0;
  for (int i = 1; i < n; ++i) c += b[i - 1] >= 0 && b[i] < 0;
  return c;
}

static float buf[kSynthRate];

void test_carrier_frequency() {
  FmVoice v;
  v.trigger(false);
  run(v, sine(1000), buf, kSynthRate);
  TEST_ASSERT_INT_WITHIN(2, 1000, crossings(buf, kSynthRate));
}

void test_zero_index_is_pure_sine() {
  FmVoice v;
  v.trigger(false);
  FmParams p = sine(500);
  p.op[1].ratio = 1;  // modulator present, level 0
  run(v, p, buf, 2000);
  for (int i = 0; i < 2000; ++i) TEST_ASSERT_FLOAT_WITHIN(0.005f, sinf(6.2831853f * 500 * i / kSynthRate), buf[i]);
}

void test_modulation_changes_wave() {
  FmVoice v;
  v.trigger(false);
  FmParams p = sine(500);
  p.op[1].level = 3;
  run(v, p, buf, 2000);
  float diff = 0;
  for (int i = 0; i < 2000; ++i) diff += fabsf(buf[i] - sinf(6.2831853f * 500 * i / kSynthRate));
  TEST_ASSERT_TRUE(diff / 2000 > 0.1f);
}

void test_carriers_by_algorithm() {
  TEST_ASSERT_TRUE(fmCarrier(FmAlg::Stack, 0));
  TEST_ASSERT_FALSE(fmCarrier(FmAlg::Stack, 1));
  TEST_ASSERT_TRUE(fmCarrier(FmAlg::TwoPairs, 2));
  TEST_ASSERT_FALSE(fmCarrier(FmAlg::TwoPairs, 3));
  TEST_ASSERT_TRUE(fmCarrier(FmAlg::OneToThree, 2));
  TEST_ASSERT_FALSE(fmCarrier(FmAlg::OneToThree, 3));
  TEST_ASSERT_TRUE(fmCarrier(FmAlg::Additive, 3));
  // A modulator alone is silent; a carrier alone sounds.
  FmVoice v;
  v.trigger(false);
  FmParams p = sine(500);
  p.op[0].level = 0;
  p.op[2].level = 1;
  run(v, p, buf, 1000);
  for (int i = 0; i < 1000; ++i) TEST_ASSERT_EQUAL_FLOAT(0, buf[i]);
  p.alg = FmAlg::TwoPairs;
  FmVoice w;
  w.trigger(false);
  run(w, p, buf, 1000);
  TEST_ASSERT_TRUE(crossings(buf, 1000) > 10);
}

void test_one_shot_decays_and_ends() {
  FmVoice v;
  v.trigger(false);
  FmParams p = sine(200);
  p.oneShot = true;
  p.ampMs = 50;
  run(v, p, buf, 160);  // 5 ms
  float peak = 0;
  for (int i = 0; i < 160; ++i) peak = fmaxf(peak, fabsf(buf[i]));
  TEST_ASSERT_TRUE(peak > 0.5f);
  TEST_ASSERT_FALSE(v.done());
  run(v, p, buf, 3200);  // +100 ms
  TEST_ASSERT_TRUE(v.done());
}

void test_pitch_envelope_starts_high() {
  FmVoice v;
  v.trigger(false);
  FmParams p = sine(500);
  p.pitchEnv = 12;
  p.pitchMs = 300;
  run(v, p, buf, kSynthRate);
  const int early = crossings(buf, 640);                  // first 20 ms
  const int late = crossings(buf + kSynthRate - 640, 640);  // last 20 ms
  TEST_ASSERT_TRUE(early > late * 3 / 2);
}

void test_above_nyquist_is_silent() {
  FmVoice v;
  v.trigger(false);
  run(v, sine(20000), buf, 1000);
  for (int i = 0; i < 1000; ++i) TEST_ASSERT_EQUAL_FLOAT(0, buf[i]);
}

void test_max_index_falls_with_pitch() {
  TEST_ASSERT_EQUAL_FLOAT(8, fmMaxIndex(100));
  TEST_ASSERT_TRUE(fmMaxIndex(6000) < 1.5f);
  TEST_ASSERT_TRUE(fmMaxIndex(20000) >= 0.2f);
}

void test_feedback_bounded() {
  FmVoice v;
  v.trigger(false);
  FmParams p = sine(300);
  p.op[0].fb = 20;
  run(v, p, buf, 4000);
  for (int i = 0; i < 4000; ++i) TEST_ASSERT_TRUE(fabsf(buf[i]) <= 1.001f);
}

void test_noise_filter_modes() {
  FmParams p;
  p.oneShot = false;
  p.noise = 1;
  p.filterMode = Svf::Mode::Lp;
  p.filterHz = 300;
  FmVoice lp;
  lp.trigger(false);
  run(lp, p, buf, 8000);
  const int lpc = crossings(buf, 8000);
  p.filterMode = Svf::Mode::Hp;
  p.filterHz = 6000;
  FmVoice hp;
  hp.trigger(false);
  run(hp, p, buf, 8000);
  TEST_ASSERT_TRUE(crossings(buf, 8000) > lpc * 3);
}

void test_clap_bursts_retrigger_noise() {
  FmParams p;
  p.oneShot = false;
  p.noise = 1;
  p.bursts = 3;
  p.burstMs = 10;
  p.noiseMs = 100;
  p.tail = 0.5f;
  FmVoice v;
  v.trigger(false);
  run(v, p, buf, 1600);  // 50 ms
  // Energy just after the 2nd burst start is above the energy just before it.
  float before = 0, after = 0;
  for (int i = 280; i < 320; ++i) before += fabsf(buf[i]);
  for (int i = 330; i < 370; ++i) after += fabsf(buf[i]);
  TEST_ASSERT_TRUE(after > before * 2);
}

void test_choke_retrigger_has_no_jump() {
  FmVoice v;
  v.trigger(false);
  FmParams p = sine(200);
  p.oneShot = true;
  p.ampMs = 2000;
  run(v, p, buf, 1000);
  const float last = buf[999];
  v.trigger(true);
  run(v, p, buf, 1);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, last, buf[0]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_carrier_frequency);
  RUN_TEST(test_zero_index_is_pure_sine);
  RUN_TEST(test_modulation_changes_wave);
  RUN_TEST(test_carriers_by_algorithm);
  RUN_TEST(test_one_shot_decays_and_ends);
  RUN_TEST(test_pitch_envelope_starts_high);
  RUN_TEST(test_above_nyquist_is_silent);
  RUN_TEST(test_max_index_falls_with_pitch);
  RUN_TEST(test_feedback_bounded);
  RUN_TEST(test_noise_filter_modes);
  RUN_TEST(test_clap_bursts_retrigger_noise);
  RUN_TEST(test_choke_retrigger_has_no_jump);
  return UNITY_END();
}
```

**Step 2:** `pio test -e native -f test_synth_fm` — FAIL.

**Step 3: реализация**

`synth_fm.h`:

```cpp
#pragma once
#include <stdint.h>
#include "synth_filter.h"

namespace mt {

constexpr int kFmOps = 4;

// Operator routing, op1 = index 0; ">" = modulates.
enum class FmAlg : uint8_t {
  Stack,       // 4>3>2>1, out 1
  TwoToOne,    // 4>3>1, 2>1, out 1
  TwoPairs,    // 4>3, 2>1, out 1 + 3
  OneToThree,  // 4>1, 4>2, 4>3, out 1 + 2 + 3
  Additive,    // out 1 + 2 + 3 + 4
  Count
};
bool fmCarrier(FmAlg a, int op);
// Largest modulation index (radians) of a modulator at hz: keeps the sidebands below ~12 kHz.
float fmMaxIndex(float hz);

// Times are to -60 dB, ms; 0 = no decay.
struct FmOp {
  float ratio = 1;    // x FmParams::hz
  float level = 0;    // carrier: gain; modulator: index, radians
  float sustain = 0;  // fraction of level the decay ends at, 0..1
  float decayMs = 0;
  float fb = 0;       // self-modulation, radians
};

// One FM voice's sound, made by a machine (synth_fm_machines) at control rate.
struct FmParams {
  FmAlg alg = FmAlg::Additive;
  FmOp op[kFmOps];
  float hz = 0;          // base frequency
  float pitchEnv = 0;    // semitones at the trigger, decaying to 0
  float pitchMs = 0;
  float fbEnv = 0;       // feedback x (1 + fbEnv) at the trigger, decaying
  float fbMs = 0;
  float noise = 0;       // white noise level
  float noiseMs = 0;     // 0 = flat
  uint8_t bursts = 0;    // noise retriggers before the decay (clap)
  float burstMs = 0;
  float tail = 1;        // noise level after the bursts
  Svf::Mode filterMode = Svf::Mode::Lp;
  float filterHz = 0;    // 0 = no filter
  float filterQ = 0.707f;
  bool filterAll = false;  // filter the operators too, else the noise only
  bool oneShot = true;     // own amplitude envelope; false = held at 1 (the caller's ADSR shapes it)
  float ampMs = 0;
};

// Runtime of one FM voice. control() takes the values at the current time and ramps them
// linearly to their values span samples later; render() advances per sample.
class FmVoice {
 public:
  // keepPhase: retrigger of a sounding voice (choke): oscillators run on and the amplitude
  // ramps from where it is, so there is no click.
  void trigger(bool keepPhase);
  void control(const FmParams& p, int span);
  // Adds n samples x gain to out.
  void render(float* out, int n, float gain);
  // One-shot sound has decayed to silence.
  bool done() const { return oneShot_ && t_ > kAttack && amp_ < 1e-4f; }

 private:
  static constexpr uint32_t kAttack = 16;  // one-shot attack, samples (0.5 ms)
  template <FmAlg A>
  void run(float* out, int n, float gain);
  float op(int i, float mod);
  float noiseAt(const FmParams& p, float t) const;
  float ampAt(const FmParams& p, float t) const;

  uint32_t ph_[kFmOps] = {0};
  float inc_[kFmOps] = {0}, dInc_[kFmOps] = {0};  // phase units (2^32 = cycle) per sample
  float lvl_[kFmOps] = {0}, dLvl_[kFmOps] = {0};  // modulators: in cycles
  float fb_[kFmOps] = {0};                         // cycles
  float last_[kFmOps] = {0}, fbIn_[kFmOps] = {0};  // feedback: mean of the last two outputs
  float noise_ = 0, dNoise_ = 0;
  float amp_ = 0, dAmp_ = 0, ampStart_ = 0;
  uint32_t rng_ = 0x12345678;
  uint32_t t_ = 0;  // samples since the trigger
  FmAlg alg_ = FmAlg::Additive;
  bool oneShot_ = false, filterOn_ = false, filterAll_ = false;
  Svf svf_;
};

}  // namespace mt
```

`synth_fm.cpp`:

```cpp
#include "synth_fm.h"
#include <math.h>

namespace mt {
namespace {

constexpr int kSineBits = 10;
constexpr int kSineLen = 1 << kSineBits;
float gSine[kSineLen + 1];  // one cycle + guard point; internal RAM
struct SineInit {
  SineInit() {
    for (int i = 0; i <= kSineLen; ++i) gSine[i] = sinf(6.2831853f * i / kSineLen);
  }
} sineInit;

constexpr float kInvTwoPi = 1.f / 6.2831853f;
constexpr float kIncPerHz = 4294967296.f / kSynthRate;
constexpr float kMaxInc = 0.45f * 4294967296.f;  // 0.45 x sample rate

float sine(uint32_t ph) {
  const uint32_t i = ph >> (32 - kSineBits);
  const float f = (ph & ((1u << (32 - kSineBits)) - 1)) * (1.f / (1u << (32 - kSineBits)));
  const float a = gSine[i];
  return a + (gSine[i + 1] - a) * f;
}

// Phase offset in cycles -> phase units, |c| < 2048.
uint32_t cycles(float c) { return static_cast<uint32_t>(static_cast<int32_t>(c * 1048576.f)) << 12; }

// Exponential decay to -60 dB in ms; t in samples.
float decayAt(float ms, float t) {
  if (ms <= 0) return 1;
  return expf(-6.9078f * t / (ms * (kSynthRate / 1000.f)));
}

float opEnv(const FmOp& o, float t) {
  const float s = o.sustain < 0 ? 0 : (o.sustain > 1 ? 1 : o.sustain);
  return s + (1.f - s) * decayAt(o.decayMs, t);
}

}  // namespace

bool fmCarrier(FmAlg a, int op) {
  switch (a) {
    case FmAlg::Stack:
    case FmAlg::TwoToOne: return op == 0;
    case FmAlg::TwoPairs: return op == 0 || op == 2;
    case FmAlg::OneToThree: return op < 3;
    default: return true;
  }
}

float fmMaxIndex(float hz) {
  if (hz <= 0) return 8;
  const float m = 12000.f / hz - 1.f;
  return m < 0.2f ? 0.2f : (m > 8.f ? 8.f : m);
}

void FmVoice::trigger(bool keepPhase) {
  ampStart_ = keepPhase ? amp_ : 0;
  t_ = 0;
  if (keepPhase) return;
  for (int i = 0; i < kFmOps; ++i) ph_[i] = 0, last_[i] = fbIn_[i] = 0;
  amp_ = 0;
  svf_.reset();
}

float FmVoice::noiseAt(const FmParams& p, float t) const {
  const float burst = p.burstMs * (kSynthRate / 1000.f);
  if (p.bursts && burst > 0) {
    const float span = p.bursts * burst;
    if (t < span) {
      const float in = t - burst * static_cast<int>(t / burst);
      return expf(-in / (burst * 0.25f));
    }
    return p.tail * decayAt(p.noiseMs, t - span);
  }
  return decayAt(p.noiseMs, t);
}

float FmVoice::ampAt(const FmParams& p, float t) const {
  if (!p.oneShot) return 1;
  if (t < kAttack) return ampStart_ + (1.f - ampStart_) * t / kAttack;
  return decayAt(p.ampMs, t - kAttack);
}

void FmVoice::control(const FmParams& p, int span) {
  alg_ = p.alg;
  oneShot_ = p.oneShot;
  const float t0 = static_cast<float>(t_), t1 = t0 + span;
  const float inv = span > 0 ? 1.f / span : 0;
  const float bend0 = exp2f(p.pitchEnv * decayAt(p.pitchMs, t0) * (1.f / 12.f));
  const float bend1 = exp2f(p.pitchEnv * decayAt(p.pitchMs, t1) * (1.f / 12.f));
  const float fbScale = 1.f + p.fbEnv * decayAt(p.fbMs, t0);
  for (int i = 0; i < kFmOps; ++i) {
    const FmOp& o = p.op[i];
    const float hz = p.hz * o.ratio;
    const float i0 = hz * bend0 * kIncPerHz, i1 = hz * bend1 * kIncPerHz;
    float l0 = o.level * opEnv(o, t0), l1 = o.level * opEnv(o, t1);
    if (!fmCarrier(p.alg, i)) {
      const float m = fmMaxIndex(hz);
      l0 = (l0 < m ? l0 : m) * kInvTwoPi;
      l1 = (l1 < m ? l1 : m) * kInvTwoPi;
    }
    if (hz <= 0 || i0 >= kMaxInc || i1 >= kMaxInc) l0 = l1 = 0;
    inc_[i] = i0 < kMaxInc ? i0 : 0;
    dInc_[i] = (i1 < kMaxInc ? i1 - inc_[i] : 0) * inv;
    lvl_[i] = l0;
    dLvl_[i] = (l1 - l0) * inv;
    fb_[i] = o.fb * fbScale * kInvTwoPi;
  }
  const float n0 = p.noise * noiseAt(p, t0), n1 = p.noise * noiseAt(p, t1);
  noise_ = n0;
  dNoise_ = (n1 - n0) * inv;
  const float a0 = ampAt(p, t0), a1 = ampAt(p, t1);
  amp_ = a0;
  dAmp_ = (a1 - a0) * inv;
  filterOn_ = p.filterHz > 0;
  filterAll_ = p.filterAll;
  if (filterOn_) svf_.set(p.filterMode, p.filterHz, p.filterQ);
}

float FmVoice::op(int i, float mod) {
  const float r = sine(ph_[i] + cycles(mod + fb_[i] * fbIn_[i]));
  fbIn_[i] = 0.5f * (r + last_[i]);
  last_[i] = r;
  return r * lvl_[i];
}

template <FmAlg A>
void FmVoice::run(float* out, int n, float gain) {
  for (int s = 0; s < n; ++s) {
    float y;
    if (A == FmAlg::Stack) {
      y = op(0, op(1, op(2, op(3, 0))));
    } else if (A == FmAlg::TwoToOne) {
      const float a = op(2, op(3, 0));
      y = op(0, a + op(1, 0));
    } else if (A == FmAlg::TwoPairs) {
      const float hi = op(2, op(3, 0));
      y = op(0, op(1, 0)) + hi;
    } else if (A == FmAlg::OneToThree) {
      const float m = op(3, 0);
      y = op(0, m) + op(1, m) + op(2, m);
    } else {
      y = op(0, 0) + op(1, 0) + op(2, 0) + op(3, 0);
    }
    if (noise_ != 0 || filterOn_) {
      rng_ ^= rng_ << 13;
      rng_ ^= rng_ >> 17;
      rng_ ^= rng_ << 5;
      const float nz = static_cast<int32_t>(rng_) * (1.f / 2147483648.f) * noise_;
      if (!filterOn_) y += nz;
      else if (filterAll_) y = svf_.process(y + nz);
      else y += svf_.process(nz);
    }
    out[s] += y * amp_ * gain;
    for (int i = 0; i < kFmOps; ++i) {
      ph_[i] += static_cast<uint32_t>(static_cast<int32_t>(inc_[i]));
      inc_[i] += dInc_[i];
      lvl_[i] += dLvl_[i];
    }
    noise_ += dNoise_;
    amp_ += dAmp_;
  }
  t_ += static_cast<uint32_t>(n);
}

void FmVoice::render(float* out, int n, float gain) {
  switch (alg_) {
    case FmAlg::Stack: run<FmAlg::Stack>(out, n, gain); break;
    case FmAlg::TwoToOne: run<FmAlg::TwoToOne>(out, n, gain); break;
    case FmAlg::TwoPairs: run<FmAlg::TwoPairs>(out, n, gain); break;
    case FmAlg::OneToThree: run<FmAlg::OneToThree>(out, n, gain); break;
    default: run<FmAlg::Additive>(out, n, gain); break;
  }
}

}  // namespace mt
```

Порядок вычисления операторов в `Stack`: `op(3,0)` вызывается первым (аргументы вложенных вызовов) — это нужный порядок «модулятор раньше несущей».

**Step 4:** `pio test -e native -f test_synth_fm` — PASS. Если `test_feedback_bounded` или `test_choke_retrigger_has_no_jump` падают — чинить ядро, не тест.

---

### Task 5: машины

**Files:**
- Create: `lib/core/src/synth_fm_machines.h`, `lib/core/src/synth_fm_machines.cpp`
- Test: `test/test_synth_fm_machines/test_main.cpp`

**Step 1: тест**

```cpp
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

void test_out_of_range_machine_is_kick() {
  float m[kFmMacros];
  macros(m, 64);
  FmParams a, b;
  fmMachine(200, m, 60, a);
  fmMachine(static_cast<uint8_t>(FmMachine::Kick), m, 60, b);
  TEST_ASSERT_EQUAL_FLOAT(b.hz, a.hz);
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
  return UNITY_END();
}
```

**Step 2:** `pio test -e native -f test_synth_fm_machines` — FAIL.

**Step 3: реализация**

`synth_fm_machines.h`:

```cpp
#pragma once
#include <stdint.h>
#include "model.h"
#include "synth_fm.h"

namespace mt {

// Sound of a machine (FmMachine, out of range = Kick). mac: DECAY..CONTOUR, 0..127, fractional
// after the LFO. pitch: MIDI note incl. transpose, fine, bend, ARP, VIB; C4 plays the machine's base
// pitch. Pure: called at control rate.
void fmMachine(uint8_t machine, const float mac[kFmMacros], float pitch, FmParams& out);
// CHORD type of a SHAPE value, e.g. "MAJ".
const char* fmChordName(uint8_t shape);
constexpr int kFmChords = 12;

}  // namespace mt
```

`synth_fm_machines.cpp`:

```cpp
#include "synth_fm_machines.h"
#include <math.h>
#include "synth_osc.h"

namespace mt {
namespace {

float u(float v) { return v <= 0 ? 0 : (v >= 127 ? 1 : v * (1.f / 127.f)); }
float lerp(float a, float b, float x) { return a + (b - a) * x; }
float expMap(float lo, float hi, float x) { return lo * powf(hi / lo, x); }
float decayMs(float d) { return expMap(5.f, 4000.f, u(d)); }  // as fmDecayMs, fractional
float toC4(float pitch) { return exp2f((pitch - 60.f) * (1.f / 12.f)); }
// Interpolated entry of a table at x 0..1.
float tableAt(const float* t, int n, float x) {
  const float pos = x * (n - 1);
  int a = static_cast<int>(pos);
  if (a > n - 2) a = n - 2;
  return lerp(t[a], t[a + 1], pos - a);
}

FmOp carrier(float ratio, float level, float decay = 0, float fb = 0) { return {ratio, level, 0, decay, fb}; }
FmOp modulator(float ratio, float index, float decay, float sustain = 0) { return {ratio, index, sustain, decay, 0}; }

void kick(const float* m, float pitch, FmParams& p) {
  const float c = u(m[kMacCol]), s = u(m[kMacShp]);
  p.alg = FmAlg::Stack;
  p.hz = 55.f * toC4(pitch);
  // SHAPE: modulator ratio 0.5..2 ("body"), upper half adds carrier feedback ("909").
  p.op[0] = carrier(1, 1, 0, s > 0.5f ? (s - 0.5f) * 2.f : 0);
  p.op[1] = modulator(s < 0.5f ? lerp(0.5f, 1, s * 2) : lerp(1, 2, (s - 0.5f) * 2), c * c * 6, lerp(15, 150, c));
  p.pitchEnv = u(m[kMacSwp]) * 48;
  p.pitchMs = expMap(5, 200, u(m[kMacCon]));
  p.ampMs = decayMs(m[kMacDec]);
}

void snare(const float* m, float pitch, FmParams& p) {
  const float c = u(m[kMacCol]), s = u(m[kMacShp]);
  const float amp = decayMs(m[kMacDec]);
  const float tone = 1.f - c;
  p.alg = FmAlg::TwoPairs;
  p.hz = 180.f * toC4(pitch);
  p.op[0] = carrier(1, tone * 0.8f, amp);
  p.op[1] = modulator(1, 0.6f, amp * 0.5f);
  p.op[2] = carrier(1.6f, tone * 0.4f, amp * 0.6f);
  p.pitchEnv = u(m[kMacSwp]) * 12;
  p.pitchMs = 30;
  const float len = lerp(0.3f, 1.5f, u(m[kMacCon]));
  p.noise = c;
  p.noiseMs = amp * len;
  if (s < 0.5f) {
    p.filterMode = Svf::Mode::Bp;
    p.filterHz = expMap(800, 4000, s * 2);
    p.filterQ = 1;
  } else {
    p.filterMode = Svf::Mode::Hp;
    p.filterHz = expMap(2000, 8000, (s - 0.5f) * 2);
  }
  p.ampMs = amp * (len > 1 ? len : 1);
}

void metal(const float* m, float pitch, FmParams& p) {
  // Carrier 1..3 and modulator ratios: bell, cowbell, gong, cymbal.
  static const float kSets[kFmOps][4] = {
      {1.f, 1.f, 1.f, 1.f}, {2.76f, 1.48f, 1.19f, 1.34f}, {5.40f, 2.20f, 1.53f, 1.87f}, {3.50f, 1.41f, 0.71f, 2.41f}};
  const float s = u(m[kMacShp]);
  const float amp = decayMs(m[kMacDec]);
  p.alg = FmAlg::OneToThree;
  p.hz = 400.f * toC4(pitch);
  const float lv[3] = {0.4f, 0.3f, 0.25f}, dl[3] = {1.f, 0.8f, 0.6f};
  for (int i = 0; i < 3; ++i) p.op[i] = carrier(tableAt(kSets[i], 4, s), lv[i], amp * dl[i]);
  p.op[3] = modulator(tableAt(kSets[3], 4, s), u(m[kMacCol]) * 5, amp * lerp(1, 0.1f, u(m[kMacCon])));
  p.pitchEnv = u(m[kMacSwp]) * 5;
  p.pitchMs = 40;
  p.ampMs = amp;
}

void perc(const float* m, float pitch, FmParams& p) {
  static const float kRatios[8] = {1.f, 1.5f, 2.f, 2.76f, 3.5f, 4.2f, 5.4f, 7.f};
  const float c = u(m[kMacCol]);
  const float amp = decayMs(m[kMacDec]);
  p.alg = FmAlg::Stack;
  p.hz = 200.f * toC4(pitch);
  p.op[0] = carrier(1, 1);
  p.op[1] = modulator(tableAt(kRatios, 8, u(m[kMacShp])), c * c * 8, amp * 0.5f);
  p.pitchEnv = u(m[kMacSwp]) * 24;
  p.pitchMs = expMap(5, 200, u(m[kMacCon]));
  p.ampMs = amp;
}

void tone(const float* m, float pitch, FmParams& p) {
  const float c = u(m[kMacCol]), w = u(m[kMacSwp]);
  const float envMs = expMap(20, 2000, u(m[kMacCon]));
  // Index = COLOR part (held) + SWEEP part (decays: pluck).
  auto mod = [&](float ratio, float scale) {
    const float held = c * scale, pluck = w * scale * 2;
    const float sum = held + pluck;
    return modulator(ratio, sum, envMs, sum > 0 ? held / sum : 0);
  };
  p.oneShot = false;
  p.alg = FmAlg::Stack;
  p.hz = noteHz(pitch);
  p.op[0] = carrier(1, 1);
  int zone = static_cast<int>(u(m[kMacShp]) * 5);
  if (zone > 4) zone = 4;
  switch (zone) {
    case 0: p.op[1] = mod(1, 1.5f); break;  // sine, brighter with COLOR
    case 1:                                 // saw-like: feedback
      p.op[0].fb = c * 1.6f;
      p.fbEnv = w * 1.5f;
      p.fbMs = envMs;
      break;
    case 2: p.op[1] = mod(2, 3); break;  // square-like: odd harmonics
    case 3:                              // stack
      p.op[1] = mod(1, 2);
      p.op[2] = mod(2, 1.5f);
      break;
    default: p.op[1] = mod(3.5f, 3); break;  // bell
  }
}

const int8_t kChords[kFmChords][kFmOps] = {
    {0, 4, 7, 12}, {0, 3, 7, 12}, {0, 2, 7, 12}, {0, 5, 7, 12}, {0, 4, 7, 10},  {0, 3, 7, 10},
    {0, 4, 7, 11}, {0, 3, 6, 9},  {0, 4, 8, 12}, {0, 7, 12, 19}, {-12, 0, 12, 24}, {0, 3, 10, 14}};
const char* const kChordNames[kFmChords] = {"MAJ", "MIN", "SUS2", "SUS4", "7", "MI7",
                                            "MAJ7", "DIM", "AUG", "5", "OCT", "MI9"};

int chordIndex(float shape) {
  const int i = static_cast<int>(u(shape) * kFmChords);
  return i >= kFmChords ? kFmChords - 1 : i;
}

void chord(const float* m, float pitch, FmParams& p) {
  const float c = u(m[kMacCol]);
  const int8_t* iv = kChords[chordIndex(m[kMacShp])];
  p.oneShot = false;
  p.alg = FmAlg::Additive;
  p.hz = noteHz(pitch);
  for (int i = 0; i < kFmOps; ++i) p.op[i] = carrier(exp2f(iv[i] * (1.f / 12.f)), 0.3f, 0, c * 1.6f);
  p.fbEnv = u(m[kMacSwp]) * 1.5f;
  p.fbMs = expMap(20, 2000, u(m[kMacCon]));
}

void clap(const float* m, float pitch, FmParams& p) {
  const float s = u(m[kMacShp]);
  p.noise = 1;
  p.bursts = static_cast<uint8_t>(2 + static_cast<int>(s * 3.99f));  // 2..5, looser with SHAPE
  p.burstMs = lerp(5, 15, s);
  p.tail = lerp(0.2f, 1, u(m[kMacCon]));
  p.noiseMs = decayMs(m[kMacDec]);
  p.filterMode = Svf::Mode::Bp;
  p.filterHz = expMap(600, 3000, u(m[kMacCol])) * toC4(pitch);
  p.filterQ = lerp(0.7f, 6, u(m[kMacSwp]));
  p.ampMs = p.noiseMs + p.bursts * p.burstMs;
}

void hat(const float* m, float pitch, FmParams& p) {
  static const float kRatios[kFmOps] = {1.f, 1.342f, 1.756f, 2.15f};
  const float s = u(m[kMacShp]);
  const float amp = decayMs(m[kMacDec]);
  const float env = amp * lerp(0.25f, 1, u(m[kMacCon]));
  const float spread = lerp(0.5f, 1.5f, u(m[kMacSwp]));
  p.alg = FmAlg::Additive;
  p.hz = 3500.f * toC4(pitch);
  for (int i = 0; i < kFmOps; ++i) p.op[i] = carrier(1 + (kRatios[i] - 1) * spread, (1 - s) * 0.25f, env, 1.2f);
  p.noise = s;
  p.noiseMs = env;
  p.filterMode = Svf::Mode::Hp;
  p.filterHz = expMap(3000, 12000, u(m[kMacCol]));
  p.filterAll = true;
  p.ampMs = amp;
}

}  // namespace

const char* fmChordName(uint8_t shape) { return kChordNames[chordIndex(shape)]; }

void fmMachine(uint8_t machine, const float mac[kFmMacros], float pitch, FmParams& out) {
  out = FmParams();
  switch (static_cast<FmMachine>(machine)) {
    case FmMachine::Snare: snare(mac, pitch, out); break;
    case FmMachine::Metal: metal(mac, pitch, out); break;
    case FmMachine::Perc: perc(mac, pitch, out); break;
    case FmMachine::Tone: tone(mac, pitch, out); break;
    case FmMachine::Chord: chord(mac, pitch, out); break;
    case FmMachine::Clap: clap(mac, pitch, out); break;
    case FmMachine::Hat: hat(mac, pitch, out); break;
    default: kick(mac, pitch, out); break;
  }
}

}  // namespace mt
```

**Step 4:** `pio test -e native -f test_synth_fm_machines` — PASS.

---

### Task 6: чанк FMIN

**Files:**
- Modify: `lib/core/src/project_io.cpp` (константа, pack/unpack, запись после `INST`, чтение в `loadProject`)
- Test: `test/test_project_io/test_main.cpp`

**Step 1: тесты**

В `fillFull` (после `i15`):

```cpp
  Instrument& i3 = p.instruments[3];
  i3.type = InstrType::Fm;
  i3.machine = static_cast<uint8_t>(FmMachine::Clap);
  for (int k = 0; k < kFmMacros; ++k) i3.macro[k] = static_cast<uint8_t>(10 + k * 20);
  i3.lfoWave = static_cast<uint8_t>(LfoWave::Random);
  i3.lfoRate = 127;
  i3.lfoDepth = -64;
  i3.lfoDest = static_cast<uint8_t>(LfoDest::Vol);
```

В `assertSame`, в цикл по инструментам:

```cpp
    TEST_ASSERT_EQUAL(m.machine, n.machine);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(m.macro, n.macro, kFmMacros);
    TEST_ASSERT_EQUAL(m.lfoWave, n.lfoWave);
    TEST_ASSERT_EQUAL(m.lfoRate, n.lfoRate);
    TEST_ASSERT_EQUAL(m.lfoDepth, n.lfoDepth);
    TEST_ASSERT_EQUAL(m.lfoDest, n.lfoDest);
```

В `test_empty_patterns_not_written`: `< 1200` → `< 1500` (комментарий `+ FMIN (16 x 16)`). В `test_audio_chunks_written`: `TEST_ASSERT_TRUE(hasChunk(out.buf, "FMIN"));`. В `test_old_file_gets_audio_defaults` список вырезаемых чанков дополнить `"FMIN"` и проверить:

```cpp
  TEST_ASSERT_EQUAL(def->instruments[3].machine, b.instruments[3].machine);
  TEST_ASSERT_EQUAL(def->instruments[3].lfoDepth, b.instruments[3].lfoDepth);
```

Новый тест:

```cpp
void test_fmin_garbage_clamped() {
  std::vector<uint8_t> f = fileHeader();
  std::vector<uint8_t> in = {1};
  std::vector<uint8_t> r(16, 0xFF);
  r[8] = 100;  // lfoDepth
  in.insert(in.end(), r.begin(), r.end());
  putChunk(f, "FMIN", in);
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  const Instrument& m = b.instruments[0];
  TEST_ASSERT_EQUAL(0, m.machine);
  for (int k = 0; k < kFmMacros; ++k) TEST_ASSERT_EQUAL(127, m.macro[k]);
  TEST_ASSERT_EQUAL(0, m.lfoWave);
  TEST_ASSERT_EQUAL(127, m.lfoRate);
  TEST_ASSERT_EQUAL(63, m.lfoDepth);
  TEST_ASSERT_EQUAL(0, m.lfoDest);
}
```

(+ `RUN_TEST(test_fmin_garbage_clamped);`)

**Step 2:** `pio test -e native -f test_project_io` — FAIL (round trip теряет FM-поля).

**Step 3: реализация** (`project_io.cpp`)

```cpp
constexpr size_t kFminSize = 16;  // 10 bytes of fields + reserved
```

```cpp
void packFm(const Instrument& m, uint8_t* b) {
  memset(b, 0, kFminSize);
  b[0] = m.machine;
  memcpy(b + 1, m.macro, kFmMacros);
  b[6] = m.lfoWave;
  b[7] = m.lfoRate;
  b[8] = static_cast<uint8_t>(m.lfoDepth);
  b[9] = m.lfoDest;
}

void unpackFm(const uint8_t* b, Instrument& m) {
  m.machine = b[0] < static_cast<int>(FmMachine::Count) ? b[0] : 0;
  for (int k = 0; k < kFmMacros; ++k) m.macro[k] = clampu(b[1 + k], 0, 127);
  m.lfoWave = b[6] < static_cast<int>(LfoWave::Count) ? b[6] : 0;
  m.lfoRate = clampu(b[7], 0, 127);
  m.lfoDepth = clamps(static_cast<int8_t>(b[8]), -64, 63);
  m.lfoDest = b[9] < static_cast<int>(LfoDest::Count) ? b[9] : 0;
}
```

`static_assert(kFminSize <= kInstSize, "readRecords buffer");` рядом с константой.

```cpp
LoadErr readFmin(CrcSource& in, uint32_t size, Project& p) {
  return readRecords(in, size, kFminSize, kInstruments,
                     [&](int i, const uint8_t* b) { unpackFm(b, p.instruments[i]); });
}
```

В `saveProject` сразу после цикла `INST`:

```cpp
  if (!o.chunk("FMIN", 1 + kInstruments * kFminSize) || !o.write(&count, 1)) return false;
  for (const Instrument& m : p.instruments) {
    uint8_t b[kFminSize];
    packFm(m, b);
    if (!o.write(b, sizeof(b))) return false;
  }
```

(`count` здесь ещё равен `kInstruments`.) В `loadProject`:

```cpp
    else if (memcmp(ch, "FMIN", 4) == 0) e = readFmin(in, size, out);
```

**Step 4:** `pio test -e native -f test_project_io` — PASS.

---

### Task 7: FM в синте — голоса, choke, локи, LFO

**Files:**
- Modify: `lib/core/src/synth_voice.h` (поля `Voice`)
- Modify: `lib/core/src/synth.h` (`TrackRt`, новые методы, `rng_`)
- Modify: `lib/core/src/synth.cpp`
- Test: `test/test_synth/test_main.cpp`

**Step 1: тесты** (добавить в `test_synth`, `#include "synth_fm_machines.h"` не нужен)

```cpp
static void useFm(int instr, FmMachine mc) {
  Instrument& m = p->instruments[instr];
  m.type = InstrType::Fm;
  fmSetMachine(m, static_cast<uint8_t>(mc));
}

static bool blockSilent() {
  s->render(buf);
  for (int i = 0; i < Synth::kBlock; ++i)
    if (buf[i] != 0) return false;
  return true;
}

void test_fm_kick_sounds_and_ends_by_itself() {
  useFm(0, FmMachine::Kick);
  p->instruments[0].macro[kMacDec] = 20;  // ~14 ms
  noteOn(0, 0, 60);
  TEST_ASSERT_FALSE(blockSilent());
  for (int k = 0; k < 30; ++k) s->render(buf);
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_fm_drum_ignores_note_off() {
  useFm(0, FmMachine::Kick);
  p->instruments[0].macro[kMacDec] = 110;
  noteOn(0, 0, 60);
  noteOff(10, 0, 60);
  for (int k = 0; k < 4; ++k) s->render(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  TEST_ASSERT_FALSE(blockSilent());
}

void test_fm_drum_chokes_on_same_track() {
  useFm(0, FmMachine::Hat);
  p->instruments[0].mono = false;  // ignored: drums are mono
  noteOn(0, 0, 60);
  noteOn(50, 0, 64);
  s->render(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
}

void test_fm_tone_poly_and_gated() {
  useFm(0, FmMachine::Tone);
  p->instruments[0].mono = false;
  p->instruments[0].sustain = 127;
  noteOn(0, 0, 60);
  noteOn(0, 0, 64);
  noteOn(0, 0, 67);
  for (int k = 0; k < 50; ++k) s->render(buf);
  TEST_ASSERT_EQUAL(3, s->activeVoices());  // held
  noteOff(0, 0, 60);
  noteOff(0, 0, 64);
  noteOff(0, 0, 67);
  s->render(buf);
  s->render(buf);
  TEST_ASSERT_EQUAL(0, s->activeVoices());  // release 0
}

void test_fm_chord_is_one_voice() {
  useFm(0, FmMachine::Chord);
  p->instruments[0].mono = false;
  noteOn(0, 0, 60);
  noteOn(0, 0, 65);
  s->render(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
}

void test_fm_lock_on_note_step() {
  useFm(0, FmMachine::Tone);
  stepStart(0, 0, 6, true);
  fx(0, 0, Fx::COL, 99);
  noteOn(0, 0, 60);
  s->render(buf);
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_EQUAL(1 << kMacCol, v.lockMask);
  TEST_ASSERT_EQUAL(99, v.lock[kMacCol]);
  stepStart(0, 0, 6, true);  // next step: no lock
  noteOn(0, 0, 62);
  s->render(buf);
  TEST_ASSERT_EQUAL(0, s->voice(s->trackVoice(0)).lockMask);
}

void test_fm_lock_on_empty_step_hits_sounding_voice() {
  useFm(0, FmMachine::Tone);
  stepStart(0, 0, 6, true);
  noteOn(0, 0, 60);
  s->render(buf);
  stepStart(0, 0, 6, false);
  fx(0, 0, Fx::SHP, 127);
  s->render(buf);
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_EQUAL(1 << kMacShp, v.lockMask);
  TEST_ASSERT_EQUAL(127, v.lock[kMacShp]);
}

void test_fm_lock_ignored_on_chip() {
  stepStart(0, 0, 6, true);
  noteOn(0, 0, 60);
  s->render(buf);
  stepStart(0, 0, 6, false);
  fx(0, 0, Fx::DCY, 1);
  s->render(buf);
  TEST_ASSERT_EQUAL(0, s->voice(s->trackVoice(0)).lockMask);
}

void test_fm_lfo_on_pitch() {
  useFm(0, FmMachine::Tone);
  Instrument& m = p->instruments[0];
  m.macro[kMacCol] = 0;  // pure sine
  m.sustain = 127;
  noteOn(0, 0, 69);
  const int plain = crossings(250);
  s->reset();
  m.lfoWave = static_cast<uint8_t>(LfoWave::Square);
  m.lfoRate = 60;
  m.lfoDepth = 63;
  m.lfoDest = static_cast<uint8_t>(LfoDest::Pitch);
  noteOn(0, 0, 69);
  TEST_ASSERT_TRUE(crossings(250) > plain * 115 / 100);  // +12 / -12 st halves: ~1.25x
}
```

(`crossings`, `stepStart`, `fx` уже есть в файле; если `stepStart`/`fx` объявлены ниже новых тестов — вставить тесты после них. Добавить все `RUN_TEST`.)

**Step 2:** `pio test -e native -f test_synth` — FAIL (нет `lockMask` и т. д.).

**Step 3: реализация**

`synth_voice.h`: `#include "synth_fm.h"`, в `struct Voice` после `Env env;`:

```cpp
  // FM instrument.
  bool fm = false;
  uint8_t lockMask = 0;              // macros locked for this note: bit = FmMacro
  uint8_t lock[kFmMacros] = {0};
  float lfoPhase = 0;                // 0..1
  float lfoRnd = 0;                  // Random wave: value of the current cycle
  FmVoice fmv;
```

`synth.h`:
- в шапочный комментарий: `cmd = Fx SLD..CON`;
- в `TrackRt`: `uint8_t lockMask; uint8_t lock[kFmMacros];  // FM macro locks: this step's note-ons`;
- приватные методы:

```cpp
  void controlFm(Voice& v, const Instrument& m, float pitch, int dt);
  float lfo(Voice& v, const Instrument& m, int dt);
  bool fmDrum(const Voice& v) const;
  static uint8_t fmMachineOf(const Instrument& m);
```

- член `uint32_t rng_ = 0x2545F491;` (LFO Random).

`synth.cpp`:

1. `startTrack`: `r.lockMask = 0;`. В ветке `kSynthStep`: `r.lockMask = 0;`.
2. В `switch` в `fx()` перед `default`:

```cpp
    case Fx::DCY:
    case Fx::COL:
    case Fx::SHP:
    case Fx::SWP:
    case Fx::CON: {
      const int k = cmd - static_cast<uint8_t>(Fx::DCY);
      const uint8_t v = val > 127 ? 127 : val;
      if (now) {
        for (auto& x : voices_)
          if (x.on && x.track == track && x.fm) {
            x.lock[k] = v;
            x.lockMask |= 1 << k;
          }
      } else {
        r.lock[k] = v;
        r.lockMask |= 1 << k;
      }
      break;
    }
```

3. Вспомогательные:

```cpp
uint8_t Synth::fmMachineOf(const Instrument& m) {
  return m.machine < static_cast<uint8_t>(FmMachine::Count) ? m.machine : 0;
}

bool Synth::fmDrum(const Voice& v) const {
  return v.fm && !fmGated(fmMachineOf(p_.instruments[v.instr]));
}
```

4. `noteOn` — изменения:

```cpp
  const bool fm = m.type == InstrType::Fm;
  const uint8_t machine = fmMachineOf(m);
  const bool drum = fm && !fmGated(machine);
  const bool tone = fm && machine == static_cast<uint8_t>(FmMachine::Tone);
  // FM: only TONE may be poly; drums and CHORD take the track's voice.
  const bool mono = fm ? (tone ? m.mono : true) : m.mono;
```

`allocVoice(voices_, track, m.mono, ...)` → `allocVoice(voices_, track, mono, ...)`. Сразу после `Voice& v = voices_[vi];`:

```cpp
  const bool keepFm = legato && v.fm;  // a sounding FM voice: choke without a click
```

`overlap` → `const bool overlap = legato && !wasReleasing && !drum;`. Глайд: `} else if (overlap && m.glide && (!fm || tone)) {`. Перед `if (restartSmp)`:

```cpp
  v.fm = fm;
  if (fm) {
    v.lockMask = r.lockMask;
    for (int k = 0; k < kFmMacros; ++k) v.lock[k] = r.lock[k];
    if (!overlap) {
      v.fmv.trigger(keepFm);
      v.lfoPhase = 0;
      v.lfoRnd = 0;
    }
  }
```

Огибающая — заменить блок `if (!overlap) { ... }` на:

```cpp
  if (!overlap) {
    const uint8_t sus = m.sustain > 127 ? 127 : m.sustain;
    if (drum) {
      v.env.set(0, 0, 1.f, 0);  // the FM voice shapes itself; the env only gates
    } else {
      const uint8_t dec = (v.lockMask & (1 << kMacDec)) ? v.lock[kMacDec] : m.macro[kMacDec];
      v.env.set(envTimeMs(m.attack), fm ? fmDecayMs(dec) : envTimeMs(m.decay), sus * (1.f / 127.f),
                envTimeMs(m.release));
    }
    v.env.gate(true);
  }
```

5. `noteOff`: условие дополнить `&& !fmDrum(v)`.

6. `control`: после блока VIB (`pitch` готов) и до `if (v.sample)`:

```cpp
  if (v.fm) {
    controlFm(v, m, pitch, dt);  // sets v.amp too
    return;
  }
```

Сигнатура в `synth.h`: `void controlFm(Voice& v, const Instrument& m, float pitch, int dt);`.

```cpp
float Synth::lfo(Voice& v, const Instrument& m, int dt) {
  if (!m.lfoDepth) return 0;
  v.lfoPhase += lfoHz(m.lfoRate) * dt * (1.f / kSynthRate);
  if (v.lfoPhase >= 1.f) {
    v.lfoPhase -= static_cast<int>(v.lfoPhase);
    rng_ = rng_ * 1664525u + 1013904223u;
    v.lfoRnd = static_cast<int32_t>(rng_) * (1.f / 2147483648.f);
  }
  const float ph = v.lfoPhase;
  float w;
  switch (static_cast<LfoWave>(m.lfoWave)) {
    case LfoWave::Tri: w = 4.f * (ph < 0.5f ? 0.5f - ph : ph - 0.5f) - 1.f; break;
    case LfoWave::Saw: w = 2.f * ph - 1.f; break;
    case LfoWave::Square: w = ph < 0.5f ? 1.f : -1.f; break;
    case LfoWave::Random: w = v.lfoRnd; break;
    default: w = sinf(6.2831853f * ph); break;
  }
  const int d = m.lfoDepth < -64 ? -64 : (m.lfoDepth > 63 ? 63 : m.lfoDepth);
  return w * d * (1.f / 64.f);
}

void Synth::controlFm(Voice& v, const Instrument& m, float pitch, int dt) {
  float mac[kFmMacros];
  for (int k = 0; k < kFmMacros; ++k) mac[k] = (v.lockMask & (1 << k)) ? v.lock[k] : m.macro[k];
  const float l = lfo(v, m, dt);
  float vol = 1;
  if (l != 0) {
    const uint8_t dest = m.lfoDest < static_cast<uint8_t>(LfoDest::Count) ? m.lfoDest : 0;
    if (dest == static_cast<uint8_t>(LfoDest::Pitch)) pitch += 12.f * l;
    else if (dest == static_cast<uint8_t>(LfoDest::Vol)) vol = clampf(1.f + l, 0.f, 2.f);
    else mac[dest - 1] = clampf(mac[dest - 1] + 64.f * l, 0.f, 127.f);
  }
  FmParams fp;
  fmMachine(fmMachineOf(m), mac, pitch, fp);
  v.fmv.control(fp, kControl);
  const uint8_t iv = m.vol > 127 ? 127 : m.vol;
  v.amp = v.gain * iv * trackVol(v.track) * vol * (1.f / (127.f * 127.f));
}
```

`#include "synth_fm_machines.h"` в `synth.cpp`.

7. `renderVoice` — в начало:

```cpp
  if (v.fm) {
    // Segments end at control boundaries: n <= kControl.
    float tmp[kControl] = {0};
    v.fmv.render(tmp, n, v.amp);
    for (int i = 0; i < n; ++i) out[i] += tmp[i] * v.env.next();
    if (v.fmv.done()) v.env.kill();
    return;
  }
```

Проверить: в `render()` `end` не дальше следующей границы `kControl` — да (`end = (pos / kControl + 1) * kControl`).

**Step 4:** `pio test -e native -f test_synth` — PASS. Затем `pio test -e native` — всё зелёное.

---

### Task 8: вкладка INST

**Files:**
- Modify: `src/ui/inst_screen.h`, `src/ui/inst_screen.cpp`

Тестов нет (UI), проверка — сборка прошивки.

**Step 1: заголовок**

- `#include "synth_fm_machines.h"` не нужен в .h.
- В `enum Row` добавить третью ветку:

```cpp
    kMachine = kCommon, kMacDecay, kMacColor, kMacShape, kMacSweep, kMacContour,
    kLfoWave, kLfoRate, kLfoDepth, kLfoDest, kFmRows
```

- `Param fm_[kFmRows];`
- `bool shownSample_ = false;` → `mt::InstrType shown_ = mt::InstrType::Chip;`
- комментарий класса: `FM: machine, macros (DECAY..CONTOUR), LFO; ADSR rows grey where the machine ignores them.`

**Step 2: конструктор** (`inst_screen.cpp`)

- `Param* const sets[] = {chip_, sample_, fm_};`
- `kType` формат:

```cpp
    p[kType] = {"Type", [this](char* o, int n) {
                  static const char* const kNames[] = {"CHIP", "SAMPLE", "FM"};
                  const int t = static_cast<int>(inst().type);
                  snprintf(o, n, "%s", kNames[t < 3 ? t : 0]);
                }, ...edit без изменений...};
```

- после блока `sample_[...]`:

```cpp
  auto isTone = [this] { return inst().machine == static_cast<uint8_t>(mt::FmMachine::Tone); };
  auto drum = [this] { return !mt::fmGated(inst().machine); };
  fm_[kAttack].dim = drum;
  fm_[kDecay].dim = [] { return true; };  // the DECAY macro replaces it
  fm_[kSustain].dim = drum;
  fm_[kRelease].dim = drum;
  fm_[kMode].dim = [isTone] { return !isTone(); };
  fm_[kGlide].dim = [this, isTone] { return !inst().mono || !isTone(); };
  fm_[kMachine] = {"Machine",
                   [this](char* o, int n) {
                     static const char* const kNames[] = {"KICK", "SNARE", "METAL", "PERC",
                                                          "TONE", "CHORD", "CLAP", "HAT"};
                     snprintf(o, n, "%s", kNames[inst().machine % 8]);
                   },
                   [this](int d) {
                     const int v = clampi(inst().machine + d, 0, static_cast<int>(mt::FmMachine::Count) - 1);
                     if (v != inst().machine) mt::fmSetMachine(inst(), static_cast<uint8_t>(v));
                   }};
  auto macroEdit = [this](int k) {
    return [this, k](int d) { inst().macro[k] = static_cast<uint8_t>(clampi(inst().macro[k] + d, 0, 127)); };
  };
  auto macroNum = [this](int k) {
    return [this, k](char* o, int n) { snprintf(o, n, "%u", inst().macro[k]); };
  };
  fm_[kMacDecay] = {"DECAY", [this](char* o, int n) {
                      const unsigned ms = mt::fmDecayMs(inst().macro[mt::kMacDec]);
                      if (ms < 1000) snprintf(o, n, "%u ms", ms);
                      else snprintf(o, n, "%u.%u s", ms / 1000, ms % 1000 / 100);
                    }, macroEdit(mt::kMacDec)};
  fm_[kMacColor] = {"COLOR", macroNum(mt::kMacCol), macroEdit(mt::kMacCol)};
  fm_[kMacShape] = {"SHAPE",
                    [this](char* o, int n) {
                      if (inst().machine == static_cast<uint8_t>(mt::FmMachine::Chord))
                        snprintf(o, n, "%u %s", inst().macro[mt::kMacShp], mt::fmChordName(inst().macro[mt::kMacShp]));
                      else snprintf(o, n, "%u", inst().macro[mt::kMacShp]);
                    },
                    macroEdit(mt::kMacShp)};
  fm_[kMacSweep] = {"SWEEP", macroNum(mt::kMacSwp), macroEdit(mt::kMacSwp)};
  fm_[kMacContour] = {"CONTOUR", macroNum(mt::kMacCon), macroEdit(mt::kMacCon)};
  auto noLfo = [this] { return inst().lfoDepth == 0; };
  fm_[kLfoWave] = {"LFO wave",
                   [this](char* o, int n) {
                     static const char* const kNames[] = {"SINE", "TRI", "SAW", "SQR", "RND"};
                     snprintf(o, n, "%s", kNames[inst().lfoWave % 5]);
                   },
                   [this](int d) {
                     inst().lfoWave = static_cast<uint8_t>(
                         clampi(inst().lfoWave + d, 0, static_cast<int>(mt::LfoWave::Count) - 1));
                   },
                   noLfo};
  fm_[kLfoRate] = {"LFO rate", [this](char* o, int n) { snprintf(o, n, "%.2f Hz", mt::lfoHz(inst().lfoRate)); },
                   [this](int d) { inst().lfoRate = static_cast<uint8_t>(clampi(inst().lfoRate + d, 0, 127)); },
                   noLfo};
  fm_[kLfoDepth] = {"LFO depth", [this](char* o, int n) { snprintf(o, n, "%+d", inst().lfoDepth); },
                    [this](int d) { inst().lfoDepth = static_cast<int8_t>(clampi(inst().lfoDepth + d, -64, 63)); }};
  fm_[kLfoDest] = {"LFO dest",
                   [this](char* o, int n) {
                     static const char* const kNames[] = {"PITCH", "DECAY", "COLOR", "SHAPE",
                                                          "SWEEP", "CONTOUR", "VOL"};
                     snprintf(o, n, "%s", kNames[inst().lfoDest % 7]);
                   },
                   [this](int d) {
                     inst().lfoDest = static_cast<uint8_t>(
                         clampi(inst().lfoDest + d, 0, static_cast<int>(mt::LfoDest::Count) - 1));
                   },
                   noLfo};
```

Добавить `#include "synth_fm_machines.h"` в `inst_screen.cpp`.

**Step 3: `syncParams` и `draw`**

```cpp
void InstScreen::syncParams() {
  const mt::InstrType t = inst().type;
  if (t == shown_) return;
  shown_ = t;
  switch (t) {
    case mt::InstrType::Sample:
      list_.setParams(sample_, kSampleRows);
      list_.setVisibleRows(kSampleVisibleRows);
      break;
    case mt::InstrType::Fm:
      list_.setParams(fm_, kFmRows);
      list_.setVisibleRows(kVisibleRows);
      break;
    default:
      list_.setParams(chip_, kChipRows);
      list_.setVisibleRows(kVisibleRows);
      break;
  }
}
```

В `draw`: `if (shownSample_)` → `if (shown_ == mt::InstrType::Sample)`. Остальные упоминания `shownSample_` (grep) заменить так же.

**Step 4:** `pio run -e wt32` — SUCCESS, без новых warning'ов.

---

### Task 9: бенч FM

**Files:**
- Modify: `src/audio/audio.cpp`

**Step 1:** комментарий к бенчу (строка ~17) дополнить:

```cpp
// FM bench: -DAUDIO_BENCH_FM instead holds 16 FM TONE voices (stack, 3 operators) on tracks 1..4
// through the real synth, instrument 16 is overwritten. Same Serial line.
```

**Step 2:** `#ifdef AUDIO_BENCH` у `benchPeak`, у записи пика в `run()` и в `pollLog()` заменить на `#if defined(AUDIO_BENCH) || defined(AUDIO_BENCH_FM)`. `benchPeak` вынести из блока `AUDIO_BENCH` (в общий `#if`).

**Step 3:** функция (после `renderBench`'а, вне его `#ifdef`):

```cpp
#ifdef AUDIO_BENCH_FM
void benchFmBegin() {
  mt::Instrument& m = project->instruments[15];
  m = mt::Instrument();
  m.type = mt::InstrType::Fm;
  mt::fmSetMachine(m, static_cast<uint8_t>(mt::FmMachine::Tone));
  m.macro[mt::kMacShp] = 80;  // stack zone: 3 operators busy
  m.macro[mt::kMacCol] = 100;
  m.sustain = 127;
  m.mono = false;
  for (int t = 0; t < 4; ++t) {
    project->tracks[t].out = mt::TrackOut::Int;
    project->tracks[t].instr = 15;
    for (int k = 0; k < mt::kPolyPerTrack; ++k) {
      const uint8_t on[3] = {0x90, static_cast<uint8_t>(48 + t * 7 + k * 3), 100};
      synth->event(0, static_cast<uint8_t>(t), on, 3);
    }
  }
}
#endif
```

В `begin()` после `benchBegin()`-блока:

```cpp
#ifdef AUDIO_BENCH_FM
  benchFmBegin();
#endif
```

**Step 4:** `pio run -e wt32` (обычная сборка) — SUCCESS. Затем временно добавить `-DAUDIO_BENCH_FM` в `build_flags` `[env:wt32]`, `pio run -e wt32` — SUCCESS, флаг убрать обратно. Прошивать и снимать цифры — пользователю (Serial: `audio bench: render N us (peak M) / 4000 us`; > 2400 us = > 60 % — ограничить FM-голоса).

---

### Task 10: документация

**Files:**
- Modify: `README.md` (раздел «Звук»), `docs/manual.html` (раздел INST и таблица fx), `docs/plans/future-audio.md` (статус)

**Step 1:** `grep -n "SAMPLE\|CHIP\|OFS" README.md docs/manual.html` — найти места, где описаны типы инструментов и synth fx.

**Step 2:** README, раздел «Звук» — добавить после описания SAMPLE:

```markdown
**FM** — 8 машин в стиле Model:Cycles: KICK, SNARE, METAL, PERC, TONE, CHORD, CLAP, HAT. Вместо операторов — 5 макросов: DECAY (длина), COLOR (яркость / индекс), SHAPE (вариант тембра; у CHORD — тип аккорда), SWEEP и CONTOUR (огибающая питча или индекса). Ударные играют one-shot (длина ноты и ADSR не важны, новая нота обрывает предыдущую), TONE и CHORD держат ноту (attack / sustain / release из ADSR). TONE может быть POLY, остальные машины моно на дорожке. LFO: форма, скорость, глубина, цель (питч, макрос, громкость).

P-lock: fx `DEC`, `COL`, `SHP`, `SWP`, `CON` (0–127) задают макрос для ноты своего шага; на шаге без ноты — для звучащей ноты до следующей.
```

**Step 3:** `manual.html` — в раздел INST тот же текст (HTML-разметкой, как соседние абзацы), в таблицу fx — 5 строк `DEC`…`CON`: «FM: макрос DECAY / COLOR / SHAPE / SWEEP / CONTOUR для ноты шага, 0–127».

**Step 4:** `future-audio.md`, раздел «Статус»: «FM 4-op» в «По-прежнему в планах» заменить на строку в «Что сделано»: `FM: 8 машин в стиле Model:Cycles, макросы, p-lock, LFO (см. [дизайн](2026-10-05-fm-machines-design.md))`; в планы добавить «фильтр на все типы инструментов», «машины 808/909».

**Step 5:** финальная проверка: `pio test -e native` — всё PASS; `pio run -e wt32` — SUCCESS. Сводка пользователю: что сделано, что ждёт железа (бенч, тембры на слух).
