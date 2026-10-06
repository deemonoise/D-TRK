# SYNTH (BL + wavetable) — план реализации

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (или subagent-driven-development) to implement this plan task-by-task.

**Goal:** пятый тип инструмента SYNTH — 2 осциллятора (BL Saw/Square/Tri на PolyBLEP или wavetable 64×256 с мипмапами), sub, noise, sync, env→SHAPE, 5 макросов на fx DCY..CON и LFO; таблицы — вшитые (генерируются в банк при старте) и импорт WAV (WaveEdit / Serum) с SD.

**Architecture:** чистое ядро в `lib/core`: `wt_mip` (DFT, сборка мипмапов), `wt_file` (WAV → канонические 64×256), `wt_builtin` (спектры вшитых), `synth_syn` (BlOsc / WtOsc / SynVoice), интеграция в `Synth` по образцу DRUM (`controlSyn`, ветка `renderVoice`). Таблицы хранятся в банке flash записями `rate = 0` (96 КБ): ключ `w` + crc32 канонических 16384 сэмплов, вшитые — `*ИМЯ`. Проект — список `wavetables[32]` (чанк `WTBL`), поля инструмента — чанк `SYNI`; исходники таблиц — `/projects/NAME/wt/<имя>.wav`.

**Tech Stack:** C++17, PlatformIO, Unity (native), ESP32-S3 Arduino, LovyanGFX.

**Дизайн:** [2026-10-06-synth-wavetable-design.md](2026-10-06-synth-wavetable-design.md).

**Отклонение от дизайна (решено при планировании):** вшитые таблицы не генерируются скриптом при сборке, а строятся тем же `wtBuild` из кода (`wt_builtin`) и пишутся в банк flash при первом старте (`audio::bankBegin`), если их там нет; записи `*ИМЯ` никогда не вытесняются. Итог тот же (flash, без SD), но без Python-генератора, который пришлось бы держать в синхроне с C++. Исходники в папке проекта — подпапка `wt/` (чтобы имя таблицы не конфликтовало с именем сэмпла).

**Правила проекта:** не коммитить (шагов Commit нет — пользователь коммитит сам). Все задачи подряд, без остановок на проверку железа; сводка в конце.

**Команды:**
- один тест: `pio test -e native -f test_synth_syn`
- все тесты: `pio test -e native`
- прошивка: `pio run -e wt32`

**Общие константы (вводятся в Task 2, используются везде):**

| имя | значение | смысл |
|---|---|---|
| `kWtFrames` | 64 | кадров в таблице |
| `kWtFrameLen` | 256 | точек в кадре уровня 0 |
| `kWtHarm` | 128 | гармоник уровня 0 |
| `kWtLevels` | 8 | мип-уровней; уровень k — гармоники 1..(128>>k) |
| `wtLevelLen(k)` | max(256>>k, 64) | 256,128,64,64,64,64,64,64 |
| `wtLevelOff(k)` | сумма длин < k | 0,256,384,448,512,576,640,704 |
| `kWtFramePts` | 768 | точек на кадр (все уровни) |
| `kWtTableSamples` | 49152 | int16 в записи банка (64 × 768) |
| `kWtSrcSamples` | 16384 | канонический исходник (64 × 256 = уровень 0) |

Раскладка записи: кадр-мажорная — `table[f * kWtFramePts + wtLevelOff(k) + i]`.

---

### Task 1: модель — тип SYNTH, поля, список таблиц

**Files:**
- Modify: `lib/core/src/model.h`, `lib/core/src/model.cpp`
- Modify: `src/storage/storage.cpp:44-60` (static_assert размеров, `snapshot()`)
- Test: `test/test_model/test_main.cpp`

**Step 1: тест** (дописать в `test/test_model/test_main.cpp`, зарегистрировать в `main`)

```cpp
void test_synth_type_defaults() {
  Instrument m;
  m.lfoDest = static_cast<uint8_t>(LfoDest::Shp);
  instrSetType(m, InstrType::Synth);
  TEST_ASSERT_EQUAL(InstrType::Synth, m.type);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(SynOsc::Saw), m.synOsc[0]);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(SynOsc::Saw), m.synOsc[1]);
  TEST_ASSERT_EQUAL(0, m.macro[kMacShp1]);
  TEST_ASSERT_EQUAL(0, m.macro[kMacMix]);
  TEST_ASSERT_EQUAL(64, m.macro[kMacDet]);
  TEST_ASSERT_EQUAL(64, m.macro[kMacSenv]);
  // SYNTH keeps macro LFO targets.
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(LfoDest::Shp), m.lfoDest);
}

void test_synth_type_order() {
  // UI order: FM, SYNTH, DRUM, SAMPLE, CHIP.
  TEST_ASSERT_EQUAL(InstrType::Fm, instrTypeAt(0));
  TEST_ASSERT_EQUAL(InstrType::Synth, instrTypeAt(1));
  TEST_ASSERT_EQUAL(1, instrTypePos(InstrType::Synth));
  TEST_ASSERT_EQUAL(InstrType::Chip, instrTypeAt(4));
}

void test_project_reset_clears_wavetables() {
  Project p;
  strcpy(p.wavetables[0].name, "X");
  p.wavetableCount = 1;
  p.reset();
  TEST_ASSERT_EQUAL(0, p.wavetableCount);
  TEST_ASSERT_EQUAL(0, p.wavetables[0].name[0]);
}
```

**Step 2:** `pio test -e native -f test_model` → FAIL (нет `InstrType::Synth`).

**Step 3: реализация**

`model.h`:
```cpp
enum class InstrType : uint8_t { Chip, Sample, Fm, Drum, Synth, Count };
// SYNTH oscillator mode. Stored in files: new modes before Count only.
enum class SynOsc : uint8_t { Saw, Square, Tri, Wt, Count };
// SYNTH macros: same slots as FM / DRUM (fx DCY..CON, LFO Dec..Con).
enum SynMacro : uint8_t { kMacShp1 = kMacDec, kMacShp2 = kMacCol, kMacMix = kMacShp, kMacDet = kMacSwp,
                          kMacSenv = kMacCon };
constexpr int kProjWavetables = 32;
struct ProjWavetable {
  char name[kSampleNameMax + 1] = {0};
  uint32_t crc = 0;  // of the canonical 64 x 256 source (wtKey)
};
```
В `Instrument` после `send`:
```cpp
  // SYNTH (see synth_syn.h): oscillators 1, 2 (SynOsc), their wavetables (project list name or a
  // built-in "*NAME"), osc 2 semitones, hard sync, sub (level, 0 = -1 / 1 = -2 octaves), noise,
  // env->SHAPE attack / decay (envTimeMs, decay 0 = hold). Macros: SHP1, SHP2, MIX, DET, SENV.
  uint8_t synOsc[2] = {0, 0};
  char synWt[2][kSampleNameMax + 1] = {{0}, {0}};
  int8_t synSemi = 0;     // -24..24
  bool synSync = false;
  uint8_t synSub = 0;     // 0..127
  uint8_t synSubOct = 0;  // 0..1
  uint8_t synNoise = 0;   // 0..127
  uint8_t synEAtk = 0, synEDec = 40;
```
В `Project` после `sampleCount`:
```cpp
  ProjWavetable wavetables[kProjWavetables];
  uint8_t wavetableCount = 0;  // names unique ignoring case
```
`model.cpp`:
- `kTypeOrder = {Fm, Synth, Drum, Sample, Chip}` (static_assert на Count уже есть — обновится сам).
- `instrSetType`: `else if (t == InstrType::Synth) { static const uint8_t kDef[kFmMacros] = {0, 0, 0, 64, 64}; memcpy(m.macro, kDef, kFmMacros); }`. Условие сброса макро-назначения LFO оставить только для Chip/Sample (Synth их держит).
- `Project::reset()`: `for (auto& w : wavetables) w = ProjWavetable{}; wavetableCount = 0;`.

`src/storage/storage.cpp`: обновить `static_assert(sizeof(Instrument) == …)` и `sizeof(Project) == …` на значения, которые покажет ошибка компиляции `pio run -e wt32`; в `snapshot()` скопировать `wavetables` и `wavetableCount` рядом с `samples`.

**Step 4:** `pio test -e native -f test_model` → PASS; `pio test -e native` — всё зелёное (ничего не сломали порядком типов: если какой-то тест проверял `instrTypeAt` — обновить ожидание под новый порядок).

---

### Task 2: `wt_mip` — DFT и сборка мипмапов

**Files:**
- Create: `lib/core/src/wt_mip.h`, `lib/core/src/wt_mip.cpp`
- Test: `test/test_wt_mip/test_main.cpp`

**Step 1: тест**

```cpp
#include <math.h>
#include <string.h>
#include <unity.h>
#include "wt_mip.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static int16_t table[kWtTableSamples];

// Saw spectrum on every frame: sin harmonics 1/h.
struct SawSrc : WtSpectrumSource {
  void spectrum(int, float* re, float* im) override {
    for (int h = 1; h <= kWtHarm; ++h) {
      re[h - 1] = 0;
      im[h - 1] = 1.f / h;
    }
  }
};
// Frame f: single harmonic f + 1 (to tell frames apart).
struct OneHarm : WtSpectrumSource {
  void spectrum(int f, float* re, float* im) override {
    for (int h = 1; h <= kWtHarm; ++h) re[h - 1] = im[h - 1] = 0;
    im[f] = 1;
  }
};

void test_layout() {
  TEST_ASSERT_EQUAL(256, wtLevelLen(0));
  TEST_ASSERT_EQUAL(128, wtLevelLen(1));
  TEST_ASSERT_EQUAL(64, wtLevelLen(7));
  TEST_ASSERT_EQUAL(0, wtLevelOff(0));
  TEST_ASSERT_EQUAL(384, wtLevelOff(2));
  TEST_ASSERT_EQUAL(704, wtLevelOff(7));
  TEST_ASSERT_EQUAL(768, kWtFramePts);
  TEST_ASSERT_EQUAL(49152, kWtTableSamples);
}

void test_analyze_roundtrip() {
  int16_t x[256];
  for (int n = 0; n < 256; ++n)
    x[n] = static_cast<int16_t>(10000 * sinf(6.2831853f * 3 * n / 256) + 5000 * cosf(6.2831853f * 7 * n / 256) + 3000);
  float re[kWtHarm], im[kWtHarm];
  wtAnalyze(x, 256, re, im);
  TEST_ASSERT_FLOAT_WITHIN(5, 10000, im[2]);
  TEST_ASSERT_FLOAT_WITHIN(5, 5000, re[6]);
  TEST_ASSERT_FLOAT_WITHIN(5, 0, re[0]);  // DC is not a harmonic
}

void test_build_levels_bandlimited() {
  SawSrc s;
  TEST_ASSERT_TRUE(wtBuild(s, table));
  for (int k = 0; k < kWtLevels; ++k) {
    const int len = wtLevelLen(k), top = kWtHarm >> k;
    const int16_t* lv = table + 5 * kWtFramePts + wtLevelOff(k);
    float re[kWtHarm], im[kWtHarm];
    wtAnalyze(lv, len, re, im);
    long sum = 0;
    for (int i = 0; i < len; ++i) sum += lv[i];
    TEST_ASSERT_TRUE(labs(sum / len) < 20);  // no DC
    TEST_ASSERT_TRUE(fabsf(im[0]) > 1000);   // fundamental present
    for (int h = top + 1; h <= len / 2 && h <= kWtHarm; ++h)
      TEST_ASSERT_TRUE(fabsf(re[h - 1]) + fabsf(im[h - 1]) < 40);  // nothing above the level's limit
  }
}

void test_build_normalized() {
  SawSrc s;
  wtBuild(s, table);
  int peak = 0;
  for (int i = 0; i < kWtTableSamples; ++i) peak = abs(table[i]) > peak ? abs(table[i]) : peak;
  TEST_ASSERT_INT_WITHIN(2, kWtPeak, peak);
}

void test_frames_distinct() {
  OneHarm s;
  wtBuild(s, table);
  float re[kWtHarm], im[kWtHarm];
  wtAnalyze(table + 9 * kWtFramePts, 256, re, im);
  TEST_ASSERT_TRUE(fabsf(im[9]) > 10000);
  TEST_ASSERT_TRUE(fabsf(im[0]) < 50);
}

void test_silent_source_fails() {
  struct Zero : WtSpectrumSource {
    void spectrum(int, float* re, float* im) override {
      for (int h = 0; h < kWtHarm; ++h) re[h] = im[h] = 0;
    }
  } z;
  TEST_ASSERT_FALSE(wtBuild(z, table));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_layout);
  RUN_TEST(test_analyze_roundtrip);
  RUN_TEST(test_build_levels_bandlimited);
  RUN_TEST(test_build_normalized);
  RUN_TEST(test_frames_distinct);
  RUN_TEST(test_silent_source_fails);
  return UNITY_END();
}
```

**Step 2:** `pio test -e native -f test_wt_mip` → FAIL (нет файла).

**Step 3: реализация**

`wt_mip.h`:
```cpp
#pragma once
#include <stdint.h>

namespace mt {

// Mip-mapped wavetable: kWtFrames frames, each stored at kWtLevels band-limited levels.
// Level k keeps harmonics 1..(kWtHarm >> k) in wtLevelLen(k) points. Frame-major layout:
// table[f * kWtFramePts + wtLevelOff(k) + i]. Level 0 of all frames is the canonical source.
constexpr int kWtFrames = 64;
constexpr int kWtFrameLen = 256;
constexpr int kWtHarm = kWtFrameLen / 2;
constexpr int kWtLevels = 8;
constexpr int kWtSrcSamples = kWtFrames * kWtFrameLen;
constexpr int kWtPeak = 30000;  // normalized peak over the whole table
constexpr int wtLevelLen(int k) { return (kWtFrameLen >> k) < 64 ? 64 : (kWtFrameLen >> k); }
constexpr int wtLevelOff(int k) { return k <= 0 ? 0 : wtLevelOff(k - 1) + wtLevelLen(k - 1); }
constexpr int kWtFramePts = wtLevelOff(kWtLevels);
constexpr int kWtTableSamples = kWtFrames * kWtFramePts;
static_assert(kWtFramePts == 768, "layout");

// Harmonics 1..kWtHarm of frame f (index h - 1): x(n) = sum re cos(2 pi h n / N) + im sin(...).
struct WtSpectrumSource {
  virtual void spectrum(int f, float* re, float* im) = 0;
};

// DFT of one cycle of len points (len >= 2): harmonics 1..min(kWtHarm, len / 2), the rest 0.
// The Nyquist bin (h = len / 2) gets 1 / len, the others 2 / len. DC is dropped.
void wtAnalyze(const int16_t* x, int len, float* re, float* im);
// All frames and levels into out (kWtTableSamples), scaled so the table peak is kWtPeak.
// Two passes over src (peak, then write). False if the table is silent (out untouched).
bool wtBuild(WtSpectrumSource& src, int16_t* out);
// Level 0 of every frame (the canonical source) into src (kWtSrcSamples).
void wtLevel0(const int16_t* table, int16_t* src);

}  // namespace mt
```

`wt_mip.cpp`:
```cpp
#include "wt_mip.h"
#include <math.h>

namespace mt {
namespace {

// Level k of one frame from its spectrum, float.
void synthLevel(const float* re, const float* im, int k, float* y) {
  const int len = wtLevelLen(k);
  const int top = kWtHarm >> k;
  for (int n = 0; n < len; ++n) {
    double acc = 0;
    for (int h = 1; h <= top; ++h) {
      const double a = 6.283185307179586 * h * n / len;
      acc += re[h - 1] * cos(a) + im[h - 1] * sin(a);
    }
    y[n] = static_cast<float>(acc);
  }
}

}  // namespace

void wtAnalyze(const int16_t* x, int len, float* re, float* im) {
  for (int h = 1; h <= kWtHarm; ++h) {
    re[h - 1] = im[h - 1] = 0;
    if (2 * h > len) continue;
    double c = 0, s = 0;
    for (int n = 0; n < len; ++n) {
      const double a = 6.283185307179586 * h * n / len;
      c += x[n] * cos(a);
      s += x[n] * sin(a);
    }
    const double g = (2 * h == len ? 1.0 : 2.0) / len;
    re[h - 1] = static_cast<float>(c * g);
    im[h - 1] = static_cast<float>(s * g);
  }
}

bool wtBuild(WtSpectrumSource& src, int16_t* out) {
  float re[kWtHarm], im[kWtHarm], y[kWtFrameLen];
  float peak = 0;
  for (int f = 0; f < kWtFrames; ++f) {
    src.spectrum(f, re, im);
    for (int k = 0; k < kWtLevels; ++k) {
      synthLevel(re, im, k, y);
      for (int n = 0; n < wtLevelLen(k); ++n) peak = fabsf(y[n]) > peak ? fabsf(y[n]) : peak;
    }
  }
  if (peak < 1e-6f) return false;
  const float g = kWtPeak / peak;
  for (int f = 0; f < kWtFrames; ++f) {
    src.spectrum(f, re, im);
    for (int k = 0; k < kWtLevels; ++k) {
      synthLevel(re, im, k, y);
      int16_t* d = out + f * kWtFramePts + wtLevelOff(k);
      for (int n = 0; n < wtLevelLen(k); ++n) d[n] = static_cast<int16_t>(lrintf(y[n] * g));
    }
  }
  return true;
}

void wtLevel0(const int16_t* table, int16_t* src) {
  for (int f = 0; f < kWtFrames; ++f)
    for (int n = 0; n < kWtFrameLen; ++n) src[f * kWtFrameLen + n] = table[f * kWtFramePts + n];
}

}  // namespace mt
```
Примечание: наивный DFT в double — ~5–10 M операций на таблицу; на ESP32 это доли секунды (импорт разовый). Если на железе медленно — заменить `cos/sin` таблицей на 256/2048 точек (индекс `h*n % len`), тесты не меняются.

**Step 4:** `pio test -e native -f test_wt_mip` → PASS.

---

### Task 3: `wav` — чанк `clm ` (Serum)

**Files:**
- Modify: `lib/core/src/wav.h` (`WavInfo`), `lib/core/src/wav.cpp:80-100`
- Test: `test/test_wav/test_main.cpp`

**Step 1: тест** — WAV в памяти с чанком `clm ` (`"<!>2048 01000000 wavetable (www.xferrecords.com)"`, 48 байт) перед `data`; `wavParse` → `info.clmFrame == 2048`; без чанка → 0; мусорный `clm ` (`"abc"`) → 0, разбор Ok. Собрать байты тем же хелпером, что уже используется в `test_wav` для `smpl` (посмотреть в файле, как строятся тестовые WAV, и повторить).

**Step 2:** FAIL (нет поля).

**Step 3:** в `WavInfo`: `uint16_t clmFrame = 0;  // Serum "clm " chunk: samples per frame, 0 = none`. В цикле чанков `wav.cpp` ветка `else if (memcmp(c, "clm ", 4) == 0)`: прочитать до 16 байт, если начинаются с `<!>` — `atoi` от цифр после (принимать 32..4096, иначе 0); пропустить остаток чанка с паддингом. Как и `smpl`, если `data` уже прочитан — `return WavErr::Ok` после разбора.

**Step 4:** PASS, весь `test_wav` зелёный.

---

### Task 4: `wt_file` — WAV → канонические 64 × 256

**Files:**
- Create: `lib/core/src/wt_file.h`, `lib/core/src/wt_file.cpp`
- Test: `test/test_wt_file/test_main.cpp`

**Правила** (из дизайна):
- `clmFrame > 0` → длина кадра = `clmFrame`; иначе длина кратна 256 и ≤ 64 кадров → 256; иначе кратна 2048 → 2048; иначе `BadLength`. (16384 сэмплов без `clm` = WaveEdit 64×256.)
- Кадров > 64 → берём 64 равномерно: `src = lrint(j * (n - 1) / 63.0)`.
- Кадров < 64 → выходной кадр j = линейная интерполяция спектров кадров `floor(pos)` и `floor(pos)+1`, `pos = j * (n - 1) / 63.0` (n = 1 → все одинаковые).
- Кадр длины L > 256 анализируется `wtAnalyze(x, L, …)` — берутся гармоники 1..128 (прореживание через спектр).
- DC и нормализация — в `wtBuild`.

**Step 1: тест**

```cpp
#include <math.h>
#include <string.h>
#include <unity.h>
#include "wt_file.h"
#include "wt_mip.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static int16_t in[64 * 2048];
static int16_t table[kWtTableSamples];

// n frames of len points; frame f = sine of harmonic (f % 8) + 1.
static void fill(int n, int len) {
  for (int f = 0; f < n; ++f)
    for (int i = 0; i < len; ++i)
      in[f * len + i] = static_cast<int16_t>(20000 * sinf(6.2831853f * (f % 8 + 1) * i / len));
}
static int strongestHarm(int frame) {
  float re[kWtHarm], im[kWtHarm];
  wtAnalyze(table + frame * kWtFramePts, kWtFrameLen, re, im);
  int best = 0;
  for (int h = 1; h < kWtHarm; ++h)
    if (fabsf(im[h]) + fabsf(re[h]) > fabsf(im[best]) + fabsf(re[best])) best = h;
  return best + 1;
}

void test_format_detect() {
  WtFormat f;
  TEST_ASSERT_EQUAL(WtErr::Ok, wtDetect(16384, 0, f));
  TEST_ASSERT_EQUAL(256, f.frameLen);
  TEST_ASSERT_EQUAL(64, f.frames);
  TEST_ASSERT_EQUAL(WtErr::Ok, wtDetect(16384, 2048, f));
  TEST_ASSERT_EQUAL(2048, f.frameLen);
  TEST_ASSERT_EQUAL(8, f.frames);
  TEST_ASSERT_EQUAL(WtErr::Ok, wtDetect(256 * 2048, 0, f));  // Serum without clm
  TEST_ASSERT_EQUAL(2048, f.frameLen);
  TEST_ASSERT_EQUAL(256, f.frames);
  TEST_ASSERT_EQUAL(WtErr::BadLength, wtDetect(1000, 0, f));
  TEST_ASSERT_EQUAL(WtErr::BadLength, wtDetect(0, 0, f));
}

void test_waveedit_identity() {
  fill(64, 256);
  TEST_ASSERT_EQUAL(WtErr::Ok, wtImport(in, 16384, 0, table));
  TEST_ASSERT_EQUAL(1, strongestHarm(0));
  TEST_ASSERT_EQUAL(4, strongestHarm(3));
  TEST_ASSERT_EQUAL(8, strongestHarm(63));
}

void test_serum_2048_decimated() {
  fill(8, 2048);
  TEST_ASSERT_EQUAL(WtErr::Ok, wtImport(in, 8 * 2048, 2048, table));
  TEST_ASSERT_EQUAL(1, strongestHarm(0));   // source frame 0
  TEST_ASSERT_EQUAL(8, strongestHarm(63));  // source frame 7
}

void test_many_frames_picked_evenly() {
  fill(64, 2048);  // 64 frames of 2048: each output frame = one source frame
  TEST_ASSERT_EQUAL(WtErr::Ok, wtImport(in, 64 * 2048, 2048, table));
  TEST_ASSERT_EQUAL(2, strongestHarm(1));
}

void test_single_frame() {
  fill(1, 256);
  TEST_ASSERT_EQUAL(WtErr::Ok, wtImport(in, 256, 0, table));
  TEST_ASSERT_EQUAL(1, strongestHarm(40));
}

void test_silent_rejected() {
  memset(in, 0, sizeof(int16_t) * 16384);
  TEST_ASSERT_EQUAL(WtErr::Silent, wtImport(in, 16384, 0, table));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_format_detect);
  RUN_TEST(test_waveedit_identity);
  RUN_TEST(test_serum_2048_decimated);
  RUN_TEST(test_many_frames_picked_evenly);
  RUN_TEST(test_single_frame);
  RUN_TEST(test_silent_rejected);
  return UNITY_END();
}
```

**Step 2:** FAIL.

**Step 3: реализация**

`wt_file.h`:
```cpp
#pragma once
#include <stdint.h>

namespace mt {

enum class WtErr : uint8_t { Ok, BadLength, Silent };
struct WtFormat {
  int frameLen = 0;  // points per source frame
  int frames = 0;    // source frames
};
constexpr uint32_t kWtMaxSrcSamples = 256 * 2048;  // import limit (Serum max)

// Frame layout of a mono wavetable of n samples; clm = WavInfo::clmFrame (0 = none).
WtErr wtDetect(uint32_t n, uint16_t clm, WtFormat& out);
// Mono samples (any wtDetect layout) -> mip-mapped table (kWtTableSamples, wt_mip.h).
WtErr wtImport(const int16_t* mono, uint32_t n, uint16_t clm, int16_t* table);

}  // namespace mt
```

`wt_file.cpp`:
```cpp
#include "wt_file.h"
#include <math.h>
#include "wt_mip.h"

namespace mt {

WtErr wtDetect(uint32_t n, uint16_t clm, WtFormat& out) {
  int len = 0;
  if (clm && n % clm == 0) len = clm;
  else if (n % kWtFrameLen == 0 && n / kWtFrameLen <= static_cast<uint32_t>(kWtFrames)) len = kWtFrameLen;
  else if (n % 2048 == 0) len = 2048;
  if (!len || n == 0 || n > kWtMaxSrcSamples) return WtErr::BadLength;
  out.frameLen = len;
  out.frames = static_cast<int>(n / len);
  return WtErr::Ok;
}

namespace {

// Output frame j: picked source frame (n > 64) or spectra interpolated between neighbours.
struct FileSrc final : WtSpectrumSource {
  const int16_t* d;
  WtFormat f;
  void spectrum(int j, float* re, float* im) override {
    if (f.frames == 1) {
      wtAnalyze(d, f.frameLen, re, im);
      return;
    }
    const double pos = j * (f.frames - 1) / double(kWtFrames - 1);
    if (f.frames > kWtFrames) {
      wtAnalyze(d + static_cast<long>(lrint(pos)) * f.frameLen, f.frameLen, re, im);
      return;
    }
    const int a = static_cast<int>(pos);
    const int b = a + 1 < f.frames ? a + 1 : a;
    const float t = static_cast<float>(pos - a);
    float re2[kWtHarm], im2[kWtHarm];
    wtAnalyze(d + a * f.frameLen, f.frameLen, re, im);
    wtAnalyze(d + b * f.frameLen, f.frameLen, re2, im2);
    for (int h = 0; h < kWtHarm; ++h) {
      re[h] += (re2[h] - re[h]) * t;
      im[h] += (im2[h] - im[h]) * t;
    }
  }
};

}  // namespace

WtErr wtImport(const int16_t* mono, uint32_t n, uint16_t clm, int16_t* table) {
  FileSrc s;
  s.d = mono;
  const WtErr e = wtDetect(n, clm, s.f);
  if (e != WtErr::Ok) return e;
  return wtBuild(s, table) ? WtErr::Ok : WtErr::Silent;
}

}  // namespace mt
```
(Анализ кадра 2048 точек в double — ~0,5 M операций; 64 кадра × 2 прохода `wtBuild` ≈ 60 M — на ESP32 секунда-две. Допустимо для импорта; при необходимости — кэш спектров в PSRAM, 64 КБ.)

**Step 4:** PASS.

---

### Task 5: `wt_builtin` — вшитые таблицы

**Files:**
- Create: `lib/core/src/wt_builtin.h`, `lib/core/src/wt_builtin.cpp`
- Test: `test/test_wt_builtin/test_main.cpp`

**API:**
```cpp
#pragma once
#include "wt_mip.h"

namespace mt {

// Built-in wavetables: generated by wtBuild from these spectra, cached in the bank as "*NAME".
constexpr int kWtBuiltins = 8;
const char* wtBuiltinName(int i);       // "*SAWSQR", ...
int wtBuiltinFind(const char* name);    // ignoring case, -1
inline bool isWtBuiltin(const char* n) { return n && n[0] == '*'; }
// Source of built-in i for wtBuild.
struct WtBuiltinSrc final : WtSpectrumSource {
  int id;
  explicit WtBuiltinSrc(int i) : id(i) {}
  void spectrum(int f, float* re, float* im) override;
};

}  // namespace mt
```

Спектры, `t = f / 63.0f` (`re` — cos, `im` — sin):

| i | имя | спектр |
|---|---|---|
| 0 | `*SAWSQR` | `im[h] = (h odd ? 1 : 1 - t) / h` |
| 1 | `*PWM` | пульс, ширина `w = 0.5 - 0.45 t`: `re[h] = sin(π h w) / h` |
| 2 | `*SINSAW` | `im[h] = clamp(t * 127 - (h - 1) + 1, 0, 1) / h` (h = 1 всегда) |
| 3 | `*TRISQR` | odd h: `im[h] = lerp((-1)^((h-1)/2) / h², 1/h, t)`, even 0 |
| 4 | `*FORMANT` | `im[h] = 0.3 [h=1] + exp(-((h - (2 + 30 t)) / 3)²)` |
| 5 | `*ORGAN` | гармоники {1,2,3,4,6,8}: веса `w_k = 0.5 + 0.5 cos(2π (t + k/6))`, `im[h] = w_k / sqrt(h)` |
| 6 | `*SYNC` | временная область: saw с hard sync, ratio `r = 1 + 7 t`: `x(n) = 2 frac(r n / 2048) - 1`, n = 0..2047 → `wtAnalyze(x, 2048)` |
| 7 | `*BELL` | FM: `x(n) = sin(2π n/2048 + I sin(2π 3 n / 2048))`, `I = 4 t` → `wtAnalyze` |

Для 6 и 7 — локальный буфер `int16_t x[2048]` (×20000) внутри `spectrum`.

**Step 1: тест**
```cpp
void test_names() {
  TEST_ASSERT_EQUAL_STRING("*SAWSQR", wtBuiltinName(0));
  TEST_ASSERT_EQUAL(1, wtBuiltinFind("*pwm"));
  TEST_ASSERT_EQUAL(-1, wtBuiltinFind("SAWSQR"));
  TEST_ASSERT_TRUE(isWtBuiltin("*X"));
  for (int i = 0; i < kWtBuiltins; ++i) TEST_ASSERT_TRUE(strlen(wtBuiltinName(i)) <= kSampleNameMax);
}
void test_all_build() {
  static int16_t t[kWtTableSamples];
  for (int i = 0; i < kWtBuiltins; ++i) {
    WtBuiltinSrc s(i);
    TEST_ASSERT_TRUE_MESSAGE(wtBuild(s, t), wtBuiltinName(i));
  }
}
void test_sawsqr_ends() {
  static int16_t t[kWtTableSamples];
  WtBuiltinSrc s(0);
  wtBuild(s, t);
  float re[kWtHarm], im[kWtHarm];
  wtAnalyze(t, kWtFrameLen, re, im);  // frame 0: saw, h2 present
  TEST_ASSERT_TRUE(fabsf(im[1]) > 0.4f * fabsf(im[0]));
  wtAnalyze(t + 63 * kWtFramePts, kWtFrameLen, re, im);  // frame 63: square, no h2
  TEST_ASSERT_TRUE(fabsf(im[1]) < 0.01f * fabsf(im[0]));
}
```
(+ `#include "model.h"` для `kSampleNameMax`, `<string.h>`, `<math.h>`, `main` с RUN_TEST.)

**Step 2:** FAIL. **Step 3:** реализовать по таблице. **Step 4:** PASS.

---

### Task 6: `synth_syn` — осцилляторы и голос

**Files:**
- Create: `lib/core/src/synth_syn.h`, `lib/core/src/synth_syn.cpp`
- Test: `test/test_synth_syn/test_main.cpp`

**Алгоритм BL (PolyBLEP с задержкой на 1 сэмпл).** Разрыв высоты `h` между сэмплами n−1 и n, `x` ∈ [0,1) — расстояние события до сэмпла n в сэмплах. Поправки: сэмпл n−1 += `h/2 · x²`, сэмпл n += `h/2 · (2x − x² − 1)`. Осциллятор держит `prev` (сэмпл n−1, ещё не отданный) и отдаёт его после того, как события интервала (n−1, n] внесли поправку. Это даёт и обычные фронты, и hard sync (событие сброса с произвольной высотой).

**Step 1: тест**

```cpp
#include <math.h>
#include <string.h>
#include <unity.h>
#include "synth_osc.h"
#include "synth_syn.h"
#include "wt_builtin.h"

using namespace mt;

void setUp() {}
void tearDown() {}

// Goertzel power at hz over x[0..n).
static double power(const float* x, int n, float hz) {
  const double w = 6.283185307179586 * hz / kSynthRate, c = 2 * cos(w);
  double s1 = 0, s2 = 0;
  for (int i = 0; i < n; ++i) {
    const double s = x[i] + c * s1 - s2;
    s2 = s1;
    s1 = s;
  }
  return s1 * s1 + s2 * s2 - c * s1 * s2;
}

static float out[8192];

// f0 = 5 kHz: harmonics 5/10/15 kHz, aliases of 20/25/30 kHz land on 12/7/2 kHz.
void test_bl_saw_alias_below_naive() {
  BlOsc o;
  const float dt = 5000.f / kSynthRate;
  for (int i = 0; i < 8192; ++i) out[i] = o.saw(dt, -1).v;
  const double bl = power(out, 8192, 2000) + power(out, 8192, 7000);
  ChipOsc c;
  for (int i = 0; i < 8192; ++i) out[i] = c.next(Wave::Saw, dt, 0.5f);
  const double naive = power(out, 8192, 2000) + power(out, 8192, 7000);
  TEST_ASSERT_TRUE(bl < naive * 0.05);  // > 13 dB less
}

void test_bl_square_pw_duty() {
  BlOsc o;
  const float dt = 100.f / kSynthRate;
  double mean = 0;
  for (int i = 0; i < kSynthRate; ++i) mean += o.square(dt, 0.25f, -1).v;
  // 25 % high: mean = 0.25 - 0.75 = -0.5.
  TEST_ASSERT_FLOAT_WITHIN(0.02f, -0.5f, static_cast<float>(mean / kSynthRate));
}

void test_bl_tri_bounded() {
  BlOsc o;
  const float dt = 440.f / kSynthRate;
  float lo = 0, hi = 0;
  for (int i = 0; i < kSynthRate; ++i) {
    const float v = o.tri(dt).v;
    if (i > kSynthRate / 2) lo = v < lo ? v : lo, hi = v > hi ? v : hi;
  }
  TEST_ASSERT_FLOAT_WITHIN(0.15f, 1.f, hi);
  TEST_ASSERT_FLOAT_WITHIN(0.15f, -1.f, lo);
}

void test_master_reports_wrap() {
  BlOsc o;
  const float dt = 1000.f / kSynthRate;  // 32 samples per cycle
  int wraps = 0;
  for (int i = 0; i < 3200; ++i) wraps += o.saw(dt, -1).wrap >= 0;
  TEST_ASSERT_INT_WITHIN(1, 100, wraps);
}

void test_sync_no_jumps() {
  // Slave at 2.37 x master, hard synced: no sample-to-sample jump near the full step size.
  BlOsc m, s;
  const float d1 = 300.f / kSynthRate, d2 = d1 * 2.37f;
  float prev = 0, maxStep = 0;
  for (int i = 0; i < 4000; ++i) {
    const float w = m.saw(d1, -1).wrap;
    const float v = s.saw(d2, w).v;
    if (i > 10) maxStep = fabsf(v - prev) > maxStep ? fabsf(v - prev) : maxStep;
    prev = v;
  }
  TEST_ASSERT_TRUE(maxStep < 1.6f);  // naive resets jump by up to 2
}

void test_wt_reads_level_and_frame() {
  static int16_t t[kWtTableSamples];
  WtBuiltinSrc src(0);
  wtBuild(src, t);
  WtOsc o;
  float mx = 0;
  for (int i = 0; i < 2000; ++i) {
    const float v = o.next(t, 220.f / kSynthRate, 220.f, 0.f, -1);
    TEST_ASSERT_FALSE(isnan(v));
    mx = fabsf(v) > mx ? fabsf(v) : mx;
  }
  TEST_ASSERT_TRUE(mx > 0.5f && mx <= 1.05f);
  TEST_ASSERT_EQUAL_FLOAT(0.f, o.next(nullptr, 0.01f, 220.f, 0.f, -1));  // missing table = silence
}

void test_wt_level_choice() {
  TEST_ASSERT_EQUAL_FLOAT(0.f, wtLevelPos(50.f));       // low notes: level 0
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.f, wtLevelPos(125.f));  // 125 * 128 = 16 kHz -> 1 octave above 8 kHz
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.f, wtLevelPos(20000.f));  // clamped
}

void test_voice_mix_and_silence() {
  SynVoice v;
  SynParams p;
  p.mode[0] = p.mode[1] = static_cast<uint8_t>(SynOsc::Saw);
  p.hz[0] = p.hz[1] = 220;
  p.mix = 0;
  v.trigger();
  v.control(p, 32);
  float buf[32] = {0};
  v.render(buf, 32, 1.f);
  float e = 0;
  for (float x : buf) e += fabsf(x);
  TEST_ASSERT_TRUE(e > 1.f);
  // Both oscillators on missing wavetables, no sub / noise: silent.
  p.mode[0] = p.mode[1] = static_cast<uint8_t>(SynOsc::Wt);
  p.wt[0] = p.wt[1] = nullptr;
  v.trigger();
  v.control(p, 32);
  memset(buf, 0, sizeof(buf));
  for (int k = 0; k < 4; ++k) v.render(buf, 32, 1.f);
  for (float x : buf) TEST_ASSERT_EQUAL_FLOAT(0.f, x);
}
```
(В `main` — RUN_TEST всех.)

**Step 2:** FAIL.

**Step 3: реализация**

`synth_syn.h`:
```cpp
#pragma once
#include <stdint.h>
#include "hot.h"
#include "model.h"
#include "synth_osc.h"
#include "wt_mip.h"

namespace mt {

// Band-limited oscillator (PolyBLEP, one sample late so a discontinuity can correct the sample
// before it too). Phase 0..1. Each call returns the sample and, for the master, where it wrapped.
struct BlOut {
  float v;
  float wrap;  // phase wrap in the last interval: distance to the current sample (0..1), -1 none
};
struct BlOsc {
  float t = 0;      // phase
  float prev = 0;   // the sample being finished (n - 1)
  float corr = 0;   // correction already due for sample n
  float tri = -1;   // Tri: leaky integral of the square
  void reset() { t = 0, prev = 0, corr = 0, tri = -1; }
  // sync: master wrap distance (BlOut::wrap) or -1. pw 0..1.
  BlOut saw(float dt, float sync);
  BlOut square(float dt, float pw, float sync);
  BlOut tri(float dt);  // no sync
};

// 0..kWtLevels-1 (fractional): level k has 128 >> k harmonics; keeps the top one under 8 kHz
// (one octave of margin, so levels k and k + 1 are both alias-free while crossfading).
float wtLevelPos(float hz);

// Wavetable oscillator over a mip-mapped table (nullptr = silent). pos 0..63 (fractional frame).
struct WtOsc {
  float t = 0;
  float prev = 0;  // one sample late, aligned with BlOsc
  void reset() { t = 0, prev = 0; }
  float next(const int16_t* table, float dt, float hz, float pos, float sync);
};

// Control-rate inputs of a SYNTH voice (Synth::controlSyn).
struct SynParams {
  uint8_t mode[2] = {0, 0};             // SynOsc
  const int16_t* wt[2] = {nullptr, nullptr};
  float hz[2] = {220, 220};
  float shape[2] = {0, 0};              // 0..1: PW 0.5..0.95 / frame 0..63; Saw / Tri ignore it
  float mix = 0;                        // 0 = osc 1, 1 = osc 2
  bool sync = false;
  float sub = 0;                        // 0..1
  uint8_t subOct = 1;                   // 1 or 2 octaves below osc 1
  float noise = 0;                      // 0..1
};

constexpr float kSynGain = 0.5f;

class SynVoice {
 public:
  void trigger();  // phases and ramps from zero (not legato)
  // Next control period of n samples: shape and mix ramp to p over it.
  void control(const SynParams& p, int n);
  MT_HOT void render(float* out, int n, float amp);

 private:
  MT_HOT float osc(int k, float sync, float& wrap);
  SynParams p_;
  float shape_[2] = {0, 0}, shapeStep_[2] = {0, 0};
  float mix_ = 0, mixStep_ = 0;
  BlOsc bl_[2], sub_;
  WtOsc wt_[2];
  uint32_t noise_ = 0x12345678u;
  bool snap_ = true;  // next control() jumps to its values (after trigger)
};

}  // namespace mt
```

`synth_syn.cpp` (ядро; `MT_HOT` на render/osc — как в `synth_fm.cpp`):
```cpp
#include "synth_syn.h"
#include <math.h>

namespace mt {
namespace {

inline float fracf(float x) { return x - static_cast<int>(x); }

// Step h at distance x (0..1) before sample n: corrections for n - 1 (o.prev) and n (o.corr).
inline void blep(BlOsc& o, float h, float x) {
  o.prev += 0.5f * h * x * x;
  o.corr += 0.5f * h * (2.f * x - x * x - 1.f);
}

}  // namespace

BlOut BlOsc::saw(float dt, float sync) {
  float wrap = -1;
  float tn = t + dt;
  if (sync >= 0) {
    // Reset at distance sync before sample n: the value just before the reset jumps to -1.
    const float te = tn - sync * dt;  // phase at the reset (unwrapped)
    if (te >= 1.f) blep(*this, -2.f, (te - 1.f) / dt + sync);  // own wrap first
    const float before = 2.f * fracf(te) - 1.f;
    blep(*this, -1.f - before, sync);
    tn = sync * dt;
  } else if (tn >= 1.f) {
    tn -= 1.f;
    wrap = tn / dt;
    blep(*this, -2.f, wrap);
  }
  t = tn;
  const BlOut r{prev, wrap};
  prev = 2.f * t - 1.f + corr;
  corr = 0;
  return r;
}

BlOut BlOsc::square(float dt, float pw, float sync) {
  float wrap = -1;
  const float t0 = t;
  float tn = t + dt;
  auto edges = [&](float a, float b) {  // own edges in phase interval (a, b], b - a <= 1
    if (a < pw && b >= pw) blep(*this, -2.f, (b - pw) / dt);
    if (b >= 1.f) {
      blep(*this, 2.f, (b - 1.f) / dt);
      if (b - 1.f >= pw) blep(*this, -2.f, (b - 1.f - pw) / dt);
    }
  };
  if (sync >= 0) {
    const float te = tn - sync * dt;
    edges(t0, te);
    const float fe = fracf(te);
    const float before = fe < pw ? 1.f : -1.f;
    blep(*this, 1.f - before, sync);  // reset to phase 0 = high
    tn = sync * dt;
    if (tn >= pw) blep(*this, -2.f, (tn - pw) / dt);
  } else {
    edges(t0, tn);
    if (tn >= 1.f) {
      tn -= 1.f;
      wrap = tn / dt;
    }
  }
  t = tn;
  const BlOut r{prev, wrap};
  prev = (t < pw ? 1.f : -1.f) + corr;
  corr = 0;
  return r;
}

BlOut BlOsc::tri(float dt) {
  const BlOut q = square(dt, 0.5f, -1);
  tri = tri * 0.9995f + q.v * 4.f * dt;
  return {tri, q.wrap};
}

float wtLevelPos(float hz) {
  const float x = log2f(hz * kWtHarm / 8000.f);
  return x < 0 ? 0 : (x > kWtLevels - 1 ? kWtLevels - 1 : x);
}

namespace {
MT_INLINE float readLevel(const int16_t* frame, int k, float t) {
  const int len = wtLevelLen(k);
  const int16_t* d = frame + wtLevelOff(k);
  const float x = t * len;
  const int i = static_cast<int>(x);
  const float f = x - i;
  const float a = d[i & (len - 1)], b = d[(i + 1) & (len - 1)];
  return a + (b - a) * f;
}
}  // namespace

float WtOsc::next(const int16_t* table, float dt, float hz, float pos, float sync) {
  const float r = prev;
  t = sync >= 0 ? sync * dt : t + dt;
  if (t >= 1.f) t -= 1.f;
  if (!table) {
    prev = 0;
    return r;
  }
  const float lp = wtLevelPos(hz);
  const int k = static_cast<int>(lp);
  const float kf = lp - k;
  const int k2 = k + 1 < kWtLevels ? k + 1 : k;
  const int f0 = static_cast<int>(pos);
  const float ff = pos - f0;
  const int f1 = f0 + 1 < kWtFrames ? f0 + 1 : f0;
  const int16_t* a = table + f0 * kWtFramePts;
  const int16_t* b = table + f1 * kWtFramePts;
  const float va = readLevel(a, k, t) + (readLevel(a, k2, t) - readLevel(a, k, t)) * kf;
  const float vb = readLevel(b, k, t) + (readLevel(b, k2, t) - readLevel(b, k, t)) * kf;
  prev = (va + (vb - va) * ff) * (1.f / kWtPeak);
  return r;
}

void SynVoice::trigger() {
  for (auto& o : bl_) o.reset();
  for (auto& o : wt_) o.reset();
  sub_.reset();
  snap_ = true;
}

void SynVoice::control(const SynParams& p, int n) {
  p_ = p;
  if (snap_) {
    snap_ = false;
    shape_[0] = p.shape[0], shape_[1] = p.shape[1], mix_ = p.mix;
    shapeStep_[0] = shapeStep_[1] = mixStep_ = 0;
    return;
  }
  const float inv = n > 0 ? 1.f / n : 1.f;
  for (int k = 0; k < 2; ++k) shapeStep_[k] = (p.shape[k] - shape_[k]) * inv;
  mixStep_ = (p.mix - mix_) * inv;
}

float SynVoice::osc(int k, float sync, float& wrap) {
  const float dt = p_.hz[k] * (1.f / kSynthRate);
  const float s = shape_[k];
  wrap = -1;
  switch (static_cast<SynOsc>(p_.mode[k])) {
    case SynOsc::Square: {
      const BlOut o = bl_[k].square(dt, 0.5f + 0.45f * s, sync);
      wrap = o.wrap;
      return o.v;
    }
    case SynOsc::Tri: {
      const BlOut o = bl_[k].tri(dt);
      wrap = o.wrap;
      return o.v;
    }
    case SynOsc::Wt: {
      // Master wrap of a WT osc 1 (for sync): phase wrap in this step.
      const float before = wt_[k].t;
      const float v = wt_[k].next(p_.wt[k], dt, p_.hz[k], s * (kWtFrames - 1), sync);
      if (sync < 0 && wt_[k].t < before) wrap = wt_[k].t / dt;
      return v;
    }
    default: {
      const BlOut o = bl_[k].saw(dt, sync);
      wrap = o.wrap;
      return o.v;
    }
  }
}

void SynVoice::render(float* out, int n, float amp) {
  const float g = amp * kSynGain;
  const float subDt = p_.hz[0] * (1.f / kSynthRate) / (p_.subOct >= 2 ? 4.f : 2.f);
  for (int i = 0; i < n; ++i) {
    float w1, w2;
    const float a = osc(0, -1, w1);
    const float b = osc(1, p_.sync ? w1 : -1, w2);
    float s = a + (b - a) * mix_;
    if (p_.sub > 0) s += sub_.square(subDt, 0.5f, -1).v * p_.sub;
    if (p_.noise > 0) {
      noise_ ^= noise_ << 13, noise_ ^= noise_ >> 17, noise_ ^= noise_ << 5;
      s += static_cast<int32_t>(noise_) * (1.f / 2147483648.f) * p_.noise;
    }
    out[i] += s * g;
    shape_[0] += shapeStep_[0], shape_[1] += shapeStep_[1], mix_ += mixStep_;
  }
}

}  // namespace mt
```
Замечания для исполнителя:
- Рампы: `control` ведёт shape/mix от текущих значений к целевым за период; после `trigger()` первый `control` прыгает сразу (`snap_`), чтобы нота не стартовала с хвоста предыдущей.
- Если тест `test_sync_no_jumps` не проходит из-за ошибки знаков в `saw(sync)` — проверить на бумаге: до сброса значение `before`, после — `-1`, высота `h = -1 - before`, расстояние `sync`.

**Step 4:** `pio test -e native -f test_synth_syn` → PASS.

---

### Task 7: SYNTH в `Synth`

**Files:**
- Modify: `lib/core/src/synth.h`, `lib/core/src/synth.cpp`, `lib/core/src/synth_voice.h`
- Test: `test/test_synth/test_main.cpp`

**Изменения:**
1. `synth.h`: рядом с `SampleSource`:
   ```cpp
   // Mip-mapped wavetables (wt_mip.h layout) for SYNTH instruments: a built-in "*NAME" or a name of
   // the project's list; nullptr if absent.
   struct WtSource {
     virtual const int16_t* findWt(const char* name) const = 0;
   };
   ```
   В `Synth`: `void setWavetables(const WtSource* w) { wt_ = w; }`, поле `const WtSource* wt_ = nullptr;`, метод `void controlSyn(Voice& v, const Instrument& m, float pitch, int dt, float l, float vol);`.
2. `synth_voice.h`: в `Voice` — `bool syn = false; uint32_t senvT = 0; SynVoice sv;` (+ `#include "synth_syn.h"`). Константа `constexpr bool kSynHeavy = false;  // SYNTH counts against kFmVoiceMax (set after the bench)`.
3. `noteOn`:
   - `const bool synT = m.type == InstrType::Synth;`; `heavy = fm || drumT || (synT && kSynHeavy)`.
   - `mono` для SYNTH = `m.mono`, glide как у CHIP (ветка `overlap && m.glide && (!fm || tone)` уже подходит).
   - `v.syn = synT;` рядом с `v.fm = fm;`.
   - Макро-локи: `v.lockMask = (heavy || synT) ? r.lockMask : (r.lockMask & ~kMacroBits);`.
   - Если `synT && !overlap`: `v.sv.trigger(); v.senvT = 0;` (legato держит фазы и env→SHAPE, как фильтр).
   - LFO фаза: SYNTH как CHIP (уже `if (!fm && !overlap)`).
4. `control()`: после `controlFilter`: `if (v.syn) { controlSyn(v, m, pitch, dt, l, lfoVol); return; }`.
5. `controlSyn`:
   ```cpp
   void Synth::controlSyn(Voice& v, const Instrument& m, float pitch, int dt, float l, float vol) {
     float mac[kFmMacros];
     macros(v, m, l, mac);
     v.senvT += static_cast<uint32_t>(dt);
     const float e = filterEnv(v.senvT, m.synEAtk, m.synEDec);  // 0..1, AD (decay 0 = hold)
     const float senv = (mac[kMacSenv] - 64.f) * (1.f / 64.f) * e;
     SynParams p;
     for (int k = 0; k < 2; ++k) {
       p.mode[k] = m.synOsc[k] < static_cast<uint8_t>(SynOsc::Count) ? m.synOsc[k] : 0;
       p.wt[k] = v.synWt[k];
       p.shape[k] = clampf(mac[kMacShp1 + k] * (1.f / 127.f) + senv, 0.f, 1.f);
     }
     const float det = (mac[kMacDet] - 64.f) * (50.f / 64.f) * 0.01f;  // semitones
     p.hz[0] = noteHz(pitch);
     p.hz[1] = noteHz(pitch + clampf(m.synSemi, -24, 24) + det);
     p.mix = mac[kMacMix] * (1.f / 127.f);
     p.sync = m.synSync;
     p.sub = (m.synSub > 127 ? 127 : m.synSub) * (1.f / 127.f);
     p.subOct = m.synSubOct ? 2 : 1;
     p.noise = (m.synNoise > 127 ? 127 : m.synNoise) * (1.f / 127.f);
     v.sv.control(p, dt ? dt : ctlLeft_);
     const uint8_t iv = m.vol > 127 ? 127 : m.vol;
     v.amp = v.gain * iv * trackVol(v.track) * vol * (1.f / (127.f * 127.f));
   }
   ```
   (Сигнатуру `filterEnv` проверить в `synth_filter.h`/`synth.cpp` — используется в `controlFilter`; если она принимает `fDec` с иным смыслом, повторить ту же формулу.)
6. Таблицы в `Voice`: `const int16_t* synWt[2] = {nullptr, nullptr};` — резолвятся в `noteOn` (аудио-задача; имя копируется посимвольно, как `m.sample`): `v.synWt[k] = (wt_ && p.mode==Wt && name[0]) ? wt_->findWt(name) : nullptr`. Смена таблицы в UI во время ноты подхватится следующей нотой.
7. `renderVoice`: перед `else if (v.sample)`:
   ```cpp
   } else if (v.syn) {
     float tmp[kControl] = {0};
     v.sv.render(tmp, n, v.amp);
     for (int i = 0; i < n; ++i) dst[i] += tmp[i] * v.env.next();
   ```
8. Подсчёт тяжёлых голосов (`x.fm || x.drum`) — добавить `|| (x.syn && kSynHeavy)` во всех местах (`grep -n "fm || x.drum\|\.fm || .*\.drum" lib/core/src`).

**Step 1: тесты** (в `test/test_synth/test_main.cpp`; `setUp` уже делает 16 CHIP-инструментов)
```cpp
struct ArrayWt : WtSource {
  int16_t* t;
  const int16_t* findWt(const char* n) const override { return strcmp(n, "*SAWSQR") == 0 ? t : nullptr; }
};

void test_synth_saw_sounds() {
  Instrument& m = p->instruments[0];
  instrSetType(m, InstrType::Synth);
  m.sustain = 127;
  noteOn(0, 0, 57);  // 220 Hz
  s->render(buf);
  int nz = 0;
  for (int i = 0; i < Synth::kBlock; ++i) nz += buf[i] != 0;
  TEST_ASSERT_TRUE(nz > 100);
  // 25 blocks = 100 ms: ~22 falling crossings at 220 Hz (helper crossings() of this file).
  TEST_ASSERT_INT_WITHIN(2, 22, crossings(25));
}
```
```cpp
void test_synth_missing_wt_silent() {
  Instrument& m = p->instruments[0];
  instrSetType(m, InstrType::Synth);
  m.synOsc[0] = m.synOsc[1] = static_cast<uint8_t>(SynOsc::Wt);
  strcpy(m.synWt[0], "NOPE");
  noteOn(0, 0, 60);
  TEST_ASSERT_TRUE(silentBlocks(4));  // no source set at all
}

void test_synth_wt_plays() {
  static int16_t t[kWtTableSamples];
  WtBuiltinSrc src(0);
  wtBuild(src, t);
  ArrayWt w;
  w.t = t;
  s->setWavetables(&w);
  Instrument& m = p->instruments[0];
  instrSetType(m, InstrType::Synth);
  m.synOsc[0] = static_cast<uint8_t>(SynOsc::Wt);
  strcpy(m.synWt[0], "*SAWSQR");
  noteOn(0, 0, 60);
  TEST_ASSERT_FALSE(silentBlocks(2));
}

void test_synth_macro_lock_applies() {
  // fx DCY on a SYNTH step locks SHP1 (macro 0) — the lock reaches the voice.
  Instrument& m = p->instruments[0];
  instrSetType(m, InstrType::Synth);
  send(0, 0, 0xF5, static_cast<uint8_t>(Fx::DCY), 100);
  noteOn(0, 0, 60);
  s->render(buf);
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_TRUE(v.lockMask & (1 << kMacShp1));
  TEST_ASSERT_EQUAL(100, v.lock[kMacShp1]);
}
```
(Формат сообщения fx `0xF5 cmd val` — сверить с уже существующими тестами FM-локов в этом файле и повторить их вызов.)

**Step 2:** FAIL. **Step 3:** изменения выше. **Step 4:** `pio test -e native -f test_synth` → PASS; затем `pio test -e native` целиком.

---

### Task 8: кодеки — `SYNI`, `WTBL`, `fixInstrument`

**Files:**
- Modify: `lib/core/src/inst_codec.h/.cpp`, `lib/core/src/project_io.cpp`
- Test: `test/test_project_io/test_main.cpp`

**Формат записи `SYNI`** (`kSynRecSize = 48`): `[0] synOsc[0]`, `[1] synOsc[1]`, `[2..17] synWt[0]` (16 байт, без нуля), `[18..33] synWt[1]`, `[34] synSemi`, `[35] synSync`, `[36] synSub`, `[37] synSubOct`, `[38] synNoise`, `[39] synEAtk`, `[40] synEDec`, `[41..47]` reserved = 0.
Чанк: `"SYNI"`, `1 + kInstruments * kSynRecSize`, u8 count, записи — как `FLTR` (`saveProject` ~строка 310, читатель через `readRecords`).

**`WTBL`**: u8 count, записи `kWtblSize = 20`: имя 16 + crc u32. Читатель по образцу `readSmpl` (пустые и повторные имена пропускать, повторный чанк заменяет список).

**unpackSyn** клампит: osc < `SynOsc::Count` иначе 0; semi −24..24; sync 0/1; sub/noise/eatk/edec 0..127; subOct 0..1; имена — ровно 16 байт + `\0`.

**fixInstrument:** для не-Synth ничего; для Synth — ничего сверх unpack (оставить комментарий). `static_assert(kSynRecSize <= kMaxRec)` и `kWtblSize <= kMaxRec`.

**Step 1: тесты**
- roundtrip: инструмент SYNTH со всеми полями ≠ дефолту + 2 таблицы в `wavetables` → `saveProject` → `loadProject` → поля и список равны (взять хелперы сохранения/загрузки в память из существующих тестов `test_project_io`).
- старый файл без чанков: загрузка → дефолты SYNTH-полей, `wavetableCount == 0` (существующие тесты старых форматов должны остаться зелёными).
- мусор: `synOsc = 9`, `synSemi = 100` → после load 0 и 24.

**Step 2:** FAIL. **Step 3:** реализация. **Step 4:** PASS.

---

### Task 9: список таблиц проекта и банк (`sample_set`)

**Files:**
- Modify: `lib/core/src/sample_set.h/.cpp`
- Test: `test/test_sample_set/test_main.cpp`

**API** (в `sample_set.h`):
```cpp
// Wavetables: bank entries named "w" + crc32 (8 lower-case hex) of the canonical source, frames
// kWtTableSamples, rate 0; built-ins "*NAME" are never evicted or cleared.
void wtKey(uint32_t crc, char out[kSampleNameMax + 1]);
bool isWtKey(const char* name);
int projWtFind(const Project& p, const char* name);  // ignoring case, -1
// Adds or replaces; -1 if full or the name is invalid (projectBaseValid) or starts with '*'.
int projWtSet(Project& p, const char* name, uint32_t crc);
void projWtRemove(Project& p, int i);
// Removes entries no instrument references (any osc, any mode). Count removed.
int projWtPrune(Project& p);
int projWtBank(const Project& p, const SampleBank& b, int i);  // bank index or -1
```
`UsedKeys`: дополнительно собирает crc таблиц проекта; `has(e)`: `e.name[0] == '*'` → true (вшитые всегда «используются»); `isWtKey(e.name)` → сравнить crc и `e.frames == kWtTableSamples`. В `bankMakeRoom` ключевыми считать `isSampleKey || isWtKey`; в `bankClearUnused` удалять и неиспользуемые `isWtKey`.

**Step 1: тесты**
- `wtKey(0xABCDEF01)` → `"wabcdef01"`; `isWtKey` true/false (`"abcdef01"` — нет).
- `projWtSet`/`Find`/`Remove`/дубликат имени без учёта регистра заменяет crc; `"*X"` → -1.
- `projWtPrune`: две таблицы, инструмент ссылается на одну → удалена одна.
- банк в RAM (`RamFlash` из существующих тестов): запись `*SAWSQR` и неиспользуемый `w…` → `bankClearUnused` удаляет только `w…`; `bankMakeRoom` при нехватке места не трогает `*SAWSQR`; используемый проектом `w…` не вытесняется.

**Step 2:** FAIL. **Step 3:** реализация. **Step 4:** PASS; `pio test -e native -f test_sample_bank` тоже зелёный.

---

### Task 10: пресеты `.mti` v3 и заводские SYNTH

**Files:**
- Modify: `lib/core/src/preset_io.h/.cpp`, `lib/core/src/presets_factory.cpp`
- Test: `test/test_preset_io/test_main.cpp`, `test/test_presets_factory/test_main.cpp`

1. `kPresetVersion = 3`, `kPresetSizeV2` = текущий `kPresetSize`, новый `kPresetSize = kPresetSizeV2 + kSynRecSize`; `loadPreset` по версии берёт размер, v3 распаковывает `unpackSyn` (v1/v2 — дефолты SYNTH-полей).
2. `applyPreset(dst, src, sampleFound)` — для SYNTH копирует SYNI-поля как есть (имена таблиц; разрешение имён — в прошивке, Task 13).
3. Заводские: 10 записей типа `InstrType::Synth`, категория `"SYNTH"` (посмотреть, как категории/папки задаются у CHIP/FM в `kAll[]`, и повторить), только вшитые таблицы. Хелпер `sy(m, osc1, wt1, osc2, wt2)`:

| имя | осц 1 | осц 2 | ключевое |
|---|---|---|---|
| BASS | Saw | Square, semi −12 | MIX 40, LP cut 50, fenv +30, dec 30 |
| ACID | Saw | — (MIX 0) | LP reso 100, fenv +45, fDec 25, mono, glide 10 |
| LEAD | Saw | Saw | DET 72, LP cut 90 |
| PAD | WT *SAWSQR | WT *FORMANT | MIX 64, A 70, R 80, LFO tri→SHP1 depth 20 |
| PLUCK | Square | — | SENV 100, eDec 25, D 35 S 0 |
| PWMSTR | Square | Square | LFO sine→SHP1 30, DET 70, A 50 |
| SYNCLD | Saw | Saw sync, semi +7 | SENV 110, eDec 60 |
| WTSWEEP | WT *SINSAW | — | SENV 127, eAtk 0, eDec 80 |
| BELL | WT *BELL | — | SHP1 40, D 70 S 0 R 70 |
| SUBBASS | Tri | — | sub 100 oct −1, LP cut 40 |

**Step 1: тесты:** v3 roundtrip SYNTH-полей; v2-файл читается (дефолты); `factoryCount` вырос на 10, все SYNTH-пресеты собираются, их `synWt` либо пусты, либо `wtBuiltinFind >= 0`.

**Step 2:** FAIL. **Step 3:** реализация. **Step 4:** PASS.

---

### Task 11: прошивка — банк таблиц (`src/audio/bank.*`, `audio.cpp`)

**Files:**
- Modify: `src/audio/bank.h`, `src/audio/bank.cpp`, `src/audio/audio.cpp`

1. `WtBankSource final : mt::WtSource` (по образцу `BankSource::find`, без snprintf — ключ собирать вручную): имя `*…` → `theBank.find(name)`; иначе `projWtFind` → `wtKey(crc)` → `find`; проверить `frames == kWtTableSamples && rate == 0`; вернуть `theBank.data(i)` (mmap). `const mt::WtSource* wavetableSource();`.
2. В `audio.cpp` там, где вызывается `synth->setBank(...)`, добавить `synth->setWavetables(audio::wavetableSource());`.
3. `BankResult importWtToCache(const char* path, const mt::Project& p, uint32_t& crc, const uint32_t* knownCrc = nullptr, BankProgressFn cb = nullptr, void* ctx = nullptr);`
   - Busy, если движок не idle (как `importToCache`).
   - `openWav` → `wavParse`; данных ≤ `kWtMaxSrcSamples` кадров, иначе `Unsupported`.
   - Быстрый путь: `info.hasCrc` (или `knownCrc`) и в банке есть `wtKey(crc)` с `frames == kWtTableSamples` → Ok без чтения.
   - Иначе: буфер PSRAM на моно-данные (`wavToMono` поблочно) + PSRAM 96 КБ на таблицу; `mt::wtImport(mono, n, info.clmFrame, table)` (`Silent`/`BadLength` → `NotWav`/`Unsupported`); `crc = sampleCrc(level0)` — для этого `wtLevel0(table, src)` во временный буфер 32 КБ.
   - Если ключ уже в банке — освободить буферы, Ok. Иначе `FlashWork`, удалить старый `~import`, `bankMakeRoom(theBank, p, kWtTableSamples, …)`, `begin("~import", kWtTableSamples, 0, 60)`, `write`, `commit`, `rename` в `wtKey(crc)`.
4. `BankResult exportWt(const char* name, const mt::Project& p, const char* path);` — найти запись (как в п.1), `wtLevel0` → WAV mono 16 bit, `wavHeader(out, kWtSrcSamples, 44100, 60, crc)` (crc = ключ) + данные, через `path.tmp` → rename (как `exportWav`).
5. Вшитые: в `bankBegin()` после монтирования — для каждого `wtBuiltinName(i)`, которого нет в банке: PSRAM 96 КБ, `WtBuiltinSrc s(i); wtBuild(s, buf)`, запись в банк под этим именем (`rate = 0`). Перед стартом аудио-задачи `FlashWork` не нужен, если `bankBegin` вызывается до неё (проверить порядок в `audio::begin`; если после — использовать `FlashWork`). Если места нет — `bankMakeRoom` с пустым проектом (`mt::Project` статический пустой нельзя — 235 КБ; передать `nullptr`-вариант нельзя → использовать текущий проект, если уже есть, иначе пропустить и залогировать `Serial`).
6. `bankResultText` — ничего нового не нужно.

**Проверка:** `pio run -e wt32` собирается.

---

### Task 12: прошивка — save/load таблиц проекта (`src/storage/storage.cpp`)

**Files:**
- Modify: `src/storage/storage.cpp`

1. Перед записью проекта: `mt::projWtPrune(live)` (под `engine::lockProject()`, как другие правки проекта).
2. `syncFolder`: для каждой `live.wavetables[i]` — путь `/projects/NAME/wt/<name>.wav`; `mkdir` подпапки; пропуск, если файл актуален (`fileCurrent` с crc из `mtcr`), иначе `audio::exportWt`. Удаление устаревших файлов — и в `wt/` (только `.wav`, которых нет в списке). Существующую чистку корня папки не трогать.
3. Save As (папка-источник): недостающие в банке таблицы копировать `copyFile` из старой `wt/`, как сэмплы.
4. `pullSamples` → после сэмплов цикл по таблицам: `audio::importWtToCache(path, live, crc, &want, progress, &pc)`; при ошибке — счётчик missing (таблица остаётся в списке, осц молчит).
5. Тост/лог про missing — тот же, что для сэмплов (общее число).

**Проверка:** `pio run -e wt32`.

---

### Task 13: UI — INST для SYNTH и выбор таблицы

**Files:**
- Modify: `src/ui/inst_screen.h`, `src/ui/inst_screen.cpp`, `src/ui/preset_browser.cpp`
- Create: `src/ui/wt_picker.h`, `src/ui/wt_picker.cpp`

1. **Имена типов:** массивы `{"CHIP","SAMPLE","FM","DRUM"}` → + `"SYNTH"` (Type-строка и `drawPageBar`), проверки `t < 4` → `t < 5`.
2. **Строки SYNTH** — массив `syn_[]`: общие 12 строк как у всех (цикл `sets[]` в конструкторе — добавить `syn_`), затем строки типа:
   - OSC (страница 3): `Osc1` (SAW/SQR/TRI/WT), `Table1` (имя, `*` вшитые; тап/клик → `WtPicker`; серое, если не WT; красное, если таблица не находится через `audio::wavetableSource()`), `Shape1` (0..127, макрос SHP1; серое для Saw/Tri), `Osc2`, `Table2`, `Shape2`, `Semi2` (±24), `Detune` (DET, показ `±N ct`), `Sync` (OFF/ON), `Mix` (MIX, показ `N%` осц 2).
   - MOD (страница 4): `Sub` (0..127), `Sub oct` (-1/-2, серое при Sub 0), `Noise`, `Env>Shp` (SENV, показ `(v−64)`), `Env atk`, `Env dec` (`envTime`, 0 = HOLD; серые при SENV = 64).
   - Переключение осц в WT с пустой таблицей → подставить `*SAWSQR`.
   - ADSR/Mode/Glide — как у CHIP (не серые). Хвост `initTail(syn_ + kSynRows, true)` (макро-цели LFO).
3. **Подписи LFO dest** для SYNTH: `SHP1, SHP2, MIX, DET, SENV` вместо `DECAY..CONTOUR` (лямбда `kLfoDest` смотрит `inst().type`).
4. **Страницы:** `pageCount()` = 6 для SYNTH, 5 иначе. Логические страницы: `MAIN, ENV, TYPE, [TYPE2], FILT, LFO`. Заменить `kPages`/`kPageW` на функции (`pageW() = kScreenW / pageCount()`); `showPage` по логической странице (для SYNTH: `kPgType` = OSC-строки, `kPgType2` = MOD-строки; смещения хвоста от `kCommon + typeCount()` как сейчас). При смене типа с 6 на 5 страниц — `page_` клампится. Имена: `OSC`, `MOD` для SYNTH.
5. **Превью кадра** на OSC: справа от значений (как `drawEnv` на ENV, x 280..470, y 8..112): для выделенного осц (строки Osc1..Shape1 → 1, иначе 2), если WT и таблица найдена — уровень 0 кадра `round(shape/127*63)`: 256 точек → ломаная 190 px; иначе — схематичная форма Saw/Square(PW)/Tri.
6. **`WtPicker`** (оверлей над списком, как `PresetBrowser`): пункты — вшитые (`wtBuiltinName`), затем `wavetables` проекта, затем `IMPORT…`. `IMPORT…` открывает список `/wavetables` (с подпапками; переиспользовать логику `FileScreen::openWavList` — вынести общий листинг в функцию, если проще, или повторить с `hw::sdNextEntry`). Выбор файла:
   - если играет — тост `STOP PLAYBACK FIRST` (как `FileScreen::playbackBusy`);
   - имя = база файла, обрезанная до 16 и `projectBaseValid`-совместимая; при конфликте с другим crc — суффикс `~2`…;
   - `audio::importWtToCache` с прогресс-тостом → `projWtSet` → имя в `synWt[k]` → `markDirty`.
7. **Пресеты (`PresetBrowser::apply`):** для SYNTH-пресета каждое имя таблицы: вшитая или есть в проекте → ок; иначе, если не играет, попытка `importWtToCache("/wavetables/<имя>.wav")` + `projWtSet`; не вышло — имя остаётся (покажется красным). Сохранение пресета — как есть.

**Проверка:** `pio run -e wt32`. Ручная — в сводке.

---

### Task 14: Wi-Fi — папка `/wavetables`

**Files:**
- Modify: `lib/core/src/file_rules.h/.cpp`, `src/net/web.cpp` (+ встроенная страница, если список папок там захардкожен)
- Test: `test/test_file_rules/test_main.cpp`

1. `WebDir::Wavetables` (`"wavetables"` → `/wavetables`): подпапки разрешены (как Samples), файлы только `.wav`, лимит размера 1 МБ (`webMaxBytesIn`).
2. `web.cpp`: в `handleList`/upload/rename/delete/mkdir/rmdir — по общим правилам, добавить вариант в парсинг `dir`; на странице — пункт выбора папки `wavetables`.
3. Тесты `file_rules`: `parseWebDir("wavetables")`, `.wav` разрешён, `.mtp` нет, лимит.

**Шаги:** тест → FAIL → реализация → PASS → `pio run -e wt32`.

---

### Task 15: бенч `-DAUDIO_BENCH_SYN`

**Files:**
- Modify: `src/audio/audio.cpp` (рядом с `benchDrum*`, те же точки вызова и guard-ы на строках ~185/423)

`benchSynBegin`: инструменты 8..15 → `instrSetType(Synth)`, осц 1 WT `*SAWSQR`, осц 2 WT `*FORMANT`, MIX 64, LFO sine→SHP1 depth 30 rate 40, sub 40, LP cut 90; дорожки 0..7 `out = Int`, `instr = 8 + t`, POLY. `benchSynTick` раз в 2 с: на каждой дорожке 2 ноты (аккорд) → 16 голосов; при `-DAUDIO_BENCH_SYN_N=8` — 1 нота. Результат — CPU в статусбаре (как у FM/DRUM).

**Проверка:** `pio run -e wt32` с флагом и без.

---

### Task 16: документация

**Files:**
- Modify: `docs/manual.html` (раздел INST → SYNTH: осцилляторы, таблицы, импорт /wavetables, макросы и fx DCY..CON, страницы OSC/MOD), `README.md` (строка про SYNTH и wavetable), `lib/core/src/fx_info.cpp` (описания DCY..CON: «FM / DRUM / SYNTH macro»), комментарий-шапка `src/ui/inst_screen.h` (страницы SYNTH).
- Modify: дизайн-док — пометка об отклонении (вшитые таблицы генерируются в банк при старте; `wt/` в папке проекта).

---

## Сводка для проверки на железе (после всех задач)

1. Разовая прошивка; первый старт — генерация 8 вшитых таблиц (≈ 770 КБ банка): время старта, лог.
2. Бенч `AUDIO_BENCH_SYN` (16 и 8 голосов): CPU; при блоке > 4 мс → `kSynHeavy = true` (лимит 8).
3. Скопировать `/Volumes/Docs/samples/Wavetables/*.WAV` (без подпапок) на карту в `/wavetables`; импорт пары таблиц из INST → Table → IMPORT; время импорта.
4. Save → удалить запись из банка (FILE → SAMPLES → Clear cache) → Load: таблица подтянулась из `/projects/NAME/wt/`.
5. Wi-Fi: папка wavetables — список, загрузка.
6. На слух: пила/квадрат на высоких нотах без «звона» алиасинга, sync-lead, PWM, переход мип-уровней при глиссандо (SLD) без скачков тембра.
