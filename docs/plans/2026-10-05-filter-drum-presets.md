# Фильтр, DRUM 808/909, пресеты — план реализации

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** SVF-фильтр с огибающей на всех типах инструментов, LFO для всех типов, новый тип DRUM (16 машин 808/909), пресеты инструментов (заводские в прошивке + свои на SD по подпапкам) с живым прослушиванием.

**Architecture:** фильтр — поля `Instrument` + `Svf` в `Voice`, cutoff на control-rate в `Synth::controlFilter`, рендер голоса через временный буфер. DRUM — движок `synth_drum` (тоны, металл из 6 квадратов, шум, click, drive) и чистые функции-машины `synth_drum_machines`, по образцу FM. Пресеты — файл `.mti` из тех же записей, что в `.mtp` (`inst_codec`), заводские — таблица функций во flash, UI — модальный `PresetBrowser` во вкладке INST.

**Tech Stack:** C++17, PlatformIO, Unity (native-тесты), ESP32-S3 Arduino, LovyanGFX.

**Дизайн:** [2026-10-05-filter-drum-presets-design.md](2026-10-05-filter-drum-presets-design.md).

**Правила проекта:** не коммитить (шаги Commit пропущены — пользователь коммитит сам). Все задачи подряд, без остановок на проверку железа; сводка в конце.

**Поправки после «сэмплы — часть проекта» (2026-10-05-project-samples):** `Project` теперь содержит `samples[128]` (чанк `SMPL`); сэмпл SAMPLE-инструмента ищется в `project.samples` (`projSampleFind`), а не в банке напрямую. Значит: в Task 1 `sizeof(Project)` = текущее значение (103184) + 128; в Task 3 `inst_codec` выносить из актуального `project_io.cpp` (там уже `SMPL`); в Task 11/14 `sampleFound` = `projSampleFind(app_.project(), src.sample) >= 0`. Код INST и FILE уже изменён той сессией — читать актуальный.

**Команды** (`pio` не в PATH: `~/.platformio/penv/bin/pio`):
- один тест: `pio test -e native -f test_synth`
- все тесты: `pio test -e native`
- прошивка: `pio run -e wt32`

**Порядок:** 1–5 фильтр и LFO · 6–9 DRUM · 10 INST UI · 11–12 формат и заводские пресеты · 13–15 хранение, браузер, Wi-Fi · 16 бенч · 17 документация.

---

### Task 1: модель — фильтр, DRUM, функции

**Files:**
- Modify: `lib/core/src/model.h`
- Modify: `lib/core/src/model.cpp`
- Modify: `src/storage/storage.cpp:42-43` (static_assert размеров)
- Test: `test/test_model/test_main.cpp`

**Step 1: тесты** (дописать в `test/test_model/test_main.cpp`, зарегистрировать в `main`)

```cpp
void test_cutoff_hz_range() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.f, cutoffHz(0));
  TEST_ASSERT_FLOAT_WITHIN(1.f, 14000.f, cutoffHz(127));
  TEST_ASSERT_TRUE(cutoffHz(64) > cutoffHz(63));
}

void test_reso_q_range() {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, resoQ(0));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.f, resoQ(127));
}

void test_filter_env_shape() {
  // fAtk 0: peak at once; fDec 0: holds the peak.
  TEST_ASSERT_EQUAL_FLOAT(1.f, filterEnv(0, 0, 0));
  TEST_ASSERT_EQUAL_FLOAT(1.f, filterEnv(100000, 0, 0));
  // Attack: linear 0 -> 1 over envTimeMs(fAtk).
  const uint32_t a = envTimeMs(60) * kSampleRateHz / 1000;
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.5f, filterEnv(a / 2, 60, 40));
  // Decay: -60 dB at envTimeMs(fDec) after the attack.
  const uint32_t d = envTimeMs(40) * kSampleRateHz / 1000;
  TEST_ASSERT_FLOAT_WITHIN(0.0005f, 0.001f, filterEnv(a + d, 60, 40));
  TEST_ASSERT_TRUE(filterEnv(a + d / 2, 60, 40) < 1.f);
}

void test_drum_set_machine() {
  Instrument m;
  drumSetMachine(m, static_cast<uint8_t>(DrumMachine::Hh8));
  TEST_ASSERT_EQUAL(static_cast<int>(DrumMachine::Hh8), m.machine);
  drumSetMachine(m, 200);
  TEST_ASSERT_EQUAL(static_cast<int>(DrumMachine::Count) - 1, m.machine);
}

void test_instr_set_type() {
  Instrument m;
  m.machine = 12;
  instrSetType(m, InstrType::Fm);
  TEST_ASSERT_TRUE(m.machine < static_cast<int>(FmMachine::Count));
  instrSetType(m, InstrType::Drum);
  TEST_ASSERT_TRUE(m.type == InstrType::Drum);
  // Macro LFO targets exist on FM / DRUM only.
  m.lfoDest = static_cast<uint8_t>(LfoDest::Col);
  instrSetType(m, InstrType::Chip);
  TEST_ASSERT_EQUAL(static_cast<int>(LfoDest::Pitch), m.lfoDest);
  m.lfoDest = static_cast<uint8_t>(LfoDest::Cutoff);
  instrSetType(m, InstrType::Sample);
  TEST_ASSERT_EQUAL(static_cast<int>(LfoDest::Cutoff), m.lfoDest);
}

void test_filter_defaults_off() {
  Instrument m;
  TEST_ASSERT_EQUAL(static_cast<int>(FltMode::Off), m.fltMode);
  TEST_ASSERT_EQUAL(127, m.cutoff);
}
```

`kSampleRateHz` — локальная константа теста `= 32000` (в `test_model` нет `synth_osc.h`); при расхождении с `kSynthRate` поправить.

**Step 2: запустить** `pio test -e native -f test_model` — ожидается ошибка компиляции (`cutoffHz` не объявлен).

**Step 3: реализация**

`model.h`:

```cpp
enum class InstrType : uint8_t { Chip, Sample, Fm, Drum, Count };
// DRUM machines (808 / 909 models). Stored in files: new machines go before Count only.
enum class DrumMachine : uint8_t {
  Bd8, Sd8, Tom8, Cp8, Rs8, Cl8, Cb8, Hh8, Cy8, Bd9, Sd9, Tom9, Cp9, Rs9, Hh9, Cy9, Count
};
// Instrument::machine of either type fits below this.
constexpr int kMachineMax = 16;
static_assert(static_cast<int>(DrumMachine::Count) <= kMachineMax, "machine field");
enum class FltMode : uint8_t { Off, Lp, Bp, Hp, Count };
// Dec..Con = macro index + 1 (FM / DRUM only). Stored in files: new targets before Count only.
enum class LfoDest : uint8_t { Pitch, Dec, Col, Shp, Swp, Con, Vol, Cutoff, Count };
// Lock bits (Voice / TrackRt lockMask, lock[]): FM / DRUM macros 0..4, then the filter.
enum LockBit : uint8_t { kLockFlt = kFmMacros, kLockRes, kLocks };
```

`FmMacro` и `kFmMacros` объявлены выше `LockBit` — порядок в файле: `FmMacro`, затем `LockBit`.

Поля в конце `Instrument` (после `lfoDest`):

```cpp
  // Filter, every type: see cutoffHz, resoQ, filterEnv.
  uint8_t fltMode = 0;          // FltMode
  uint8_t cutoff = 127;         // 0..127
  uint8_t reso = 0;             // 0..127
  int8_t fenv = 0;              // -64..63: +-6 octaves at the envelope's peak
  uint8_t fAtk = 0, fDec = 40;  // envTimeMs; fDec 0 = hold
  uint8_t keytrack = 0;         // 0..127 = 0..100 %
```

Комментарий у `machine` сменить на `// FmMachine or DrumMachine, by type`, у `macro` — `// DECAY..CONTOUR 0..127 (FM / DRUM)`.

Объявления после `fmSetMachine`:

```cpp
// Filter cutoff 0..127 (fractional after locks / LFO) -> 20 Hz x 700^(v/127): 20 Hz .. 14 kHz.
float cutoffHz(float v);
// Resonance 0..127 -> Q 0.5 x 40^(v/127): 0.5 .. 20.
float resoQ(float v);
// Filter envelope t samples after the trigger: 0 -> 1 linearly over envTimeMs(fAtk), then down to
// -60 dB over envTimeMs(fDec) (exponential); fDec 0 holds 1.
float filterEnv(uint32_t t, uint8_t fAtk, uint8_t fDec);
// DRUM: sets the machine (clamped) and its default macros.
void drumSetMachine(Instrument& m, uint8_t machine);
// Changes the type. FM / DRUM: the machine (clamped) with its default macros (they mean other
// things per type). CHIP / SAMPLE: a macro LFO target becomes PITCH.
void instrSetType(Instrument& m, InstrType t);
```

`model.cpp` (`kRate` — локальная копия 32000: `model.cpp` не зависит от синта; `static_assert` в `synth_osc.h` не нужен — тест Task 4 проверит совпадение фильтра с синтом):

```cpp
namespace {
constexpr float kRate = 32000.f;  // = kSynthRate (synth_osc.h)
}

float cutoffHz(float v) {
  v = v < 0 ? 0 : (v > 127 ? 127 : v);
  return 20.f * powf(700.f, v / 127.f);
}

float resoQ(float v) {
  v = v < 0 ? 0 : (v > 127 ? 127 : v);
  return 0.5f * powf(40.f, v / 127.f);
}

float filterEnv(uint32_t t, uint8_t fAtk, uint8_t fDec) {
  const uint32_t a = static_cast<uint32_t>(envTimeMs(fAtk) * kRate / 1000.f);
  if (t < a) return static_cast<float>(t) / a;
  const float d = envTimeMs(fDec) * kRate / 1000.f;
  if (d <= 0) return 1.f;
  return expf(-6.9077553f * (t - a) / d);
}

void drumSetMachine(Instrument& m, uint8_t machine) {
  // DECAY, COLOR, SHAPE, SWEEP, CONTOUR per machine (see synth_drum_machines.cpp).
  static const uint8_t kDefaults[static_cast<int>(DrumMachine::Count)][kFmMacros] = {
      {90, 20, 10, 40, 50},  // BD8
      {55, 50, 70, 30, 64},  // SD8
      {75, 20, 0, 40, 50},   // TOM8
      {60, 64, 64, 40, 50},  // CP8
      {40, 64, 30, 20, 0},   // RS8
      {50, 64, 0, 10, 0},    // CL8
      {70, 64, 64, 50, 0},   // CB8
      {30, 64, 30, 64, 64},  // HH8: closed
      {90, 64, 40, 64, 64},  // CY8
      {75, 70, 40, 60, 40},  // BD9
      {55, 64, 80, 30, 64},  // SD9
      {70, 20, 10, 50, 50},  // TOM9
      {60, 70, 64, 50, 50},  // CP9
      {35, 64, 30, 20, 0},   // RS9
      {30, 70, 50, 64, 64},  // HH9: closed
      {95, 64, 50, 64, 64},  // CY9
  };
  constexpr int kLast = static_cast<int>(DrumMachine::Count) - 1;
  m.machine = static_cast<uint8_t>(machine > kLast ? kLast : machine);
  for (int k = 0; k < kFmMacros; ++k) m.macro[k] = kDefaults[m.machine][k];
}

void instrSetType(Instrument& m, InstrType t) {
  if (t >= InstrType::Count) t = InstrType::Chip;
  m.type = t;
  if (t == InstrType::Fm) fmSetMachine(m, m.machine);
  else if (t == InstrType::Drum) drumSetMachine(m, m.machine);
  const bool macroDest = m.lfoDest >= static_cast<uint8_t>(LfoDest::Dec) &&
                         m.lfoDest <= static_cast<uint8_t>(LfoDest::Con);
  if ((t == InstrType::Chip || t == InstrType::Sample) && macroDest)
    m.lfoDest = static_cast<uint8_t>(LfoDest::Pitch);
}
```

`#include <math.h>` в `model.cpp`, если ещё нет.

`src/storage/storage.cpp`: `sizeof(mt::Instrument)` станет 70 (62 + 7 байт + выравнивание), `sizeof(mt::Project)` — 103312 (103184 + 16 × 8). Поставить фактические значения из ошибки компилятора `pio run -e wt32`. `snapshot()` копирует `instruments` целиком через `memcpy` — правка не нужна.

**Step 4:** `pio test -e native -f test_model` — PASS.

---

### Task 2: fx FLT, RES

**Files:**
- Modify: `lib/core/src/model.h` (enum `Fx`)
- Modify: `lib/core/src/fx_info.cpp:37-43`
- Test: `test/test_fx_info/test_main.cpp:53-72`

**Step 1: тесты** — обновить существующие и добавить:

```cpp
// в test_fx_next (строки 53-56): последняя команда теперь RES
  TEST_ASSERT_TRUE(fxNextCmd(Fx::None, -1) == Fx::RES);
  TEST_ASSERT_TRUE(fxNextCmd(Fx::CON, 1) == Fx::FLT);
  TEST_ASSERT_TRUE(fxNextCmd(Fx::RES, 1) == Fx::None);

void test_filter_fx() {
  TEST_ASSERT_EQUAL(static_cast<int>(Fx::DCY) + kLockFlt, static_cast<int>(Fx::FLT));
  TEST_ASSERT_EQUAL(static_cast<int>(Fx::DCY) + kLockRes, static_cast<int>(Fx::RES));
  TEST_ASSERT_EQUAL_STRING("FLT", fxName(Fx::FLT));
  TEST_ASSERT_EQUAL_STRING("RES", fxName(Fx::RES));
  TEST_ASSERT_TRUE(fxSynthOnly(Fx::FLT));
  TEST_ASSERT_TRUE(fxSynthOnly(Fx::RES));
  TEST_ASSERT_EQUAL(127, fxInfoMax(Fx::FLT));  // имя функции — как в fx_info.h
}
```

Если в `fx_info.h` нет функции максимума, проверить через существующий API (`fxClamp` / формат) — как в соседних тестах DCY.

**Step 2:** `pio test -e native -f test_fx_info` — FAIL (нет `Fx::FLT`).

**Step 3: реализация**

`model.h`, enum `Fx`: `..., DCY, COL, SHP, SWP, CON, FLT, RES, Count`. Комментарий: `// FLT, RES lock the filter cutoff / resonance (Fx::DCY + kLockFlt / kLockRes), every INT instrument.`

`fx_info.cpp` после `CON`:

```cpp
    {"FLT", 0, 127, 64, false},  // FLT: filter cutoff lock
    {"RES", 0, 127, 0, false},   // RES: filter resonance lock
```

Добавить описания в справочный текст fx, если в `fx_info.cpp` есть таблица описаний (по образцу DEC).

**Step 4:** `pio test -e native -f test_fx_info` — PASS. Затем `pio test -e native` — всё PASS (GRID берёт список fx из `fxNextCmd`).

---

### Task 3: `inst_codec`, чанк FLTR, нормализация машины

**Files:**
- Create: `lib/core/src/inst_codec.h`
- Create: `lib/core/src/inst_codec.cpp`
- Modify: `lib/core/src/project_io.cpp` (вынести pack/unpack, чанк `FLTR`, `fixInstrument` после CRC)
- Test: `test/test_project_io/test_main.cpp`

**Step 1: тесты**

```cpp
void test_fltr_roundtrip() {
  Instrument& m = a.instruments[5];
  m.fltMode = static_cast<uint8_t>(FltMode::Bp);
  m.cutoff = 33;
  m.reso = 99;
  m.fenv = -40;
  m.fAtk = 7;
  m.fDec = 88;
  m.keytrack = 127;
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  const Instrument& r = b.instruments[5];
  TEST_ASSERT_EQUAL(static_cast<int>(FltMode::Bp), r.fltMode);
  TEST_ASSERT_EQUAL(33, r.cutoff);
  TEST_ASSERT_EQUAL(99, r.reso);
  TEST_ASSERT_EQUAL(-40, r.fenv);
  TEST_ASSERT_EQUAL(7, r.fAtk);
  TEST_ASSERT_EQUAL(88, r.fDec);
  TEST_ASSERT_EQUAL(127, r.keytrack);
}

void test_file_without_fltr() {
  // Сохранить, вырезать чанк FLTR из буфера (helper removeChunk, как в тесте "без FMIN"), пересчитать CRC.
  a.instruments[0].fltMode = static_cast<uint8_t>(FltMode::Lp);
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  std::vector<uint8_t> v = removeChunk(out.buf, "FLTR");
  VecSource in(v);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_EQUAL(static_cast<int>(FltMode::Off), b.instruments[0].fltMode);
  TEST_ASSERT_EQUAL(127, b.instruments[0].cutoff);
}

void test_drum_type_roundtrip() {
  Instrument& m = a.instruments[2];
  m.type = InstrType::Drum;
  drumSetMachine(m, static_cast<uint8_t>(DrumMachine::Cy9));
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_TRUE(b.instruments[2].type == InstrType::Drum);
  TEST_ASSERT_EQUAL(static_cast<int>(DrumMachine::Cy9), b.instruments[2].machine);
}

void test_fm_machine_out_of_range() {
  Instrument& m = a.instruments[3];
  m.type = InstrType::Fm;
  m.machine = 12;  // a DRUM number on an FM instrument
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_EQUAL(0, b.instruments[3].machine);
}
```

Если хелпера вырезания чанка нет — написать в тесте: пройти по чанкам после 8-байтного заголовка, пропустить нужный, в конце пересчитать `"CRC "` через `crc32` от всех байт до него.

**Step 2:** `pio test -e native -f test_project_io` — FAIL.

**Step 3: реализация**

`inst_codec.h`:

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "model.h"

namespace mt {

// Instrument records shared by .mtp (INST, FMIN, FLTR chunks) and .mti presets. Little-endian.
// Unpack clamps every value to its valid range.
constexpr size_t kInstRecSize = 48;  // 47 bytes of fields + 1 reserved
constexpr size_t kFmRecSize = 16;    // 10 bytes of fields + reserved
constexpr size_t kFltRecSize = 8;    // 7 bytes of fields + 1 reserved

void packInst(const Instrument& m, uint8_t* b);
void unpackInst(const uint8_t* b, Instrument& m);
void packFm(const Instrument& m, uint8_t* b);
void unpackFm(const uint8_t* b, Instrument& m);
void packFlt(const Instrument& m, uint8_t* b);
void unpackFlt(const uint8_t* b, Instrument& m);
// After all records of an instrument: a machine number the type does not have becomes 0.
void fixInstrument(Instrument& m);

}  // namespace mt
```

`inst_codec.cpp`: перенести `packInst`, `unpackInst`, `packFm`, `unpackFm` и хелперы `rd16`, `wr16`, `clampu`, `clamps` из `project_io.cpp` (в анонимном namespace `inst_codec.cpp`; в `project_io.cpp` оставить свои копии хелперов, которые там ещё нужны). Изменения:

```cpp
// unpackFm: machine of either type; fixInstrument() narrows it by type.
  m.machine = b[0] < kMachineMax ? b[0] : 0;

void packFlt(const Instrument& m, uint8_t* b) {
  memset(b, 0, kFltRecSize);
  b[0] = m.fltMode;
  b[1] = m.cutoff;
  b[2] = m.reso;
  b[3] = static_cast<uint8_t>(m.fenv);
  b[4] = m.fAtk;
  b[5] = m.fDec;
  b[6] = m.keytrack;
}

void unpackFlt(const uint8_t* b, Instrument& m) {
  m.fltMode = b[0] < static_cast<int>(FltMode::Count) ? b[0] : 0;
  m.cutoff = clampu(b[1], 0, 127);
  m.reso = clampu(b[2], 0, 127);
  m.fenv = clamps(static_cast<int8_t>(b[3]), -64, 63);
  m.fAtk = clampu(b[4], 0, 127);
  m.fDec = clampu(b[5], 0, 127);
  m.keytrack = clampu(b[6], 0, 127);
}

void fixInstrument(Instrument& m) {
  int n = 0;
  if (m.type == InstrType::Fm) n = static_cast<int>(FmMachine::Count);
  else if (m.type == InstrType::Drum) n = static_cast<int>(DrumMachine::Count);
  if (n && m.machine >= n) m.machine = 0;
}
```

`project_io.cpp`:
- `#include "inst_codec.h"`; `kInstSize` → `kInstRecSize`, `kFminSize` → `kFmRecSize`; удалить перенесённые функции.
- запись после `FMIN`:

```cpp
  if (!o.chunk("FLTR", 1 + kInstruments * kFltRecSize) || !o.write(&count, 1)) return false;
  for (const Instrument& m : p.instruments) {
    uint8_t b[kFltRecSize];
    packFlt(m, b);
    if (!o.write(b, sizeof(b))) return false;
  }
```

- чтение: `readFltr` по образцу `readFmin` (`readRecords(in, size, kFltRecSize, kInstruments, ...unpackFlt...)`), ветка `else if (memcmp(ch, "FLTR", 4) == 0) e = readFltr(in, size, out);`.
- в ветке `"CRC "`:

```cpp
      if (rd32(v) != in.crc()) return LoadErr::BadCrc;
      for (Instrument& m : out.instruments) fixInstrument(m);
      return LoadErr::Ok;
```

**Step 4:** `pio test -e native -f test_project_io` — PASS; `pio test -e native` — PASS.

---

### Task 4: фильтр в синте + локи FLT/RES

**Files:**
- Modify: `lib/core/src/synth_voice.h` (поля фильтра, `lock[kLocks]`)
- Modify: `lib/core/src/synth.h` (`TrackRt::lock[kLocks]`, `controlFilter`)
- Modify: `lib/core/src/synth.cpp` (`fx`, `noteOn`, `control`, `renderVoice`)
- Test: `test/test_synth/test_main.cpp`

**Step 1: тесты** (хелперы `noteOn`, `send`, `fx` уже есть в файле)

```cpp
// RMS over n blocks.
static float rmsBlocks(int n) {
  double acc = 0;
  for (int k = 0; k < n; ++k) {
    s->render(buf);
    for (int i = 0; i < Synth::kBlock; ++i) acc += double(buf[i]) * buf[i];
  }
  return sqrtf(static_cast<float>(acc / (n * Synth::kBlock)));
}

void test_filter_off_is_bit_identical() {
  noteOn(0, 0, 72);
  int16_t ref[Synth::kBlock * 4];
  for (int k = 0; k < 4; ++k) s->render(ref + k * Synth::kBlock);
  s->reset();
  p->instruments[0].fltMode = static_cast<uint8_t>(FltMode::Off);
  p->instruments[0].cutoff = 10;  // ignored while Off
  noteOn(0, 0, 72);
  for (int k = 0; k < 4; ++k) {
    s->render(buf);
    TEST_ASSERT_EQUAL_INT16_ARRAY(ref + k * Synth::kBlock, buf, Synth::kBlock);
  }
}

void test_lp_darkens_saw() {
  noteOn(0, 0, 84);
  s->render(buf);
  const float open = rmsBlocks(8);
  s->reset();
  p->instruments[0].fltMode = static_cast<uint8_t>(FltMode::Lp);
  p->instruments[0].cutoff = 20;  // ~56 Hz, far below C6
  noteOn(0, 0, 84);
  s->render(buf);
  TEST_ASSERT_TRUE(rmsBlocks(8) < open * 0.2f);
}

void test_filter_env_opens() {
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  m.cutoff = 20;
  m.fenv = 63;
  m.fAtk = 0;
  m.fDec = 70;  // ~ hundreds of ms
  noteOn(0, 0, 84);
  const float early = rmsBlocks(4);
  rmsBlocks(200);  // ~0.8 s
  const float late = rmsBlocks(4);
  TEST_ASSERT_TRUE(early > late * 3.f);
}

void test_max_reso_stays_finite() {
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Bp);
  m.reso = 127;
  m.cutoff = 127;
  m.fenv = 63;
  noteOn(0, 0, 100);
  for (int k = 0; k < 50; ++k) s->render(buf);  // softClip keeps int16 in range; must not hang / NaN
  TEST_ASSERT_TRUE(s->activeVoices() == 1);
}

void test_flt_lock_on_note_step() {
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  m.cutoff = 127;
  step(0, 0, true);               // helper: 0xF5 kSynthStep with the note bit
  fx(0, 0, Fx::FLT, 15);
  noteOn(0, 0, 84);
  s->render(buf);
  const float locked = rmsBlocks(8);
  s->reset();
  step(0, 0, true);
  noteOn(0, 0, 84);               // a new step drops the lock
  s->render(buf);
  TEST_ASSERT_TRUE(locked < rmsBlocks(8) * 0.3f);
}

void test_flt_lock_without_note_hits_sounding_voice() {
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  noteOn(0, 0, 84);
  s->render(buf);
  const float open = rmsBlocks(4);
  step(0, 0, false);
  fx(0, 0, Fx::FLT, 15);
  s->render(buf);
  TEST_ASSERT_TRUE(rmsBlocks(4) < open * 0.3f);
}
```

`step(off, track, hasNote)` — хелпер, если его нет: `send(off, track, 0xF5, kSynthStep, 24 | (hasNote ? 0x80 : 0))`. `fx(off, track, Fx, val)` — существующий хелпер (строка 661).

**Step 2:** `pio test -e native -f test_synth` — FAIL.

**Step 3: реализация**

`synth_voice.h`, в `Voice`:

```cpp
  uint8_t lockMask = 0;              // locked for this note: bit = LockBit (macros: FM / DRUM only)
  uint8_t lock[kLocks] = {0};
  ...
  // Filter, every type (Synth::controlFilter).
  bool fltOn = false;
  uint32_t fenvT = 0;                // samples since the filter envelope's trigger
  Svf flt;
```

`#include "synth_filter.h"` (уже через `synth_fm.h`, добавить явно). `fpMac[kFmMacros]` не трогать.

`synth.h`: `TrackRt::lock[kLocks]`, комментарий у `lockMask`: `// LockBit: this step's note-ons`. Объявить `void controlFilter(Voice& v, const Instrument& m, float pitch, float lfoCut);`.

`synth.cpp`, `fx()` — объединить ветку макро-локов с фильтром:

```cpp
    case Fx::DCY:
    case Fx::COL:
    case Fx::SHP:
    case Fx::SWP:
    case Fx::CON:
    case Fx::FLT:
    case Fx::RES: {
      // Lock: this step's note-ons, or the track's sounding voices. Macros: FM / DRUM voices only.
      const int k = cmd - static_cast<uint8_t>(Fx::DCY);
      const bool macro = k < kFmMacros;
      const uint8_t lv = val > 127 ? 127 : val;
      if (now) {
        for (auto& x : voices_)
          if (x.on && x.track == track && (!macro || x.fm || x.drum)) {
            x.lock[k] = lv;
            x.lockMask |= 1 << k;
          }
      } else {
        r.lock[k] = lv;
        r.lockMask |= 1 << k;
      }
      break;
    }
```

`x.drum` появится в Task 8 — до тех пор написать `(!macro || x.fm)` и дополнить в Task 8.

`noteOn`:

```cpp
  // Filter locks go to every type, macro locks to FM (DRUM: Task 8).
  constexpr uint8_t kMacroBits = (1 << kFmMacros) - 1;
  v.lockMask = fm ? r.lockMask : (r.lockMask & ~kMacroBits);
  for (int k = 0; k < kLocks; ++k) v.lock[k] = r.lock[k];
```

(заменяет `v.lockMask = fm ? r.lockMask : 0;` и копирование `lock` внутри `if (fm)`). Перед `control(v, 0);`:

```cpp
  if (!overlap) v.fenvT = 0;  // legato keeps the filter envelope running (303 style)
  if (!legato) v.flt.reset();
```

`control()`: после блока VIB, перед `if (v.fm)`:

```cpp
  controlFilter(v, m, pitch, 0);  // LFO CUTOFF: Task 5
```

Новая функция:

```cpp
void Synth::controlFilter(Voice& v, const Instrument& m, float pitch, float lfoCut) {
  const uint8_t mode = m.fltMode < static_cast<uint8_t>(FltMode::Count) ? m.fltMode : 0;
  v.fltOn = mode != static_cast<uint8_t>(FltMode::Off);
  if (!v.fltOn) return;
  const float cut = (v.lockMask & (1 << kLockFlt)) ? v.lock[kLockFlt] : (m.cutoff > 127 ? 127 : m.cutoff);
  const float res = (v.lockMask & (1 << kLockRes)) ? v.lock[kLockRes] : (m.reso > 127 ? 127 : m.reso);
  // Octaves above 20 Hz: cutoff, envelope, key tracking (from C4).
  float oct = clampf(cut + lfoCut, 0.f, 127.f) * (9.451211f / 127.f);  // log2(700)
  oct += clampf(m.fenv, -64, 63) * (6.f / 64.f) * filterEnv(v.fenvT, m.fAtk, m.fDec);
  oct += (m.keytrack > 127 ? 127 : m.keytrack) * (1.f / 127.f) * (pitch - 60.f) * (1.f / 12.f);
  v.flt.set(static_cast<Svf::Mode>(mode - 1), 20.f * exp2f(oct), resoQ(res));
}
```

Счётчик огибающей — в начале `control()`: `v.fenvT += static_cast<uint32_t>(dt);` (до вызова `controlFilter`; на note-on `dt = 0`).

`renderVoice` — общий временный буфер и фильтр:

```cpp
void Synth::renderVoice(Voice& v, float* out, int n) {
  // Segments end at control boundaries: n <= kControl.
  float tmp[kControl] = {0};
  if (v.fm) {
    v.fmv.render(tmp, n, v.amp);
    for (int i = 0; i < n; ++i) tmp[i] *= v.env.next();
    if (v.fmv.done()) v.env.kill();
  } else if (v.sample) {
    renderSample(v, tmp, n);
  } else {
    const Wave w = static_cast<Wave>(v.wave);
    const float inc = v.inc, duty = v.duty, amp = v.amp;
    for (int i = 0; i < n; ++i) tmp[i] = v.osc.next(w, inc, duty) * v.env.next() * amp;
  }
  if (v.fltOn)
    for (int i = 0; i < n; ++i) tmp[i] = v.flt.process(tmp[i]);
  for (int i = 0; i < n; ++i) out[i] += tmp[i];
}
```

`renderSample` пишет `+=` в чистый `tmp` — поведение то же. Порядок операций для CHIP прежний (`osc × env × amp`), поэтому OFF совпадает бит-в-бит.

**Step 4:** `pio test -e native -f test_synth` — PASS; `pio test -e native` — PASS.

---

### Task 5: LFO для всех типов, цель CUTOFF

**Files:**
- Modify: `lib/core/src/synth.h` (`controlFm` — новые параметры)
- Modify: `lib/core/src/synth.cpp` (`noteOn`, `control`, `controlFm`)
- Test: `test/test_synth/test_main.cpp`

**Step 1: тесты**

```cpp
void test_lfo_pitch_on_chip() {
  Instrument& m = p->instruments[0];
  m.wave = static_cast<uint8_t>(Wave::Triangle);
  noteOn(0, 0, 69);
  const int still = crossings(40);
  s->reset();
  m.lfoDest = static_cast<uint8_t>(LfoDest::Pitch);
  m.lfoWave = static_cast<uint8_t>(LfoWave::Square);
  m.lfoRate = 0;     // 0.05 Hz: the first half cycle stays at +depth
  m.lfoDepth = 63;   // ~ +12 semitones
  noteOn(0, 0, 69);
  TEST_ASSERT_INT_WITHIN(still / 10, still * 2, crossings(40));
}

void test_lfo_cutoff_on_chip() {
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  m.cutoff = 127;
  m.lfoDest = static_cast<uint8_t>(LfoDest::Cutoff);
  m.lfoWave = static_cast<uint8_t>(LfoWave::Square);
  m.lfoRate = 0;
  m.lfoDepth = -64;  // square starts at +1: -64 units of cutoff
  noteOn(0, 0, 84);
  s->render(buf);
  const float swept = rmsBlocks(8);
  s->reset();
  m.lfoDepth = 0;
  noteOn(0, 0, 84);
  s->render(buf);
  TEST_ASSERT_TRUE(swept < rmsBlocks(8) * 0.7f);
}

void test_lfo_macro_dest_ignored_on_chip() {
  Instrument& m = p->instruments[0];
  noteOn(0, 0, 69);
  const float ref = rmsBlocks(8);
  s->reset();
  m.lfoDest = static_cast<uint8_t>(LfoDest::Col);
  m.lfoDepth = 63;
  noteOn(0, 0, 69);
  TEST_ASSERT_FLOAT_WITHIN(ref * 0.01f, ref, rmsBlocks(8));
}
```

Существующие LFO-тесты FM (строки ~677–840) должны пройти без изменений.

**Step 2:** `pio test -e native -f test_synth` — FAIL.

**Step 3: реализация**

`noteOn`: LFO не-FM голосов сбрасывается на новой ноте без легато (у FM — как было, в ветке `if (fm)`):

```cpp
  if (!fm && !legato) {
    v.lfoPhase = 0;
    v.lfoRnd = rnd();
  }
```

`control()`: после VIB, вместо вызова из Task 4:

```cpp
  // LFO: PITCH, VOL, CUTOFF on every type; macro targets go to controlFm (FM / DRUM).
  const float l = lfo(v, m, dt);
  float lfoVol = 1, lfoCut = 0;
  if (l != 0) {
    const uint8_t dest = m.lfoDest < static_cast<uint8_t>(LfoDest::Count) ? m.lfoDest : 0;
    if (dest == static_cast<uint8_t>(LfoDest::Pitch)) pitch += 12.f * l;
    else if (dest == static_cast<uint8_t>(LfoDest::Vol)) lfoVol = clampf(1.f + l, 0.f, 2.f);
    else if (dest == static_cast<uint8_t>(LfoDest::Cutoff)) lfoCut = 64.f * l;
  }
  controlFilter(v, m, pitch, lfoCut);
  if (v.fm) {
    controlFm(v, m, pitch, dt, l, lfoVol);  // sets v.amp too
    return;
  }
```

и в конце CHIP / SAMPLE: `v.amp = v.gain * iv * trackVol(v.track) * lfoVol * (1.f / (127.f * 127.f));`.

`controlFm(Voice& v, const Instrument& m, float pitch, int dt, float l, float vol)`: удалить свой вызов `lfo()` и разбор PITCH / VOL; оставить только макро-цели:

```cpp
  float mac[kFmMacros];
  for (int k = 0; k < kFmMacros; ++k) mac[k] = (v.lockMask & (1 << k)) ? v.lock[k] : m.macro[k];
  const uint8_t dest = m.lfoDest < static_cast<uint8_t>(LfoDest::Count) ? m.lfoDest : 0;
  if (l != 0 && dest >= static_cast<uint8_t>(LfoDest::Dec) && dest <= static_cast<uint8_t>(LfoDest::Con))
    mac[dest - 1] = clampf(mac[dest - 1] + 64.f * l, 0.f, 127.f);
```

`dt` в `controlFm` остаётся для ramp. `lfo()` двигает фазу один раз за control — как раньше.

**Step 4:** `pio test -e native -f test_synth` — PASS (включая старые LFO-тесты FM); `pio test -e native` — PASS.

---

### Task 6: DRUM-ядро `synth_drum`

**Files:**
- Create: `lib/core/src/synth_drum.h`
- Create: `lib/core/src/synth_drum.cpp`
- Test: `test/test_synth_drum/test_main.cpp`

**Step 1: тесты**

```cpp
#include <math.h>
#include <unity.h>
#include "synth_drum.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static float buf[kSynthRate];  // 1 s

static void run(DrumVoice& d, const DrumParams& p, int n) {
  for (int i = 0; i < n; ++i) buf[i] = 0;
  for (int pos = 0; pos < n; pos += 32) {
    d.control(p, 32);
    d.render(buf + pos, 32, 1.f);
  }
}

void test_tone_frequency() {
  DrumParams p;
  p.toneHz[0] = 200;
  p.toneLvl[0] = 1;
  p.toneMs = 0;  // no decay
  DrumVoice d;
  d.trigger(false);
  run(d, p, kSynthRate / 2);
  int c = 0;
  for (int i = 1; i < kSynthRate / 2; ++i) c += buf[i - 1] > 0 && buf[i] <= 0;
  TEST_ASSERT_INT_WITHIN(2, 100, c);
}

void test_decay_and_done() {
  DrumParams p;
  p.toneHz[0] = 100;
  p.toneLvl[0] = 1;
  p.toneMs = 100;
  DrumVoice d;
  d.trigger(false);
  run(d, p, kSynthRate / 4);  // 250 ms > 100 ms to -60 dB
  float peak = 0;
  for (int i = kSynthRate / 8; i < kSynthRate / 4; ++i) peak = fmaxf(peak, fabsf(buf[i]));
  TEST_ASSERT_TRUE(peak < 0.002f);
  run(d, p, kSynthRate / 4);
  TEST_ASSERT_TRUE(d.done());
}

void test_metal_bounded_finite() {
  DrumParams p;
  const float hz[kDrumMetal] = {205.3f, 304.4f, 369.6f, 522.7f, 540.f, 800.f};
  for (int k = 0; k < kDrumMetal; ++k) p.metalHz[k] = hz[k];
  p.metalLvl = 1;
  p.metalMs = 500;
  p.metalBp1 = 3440;
  p.metalBp2 = 7100;
  p.metalHp = 7000;
  p.noiseLvl = 1;
  p.noiseMs = 300;
  p.noiseHz = 8000;
  p.drive = 1;
  DrumVoice d;
  d.trigger(false);
  run(d, p, kSynthRate / 2);
  for (int i = 0; i < kSynthRate / 2; ++i) {
    TEST_ASSERT_TRUE(isfinite(buf[i]));
    TEST_ASSERT_TRUE(fabsf(buf[i]) <= 1.01f);
  }
}

void test_clap_bursts() {
  DrumParams p;
  p.noiseLvl = 1;
  p.noiseMs = 200;
  p.bursts = 3;
  p.burstMs = 10;
  p.tail = 1;
  DrumVoice d;
  d.trigger(false);
  run(d, p, kSynthRate / 10);
  // Envelope peaks near 0, 10, 20 ms: energy right after each restart beats the one before it.
  auto e = [](int ms) {
    float a = 0;
    for (int i = ms * 32; i < ms * 32 + 32; ++i) a += buf[i] * buf[i];
    return a;
  };
  TEST_ASSERT_TRUE(e(10) > e(9) * 2);
  TEST_ASSERT_TRUE(e(20) > e(19) * 2);
}

void test_choke_tail_no_jump() {
  DrumParams p;
  p.toneHz[0] = 50;
  p.toneLvl[0] = 1;
  p.toneMs = 2000;
  DrumVoice d;
  d.trigger(false);
  run(d, p, 1000);
  const float last = buf[999];
  d.trigger(true);  // choke: the old output fades over ~2 ms
  float first[32] = {0};
  d.control(p, 32);
  d.render(first, 32, 1.f);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, last, first[0]);
}
```

**Step 2:** `pio test -e native -f test_synth_drum` — FAIL (нет файла).

**Step 3: реализация**

`synth_drum.h`:

```cpp
#pragma once
#include <stdint.h>
#include "hot.h"
#include "synth_filter.h"

namespace mt {

constexpr int kDrumTones = 2;
constexpr int kDrumMetal = 6;

// One DRUM voice's sound, made by a machine (synth_drum_machines) at control rate. Times are to
// -60 dB, ms; 0 = no decay. Every part has its own exponential envelope from the trigger.
struct DrumParams {
  float toneHz[kDrumTones] = {0, 0};  // sines, 0 = off
  float toneLvl[kDrumTones] = {0, 0};
  float toneMs = 0;
  float pitchEnv = 0, pitchMs = 0;    // semitones at the trigger, decaying to 0 (tones only)
  float click = 0, clickMs = 2;       // noise burst + impulse at the trigger
  float metalHz[kDrumMetal] = {0, 0, 0, 0, 0, 0};  // squares, 0 = off
  float metalW[kDrumMetal] = {1, 1, 1, 1, 1, 1};   // per-square weight
  float metalLvl = 0, metalMs = 0;
  float metalAccent = 0;              // extra level decaying in kAccentMs (cowbell attack)
  float metalBp1 = 0, metalBp2 = 0;   // band-pass centres (summed), 0 = skip both
  float metalQ = 1.5f;
  float metalHp = 0;                  // high-pass after the band-passes, 0 = skip
  float noiseLvl = 0, noiseMs = 0;
  Svf::Mode noiseMode = Svf::Mode::Hp;
  float noiseHz = 0, noiseQ = 0.707f; // 0 Hz = unfiltered
  uint8_t bursts = 0;                 // noise restarts before its decay (clap)
  float burstMs = 0;
  float tail = 1;                     // noise level after the bursts
  float drive = 0;                    // 0..1, tanh saturation of the sum
};
constexpr float kAccentMs = 15;

// Runtime of one DRUM voice. control() takes the params at the current time and ramps the tone
// pitch linearly to its value span samples later; render() adds amp x sound to out.
class DrumVoice {
 public:
  // keepTail: choke — the voice's last output fades out over ~2 ms under the new hit.
  void trigger(bool keepTail);
  void control(const DrumParams& p, int span);
  MT_HOT void render(float* out, int n, float amp);
  // Every part below -80 dB and no bursts left.
  bool done() const;

 private:
  float tonePh_[kDrumTones] = {0, 0};
  float toneInc_[kDrumTones] = {0, 0}, toneIncStep_[kDrumTones] = {0, 0};
  float toneLvl_[kDrumTones] = {0, 0};
  float toneE_ = 0, toneK_ = 1;
  float pitchE_ = 0, pitchK_ = 1;     // pitch envelope, 0..1
  float clickE_ = 0, clickK_ = 1, click_ = 0;
  bool impulse_ = false;
  uint32_t metalPh_[kDrumMetal] = {0, 0, 0, 0, 0, 0};
  uint32_t metalInc_[kDrumMetal] = {0, 0, 0, 0, 0, 0};
  float metalW_[kDrumMetal] = {0, 0, 0, 0, 0, 0};
  float metalE_ = 0, metalK_ = 1, accE_ = 0, accK_ = 1, metalLvl_ = 0, metalAcc_ = 0;
  bool metalOn_ = false, bpOn_ = false, hpOn_ = false;
  Svf bp1_, bp2_, hp_;
  float noiseE_ = 0, noiseK_ = 1, noiseLvl_ = 0, tail_ = 1;
  bool noiseFlt_ = false;
  Svf nf_;
  uint32_t rng_ = 0x9E3779B9u;
  uint8_t burstsLeft_ = 0;
  int32_t burstT_ = 0, burstLen_ = 0;  // samples
  float drive_ = 0;
  float last_ = 0, tailV_ = 0;         // choke crossfade
  bool started_ = false;               // first control() after trigger sets the envelopes
};

}  // namespace mt
```

`synth_drum.cpp`:

```cpp
#include "synth_drum.h"
#include <math.h>
#include "synth_osc.h"

namespace mt {
namespace {

// Per-sample multiplier reaching -60 dB in ms; 0 ms = no decay.
float decayK(float ms) { return ms > 0 ? expf(-6.9077553f / (ms * kSynthRate * 0.001f)) : 1.f; }
constexpr float kChokeK = 0.98450f;  // ~2 ms to -60 dB at 32 kHz... (exp(-6.9/64))
constexpr float kOff = 1e-4f;         // -80 dB

MT_INLINE float fastTanh(float x) {
  if (x > 3.f) return 1.f;
  if (x < -3.f) return -1.f;
  const float x2 = x * x;
  return x * (27.f + x2) / (27.f + 9.f * x2);
}

}  // namespace

void DrumVoice::trigger(bool keepTail) {
  tailV_ = keepTail ? last_ : 0;
  for (auto& ph : tonePh_) ph = 0;
  toneE_ = pitchE_ = clickE_ = metalE_ = accE_ = noiseE_ = 1;
  impulse_ = true;
  burstsLeft_ = 0;
  burstT_ = 0;
  started_ = false;
  bp1_.reset();
  bp2_.reset();
  hp_.reset();
  nf_.reset();
}

void DrumVoice::control(const DrumParams& p, int span) {
  if (span < 1) span = 1;
  toneK_ = decayK(p.toneMs);
  pitchK_ = decayK(p.pitchMs);
  clickK_ = decayK(p.clickMs);
  click_ = p.click;
  // Tone pitch: now and span samples later (the pitch envelope decays in between).
  const float pe1 = pitchE_ * powf(pitchK_, static_cast<float>(span));
  for (int k = 0; k < kDrumTones; ++k) {
    toneLvl_[k] = p.toneHz[k] > 0 ? p.toneLvl[k] : 0;
    const float hz0 = p.toneHz[k] * exp2f(p.pitchEnv * pitchE_ * (1.f / 12.f));
    const float hz1 = p.toneHz[k] * exp2f(p.pitchEnv * pe1 * (1.f / 12.f));
    const float lim = kSynthRate * 0.45f;
    const float i0 = (hz0 < lim ? hz0 : lim) / kSynthRate, i1 = (hz1 < lim ? hz1 : lim) / kSynthRate;
    toneInc_[k] = i0;
    toneIncStep_[k] = (i1 - i0) / span;
  }
  metalOn_ = false;
  for (int k = 0; k < kDrumMetal; ++k) {
    const float hz = p.metalHz[k] < kSynthRate * 0.45f ? p.metalHz[k] : kSynthRate * 0.45f;
    metalInc_[k] = static_cast<uint32_t>(hz / kSynthRate * 4294967296.f);
    metalW_[k] = hz > 0 ? p.metalW[k] : 0;
    metalOn_ |= hz > 0 && p.metalLvl > 0;
  }
  metalLvl_ = p.metalLvl;
  metalAcc_ = p.metalAccent;
  metalK_ = decayK(p.metalMs);
  accK_ = decayK(kAccentMs);
  bpOn_ = p.metalBp1 > 0;
  if (bpOn_) {
    bp1_.set(Svf::Mode::Bp, p.metalBp1, p.metalQ);
    bp2_.set(Svf::Mode::Bp, p.metalBp2 > 0 ? p.metalBp2 : p.metalBp1, p.metalQ);
  }
  hpOn_ = p.metalHp > 0;
  if (hpOn_) hp_.set(Svf::Mode::Hp, p.metalHp, 0.707f);
  noiseLvl_ = p.noiseLvl;
  noiseK_ = decayK(p.bursts && burstsLeft_ ? p.burstMs : p.noiseMs);
  tail_ = p.tail;
  noiseFlt_ = p.noiseHz > 0;
  if (noiseFlt_) nf_.set(p.noiseMode, p.noiseHz, p.noiseQ);
  drive_ = p.drive < 0 ? 0 : (p.drive > 1 ? 1 : p.drive);
  if (!started_) {
    started_ = true;
    burstsLeft_ = p.bursts;
    burstLen_ = static_cast<int32_t>(p.burstMs * kSynthRate * 0.001f);
    burstT_ = burstLen_;
    if (burstsLeft_) noiseK_ = decayK(p.burstMs);
  }
}

void DrumVoice::render(float* out, int n, float amp) {
  for (int i = 0; i < n; ++i) {
    float s = 0;
    // Tones.
    for (int k = 0; k < kDrumTones; ++k) {
      if (toneLvl_[k] == 0) continue;
      s += toneLvl_[k] * sinf(6.2831853f * tonePh_[k]);  // TODO(perf): table sine, как FM
      tonePh_[k] += toneInc_[k];
      tonePh_[k] -= static_cast<int>(tonePh_[k]);
      toneInc_[k] += toneIncStep_[k];
    }
    s *= toneE_;
    toneE_ *= toneK_;
    pitchE_ *= pitchK_;
    // Noise source, shared by click and noise.
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    const float wn = static_cast<int32_t>(rng_) * (1.f / 2147483648.f);
    if (click_ > 0) {
      s += click_ * clickE_ * (impulse_ ? 1.f : wn);
      clickE_ *= clickK_;
    }
    impulse_ = false;
    // Metal: squares -> band-passes -> high-pass.
    if (metalOn_) {
      float m = 0;
      for (int k = 0; k < kDrumMetal; ++k) {
        m += (metalPh_[k] & 0x80000000u) ? metalW_[k] : -metalW_[k];
        metalPh_[k] += metalInc_[k];
      }
      m *= 1.f / kDrumMetal;
      if (bpOn_) m = bp1_.process(m) + bp2_.process(m);
      if (hpOn_) m = hp_.process(m);
      s += m * (metalLvl_ * metalE_ + metalAcc_ * accE_);
      metalE_ *= metalK_;
      accE_ *= accK_;
    }
    // Noise with bursts.
    if (noiseLvl_ > 0) {
      float nz = noiseFlt_ ? nf_.process(wn) : wn;
      s += nz * noiseLvl_ * noiseE_;
      noiseE_ *= noiseK_;
      if (burstsLeft_ && --burstT_ <= 0) {
        --burstsLeft_;
        burstT_ = burstLen_;
        noiseE_ = burstsLeft_ ? 1.f : tail_;
        if (!burstsLeft_) noiseK_ = 1.f;  // control() sets the tail decay from the next update
      }
    }
    if (drive_ > 0) s = fastTanh(s * (1.f + 7.f * drive_));
    s = s > 1.f ? 1.f : (s < -1.f ? -1.f : s);
    last_ = s;
    out[i] += s * amp + tailV_;
    tailV_ *= kChokeK;
  }
}

bool DrumVoice::done() const {
  const float t = toneE_ * (toneLvl_[0] + toneLvl_[1]);
  const float m = metalOn_ ? metalE_ * metalLvl_ + accE_ * metalAcc_ : 0;
  const float nz = noiseE_ * noiseLvl_;
  const float c = clickE_ * click_;
  return started_ && !burstsLeft_ && t < kOff && m < kOff && nz < kOff && c < kOff &&
         fabsf(tailV_) < kOff;
}

}  // namespace mt
```

Замечания для исполнителя:
- `kChokeK` посчитать точно: `expf(-6.9077553f / (2 ms × 32))` ≈ 0,8977. Число в черновике выше — заглушка, взять формулу.
- `tailV_` складывается без `amp`: это уже отданный сэмпл голоса. Поэтому `last_` хранить как `s * amp + tailV_` (выход голоса), не как `s`. Поправить при реализации, тест `test_choke_tail_no_jump` проверяет именно это.
- `sinf` на сэмпл — допустимо для 2 тонов (~40 тактов на S3 с FPU), но лучше взять таблицу синуса из `synth_fm.cpp` (вынести в общий `inline` в `synth_osc.h`). Решить по бенчу Task 16.
- Ядро — в IRAM (`MT_HOT`), как `FmVoice::render`.

**Step 4:** `pio test -e native -f test_synth_drum` — PASS.

---

### Task 7: DRUM-машины `synth_drum_machines`

**Files:**
- Create: `lib/core/src/synth_drum_machines.h`
- Create: `lib/core/src/synth_drum_machines.cpp`
- Test: `test/test_synth_drum_machines/test_main.cpp`

**Step 1: тесты**

```cpp
#include <math.h>
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
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 55.f, p.toneHz[0]);
  drumMachine(static_cast<uint8_t>(DrumMachine::Bd8), mac, 72, p);
  TEST_ASSERT_FLOAT_WITHIN(1.f, 110.f, p.toneHz[0]);
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
```

**Step 2:** `pio test -e native -f test_synth_drum_machines` — FAIL.

**Step 3: реализация**

`synth_drum_machines.h`:

```cpp
#pragma once
#include <stdint.h>
#include "model.h"
#include "synth_drum.h"

namespace mt {

// Sound of a DRUM machine (DrumMachine, out of range = BD8). mac: DECAY..CONTOUR 0..127, fractional
// after the LFO. pitch: MIDI note incl. transpose, fine, bend, ARP, VIB; C4 plays the machine's base
// pitch. Pure: called at control rate.
void drumMachine(uint8_t machine, const float mac[kFmMacros], float pitch, DrumParams& out);
const char* drumMachineName(uint8_t machine);  // "BD8"
// Macro label of the machine, e.g. "SNAPPY"; "" = the slot does nothing on it.
const char* drumMacroName(uint8_t machine, int macro);

}  // namespace mt
```

`synth_drum_machines.cpp` (значения — из таблицы дизайна; тембры доводятся на железе):

```cpp
#include "synth_drum_machines.h"
#include <math.h>

namespace mt {
namespace {

float u(float mac) { return mac <= 0 ? 0 : (mac >= 127 ? 1 : mac / 127.f); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
float expMap(float lo, float hi, float t) { return lo * powf(hi / lo, t); }

constexpr float kMetal808[kDrumMetal] = {205.3f, 304.4f, 369.6f, 522.7f, 540.f, 800.f};
constexpr float kMetal909[kDrumMetal] = {526.f, 800.f, 842.f, 948.f, 1174.f, 1690.f};

struct Names { const char* machine; const char* mac[kFmMacros]; };
constexpr Names kNames[static_cast<int>(DrumMachine::Count)] = {
    {"BD8", {"DECAY", "TONE", "DRIVE", "SWEEP", "TIME"}},
    {"SD8", {"DECAY", "TONE", "SNAPPY", "SWEEP", "N.DEC"}},
    {"TOM8", {"DECAY", "NOISE", "DRIVE", "SWEEP", "TIME"}},
    {"CP8", {"DECAY", "FREQ", "SPREAD", "Q", "TAIL"}},
    {"RS8", {"DECAY", "TONE", "NOISE", "SWEEP", ""}},
    {"CL8", {"DECAY", "TONE", "DRIVE", "SWEEP", ""}},
    {"CB8", {"DECAY", "FREQ", "BAL", "ACCENT", ""}},
    {"HH8", {"DECAY", "HP", "NOISE", "SPREAD", "N.DEC"}},
    {"CY8", {"DECAY", "HP", "NOISE", "SPREAD", "N.DEC"}},
    {"BD9", {"DECAY", "ATTACK", "DRIVE", "SWEEP", "TIME"}},
    {"SD9", {"DECAY", "TONE", "SNAPPY", "SWEEP", "N.DEC"}},
    {"TOM9", {"DECAY", "NOISE", "DRIVE", "SWEEP", "TIME"}},
    {"CP9", {"DECAY", "FREQ", "SPREAD", "Q", "TAIL"}},
    {"RS9", {"DECAY", "TONE", "NOISE", "SWEEP", ""}},
    {"HH9", {"DECAY", "HP", "NOISE", "SPREAD", "N.DEC"}},
    {"CY9", {"DECAY", "HP", "RIDE/CR", "SPREAD", "N.DEC"}},
};

void metal(DrumParams& o, const float* set, float scale, float spread, float r) {
  // spread stretches the set around its centre (x0.8..1.25).
  for (int k = 0; k < kDrumMetal; ++k) o.metalHz[k] = set[k] * scale * r * powf(spread, (k - 2.5f) / 2.5f);
}

void snare(DrumParams& o, const float mac[], float r, float lo, float hi, float noiseHz) {
  const float dec = fmDecayMs(static_cast<uint8_t>(mac[kMacDec]));
  const float c = u(mac[kMacCol]);
  o.toneHz[0] = lo * r;
  o.toneHz[1] = hi * r;
  o.toneLvl[0] = 0.6f * (0.9f * (1 - c) + 0.1f);
  o.toneLvl[1] = 0.6f * (0.9f * c + 0.1f);
  o.toneMs = dec;
  o.pitchEnv = 7.f * u(mac[kMacSwp]);
  o.pitchMs = 15;
  o.noiseLvl = u(mac[kMacShp]);
  o.noiseMs = dec * expMap(0.3f, 2.f, u(mac[kMacCon]));
  o.noiseMode = Svf::Mode::Hp;
  o.noiseHz = noiseHz;
}

void clap(DrumParams& o, const float mac[], float lo, float hi, uint8_t bursts) {
  o.noiseLvl = 1;
  o.noiseMs = fmDecayMs(static_cast<uint8_t>(mac[kMacDec]));
  o.noiseMode = Svf::Mode::Bp;
  o.noiseHz = expMap(lo, hi, u(mac[kMacCol]));
  o.bursts = bursts;
  o.burstMs = lerp(6, 14, u(mac[kMacShp]));
  o.noiseQ = expMap(1, 6, u(mac[kMacSwp]));
  o.tail = u(mac[kMacCon]);
}

void hat(DrumParams& o, const float mac[], const float* set, float scale, float decLo, float decHi,
         float hpLo, float hpHi, float bp1, float bp2, float r) {
  metal(o, set, scale, expMap(0.8f, 1.25f, u(mac[kMacSwp])), r);
  const float n = u(mac[kMacShp]);
  o.metalLvl = 1.f - 0.7f * n;
  o.metalMs = expMap(decLo, decHi, u(mac[kMacDec]));
  o.metalBp1 = bp1;
  o.metalBp2 = bp2;
  o.metalHp = expMap(hpLo, hpHi, u(mac[kMacCol]));
  o.noiseLvl = n;
  o.noiseMs = o.metalMs * expMap(0.3f, 1.5f, u(mac[kMacCon]));
  o.noiseMode = Svf::Mode::Hp;
  o.noiseHz = o.metalHp;
}

}  // namespace

const char* drumMachineName(uint8_t m) {
  return kNames[m < static_cast<int>(DrumMachine::Count) ? m : 0].machine;
}

const char* drumMacroName(uint8_t m, int k) {
  if (k < 0 || k >= kFmMacros) return "";
  return kNames[m < static_cast<int>(DrumMachine::Count) ? m : 0].mac[k];
}

void drumMachine(uint8_t machine, const float mac[kFmMacros], float pitch, DrumParams& o) {
  o = DrumParams();
  const float r = exp2f((pitch - 60.f) * (1.f / 12.f));
  const float dec = fmDecayMs(static_cast<uint8_t>(mac[kMacDec] < 0 ? 0 : (mac[kMacDec] > 127 ? 127 : mac[kMacDec])));
  const float col = u(mac[kMacCol]), shp = u(mac[kMacShp]), swp = u(mac[kMacSwp]), con = u(mac[kMacCon]);
  const float d01 = u(mac[kMacDec]);
  switch (static_cast<DrumMachine>(machine < static_cast<int>(DrumMachine::Count) ? machine : 0)) {
    case DrumMachine::Bd8:
      o.toneHz[0] = 55.f * r; o.toneLvl[0] = 1; o.toneMs = dec;
      o.click = 0.3f * col; o.clickMs = 1.5f;
      o.drive = shp; o.pitchEnv = 12.f * swp; o.pitchMs = expMap(5, 100, con);
      break;
    case DrumMachine::Sd8: snare(o, mac, r, 180, 330, 1800); break;
    case DrumMachine::Tom8:
      o.toneHz[0] = 110.f * r; o.toneLvl[0] = 1; o.toneMs = dec;
      o.noiseLvl = 0.3f * col; o.noiseMs = dec * 0.3f; o.noiseMode = Svf::Mode::Lp; o.noiseHz = 3000;
      o.drive = shp; o.pitchEnv = 7.f * swp; o.pitchMs = expMap(10, 200, con);
      break;
    case DrumMachine::Cp8: clap(o, mac, 600, 2500, 3); break;
    case DrumMachine::Rs8:
      o.toneHz[0] = 500.f * r; o.toneHz[1] = 1700.f * r;
      o.toneLvl[0] = 0.7f * (1 - col) + 0.15f; o.toneLvl[1] = 0.7f * col + 0.15f;
      o.toneMs = expMap(10, 120, d01);
      o.noiseLvl = 0.5f * shp; o.noiseMs = 5; o.noiseHz = 5000;
      o.pitchEnv = 5.f * swp; o.pitchMs = 3;
      break;
    case DrumMachine::Cl8:
      o.toneHz[0] = 2500.f * r * exp2f((col - 0.5f) * 2.f); o.toneLvl[0] = 1;
      o.toneMs = expMap(10, 120, d01);
      o.drive = shp; o.pitchEnv = 3.f * swp; o.pitchMs = 2;
      break;
    case DrumMachine::Cb8:
      o.metalHz[0] = 540.f * r; o.metalHz[1] = 800.f * r;
      o.metalW[0] = 1.f - 0.8f * shp; o.metalW[1] = 0.2f + 0.8f * shp;
      o.metalLvl = 1; o.metalMs = dec; o.metalAccent = 2.f * swp;
      o.metalBp1 = expMap(1500, 4000, col); o.metalBp2 = o.metalBp1; o.metalQ = 3;
      break;
    case DrumMachine::Hh8: hat(o, mac, kMetal808, 1.f, 20, 2000, 4000, 12000, 3440, 7100, r); break;
    case DrumMachine::Cy8: hat(o, mac, kMetal808, 0.7f, 300, 4000, 2000, 8000, 3000, 6000, r); break;
    case DrumMachine::Bd9:
      o.toneHz[0] = 50.f * r; o.toneLvl[0] = 1; o.toneMs = dec;
      o.click = col; o.clickMs = 3;
      o.drive = shp; o.pitchEnv = 24.f * swp; o.pitchMs = expMap(10, 80, con);
      break;
    case DrumMachine::Sd9: snare(o, mac, r, 190, 345, 1000); break;
    case DrumMachine::Tom9:
      o.toneHz[0] = 120.f * r; o.toneLvl[0] = 1; o.toneMs = dec;
      o.noiseLvl = 0.3f * col; o.noiseMs = dec * 0.3f; o.noiseMode = Svf::Mode::Lp; o.noiseHz = 4000;
      o.drive = shp; o.pitchEnv = 12.f * swp; o.pitchMs = expMap(10, 200, con);
      break;
    case DrumMachine::Cp9: clap(o, mac, 800, 3000, 4); break;
    case DrumMachine::Rs9:
      o.toneHz[0] = 1700.f * r; o.toneHz[1] = 3400.f * r;
      o.toneLvl[0] = 0.7f * (1 - col) + 0.15f; o.toneLvl[1] = 0.7f * col + 0.15f;
      o.toneMs = expMap(10, 80, d01);
      o.noiseLvl = 0.5f * shp; o.noiseMs = 4; o.noiseHz = 6000;
      o.pitchEnv = 5.f * swp; o.pitchMs = 2;
      break;
    case DrumMachine::Hh9: hat(o, mac, kMetal909, 2.f, 20, 2000, 6000, 14000, 6000, 9000, r); break;
    case DrumMachine::Cy9:
      hat(o, mac, kMetal909, 1.2f, 400, 4000, 3000, 10000, 4000, 7000, r);
      // SHAPE: ride (metal, band-passed lower) -> crash (noise, brighter).
      o.noiseLvl = 0.2f + 0.6f * shp;
      o.metalLvl = 1.f - 0.6f * shp;
      o.metalBp1 = expMap(2500, 5000, shp);
      o.metalBp2 = o.metalBp1 * 1.8f;
      break;
    default: break;
  }
  // Everything below the Nyquist margin (high notes).
  const float lim = kSynthRate * 0.45f;
  for (float& h : o.toneHz) h = h < lim ? h : lim * 0.99f;
  for (float& h : o.metalHz) h = h < lim ? h : 0;  // a square above the limit drops out
  if (o.metalBp1 >= lim) o.metalBp1 = lim * 0.99f;
  if (o.metalBp2 >= lim) o.metalBp2 = lim * 0.99f;
  if (o.metalHp >= lim) o.metalHp = lim * 0.99f;
  if (o.noiseHz >= lim) o.noiseHz = lim * 0.99f;
}

}  // namespace mt
```

`fmDecayMs` объявлена в `model.h`. Подключить `synth_osc.h` ради `kSynthRate`. Набор `kMetal909` = таблица дизайна × 2 (263 / 400 / 421 / 474 / 587 / 845 Гц); `scale` в `hat()` подобран под это.

После реализации обновить таблицу машин в дизайн-доке, если значения разошлись.

**Step 4:** `pio test -e native -f test_synth_drum_machines` — PASS.

---

### Task 8: DRUM в синте

**Files:**
- Modify: `lib/core/src/synth_voice.h` (`drum`, `DrumVoice drv`)
- Modify: `lib/core/src/synth_voice.cpp` (лимит тяжёлых голосов = FM + DRUM)
- Modify: `lib/core/src/synth.h`, `lib/core/src/synth.cpp`
- Test: `test/test_synth/test_main.cpp`, `test/test_voices/test_main.cpp`

**Step 1: тесты**

`test_synth`:

```cpp
static void drumInstr(int i, DrumMachine mc) {
  Instrument& m = p->instruments[i];
  m.type = InstrType::Drum;
  drumSetMachine(m, static_cast<uint8_t>(mc));
}

void test_drum_sounds_and_ends() {
  drumInstr(0, DrumMachine::Bd8);
  p->instruments[0].macro[kMacDec] = 40;  // short
  noteOn(0, 0, 60);
  TEST_ASSERT_FALSE(silentBlocks(1));
  for (int k = 0; k < 400 && s->activeVoices(); ++k) s->render(buf);
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_drum_ignores_note_off() {
  drumInstr(0, DrumMachine::Cy8);
  noteOn(0, 0, 60);
  s->render(buf);
  noteOff(0, 0, 60);
  for (int k = 0; k < 20; ++k) s->render(buf);  // 80 ms
  TEST_ASSERT_EQUAL(1, s->activeVoices());
}

void test_drum_mono_choke() {
  drumInstr(0, DrumMachine::Hh8);
  p->instruments[0].macro[kMacDec] = 120;  // open hat
  p->instruments[0].mono = false;           // ignored: DRUM is always mono
  noteOn(0, 0, 60);
  s->render(buf);
  noteOn(0, 0, 60);
  s->render(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
}

void test_drum_dec_lock() {
  drumInstr(0, DrumMachine::Hh8);
  p->instruments[0].macro[kMacDec] = 120;
  step(0, 0, true);
  fx(0, 0, Fx::DCY, 5);  // closed
  noteOn(0, 0, 60);
  for (int k = 0; k < 40 && s->activeVoices(); ++k) s->render(buf);  // 160 ms
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_heavy_voice_limit_counts_drum() {
  for (int t = 0; t < kTracks; ++t) {
    drumInstr(t, DrumMachine::Cy9);
    p->tracks[t].instr = t;
  }
  for (int t = 0; t < kTracks; ++t) noteOn(0, t, 60);
  s->render(buf);
  // 8 tracks fill kFmVoiceMax; a 9th heavy note (preview track) steals one of them.
  p->instruments[8].type = InstrType::Fm;
  send(0, kPreviewTrack, 0xC0, 8);
  noteOn(0, kPreviewTrack, 60);
  s->render(buf);
  TEST_ASSERT_TRUE(s->activeVoices() <= kFmVoiceMax);
}
```

`test_voices`: копия существующего теста лимита FM, где часть голосов помечена `drum = true` вместо `fm` — `allocVoice(..., true)` должна считать их вместе.

**Step 2:** `pio test -e native -f test_synth` и `-f test_voices` — FAIL.

**Step 3: реализация**

`synth_voice.h`: `#include "synth_drum.h"`; в `Voice` после FM-блока:

```cpp
  // DRUM instrument (machine above holds its DrumMachine).
  bool drum = false;
  DrumVoice drv;
```

Комментарии `kFmVoiceMax` и `allocVoice`: «FM and DRUM voices (heavy)». Параметр `fm` → `heavy`.

`synth_voice.cpp`: `if (x.fm || x.drum)` в подсчёте и `!(v[pick].on && (v[pick].fm || v[pick].drum))` в условии.

`synth.h`: `void controlDrum(Voice& v, const Instrument& m, float pitch, int dt, float l, float vol);`, `static bool oneShot(const Voice& v);` (заменяет `fmDrum`), `static uint8_t machineOf(const Instrument& m);` (заменяет `fmMachineOf`).

`synth.cpp`:

```cpp
uint8_t Synth::machineOf(const Instrument& m) {
  const uint8_t n = static_cast<uint8_t>(m.type == InstrType::Drum ? DrumMachine::Count : FmMachine::Count);
  return m.machine < n ? m.machine : 0;
}

// One-shot drum voice: ignores note-offs, always retriggers (choke).
bool Synth::oneShot(const Voice& v) { return v.drum || (v.fm && !fmGated(v.machine)); }
```

`noteOn`:

```cpp
  const bool fm = m.type == InstrType::Fm;
  const bool drumT = m.type == InstrType::Drum;
  const uint8_t machine = machineOf(m);
  const bool drum = drumT || (fm && !fmGated(machine));      // one-shot, choke
  const bool tone = fm && machine == static_cast<uint8_t>(FmMachine::Tone);
  const bool mono = fm ? (tone ? m.mono : true) : (drumT ? true : m.mono);
  ...
  // SLD from a light voice into a heavy one past the limit (как было для FM): heavy = fm || drumT
  if (held >= 0 && (fm || drumT) && !(voices_[held].fm || voices_[held].drum)) { ... x.fm || x.drum ... }
  ...
  vi = allocVoice(voices_, track, mono, age_, legato, fm || drumT);
  ...
  const bool keepFm = legato && v.fm;
  const bool prevDrum = legato && oneShot(v);
  ...
  v.fm = fm;
  v.drum = drumT;
  v.machine = machine;
  constexpr uint8_t kMacroBits = (1 << kFmMacros) - 1;
  v.lockMask = (fm || drumT) ? r.lockMask : (r.lockMask & ~kMacroBits);
  ...
  if (drumT) {
    v.drv.trigger(prevDrum && v.drum);  // заменить: флаг до перезаписи v.drum — сохранить wasDrum заранее
    v.lfoPhase = 0;
    v.lfoRnd = rnd();
  }
```

Внимание: `v.drum` перезаписывается выше — сохранить `const bool wasDrum = legato && v.drum;` рядом с `keepFm` и передать `v.drv.trigger(wasDrum)`.

Огибающая: ветка `if (drum)` уже ставит `env.set(0, 0, 1, kDrumReleaseMs)` — `drum` теперь включает DRUM.

`noteOff`: `!fmDrum(v)` → `!oneShot(v)`.

`fx()`: условие макро-локов `(!macro || x.fm || x.drum)` (из Task 4).

`control()`: после `controlFilter`:

```cpp
  if (v.drum) {
    controlDrum(v, m, pitch, dt, l, lfoVol);
    return;
  }
```

```cpp
void Synth::controlDrum(Voice& v, const Instrument& m, float pitch, int dt, float l, float vol) {
  float mac[kFmMacros];
  for (int k = 0; k < kFmMacros; ++k) mac[k] = (v.lockMask & (1 << k)) ? v.lock[k] : m.macro[k];
  const uint8_t dest = m.lfoDest < static_cast<uint8_t>(LfoDest::Count) ? m.lfoDest : 0;
  if (l != 0 && dest >= static_cast<uint8_t>(LfoDest::Dec) && dest <= static_cast<uint8_t>(LfoDest::Con))
    mac[dest - 1] = clampf(mac[dest - 1] + 64.f * l, 0.f, 127.f);
  DrumParams dp;
  drumMachine(v.machine, mac, pitch, dp);
  v.drv.control(dp, dt ? dt : ctlLeft_);
  const uint8_t iv = m.vol > 127 ? 127 : m.vol;
  v.amp = v.gain * iv * trackVol(v.track) * vol * (1.f / (127.f * 127.f));
}
```

`renderVoice`, перед `else if (v.sample)`:

```cpp
  } else if (v.drum) {
    v.drv.render(tmp, n, v.amp);
    for (int i = 0; i < n; ++i) tmp[i] *= v.env.next();
    if (v.drv.done()) v.env.kill();
```

`#include "synth_drum_machines.h"` в `synth.cpp`.

**Step 4:** `pio test -e native` — всё PASS.

---

### Task 9: preview и GRID для DRUM

**Files:**
- Modify: `src/audio/audio.cpp` (если preview / CPU-статистика различают типы — добавить DRUM)
- Modify: `src/ui/grid_screen.cpp` (если цвета / описания fx зависят от типа инструмента)

**Step 1:** `grep -rn "InstrType::Fm\|InstrType::Sample" src` — пройти по каждому месту и решить, нужен ли DRUM (как FM-ударные: one-shot, без OFS). Ожидаемо: `audio.cpp` (preview), `inst_screen.cpp` (Task 10), `web.cpp` (нет).

**Step 2:** `pio run -e wt32` — сборка OK.

---

### Task 10: вкладка INST — хвост фильтра и LFO, тип DRUM

**Files:**
- Modify: `src/ui/inst_screen.h`
- Modify: `src/ui/inst_screen.cpp`

Без native-тестов (UI). Проверка — сборка и ручной сценарий в сводке.

**Step 1: строки**

`inst_screen.h`:

```cpp
  enum Row : int {
    kName, kType, kVol, kTranspose, kFine, kAttack, kDecay, kSustain, kRelease, kMode, kGlide, kCommon,
    kWave = kCommon, kDuty, kPwmRate, kPwmDepth, kChipRows,
    kSample = kCommon, kRoot, kStart, kEnd, kLoop, kLoopStart, kReverse, kSampleRows,
    kMachine = kCommon, kMac0, kMacRows = kMac0 + mt::kFmMacros,  // FM and DRUM
  };
  // Filter and LFO: after the type's own rows (index = the type's row count + Tail).
  enum Tail : int {
    kFltMode, kCutoff, kReso, kFEnv, kFAtk, kFDec, kKeytrack,
    kLfoWave, kLfoRate, kLfoDepth, kLfoDest, kTailRows
  };
  ...
  Param chip_[kChipRows + kTailRows];
  Param sample_[kSampleRows + kTailRows];
  Param fm_[kMacRows + kTailRows];
  Param drum_[kMacRows + kTailRows];
  void initTail(Param* t, bool macros);  // t = &rows[typeRows]
  void relabel();                        // DRUM macro labels of the current machine
```

Комментарий класса: «Filter and LFO rows on every type; DRUM: machine, macros named per machine».

**Step 2: реализация** (`inst_screen.cpp`)

- Общие строки — цикл по `{chip_, sample_, fm_, drum_}`.
- `kType`: имена `{"CHIP", "SAMPLE", "FM", "DRUM"}`, правка через `mt::instrSetType(inst(), static_cast<mt::InstrType>(v))`.
- FM-строки: `kMacDecay..kMacContour` → `kMac0 + k`; LFO-строки FM удалить (переехали в хвост).
- DRUM:

```cpp
  auto always = [] { return true; };
  for (int r : {kAttack, kDecay, kSustain, kRelease, kMode, kGlide}) drum_[r].dim = always;
  drum_[kMachine] = {"Machine",
                     [this](char* o, int n) { snprintf(o, n, "%s", mt::drumMachineName(inst().machine)); },
                     [this](int d) {
                       const int v = clampi(inst().machine + d, 0, static_cast<int>(mt::DrumMachine::Count) - 1);
                       if (v != inst().machine) mt::drumSetMachine(inst(), static_cast<uint8_t>(v));
                     }};
  for (int k = 0; k < mt::kFmMacros; ++k) {
    drum_[kMac0 + k] = {"", macroNum(k), macroEdit(k),
                        [this, k] { return !*mt::drumMacroName(inst().machine, k); }};
  }
  drum_[kMac0 + mt::kMacDec].format = /* как DECAY у FM: fmDecayMs; для RS/CL/HH/CY — число */;
```

`relabel()` в `draw()` перед `list_.draw`: при `shown_ == Drum` — `drum_[kMac0 + k].label = mt::drumMacroName(inst().machine, k)`, пустое имя → `"-"`. DECAY показывать числом 0..127 для всех DRUM-машин (у части свой диапазон — мс неверны); у FM формат прежний.

- `initTail(Param* t, bool macros)`:

```cpp
void InstScreen::initTail(Param* t, bool macros) {
  auto off = [this] { return inst().fltMode == static_cast<uint8_t>(mt::FltMode::Off); };
  auto noEnv = [this, off] { return off() || inst().fenv == 0; };
  t[kFltMode] = {"Filter",
                 [this](char* o, int n) {
                   static const char* const kNames[] = {"OFF", "LP", "BP", "HP"};
                   snprintf(o, n, "%s", kNames[inst().fltMode % 4]);
                 },
                 [this](int d) {
                   inst().fltMode = static_cast<uint8_t>(
                       clampi(inst().fltMode + d, 0, static_cast<int>(mt::FltMode::Count) - 1));
                 }};
  t[kCutoff] = {"Cutoff",
                [this](char* o, int n) {
                  const float hz = mt::cutoffHz(inst().cutoff);
                  if (hz < 1000) snprintf(o, n, "%u Hz", static_cast<unsigned>(hz + 0.5f));
                  else snprintf(o, n, "%.1f kHz", hz / 1000.f);
                },
                [this](int d) { inst().cutoff = static_cast<uint8_t>(clampi(inst().cutoff + d, 0, 127)); }, off};
  t[kReso] = {"Reso", [this](char* o, int n) { snprintf(o, n, "%u", inst().reso); },
              [this](int d) { inst().reso = static_cast<uint8_t>(clampi(inst().reso + d, 0, 127)); }, off};
  t[kFEnv] = {"Flt env", [this](char* o, int n) { snprintf(o, n, "%+d", inst().fenv); },
              [this](int d) { inst().fenv = static_cast<int8_t>(clampi(inst().fenv + d, -64, 63)); }, off};
  t[kFAtk] = {"Flt attack", [this](char* o, int n) { envTime(inst().fAtk, o, n); },
              [this](int d) { inst().fAtk = static_cast<uint8_t>(clampi(inst().fAtk + d, 0, 127)); }, noEnv};
  t[kFDec] = {"Flt decay",
              [this](char* o, int n) {
                if (inst().fDec) envTime(inst().fDec, o, n);
                else snprintf(o, n, "HOLD");
              },
              [this](int d) { inst().fDec = static_cast<uint8_t>(clampi(inst().fDec + d, 0, 127)); }, noEnv};
  t[kKeytrack] = {"Key track", [this](char* o, int n) { snprintf(o, n, "%d%%", (inst().keytrack * 100 + 63) / 127); },
                  [this](int d) { inst().keytrack = static_cast<uint8_t>(clampi(inst().keytrack + d, 0, 127)); },
                  off};
  // LFO: rows moved from the FM list; the target list depends on the type.
  ... kLfoWave, kLfoRate, kLfoDepth — как были у FM ...
  t[kLfoDest] = {"LFO dest",
                 [this](char* o, int n) {
                   static const char* const kNames[] = {"PITCH", "DECAY", "COLOR", "SHAPE", "SWEEP",
                                                        "CONTOUR", "VOL", "CUTOFF"};
                   snprintf(o, n, "%s", kNames[inst().lfoDest % 8]);
                 },
                 [this, macros](int d) {
                   // CHIP / SAMPLE skip the macro targets.
                   int v = inst().lfoDest;
                   const int last = static_cast<int>(mt::LfoDest::Count) - 1;
                   const int step = d > 0 ? 1 : -1;
                   for (int i = 0; i < (d > 0 ? d : -d); ++i) {
                     int nv = v + step;
                     while (!macros && nv >= 1 && nv <= 5) nv += step;
                     if (nv < 0 || nv > last) break;
                     v = nv;
                   }
                   inst().lfoDest = static_cast<uint8_t>(v);
                 },
                 noLfo};
}
```

Для DRUM имена макро-целей LFO: «DECAY…CONTOUR» — общие (без имён машины), допустимо.

Вызовы: `initTail(chip_ + kChipRows, false)`, `initTail(sample_ + kSampleRows, false)`, `initTail(fm_ + kMacRows, true)`, `initTail(drum_ + kMacRows, true)`.

- `syncParams`: размеры `kXxxRows + kTailRows`, ветка `Drum` → `drum_`.

**Step 3:** `pio run -e wt32` — OK; `pio test -e native` — PASS.

---

### Task 11: формат пресета `.mti` и `applyPreset`

**Files:**
- Create: `lib/core/src/preset_io.h`
- Create: `lib/core/src/preset_io.cpp`
- Test: `test/test_preset_io/test_main.cpp`

**Step 1: тесты**

```cpp
#include <string.h>
#include <unity.h>
#include <vector>
#include "preset_io.h"

using namespace mt;

// VecSink / VecSource — копия из test_project_io.

void setUp() {}
void tearDown() {}

static Instrument sample() {
  Instrument m;
  strcpy(m.name, "ACID");
  m.type = InstrType::Chip;
  m.wave = static_cast<uint8_t>(Wave::Saw);
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  m.cutoff = 30;
  m.reso = 100;
  m.fenv = 50;
  m.lfoDest = static_cast<uint8_t>(LfoDest::Cutoff);
  m.lfoDepth = -20;
  return m;
}

void test_roundtrip() {
  const Instrument a = sample();
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(a, out));
  TEST_ASSERT_EQUAL(kPresetSize, out.buf.size());
  VecSource in(out.buf);
  Instrument b;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in, b)));
  TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof(Instrument));
}

void test_drum_roundtrip() {
  Instrument a;
  a.type = InstrType::Drum;
  drumSetMachine(a, static_cast<uint8_t>(DrumMachine::Hh9));
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(a, out));
  VecSource in(out.buf);
  Instrument b;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in, b)));
  TEST_ASSERT_EQUAL(static_cast<int>(DrumMachine::Hh9), b.machine);
}

static void expectUntouched(std::vector<uint8_t> buf, LoadErr want) {
  VecSource in(buf);
  Instrument b;
  strcpy(b.name, "KEEP");
  TEST_ASSERT_EQUAL(static_cast<int>(want), static_cast<int>(loadPreset(in, b)));
  TEST_ASSERT_EQUAL_STRING("KEEP", b.name);
}

void test_bad_files() {
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(sample(), out));
  std::vector<uint8_t> v = out.buf;
  v[0] = 'X';
  expectUntouched(v, LoadErr::BadMagic);
  v = out.buf;
  v[4] = 99;
  expectUntouched(v, LoadErr::BadVersion);
  v = out.buf;
  v[20] ^= 0xFF;
  expectUntouched(v, LoadErr::BadCrc);
  v = out.buf;
  v.resize(v.size() - 3);
  expectUntouched(v, LoadErr::Truncated);
}

void test_apply_sample_found_and_missing() {
  Instrument dst;
  strcpy(dst.sample, "MINE");
  dst.root = 48;
  Instrument src;
  src.type = InstrType::Sample;
  strcpy(src.name, "PAD");
  strcpy(src.sample, "THEIRS");
  src.root = 72;
  src.loop = static_cast<uint8_t>(LoopMode::Forward);
  Instrument a = dst;
  applyPreset(a, src, true);
  TEST_ASSERT_EQUAL_STRING("THEIRS", a.sample);
  TEST_ASSERT_EQUAL(72, a.root);
  Instrument b = dst;
  applyPreset(b, src, false);
  TEST_ASSERT_EQUAL_STRING("MINE", b.sample);
  TEST_ASSERT_EQUAL(48, b.root);
  TEST_ASSERT_EQUAL(static_cast<int>(LoopMode::Forward), b.loop);  // the rest comes from the preset
  TEST_ASSERT_EQUAL_STRING("PAD", b.name);
}
```

**Step 2:** `pio test -e native -f test_preset_io` — FAIL.

**Step 3: реализация**

`preset_io.h`:

```cpp
#pragma once
#include "inst_codec.h"
#include "project_io.h"

namespace mt {

// .mti: "MTI1" u8 version u8 type u16 reserved, INST + FMIN + FLTR records (inst_codec),
// u32 crc32 of every byte before it. Little-endian.
constexpr uint8_t kPresetVersion = 1;
constexpr size_t kPresetSize = 8 + kInstRecSize + kFmRecSize + kFltRecSize + 4;

bool savePreset(const Instrument& m, ByteSink& out);
// out is written only on Ok. Values are clamped like a project's.
LoadErr loadPreset(ByteSource& in, Instrument& out);
// Copies src into dst. A SAMPLE preset whose sample is not in the bank (sampleFound false) keeps
// dst's sample name and root.
void applyPreset(Instrument& dst, const Instrument& src, bool sampleFound);

}  // namespace mt
```

`preset_io.cpp`:

```cpp
#include "preset_io.h"
#include <string.h>

namespace mt {

bool savePreset(const Instrument& m, ByteSink& out) {
  uint8_t b[kPresetSize] = {'M', 'T', 'I', '1', kPresetVersion, static_cast<uint8_t>(m.type), 0, 0};
  size_t p = 8;
  packInst(m, b + p);
  p += kInstRecSize;
  packFm(m, b + p);
  p += kFmRecSize;
  packFlt(m, b + p);
  p += kFltRecSize;
  const uint32_t c = crc32(b, p);
  b[p] = static_cast<uint8_t>(c);
  b[p + 1] = static_cast<uint8_t>(c >> 8);
  b[p + 2] = static_cast<uint8_t>(c >> 16);
  b[p + 3] = static_cast<uint8_t>(c >> 24);
  return out.write(b, sizeof(b));
}

LoadErr loadPreset(ByteSource& in, Instrument& out) {
  uint8_t b[kPresetSize];
  if (!in.read(b, 8)) return LoadErr::Truncated;
  if (memcmp(b, "MTI1", 4) != 0) return LoadErr::BadMagic;
  if (b[4] > kPresetVersion) return LoadErr::BadVersion;
  if (!in.read(b + 8, kPresetSize - 8)) return LoadErr::Truncated;
  const size_t p = kPresetSize - 4;
  const uint32_t c = b[p] | (b[p + 1] << 8) | (b[p + 2] << 16) | (static_cast<uint32_t>(b[p + 3]) << 24);
  if (c != crc32(b, p)) return LoadErr::BadCrc;
  Instrument m;
  unpackInst(b + 8, m);
  unpackFm(b + 8 + kInstRecSize, m);
  unpackFlt(b + 8 + kInstRecSize + kFmRecSize, m);
  fixInstrument(m);
  out = m;
  return LoadErr::Ok;
}

void applyPreset(Instrument& dst, const Instrument& src, bool sampleFound) {
  char sample[kSampleNameMax + 1];
  memcpy(sample, dst.sample, sizeof(sample));
  const uint8_t root = dst.root;
  dst = src;
  if (src.type == InstrType::Sample && !sampleFound) {
    memcpy(dst.sample, sample, sizeof(sample));
    dst.root = root;
  }
}

}  // namespace mt
```

Тест `test_roundtrip` сравнивает память `Instrument` целиком: паддинг и хвосты строк должны совпасть. `unpackInst` копирует 8 байт имени и 16 байт сэмпла, `Instrument{}` обнуляет их — совпадёт. Если паддинг мешает — сравнить по полям.

**Step 4:** `pio test -e native -f test_preset_io` — PASS.

---

### Task 12: заводские пресеты

**Files:**
- Create: `lib/core/src/presets_factory.h`
- Create: `lib/core/src/presets_factory.cpp`
- Test: `test/test_presets_factory/test_main.cpp`

**Step 1: тесты**

```cpp
#include <string.h>
#include <unity.h>
#include "inst_codec.h"
#include "presets_factory.h"
#include "synth.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_counts() {
  TEST_ASSERT_TRUE(factoryCount(InstrType::Chip) >= 10);
  TEST_ASSERT_TRUE(factoryCount(InstrType::Fm) >= 10);
  TEST_ASSERT_TRUE(factoryCount(InstrType::Drum) >= 10);
  TEST_ASSERT_EQUAL(0, factoryCount(InstrType::Sample));
}

void test_names_unique_and_short() {
  for (int t = 0; t < static_cast<int>(InstrType::Count); ++t) {
    const InstrType ty = static_cast<InstrType>(t);
    for (int i = 0; i < factoryCount(ty); ++i) {
      const FactoryPreset& a = factoryPreset(ty, i);
      TEST_ASSERT_TRUE(strlen(a.name) >= 1 && strlen(a.name) <= 8);
      TEST_ASSERT_TRUE(strlen(a.category) >= 1 && strlen(a.category) <= 16);
      for (int j = i + 1; j < factoryCount(ty); ++j)
        TEST_ASSERT_TRUE_MESSAGE(strcmp(a.name, factoryPreset(ty, j).name) != 0, a.name);
    }
  }
}

void test_valid_values() {
  for (int t = 0; t < static_cast<int>(InstrType::Count); ++t) {
    const InstrType ty = static_cast<InstrType>(t);
    for (int i = 0; i < factoryCount(ty); ++i) {
      Instrument m;
      factoryBuild(ty, i, m);
      TEST_ASSERT_TRUE_MESSAGE(m.type == ty, factoryPreset(ty, i).name);
      TEST_ASSERT_EQUAL_STRING(factoryPreset(ty, i).name, m.name);
      // Clamped values survive pack -> unpack unchanged: everything is in range.
      uint8_t a[kInstRecSize], f[kFmRecSize], l[kFltRecSize];
      packInst(m, a);
      packFm(m, f);
      packFlt(m, l);
      Instrument r;
      unpackInst(a, r);
      unpackFm(f, r);
      unpackFlt(l, r);
      fixInstrument(r);
      TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&m, &r, sizeof(Instrument), m.name);
    }
  }
}

void test_every_preset_sounds() {
  static Project p;
  for (int t = 0; t < static_cast<int>(InstrType::Count); ++t) {
    const InstrType ty = static_cast<InstrType>(t);
    for (int i = 0; i < factoryCount(ty); ++i) {
      p.reset();
      p.masterVol = 100;
      p.tracks[0].out = TrackOut::Int;
      p.tracks[0].vol = 127;
      factoryBuild(ty, i, p.instruments[0]);
      Synth s(p);
      const uint8_t on[3] = {0x90, 60, 127};
      s.event(0, 0, on, 3);
      int16_t buf[Synth::kBlock];
      int peak = 0;
      for (int k = 0; k < 40; ++k) {  // 160 ms
        s.render(buf);
        for (int16_t x : buf) peak = x > peak ? x : (-x > peak ? -x : peak);
      }
      TEST_ASSERT_TRUE_MESSAGE(peak > 300, factoryPreset(ty, i).name);
    }
  }
}
```

**Step 2:** `pio test -e native -f test_presets_factory` — FAIL.

**Step 3: реализация**

`presets_factory.h`:

```cpp
#pragma once
#include "model.h"

namespace mt {

// Built-in presets, read-only. Order is the browser's: by category, then as listed.
struct FactoryPreset {
  InstrType type;
  const char* category;          // folder in the browser, e.g. "BASS"
  const char* name;              // <= 8 chars, also the instrument name
  void (*fill)(Instrument& m);   // on top of Instrument{} with type and name set
};

int factoryCount(InstrType t);
const FactoryPreset& factoryPreset(InstrType t, int i);  // 0 <= i < factoryCount(t)
// Instrument{} + type + name + fill.
void factoryBuild(InstrType t, int i, Instrument& out);

}  // namespace mt
```

`presets_factory.cpp` — таблица. Значения стартовые, подбираются на слух на железе:

```cpp
#include "presets_factory.h"
#include <stdio.h>

namespace mt {
namespace {

using I = Instrument;
constexpr uint8_t W(Wave w) { return static_cast<uint8_t>(w); }
constexpr uint8_t F(FltMode f) { return static_cast<uint8_t>(f); }
constexpr uint8_t D(LfoDest d) { return static_cast<uint8_t>(d); }
constexpr uint8_t Wt(int n) { return static_cast<uint8_t>(static_cast<int>(Wave::Wt1) + n); }
void fm(I& m, FmMachine mc) { fmSetMachine(m, static_cast<uint8_t>(mc)); }
void dr(I& m, DrumMachine mc) { drumSetMachine(m, static_cast<uint8_t>(mc)); }
void mac(I& m, uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e) {
  m.macro[0] = a; m.macro[1] = b; m.macro[2] = c; m.macro[3] = d; m.macro[4] = e;
}

constexpr FactoryPreset kAll[] = {
    // CHIP
    {InstrType::Chip, "LEAD", "SQ LEAD", [](I& m) {
       m.wave = W(Wave::Pulse); m.duty = 25; m.attack = 0; m.decay = 50; m.sustain = 90; m.release = 40;
       m.mono = true; m.glide = 10; m.lfoDest = D(LfoDest::Pitch); m.lfoRate = 70; m.lfoDepth = 2; }},
    {InstrType::Chip, "LEAD", "ARP PLK", [](I& m) {
       m.wave = W(Wave::Pulse); m.duty = 12; m.decay = 45; m.sustain = 0; m.release = 30;
       m.fltMode = F(FltMode::Lp); m.cutoff = 70; m.fenv = 40; m.fDec = 40; }},
    {InstrType::Chip, "BASS", "TRI BASS", [](I& m) {
       m.wave = W(Wave::Triangle); m.transpose = -12; m.decay = 60; m.sustain = 100; m.release = 20;
       m.mono = true; }},
    {InstrType::Chip, "BASS", "ACID", [](I& m) {
       m.wave = W(Wave::Saw); m.transpose = -12; m.decay = 55; m.sustain = 80; m.release = 15;
       m.mono = true; m.glide = 15; m.fltMode = F(FltMode::Lp); m.cutoff = 35; m.reso = 105;
       m.fenv = 45; m.fDec = 45; m.keytrack = 64; }},
    {InstrType::Chip, "PAD", "PWM PAD", [](I& m) {
       m.wave = W(Wave::Pulse); m.duty = 50; m.pwmRate = 20; m.pwmDepth = 35;
       m.attack = 75; m.decay = 80; m.sustain = 100; m.release = 85;
       m.fltMode = F(FltMode::Lp); m.cutoff = 85; m.reso = 20; }},
    {InstrType::Chip, "PAD", "WT PAD", [](I& m) {
       m.wave = Wt(3); m.attack = 80; m.decay = 90; m.sustain = 100; m.release = 90;
       m.lfoDest = D(LfoDest::Cutoff); m.lfoRate = 25; m.lfoDepth = 20;
       m.fltMode = F(FltMode::Lp); m.cutoff = 80; }},
    {InstrType::Chip, "KEYS", "WT BELL", [](I& m) {
       m.wave = Wt(7); m.decay = 85; m.sustain = 0; m.release = 70; }},
    {InstrType::Chip, "PERC", "NOIS HH", [](I& m) {
       m.wave = W(Wave::Noise); m.transpose = 24; m.decay = 25; m.sustain = 0; m.release = 10;
       m.fltMode = F(FltMode::Hp); m.cutoff = 105; }},
    {InstrType::Chip, "PERC", "NOIS SN", [](I& m) {
       m.wave = W(Wave::Noise); m.transpose = 12; m.decay = 40; m.sustain = 0; m.release = 20;
       m.fltMode = F(FltMode::Bp); m.cutoff = 85; m.reso = 30; }},
    {InstrType::Chip, "PERC", "METALBL", [](I& m) {
       m.wave = W(Wave::Metal); m.decay = 55; m.sustain = 0; m.release = 40;
       m.fltMode = F(FltMode::Bp); m.cutoff = 95; m.reso = 60; }},
    // FM
    {InstrType::Fm, "DRUMS", "KICK", [](I& m) { fm(m, FmMachine::Kick); }},
    {InstrType::Fm, "DRUMS", "SNARE", [](I& m) { fm(m, FmMachine::Snare); }},
    {InstrType::Fm, "DRUMS", "CLAP", [](I& m) { fm(m, FmMachine::Clap); }},
    {InstrType::Fm, "DRUMS", "HAT C", [](I& m) { fm(m, FmMachine::Hat); }},
    {InstrType::Fm, "DRUMS", "HAT O", [](I& m) { fm(m, FmMachine::Hat); m.macro[kMacDec] = 90; }},
    {InstrType::Fm, "DRUMS", "WOODBLK", [](I& m) { fm(m, FmMachine::Perc); mac(m, 45, 90, 80, 20, 30); }},
    {InstrType::Fm, "KEYS", "BELL", [](I& m) { fm(m, FmMachine::Metal); mac(m, 100, 60, 0, 0, 64); }},
    {InstrType::Fm, "KEYS", "E.PIANO", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 70, 50, 0, 60, 40); m.release = 50; }},
    {InstrType::Fm, "KEYS", "CHORD M7", [](I& m) {
       fm(m, FmMachine::Chord); m.macro[kMacShp] = 60;  // проверить индекс m7 по fmChordName
       m.attack = 10; m.release = 60; }},
    {InstrType::Fm, "BASS", "FM BASS", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 60, 70, 40, 50, 30); m.transpose = -12; m.mono = true;
       m.fltMode = F(FltMode::Lp); m.cutoff = 70; m.fenv = 20; }},
    // DRUM
    {InstrType::Drum, "808", "BD808", [](I& m) { dr(m, DrumMachine::Bd8); }},
    {InstrType::Drum, "808", "BD808 L", [](I& m) { dr(m, DrumMachine::Bd8); mac(m, 115, 10, 25, 30, 60); }},
    {InstrType::Drum, "808", "SD808", [](I& m) { dr(m, DrumMachine::Sd8); }},
    {InstrType::Drum, "808", "CH808", [](I& m) { dr(m, DrumMachine::Hh8); }},
    {InstrType::Drum, "808", "OH808", [](I& m) { dr(m, DrumMachine::Hh8); m.macro[kMacDec] = 85; }},
    {InstrType::Drum, "808", "CP808", [](I& m) { dr(m, DrumMachine::Cp8); }},
    {InstrType::Drum, "808", "CB808", [](I& m) { dr(m, DrumMachine::Cb8); }},
    {InstrType::Drum, "909", "BD909", [](I& m) { dr(m, DrumMachine::Bd9); }},
    {InstrType::Drum, "909", "SD909", [](I& m) { dr(m, DrumMachine::Sd9); }},
    {InstrType::Drum, "909", "OH909", [](I& m) { dr(m, DrumMachine::Hh9); m.macro[kMacDec] = 85; }},
};
constexpr int kAllCount = sizeof(kAll) / sizeof(kAll[0]);

}  // namespace

int factoryCount(InstrType t) {
  int n = 0;
  for (const auto& p : kAll) n += p.type == t;
  return n;
}

const FactoryPreset& factoryPreset(InstrType t, int i) {
  for (const auto& p : kAll)
    if (p.type == t && i-- == 0) return p;
  return kAll[0];
}

void factoryBuild(InstrType t, int i, Instrument& out) {
  const FactoryPreset& p = factoryPreset(t, i);
  out = Instrument();
  out.type = p.type;
  snprintf(out.name, sizeof(out.name), "%s", p.name);
  p.fill(out);
}

}  // namespace mt
```

Лямбды без захвата приводятся к `void(*)(Instrument&)` — `constexpr`-таблица во flash. Если GCC не примет лямбды в `constexpr`-массиве (C++17 разрешает), — `static const` массив. `vol` у всех по умолчанию 100, `name` — из таблицы.

Перед `CHORD M7` проверить, какое значение SHAPE даёт «M7» (`fmChordName`, 12 типов: значение ≈ index × 127 / 12) и поставить середину зоны.

**Step 4:** `pio test -e native -f test_presets_factory` — PASS. Если «звучит» падает на каком-то пресете — поправить значения (чаще всего cutoff слишком низкий для C4).

---

### Task 13: хранение пресетов на SD

**Files:**
- Create: `src/storage/presets.h`
- Create: `src/storage/presets.cpp`
- Modify: `src/hw/sdcard.cpp:40-43` (создание `/presets/<TYPE>`)

Без native-тестов (FS). Проверка — сборка, ручной сценарий в сводке.

**Step 1: API**

```cpp
#pragma once
#include "model.h"
#include "storage.h"

namespace storage {

// User presets: /presets/<TYPE>/[folders up to kPresetDepthMax]/NAME.mti. UI task only.
constexpr int kPresetDepthMax = 4;
// "/presets/CHIP" etc.
const char* presetRoot(mt::InstrType t);
// Folders below the type root of dir (0 = the root itself).
int presetDepth(const char* dir);
bool presetExists(const char* dir, const char* name);
// Writes NAME.tmp, reads it back (loadPreset), replaces NAME.mti.
Result savePreset(const char* dir, const char* name, const mt::Instrument& m);
Result loadPreset(const char* dir, const char* name, mt::Instrument& out);
Result removePreset(const char* dir, const char* name);
Result makePresetDir(const char* dir, const char* name);

}  // namespace storage
```

**Step 2: реализация** — по образцу `storage::save` (`storage.cpp:160-200`: tmp → проверка → rename) и `hw::FileSink/FileSource`. `name` проходит `storage::validName` до вызова (UI). Путь: `snprintf(path, sizeof(path), "%s/%s.mti", dir, name)`, буфер 192. `sdBegin()` создаёт `/presets` и четыре папки типов (`mkdir`, если нет).

`removePreset` удаляет только `.mti`. Пустые папки удаляются через Wi-Fi (как в `/samples`).

**Step 3:** `pio run -e wt32` — OK.

---

### Task 14: браузер пресетов в INST

**Files:**
- Create: `src/ui/preset_browser.h`
- Create: `src/ui/preset_browser.cpp`
- Modify: `src/ui/inst_screen.h`, `src/ui/inst_screen.cpp` (кнопка PRESET, владение браузером)

**Step 1: шапка INST**

Новые зоны (`kCharW = 8`, ширина 480): левая стрелка `0..56`, заголовок `56..232`, правая стрелка `232..288`, `PRESET` `296..376`, `PREVIEW` `384..472`. Тап по PRESET или Shift + EncLong → `app_.menu().open("PRESET", {LOAD, SAVE}, ...)`.

**Step 2: `PresetBrowser`**

```cpp
#pragma once
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "hw/sdcard.h"
#include "model.h"
#include "theme.h"
#include "touch.h"

namespace ui {

class App;

// Preset browser over the work area (INST). Load: the type's root lists [FACTORY] (built-in, by
// category), the user's folders and .mti files of /presets/<TYPE>; ".." goes up. Selecting a preset
// applies it to the instrument at once and plays C4; OK keeps it, CANCEL (or EncLong) puts the
// instrument back. DEL removes a user preset (menu confirm). Header arrows switch the type.
// Save: the instrument's type, user folders only; SAVE HERE asks the name (keyboard) and confirms
// an overwrite; + FOLDER makes a folder. The last folder per type is kept while powered.
class PresetBrowser {
 public:
  enum class Mode : uint8_t { Load, Save };
  explicit PresetBrowser(App& app) : app_(app) {}
  void open(Mode mode, int instr);
  void close(bool keep);  // Load: false restores the instrument
  bool isOpen() const { return open_; }
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y0);

 private:
  static constexpr int kMaxEntries = 64;
  static constexpr int kHeaderH = 28, kRowH = 24;
  static constexpr int kRows = (kAreaH - kHeaderH) / kRowH;
  enum class Kind : uint8_t { Up, Factory, Category, Folder, File, FactoryFile };
  struct Entry {
    Kind kind;
    int16_t index;  // factory preset index / category ordinal
    char name[hw::kNameMax];
  };

  void list();          // fills entries_ for type_ / dir_ / inFactory_
  void enter(int i);    // folder / category / ".."
  void pick(int i);     // Load: apply + preview
  void applyFile(const char* name);
  void applyFactory(int index);
  void apply(const mt::Instrument& src);  // applyPreset under lockProject, markDirty, preview
  void saveAs(const char* name);
  void switchType(int d);

  App& app_;
  bool open_ = false;
  Mode mode_ = Mode::Load;
  int instr_ = 0;
  mt::InstrType type_ = mt::InstrType::Chip;
  char dir_[160] = {0};
  bool inFactory_ = false;
  char category_[17] = {0};  // inside [FACTORY]: "" = category list
  Entry* entries_ = nullptr;  // PSRAM, kMaxEntries, while open
  int count_ = 0, sel_ = 0, top_ = 0, dragAcc_ = 0;
  mt::Instrument backup_;
  uint32_t seqBefore_ = 0;
  bool changed_ = false;
  // Last folder per type (RAM).
  char lastDir_[static_cast<int>(mt::InstrType::Count)][160] = {};
};

}  // namespace ui
```

Поведение (реализовать по образцу списков `FileScreen`: `sdListDirs`, `sdList(dir, ".mti", ...)`, прокрутка и drag как в `BankScreen::chainTouch`):

- `open(Load)`: `backup_ = inst`, `seqBefore_ = app_.editSeq()`, `type_ = inst.type`, `dir_ = lastDir_[type]` или `presetRoot(type)`; если папки нет — корень.
- `pick` на `File` / `FactoryFile`: загрузка (`storage::loadPreset` / `factoryBuild`), `apply`. `apply`: `sampleFound = mt::projSampleFind(app_.project(), src.sample) >= 0` (сэмплы проекта, `sample_set.h`); `engine::lockProject(); applyPreset(inst, src, sampleFound); engine::unlockProject(); app_.markDirty(); audio::preview(instr_, 60); changed_ = true;`. Ошибка чтения — toast `resultText`.
- Поворот энкодера двигает `sel_`; если строка — файл, сразу `pick` (живое прослушивание). Клик: файл — OK (`close(true)`), папка — `enter`. EncLong — `close(false)`.
- `close(false)` при `changed_`: восстановить `backup_` под `lockProject`; если `app_.editSeq()` совпадает с последним значением после собственных правок браузера (запомнить `seqAfter_` при каждом `apply`), — `app_.rewindEditSeq(seqBefore_)`.
- `switchType` (только Load): `type_` ±1 по `InstrType::Count`, `dir_ = lastDir_` нового типа, `inFactory_ = false`. Пресет другого типа меняет тип инструмента — это нормально (`applyPreset` копирует `type`).
- `[FACTORY]`: категории — уникальные `category` в порядке таблицы; внутри категории — пресеты. SAMPLE: `factoryCount = 0` → строку `[FACTORY]` не показывать.
- Save: `type_ = inst.type`, без `[FACTORY]`. Кнопка SAVE HERE → `app_.keyboard()` (если клавиатура не в `App`, а в `FileScreen` — завести экземпляр `Keyboard` внутри браузера) с начальным текстом `inst.name`. `storage::sanitize` → если `presetExists` — меню «OVERWRITE?» YES / NO. Запись: копия `inst`, имя = первые 8 символов (в верхнем регистре не переводить), `storage::savePreset`. Успех — toast `SAVED`, имя инструмента тоже меняется (под `lockProject`, `markDirty`), `close(true)`.
- + FOLDER: клавиатура → `sanitize` → `makePresetDir`; глубина `presetDepth(dir_) < kPresetDepthMax`, иначе toast `TOO DEEP`.
- DEL (Load, только `File`): меню «DELETE NAME?» → `removePreset`, `list()`.
- Перед SD-операциями — `app_.showBusy("...")` как в `FileScreen`.
- `entries_` — `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` на `open`, освобождение на `close`.

Шапка браузера: `◀ CHIP ▶` (стрелки только в Load), путь от корня типа (обрезанный слева), кнопки справа: Load — `OK` `DEL`, Save — `SAVE` `+DIR`; `CANCEL` — по EncLong и тапу по пути. Конкретные координаты — подобрать по ширине (480), стиль кнопок — как `PREVIEW` в INST (`kPlayBg`, `kCursor`).

**Step 3: интеграция в `InstScreen`**

- член `PresetBrowser presets_{app_};`
- `onInput` / `onTouch` / `draw`: если `presets_.isOpen()` — всё уходит браузеру.
- `onLeave()` (переопределить): `if (presets_.isOpen()) presets_.close(false);`
- `onProjectReplaced()`: закрыть браузер без восстановления (проект уже другой) — `close(true)`.
- После `close` — `syncParams()` (тип мог смениться) и `fixNames()`.

**Step 4:** `pio run -e wt32` — OK.

---

### Task 15: Wi-Fi — раздел /presets

**Files:**
- Modify: `src/net/web.cpp:82-120, 319-340` (секции, подпапки)
- Modify: `src/net/web_page.h:7, 46-70, 145-146`

**Step 1:** секция `presets` рядом с `samples`: подпапки разрешены (обобщить проверку «samples only» до `samples` и `presets`), загрузка только `.mti`, размер ≤ 1024. После приёма tmp-файла — проверка `mt::loadPreset` (через `hw::FileSource`), при ошибке — удалить и ответить 400 `bad preset`. Глубина — `storage::kPresetDepthMax` + 1 (папка типа).

**Step 2:** страница: `<section id="presets"><h2>Пресеты (/presets)</h2>` с крошками и «новая папка» как у samples; `sub.presets=''`; добавить в цикл `for (const dir of ['midi','projects','samples','presets'])`; кнопка mkdir для `#presets`.

**Step 3:** `pio run -e wt32` — OK.

---

### Task 16: бенч DRUM и фильтра

**Files:**
- Modify: `src/audio/audio.cpp:19-30, 178-410`

**Step 1:** флаг `-DAUDIO_BENCH_DRUM` по образцу `AUDIO_BENCH_FM`: 8 дорожек с DRUM (BD8, SD8, HH8 открытый, CY9, CP8, CB8, TOM9, HH9) на каждые 16-е, плюс 8 CHIP-голосов POLY с `fltMode = Lp`, `reso = 100`, `fenv = 40`. CPU в Serial — как у FM-бенча.

**Step 2:** `pio run -e wt32` (без флага) — OK. С флагом — собрать: `pio run -e wt32 -O "build_flags=... -DAUDIO_BENCH_DRUM"` или временно добавить в `platformio.ini` и убрать.

Замер — на железе: цель < 60 % ядра 0. Иначе — таблица синуса в `DrumVoice` (Task 6, заметка), затем `kFmVoiceMax` для DRUM.

---

### Task 17: документация

**Files:**
- Modify: `README.md` (раздел «Звук»: фильтр, LFO на всех типах, DRUM, пресеты, fx FLT/RES)
- Modify: `docs/manual.html` (INST: строки фильтра и LFO, тип DRUM и таблица машин с именами макросов, браузер пресетов; GRID: fx FLT, RES; FILE / Wi-Fi: /presets)
- Modify: `docs/plans/future-audio.md` (статус: фильтр и 808/909 сделаны, пресеты)
- Modify: `docs/plans/2026-10-05-filter-drum-presets-design.md` (если значения машин разошлись с кодом)

**Step 1:** текст. **Step 2:** `pio test -e native` — PASS, `pio run -e wt32` — OK. Сводка пользователю: что сделано, отклонения от плана, что проверить на железе (тембры DRUM и заводских пресетов, CPU бенча, браузер пресетов, Wi-Fi /presets).
