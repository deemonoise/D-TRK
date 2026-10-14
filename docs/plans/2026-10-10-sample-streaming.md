# Стриминг сэмплов с SD — план реализации

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Сэмплы проекта играют стримом с SD Teensy через пул из 16 слотов в RAM2. Начала нот берутся из «голов» во flash, PSRAM служит кэшем целых сэмплов. Лимит банка (≈5,8 МБ) и запись сэмплов во flash уходят.

**Architecture:** В `lib/core` появляются портируемый блочный кэш (`StreamPool`), загрузчик по пути воспроизведения (`stream_loader`), планировщик по запасу времени (`schedulePass`) и новый интерфейс источника (`SampleSource::find(name, SampleData&)`). Голос берёт кадр из целого сэмпла (flash / PSRAM), из «головы» или из блока слота; если кадра нет нигде, голос уходит в фейд 2 мс (недогрузка). Teensy читает блоки сырыми секторами из непрерывных файлов хранилища `/mt-store/<crc>.wav` в кооперативном `loop()`. ESP заранее шлёт подсказки (псевдо-событие `0xF4` в `EvBatch`) и показывает недогрузки.

**Tech Stack:** C++17, PlatformIO: Teensy 4.1 (Teensyduino: Audio, SdFat SDIO в режиме FIFO), ESP32-S3 Arduino, Unity (native), Python 3 (`scripts/link_desk.py`).

Дизайн: `docs/plans/2026-10-10-sample-streaming-design.md`. **Без коммитов** (правило пользователя): шагов `git commit` в плане нет, коммитит пользователь по просьбе.

---

## Отклонения от дизайна

1. **Хранилище по CRC вместо стриминга из `/projects/NAME/<имя>.wav`.** Сэмплы играют из `/mt-store/<8hex crc>.wav` на карте Teensy: канонический WAV (моно, 16 бит, ≤44,1 кГц), `data` со смещения 512, файл непрерывный (`preAllocate`). Причины:
   - у безымянного или несохранённого проекта нет папки;
   - переименование сэмпла и Save As меняют путь только после сохранения;
   - один сэмпл может входить в несколько проектов.

   Папка проекта остаётся сохранённой копией. `BankSync` докладывает недостающее из неё в хранилище, `AssetsSave` копирует хранилище в папку, `BankImport` пишет в хранилище. API ESP почти не меняется: «cached» теперь значит «есть в хранилище».
2. **«Исходная частота» = как сейчас, ≤44,1 кГц** (`kWavMaxRate`). Выше 44,1 кГц импорт по-прежнему понижает частоту (`Downsampler`): пул и бюджет чтения считаются на 44,1 кГц.
3. **Упреждение — псевдо-событие `{0xF4, note, ofs}` в `EvBatch`, а не новое сообщение `Prefetch`.** Подсказка идёт тем же путём и с той же меткой времени, что ноты. Teensy перехватывает её в `onEv` до `EvQueue`, иначе событие «из будущего» блокировало бы FIFO. Инструмент определяет Teensy (`trackInstr`), так что подсказка привязана к дорожке, а не к инструменту.
4. **Кража слота — не передача.** Если слотов нет, у самого старого стрим-голоса включается фейд `kStealMs`. Новый голос тем временем играет из «головы» и забирает слот, когда старый освободится: на каждом control-шаге голос без слота повторяет `acquire`. Так ISR ни в какой момент не читает слот чужого ключа.
5. **Бюджет RAM2: delay остаётся 2 с (моно, 176 400 Б), PERF уходит из RAM2.** Пул 96 КБ в `DMAMEM` поднимает `.bss.dma` до ≈130,8 КБ. После мержа main (2026-10-13) `allocPerf` без PSRAM на CS0 брал 1 с PERF (176 400 Б) из RAM2 — с пулом ему не хватает места, PERF молчал бы. Решение — отклонение 14: буфер PERF в арене PSRAM CS1, из RAM2 PERF не берёт. Код `allocDelay` не меняется; точные числа — замер в Task 9.
6. **Запросы ФС от ESP не ждут планировщика.** Они короткие (≤480 Б, ≈0,1 мс) и обслуживаются сразу в `link::poll`. Фоновые порции — это банк/хранилище, инструменты (пики, онсеты), наполнение PSRAM и поиск в хранилище.
7. **Превью на слоте пула.** У `StreamRing` появляется внешний буфер: 3072 кадра (≈70 мс) вместо 8192 (186 мс). Кольцо превью — потребитель планировщика. 16 КБ в DTCM освобождаются.
8. **`WavePeaks` / `Onsets` отвечают отложенно.** Теперь это задачи (ESP ждёт через `requestLong`). Для широких колонок пики берутся из обзорного файла `/mt-store/<crc>.pk` (min/max int8 на 256 кадров), он пишется вместе с файлом хранилища. Узкие колонки и онсеты читают кадры (`OnsetScan` потоковый, результат равен `detectOnsets`). Если сэмпл есть в PSRAM, используется быстрый путь.
9. **Офлайн-рендер грузит синхронно.** Рендер идёт на том же синте при припаркованном звуке, поэтому пул общий. На время рендера `pool.sync` указывает на синхронный ридер: при промахе блок читается сразу, и рендер бит-в-бит равен RAM-рендеру (если одновременно звучит больше 16 стрим-голосов, лишние крадутся, как вживую).
10. **«Головы» пишутся только при `BankSync` и `BankImport`** (под `park`). Если loopStart или слайсы изменили во время игры, до следующей загрузки проекта они идут с SD и через упреждение. Одна запись банка `"h"+crc` на сэмпл, в ней до 40 голов.
11. **SLC в FX-режиме в подсказку не попадает.** В подсказке только OFS; для такого шага старт берётся из «головы» слайса.
12. **Добавлен float32 WAV** (`wavParse` / `wavToMono`): дизайн требует конвертировать float, а сейчас он не поддержан.
13. **Status дописывается в конец (после `scope`), `kProtocol = 3`.** Python-клиент тоже переходит на `PROTOCOL = 3`.

14. **PERF — в арене PSRAM CS1 (после мержа main, 2026-10-13).** Main добавил кольцевой буфер PERF (`allocPerf`, `Synth::setPerfBuffer`, `Hello.perfSec`). На этой плате CS0 нет, так что `extmem_calloc` не работает. Порядок `setup` меняется: `card::begin` → `bank::begin` → `psram::begin` (Task 18) → `allocPerf` → `allocDelay` → … → `link::begin(..., perfSec)`. `allocPerf`: если `psram::ok()` — 6 с стерео (6 × 44 100 × 2 × 2 = 1 058 400 Б) из зарезервированного участка `PsramArena` в начале арены (до кэша сэмплов, Task 16: `PsramArena` получает `reserve(bytes)` до первого `alloc`); иначе `perfSec = 0`. Запасной вариант из RAM2 (и ветку `extmem_calloc`) удаляет уже Task 9 — вместе с пулом, иначе он отнимет RAM2 у delay; до Task 18 `allocPerf` возвращает 0. Сторож PSRAM при сбое отцепляет буфер (`synth.setPerfBuffer(nullptr, 0, false)` под `AudioNoInterrupts`), PERF замолкает до перезагрузки; ESP узнаёт по `Status::psram == 2`. `perfSec` в `Hello` не меняется на ходу.
15. **Подсказка упреждения — байт `0xF4` (`kSynthHint`), не `0xF6`.** `0xF6` занят PERF (`0xF6 op val`; секвенсор шлёт шаговые часы `0xF6 4` через `pushPerfStep` тем же `EvBatch`). Во всём плане `0xF6` заменён на `0xF4`. `Synth::dispatch` продолжает обрабатывать `0xF6`; ранний выход в `Synth::event` для `kSynthHint` не должен ловить `0xF6`; `isOff` по-прежнему считает `0xF6 1/3` отпусканиями. `audio_out` уже отбрасывает опоздавшие `0xF6 4` — `0xF4` обрабатывается отдельно.
16. **`kProtocol = 3`, `Msg::Debug = 41`.** Main уже поднял протокол до 2 (`Msg::Perf = 40`, `Hello.perfSec`, `Status.parks`). Status сейчас 29 + 256 (scope) + 7 = 292 Б; поля стрима — после `scope`, `parks` остаётся на месте. Декодер Hello по-прежнему терпит отсутствие `perfSec`. `link_desk.py` уже знает `PROTOCOL = 2`, `Perf`, `perfSec`, `parse_status` с `<HBH> p[24:29]`: переход на `PROTOCOL = 3`, `Debug` = 41, поля стрима читаются с `i = 29 + scope_n`.
17. **Строка «Samples» — в FILE → SYSTEM.** Страницы PROJ → SYS больше нет: системные строки переехали в `src/ui/file_screen.h` (`SysRow`, вставка после `kCard`) и `file_screen.cpp` (`sysParams_`, ≈стр. 1050+). Якорь в `audio_link.cpp` — ≈стр. 171.
18. **Сдвиги якорей после мержа main** (номера строк в задачах — ориентиры, искать по имени):
    - Task 1: `startSample` — `synth.cpp` 783–830.
    - Task 2: `noteOn` 543–781 (поиск сэмпла 574–580, `restartSmp` 662, присваивания 667–672); `FakeBank` в `test_synth` ≈455, `ArrayBank` ≈1441. В `Voice` новые поля pan / LFO, вставка после `smpRate` по-прежнему годится.
    - Task 5: `reset()` ≈115 (сбрасывает ещё `perf_` и компрессор); ветка SAMPLE в control ≈970; `renderVoice` ≈1266, вызов `renderSample` ≈1296 внутри `} else if (v.sample) {`; голоса освобождаются только в ≈1489; `stolen` также используется поиском жертвы CPU-лимита в `setLoad` — не ломать.
    - Task 9: `setup` 112–137, `loop` 139–148; между `bank::begin` и `allocDelay` стоит `allocPerf` (порядок — отклонение 14); `link::begin` принимает `perfSec`; переименование stream → out затрагивает и `bench::begin`, и строку лога.
    - Task 10: в `wav.cpp` изменился только `wavHeader` (стерео-каналы).
    - Task 15: запись «голов» под `park` сбрасывает синт, а с ним PERF (отпускание, `Status.parks` растёт) — допустимо: идёт при `BankSync` / `BankImport`.
    - Task 18: лог печатает `external_psram_size` (0 на этой плате) — добавить PSRAM CS1 и `perfSec`.
    - Task 19: якорь в секвенсоре — `playTrackStep` (`pushStepStart` ≈495 с аргументом `tps`, без `su`): брать `c.stepUs`; `minT_` сохранять и восстанавливать, как в `pushPerfStep`. `kSynthStep` — `model.h` ≈287.
    - Task 20: читать `p_.instruments` напрямую, не через кэш `inst()` / `cacheInstrument` (гонка с ISR).
    - Task 21: `onEv` ≈95.
    - Task 23: новые поля в `sendStatus` — после `s.parks`.
    - Task 27: `manual.md` — строка состояния ≈121, раздел SYSTEM под FILE ≈1457, SAMPLES ≈1415, Internal sound ≈1545, Limits ≈1571.

## Условности

- Нативные тесты: `pio test -e native -f <dir>`, весь набор — `pio test -e native`.
- Сборки: `pio run -e wt32 -e teensy41 -e teensy41-desk -e teensy41-bench`. После каждой задачи должны собираться все четыре.
- Стиль: 2 пробела, `namespace mt` в `lib/core`, английский в коде, комментарии такой же плотности, как вокруг.
- Desk-проверки: голый Teensy по USB, `python3 scripts/link_desk.py --port /dev/cu.usbmodemXXXX <cmd>`. Ниже `LD` означает эту команду целиком.
- Не останавливаться между задачами (правило autonomous-finish). Остановиться, только если сборку или тест не удаётся довести до зелёного.

## Числа

| Что | Значение |
|---|---|
| Слоты пула | `kStreamSlots = MT_STREAM_SLOTS = 16` |
| Блок | `kStreamBlock = 512` кадров (1 КБ, 2 сектора) |
| Блоков в слоте | `kSlotBlocks = 6` (3072 кадра, ≈70 мс при 44,1 кГц) |
| Пул | 16 × 6 × 1 КБ = 98 304 Б, `DMAMEM` |
| Впереди | текущий блок + `kAhead = 4` |
| Одна загрузка | ≤ `kLoadMax = 4` блока подряд, один `readSectors` (≤4 КБ) |
| Проход планировщика | ≤ `kPassLoads = 4` загрузки по возрастанию запаса; одна фоновая порция, только если все запасы > `kBgReserveUs = 30000` |
| «Головы» | старт и петля 100 мс, слайс 25 мс, ≤ `kMaxHeads = 40` на сэмпл |
| Недогрузка | удержание последнего значения, рампа 2 мс (`kUnderStep = 1/88`), счётчик |
| PSRAM | арена 8 МБ − 4 КБ сторожа − PERF 1 058 400 Б, выравнивание 32; сторож раз в ≈100 мс |
| Хранилище | `kStoreEntries = 256`, пул голов `kHeadPool = 1024` |

**RAM2 (512 КБ), оценка по `.pio/build/teensy41/firmware.elf` до мержа main, уточнить замером в Task 9:**

| | было | станет |
|---|---|---|
| `.bss.dma` | 32 448 | 130 752 (+пул 98 304) |
| delay | 2 с моно = 176 400 | 2 с моно = 176 400 |
| PERF | 1 с стерео = 176 400 из RAM2 (если хватает) | 0 в RAM2; 6 с = 1 058 400 в PSRAM CS1 (отклонение 14) |

DTCM: кольцо превью 16 384 Б уходит, пул голов +12 288 Б, таблица хранилища +256 × 28 = 7 168 Б.

---

## Этап 1 — блочный кэш и голос на абстракции источника (lib/core)

После этапа Teensy играет как раньше: целиком из flash, пул не подключён. Весь новый путь проверен нативными тестами.

### Task 1: Чистая функция региона сэмпла

**Files:**
- Create: `lib/core/src/sample_region.h`, `lib/core/src/sample_region.cpp`
- Modify: `lib/core/src/synth.cpp:680-725` (`Synth::startSample`)
- Test: `test/test_sample_region/test_main.cpp` (Create)

**Step 1: Падающий тест**

```cpp
#include <unity.h>
#include "sample_region.h"
#include "slices.h"

using namespace mt;

static Instrument m;

void setUp() {
  m = Instrument();
  m.start = 0;
  m.end = 0xFFFF;
  m.loopStart = 0;
  m.loop = static_cast<uint8_t>(LoopMode::Off);
  m.reverse = false;
  m.sliceCount = 0;
}
void tearDown() {}

void test_whole_region_plays_from_start() {
  const SampleRegion r = sampleRegion(m, -1, 1000, 0);
  TEST_ASSERT_EQUAL_UINT32(0, r.from);
  TEST_ASSERT_EQUAL_UINT32(1000, r.to);
  TEST_ASSERT_EQUAL_UINT32(0, r.loopLo);
  TEST_ASSERT_EQUAL_UINT32(1000, r.loopHi);
  TEST_ASSERT_EQUAL(1, r.dir);
  TEST_ASSERT_TRUE(r.pos == 0);
}

void test_forward_loop_and_ofs() {
  m.loop = static_cast<uint8_t>(LoopMode::Forward);
  m.loopStart = 0x8000;
  const SampleRegion r = sampleRegion(m, -1, 1000, 64);
  TEST_ASSERT_EQUAL_UINT32(500, r.loopLo);
  TEST_ASSERT_EQUAL_UINT32(1000, r.loopHi);
  TEST_ASSERT_TRUE(r.pos == (int64_t(250) << 32));
}

void test_reverse_mirrors_loop_and_start() {
  m.loop = static_cast<uint8_t>(LoopMode::Forward);
  m.loopStart = 0x8000;
  m.reverse = true;
  const SampleRegion r = sampleRegion(m, -1, 1000, 64);
  TEST_ASSERT_EQUAL(-1, r.dir);
  TEST_ASSERT_EQUAL_UINT32(0, r.loopLo);
  TEST_ASSERT_EQUAL_UINT32(500, r.loopHi);
  TEST_ASSERT_TRUE(r.pos == (int64_t(749) << 32));
}

void test_end_before_start_clamps() {
  m.start = 0xFFFF;
  m.end = 0;
  const SampleRegion r = sampleRegion(m, -1, 1000, 0);
  TEST_ASSERT_EQUAL_UINT32(999, r.from);
  TEST_ASSERT_EQUAL_UINT32(1000, r.to);
}

void test_slice_ignores_loop() {
  m.loop = static_cast<uint8_t>(LoopMode::Forward);
  TEST_ASSERT_TRUE(sliceInsert(m, 0x4000) >= 0);
  TEST_ASSERT_TRUE(sliceInsert(m, 0x8000) >= 0);
  uint32_t f = 0, t = 0;
  TEST_ASSERT_TRUE(sliceRegion(m, 1, 1000, f, t));
  const SampleRegion r = sampleRegion(m, 1, 1000, 0);
  TEST_ASSERT_EQUAL_UINT32(f, r.from);
  TEST_ASSERT_EQUAL_UINT32(t, r.to);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(LoopMode::Off), r.loopMode);
  TEST_ASSERT_EQUAL_UINT32(f, r.loopLo);
  TEST_ASSERT_EQUAL_UINT32(t, r.loopHi);
  TEST_ASSERT_TRUE(r.pos == (int64_t(f) << 32));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_whole_region_plays_from_start);
  RUN_TEST(test_forward_loop_and_ofs);
  RUN_TEST(test_reverse_mirrors_loop_and_start);
  RUN_TEST(test_end_before_start_clamps);
  RUN_TEST(test_slice_ignores_loop);
  return UNITY_END();
}
```

**Step 2: Запуск, ожидается падение**

Run: `pio test -e native -f test_sample_region`
Expected: FAIL: `fatal error: sample_region.h: No such file or directory`

**Step 3: Минимальная реализация**

`lib/core/src/sample_region.h`:

```cpp
#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

// Where a SAMPLE note plays: the region, the loop and the start position. Shared by the voice start
// (Synth::startSample) and the prefetch hint (Synth::prefetch), so both agree on the first frame.
struct SampleRegion {
  uint32_t from = 0, to = 1;        // played region [from, to)
  uint32_t loopLo = 0, loopHi = 1;  // loop (LoopMode::Off: the region)
  int64_t pos = 0;                  // start position, 32.32
  int8_t dir = 1;                   // -1 = reverse
  uint8_t loopMode = 0;             // LoopMode
};

// slice < 0 (or past sliceCount): the instrument's start / end / loop. ofs: OFS, /256 of the region.
SampleRegion sampleRegion(const Instrument& m, int slice, uint32_t len, uint8_t ofs);

}  // namespace mt
```

`lib/core/src/sample_region.cpp`: тело переносится из `startSample` без изменений, кроме имён:

```cpp
#include "sample_region.h"
#include "slices.h"

namespace mt {

SampleRegion sampleRegion(const Instrument& m, int slice, uint32_t len, uint8_t ofs) {
  SampleRegion g;
  uint32_t from, to, lf;
  // A slice plays [from, to) of its own once: the loop is ignored.
  const bool sliced = slice >= 0 && sliceRegion(m, slice, len, from, to);
  if (!sliced) {
    from = static_cast<uint32_t>(static_cast<uint64_t>(m.start) * len / 0xFFFF);
    to = static_cast<uint32_t>(static_cast<uint64_t>(m.end) * len / 0xFFFF);
    if (from >= len) from = len - 1;
    if (to > len) to = len;
    if (to <= from) to = from + 1;
  }
  const uint32_t span = to - from;
  if (sliced) {
    lf = from;
  } else {
    lf = from + static_cast<uint32_t>(static_cast<uint64_t>(m.loopStart) * span / 0xFFFF);
    if (lf >= to) lf = to - 1;
  }
  g.from = from;
  g.to = to;
  g.loopMode = sliced ? static_cast<uint8_t>(LoopMode::Off)
                      : (m.loop < static_cast<uint8_t>(LoopMode::Count) ? m.loop : 0);
  const uint32_t o = static_cast<uint32_t>(static_cast<uint64_t>(ofs) * span / 256);
  if (m.reverse) {
    // Reverse plays the region mirrored: the loop is [from, to - (lf - from)) walked downwards.
    g.dir = -1;
    g.loopLo = from;
    g.loopHi = to - (lf - from);
    g.pos = static_cast<int64_t>(to - 1 - o) << 32;
  } else {
    g.dir = 1;
    g.loopLo = lf;
    g.loopHi = to;
    g.pos = static_cast<int64_t>(from + o) << 32;
  }
  if (g.loopMode == static_cast<uint8_t>(LoopMode::Off)) {
    g.loopLo = from;
    g.loopHi = to;
  }
  return g;
}

}  // namespace mt
```

В `synth.cpp` добавить `#include "sample_region.h"` и заменить тело `startSample`:

```cpp
void Synth::startSample(Voice& v, const Instrument& m) const {
  const SampleRegion g = sampleRegion(m, v.slice, v.smpLen, v.ofs);
  v.from = g.from;
  v.to = g.to;
  v.loopMode = g.loopMode;
  v.dir = g.dir;
  v.loopLo = g.loopLo;
  v.loopHi = g.loopHi;
  v.pos = g.pos;
}
```

**Step 4: Повторный запуск**

Run: `pio test -e native -f test_sample_region` и `pio test -e native -f test_synth`
Expected: PASS, тесты синта не изменились.

### Task 2: Интерфейс `SampleSource::find(name, SampleData&)`

**Files:**
- Create: `lib/core/src/sample_source.h`
- Modify: `lib/core/src/synth.h:1-40` (старый `struct SampleSource` удалить, подключить `sample_source.h`)
- Modify: `lib/core/src/synth_voice.h` (поля `Voice` после `smpRate`)
- Modify: `lib/core/src/synth.cpp:470-580` (`noteOn`: поиск сэмпла, `restartSmp`, присваивания)
- Modify: `lib/core/src/bank_ops.h:44-52`, `lib/core/src/bank_ops.cpp:112-126`
- Test: `test/test_bank_ops/test_main.cpp:354-367`, `test/test_synth/test_main.cpp:508-519` (`FakeBank`), `:1256-1265` (`ArrayBank`)

**Step 1: Падающий тест**

В `test_bank_ops/test_main.cpp` заменить `test_sample_source`:

```cpp
void test_sample_source() {
  writePlainWav("/a.wav", ramp(2000, 5), 1, 32000);
  BankJob j(*bank, *disk);
  j.startImport("/a.wav", false, *proj);
  TEST_ASSERT_EQUAL(BankResult::Ok, run(j));
  projSampleSet(*proj, "HAT", j.imported().crc, j.imported().frames);
  BankSampleSource s(*bank, *proj);
  SampleData d;
  TEST_ASSERT_TRUE(s.find("hat", d));
  TEST_ASSERT_NOT_NULL(d.whole);
  TEST_ASSERT_EQUAL_UINT32(j.imported().crc, d.key);
  TEST_ASSERT_EQUAL_UINT32(2000, d.frames);
  TEST_ASSERT_EQUAL_UINT32(32000, d.rate);
  TEST_ASSERT_FALSE(d.streamable);
  TEST_ASSERT_EQUAL(0, d.nHeads);
  TEST_ASSERT_FALSE(s.find("NONE", d));
}
```

В `test_synth/test_main.cpp` фейки переходят на новый интерфейс:

```cpp
struct FakeBank : SampleSource {
  mutable char asked[kSampleNameMax + 1] = {0};
  int16_t data[1000] = {0};
  bool find(const char* name, SampleData& o) const override {
    strncpy(asked, name, kSampleNameMax);
    if (strcmp(name, "kick") != 0) return false;
    o = SampleData();
    o.key = 1;
    o.frames = 1000;
    o.rate = 16000;
    o.whole = data;
    return true;
  }
};
```

```cpp
struct ArrayBank : SampleSource {
  int16_t data[400] = {0};
  uint32_t n = 100, rate = kSynthRate;
  bool find(const char* name, SampleData& o) const override {
    if (strcmp(name, "s") != 0) return false;
    o = SampleData();
    o.key = 2;
    o.frames = n;
    o.rate = rate;
    o.whole = data;
    return true;
  }
};
```

**Step 2: Запуск, ожидается падение**

Run: `pio test -e native -f test_bank_ops`
Expected: FAIL (компиляция): `'SampleData' was not declared in this scope`

**Step 3: Минимальная реализация**

`lib/core/src/sample_source.h`:

```cpp
#pragma once
#include <stdint.h>

namespace mt {

constexpr int kMaxHeads = 40;  // heads per sample

// A run of a sample's frames kept in memory: frames [frame, frame + len) are d[0 .. len).
struct SampleHead {
  uint32_t frame = 0, len = 0;
  const int16_t* d = nullptr;
};

// What a voice needs to play a sample. Either whole (all frames in memory: flash bank, PSRAM, a test
// array) or streamable (frames load by key into a StreamPool slot; heads cover note starts).
struct SampleData {
  uint32_t key = 0;  // sample CRC: the pool's and the store's key
  uint32_t frames = 0, rate = 0;
  const int16_t* whole = nullptr;
  bool psram = false;  // whole points into PSRAM (dropped if it fails, see StreamPool::psramOk)
  const SampleHead* heads = nullptr;
  uint8_t nHeads = 0;
  bool streamable = false;
};

// Lookup by name for the synth (audio interrupt, note-on). Whatever it returns stays valid until the
// synth is parked.
struct SampleSource {
  virtual bool find(const char* name, SampleData& out) const = 0;
};

}  // namespace mt
```

В `synth.h` удалить старый `struct SampleSource {...};`, добавить `#include "sample_source.h"`.

`synth_voice.h`: после `smpRate` добавить (`smp` остаётся, это `SampleData::whole`):

```cpp
  // Streaming (smp == nullptr): frames come from the heads and a StreamPool slot.
  uint32_t srcKey = 0;                // SampleData::key
  const SampleHead* heads = nullptr;  // SampleData::heads
  uint8_t nHeads = 0;
  bool stream = false;                // SampleData::streamable, no smp
  bool psram = false;                 // smp points into PSRAM
  int8_t slot = -1;                   // StreamPool slot, -1 = none (retried every control update)
  bool under = false;                 // underrun: holding the last value
  float hold = 0;                     // last interpolated value
  float ug = 1;                       // underrun gain, 2 ms ramps
```

и `#include "sample_source.h"` в начало.

`synth.cpp`, `noteOn`, поиск (стр. 470–480) заменить на:

```cpp
  const bool sample = m.type == InstrType::Sample;
  SampleData sd;
  if (sample) {
    // The UI may be editing the name: copy it first.
    char name[kSampleNameMax + 1];
    for (int i = 0; i < kSampleNameMax; ++i) name[i] = m.sample[i];
    name[kSampleNameMax] = 0;
    if (!bank_ || !bank_->find(name, sd) || sd.frames == 0 || sd.rate == 0) return;
    if (!sd.whole && !sd.streamable && !sd.nHeads) return;
  }
```

Условие `restartSmp`:

```cpp
  const bool restartSmp = sample && (!overlap || !v.sample || v.smp != sd.whole || v.srcKey != sd.key ||
                                     v.slice != slice);
```

Присваивания (стр. 569–572):

```cpp
  v.smp = sd.whole;
  v.psram = sd.psram && sd.whole;
  v.srcKey = sd.key;
  v.heads = sd.heads;
  v.nHeads = sd.nHeads;
  v.stream = !sd.whole && sd.streamable;
  v.slice = static_cast<int8_t>(slice);
  v.smpLen = sd.frames;
  v.smpRate = sd.rate;
```

Остальные ссылки на локальные `smp` / `frames` / `rate` в `noteOn` переименовать в `sd.whole` / `sd.frames` / `sd.rate`. Список даст `grep -n "\bsmp\b\|\bframes\b\|\brate\b" lib/core/src/synth.cpp | sed -n '1,40p'`, смотреть в пределах 446–678.

Пока пул не подключён, голос без `smp` молчит: `renderVoice` (стр. 1103) вызывает `renderSample`, только если `v.smp` (остальное делает Task 5):

```cpp
    if (v.smp) renderSample(v, dst, n);
```

`bank_ops.h`:

```cpp
  bool find(const char* name, SampleData& out) const override;
```

`bank_ops.cpp:112`:

```cpp
bool BankSampleSource::find(const char* name, SampleData& out) const {
  // projSampleBank without snprintf.
  const int k = projSampleFind(m_, name);
  if (k < 0) return false;
  const ProjSample& ps = m_.samples[k];
  char key[kSampleNameMax + 1];
  hexKey(ps.crc, key);
  int i = b_.find(key);
  if (i >= 0 && b_.entry(i)->frames != ps.frames) i = -1;
  const int16_t* d = b_.data(i);
  if (!d) return false;
  out = SampleData();
  out.key = ps.crc;
  out.frames = b_.entry(i)->frames;
  out.rate = b_.entry(i)->rate;
  out.whole = d;
  return true;
}
```

**Step 4: Повторный запуск**

Run: `pio test -e native -f test_bank_ops` и `pio test -e native -f test_synth`
Expected: PASS. Сборка Teensy: `pio run -e teensy41` — `bank.cpp` использует `BankSampleSource` без изменений.

### Task 3: `StreamPool` — слоты, теги, владельцы

**Files:**
- Create: `lib/core/src/stream_pool.h`, `lib/core/src/stream_pool.cpp`
- Test: `test/test_stream_pool/test_main.cpp` (Create)

**Step 1: Падающий тест**

```cpp
#include <string.h>
#include <unity.h>
#include "stream_pool.h"

using namespace mt;

static int16_t mem[kStreamSlots * kSlotBlocks * kStreamBlock];
static StreamPool pool;
static int16_t data[kLoadMax * kStreamBlock];

static SampleRegion reg(uint32_t start) {
  SampleRegion g;
  g.from = g.loopLo = 0;
  g.to = g.loopHi = 100000;
  g.pos = static_cast<int64_t>(start) << 32;
  return g;
}
static int get(uint32_t key, SlotOwner o = SlotOwner::Voice, uint32_t start = 0) {
  return pool.acquire(key, 100000, 44100, nullptr, 0, reg(start), o);
}
// Loads block b of slot s into way 0, as the loader would.
static bool load(int s, uint32_t b) {
  StreamLoad l;
  l.slot = s;
  l.key = pool.slot(s).key;
  l.gen = pool.slot(s).gen;
  l.block = b;
  l.n = 1;
  l.way[0] = 0;
  for (int i = 0; i < kStreamBlock; ++i) data[i] = static_cast<int16_t>(b * 1000 + i);
  return pool.commit(l, data);
}

void setUp() {
  memset(mem, 0, sizeof mem);
  pool.init(mem);
}
void tearDown() {}

void test_acquire_gives_distinct_slots_until_full() {
  bool used[kStreamSlots] = {};
  for (int i = 0; i < kStreamSlots; ++i) {
    const int s = get(100 + i);
    TEST_ASSERT_TRUE(s >= 0 && s < kStreamSlots);
    TEST_ASSERT_FALSE(used[s]);
    used[s] = true;
  }
  TEST_ASSERT_EQUAL(-1, get(999));
}

void test_loaded_block_is_found() {
  const int s = get(7);
  TEST_ASSERT_NULL(pool.block(s, 3));
  TEST_ASSERT_TRUE(load(s, 3));
  const int16_t* d = pool.block(s, 3);
  TEST_ASSERT_NOT_NULL(d);
  TEST_ASSERT_EQUAL_INT16(3000, d[0]);
  TEST_ASSERT_EQUAL_INT16(3511, d[511]);
}

void test_released_slot_keeps_blocks_for_same_key() {
  const int s = get(7);
  TEST_ASSERT_TRUE(load(s, 0));
  pool.release(s);
  TEST_ASSERT_EQUAL(s, get(7));
  TEST_ASSERT_NOT_NULL(pool.block(s, 0));
}

void test_new_key_drops_blocks() {
  int s7 = -1;
  for (int i = 0; i < kStreamSlots; ++i) {
    const int s = get(1 + i);
    if (i == 0) s7 = s;
  }
  TEST_ASSERT_TRUE(load(s7, 0));
  pool.release(s7);
  TEST_ASSERT_EQUAL(s7, get(99));  // the only free slot
  TEST_ASSERT_NULL(pool.block(s7, 0));
}

void test_prefetch_slot_is_adopted() {
  const int s = get(5, SlotOwner::Prefetch);
  TEST_ASSERT_TRUE(load(s, 0));
  TEST_ASSERT_EQUAL(s, get(5));
  TEST_ASSERT_EQUAL(SlotOwner::Voice, pool.slot(s).owner);
  TEST_ASSERT_NOT_NULL(pool.block(s, 0));
}

void test_prefetch_evicted_when_full() {
  for (int i = 0; i < kStreamSlots - 1; ++i) get(1 + i);
  const int p = get(500, SlotOwner::Prefetch);
  TEST_ASSERT_TRUE(p >= 0);
  TEST_ASSERT_EQUAL(p, get(600));
  TEST_ASSERT_EQUAL_UINT32(600, pool.slot(p).key);
}

void test_preview_never_evicted() {
  for (int i = 0; i < kStreamSlots - 1; ++i) get(1 + i);
  TEST_ASSERT_TRUE(get(kPreviewKey, SlotOwner::Preview) >= 0);
  TEST_ASSERT_EQUAL(-1, get(600));
}

void test_commit_after_rekey_is_dropped() {
  int s0 = -1;
  for (int i = 0; i < kStreamSlots; ++i) {
    const int s = get(1 + i);
    if (i == 0) s0 = s;
  }
  StreamLoad l;
  l.slot = s0;
  l.key = pool.slot(s0).key;
  l.gen = pool.slot(s0).gen;
  l.block = 0;
  l.n = 1;
  l.way[0] = 0;
  pool.release(s0);
  TEST_ASSERT_EQUAL(s0, get(77));  // re-keyed while the card was read
  TEST_ASSERT_FALSE(pool.commit(l, data));
  TEST_ASSERT_NULL(pool.block(s0, 0));
}

void test_snapshot_round_trip() {
  const int s = get(9);
  pool.publish(s, int64_t(1234) << 32, -(int64_t(3) << 31));  // 1.5 frames per sample, backwards
  PlaySnap p;
  TEST_ASSERT_TRUE(pool.snap(s, p));
  TEST_ASSERT_EQUAL_UINT32(1234, p.pos);
  TEST_ASSERT_EQUAL_INT32(-98304, p.step16);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_acquire_gives_distinct_slots_until_full);
  RUN_TEST(test_loaded_block_is_found);
  RUN_TEST(test_released_slot_keeps_blocks_for_same_key);
  RUN_TEST(test_new_key_drops_blocks);
  RUN_TEST(test_prefetch_slot_is_adopted);
  RUN_TEST(test_prefetch_evicted_when_full);
  RUN_TEST(test_preview_never_evicted);
  RUN_TEST(test_commit_after_rekey_is_dropped);
  RUN_TEST(test_snapshot_round_trip);
  return UNITY_END();
}
```

**Step 2: Запуск, ожидается падение**

Run: `pio test -e native -f test_stream_pool`
Expected: FAIL: `fatal error: stream_pool.h: No such file or directory`

**Step 3: Минимальная реализация**

`lib/core/src/stream_pool.h`:

```cpp
#pragma once
#include <stdint.h>
#include "sample_region.h"
#include "sample_source.h"

namespace mt {

// Slots of streamed sample data, shared by the audio interrupt (voices: acquire / block / publish /
// release) and the loader in loop() (plan / commit). A slot holds kSlotBlocks blocks of kStreamBlock
// frames of one sample (key), each way tagged (gen << 24) | (block + 1). A new key bumps gen, so a
// load planned for the old key never matches: the loader checks key / gen again when it commits.
// The interrupt may preempt the loader, not the reverse; acquire / release from loop() (prefetch,
// preview) run with the audio interrupt masked.
#ifndef MT_STREAM_SLOTS
#define MT_STREAM_SLOTS 16
#endif
constexpr int kStreamSlots = MT_STREAM_SLOTS;
constexpr int kStreamBlock = 512;  // frames (1 KB, 2 card sectors)
constexpr int kSlotBlocks = 6;
constexpr int kLoadMax = 4;  // blocks per card read
constexpr uint32_t kPreviewKey = 0xFFFFFFFFu;  // PreviewFile's ring, not a sample

enum class SlotOwner : uint8_t { Free, Voice, Prefetch, Preview };

// Where a slot's consumer plays, published by the interrupt for the loader.
struct PlaySnap {
  uint32_t pos = 0;   // frame
  int32_t step16 = 0; // frames per output sample, 16.16, < 0 backwards
  uint32_t loopLo = 0, loopHi = 1, from = 0, to = 1;
  uint8_t loopMode = 0;
};

struct StreamSlot {
  volatile uint32_t key = 0;
  volatile uint8_t gen = 0;
  volatile SlotOwner owner = SlotOwner::Free;
  volatile uint32_t tag[kSlotBlocks] = {};  // 0 = empty
  uint32_t frames = 0, rate = 0;
  uint32_t age = 0;       // acquire / release order
  uint32_t stamp = 0;     // Prefetch: loop() time (ms) it was asked
  const SampleHead* heads = nullptr;
  uint8_t nHeads = 0;
  volatile uint32_t seq = 0;  // snapshot seqlock (odd: being written)
  PlaySnap play;
  int16_t* buf = nullptr;  // kSlotBlocks * kStreamBlock frames
};

// A planned load: blocks [block, block + n) of key into ways way[0..n).
struct StreamLoad {
  int slot = -1;
  uint32_t key = 0;
  uint8_t gen = 0;
  uint32_t block = 0;
  int n = 0;
  int way[kLoadMax] = {};
};

// Card access for loads: n blocks from block on (frames past the end: anything).
struct BlockReader {
  virtual bool read(uint32_t key, uint32_t block, int n, int16_t* out) = 0;
};

class StreamPool {
 public:
  // mem: slots * kSlotBlocks * kStreamBlock frames; slots <= kStreamSlots.
  void init(int16_t* mem, int slots = kStreamSlots);
  int slots() const { return n_; }

  // Takes a slot for key starting at g: a Prefetch slot of key, a free slot of key (its blocks
  // stay), the longest-free slot, the oldest Prefetch slot. -1 if none (all Voice / Preview).
  int acquire(uint32_t key, uint32_t frames, uint32_t rate, const SampleHead* heads, uint8_t nHeads,
              const SampleRegion& g, SlotOwner o);
  void release(int s);
  // Block b of slot s, nullptr if not loaded.
  const int16_t* block(int s, uint32_t b) const;
  // The consumer's position and signed step (32.32) after it rendered.
  void publish(int s, int64_t pos, int64_t step);
  bool snap(int s, PlaySnap& out) const;  // false: the slot is free
  const StreamSlot& slot(int s) const { return slot_[s]; }
  StreamSlot& slotRef(int s) { return slot_[s]; }

  // Loader: copies l.n blocks from data into l's ways and tags them, if the slot still holds l.key at
  // l.gen. False (nothing tagged) otherwise.
  bool commit(const StreamLoad& l, const int16_t* data);

  BlockReader* sync = nullptr;     // offline render: a miss loads at once (see Synth::frameRun)
  volatile uint32_t underruns = 0; // voices that ran out of data, counted once per underrun
  volatile bool psramOk = false;   // PSRAM copies usable; cleared for good on a failure

 private:
  StreamSlot slot_[kStreamSlots];
  int n_ = 0;
  uint32_t age_ = 0;
  void take(int s, uint32_t key, uint32_t frames, uint32_t rate, const SampleHead* heads, uint8_t nHeads,
            const SampleRegion& g, SlotOwner o);
};

}  // namespace mt
```

`lib/core/src/stream_pool.cpp`:

```cpp
#include "stream_pool.h"
#include <atomic>
#include <string.h>

namespace mt {

namespace {
inline void fence() { std::atomic_signal_fence(std::memory_order_seq_cst); }
inline uint32_t tagOf(uint8_t gen, uint32_t b) { return (static_cast<uint32_t>(gen) << 24) | (b + 1); }
}  // namespace

void StreamPool::init(int16_t* mem, int slots) {
  n_ = slots < kStreamSlots ? slots : kStreamSlots;
  for (int s = 0; s < kStreamSlots; ++s) {
    slot_[s] = StreamSlot();
    slot_[s].buf = s < n_ ? mem + static_cast<uint32_t>(s) * kSlotBlocks * kStreamBlock : nullptr;
  }
  age_ = 0;
  underruns = 0;
}

void StreamPool::take(int s, uint32_t key, uint32_t frames, uint32_t rate, const SampleHead* heads,
                      uint8_t nHeads, const SampleRegion& g, SlotOwner o) {
  StreamSlot& x = slot_[s];
  if (x.key != key) {
    x.gen = static_cast<uint8_t>(x.gen + 1);
    for (auto& t : x.tag) t = 0;
    fence();
    x.key = key;
  }
  x.frames = frames;
  x.rate = rate;
  x.heads = heads;
  x.nHeads = nHeads;
  x.age = ++age_;
  x.play.from = g.from;
  x.play.to = g.to;
  x.play.loopLo = g.loopLo;
  x.play.loopHi = g.loopHi;
  x.play.loopMode = g.loopMode;
  const int64_t step = (static_cast<int64_t>(rate) << 32) / 44100;  // nominal pitch until published
  fence();
  x.owner = o;
  publish(s, g.pos, g.dir < 0 ? -step : step);
}

int StreamPool::acquire(uint32_t key, uint32_t frames, uint32_t rate, const SampleHead* heads, uint8_t nHeads,
                        const SampleRegion& g, SlotOwner o) {
  int same = -1, freeOld = -1, pre = -1;
  for (int s = 0; s < n_; ++s) {
    const StreamSlot& x = slot_[s];
    if (x.owner == SlotOwner::Prefetch && x.key == key && o == SlotOwner::Voice) {
      slot_[s].owner = SlotOwner::Voice;  // adopt: the loads it made stay
      slot_[s].age = ++age_;
      publish(s, g.pos, (g.dir < 0 ? -1 : 1) * ((static_cast<int64_t>(rate) << 32) / 44100));
      return s;
    }
    if (x.owner == SlotOwner::Free) {
      if (x.key == key) same = s;
      if (freeOld < 0 || x.age < slot_[freeOld].age) freeOld = s;
    } else if (x.owner == SlotOwner::Prefetch && (pre < 0 || x.age < slot_[pre].age)) {
      pre = s;
    }
  }
  const int s = same >= 0 ? same : (freeOld >= 0 ? freeOld : (o != SlotOwner::Prefetch ? pre : -1));
  if (s < 0) return -1;
  take(s, key, frames, rate, heads, nHeads, g, o);
  return s;
}

void StreamPool::release(int s) {
  if (s < 0 || s >= n_) return;
  slot_[s].owner = SlotOwner::Free;
  slot_[s].age = ++age_;
}

const int16_t* StreamPool::block(int s, uint32_t b) const {
  const StreamSlot& x = slot_[s];
  const uint32_t t = tagOf(x.gen, b);
  for (int w = 0; w < kSlotBlocks; ++w)
    if (x.tag[w] == t) return x.buf + w * kStreamBlock;
  return nullptr;
}

void StreamPool::publish(int s, int64_t pos, int64_t step) {
  StreamSlot& x = slot_[s];
  x.seq = x.seq + 1;
  fence();
  x.play.pos = static_cast<uint32_t>(pos >> 32);
  x.play.step16 = static_cast<int32_t>(step >> 16);
  fence();
  x.seq = x.seq + 1;
}

bool StreamPool::snap(int s, PlaySnap& out) const {
  const StreamSlot& x = slot_[s];
  for (;;) {
    const uint32_t a = x.seq;
    fence();
    out = x.play;
    fence();
    if (!(a & 1) && a == x.seq) break;
  }
  return x.owner != SlotOwner::Free;
}

bool StreamPool::commit(const StreamLoad& l, const int16_t* data) {
  StreamSlot& x = slot_[l.slot];
  if (x.key != l.key || x.gen != l.gen || x.owner == SlotOwner::Free) return false;
  for (int i = 0; i < l.n; ++i) {
    x.tag[l.way[i]] = 0;  // the interrupt never reads a way being filled
    fence();
    memcpy(x.buf + l.way[i] * kStreamBlock, data + i * kStreamBlock, kStreamBlock * sizeof(int16_t));
    fence();
    if (x.key != l.key || x.gen != l.gen) return false;  // re-keyed meanwhile: leave it empty
    x.tag[l.way[i]] = tagOf(l.gen, l.block + i);
  }
  return true;
}

}  // namespace mt
```

**Step 4: Повторный запуск**

Run: `pio test -e native -f test_stream_pool`
Expected: PASS (9 tests).

### Task 4: Проход по пути воспроизведения и план загрузки

**Files:**
- Create: `lib/core/src/stream_loader.h`, `lib/core/src/stream_loader.cpp`
- Test: `test/test_stream_loader/test_main.cpp` (Create)

**Step 1: Падающий тест**

```cpp
#include <string.h>
#include <unity.h>
#include <vector>
#include "stream_loader.h"

using namespace mt;

static int16_t mem[kStreamSlots * kSlotBlocks * kStreamBlock];
static StreamPool pool;

struct VecReader : BlockReader {
  std::vector<int16_t> d;
  int reads = 0;
  bool read(uint32_t, uint32_t b, int n, int16_t* out) override {
    ++reads;
    for (int i = 0; i < n * kStreamBlock; ++i) {
      const uint32_t f = b * kStreamBlock + i;
      out[i] = f < d.size() ? d[f] : 0;
    }
    return true;
  }
};
static VecReader rd;

static PlaySnap snap(uint32_t pos, int32_t step16, uint32_t lo, uint32_t hi, LoopMode mode) {
  PlaySnap p;
  p.pos = pos;
  p.step16 = step16;
  p.from = p.loopLo = lo;
  p.to = p.loopHi = hi;
  p.loopMode = static_cast<uint8_t>(mode);
  return p;
}
static void expectWant(const PlaySnap& p, std::vector<uint32_t> want) {
  uint32_t got[kAhead + 1];
  const int n = streamWant(p, got);
  TEST_ASSERT_EQUAL(static_cast<int>(want.size()), n);
  for (int i = 0; i < n; ++i) TEST_ASSERT_EQUAL_UINT32(want[i], got[i]);
}
static int voice(uint32_t frames, uint32_t start) {
  SampleRegion g;
  g.from = g.loopLo = 0;
  g.to = g.loopHi = frames;
  g.pos = static_cast<int64_t>(start) << 32;
  return pool.acquire(1, frames, 44100, nullptr, 0, g, SlotOwner::Voice);
}

void setUp() {
  pool.init(mem);
  rd.d.assign(100000, 0);
  for (uint32_t i = 0; i < rd.d.size(); ++i) rd.d[i] = static_cast<int16_t>(i);
  rd.reads = 0;
}
void tearDown() {}

void test_want_forward() { expectWant(snap(1000, 65536, 0, 100000, LoopMode::Off), {1, 2, 3, 4, 5}); }
void test_want_stops_at_end_without_loop() { expectWant(snap(1000, 65536, 0, 1500, LoopMode::Off), {1, 2}); }
void test_want_reverse() { expectWant(snap(3000, -65536, 0, 100000, LoopMode::Off), {5, 4, 3, 2, 1}); }
void test_want_forward_loop_wraps() { expectWant(snap(1900, 65536, 0, 2048, LoopMode::Forward), {3, 0, 1, 2}); }
void test_want_pingpong_turns() { expectWant(snap(3000, 65536, 0, 3072, LoopMode::PingPong), {5, 4, 3, 2, 1}); }

void test_plan_reads_consecutive_missing_blocks() {
  const int s = voice(100000, 0);
  StreamLoad l;
  TEST_ASSERT_TRUE(streamPlan(pool, s, l));
  TEST_ASSERT_EQUAL_UINT32(0, l.block);
  TEST_ASSERT_EQUAL(kLoadMax, l.n);
  TEST_ASSERT_TRUE(streamLoad(pool, s, rd));
  TEST_ASSERT_TRUE(streamPlan(pool, s, l));
  TEST_ASSERT_EQUAL_UINT32(4, l.block);
  TEST_ASSERT_EQUAL(1, l.n);
  TEST_ASSERT_TRUE(streamLoad(pool, s, rd));
  TEST_ASSERT_FALSE(streamPlan(pool, s, l));  // all wanted blocks in
  TEST_ASSERT_NULL(pool.block(s, 5));  // not wanted yet
}

void test_plan_evicts_blocks_behind() {
  const int s = voice(100000, 0);
  while (streamLoad(pool, s, rd)) {
  }
  pool.publish(s, int64_t(2600) << 32, int64_t(1) << 32);  // block 5
  StreamLoad l;
  TEST_ASSERT_TRUE(streamPlan(pool, s, l));
  TEST_ASSERT_EQUAL_UINT32(5, l.block);
  TEST_ASSERT_EQUAL(kLoadMax, l.n);
  TEST_ASSERT_TRUE(streamLoad(pool, s, rd));
  TEST_ASSERT_NULL(pool.block(s, 0));  // behind: evicted (any unwanted way may go, not only the farthest)
  const int16_t* d = pool.block(s, 7);
  TEST_ASSERT_NOT_NULL(d);
  TEST_ASSERT_EQUAL_INT16(7 * 512 + 3, d[3]);
}

void test_reserve_counts_loaded_frames_ahead() {
  const int s = voice(100000, 0);
  TEST_ASSERT_EQUAL_UINT32(0, streamReserveUs(pool, s));
  streamLoad(pool, s, rd);  // blocks 0..3: 2048 frames at 1.0 = 46.44 ms
  TEST_ASSERT_UINT32_WITHIN(50, 46440, streamReserveUs(pool, s));
  pool.publish(s, 0, int64_t(2) << 32);  // an octave up: half the time
  TEST_ASSERT_UINT32_WITHIN(50, 23220, streamReserveUs(pool, s));
}

void test_heads_count_as_loaded() {
  static int16_t h[1024];
  static SampleHead head;
  head.frame = 0;
  head.len = 1024;
  head.d = h;
  SampleRegion g;
  g.to = g.loopHi = 100000;
  const int s = pool.acquire(1, 100000, 44100, &head, 1, g, SlotOwner::Voice);
  StreamLoad l;
  TEST_ASSERT_TRUE(streamPlan(pool, s, l));
  TEST_ASSERT_EQUAL_UINT32(2, l.block);  // blocks 0, 1 are in the head
  TEST_ASSERT_UINT32_WITHIN(50, 23220, streamReserveUs(pool, s));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_want_forward);
  RUN_TEST(test_want_stops_at_end_without_loop);
  RUN_TEST(test_want_reverse);
  RUN_TEST(test_want_forward_loop_wraps);
  RUN_TEST(test_want_pingpong_turns);
  RUN_TEST(test_plan_reads_consecutive_missing_blocks);
  RUN_TEST(test_plan_evicts_blocks_behind);
  RUN_TEST(test_reserve_counts_loaded_frames_ahead);
  RUN_TEST(test_heads_count_as_loaded);
  return UNITY_END();
}
```

**Step 2: Запуск, ожидается падение**

Run: `pio test -e native -f test_stream_loader`
Expected: FAIL: `fatal error: stream_loader.h: No such file or directory`

**Step 3: Минимальная реализация**

`lib/core/src/stream_loader.h`:

```cpp
#pragma once
#include <stdint.h>
#include "stream_pool.h"

namespace mt {

constexpr int kAhead = 4;  // blocks wanted past the current one

// Blocks a consumer at p needs, in play order: the current one and kAhead more along its path
// (loops wrap, ping-pong turns, LoopMode::Off stops at the edge). Returns the count (1..kAhead + 1).
int streamWant(const PlaySnap& p, uint32_t want[kAhead + 1]);
// Time until slot s runs out of data: frames in memory (blocks, heads) ahead along the path / step,
// in us at 44.1 kHz. UINT32_MAX for a free slot or a slot with nothing left to load.
uint32_t streamReserveUs(const StreamPool& pool, int s);
// The next load for slot s: the first wanted block that is missing and up to kLoadMax - 1 more
// missing ones right after it, into ways no wanted block uses. False if nothing is missing.
bool streamPlan(const StreamPool& pool, int s, StreamLoad& out);
// streamPlan + read + commit. False: nothing to load, or the read failed.
bool streamLoad(StreamPool& pool, int s, BlockReader& r);

}  // namespace mt
```

`lib/core/src/stream_loader.cpp`:

```cpp
#include "stream_loader.h"

namespace mt {

namespace {

constexpr uint32_t kOutRate = 44100;
int16_t stage_[kLoadMax * kStreamBlock];  // one load's read

bool inHead(const StreamSlot& x, uint32_t b) {
  const uint32_t lo = b * kStreamBlock;
  uint32_t hi = lo + kStreamBlock;
  if (hi > x.frames) hi = x.frames;
  for (int h = 0; h < x.nHeads; ++h)
    if (x.heads[h].frame <= lo && x.heads[h].frame + x.heads[h].len >= hi) return true;
  return false;
}
bool have(const StreamPool& pool, int s, uint32_t b) {
  return pool.block(s, b) || inHead(pool.slot(s), b);
}

}  // namespace

int streamWant(const PlaySnap& p, uint32_t want[kAhead + 1]) {
  int n = 0;
  auto add = [&](uint32_t b) {
    for (int i = 0; i < n; ++i)
      if (want[i] == b) return;
    want[n++] = b;
  };
  const uint32_t lo = p.loopLo, hi = p.loopHi > p.loopLo ? p.loopHi : p.loopLo + 1;
  const uint8_t mode = p.loopMode;
  int dir = p.step16 < 0 ? -1 : 1;
  uint32_t f = p.pos;
  add(f / kStreamBlock);
  for (int guard = 0; guard < 2 * (kAhead + 1) + 4 && n < kAhead + 1; ++guard) {
    const uint32_t b = f / kStreamBlock;
    if (dir > 0) {
      const uint32_t next = (b + 1) * kStreamBlock;
      if (next >= hi) {  // the edge is in this block
        if (mode == static_cast<uint8_t>(LoopMode::Forward)) {
          f = lo;
        } else if (mode == static_cast<uint8_t>(LoopMode::PingPong)) {
          dir = -1;
          if (b * kStreamBlock <= lo) break;
          f = b * kStreamBlock - 1;
        } else {
          break;
        }
      } else {
        f = next;
      }
    } else {
      const uint32_t start = b * kStreamBlock;
      if (start <= lo) {
        if (mode == static_cast<uint8_t>(LoopMode::Forward)) {
          f = hi - 1;
        } else if (mode == static_cast<uint8_t>(LoopMode::PingPong)) {
          dir = 1;
          if ((b + 1) * kStreamBlock >= hi) break;
          f = (b + 1) * kStreamBlock;
        } else {
          break;
        }
      } else {
        f = start - 1;
      }
    }
    add(f / kStreamBlock);
  }
  return n;
}

uint32_t streamReserveUs(const StreamPool& pool, int s) {
  PlaySnap p;
  if (!pool.snap(s, p)) return UINT32_MAX;
  uint32_t want[kAhead + 1];
  const int n = streamWant(p, want);
  uint64_t frames = 0;
  int i = 0;
  for (; i < n && have(pool, s, want[i]); ++i) {
    if (i == 0) {  // the current block: only what is left of it in the play direction
      const uint32_t off = p.pos % kStreamBlock;
      frames += p.step16 < 0 ? off + 1 : kStreamBlock - off;
    } else {
      frames += kStreamBlock;
    }
  }
  if (i == n) return UINT32_MAX;  // everything it can want is in
  const uint32_t step = static_cast<uint32_t>(p.step16 < 0 ? -p.step16 : p.step16);
  if (!step) return UINT32_MAX;
  return static_cast<uint32_t>((frames << 16) / step * 1000000 / kOutRate);
}

bool streamPlan(const StreamPool& pool, int s, StreamLoad& out) {
  PlaySnap p;
  if (!pool.snap(s, p)) return false;
  const StreamSlot& x = pool.slot(s);
  uint32_t want[kAhead + 1];
  const int n = streamWant(p, want);
  int i = 0;
  while (i < n && have(pool, s, want[i])) ++i;
  if (i == n) return false;
  // A run of missing blocks, ascending on the card whichever way it plays.
  uint32_t lo = want[i], hi = want[i];
  for (int j = i + 1; j < n && hi - lo + 1 < kLoadMax; ++j) {
    if (have(pool, s, want[j])) break;
    if (want[j] == hi + 1) {
      hi = want[j];
    } else if (want[j] + 1 == lo) {
      lo = want[j];
    } else {
      break;
    }
  }
  out.slot = s;
  out.key = x.key;
  out.gen = x.gen;
  out.block = lo;
  out.n = static_cast<int>(hi - lo + 1);
  // Ways: empty ones first, then those holding blocks no longer wanted.
  int k = 0;
  for (int pass = 0; pass < 2 && k < out.n; ++pass)
    for (int w = 0; w < kSlotBlocks && k < out.n; ++w) {
      const uint32_t t = x.tag[w];
      bool free = t == 0 || (t >> 24) != x.gen;
      if (!free && pass == 1) {
        free = true;
        for (int j = 0; j < n; ++j)
          if ((t & 0xFFFFFF) == want[j] + 1) free = false;
      }
      bool taken = false;
      for (int q = 0; q < k; ++q) taken |= out.way[q] == w;
      if (free && !taken) out.way[k++] = w;
    }
  out.n = k;
  return k > 0;
}

bool streamLoad(StreamPool& pool, int s, BlockReader& r) {
  StreamLoad l;
  if (!streamPlan(pool, s, l)) return false;
  if (!r.read(l.key, l.block, l.n, stage_)) return false;
  pool.commit(l, stage_);
  return true;
}

}  // namespace mt
```

**Step 4: Повторный запуск**

Run: `pio test -e native -f test_stream_loader`
Expected: PASS (9 tests).

### Task 5: Стрим-путь голоса в `Synth` и синхронная загрузка для рендера

**Files:**
- Modify: `lib/core/src/synth.h` (`setPool`, `pool_`, `renderStream`, `frameRun`, `dropSlot`, `startSample` больше не `const`)
- Modify: `lib/core/src/synth.cpp`: `reset()` (стр. 53), `startSample`, `control` (ветка SAMPLE около стр. 848), `renderVoice` (стр. 1103), освобождение голоса (стр. 1251, 1303)
- Test: `test/test_sample_stream/test_main.cpp` (Create)

**Step 1: Падающий тест**

```cpp
#include <string.h>
#include <unity.h>
#include <vector>
#include "stream_loader.h"
#include "synth.h"

using namespace mt;

static Project* p;
static Synth* s;
static int16_t mem[kStreamSlots * kSlotBlocks * kStreamBlock];
static StreamPool pool;

// One sample "s": whole (RAM) or streamable, optionally with a head.
struct Src : SampleSource {
  std::vector<int16_t> d;
  bool stream = false, psram = false;
  SampleHead head;
  uint8_t nHeads = 0;
  bool find(const char* name, SampleData& o) const override {
    if (strcmp(name, "s") != 0) return false;
    o = SampleData();
    o.key = 0x1234;
    o.frames = static_cast<uint32_t>(d.size());
    o.rate = 44100;
    o.heads = nHeads ? &head : nullptr;
    o.nHeads = nHeads;
    if (stream) {
      o.streamable = true;
    } else {
      o.whole = d.data();
      o.psram = psram;
      o.streamable = psram;
    }
    return true;
  }
};
struct VecReader : BlockReader {
  const std::vector<int16_t>* d = nullptr;
  bool read(uint32_t, uint32_t b, int n, int16_t* out) override {
    for (int i = 0; i < n * kStreamBlock; ++i) {
      const uint32_t f = b * kStreamBlock + i;
      out[i] = f < d->size() ? (*d)[f] : 0;
    }
    return true;
  }
};
static Src src;
static VecReader rd;

void setUp() {
  p = new Project();
  p->masterVol = 100;
  for (int t = 0; t < kTracks; ++t) {
    p->tracks[t].out = TrackOut::Int;
    p->tracks[t].vol = 127;
  }
  Instrument& m = p->instruments[0];
  instrSetType(m, InstrType::Sample);
  strcpy(m.sample, "s");
  m.root = 60;
  m.vol = 127;
  m.attack = 0;
  m.decay = 0;
  m.sustain = 127;
  m.release = 0;
  src = Src();
  src.d.resize(30000);
  uint32_t x = 1;
  for (auto& v : src.d) {  // noise: any lost or repeated frame shows
    x = x * 1664525u + 1013904223u;
    v = static_cast<int16_t>(x >> 18);
  }
  rd.d = &src.d;
  pool.init(mem);
  pool.sync = nullptr;
  pool.psramOk = true;
  s = new Synth(*p);
  s->setBank(&src);
  s->setPool(&pool);
}
void tearDown() {
  delete s;
  delete p;
}

static std::vector<int16_t> play(uint8_t note, int blocks) {
  const uint8_t on[3] = {0x90, note, 127};
  TEST_ASSERT_TRUE(s->event(0, 0, on, 3));
  std::vector<int16_t> out;
  int16_t l[Synth::kBlock], r[Synth::kBlock];
  for (int k = 0; k < blocks; ++k) {
    s->render(l, r);
    out.insert(out.end(), l, l + Synth::kBlock);
  }
  return out;
}
static std::vector<int16_t> ram(uint8_t note, int blocks) {
  src.stream = false;
  s->reset();
  return play(note, blocks);
}
static std::vector<int16_t> streamed(uint8_t note, int blocks) {
  src.stream = true;
  pool.sync = &rd;
  s->reset();
  return play(note, blocks);
}

void test_stream_matches_ram_forward_loop() {
  p->instruments[0].loop = static_cast<uint8_t>(LoopMode::Forward);
  p->instruments[0].loopStart = 0x4000;
  const auto a = ram(72, 600);
  const auto b = streamed(72, 600);
  TEST_ASSERT_EQUAL_INT16_ARRAY(a.data(), b.data(), a.size());
  TEST_ASSERT_EQUAL_UINT32(0, pool.underruns);
}

void test_stream_matches_ram_reverse_pingpong() {
  p->instruments[0].loop = static_cast<uint8_t>(LoopMode::PingPong);
  p->instruments[0].loopStart = 0x2000;
  p->instruments[0].reverse = true;
  const auto a = ram(55, 600);
  const auto b = streamed(55, 600);
  TEST_ASSERT_EQUAL_INT16_ARRAY(a.data(), b.data(), a.size());
}

void test_underrun_fades_and_counts() {
  const auto a = ram(60, 60);
  src.stream = true;
  src.head.frame = 0;
  src.head.len = 4410;
  src.head.d = src.d.data();
  src.nHeads = 1;
  pool.sync = nullptr;  // nothing ever loads
  s->reset();
  const auto b = play(60, 60);
  // The head plays like RAM (one frame short: interpolation needs the next one).
  TEST_ASSERT_EQUAL_INT16_ARRAY(a.data(), b.data(), 4400);
  // Then it fades out within 2 ms and stays silent; the voice keeps going.
  for (size_t i = 4410 + 90; i < b.size(); ++i) TEST_ASSERT_EQUAL_INT16(0, b[i]);
  TEST_ASSERT_EQUAL_UINT32(1, pool.underruns);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
}

void test_voice_frees_its_slot() {
  src.d.resize(3000);  // ends after 3000 frames, LoopMode::Off
  streamed(60, 60);
  for (int i = 0; i < pool.slots(); ++i) TEST_ASSERT_EQUAL(SlotOwner::Free, pool.slot(i).owner);
}

void test_failed_psram_falls_back_to_stream() {
  p->instruments[0].loop = static_cast<uint8_t>(LoopMode::Forward);
  const auto a = ram(64, 300);
  src.stream = false;
  src.psram = true;
  pool.sync = &rd;
  s->reset();
  const uint8_t on[3] = {0x90, 64, 127};
  TEST_ASSERT_TRUE(s->event(0, 0, on, 3));
  std::vector<int16_t> b;
  int16_t l[Synth::kBlock], r[Synth::kBlock];
  for (int k = 0; k < 300; ++k) {
    if (k == 100) pool.psramOk = false;
    s->render(l, r);
    b.insert(b.end(), l, l + Synth::kBlock);
  }
  TEST_ASSERT_EQUAL_INT16_ARRAY(a.data(), b.data(), a.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_stream_matches_ram_forward_loop);
  RUN_TEST(test_stream_matches_ram_reverse_pingpong);
  RUN_TEST(test_underrun_fades_and_counts);
  RUN_TEST(test_voice_frees_its_slot);
  RUN_TEST(test_failed_psram_falls_back_to_stream);
  return UNITY_END();
}
```

**Step 2: Запуск, ожидается падение**

Run: `pio test -e native -f test_sample_stream`
Expected: FAIL (компиляция): `'class mt::Synth' has no member named 'setPool'`

**Step 3: Минимальная реализация**

`synth.h`: `#include "stream_pool.h"`. В public:

```cpp
  // Streamed samples (SampleData without whole) play from this pool; nullptr: such samples are silent.
  void setPool(StreamPool* p) { pool_ = p; }
```

В private (рядом с `renderSample`):

```cpp
  struct FrameRun {
    const int16_t* d;   // frame i is d[i - lo]
    uint32_t lo, hi;
  };
  bool frameRun(Voice& v, uint32_t idx, FrameRun& r);
  void renderStream(Voice& v, float* out, int n);
  void startSlot(Voice& v, const SampleRegion& g);
  void dropSlot(Voice& v);
  StreamPool* pool_ = nullptr;
```

`startSample` теряет `const` (в `.h` и `.cpp`): ему нужен `pool_->acquire`.

`synth.cpp`, константа рядом с `kChokeMs`:

```cpp
constexpr float kUnderStep = 1.f / 88;  // underrun fade: 2 ms at 44.1 kHz
```

`startSample` (из Task 1) в конце:

```cpp
  if (v.stream) startSlot(v, g);
```

Новые функции:

```cpp
void Synth::startSlot(Voice& v, const SampleRegion& g) {
  v.under = false;
  v.ug = 1;
  v.hold = 0;
  if (!pool_) return;
  if (v.slot >= 0) {
    // The same sample again (legato restart, retrigger): keep the slot and its blocks.
    if (pool_->slot(v.slot).key == v.srcKey) {
      pool_->publish(v.slot, g.pos, g.dir < 0 ? -1 : 1);
      return;
    }
    dropSlot(v);
  }
  v.slot = static_cast<int8_t>(
      pool_->acquire(v.srcKey, v.smpLen, v.smpRate, v.heads, v.nHeads, g, SlotOwner::Voice));
  if (v.slot >= 0) return;
  // No slot: the oldest streaming voice fades out (kStealMs) and frees one; this voice plays its
  // head meanwhile and retries in control().
  Voice* old = nullptr;
  for (auto& x : voices_)
    if (x.on && &x != &v && x.slot >= 0 && !x.stolen && (!old || x.age < old->age)) old = &x;
  if (old) {
    old->env.fade(kStealMs);
    old->stolen = true;
  }
}

void Synth::dropSlot(Voice& v) {
  if (v.slot >= 0 && pool_) pool_->release(v.slot);
  v.slot = -1;
}
```

`noteOn`: перед присваиваниями из Task 2 голос отдаёт чужой слот:

```cpp
  if (v.slot >= 0 && (!sample || v.srcKey != sd.key || !(!sd.whole && sd.streamable))) dropSlot(v);
```

`reset()` (стр. 53), перед `v = Voice()`:

```cpp
    dropSlot(v);
```

Освобождение голоса (стр. 1251 и 1303):

```cpp
        if (v.env.idle() && !(v.fltOn && v.flt.ringing())) {
          v.on = false;
          dropSlot(v);
        }
```

`control`, в начале ветки SAMPLE (перед расчётом `inc`, стр. ~848):

```cpp
    if (v.psram && pool_ && !pool_->psramOk) {  // PSRAM failed: the rest streams
      v.smp = nullptr;
      v.psram = false;
      v.stream = true;
      SampleRegion g;
      g.from = v.from;
      g.to = v.to;
      g.loopLo = v.loopLo;
      g.loopHi = v.loopHi;
      g.loopMode = v.loopMode;
      g.pos = v.pos;
      g.dir = v.dir;
      startSlot(v, g);
    } else if (v.stream && v.slot < 0 && pool_) {
      SampleRegion g;
      g.from = v.from;
      g.to = v.to;
      g.loopLo = v.loopLo;
      g.loopHi = v.loopHi;
      g.loopMode = v.loopMode;
      g.pos = v.pos;
      g.dir = v.dir;
      v.slot = static_cast<int8_t>(
          pool_->acquire(v.srcKey, v.smpLen, v.smpRate, v.heads, v.nHeads, g, SlotOwner::Voice));
    }
```

(повторяющийся кусок вынести в `static SampleRegion regionOf(const Voice& v)`.)

`renderVoice` (стр. 1103):

```cpp
    if (v.smp) {
      renderSample(v, dst, n);
    } else if ((v.stream || v.nHeads) && pool_) {
      renderStream(v, dst, n);
    }
```

Поиск кадра и цикл:

```cpp
bool Synth::frameRun(Voice& v, uint32_t idx, FrameRun& r) {
  for (int h = 0; h < v.nHeads; ++h) {
    const SampleHead& x = v.heads[h];
    if (idx >= x.frame && idx - x.frame < x.len) {
      r = {x.d, x.frame, x.frame + x.len};
      return true;
    }
  }
  if (v.slot < 0 || !pool_) return false;
  const uint32_t b = idx / kStreamBlock;
  const int16_t* d = pool_->block(v.slot, b);
  if (!d && pool_->sync) {  // offline render: load it now, planned from this frame
    pool_->publish(v.slot, static_cast<int64_t>(idx) << 32, v.dir < 0 ? -v.step : v.step);
    if (streamLoad(*pool_, v.slot, *pool_->sync)) d = pool_->block(v.slot, b);
  }
  if (!d) return false;
  r.d = d;
  r.lo = b * kStreamBlock;
  r.hi = r.lo + kStreamBlock < v.smpLen ? r.lo + kStreamBlock : v.smpLen;
  return true;
}

// renderSample with frames from the heads / the pool. A missing frame holds the last value and fades
// it out in 2 ms (position goes on); data back fades in. With data all along it is renderSample to the
// bit: s * ug with ug == 1 is s.
MT_HOT void Synth::renderStream(Voice& v, float* out, int n) {
  const uint32_t last = v.smpLen - 1;
  const int64_t lo = static_cast<int64_t>(v.loopLo) << 32, hi = static_cast<int64_t>(v.loopHi) << 32;
  const int64_t one = int64_t(1) << 32;
  const float amp = v.amp * (kSampleGain / 32768.f);
  const uint8_t mode = v.loopMode;
  int64_t pos = v.pos;
  int64_t step = v.dir > 0 ? v.step : -v.step;
  Env env = v.env;
  float ug = v.ug, hold = v.hold;
  bool under = v.under;
  FrameRun run{nullptr, 1, 0};
  for (int i = 0; i < n; ++i) {
    const uint32_t idx = static_cast<uint32_t>(pos >> 32);
    const uint32_t nx = idx < last ? idx + 1 : last;
    bool ok = (idx >= run.lo && idx < run.hi) || frameRun(v, idx, run);
    int32_t a = 0, b = 0;
    if (ok) {
      a = run.d[idx - run.lo];
      if (nx >= run.lo && nx < run.hi) {
        b = run.d[nx - run.lo];
      } else {
        FrameRun r2;
        ok = frameRun(v, nx, r2);
        if (ok) b = r2.d[nx - r2.lo];
      }
    }
    float s;
    if (ok) {
      s = a + (b - a) * (static_cast<uint32_t>(pos) * 2.3283064e-10f);
      hold = s;
      under = false;
      if (ug < 1.f) ug = ug + kUnderStep < 1.f ? ug + kUnderStep : 1.f;
    } else {
      if (!under) {
        under = true;
        pool_->underruns = pool_->underruns + 1;
      }
      s = hold;
      if (ug > 0.f) ug = ug - kUnderStep > 0.f ? ug - kUnderStep : 0.f;
    }
    out[i] += s * ug * env.next() * amp;
    pos += step;
    if (step >= 0 ? pos < hi : pos >= lo) continue;
    if (mode == static_cast<uint8_t>(LoopMode::Forward)) {
      const int64_t span = hi - lo;
      if (step >= 0) {
        if (pos >= hi) pos = lo + (pos - lo) % span;
      } else {
        if (pos < lo) pos = hi - 1 - (hi - 1 - pos) % span;
      }
    } else if (mode == static_cast<uint8_t>(LoopMode::PingPong)) {
      pos = step >= 0 ? 2 * (hi - one) - pos : 2 * lo - pos;
      step = -step;
      if (pos < lo) pos = lo;
      if (pos >= hi) pos = hi - 1;
    } else {
      env.kill();
      break;
    }
  }
  v.env = env;
  v.pos = pos;
  v.dir = step >= 0 ? 1 : -1;
  v.ug = ug;
  v.hold = hold;
  v.under = under;
  if (v.slot >= 0) pool_->publish(v.slot, pos, step);
}
```

Без пула (`pool_ == nullptr`) стрим-голос молчит: недогрузка считается в `pool_`, поэтому ветка требует его.

`synth.cpp`: `#include "stream_loader.h"`.

**Step 4: Повторный запуск**

Run: `pio test -e native -f test_sample_stream`, затем `pio test -e native -f test_synth`
Expected: PASS (5 tests). Тесты синта без изменений.

### Task 6: Планировщик `schedulePass`

**Files:**
- Create: `lib/core/src/stream_sched.h`, `lib/core/src/stream_sched.cpp`
- Test: `test/test_stream_sched/test_main.cpp` (Create)

**Step 1: Падающий тест**

```cpp
#include <string>
#include <unity.h>
#include "stream_sched.h"

using namespace mt;

static std::string order;

struct C : StreamConsumer {
  char name;
  uint32_t res;
  uint32_t full = 200000;
  C(char n, uint32_t r) : name(n), res(r) {}
  bool needs(uint32_t& r) override {
    r = res;
    return res < full;
  }
  void load() override {
    order += name;
    res += 50000;
  }
};
struct B : BgWork {
  char name;
  bool work;
  B(char n, bool w) : name(n), work(w) {}
  bool step() override {
    if (work) order += name;
    return work;
  }
};

void setUp() { order.clear(); }
void tearDown() {}

void test_lowest_reserve_first_and_at_most_four_loads() {
  C a('a', 10000), b('b', 5000), c('c', 20000);
  StreamConsumer* cs[] = {&a, &b, &c};
  int next = 0;
  TEST_ASSERT_EQUAL(kPassLoads, schedulePass(cs, 3, nullptr, 0, next));
  TEST_ASSERT_EQUAL_STRING("bacb", order.c_str());
}

void test_background_only_when_every_reserve_is_large() {
  C a('a', 10000);
  B x('x', true);
  StreamConsumer* cs[] = {&a};
  BgWork* bg[] = {&x};
  int next = 0;
  schedulePass(cs, 1, bg, 1, next);  // a: 10 -> 60 ms ... after loads > 30 ms
  TEST_ASSERT_EQUAL_STRING("aaaax", order.c_str());
  order.clear();
  a.res = 1000;
  a.full = 2000;  // one load leaves it at 51 ms but it still "needs": reserve counts
  schedulePass(cs, 1, bg, 1, next);
  TEST_ASSERT_EQUAL_STRING("ax", order.c_str());
}

void test_background_waits_below_30_ms() {
  B x('x', true);
  BgWork* bg[] = {&x};
  int next = 0;
  struct Stuck : StreamConsumer {
    bool needs(uint32_t& r) override {
      r = 20000;
      return true;
    }
    void load() override {}
  } st;
  StreamConsumer* cs2[] = {&st};
  schedulePass(cs2, 1, bg, 1, next);
  TEST_ASSERT_EQUAL_STRING("", order.c_str());
}

void test_background_round_robin_skips_idle() {
  B x('x', true), y('y', false), z('z', true);
  BgWork* bg[] = {&x, &y, &z};
  int next = 0;
  for (int i = 0; i < 4; ++i) schedulePass(nullptr, 0, bg, 3, next);
  TEST_ASSERT_EQUAL_STRING("xzxz", order.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_lowest_reserve_first_and_at_most_four_loads);
  RUN_TEST(test_background_only_when_every_reserve_is_large);
  RUN_TEST(test_background_waits_below_30_ms);
  RUN_TEST(test_background_round_robin_skips_idle);
  return UNITY_END();
}
```

**Step 2: Запуск, ожидается падение**

Run: `pio test -e native -f test_stream_sched`
Expected: FAIL: `fatal error: stream_sched.h: No such file or directory`

**Step 3: Минимальная реализация**

`lib/core/src/stream_sched.h`:

```cpp
#pragma once
#include <stdint.h>

namespace mt {

constexpr int kPassLoads = 4;             // card loads per loop() pass
constexpr uint32_t kBgReserveUs = 30000;  // background work only when every consumer has more

// Something that must get card data on time (a voice slot, the preview ring).
struct StreamConsumer {
  // Time until it runs dry (us); false if it has nothing to load (r still says its reserve).
  virtual bool needs(uint32_t& reserveUs) = 0;
  virtual void load() = 0;  // one load (<= kLoadMax blocks, <= 4 KB)
};
// Work that may wait: one bounded portion per step(); false if it had nothing to do.
struct BgWork {
  virtual bool step() = 0;
};

// One loop() pass: up to kPassLoads loads, each for the consumer with the least reserve; then, if
// every reserve is above kBgReserveUs, one portion of background work, round robin from bgNext
// (idle ones are skipped). Returns the loads done.
int schedulePass(StreamConsumer* const* c, int n, BgWork* const* bg, int nBg, int& bgNext);

}  // namespace mt
```

`lib/core/src/stream_sched.cpp`:

```cpp
#include "stream_sched.h"

namespace mt {

int schedulePass(StreamConsumer* const* c, int n, BgWork* const* bg, int nBg, int& bgNext) {
  int loads = 0;
  uint32_t minRes = UINT32_MAX;
  for (; loads < kPassLoads; ++loads) {
    int best = -1;
    uint32_t bestRes = UINT32_MAX;
    minRes = UINT32_MAX;
    for (int i = 0; i < n; ++i) {
      uint32_t r = UINT32_MAX;
      const bool need = c[i]->needs(r);
      if (r < minRes) minRes = r;
      if (need && (best < 0 || r < bestRes)) {
        best = i;
        bestRes = r;
      }
    }
    if (best < 0) break;
    c[best]->load();
  }
  if (loads == kPassLoads) {  // reserves after the last load
    minRes = UINT32_MAX;
    for (int i = 0; i < n; ++i) {
      uint32_t r = UINT32_MAX;
      c[i]->needs(r);
      if (r < minRes) minRes = r;
    }
  }
  if (minRes <= kBgReserveUs || nBg <= 0) return loads;
  for (int k = 0; k < nBg; ++k) {
    const int i = (bgNext + k) % nBg;
    if (bg[i]->step()) {
      bgNext = (i + 1) % nBg;
      break;
    }
  }
  return loads;
}

}  // namespace mt
```

> В `test_background_only_when_every_reserve_is_large` первая половина: 10 → 60 → 110 → 160 → 210 мс за четыре загрузки (после четвёртой `needs` уже false), минимум 210 > 30 → `x`. Вторая половина: `a.full = 2000` — после одной загрузки `res = 51000`, `needs` false, проход заканчивается на второй итерации без загрузки; минимум 51 мс > 30 → `x`. `test_background_waits_below_30_ms`: потребитель с запасом 20 мс «нуждается» всегда, четыре пустые загрузки, минимум 20 мс ≤ 30 → без фоновой работы.

**Step 4: Повторный запуск**

Run: `pio test -e native -f test_stream_sched`
Expected: PASS (4 tests).

### Проверка этапа 1

Run: `pio run -e wt32 -e teensy41 -e teensy41-desk -e teensy41-bench`
Expected: `SUCCESS` по всем четырём.

Run: `pio test -e native`
Expected: все тесты PASS. Добавились `test_sample_region` (5), `test_stream_pool` (9), `test_stream_loader` (9), `test_sample_stream` (5), `test_stream_sched` (4).

Desk (голый Teensy, прошивка `teensy41-desk`): `LD hello`, `LD pnote 0 60 1000`. Звук и `notes` как раньше: голос берёт целый сэмпл из flash через `SampleData::whole`.

---

## Этап 2 — пул на Teensy, планировщик в `loop()`, превью на слоте

После этапа на Teensy работают пул и планировщик, превью играет со слота. Сэмплы из flash звучат как раньше. Файл, лежащий в `/mt-store` и прописанный в модели, стримится.

### Task 7: `StreamRing` на внешнем буфере

**Files:**
- Modify: `lib/core/src/stream_ring.h`, `lib/core/src/stream_ring.cpp:33,48`
- Test: `test/test_stream_ring/test_main.cpp` (`setUp`, новые тесты)

**Step 1: Падающий тест**

В начало файла теста, после `static StreamRing ring;`:

```cpp
static int16_t ringBuf[8192];
```

`setUp`:

```cpp
void setUp() {
  ring.stop();
  ring.setBuffer(ringBuf, 8192);
  ring.reset();
}
```

Везде, где тест использует `StreamRing::kLen`, заменить на `ring.len()`. Новые тесты:

```cpp
void test_ring_external_buffer_wraps() {
  static int16_t small[3072];
  ring.stop();
  ring.setBuffer(small, 3072);
  ring.reset();
  TEST_ASSERT_EQUAL_UINT32(3072, ring.space());
  int16_t blk[128];
  int next = 0;
  ring.start();
  for (int round = 0; round < 60; ++round) {  // 60 x 128 frames: the ring wraps twice
    for (int i = 0; i < 128; ++i) in[i] = static_cast<int16_t>(round * 128 + i);
    ring.write(in, 128);
    for (auto& v : blk) v = 0;
    ring.mix(blk, 128, 1.f);
    for (int i = 0; i < 128; ++i, ++next) TEST_ASSERT_EQUAL_INT16(next, blk[i]);
  }
  TEST_ASSERT_EQUAL_UINT32(0, ring.takeUnderruns());
}

void test_ring_without_buffer_is_silent() {
  ring.stop();
  ring.setBuffer(nullptr, 0);
  ring.reset();
  TEST_ASSERT_EQUAL_UINT32(0, ring.space());
  ring.start();
  int16_t blk[16] = {7};
  ring.mix(blk, 16, 1.f);
  TEST_ASSERT_EQUAL_INT16(7, blk[0]);
}
```

и `RUN_TEST` для обоих.

**Step 2: Запуск, ожидается падение**

Run: `pio test -e native -f test_stream_ring`
Expected: FAIL (компиляция): `'class mt::StreamRing' has no member named 'setBuffer'`

**Step 3: Минимальная реализация**

`stream_ring.h`: убрать `kLen` и массив, добавить:

```cpp
  // The ring's memory (a pool slot on the synth board); only while stopped. nullptr: plays nothing.
  void setBuffer(int16_t* buf, uint32_t len) {
    buf_ = buf;
    len_ = buf ? len : 0;
  }
  uint32_t len() const { return len_; }

  uint32_t space() const { return len_ - avail(); }
```

```cpp
 private:
  int16_t* buf_ = nullptr;
  uint32_t len_ = 0;
```

`stream_ring.cpp`: `buf_[w % kLen]` → `buf_[w % len_]`, `buf_[r % kLen]` → `buf_[r % len_]`. В начале `mix`, после проверки `active_`:

```cpp
  if (!len_) return;
```

Счётчики остаются свободно бегущими `uint32_t`: при `len_`, не равной степени двойки, `% len_` верен, пока `w_ - r_ <= len_` (это гарантирует `write(n <= space())`). Переполнение `uint32_t` наступит через 27 ч непрерывного превью; `reset()` в начале каждого превью обнуляет счётчики.

**Step 4: Повторный запуск**

Run: `pio test -e native -f test_stream_ring`
Expected: PASS.

Сразу после этого `src/teensy/preview_stream.cpp` перестаёт собираться (кольцо без буфера молчит). Это исправляет Task 9, а до него в `preview::begin` временно: `static int16_t tmp[8192]; ring.setBuffer(tmp, 8192);`.

### Task 8: Хранилище: заголовок, путь, таблица, источник

**Files:**
- Create: `lib/core/src/sample_store.h`, `lib/core/src/sample_store.cpp`
- Test: `test/test_sample_store/test_main.cpp` (Create)

**Step 1: Падающий тест**

```cpp
#include <string.h>
#include <unity.h>
#include <vector>
#include "sample_set.h"
#include "sample_store.h"
#include "wav.h"

using namespace mt;

struct VecSource : ByteSource {
  const std::vector<uint8_t>& buf;
  size_t pos = 0;
  explicit VecSource(const std::vector<uint8_t>& b) : buf(b) {}
  bool read(void* d, size_t n) override {
    if (pos + n > buf.size()) return false;
    memcpy(d, buf.data() + pos, n);
    pos += n;
    return true;
  }
  bool skip(size_t n) override {
    if (pos + n > buf.size()) return false;
    pos += n;
    return true;
  }
};

static StoreTable* table;
static StreamPool* pool;
static Project* model;  // projSampleSet takes a Project

void setUp() {
  table = new StoreTable();
  pool = new StreamPool();
  model = new Project();
}
void tearDown() {
  delete model;
  delete pool;
  delete table;
}

void test_path_is_lower_hex() {
  char p[kStorePathMax];
  storePath(0x00ABCDEFu, p);
  TEST_ASSERT_EQUAL_STRING("/mt-store/00abcdef.wav", p);
  storePeaksPath(0x00ABCDEFu, p);
  TEST_ASSERT_EQUAL_STRING("/mt-store/00abcdef.pk", p);
}

void test_header_puts_data_at_512_and_parses_back() {
  std::vector<uint8_t> f(kStoreDataOffset);
  storeHeader(f.data(), 1000, 22050, 48, 0xABCD1234u);
  f.resize(kStoreDataOffset + 2000, 0);
  VecSource src(f);
  WavInfo w;
  TEST_ASSERT_EQUAL(WavErr::Ok, wavParse(src, w));
  TEST_ASSERT_EQUAL_UINT32(kStoreDataOffset, w.dataOffset);
  TEST_ASSERT_EQUAL_UINT32(1000, w.frames());
  TEST_ASSERT_EQUAL_UINT32(22050, w.rate);
  TEST_ASSERT_EQUAL(1, w.channels);
  TEST_ASSERT_EQUAL(16, w.bits);
  TEST_ASSERT_TRUE(w.hasCrc);
  TEST_ASSERT_EQUAL_HEX32(0xABCD1234u, w.crc);
  TEST_ASSERT_EQUAL(48, w.root);
  uint32_t riff;
  memcpy(&riff, f.data() + 4, 4);
  TEST_ASSERT_EQUAL_UINT32(f.size() - 8, riff);
}

void test_table_add_find_and_full() {
  TEST_ASSERT_EQUAL(-1, table->find(5));
  const int i = table->add(5, 100, 44100, 1234);
  TEST_ASSERT_TRUE(i >= 0);
  TEST_ASSERT_EQUAL(i, table->find(5));
  TEST_ASSERT_EQUAL(i, table->add(5, 100, 44100, 1234));  // already there
  for (uint32_t k = 6; table->count() < kStoreEntries; ++k) TEST_ASSERT_TRUE(table->add(k, 1, 1, 1) >= 0);
  TEST_ASSERT_EQUAL(-1, table->add(0xFFFF0000u, 1, 1, 1));
  table->clear();
  TEST_ASSERT_EQUAL(0, table->count());
  TEST_ASSERT_EQUAL(-1, table->find(5));
}

void test_table_keeps_heads() {
  static int16_t d[100];
  SampleHead h[2];
  h[0].frame = 0;
  h[0].len = 50;
  h[0].d = d;
  h[1].frame = 500;
  h[1].len = 50;
  h[1].d = d + 50;
  const int i = table->add(9, 1000, 44100, 77);
  TEST_ASSERT_TRUE(table->setHeads(i, h, 2));
  TEST_ASSERT_EQUAL(2, table->at(i).nHeads);
  TEST_ASSERT_EQUAL_UINT32(500, table->heads(i)[1].frame);
  TEST_ASSERT_TRUE(table->heads(i)[1].d == d + 50);
}

void test_source_streams_and_prefers_psram() {
  projSampleSet(*model, "PAD", 0x1111u, 4000);
  const int i = table->add(0x1111u, 4000, 32000, 900);
  StoreSampleSource src(*table, *pool, nullptr, *model);
  SampleData d;
  TEST_ASSERT_TRUE(src.find("pad", d));
  TEST_ASSERT_EQUAL_HEX32(0x1111u, d.key);
  TEST_ASSERT_TRUE(d.streamable);
  TEST_ASSERT_NULL(d.whole);
  TEST_ASSERT_EQUAL_UINT32(32000, d.rate);
  static int16_t copy[4000];
  table->setPsram(i, copy);
  pool->psramOk = true;
  TEST_ASSERT_TRUE(src.find("pad", d));
  TEST_ASSERT_TRUE(d.whole == copy);
  TEST_ASSERT_TRUE(d.psram);
  pool->psramOk = false;
  TEST_ASSERT_TRUE(src.find("pad", d));
  TEST_ASSERT_NULL(d.whole);
}

void test_source_skips_stale_or_scattered_entries() {
  projSampleSet(*model, "PAD", 0x2222u, 4000);
  table->add(0x2222u, 3999, 32000, 900);  // other length: another file under this crc
  StoreSampleSource src(*table, *pool, nullptr, *model);
  SampleData d;
  TEST_ASSERT_FALSE(src.find("pad", d));
  table->clear();
  table->add(0x2222u, 4000, 32000, 0);  // not contiguous: cannot stream
  TEST_ASSERT_FALSE(src.find("pad", d));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_path_is_lower_hex);
  RUN_TEST(test_header_puts_data_at_512_and_parses_back);
  RUN_TEST(test_table_add_find_and_full);
  RUN_TEST(test_table_keeps_heads);
  RUN_TEST(test_source_streams_and_prefers_psram);
  RUN_TEST(test_source_skips_stale_or_scattered_entries);
  return UNITY_END();
}
```

**Step 2: Запуск, ожидается падение**

Run: `pio test -e native -f test_sample_store`
Expected: FAIL: `fatal error: sample_store.h: No such file or directory`

**Step 3: Минимальная реализация**

`lib/core/src/sample_store.h`:

```cpp
#pragma once
#include <stdint.h>
#include "model.h"
#include "sample_source.h"
#include "stream_pool.h"

namespace mt {

// The sample store on the synth board's card: /mt-store/<crc>.wav, mono 16-bit <= kWavMaxRate, data
// from byte kStoreDataOffset (sector 1 of a contiguous file), plus /mt-store/<crc>.pk (peak overview).
// Keyed by the data CRC, so it serves every project; the project folders keep their own copies.
constexpr uint32_t kStoreDataOffset = 512;
constexpr int kStorePathMax = 28;
constexpr int kStoreEntries = 256;
constexpr int kHeadPool = 1024;
extern const char kStoreDir[];  // "/mt-store"

void storePath(uint32_t crc, char out[kStorePathMax]);       // /mt-store/0123abcd.wav
void storePeaksPath(uint32_t crc, char out[kStorePathMax]);  // /mt-store/0123abcd.pk
// RIFF + fmt + smpl + mtcr + JUNK (pad) + data header: kStoreDataOffset bytes.
void storeHeader(uint8_t out[kStoreDataOffset], uint32_t frames, uint32_t rate, uint8_t root, uint32_t crc);

// What the synth board knows of the store files. Append only (the audio interrupt reads it
// unlocked: an entry is filled first, then its crc, then the count); clear() only with the synth parked.
struct StoreEntry {
  volatile uint32_t crc = 0;  // 0 = empty
  uint32_t frames = 0, rate = 0;
  uint32_t sector = 0;        // first card sector of the file (data from sector + 1); 0 = not contiguous
  uint16_t head0 = 0;         // first of nHeads in the head pool
  uint8_t nHeads = 0;
  const int16_t* volatile psram = nullptr;  // verified whole copy, nullptr = none
};

class StoreTable {
 public:
  int find(uint32_t crc) const;  // -1 if absent
  // Index of crc (added if absent); -1 if the table is full.
  int add(uint32_t crc, uint32_t frames, uint32_t rate, uint32_t sector);
  // Heads of entry i (once per entry); false if the head pool is full.
  bool setHeads(int i, const SampleHead* h, int n);
  const SampleHead* heads(int i) const { return head_ + e_[i].head0; }
  void setPsram(int i, const int16_t* d) { e_[i].psram = d; }
  const StoreEntry& at(int i) const { return e_[i]; }
  int count() const { return n_; }
  void clear();

 private:
  StoreEntry e_[kStoreEntries];
  volatile int n_ = 0;
  SampleHead head_[kHeadPool];
  int nHead_ = 0;
};

// The synth's samples: name -> the model's list -> the store entry of its crc (PSRAM copy while
// pool.psramOk, else streamed with its heads). A sample not in the store comes from legacy (the
// flash bank of before the store; nullptr = none).
class StoreSampleSource final : public SampleSource {
 public:
  StoreSampleSource(const StoreTable& t, const StreamPool& p, const SampleSource* legacy, const SynthModel& m)
      : t_(t), p_(p), legacy_(legacy), m_(m) {}
  bool find(const char* name, SampleData& out) const override;

 private:
  const StoreTable& t_;
  const StreamPool& p_;
  const SampleSource* legacy_;
  const SynthModel& m_;
};

}  // namespace mt
```

`lib/core/src/sample_store.cpp`:

```cpp
#include "sample_store.h"
#include <atomic>
#include <string.h>
#include "sample_set.h"
#include "wav.h"

namespace mt {

const char kStoreDir[] = "/mt-store";

namespace {

inline void fence() { std::atomic_signal_fence(std::memory_order_seq_cst); }
void le32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}
void path(uint32_t crc, const char* ext, char* out) {
  static const char hex[] = "0123456789abcdef";
  strcpy(out, kStoreDir);
  char* p = out + strlen(out);
  *p++ = '/';
  for (int i = 7; i >= 0; --i) *p++ = hex[(crc >> (i * 4)) & 15];
  strcpy(p, ext);
}

}  // namespace

void storePath(uint32_t crc, char out[kStorePathMax]) { path(crc, ".wav", out); }
void storePeaksPath(uint32_t crc, char out[kStorePathMax]) { path(crc, ".pk", out); }

void storeHeader(uint8_t out[kStoreDataOffset], uint32_t frames, uint32_t rate, uint8_t root, uint32_t crc) {
  uint8_t h[kWavHeaderBytes];
  wavHeader(h, frames, rate, root, crc, 1);
  constexpr uint32_t pre = kWavHeaderBytes - 8;  // wavHeader ends with the "data" chunk header
  constexpr uint32_t junk = kStoreDataOffset - pre - 8 - 8;
  static_assert(junk % 2 == 0, "chunks are word aligned");
  memcpy(out, h, pre);
  memcpy(out + pre, "JUNK", 4);
  le32(out + pre + 4, junk);
  memset(out + pre + 8, 0, junk);
  memcpy(out + kStoreDataOffset - 8, h + pre, 8);
  le32(out + 4, kStoreDataOffset - 8 + frames * 2);
}

int StoreTable::find(uint32_t crc) const {
  const int n = n_;
  for (int i = 0; i < n; ++i)
    if (e_[i].crc == crc) return i;
  return -1;
}

int StoreTable::add(uint32_t crc, uint32_t frames, uint32_t rate, uint32_t sector) {
  const int have = find(crc);
  if (have >= 0) return have;
  if (n_ >= kStoreEntries || !crc) return -1;
  StoreEntry& e = e_[n_];
  e.frames = frames;
  e.rate = rate;
  e.sector = sector;
  e.head0 = 0;
  e.nHeads = 0;
  e.psram = nullptr;
  fence();
  e.crc = crc;
  fence();
  n_ = n_ + 1;
  return n_ - 1;
}

bool StoreTable::setHeads(int i, const SampleHead* h, int n) {
  if (i < 0 || i >= n_ || e_[i].nHeads || n > kMaxHeads || nHead_ + n > kHeadPool) return false;
  for (int k = 0; k < n; ++k) head_[nHead_ + k] = h[k];
  e_[i].head0 = static_cast<uint16_t>(nHead_);
  nHead_ += n;
  fence();
  e_[i].nHeads = static_cast<uint8_t>(n);
  return true;
}

void StoreTable::clear() {
  n_ = 0;
  nHead_ = 0;
  for (auto& e : e_) e = StoreEntry();
}

bool StoreSampleSource::find(const char* name, SampleData& out) const {
  const int k = projSampleFind(m_, name);
  if (k < 0) return false;
  const ProjSample& ps = m_.samples[k];
  const int i = t_.find(ps.crc);
  if (i >= 0 && t_.at(i).frames == ps.frames) {
    const StoreEntry& e = t_.at(i);
    out = SampleData();
    out.key = ps.crc;
    out.frames = e.frames;
    out.rate = e.rate;
    out.heads = e.nHeads ? t_.heads(i) : nullptr;
    out.nHeads = e.nHeads;
    out.streamable = e.sector != 0;
    const int16_t* ps2 = e.psram;
    if (ps2 && p_.psramOk) {
      out.whole = ps2;
      out.psram = true;
    }
    if (out.whole || out.streamable) return true;
  }
  return legacy_ && legacy_->find(name, out);
}

}  // namespace mt
```

**Step 4: Повторный запуск**

Run: `pio test -e native -f test_sample_store`
Expected: PASS (6 tests).

### Task 9: Teensy: модуль `stream`, планировщик в `loop()`, превью на слоте

**Files:**
- Create: `src/teensy/stream.h`, `src/teensy/stream.cpp`
- Modify: `src/teensy/main.cpp:91-126` (`setup` / `loop`)
- Modify: `src/teensy/preview_stream.cpp` (кольцо на слоте пула, потребитель)
- Modify: `src/teensy/preview_stream.h` (`consumer()`)
- Modify: `src/teensy/bank.cpp:290-300` (источник синта — `StoreSampleSource`, legacy — `BankSampleSource`), `:406-420` (`step` → `bool bgStep()`), обработчик `BankSync` (пересканировать хранилище в конце)
- Modify: `src/teensy/bank.h`
- Modify: `src/teensy/render_server.cpp:151,84` (на время рендера `stream::pool().sync`)

Нативного теста нет: всё здесь — склейка с железом. Проверка — desk.

**Step 1: `stream.h`**

```cpp
#pragma once
#include <stdint.h>
#include "sample_store.h"
#include "stream_pool.h"
#include "stream_sched.h"

namespace mt {
class Synth;
}

// Sample streaming on the synth board: the slot pool (RAM2), the store table, card reads of raw sectors
// and the loop() scheduler (voice / preview loads first, background work when every reserve allows).
namespace stream {

void begin(mt::Synth& s);
// One loop() pass: schedulePass over the slots and the preview, then one background portion.
void step();
// Background work, run round robin when every consumer has > 30 ms (bank jobs, tools, PSRAM fill).
void addBackground(mt::BgWork* w);
// Re-reads the store entries of the model's samples (contiguous files only). The synth is parked.
void rescan(const mt::SynthModel& m);
// Registers one store file now (an import just wrote it). The synth may be running.
bool add(uint32_t crc);

mt::StreamPool& pool();
mt::StoreTable& table();
mt::BlockReader& reader();  // raw card reads by key (also the offline render's sync reader)

struct Stats {
  uint8_t voices = 0;          // slots owned by voices now
  uint16_t underruns = 0;      // since the last take
  uint16_t minReserveMs = 0;   // lowest voice reserve seen since the last take (0xFFFF = none)
};
Stats takeStats();

}  // namespace stream
```

**Step 2: `stream.cpp`**

```cpp
#include "stream.h"
#include <Arduino.h>
#include <SD.h>
#include "fs_server.h"
#include "link_server.h"
#include "sample_set.h"
#include "stream_loader.h"
#include "synth.h"
#include "wav.h"

namespace stream {

namespace {

// 96 KB in RAM2 (OCRAM, cached): SdFat SDIO runs in FIFO mode, the CPU copies, no DMA coherence.
DMAMEM int16_t poolMem[mt::kStreamSlots * mt::kSlotBlocks * mt::kStreamBlock] __attribute__((aligned(32)));
mt::StreamPool thePool;
mt::StoreTable theTable;

class SdReader final : public mt::BlockReader {
 public:
  bool read(uint32_t key, uint32_t block, int n, int16_t* out) override {
    const int i = theTable.find(key);
    if (i < 0 || !card::ready()) return false;
    const uint32_t s = theTable.at(i).sector;
    if (!s) return false;
    return SD.sdfs.card()->readSectors(s + 1 + 2 * block, reinterpret_cast<uint8_t*>(out), 2 * n);
  }
};
SdReader rd;

class SlotConsumer final : public mt::StreamConsumer {
 public:
  int s = 0;
  bool needs(uint32_t& r) override {
    const mt::StreamSlot& x = thePool.slot(s);
    if (x.owner != mt::SlotOwner::Voice && x.owner != mt::SlotOwner::Prefetch) {
      r = UINT32_MAX;
      return false;
    }
    r = mt::streamReserveUs(thePool, s);
    mt::StreamLoad l;
    return mt::streamPlan(thePool, s, l);
  }
  void load() override { mt::streamLoad(thePool, s, rd); }
};
SlotConsumer slots[mt::kStreamSlots];
mt::StreamConsumer* consumers[mt::kStreamSlots + 1];
int nConsumers = 0;

constexpr int kBgMax = 6;
mt::BgWork* bg[kBgMax];
int nBg = 0, bgNext = 0;

uint32_t underrunsSeen = 0;
uint32_t minReserve = UINT32_MAX;

// Sector of a store file's first byte if it is one contiguous run, else 0.
uint32_t firstSector(FsFile& f) {
  uint32_t bgn = 0, end = 0;
  if (!f.contiguousRange(&bgn, &end)) return 0;
  return bgn;
}

}  // namespace

void begin(mt::Synth& s) {
  thePool.init(poolMem);
  for (int i = 0; i < mt::kStreamSlots; ++i) {
    slots[i].s = i;
    consumers[nConsumers++] = &slots[i];
  }
  consumers[nConsumers++] = preview::consumer();
  s.setPool(&thePool);
}

void addBackground(mt::BgWork* w) {
  if (nBg < kBgMax) bg[nBg++] = w;
}

void step() {
  mt::schedulePass(consumers, nConsumers, bg, nBg, bgNext);
  for (int i = 0; i < mt::kStreamSlots; ++i)
    if (thePool.slot(i).owner == mt::SlotOwner::Voice) {
      const uint32_t r = mt::streamReserveUs(thePool, i);
      if (r < minReserve) minReserve = r;
    }
}

bool add(uint32_t crc) {
  char path[mt::kStorePathMax];
  mt::storePath(crc, path);
  FsFile f = SD.sdfs.open(path, O_RDONLY);
  if (!f) return false;
  struct Src final : mt::ByteSource {
    FsFile* f;
    bool read(void* d, size_t n) override { return f->read(d, n) == static_cast<int>(n); }
    bool skip(size_t n) override { return f->seekCur(static_cast<int64_t>(n)); }
  } src;
  src.f = &f;
  mt::WavInfo w;
  const bool ok = mt::wavParse(src, w) == mt::WavErr::Ok && w.hasCrc && w.crc == crc && w.channels == 1 &&
                  w.bits == 16 && w.dataOffset == mt::kStoreDataOffset;
  const uint32_t sector = ok ? firstSector(f) : 0;
  f.close();
  if (!ok) return false;
  if (!sector) link::log("store: %08lx is not contiguous", static_cast<unsigned long>(crc));
  return theTable.add(crc, w.frames(), w.rate, sector) >= 0;
}

void rescan(const mt::SynthModel& m) {
  theTable.clear();
  int n = 0;
  for (int i = 0; i < m.sampleCount; ++i) n += add(m.samples[i].crc);
  link::log("store: %d of %d samples", n, m.sampleCount);
}

mt::StreamPool& pool() { return thePool; }
mt::StoreTable& table() { return theTable; }
mt::BlockReader& reader() { return rd; }

Stats takeStats() {
  Stats s;
  for (int i = 0; i < mt::kStreamSlots; ++i) s.voices += thePool.slot(i).owner == mt::SlotOwner::Voice;
  const uint32_t u = thePool.underruns;
  s.underruns = static_cast<uint16_t>(u - underrunsSeen > 0xFFFF ? 0xFFFF : u - underrunsSeen);
  underrunsSeen = u;
  s.minReserveMs = minReserve == UINT32_MAX ? 0xFFFF
                                            : static_cast<uint16_t>(minReserve / 1000 > 0xFFFE ? 0xFFFE : minReserve / 1000);
  minReserve = UINT32_MAX;
  return s;
}

}  // namespace stream
```

`#include "preview_stream.h"` в начало `stream.cpp`.

**Step 3: Превью на слоте** (`preview_stream.cpp`)

- Удалить `mt::StreamRing ring;` как владельца памяти: кольцо остаётся объектом, буфер даёт слот.
- `handle()`: после успешного `open(...)`:

```cpp
      AudioNoInterrupts();
      slot = stream::pool().acquire(mt::kPreviewKey, 0, 0, nullptr, 0, mt::SampleRegion(), mt::SlotOwner::Preview);
      AudioInterrupts();
      if (slot < 0) {
        closeFile();
        m.result = code(BankResult::Busy);
      } else {
        ring.setBuffer(stream::pool().slotRef(slot).buf, mt::kSlotBlocks * mt::kStreamBlock);
        ring.reset();
        for (int i = 0; i < 8 && left && ring.space() > kChunk * 6 + 2; ++i) fill();  // a head start
        ring.start();
        streaming = true;
      }
```

- `stop()` и конец в `step()`, после `ring.stop()`:

```cpp
  if (slot >= 0) {
    AudioNoInterrupts();
    stream::pool().release(slot);
    AudioInterrupts();
    slot = -1;
  }
```

- Потребитель планировщика (`step()` больше не вызывает `fill()`):

```cpp
class RingConsumer final : public mt::StreamConsumer {
 public:
  bool needs(uint32_t& r) override {
    if (!ring.playing() || !left) {
      r = UINT32_MAX;
      return false;
    }
    r = static_cast<uint32_t>(static_cast<uint64_t>(ring.avail()) * 1000000 / 44100);
    return ring.space() > kChunk * 6 + 2;
  }
  void load() override { fill(); }
};
RingConsumer cons;
```

`mt::StreamConsumer* consumer() { return &cons; }` — объявить в `preview_stream.h`.

**Step 4: Банк**

`bank.cpp:290-300`, вместо `new (srcMem) mt::BankSampleSource(theBank, m)`:

```cpp
  auto* legacy = new (srcMem) mt::BankSampleSource(theBank, m);
  auto* src = new (storeMem) mt::StoreSampleSource(stream::table(), stream::pool(), legacy, m);
  synth.setBank(src);
```

с `alignas(mt::StoreSampleSource) uint8_t storeMem[sizeof(mt::StoreSampleSource)];`.

`bank::step()` → `bool bgStep()`: возвращает `true`, если `job.busy()` и шаг выполнен. Обёртка регистрируется в `bank::begin`:

```cpp
struct Bg final : mt::BgWork {
  bool step() override { return bgStep(); }
} bgWork;
...
stream::addBackground(&bgWork);
```

В `jobDone` для `Kind::Sync`, пока синт ещё припаркован (до `park(false)`): `stream::rescan(*model);`. Так же в конце `bank::begin` (загрузка, синт ещё не запущен).

**Step 5: `main.cpp`**

`setup`: после `bank::begin(...)` и до `stream.begin(&synth)` (выход звука) — `stream::begin(synth);` (имена: модуль `stream` против объекта `SynthStream stream` — объект переименовать в `out`, все упоминания в `main.cpp`). `loop`:

```cpp
void loop() {
  link::poll();
  stream::step();  // voice / preview loads, then one background portion (bank job, ...)
  preview::step();
  render::step();
  fw::step();
#ifdef AUDIO_BENCH_POOL
  bench::step();
#endif
}
```

`preview::step()` теперь только следит за концом превью.

**Step 6: Рендер**

`render_server.cpp`: рядом с `out->park(true)` (стр. 151) — `stream::pool().sync = &stream::reader();`, рядом с `out->park(false)` (стр. 84) — `stream::pool().sync = nullptr;`.

**Step 7: Сборка**

Run: `pio run -e teensy41 -e teensy41-desk -e teensy41-bench`
Expected: SUCCESS. В выводе размера: `RAM2` (`.bss.dma`) вырос на 98 304 байт, `DTCM` уменьшился примерно на 16 384.

**Step 8: Desk-проверка** (голый Teensy с картой, прошивка `teensy41-desk`)

1. `LD hello`, затем в логе (`LD status 2` печатает лог) — `synth board up, ... delay 2000 ms, PERF 0 s, RAM2 free ...`. Задержка 2 с, PERF 0 s до Task 18 (отклонения 5, 14). Затем `store: 0 of N samples`.
2. `LD pnote 0 60 1000` — нота звучит, как раньше (сэмпл из flash, путь legacy).
3. `LD preview /desk/long.wav --stop 5` (любой WAV ≥10 с на карте) — звучит 5 с. В логе нет `preview: N underruns`.
4. `LD card put big.bin /desk/big.bin` (файл 20 МБ) во время `LD preview /desk/long.wav` в другом терминале. Превью не прерывается: ФС-запросы идут между проходами, кольцо получает загрузку каждым проходом. Затем `LD card get /desk/big.bin /tmp/big2.bin && cmp big.bin /tmp/big2.bin` — без вывода (файл цел при смеси записей ФС и чтений превью).

### Проверка этапа 2

Run: `pio run -e wt32 -e teensy41 -e teensy41-desk -e teensy41-bench`
Expected: SUCCESS ×4.

Run: `pio test -e native`
Expected: PASS, добавился `test_sample_store` (6), `test_stream_ring` +2.

---

## Этап 3 — хранилище: импорт, синхронизация, «головы», миграция

После этапа импорт, загрузка и сохранение проекта работают через `/mt-store`. Старые сэмплы банка переезжают на карту, а во flash остаются wavetable-таблицы и «головы». Инструменты (пики, онсеты) читают с карты.

### Task 10: WAV float32

**Files:**
- Modify: `lib/core/src/wav.h:9-21` (`WavInfo::isFloat`), `lib/core/src/wav.cpp:28-42` (`parseFmt`), `:181-195` (`wavToMono`)
- Test: `test/test_wav/test_main.cpp`

**Step 1: Падающий тест**

```cpp
static std::vector<uint8_t> floatWav(uint16_t ch, const std::vector<float>& v, bool extensible) {
  std::vector<uint8_t> data;
  for (float f : v) {
    uint32_t u;
    memcpy(&u, &f, 4);
    put32(data, u);
  }
  std::vector<uint8_t> w;
  putId(w, "RIFF");
  put32(w, 0);
  putId(w, "WAVE");
  chunk(w, "fmt ", fmtBody(3, ch, 48000, 32, extensible));
  chunk(w, "data", data);
  const uint32_t riff = static_cast<uint32_t>(w.size() - 8);
  memcpy(w.data() + 4, &riff, 4);
  return w;
}

void test_float32_mono_converts_and_clips() {
  const auto w = floatWav(1, {0.f, 0.5f, -1.f, 2.f, NAN}, false);
  VecSource src(w);
  WavInfo info;
  TEST_ASSERT_EQUAL(WavErr::Ok, wavParse(src, info));
  TEST_ASSERT_TRUE(info.isFloat);
  TEST_ASSERT_EQUAL(32, info.bits);
  TEST_ASSERT_EQUAL_UINT32(5, info.frames());
  int16_t out[5];
  wavToMono(w.data() + info.dataOffset, 5, info, out);
  TEST_ASSERT_EQUAL_INT16(0, out[0]);
  TEST_ASSERT_EQUAL_INT16(16384, out[1]);
  TEST_ASSERT_EQUAL_INT16(-32768, out[2]);
  TEST_ASSERT_EQUAL_INT16(32767, out[3]);
  TEST_ASSERT_EQUAL_INT16(0, out[4]);
}

void test_float32_stereo_extensible_downmixes() {
  const auto w = floatWav(2, {0.5f, -0.5f, 0.25f, 0.25f}, true);
  VecSource src(w);
  WavInfo info;
  TEST_ASSERT_EQUAL(WavErr::Ok, wavParse(src, info));
  int16_t out[2];
  wavToMono(w.data() + info.dataOffset, 2, info, out);
  TEST_ASSERT_EQUAL_INT16(0, out[0]);
  TEST_ASSERT_EQUAL_INT16(8192, out[1]);
}

void test_float64_still_unsupported() {
  std::vector<uint8_t> w;
  putId(w, "RIFF");
  put32(w, 4 + 8 + 16);
  putId(w, "WAVE");
  chunk(w, "fmt ", fmtBody(3, 1, 48000, 64));
  VecSource src(w);
  WavInfo info;
  TEST_ASSERT_EQUAL(WavErr::Unsupported, wavParse(src, info));
}
```

и три `RUN_TEST`.

**Step 2: Запуск, ожидается падение**

Run: `pio test -e native -f test_wav`
Expected: FAIL (компиляция): `'struct mt::WavInfo' has no member named 'isFloat'`

**Step 3: Минимальная реализация**

`wav.h`, в `WavInfo`:

```cpp
  bool isFloat = false;                    // IEEE float 32 (format 3), else integer PCM
```

`wav.cpp`: константа `constexpr uint16_t kFmtFloat = 3;`. В `parseFmt` две последние проверки заменить на:

```cpp
  const bool fl = format == kFmtFloat && w.bits == 32;
  if ((format != kFmtPcm && !fl) || w.channels == 0 || w.rate == 0) return WavErr::Unsupported;
  if (!fl && w.bits != 8 && w.bits != 16 && w.bits != 24) return WavErr::Unsupported;
  w.isFloat = fl;
```

`wavToMono`, в `switch (bps)` перед `default`:

```cpp
        case 4: {  // float 32, -1..1
          float f;
          memcpy(&f, raw, 4);
          const float x = f * 32768.f;
          sum += x != x ? 0 : (x >= 32767.f ? 32767 : (x <= -32768.f ? -32768 : static_cast<int32_t>(x)));
          break;
        }
```

(`#include <string.h>`, если его ещё нет.) Комментарий к `wavToMono` в `wav.h`: «8/16/24 bit PCM or 32-bit float».

**Step 4: Повторный запуск**

Run: `pio test -e native -f test_wav`
Expected: PASS.

### Task 11: Обзор пиков и потоковый `OnsetScan`

**Files:**
- Modify: `lib/core/src/wave_peaks.h`, `lib/core/src/wave_peaks.cpp` (`kPeakBlock`, `peakOverview`, `PeakReader`, `wavePeaksRead`)
- Modify: `lib/core/src/onset.h`, `lib/core/src/onset.cpp:1-103` (`Cands` и `refine` — в `OnsetScan`, `detectOnsets` через него)
- Test: `test/test_wave_peaks/test_main.cpp`, `test/test_onset/test_main.cpp`

**Step 1: Падающие тесты**

`test_wave_peaks/test_main.cpp`:

```cpp
#include <vector>

struct MemPeaks : PeakReader {
  const std::vector<int16_t>* d;
  std::vector<int8_t> pk;
  int reads = 0;
  bool pairs(uint32_t first, uint32_t n, int8_t* out) override {
    ++reads;
    if ((first + n) * 2 > pk.size()) return false;
    memcpy(out, pk.data() + first * 2, n * 2);
    return true;
  }
  bool frames(uint32_t first, uint32_t n, int16_t* out) override {
    ++reads;
    if (first + n > d->size()) return false;
    memcpy(out, d->data() + first, n * 2);
    return true;
  }
};

static std::vector<int16_t> noise(uint32_t n) {
  std::vector<int16_t> v(n);
  uint32_t x = 7;
  for (auto& s : v) {
    x = x * 1664525u + 1013904223u;
    s = static_cast<int16_t>(x >> 16);
  }
  return v;
}

void test_overview_pairs_per_block() {
  const auto d = noise(1000);
  std::vector<int8_t> pk(8);
  TEST_ASSERT_EQUAL(4, peakOverview(d.data(), 1000, pk.data()));  // 256, 256, 256, 232
  int lo = 32767, hi = -32768;
  for (int i = 768; i < 1000; ++i) {
    lo = d[i] < lo ? d[i] : lo;
    hi = d[i] > hi ? d[i] : hi;
  }
  TEST_ASSERT_EQUAL_INT8(lo >> 8, pk[6]);
  TEST_ASSERT_EQUAL_INT8(hi >> 8, pk[7]);
}

void test_read_wide_columns_from_overview() {
  const auto d = noise(100000);
  MemPeaks r;
  r.d = &d;
  r.pk.resize(2 * ((100000 + 255) / 256));
  peakOverview(d.data(), 100000, r.pk.data());
  int8_t mn[40], mx[40];
  TEST_ASSERT_TRUE(wavePeaksRead(r, 100000, 0, 100000, 40, 40, mn, mx));  // 2500 frames per column
  for (int c = 0; c < 40; ++c) {
    const uint32_t a = c * 2500 / kPeakBlock * kPeakBlock, b = ((c + 1) * 2500 + kPeakBlock - 1) / kPeakBlock * kPeakBlock;
    int lo = 32767, hi = -32768;
    for (uint32_t i = a; i < b && i < 100000; ++i) {
      lo = d[i] < lo ? d[i] : lo;
      hi = d[i] > hi ? d[i] : hi;
    }
    TEST_ASSERT_EQUAL_INT8(lo >> 8, mn[c]);
    TEST_ASSERT_EQUAL_INT8(hi >> 8, mx[c]);
  }
}

void test_read_narrow_columns_reads_frames() {
  const auto d = noise(5000);
  MemPeaks r;
  r.d = &d;  // no overview: narrow columns never ask for it
  int8_t mn[50], mx[50], wmn[50], wmx[50];
  TEST_ASSERT_TRUE(wavePeaksRead(r, 5000, 10, 5000, 100, 50, mn, mx));  // 50 frames per column
  wavePeaks(d.data(), 5000, 10, 5000, 100, 50, wmn, wmx);
  TEST_ASSERT_EQUAL_INT8_ARRAY(wmn, mn, 50);
  TEST_ASSERT_EQUAL_INT8_ARRAY(wmx, mx, 50);
}

void test_read_past_end_is_empty_column() {
  const auto d = noise(1000);
  MemPeaks r;
  r.d = &d;
  int8_t mn[4], mx[4];
  TEST_ASSERT_TRUE(wavePeaksRead(r, 1000, 30, 1000, 32, 4, mn, mx));  // columns 30..33 of 32
  TEST_ASSERT_TRUE(mn[2] > mx[2]);
  TEST_ASSERT_TRUE(mn[3] > mx[3]);
}
```

`test_onset/test_main.cpp`:

```cpp
#include <vector>

static std::vector<int16_t> clicks(uint32_t n, uint32_t every) {
  std::vector<int16_t> v(n, 0);
  uint32_t x = 3;
  for (uint32_t i = 0; i < n; ++i) {
    x = x * 1664525u + 1013904223u;
    const uint32_t ph = i % every;
    const int amp = ph < 2000 ? 20000 - static_cast<int>(ph) * 10 : 30;
    v[i] = static_cast<int16_t>((static_cast<int>(x >> 16) - 32768) * amp / 32768);
  }
  return v;
}

void test_scan_in_pieces_equals_detect() {
  const auto d = clicks(200000, 7919);
  Onset a[kMaxOnsets], b[kMaxOnsets];
  const int na = detectOnsets(d.data(), 200000, 44100, a, kMaxOnsets);
  TEST_ASSERT_TRUE(na > 10);
  OnsetScan s(200000, 44100, kMaxOnsets);
  uint32_t at = 0, piece = 1;
  while (at < 200000) {  // ragged pieces: 1, 2, 3, ... frames up to 997
    const uint32_t n = at + piece <= 200000 ? piece : 200000 - at;
    s.feed(d.data() + at, n);
    at += n;
    piece = piece % 997 + 1;
  }
  TEST_ASSERT_EQUAL(na, s.finish(b));
  for (int i = 0; i < na; ++i) {
    TEST_ASSERT_EQUAL_UINT32(a[i].pos, b[i].pos);
    TEST_ASSERT_EQUAL_UINT16(a[i].strength, b[i].strength);
  }
}

void test_scan_short_input_finds_nothing() {
  OnsetScan s(100, 44100, kMaxOnsets);
  int16_t z[100] = {};
  s.feed(z, 100);
  Onset o[1];
  TEST_ASSERT_EQUAL(0, s.finish(o));
}
```

и `RUN_TEST` для всех новых.

**Step 2: Запуск, ожидается падение**

Run: `pio test -e native -f test_wave_peaks` и `pio test -e native -f test_onset`
Expected: FAIL (компиляция): `'PeakReader' was not declared in this scope`, `'OnsetScan' was not declared in this scope`

**Step 3: Минимальная реализация**

`wave_peaks.h`:

```cpp
// The store's peak overview (/mt-store/<crc>.pk): min / max (>> 8) of every kPeakBlock frames.
constexpr uint32_t kPeakBlock = 256;
// Pairs of d (n frames, the last block may be shorter) into out; returns the pair count.
int peakOverview(const int16_t* d, uint32_t n, int8_t* out);

// A sample on the card: overview pairs and frames, read on demand.
struct PeakReader {
  virtual bool pairs(uint32_t first, uint32_t n, int8_t* out) = 0;    // pairs [first, first + n)
  virtual bool frames(uint32_t first, uint32_t n, int16_t* out) = 0;  // frames [first, first + n)
};
// wavePeaks for a sample behind r: a column of kPeakBlock frames or more takes the overview pairs of
// the blocks it touches, a narrower one reads its frames (exact). False on a read error.
bool wavePeaksRead(PeakReader& r, uint32_t frames, uint32_t col0, uint32_t span, uint32_t width, int cols,
                   int8_t* mn, int8_t* mx);
```

`wave_peaks.cpp`:

```cpp
int peakOverview(const int16_t* d, uint32_t n, int8_t* out) {
  int k = 0;
  for (uint32_t a = 0; a < n; a += kPeakBlock, ++k) {
    const uint32_t b = a + kPeakBlock < n ? a + kPeakBlock : n;
    int lo = 32767, hi = -32768;
    for (uint32_t i = a; i < b; ++i) {
      lo = d[i] < lo ? d[i] : lo;
      hi = d[i] > hi ? d[i] : hi;
    }
    out[2 * k] = static_cast<int8_t>(lo >> 8);
    out[2 * k + 1] = static_cast<int8_t>(hi >> 8);
  }
  return k;
}

bool wavePeaksRead(PeakReader& r, uint32_t frames, uint32_t col0, uint32_t span, uint32_t width, int cols,
                   int8_t* mn, int8_t* mx) {
  if (!width) width = 1;
  auto gridFrame = [&](uint64_t g) { return static_cast<uint32_t>(g * span / width); };
  int8_t pk[2 * 128];
  int16_t fr[kPeakBlock];
  for (int x = 0; x < cols; ++x) {
    const uint32_t a = gridFrame(static_cast<uint64_t>(col0) + x);
    uint32_t b = gridFrame(static_cast<uint64_t>(col0) + x + 1);
    if (b <= a) b = a + 1;
    if (a >= frames) {
      mn[x] = 1;
      mx[x] = 0;
      continue;
    }
    if (b > frames) b = frames;
    int lo = 127, hi = -128;
    if (b - a >= kPeakBlock) {
      const uint32_t p0 = a / kPeakBlock, p1 = (b + kPeakBlock - 1) / kPeakBlock;
      for (uint32_t p = p0; p < p1; p += 128) {
        const uint32_t n = p1 - p < 128 ? p1 - p : 128;
        if (!r.pairs(p, n, pk)) return false;
        for (uint32_t i = 0; i < n; ++i) {
          lo = pk[2 * i] < lo ? pk[2 * i] : lo;
          hi = pk[2 * i + 1] > hi ? pk[2 * i + 1] : hi;
        }
      }
    } else {
      // Narrow: every frame, like wavePeaks with stride 1.
      if (!r.frames(a, b - a, fr)) return false;
      int l = 32767, h = -32768;
      for (uint32_t i = 0; i < b - a; ++i) {
        l = fr[i] < l ? fr[i] : l;
        h = fr[i] > h ? fr[i] : h;
      }
      lo = l >> 8;
      hi = h >> 8;
    }
    mn[x] = static_cast<int8_t>(lo);
    mx[x] = static_cast<int8_t>(hi);
  }
  return true;
}
```

(`>> 8` у min/max совпадает с min/max от `>> 8`: сдвиг монотонен, поэтому склейка пар по блокам точна.)

`onset.h`:

```cpp
// detectOnsets over data fed in pieces (a sample read from the card): the same result for the same
// frames. frames: the total that will be fed.
class OnsetScan {
 public:
  OnsetScan(uint32_t frames, uint32_t rate, int max);
  void feed(const int16_t* d, uint32_t n);  // the next n frames, in order
  int finish(Onset* out);                   // after all frames; like detectOnsets

 private:
  static constexpr uint32_t kRing = 1024;  // > the 512 frames a window and its refine look back
  struct Cands {
    Onset out[kMaxOnsets];
    float fl[kMaxOnsets];
    int n = 0, max = kMaxOnsets;
    void remove(int i);
    void add(uint32_t pos, float f, uint32_t gap);
  };
  void window(uint32_t k);
  int16_t ring_[kRing];
  uint32_t frames_, windows_, gap_;
  uint32_t got_ = 0, k_ = 0;
  float prevDb_ = -120.f;
  float flux_[3] = {0, 0, 0};
  Cands c_;
};
```

`onset.cpp`: `Cands::remove` / `Cands::add` переносятся как члены `OnsetScan::Cands` (`out` теперь массив). `refine` получает функцию доступа:

```cpp
// Start of the first kRefine block of [from, to) reaching half its peak; at(i) = frame i.
template <class At>
uint32_t refine(At at, uint32_t from, uint32_t to) {
  auto absAt = [&](uint32_t i) { const int v = at(i); return v < 0 ? -v : v; };
  int peak = 0;
  for (uint32_t i = from; i < to; ++i) peak = absAt(i) > peak ? absAt(i) : peak;
  for (uint32_t b = from; b < to; b += kRefine) {
    const uint32_t e = b + kRefine < to ? b + kRefine : to;
    for (uint32_t i = b; i < e; ++i)
      if (2 * absAt(i) >= peak) return b;
  }
  return from;
}
```

Тело цикла `detectOnsets` переезжает в `window()` без изменения арифметики:

```cpp
OnsetScan::OnsetScan(uint32_t frames, uint32_t rate, int max) : frames_(frames), gap_(rate * 40 / 1000) {
  c_.max = max > kMaxOnsets ? kMaxOnsets : (max < 0 ? 0 : max);
  windows_ = frames >= static_cast<uint32_t>(kWin) ? (frames - kWin) / kHop + 1 : 0;
}

void OnsetScan::feed(const int16_t* d, uint32_t n) {
  for (uint32_t i = 0; i < n; ++i) {
    ring_[got_ % kRing] = d[i];
    ++got_;
    while (k_ < windows_ && got_ >= k_ * kHop + kWin) window(k_++);
  }
}

void OnsetScan::window(uint32_t k) {
  auto at = [&](uint32_t i) { return static_cast<int>(ring_[i % kRing]); };
  float f = 0;
  if (k < windows_) {
    uint64_t sum = 0;
    for (uint32_t i = k * kHop; i < k * kHop + kWin; ++i) sum += static_cast<int64_t>(at(i)) * at(i);
    const float e = static_cast<float>(sum) / kWin / (32768.f * 32768.f);
    const float db = 10.f * log10f(e + 1e-12f);
    f = db > kGateDb ? db - prevDb_ : 0.f;
    prevDb_ = db;
  }
  flux_[0] = flux_[1];
  flux_[1] = flux_[2];
  flux_[2] = f;
  if (k == 0 || !(flux_[1] >= kRiseDb && flux_[1] > flux_[0] && flux_[1] >= flux_[2])) return;
  const uint32_t p = k - 1;  // the peak window
  const uint32_t from = p > 0 ? (p - 1) * kHop : 0;
  const uint32_t to = p * kHop + kWin < frames_ ? p * kHop + kWin : frames_;
  c_.add(refine(at, from, to), flux_[1], gap_);
}

int OnsetScan::finish(Onset* out) {
  if (!windows_ || c_.max <= 0) return 0;
  window(windows_);  // the last peak test (flux 0 after the end)
  float top = 0;
  for (int i = 0; i < c_.n; ++i) top = c_.fl[i] > top ? c_.fl[i] : top;
  for (int i = 0; i < c_.n; ++i) {
    const int s = static_cast<int>(lroundf(c_.fl[i] / top * 1000.f));
    out[i] = {c_.out[i].pos, static_cast<uint16_t>(s < 1 ? 1 : s > 1000 ? 1000 : s)};
  }
  return c_.n;
}

int detectOnsets(const int16_t* d, uint32_t frames, uint32_t rate, Onset* out, int max) {
  if (!d) return 0;
  OnsetScan s(frames, rate, max);
  s.feed(d, frames);
  return s.finish(out);
}
```

`OnsetScan` занимает ≈3,1 КБ: на Teensy — статический объект в задаче инструментов, не на стеке.

**Step 4: Повторный запуск**

Run: `pio test -e native -f test_wave_peaks` и `pio test -e native -f test_onset`
Expected: PASS: старые тесты `test_onset` без изменений, новые проходят.

### Task 12: `StoreJob`: импорт, синхронизация, сохранение

**Files:**
- Modify: `lib/core/src/bank_ops.h:27-31` (`WriteFile`: `reserve`, `seek`)
- Modify: `lib/core/src/sample_store.h`, `lib/core/src/sample_store.cpp` (`kStoreTmp`, `storeFileCurrent`, `StoreJob`)
- Test: `test/test_bank_ops/test_main.cpp` (общие фикстуры `PosixDisk`): `PosixWrite::seek`, `runStore`, `readAll`, новые тесты

**Step 1: Падающий тест**

Фикстуры:

```cpp
struct PosixWrite final : WriteFile {
  FILE* f = nullptr;
  bool write(const void* d, size_t n) override { return fwrite(d, 1, n, f) == n; }
  bool seek(uint32_t pos) override { return fseek(f, static_cast<long>(pos), SEEK_SET) == 0; }
};
```

```cpp
#include "sample_store.h"
#include "wave_peaks.h"

static BankResult runStore(StoreJob& j) {
  BankResult r;
  int guard = 0;
  while ((r = j.step()) == BankResult::Running) TEST_ASSERT_TRUE(++guard < 1000000);
  return r;
}
static std::vector<uint8_t> readAll(const char* path) {
  std::vector<uint8_t> v;
  FILE* f = fopen(disk->full(path).c_str(), "rb");
  if (!f) return v;
  uint8_t b[4096];
  size_t n;
  while ((n = fread(b, 1, sizeof b, f)) > 0) v.insert(v.end(), b, b + n);
  fclose(f);
  return v;
}
```

Тесты:

```cpp
void test_store_import_writes_canonical_file() {
  const auto d = ramp(3000, 3);
  writePlainWav("/a.wav", d, 1, 32000);
  StoreJob j(*disk);
  TEST_ASSERT_EQUAL(BankResult::Ok, j.startImport("/a.wav"));
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  const uint32_t crc = sampleCrc(d.data(), 3000, 0);
  TEST_ASSERT_EQUAL_HEX32(crc, j.imported().crc);
  TEST_ASSERT_EQUAL_UINT32(3000, j.imported().frames);
  TEST_ASSERT_EQUAL_UINT32(32000, j.imported().rate);
  TEST_ASSERT_TRUE(storeFileCurrent(*disk, crc, 3000));
  char p[kStorePathMax];
  storePath(crc, p);
  const auto f = readAll(p);
  TEST_ASSERT_EQUAL_UINT32(kStoreDataOffset + 6000, f.size());
  TEST_ASSERT_EQUAL_MEMORY(d.data(), f.data() + kStoreDataOffset, 6000);
  storePeaksPath(crc, p);
  const auto pk = readAll(p);
  TEST_ASSERT_EQUAL_UINT32(2 * 12, pk.size());  // ceil(3000 / 256) pairs
  TEST_ASSERT_EQUAL_INT8(3000 >> 8, static_cast<int8_t>(pk[0]));
  TEST_ASSERT_EQUAL_INT8((3000 + 255 * 7) >> 8, static_cast<int8_t>(pk[1]));
  TEST_ASSERT_FALSE(disk->exists(kStoreTmp));
}

void test_store_import_stereo_48k_downmixes_and_downsamples() {
  writePlainWav("/s.wav", ramp(9600, 1), 2, 48000);  // 4800 stereo frames
  StoreJob j(*disk);
  TEST_ASSERT_EQUAL(BankResult::Ok, j.startImport("/s.wav"));
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  TEST_ASSERT_EQUAL_UINT32(44100, j.imported().rate);
  TEST_ASSERT_UINT32_WITHIN(2, 4410, j.imported().frames);
  TEST_ASSERT_TRUE(storeFileCurrent(*disk, j.imported().crc, j.imported().frames));
}

void test_store_import_again_keeps_one_file() {
  writePlainWav("/a.wav", ramp(1000, 2), 1, 44100);
  StoreJob j(*disk);
  j.startImport("/a.wav");
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  const uint32_t crc = j.imported().crc;
  j.startImport("/a.wav");
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  TEST_ASSERT_EQUAL_HEX32(crc, j.imported().crc);
  TEST_ASSERT_FALSE(disk->exists(kStoreTmp));
}

void test_store_import_known_crc_skips_reading() {
  writePlainWav("/a.wav", ramp(1000, 2), 1, 44100);
  StoreJob j(*disk);
  j.startImport("/a.wav");
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  const uint32_t crc = j.imported().crc;
  TEST_ASSERT_EQUAL(BankResult::Ok, j.startImport("/a.wav", &crc));
  TEST_ASSERT_FALSE(j.busy() && j.done() > 0);
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  TEST_ASSERT_EQUAL_HEX32(crc, j.imported().crc);
}

void test_store_sync_takes_project_files() {
  disk->mkdir("/projects/P");
  const auto a = ramp(2000, 4), b = ramp(1500, 6);
  writePlainWav("/projects/P/KICK.wav", a, 1, 44100);
  writePlainWav("/projects/P/SNARE.wav", b, 1, 44100);
  projSampleSet(*proj, "KICK", sampleCrc(a.data(), 2000, 0), 2000);
  projSampleSet(*proj, "SNARE", 0xDEADBEEFu, 1500);  // the folder holds other data
  projSampleSet(*proj, "HAT", 0x12345678u, 100);     // no file
  StoreJob j(*disk);
  TEST_ASSERT_EQUAL(BankResult::Ok, j.startSync("P", *proj));
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  TEST_ASSERT_TRUE(storeFileCurrent(*disk, sampleCrc(a.data(), 2000, 0), 2000));
  TEST_ASSERT_EQUAL(2, j.missing());
  TEST_ASSERT_FALSE(j.sampleMissing(projSampleFind(*proj, "KICK")));
  TEST_ASSERT_TRUE(j.sampleMissing(projSampleFind(*proj, "SNARE")));
  TEST_ASSERT_TRUE(j.sampleMissing(projSampleFind(*proj, "HAT")));
  TEST_ASSERT_FALSE(disk->exists(kStoreTmp));
}

void test_store_sync_without_project_marks_missing() {
  projSampleSet(*proj, "KICK", 0x11111111u, 100);
  StoreJob j(*disk);
  TEST_ASSERT_EQUAL(BankResult::Ok, j.startSync("", *proj));
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  TEST_ASSERT_EQUAL(1, j.missing());
}

void test_store_save_copies_into_project() {
  const auto a = ramp(2500, 5);
  writePlainWav("/a.wav", a, 1, 22050);
  StoreJob j(*disk);
  j.startImport("/a.wav");
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  const uint32_t crc = j.imported().crc;
  projSampleSet(*proj, "PAD", crc, 2500);
  projSampleSet(*proj, "GONE", 0x22222222u, 10);  // not in the store
  disk->mkdir("/projects/Q");
  TEST_ASSERT_EQUAL(BankResult::Ok, j.startSave("Q", *proj));
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  TEST_ASSERT_EQUAL(1, j.missing());
  TEST_ASSERT_TRUE(bankFileCurrent(*disk, "/projects/Q/PAD.wav", crc, 2500));
  TEST_ASSERT_FALSE(disk->exists("/projects/Q/PAD.wav.tmp"));
}

struct Added {
  std::vector<uint32_t> crc;
};
static void onAdded(uint32_t crc, void* ctx) { static_cast<Added*>(ctx)->crc.push_back(crc); }

void test_store_reports_added_files() {
  writePlainWav("/a.wav", ramp(700, 8), 1, 44100);
  Added got;
  StoreJob j(*disk);
  j.setOnAdded(onAdded, &got);
  j.startImport("/a.wav");
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  TEST_ASSERT_EQUAL(1, got.crc.size());
  TEST_ASSERT_EQUAL_HEX32(j.imported().crc, got.crc[0]);
}
```

и `RUN_TEST` для всех.

**Step 2: Запуск, ожидается падение**

Run: `pio test -e native -f test_bank_ops`
Expected: FAIL (компиляция): `'bool PosixWrite::seek(uint32_t)' marked 'override', but does not override`

**Step 3: Минимальная реализация**

`bank_ops.h`:

```cpp
// A file being written: ByteSink write takes exactly n bytes or fails.
struct WriteFile : ByteSink {
  // Room for bytes in one contiguous run if the card can (SdFat preAllocate); a hint.
  virtual bool reserve(uint32_t bytes) {
    (void)bytes;
    return true;
  }
  virtual bool seek(uint32_t pos) {  // rewrite from pos (the size stays the largest written)
    (void)pos;
    return false;
  }
};
```

`sample_store.h` (+ `#include "bank_ops.h"`):

```cpp
extern const char kStoreTmp[];  // a store file being written
// crc's store file is there with frames of data (header and size, no data read).
bool storeFileCurrent(BankDisk& d, uint32_t crc, uint32_t frames);

// One store operation on the card, a piece at a time (step()) between link and audio work. Nothing
// here changes the flash bank: the synth keeps playing (register new files through setOnAdded).
//  - Import: any supported WAV -> the store (mono 16 bit, <= kWavMaxRate, root from "smpl"). With the
//    file's mtcr crc (or knownCrc) already stored at its length nothing is read.
//  - Sync: every sample of m not in the store comes from /projects/<project>/<name>.wav; other data
//    (crc) or no file: missing.
//  - Save: every sample of m in the store whose project file is not current is copied there (via
//    <file>.tmp); one not in the store: missing.
//  - Migrate: every sample entry of the flash bank (8-hex name) not in the store is written there.
// Every new store file is followed by its peak overview (<crc>.pk).
class StoreJob {
 public:
  enum class Kind : uint8_t { None, Import, Sync, Save, Migrate };
  static constexpr uint32_t kStepFrames = 1024;  // per step; a multiple of kPeakBlock
  using Added = void (*)(uint32_t crc, void* ctx);

  explicit StoreJob(BankDisk& d) : d_(d) {}
  ~StoreJob() { cancel(); }
  void setOnAdded(Added f, void* ctx) {
    added_ = f;
    addedCtx_ = ctx;
  }
  BankResult startImport(const char* path, const uint32_t* knownCrc = nullptr);
  BankResult startSync(const char* project, const SynthModel& m);
  BankResult startSave(const char* project, const SynthModel& m);
  BankResult startMigrate(const SampleBank& b);
  BankResult step();  // Running until it ends
  void cancel();

  Kind kind() const { return kind_; }
  bool busy() const { return kind_ != Kind::None; }
  const ImportOut& imported() const { return imp_; }
  bool sampleMissing(int i) const { return i >= 0 && i < kProjSamples && missing_[i]; }
  int missing() const;
  int item() const { return item_; }
  uint32_t done() const;
  uint32_t total() const;

 private:
  enum class Phase : uint8_t { Next, Convert, Copy, Peaks };
  BankResult start(Kind k);
  BankResult nextItem();
  BankResult openConvert(const char* path, const uint32_t* expect, const uint32_t* known);
  BankResult openCopy(uint32_t crc);
  BankResult beginWrite(uint32_t frames);
  BankResult convertStep();
  BankResult finishConvert();
  BankResult copyStep();
  BankResult openPeaks();
  BankResult peaksStep();
  BankResult endItem(BankResult r);
  BankResult finish(BankResult r);
  void closeAll();

  BankDisk& d_;
  Added added_ = nullptr;
  void* addedCtx_ = nullptr;
  Kind kind_ = Kind::None;
  Phase phase_ = Phase::Next;
  const SynthModel* m_ = nullptr;
  const SampleBank* b_ = nullptr;
  char project_[64] = {};
  char path_[96] = {};  // the source (Convert) or the .tmp copy (Copy)
  char dst_[96] = {};   // the project file (Copy) or the overview (Peaks)
  int item_ = 0, count_ = 0;
  bool missing_[kProjSamples] = {};
  ImportOut imp_;
  ReadFile* in_ = nullptr;
  WriteFile* out_ = nullptr;
  WavInfo w_;
  Downsampler ds_{1};
  int bankEntry_ = -1;
  uint32_t at_ = 0, left_ = 0, frames_ = 0, crc_ = 0, expect_ = 0, rate_ = 0;
  bool hasExpect_ = false;
  uint8_t root_ = 60;
  uint32_t subDone_ = 0, subTotal_ = 0;
  uint8_t raw_[kStepFrames * 8];  // stereo float: 8 bytes a frame
  int16_t mono_[kStepFrames];
  int16_t res_[kStepFrames + 2];
};
```

`sample_store.cpp` (+ `#include <stdio.h>`, `<stdlib.h>`, `"wave_peaks.h"`):

```cpp
const char kStoreTmp[] = "/mt-store/import.tmp";

namespace {
BankResult fromWav(WavErr e) {
  return e == WavErr::NotWav ? BankResult::NotWav
                             : (e == WavErr::Truncated ? BankResult::Truncated : BankResult::Unsupported);
}
}  // namespace

bool storeFileCurrent(BankDisk& d, uint32_t crc, uint32_t frames) {
  char p[kStorePathMax];
  storePath(crc, p);
  return bankFileCurrent(d, p, crc, frames);
}

int StoreJob::missing() const {
  int n = 0;
  for (bool m : missing_) n += m;
  return n;
}
uint32_t StoreJob::done() const {
  if (kind_ == Kind::Import) return subDone_;
  return static_cast<uint32_t>(item_) * 1000 + (subTotal_ ? static_cast<uint32_t>(uint64_t(subDone_) * 1000 / subTotal_) : 0);
}
uint32_t StoreJob::total() const {
  return kind_ == Kind::Import ? subTotal_ : static_cast<uint32_t>(count_) * 1000;
}

void StoreJob::closeAll() {
  if (in_) d_.closeRead();
  in_ = nullptr;
  if (out_) d_.closeWrite();
  out_ = nullptr;
}

BankResult StoreJob::start(Kind k) {
  if (busy()) return BankResult::Busy;
  if (!d_.ready()) return BankResult::NoSd;
  d_.mkdir(kStoreDir);  // false when it is there
  kind_ = k;
  phase_ = Phase::Next;
  item_ = count_ = 0;
  subDone_ = subTotal_ = 0;
  for (auto& x : missing_) x = false;
  return BankResult::Ok;
}

BankResult StoreJob::finish(BankResult r) {
  closeAll();
  kind_ = Kind::None;
  phase_ = Phase::Next;
  return r;
}

void StoreJob::cancel() {
  if (!busy()) return;
  const Phase ph = phase_;
  closeAll();
  if (ph == Phase::Convert) d_.remove(kStoreTmp);
  if (ph == Phase::Copy) d_.remove(path_);
  if (ph == Phase::Peaks) d_.remove(dst_);
  kind_ = Kind::None;
  phase_ = Phase::Next;
}

BankResult StoreJob::startImport(const char* path, const uint32_t* knownCrc) {
  BankResult r = start(Kind::Import);
  if (r != BankResult::Ok) return r;
  imp_ = ImportOut();
  r = openConvert(path, nullptr, knownCrc);
  if (r != BankResult::Ok) finish(r);
  return r;
}

BankResult StoreJob::startSync(const char* project, const SynthModel& m) {
  const BankResult r = start(Kind::Sync);
  if (r != BankResult::Ok) return r;
  m_ = &m;
  strncpy(project_, project, sizeof project_ - 1);
  project_[sizeof project_ - 1] = 0;
  count_ = m.sampleCount;
  return r;
}

BankResult StoreJob::startSave(const char* project, const SynthModel& m) {
  const BankResult r = start(Kind::Save);
  if (r != BankResult::Ok) return r;
  m_ = &m;
  strncpy(project_, project, sizeof project_ - 1);
  project_[sizeof project_ - 1] = 0;
  count_ = m.sampleCount;
  return r;
}

BankResult StoreJob::startMigrate(const SampleBank& b) {
  const BankResult r = start(Kind::Migrate);
  if (r != BankResult::Ok) return r;
  b_ = &b;
  count_ = b.count();
  return r;
}

BankResult StoreJob::step() {
  if (!busy()) return BankResult::Ok;
  switch (phase_) {
    case Phase::Convert: return convertStep();
    case Phase::Copy: return copyStep();
    case Phase::Peaks: return peaksStep();
    default: return kind_ == Kind::Import ? finish(BankResult::Ok) : nextItem();
  }
}

BankResult StoreJob::endItem(BankResult r) {
  const Phase ph = phase_;
  closeAll();
  if (ph == Phase::Convert) d_.remove(kStoreTmp);
  if (ph == Phase::Copy && r != BankResult::Ok) d_.remove(path_);
  if (ph == Phase::Peaks && left_) d_.remove(dst_);  // a cut overview is worse than none
  phase_ = Phase::Next;
  bankEntry_ = -1;
  subDone_ = subTotal_ = 0;
  if (kind_ == Kind::Import) return finish(r);
  if (r != BankResult::Ok && kind_ != Kind::Migrate && item_ < kProjSamples) missing_[item_] = true;
  ++item_;
  return BankResult::Running;
}

BankResult StoreJob::nextItem() {
  for (; item_ < count_; ++item_) {
    if (kind_ == Kind::Migrate) {
      const BankEntry* e = b_->entry(item_);
      if (!e || !isSampleKey(e->name)) continue;
      const uint32_t crc = static_cast<uint32_t>(strtoul(e->name, nullptr, 16));
      if (storeFileCurrent(d_, crc, e->frames)) continue;
      bankEntry_ = item_;
      at_ = 0;
      left_ = subTotal_ = e->frames;
      subDone_ = 0;
      hasExpect_ = true;
      expect_ = crc;
      rate_ = e->rate;
      root_ = e->root;
      ds_ = Downsampler(e->rate);
      if (beginWrite(e->frames) != BankResult::Ok) return finish(BankResult::WriteFail);
      return BankResult::Running;
    }
    const ProjSample& ps = m_->samples[item_];
    if (kind_ == Kind::Sync) {
      if (storeFileCurrent(d_, ps.crc, ps.frames)) continue;
      const int n = snprintf(path_, sizeof path_, "/projects/%s/%s.wav", project_, ps.name);
      if (!project_[0] || n <= 0 || n >= static_cast<int>(sizeof path_) ||
          openConvert(path_, &ps.crc, nullptr) != BankResult::Ok) {
        missing_[item_] = true;
        continue;
      }
      if (phase_ == Phase::Next) continue;  // was stored after all
      return BankResult::Running;
    }
    // Save
    if (!storeFileCurrent(d_, ps.crc, ps.frames)) {
      missing_[item_] = true;
      continue;
    }
    const int n = snprintf(dst_, sizeof dst_, "/projects/%s/%s.wav", project_, ps.name);
    if (!project_[0] || n <= 0 || n + 4 >= static_cast<int>(sizeof dst_)) {
      missing_[item_] = true;
      continue;
    }
    if (bankFileCurrent(d_, dst_, ps.crc, ps.frames)) continue;
    if (openCopy(ps.crc) != BankResult::Ok) {
      missing_[item_] = true;
      continue;
    }
    return BankResult::Running;
  }
  return finish(BankResult::Ok);
}

BankResult StoreJob::openConvert(const char* path, const uint32_t* expect, const uint32_t* known) {
  in_ = d_.openRead(path);
  if (!in_) return BankResult::OpenFail;
  w_ = WavInfo();
  const WavErr e = wavParse(*in_, w_);
  if (e != WavErr::Ok) {
    closeAll();
    return fromWav(e);
  }
  const uint32_t fb = w_.frameBytes();
  if (!fb || fb > 8) {
    closeAll();
    return BankResult::Unsupported;
  }
  uint32_t frames = w_.frames();
  const uint32_t size = in_->size();
  if (w_.dataOffset >= size) frames = 0;
  else if (frames > (size - w_.dataOffset) / fb) frames = (size - w_.dataOffset) / fb;  // a cut file
  if (!frames) {
    closeAll();
    return BankResult::Truncated;
  }
  hasExpect_ = expect != nullptr;
  expect_ = expect ? *expect : 0;
  root_ = w_.root;
  ds_ = Downsampler(w_.rate);
  rate_ = ds_.outRate();
  const uint32_t* k = expect ? expect : (known ? known : (w_.hasCrc ? &w_.crc : nullptr));
  if (k && rate_ == w_.rate && storeFileCurrent(d_, *k, frames)) {  // stored already: nothing to read
    closeAll();
    imp_.crc = *k;
    imp_.frames = frames;
    imp_.rate = rate_;
    imp_.root = root_;
    phase_ = Phase::Next;
    return BankResult::Ok;
  }
  if (!in_->seek(w_.dataOffset)) {
    closeAll();
    return BankResult::ReadFail;
  }
  bankEntry_ = -1;
  left_ = subTotal_ = frames;
  subDone_ = 0;
  return beginWrite(static_cast<uint32_t>(static_cast<uint64_t>(frames) * rate_ / w_.rate) + 2);
}

BankResult StoreJob::beginWrite(uint32_t frames) {
  d_.remove(kStoreTmp);
  out_ = d_.openWrite(kStoreTmp);
  if (!out_) {
    closeAll();
    return BankResult::WriteFail;
  }
  out_->reserve(kStoreDataOffset + frames * 2);
  static const uint8_t zero[kStoreDataOffset] = {};
  if (!out_->write(zero, sizeof zero)) {  // the header goes in at the end (crc)
    closeAll();
    d_.remove(kStoreTmp);
    return BankResult::WriteFail;
  }
  frames_ = crc_ = 0;
  phase_ = Phase::Convert;
  return BankResult::Ok;
}

BankResult StoreJob::convertStep() {
  uint32_t n = left_ < kStepFrames ? left_ : kStepFrames;
  int k;
  if (bankEntry_ >= 0) {
    if (!b_->readData(bankEntry_, at_, res_, n)) return endItem(BankResult::ReadFail);
    at_ += n;
    k = static_cast<int>(n);
  } else {
    const uint32_t fb = w_.frameBytes();
    if (n > sizeof raw_ / fb) n = sizeof raw_ / fb;
    if (!in_->read(raw_, n * fb)) return endItem(BankResult::Truncated);
    wavToMono(raw_, n, w_, mono_);
    k = ds_.push(mono_, static_cast<int>(n), res_);
  }
  left_ -= n;
  subDone_ += n;
  crc_ = sampleCrc(res_, static_cast<uint32_t>(k), crc_);
  frames_ += static_cast<uint32_t>(k);
  if (k && !out_->write(res_, static_cast<size_t>(k) * 2)) return endItem(BankResult::WriteFail);
  return left_ ? BankResult::Running : finishConvert();
}

BankResult StoreJob::finishConvert() {
  if (in_) d_.closeRead();
  in_ = nullptr;
  if (hasExpect_ && crc_ != expect_) return endItem(BankResult::ReadFail);  // other data than listed
  uint8_t h[kStoreDataOffset];
  storeHeader(h, frames_, rate_, root_, crc_);
  const bool ok = out_->seek(0) && out_->write(h, sizeof h);
  out_ = nullptr;
  if (!d_.closeWrite() || !ok) {
    d_.remove(kStoreTmp);
    return endItem(BankResult::WriteFail);
  }
  char p[kStorePathMax];
  storePath(crc_, p);
  if (d_.exists(p)) {
    d_.remove(kStoreTmp);  // the same data is there already
  } else if (!d_.rename(kStoreTmp, p)) {
    d_.remove(kStoreTmp);
    phase_ = Phase::Next;
    return endItem(BankResult::WriteFail);
  }
  if (kind_ == Kind::Import) {
    imp_.crc = crc_;
    imp_.frames = frames_;
    imp_.rate = rate_;
    imp_.root = root_;
  }
  if (added_) added_(crc_, addedCtx_);
  return openPeaks();
}

BankResult StoreJob::openPeaks() {
  char p[kStorePathMax];
  storePath(crc_, p);
  phase_ = Phase::Next;
  in_ = d_.openRead(p);
  storePeaksPath(crc_, dst_);
  out_ = in_ ? d_.openWrite(dst_) : nullptr;
  left_ = frames_;
  if (!in_ || !out_ || !in_->seek(kStoreDataOffset)) {
    phase_ = Phase::Peaks;
    return endItem(BankResult::Ok);  // no overview: the tools read frames instead
  }
  phase_ = Phase::Peaks;
  return BankResult::Running;
}

BankResult StoreJob::peaksStep() {
  const uint32_t n = left_ < kStepFrames ? left_ : kStepFrames;
  if (!in_->read(res_, n * 2)) return endItem(BankResult::Ok);
  int8_t pk[2 * kStepFrames / kPeakBlock];
  const int np = peakOverview(res_, n, pk);
  if (!out_->write(pk, static_cast<size_t>(np) * 2)) return endItem(BankResult::Ok);
  left_ -= n;
  return left_ ? BankResult::Running : endItem(BankResult::Ok);
}

BankResult StoreJob::openCopy(uint32_t crc) {
  char src[kStorePathMax];
  storePath(crc, src);
  snprintf(path_, sizeof path_, "%s.tmp", dst_);
  in_ = d_.openRead(src);
  out_ = in_ ? d_.openWrite(path_) : nullptr;
  if (!in_ || !out_) {
    closeAll();
    return BankResult::OpenFail;
  }
  left_ = subTotal_ = in_->size();
  subDone_ = 0;
  phase_ = Phase::Copy;
  return BankResult::Ok;
}

BankResult StoreJob::copyStep() {
  const uint32_t n = left_ < sizeof raw_ ? left_ : sizeof raw_;
  if (!in_->read(raw_, n)) return endItem(BankResult::ReadFail);
  if (!out_->write(raw_, n)) return endItem(BankResult::WriteFail);
  left_ -= n;
  subDone_ += n;
  if (left_) return BankResult::Running;
  d_.closeRead();
  in_ = nullptr;
  out_ = nullptr;
  if (!d_.closeWrite()) return endItem(BankResult::WriteFail);
  d_.remove(dst_);
  if (!d_.rename(path_, dst_)) return endItem(BankResult::WriteFail);
  return endItem(BankResult::Ok);
}
```

> `sampleKey` / `isSampleKey` из `sample_set.h`, `bankFileCurrent` из `bank_ops.h`. Сохранённый файл проекта — копия файла хранилища. `bankFileCurrent` проверяет `mtcr`, число кадров и размер ≥ `dataOffset + frames * 2`, поэтому JUNK-набивка ему не мешает, и ESP читает такой файл как обычный WAV.

**Step 4: Повторный запуск**

Run: `pio test -e native -f test_bank_ops`
Expected: PASS (старые и 8 новых).

### Task 13: Миграция сэмплов банка и `SampleBank::removeMany`

**Files:**
- Modify: `lib/core/src/sample_bank.h:55`, `lib/core/src/sample_bank.cpp:226` (`removeMany`)
- Modify: `lib/core/src/sample_store.h/.cpp` (`bankDropMigrated`)
- Test: `test/test_sample_bank/test_main.cpp`, `test/test_bank_ops/test_main.cpp`

**Step 1: Падающие тесты**

`test_sample_bank` (фикстуры файла: `RamFlash` и банк. Если имена другие, взять из соседнего теста `remove`):

```cpp
void test_remove_many_in_one_save() {
  int16_t d[100] = {};
  for (const char* n : {"a", "b", "c", "d"}) {
    TEST_ASSERT_TRUE(bank->begin(n, 100, 44100, 60));
    TEST_ASSERT_TRUE(bank->write(d, 100));
    TEST_ASSERT_TRUE(bank->commit());
  }
  const uint32_t gen = bank->generation();
  bool drop[kBankEntries] = {};
  drop[bank->find("b")] = true;
  drop[bank->find("d")] = true;
  TEST_ASSERT_TRUE(bank->removeMany(drop));
  TEST_ASSERT_EQUAL(2, bank->count());
  TEST_ASSERT_TRUE(bank->find("a") >= 0);
  TEST_ASSERT_EQUAL(-1, bank->find("b"));
  TEST_ASSERT_TRUE(bank->generation() != gen);
  SampleBank again(*flash);
  TEST_ASSERT_TRUE(again.mount());
  TEST_ASSERT_EQUAL(2, again.count());
}
```

`test_bank_ops`:

```cpp
void test_store_migrates_bank_samples() {
  const auto a = ramp(3000, 9);
  writePlainWav("/a.wav", a, 1, 32000);
  BankJob bj(*bank, *disk);  // the old way: into the flash bank
  bj.startImport("/a.wav", false, *proj);
  TEST_ASSERT_EQUAL(BankResult::Ok, run(bj));
  const uint32_t crc = bj.imported().crc;
  StoreJob j(*disk);
  TEST_ASSERT_EQUAL(BankResult::Ok, j.startMigrate(*bank));
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  TEST_ASSERT_TRUE(storeFileCurrent(*disk, crc, 3000));
  const int before = bank->count();
  TEST_ASSERT_EQUAL(1, bankDropMigrated(*bank, *disk));
  TEST_ASSERT_EQUAL(before - 1, bank->count());
  char key[kSampleNameMax + 1];
  sampleKey(crc, key);
  TEST_ASSERT_EQUAL(-1, bank->find(key));
}
```

**Step 2: Запуск, ожидается падение**

Run: `pio test -e native -f test_sample_bank`
Expected: FAIL (компиляция): `'class mt::SampleBank' has no member named 'removeMany'`

**Step 3: Минимальная реализация**

`sample_bank.h`, после `remove`:

```cpp
  // Removes every entry i with drop[i] (drop has count() items) in one table save. False if nothing
  // was stored (then nothing changes).
  bool removeMany(const bool* drop);
```

`sample_bank.cpp`:

```cpp
bool SampleBank::removeMany(const bool* drop) {
  if (adding_) return false;
  BankEntry old[kBankEntries];
  const int n = n_;
  int k = 0;
  for (int i = 0; i < n; ++i) {
    old[i] = e_[i];
    if (!drop[i]) e_[k++] = e_[i];
  }
  if (k == n) return true;
  n_ = k;
  if (saveTable()) return true;
  for (int i = 0; i < n; ++i) e_[i] = old[i];  // not stored: keep them
  n_ = n;
  return false;
}
```

(Если `remove` меняет `gen_` вне `saveTable`, сделать здесь так же: тест проверяет смену `generation()`.)

`sample_store.h`:

```cpp
// Drops every sample entry (8-hex name) of the flash bank whose store file is current, in one table
// save. The synth is parked (the bank changes). Returns how many.
int bankDropMigrated(SampleBank& b, BankDisk& d);
```

`sample_store.cpp`:

```cpp
int bankDropMigrated(SampleBank& b, BankDisk& d) {
  bool drop[kBankEntries] = {};
  int n = 0;
  for (int i = 0; i < b.count(); ++i) {
    const BankEntry* e = b.entry(i);
    if (!isSampleKey(e->name)) continue;
    const uint32_t crc = static_cast<uint32_t>(strtoul(e->name, nullptr, 16));
    if (storeFileCurrent(d, crc, e->frames)) {
      drop[i] = true;
      ++n;
    }
  }
  return n && b.removeMany(drop) ? n : 0;
}
```

**Step 4: Повторный запуск**

Run: `pio test -e native -f test_sample_bank` и `pio test -e native -f test_bank_ops`
Expected: PASS.

### Task 14: «Головы»: точки, раскладка записи, разбор

**Files:**
- Create: `lib/core/src/sample_heads.h`, `lib/core/src/sample_heads.cpp`
- Modify: `lib/core/src/sample_set.cpp:17-45` (`UsedKeys::has`: запись `"h"+crc` занята, пока crc в списке проекта)
- Test: `test/test_sample_heads/test_main.cpp` (Create)

**Step 1: Падающий тест**

```cpp
#include <string.h>
#include <unity.h>
#include <vector>
#include "sample_heads.h"
#include "sample_set.h"
#include "slices.h"

using namespace mt;

static Project* p;
constexpr uint32_t kCrc = 0xC0FFEE00u, kFrames = 100000;

static Instrument& sampler(int i) {
  Instrument& m = p->instruments[i];
  instrSetType(m, InstrType::Sample);
  strcpy(m.sample, "S");
  m.start = 0;
  m.end = 0xFFFF;
  m.loop = static_cast<uint8_t>(LoopMode::Off);
  m.reverse = false;
  m.sliceCount = 0;
  return m;
}

void setUp() {
  p = new Project();
  projSampleSet(*p, "S", kCrc, kFrames);
}
void tearDown() { delete p; }

void test_start_only() {
  sampler(0);
  HeadPoint h[kMaxHeads];
  TEST_ASSERT_EQUAL(1, headPoints(*p, kCrc, kFrames, 44100, h, kMaxHeads));
  TEST_ASSERT_EQUAL_UINT32(0, h[0].frame);
  TEST_ASSERT_EQUAL_UINT32(4410, h[0].len);
}

void test_forward_loop_adds_loop_start() {
  Instrument& m = sampler(0);
  m.loop = static_cast<uint8_t>(LoopMode::Forward);
  m.loopStart = 0x8000;
  HeadPoint h[kMaxHeads];
  TEST_ASSERT_EQUAL(2, headPoints(*p, kCrc, kFrames, 44100, h, kMaxHeads));
  TEST_ASSERT_EQUAL_UINT32(50000, h[1].frame);
  TEST_ASSERT_EQUAL_UINT32(4410, h[1].len);
}

void test_reverse_head_ends_at_start() {
  sampler(0).reverse = true;
  HeadPoint h[kMaxHeads];
  TEST_ASSERT_EQUAL(1, headPoints(*p, kCrc, kFrames, 44100, h, kMaxHeads));
  TEST_ASSERT_EQUAL_UINT32(kFrames - 4410, h[0].frame);
  TEST_ASSERT_EQUAL_UINT32(4410, h[0].len);
}

void test_slices_get_short_heads() {
  Instrument& m = sampler(0);
  m.start = 0x4000;  // the start head is not at 0
  TEST_ASSERT_TRUE(sliceInsert(m, 0x4000) >= 0);
  TEST_ASSERT_TRUE(sliceInsert(m, 0xC000) >= 0);
  HeadPoint h[kMaxHeads];
  const int n = headPoints(*p, kCrc, kFrames, 44100, h, kMaxHeads);
  uint32_t f, t;
  TEST_ASSERT_TRUE(sliceRegion(m, 1, kFrames, f, t));
  bool found = false;
  for (int i = 0; i < n; ++i) found |= h[i].frame == f && h[i].len == 1102;
  TEST_ASSERT_TRUE(found);
}

void test_same_points_merge_and_others_ignored() {
  sampler(0);
  sampler(1);  // same sample, same start
  Instrument& o = sampler(2);
  strcpy(o.sample, "OTHER");
  HeadPoint h[kMaxHeads];
  TEST_ASSERT_EQUAL(1, headPoints(*p, kCrc, kFrames, 44100, h, kMaxHeads));
  TEST_ASSERT_EQUAL(0, headPoints(*p, 0x1234u, kFrames, 44100, h, kMaxHeads));
}

void test_kit_lane_gets_frame_zero() {
  Instrument& k = p->instruments[3];
  instrSetType(k, InstrType::Kit);
  strcpy(k.kit[0].sample, "S");
  k.kit[0].instr = kNoInstr;
  HeadPoint h[kMaxHeads];
  TEST_ASSERT_EQUAL(1, headPoints(*p, kCrc, kFrames, 44100, h, kMaxHeads));
  TEST_ASSERT_EQUAL_UINT32(0, h[0].frame);
}

void test_cap_keeps_starts_first() {
  Instrument& m = sampler(0);
  m.start = 0x8000;
  for (int i = 0; i < kMaxSlices; ++i) sliceInsert(m, static_cast<uint16_t>(0x100 + i * 0x700));
  HeadPoint h[4];
  const int n = headPoints(*p, kCrc, kFrames, 44100, h, 4);
  TEST_ASSERT_EQUAL(4, n);
  bool start = false;
  for (int i = 0; i < n; ++i) start |= h[i].frame == fracToFrame(0x8000, kFrames);
  TEST_ASSERT_TRUE(start);
}

void test_layout_parse_round_trip() {
  HeadPoint pts[2] = {{0, 100}, {5000, 50}};
  int16_t index[kHeadIndexFrames];
  const uint32_t len = headLayout(pts, 2, index);
  TEST_ASSERT_EQUAL_UINT32(kHeadIndexFrames + 150, len);
  std::vector<int16_t> entry(len);
  memcpy(entry.data(), index, sizeof index);
  for (int i = 0; i < 100; ++i) entry[kHeadIndexFrames + i] = static_cast<int16_t>(i);
  for (int i = 0; i < 50; ++i) entry[kHeadIndexFrames + 100 + i] = static_cast<int16_t>(5000 + i);
  SampleHead h[kMaxHeads];
  int n = 0;
  TEST_ASSERT_TRUE(headParse(entry.data(), len, h, n));
  TEST_ASSERT_EQUAL(2, n);
  TEST_ASSERT_EQUAL_UINT32(5000, h[1].frame);
  TEST_ASSERT_EQUAL_INT16(5049, h[1].d[49]);
  TEST_ASSERT_TRUE(headSame(entry.data(), len, pts, 2));
  pts[1].len = 51;
  TEST_ASSERT_FALSE(headSame(entry.data(), len, pts, 2));
}

void test_parse_rejects_broken_index() {
  std::vector<int16_t> entry(kHeadIndexFrames + 10, 0);
  entry[0] = 1;  // one head ...
  const uint32_t frame = 0, len = 500, off = kHeadIndexFrames;  // ... longer than the entry
  memcpy(&entry[2], &frame, 4);
  memcpy(&entry[4], &len, 4);
  memcpy(&entry[6], &off, 4);
  SampleHead h[kMaxHeads];
  int n = 0;
  TEST_ASSERT_FALSE(headParse(entry.data(), static_cast<uint32_t>(entry.size()), h, n));
}

void test_head_entry_counts_as_used() {
  BankEntry e = {};
  headKey(kCrc, e.name);
  TEST_ASSERT_EQUAL_STRING("hc0ffee00", e.name);
  TEST_ASSERT_TRUE(bankEntryUsed(*p, e));
  headKey(0x1234u, e.name);
  TEST_ASSERT_FALSE(bankEntryUsed(*p, e));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_start_only);
  RUN_TEST(test_forward_loop_adds_loop_start);
  RUN_TEST(test_reverse_head_ends_at_start);
  RUN_TEST(test_slices_get_short_heads);
  RUN_TEST(test_same_points_merge_and_others_ignored);
  RUN_TEST(test_kit_lane_gets_frame_zero);
  RUN_TEST(test_cap_keeps_starts_first);
  RUN_TEST(test_layout_parse_round_trip);
  RUN_TEST(test_parse_rejects_broken_index);
  RUN_TEST(test_head_entry_counts_as_used);
  return UNITY_END();
}
```

(`bankEntryUsed` объявлена в `sample_set.h`. Если нет, объявить рядом с `bankClearUnused`. Имя значения KIT в `InstrType` сверить с `model.h`.)

**Step 2: Запуск, ожидается падение**

Run: `pio test -e native -f test_sample_heads`
Expected: FAIL: `fatal error: sample_heads.h: No such file or directory`

**Step 3: Минимальная реализация**

`lib/core/src/sample_heads.h`:

```cpp
#pragma once
#include <stdint.h>
#include "model.h"
#include "sample_source.h"

namespace mt {

// Heads: runs of a store sample kept in the flash bank (entry "h" + crc), so notes start without
// waiting for the card. The entry is an index (kHeadIndexFrames int16: count, 0, then frame, len,
// offset as uint32 per head) followed by the heads' frames.
constexpr uint32_t kHeadStartMs = 100;  // a note start, a forward loop's start
constexpr uint32_t kHeadSliceMs = 25;   // a slice start
constexpr int kHeadIndexFrames = 256;   // 512 bytes

struct HeadPoint {
  uint32_t frame = 0, len = 0;
};

// Runs of crc (frames long at rate) worth a head: per instrument of m playing it, its start and the
// start of a forward loop (played backwards: the run ending there), then its slice starts; frame 0
// for KIT sampler lanes. Taken in that order up to max, then sorted and merged. Returns the count.
int headPoints(const SynthModel& m, uint32_t crc, uint32_t frames, uint32_t rate, HeadPoint* out, int max);
void headKey(uint32_t crc, char out[kSampleNameMax + 1]);  // "h" + 8 hex
// The index for heads p; returns the entry's length in frames (index included).
uint32_t headLayout(const HeadPoint* p, int n, int16_t index[kHeadIndexFrames]);
// Heads of an entry (pointers into d); false if the index does not fit the entry.
bool headParse(const int16_t* d, uint32_t entryFrames, SampleHead* out, int& n);
// The entry holds exactly the runs p.
bool headSame(const int16_t* d, uint32_t entryFrames, const HeadPoint* p, int n);

}  // namespace mt
```

`lib/core/src/sample_heads.cpp`:

```cpp
#include "sample_heads.h"
#include <string.h>
#include "sample_region.h"
#include "sample_set.h"

namespace mt {

namespace {
void put32(int16_t* at, uint32_t v) { memcpy(at, &v, 4); }
uint32_t get32(const int16_t* at) {
  uint32_t v;
  memcpy(&v, at, 4);
  return v;
}
}  // namespace

int headPoints(const SynthModel& m, uint32_t crc, uint32_t frames, uint32_t rate, HeadPoint* out, int max) {
  if (!frames || max <= 0) return 0;
  const uint32_t lStart = rate * kHeadStartMs / 1000, lSlice = rate * kHeadSliceMs / 1000;
  int n = 0;
  auto push = [&](uint32_t at, uint32_t len, bool back) {
    if (n >= max || at >= frames) return;
    HeadPoint h;
    if (back) {
      h.frame = at + 1 >= len ? at + 1 - len : 0;
      h.len = at + 1 - h.frame;
    } else {
      h.frame = at;
      h.len = frames - at < len ? frames - at : len;
    }
    out[n++] = h;
  };
  auto uses = [&](const char* name) {
    const int k = projSampleFind(m, name);
    return k >= 0 && m.samples[k].crc == crc && m.samples[k].frames == frames;
  };
  for (int pass = 0; pass < 2; ++pass)
    for (const Instrument& ins : m.instruments) {
      if (ins.type == InstrType::Kit) {
        if (pass == 0)
          for (const KitLane& l : ins.kit)
            if (l.instr == kNoInstr && uses(l.sample)) push(0, lStart, false);
        continue;
      }
      if (ins.type != InstrType::Sample || !uses(ins.sample)) continue;
      if (pass == 0) {
        const SampleRegion g = sampleRegion(ins, -1, frames, 0);
        push(static_cast<uint32_t>(g.pos >> 32), lStart, g.dir < 0);
        if (g.loopMode == static_cast<uint8_t>(LoopMode::Forward))
          push(g.dir < 0 ? g.loopHi - 1 : g.loopLo, lStart, g.dir < 0);
      } else {
        const int cnt = ins.sliceCount < kMaxSlices ? ins.sliceCount : kMaxSlices;
        for (int i = 0; i < cnt; ++i) {
          const SampleRegion g = sampleRegion(ins, i, frames, 0);
          push(static_cast<uint32_t>(g.pos >> 32), lSlice, g.dir < 0);
        }
      }
    }
  // Sort by frame (n <= kMaxHeads: insertion sort), then merge overlapping / touching runs.
  for (int i = 1; i < n; ++i)
    for (int j = i; j > 0 && out[j - 1].frame > out[j].frame; --j) {
      const HeadPoint t = out[j];
      out[j] = out[j - 1];
      out[j - 1] = t;
    }
  int k = 0;
  for (int i = 0; i < n; ++i) {
    if (k && out[i].frame <= out[k - 1].frame + out[k - 1].len) {
      const uint32_t end = out[i].frame + out[i].len, have = out[k - 1].frame + out[k - 1].len;
      if (end > have) out[k - 1].len = end - out[k - 1].frame;
    } else {
      out[k++] = out[i];
    }
  }
  return k;
}

void headKey(uint32_t crc, char out[kSampleNameMax + 1]) {
  out[0] = 'h';
  sampleKey(crc, out + 1);
}

uint32_t headLayout(const HeadPoint* p, int n, int16_t index[kHeadIndexFrames]) {
  memset(index, 0, kHeadIndexFrames * sizeof(int16_t));
  index[0] = static_cast<int16_t>(n);
  uint32_t off = kHeadIndexFrames;
  for (int i = 0; i < n; ++i) {
    put32(index + 2 + 6 * i, p[i].frame);
    put32(index + 4 + 6 * i, p[i].len);
    put32(index + 6 + 6 * i, off);
    off += p[i].len;
  }
  return off;
}

bool headParse(const int16_t* d, uint32_t entryFrames, SampleHead* out, int& n) {
  n = 0;
  if (!d || entryFrames < static_cast<uint32_t>(kHeadIndexFrames)) return false;
  const int cnt = d[0];
  if (cnt < 0 || cnt > kMaxHeads) return false;
  for (int i = 0; i < cnt; ++i) {
    const uint32_t frame = get32(d + 2 + 6 * i), len = get32(d + 4 + 6 * i), off = get32(d + 6 + 6 * i);
    if (off < static_cast<uint32_t>(kHeadIndexFrames) || off > entryFrames || len > entryFrames - off) return false;
    out[i].frame = frame;
    out[i].len = len;
    out[i].d = d + off;
  }
  n = cnt;
  return true;
}

bool headSame(const int16_t* d, uint32_t entryFrames, const HeadPoint* p, int n) {
  SampleHead h[kMaxHeads];
  int k = 0;
  if (!headParse(d, entryFrames, h, k) || k != n) return false;
  for (int i = 0; i < n; ++i)
    if (h[i].frame != p[i].frame || h[i].len != p[i].len) return false;
  return true;
}

}  // namespace mt
```

`sample_set.cpp`, `UsedKeys::has`, перед `if (!isSampleKey(e.name)) return false;`:

```cpp
    if (e.name[0] == 'h' && isSampleKey(e.name + 1)) {  // heads of a listed sample
      const uint32_t c = static_cast<uint32_t>(strtoul(e.name + 1, nullptr, 16));
      for (int i = 0; i < n; ++i)
        if (crc[i] == c) return true;
      return false;
    }
```

**Step 4: Повторный запуск**

Run: `pio test -e native -f test_sample_heads` и `pio test -e native -f test_sample_set`
Expected: PASS.

### Task 15: Teensy: хранилище вместо банка для сэмплов, «головы», инструменты, миграция; ESP ждёт инструменты

**Files:**
- Modify: `src/teensy/bank.cpp` (диск: `SdWrite::reserve/seek`, обрезка при закрытии; `StoreJob` рядом с `BankJob`; обработчики `BankImport` / `BankSync` / `AssetsSave` / `SampleInfo` / `BankIndex` / `BankClear`; запись голов; миграция при загрузке)
- Modify: `src/teensy/bank.h` (`sampleData` → `sampleAccess`)
- Modify: `src/teensy/stream.cpp` (`rescan` подхватывает головы, `add` для новых файлов)
- Modify: `src/teensy/sample_tools.cpp` (задачи с отложенным ответом)
- Modify: `src/teensy/render_server.cpp:222` (ресемпл в хранилище, а не в банк)
- Modify: `lib/core/src/bank_ops.h/.cpp` (`BankJob::startSync/startSave(project, m, bool samples = true)`)
- Modify: `src/link/bank_client.cpp:~330-380` (`wavePeaks`, `onsets` через `requestLong`)
- Test: `test/test_bank_ops/test_main.cpp` (флаг `samples`)

**Step 1: Падающий нативный тест** (единственная часть с логикой в `lib/core`)

```cpp
void test_sync_wavetables_only_leaves_samples() {
  disk->mkdir("/projects/P");
  const auto a = ramp(2000, 4);
  writePlainWav("/projects/P/KICK.wav", a, 1, 44100);
  projSampleSet(*proj, "KICK", sampleCrc(a.data(), 2000, 0), 2000);
  BankJob j(*bank, *disk);
  TEST_ASSERT_EQUAL(BankResult::Ok, j.startSync("P", *proj, false));
  TEST_ASSERT_EQUAL(BankResult::Ok, run(j));
  TEST_ASSERT_EQUAL(0, j.missing());  // samples are not its business
  char key[kSampleNameMax + 1];
  sampleKey(sampleCrc(a.data(), 2000, 0), key);
  TEST_ASSERT_EQUAL(-1, bank->find(key));
}
```

Run: `pio test -e native -f test_bank_ops`
Expected: FAIL (компиляция): `no matching function for call to 'mt::BankJob::startSync(const char [2], mt::Project&, bool)'`

**Step 2: Реализация в `bank_ops`**

`BankJob`: поле `bool samples_ = true;`. `startSync` / `startSave` получают `bool samples = true` и сохраняют его. В `nextItem` (стр. 347) элементы-сэмплы пропускаются, когда `!samples_`, — так же, как пропускаются уже кэшированные: без отметки `missing`. Повторный запуск: PASS.

**Step 3: Teensy, диск**

`SdWrite` в `bank.cpp`:

```cpp
  bool reserve(uint32_t bytes) override { return f.preAllocate(bytes); }
  bool seek(uint32_t pos) override {
    if (f.curPosition() > top) top = f.curPosition();
    return f.seekSet(pos);
  }
  uint64_t top = 0;  // largest position written: the size at close
```

В `SdDisk::closeWrite()` перед `close()`: `if (w.f.curPosition() > w.top) w.top = w.f.curPosition(); w.f.truncate(w.top); w.top = 0;`. Это отрезает хвост `preAllocate`, но оставляет непрерывный участок. В `openWrite` обнулять `top`.

**Step 4: Teensy, работы банка**

- `mt::StoreJob store(disk);` рядом с `job`, `store.setOnAdded([](uint32_t crc, void*) { stream::add(crc); }, nullptr);`.
- `BankImport` для сэмпла (`q.kind == 0`): `store.startImport(q.path, q.hasCrc ? &q.crc : nullptr)` **без** `park`. Для wavetable — `job.startImport` под `park`, как было.
- `BankSync`: `park(true)`; затем `store.startSync(q.project, *model)`; после неё — `job.startSync(q.project, *model, false)` (таблицы); затем запись голов (Step 5); `stream::rescan(*model)`; `park(false)`; ответ `BankSyncRep`, где маска `samples` — от `store.sampleMissing(i)`, `wavetables` — от `job.wtMissing(i)`. Порядок: две задачи подряд в `bgStep` (`store` первой) и один ответ в конце — по образцу `jobDone`.
- `AssetsSave`: `store.startSave` без `park`, затем `job.startSave(q.project, *model, false)`.
- `SampleInfo` / `BankIndex`: сэмпл «в кэше», если `stream::table().find(crc) >= 0` и `frames` совпадают. `free` / `capacity` банка остаются для таблиц и голов.
- `BankClear`: кроме `bankClearUnused` удалить файлы хранилища, не входящие в модель: `SD.sdfs.open(kStoreDir)` + `openNext`, имя `XXXXXXXX.wav` / `.pk` с crc не из `model->samples` → `remove` (риск: см. «Открытые вопросы»).
- `bgStep()`: шаг `store`, если занят, иначе шаг `job`. Возвращает `true`, если что-то сделано.

**Step 5: Teensy, запись голов** (под `park`, в конце `BankSync` и после `BankImport` сэмпла, если сэмпл уже в модели)

```cpp
// Writes / refreshes the heads of the model's store samples, then hands them to the table.
void writeHeads() {
  for (int k = 0; k < model->sampleCount; ++k) {
    const mt::ProjSample& ps = model->samples[k];
    const int t = stream::table().find(ps.crc);
    if (t < 0) continue;
    const mt::StoreEntry& e = stream::table().at(t);
    mt::HeadPoint pts[mt::kMaxHeads];
    const int n = mt::headPoints(*model, ps.crc, e.frames, e.rate, pts, mt::kMaxHeads);
    char key[mt::kSampleNameMax + 1];
    mt::headKey(ps.crc, key);
    int b = theBank.find(key);
    if (b >= 0 && mt::headSame(theBank.data(b), theBank.entry(b)->frames, pts, n)) continue;
    if (b >= 0) theBank.remove(b);
    if (!n) continue;
    int16_t index[mt::kHeadIndexFrames];
    const uint32_t len = mt::headLayout(pts, n, index);
    if (!mt::bankMakeRoom(theBank, *model, len, keepalive, nullptr) || !theBank.begin(key, len, 0, 60)) {
      link::log("heads: no room for %s (%lu frames)", key, static_cast<unsigned long>(len));
      continue;
    }
    bool ok = theBank.write(index, mt::kHeadIndexFrames);
    for (int i = 0; ok && i < n; ++i) ok = copyFrames(ps.crc, pts[i].frame, pts[i].len);  // store -> bank
    if (!ok || !theBank.commit()) theBank.abort();
  }
}
```

`copyFrames(crc, frame, len)` читает кадры кусками по 2 КБ из файла хранилища через `stream::reader()` (по блокам: блок `frame / 512` и далее, начальное смещение внутри блока отрезается) и пишет `theBank.write`.

В `stream::rescan` (Task 9) после `theTable.add` для каждого сэмпла:

```cpp
  char key[mt::kSampleNameMax + 1];
  mt::headKey(crc, key);
  const int b = bank::flash().find(key);
  mt::SampleHead hs[mt::kMaxHeads];
  int n = 0;
  if (b >= 0 && mt::headParse(bank::flash().data(b), bank::flash().entry(b)->frames, hs, n) && n)
    theTable.setHeads(i, hs, n);
```

(`bank::flash()` — новый доступ к `theBank` из `bank.h`.)

**Step 6: Teensy, миграция при загрузке**

В `bank::begin`, если в банке есть хоть одна запись с 8-hex именем (`isSampleKey`), запускается `store.startMigrate(theBank)` фоновой задачей (`bgStep`). В конце, под `park`: `bankDropMigrated(theBank, disk)`, `link::log("migrated %d samples to the store", n)`, затем `stream::rescan(*model)`. До конца миграции сэмплы играют из flash (источник legacy в `StoreSampleSource`).

**Step 7: Teensy, инструменты** (`sample_tools.cpp`)

- `bank::sampleAccess(index, crc, frames, rate, whole)` возвращает данные сэмпла по хранилищу. `whole` — указатель PSRAM (этап 4) или legacy во flash, иначе `nullptr`.
- `WavePeaks`: при `whole` — как раньше, ответ сразу. Иначе задача: `PeakReader` поверх `.pk` (`FsFile` + `seekSet(first * 2)`) и кадров (`stream::reader()`). `wavePeaksRead` за одну фоновую порцию на ≤8 колонок; когда все колонки готовы — ответ с тем же `seq` (по образцу `bank.cpp` `answer`). Пока идёт — `Progress` раз в 200 мс.
- `Onsets`: задача `OnsetScan` (статический объект) кормится кусками по 2 КБ из файла хранилища за фоновую порцию. Результат кэшируется по `(crc, frames)` вместо `(d, gen)`.
- Обёртка `BgWork` для инструментов регистрируется в `stream::addBackground`.

**Step 8: Рендер** (`render_server.cpp:222`)

`bank::writeFrames(...)` (во flash) заменяется записью в хранилище: `StoreJob` не подходит, кадры генерирует рендер. Поэтому в `sample_store` добавить свободную функцию `storeWriteFrames(BankDisk&, uint32_t frames, uint32_t rate, FrameFill fill, void* ctx, ImportOut& out)` — тот же путь, что `beginWrite` / `convertStep` / `finishConvert`, с `fill` вместо чтения. Нативный тест в `test_bank_ops`:

```cpp
void test_store_write_frames() {
  FillCtx c;
  ImportOut o;
  TEST_ASSERT_EQUAL(BankResult::Ok, storeWriteFrames(*disk, 5000, 44100, fillRamp, &c, o));
  std::vector<int16_t> want(5000);
  for (int i = 0; i < 5000; ++i) want[i] = static_cast<int16_t>(i * 3);
  TEST_ASSERT_EQUAL_HEX32(sampleCrc(want.data(), 5000, 0), o.crc);
  TEST_ASSERT_TRUE(storeFileCurrent(*disk, o.crc, 5000));
}
```

Run: `pio test -e native -f test_bank_ops` → сначала FAIL (`'storeWriteFrames' was not declared`), после реализации PASS. После записи — `stream::add(o.crc)`.

**Step 9: ESP** (`src/link/bank_client.cpp`)

`wavePeaks` и `onsets` переходят с `shortCall(..., 300 / 1000)` на `slink::requestLong(...)` с `onProgress` (как `importToCache`): Teensy отвечает, когда задача закончена, и шлёт `Progress`, пока она идёт.

**Step 10: Сборка и desk-проверка** (голый Teensy с картой, прошивка `teensy41-desk`)

Run: `pio run -e wt32 -e teensy41 -e teensy41-desk -e teensy41-bench` → SUCCESS.

1. Первая загрузка после обновления, банк со старыми сэмплами: `LD status 30`. В логе `migrated N samples to the store`, затем `store: N of N samples`. `LD card ls /mt-store` — пары `XXXXXXXX.wav` / `.pk`.
2. `LD card put long.wav /desk/long.wav` (WAV 60 с, стерео 48 кГц), `LD bank /desk/long.wav`. Строки `  done / total` идут, звук во время импорта не прерывается (импорт без `park`). Итог: `import: Ok crc XXXXXXXX, 2646000 frames at 44100 Hz`.
3. `LD bank`: `bank ... N KB free` вырос на объём мигрированных сэмплов. Записей `h........` столько, сколько сэмплов модели в хранилище.
4. `LD pnote 0 60 3000` на инструменте с длинным сэмплом — звучит все 3 с. `LD status 3` во время ноты: `lost 0`.

### Проверка этапа 3

Run: `pio run -e wt32 -e teensy41 -e teensy41-desk -e teensy41-bench`
Expected: SUCCESS ×4.

Run: `pio test -e native`
Expected: PASS, добавились `test_sample_heads` (10), тесты `test_bank_ops` / `test_wav` / `test_wave_peaks` / `test_onset` / `test_sample_bank`.

---

## Этап 4 — PSRAM: своя инициализация, самотест, сторож, кэш целых сэмплов

После этапа короткие сэмплы проекта после загрузки лежат в PSRAM целиком и в лимит 16 стрим-голосов не входят. Если чипа нет, самотест не прошёл или сторож поймал сбой, всё играет с SD, как после этапа 3. Логика (арена, самотест, сторож, порядок наполнения, проверка CRC) живёт в `lib/core` и проверена нативно. На Teensy остаются регистры FlexSPI2 и обслуживание кэша.

**MPU.** `configure_cache()` (`cores/teensy4/startup.c:323-324`) ставит регион `0x70000000`, `SIZE_32M`, `MEM_CACHE_WBWA | READWRITE | NOEXEC` безусловно, не глядя на `external_psram_size`. Свой регион не нужен, MPU не трогаем. Регион кэшируемый (write-back), поэтому самотест, сторож и проверка копии сначала выталкивают строки в чип (`arm_dcache_flush_delete`), потом сбрасывают их (`arm_dcache_delete`) и только затем читают. Иначе они проверяли бы кэш, а не чип.

**Почему своя инициализация.** На CS0 чипа нет, поэтому startup ставит `external_psram_size = 0` и до CS1 не доходит (`startup.c:537`). При этом LUT FlexSPI2 он уже записал (последовательности 0–6), и `FLSHA1CR2` / `FLSHA2CR2` указывают на AHB-последовательности 5 / 6. Свой код только пробует чип как второе устройство, а потом отображает его с `0x70000000` (размер CS0 = 0). Последовательность проверена на этой плате полным memtest.

### Task 16: `PsramArena`, самотест, сторож (lib/core)

**Files:**
- Create: `lib/core/src/psram_cache.h`, `lib/core/src/psram_cache.cpp`
- Test: `test/test_psram_cache/test_main.cpp` (Create)

**Step 1: Падающий тест**

```cpp
#include <string.h>
#include <unity.h>
#include <vector>
#include "psram_cache.h"

using namespace mt;

// RAM in place of the chip: no cache, flush / drop only count, unless a fault is set.
struct FakeHw : PsramHw {
  uint32_t* mem = nullptr;
  int aliasBit = -1;  // address bit stuck low: word 1 << bit reads what word 0 holds
  bool flip = false;  // the next drop flips bit 0 of the first dropped word
  int flushes = 0, drops = 0;
  void flush(const void*, uint32_t) override { ++flushes; }
  void drop(const void* p, uint32_t bytes) override {
    ++drops;
    if (aliasBit >= 0) mem[1u << aliasBit] = mem[0];
    if (flip && bytes) {
      flip = false;
      *static_cast<uint32_t*>(const_cast<void*>(p)) ^= 1;
    }
  }
};

static std::vector<uint32_t> chip;
static FakeHw hw;

void setUp() {
  chip.assign(256 * 1024, 0);  // 1 MB stands for the chip
  hw = FakeHw();
  hw.mem = chip.data();
}
void tearDown() {}

void test_arena_aligns_and_fills() {
  PsramArena a;
  a.init(chip.data(), 1000);
  uint8_t* base = reinterpret_cast<uint8_t*>(chip.data());
  TEST_ASSERT_EQUAL_PTR(base, a.alloc(10));
  TEST_ASSERT_EQUAL_PTR(base + 32, a.alloc(40));  // next cache line
  TEST_ASSERT_EQUAL_UINT32(72, a.used());
  TEST_ASSERT_NULL(a.alloc(1000 - 96 + 1));
  TEST_ASSERT_EQUAL_PTR(base + 96, a.alloc(1000 - 96));
  TEST_ASSERT_EQUAL_UINT32(1000, a.used());
  TEST_ASSERT_NULL(a.alloc(1));
  a.reset();
  TEST_ASSERT_EQUAL_UINT32(0, a.used());
  TEST_ASSERT_EQUAL_PTR(base, a.alloc(1));
}

void test_selftest_passes_on_good_memory() {
  TEST_ASSERT_TRUE(psramSelfTest(chip.data(), chip.size() * 4, hw));
  TEST_ASSERT_TRUE(hw.flushes > 0);
  TEST_ASSERT_TRUE(hw.drops > 0);  // read back past the cache
}

void test_selftest_finds_stuck_address_line() {
  hw.aliasBit = 17;  // word 128 K = byte 512 K
  TEST_ASSERT_FALSE(psramSelfTest(chip.data(), chip.size() * 4, hw));
}

void test_selftest_finds_bad_bit() {
  hw.flip = true;
  TEST_ASSERT_FALSE(psramSelfTest(chip.data(), chip.size() * 4, hw));
}

void test_watch_every_100ms_catches_a_flip_and_stays_failed() {
  PsramWatch w;
  w.init(chip.data() + 1024, hw);  // a 4 KB guard
  TEST_ASSERT_TRUE(w.step(0));     // the first call checks
  const int d = hw.drops;
  TEST_ASSERT_TRUE(w.step(50));
  TEST_ASSERT_EQUAL(d, hw.drops);  // not due: the chip is not touched
  TEST_ASSERT_TRUE(w.step(100));
  TEST_ASSERT_TRUE(hw.drops > d);
  hw.flip = true;
  TEST_ASSERT_FALSE(w.step(200));
  TEST_ASSERT_TRUE(w.failed());
  const int d2 = hw.drops;
  TEST_ASSERT_FALSE(w.step(10000));  // back only after a reboot
  TEST_ASSERT_EQUAL(d2, hw.drops);
}

void test_watch_pattern_changes_every_check() {
  PsramWatch w;
  uint32_t* g = chip.data() + 1024;
  w.init(g, hw);
  w.step(0);
  const uint32_t a = g[0];
  w.step(100);
  TEST_ASSERT_NOT_EQUAL(a, g[0]);  // a stale word cannot pass for a fresh one
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_arena_aligns_and_fills);
  RUN_TEST(test_selftest_passes_on_good_memory);
  RUN_TEST(test_selftest_finds_stuck_address_line);
  RUN_TEST(test_selftest_finds_bad_bit);
  RUN_TEST(test_watch_every_100ms_catches_a_flip_and_stays_failed);
  RUN_TEST(test_watch_pattern_changes_every_check);
  return UNITY_END();
}
```

**Step 2: Запуск, ожидается падение**

Run: `pio test -e native -f test_psram_cache`
Expected: FAIL: `fatal error: psram_cache.h: No such file or directory`

**Step 3: Реализация**

`lib/core/src/psram_cache.h`:

```cpp
#pragma once
#include <stdint.h>

namespace mt {

// Optional PSRAM (one 8 MB chip on FlexSPI2): an arena for whole-sample copies, a boot self-test and a
// watchdog on a reserved guard region. The chip sits behind the data cache (write-back), so the checks
// push their writes to the chip and drop the lines before they read back.
constexpr uint32_t kPsramBytes = 8u << 20;
constexpr uint32_t kPsramGuardBytes = 4096;  // the chip's last 4 KB: the watchdog's
constexpr uint32_t kPsramAlign = 32;         // cache line
constexpr uint32_t kWatchMs = 100;
constexpr int kGuardWords = 64;              // checked words, one per 64 B of the guard
constexpr uint32_t kSelfTestBytes = 64 * 1024;

// Cache maintenance (Teensy: arm_dcache_*; host: nothing).
struct PsramHw {
  virtual void flush(const void* p, uint32_t bytes) = 0;  // dirty lines to the chip, then dropped
  virtual void drop(const void* p, uint32_t bytes) = 0;   // lines dropped: the next reads hit the chip
};

// Bump allocator over the copies; reset() only while nothing reads them (synth parked).
class PsramArena {
 public:
  void init(void* base, uint32_t bytes);  // base: 32-byte aligned (0x70000000)
  // Offset from base aligned to kPsramAlign; nullptr if it does not fit.
  void* alloc(uint32_t bytes);
  void reset() { used_ = 0; }
  uint32_t used() const { return used_; }
  uint32_t size() const { return size_; }

 private:
  uint8_t* base_ = nullptr;
  uint32_t size_ = 0, used_ = 0;
};

// Address lines (a marker at every power-of-two word, word 0 must survive them all), then
// kSelfTestBytes of patterns at base. Destroys what is there. False on any mismatch.
bool psramSelfTest(uint32_t* base, uint32_t bytes, PsramHw& hw);

// Every kWatchMs writes a fresh pattern into the guard, pushes it to the chip and reads it back.
// After a mismatch it stays failed: PSRAM comes back only after a reboot.
class PsramWatch {
 public:
  void init(uint32_t* guard, PsramHw& hw);  // guard: kPsramGuardBytes, nothing else uses it
  // False once failed. Not due: true without touching the chip.
  bool step(uint32_t nowMs);
  bool failed() const { return failed_; }

 private:
  volatile uint32_t* g_ = nullptr;
  PsramHw* hw_ = nullptr;
  uint32_t last_ = 0, seed_ = 0;
  bool started_ = false, failed_ = false;
};

}  // namespace mt
```

`lib/core/src/psram_cache.cpp`:

```cpp
#include "psram_cache.h"

namespace mt {

namespace {

constexpr uint32_t kGuardStride = kPsramGuardBytes / 4 / kGuardWords;  // words between checked ones
static_assert(kGuardStride >= 1, "guard words");

// A word that depends on its index and the pass: a stuck, shorted or stale word shows.
uint32_t mix(uint32_t i, uint32_t seed) {
  uint32_t x = i * 0x9E3779B1u ^ seed * 0x85EBCA6Bu;
  x ^= x >> 15;
  return x * 0x2C1B3C6Du;
}

}  // namespace

void PsramArena::init(void* base, uint32_t bytes) {
  base_ = static_cast<uint8_t*>(base);
  size_ = bytes;
  used_ = 0;
}

void* PsramArena::alloc(uint32_t bytes) {
  const uint32_t at = (used_ + kPsramAlign - 1) & ~(kPsramAlign - 1);
  if (at > size_ || bytes > size_ - at) return nullptr;
  used_ = at + bytes;
  return base_ + at;
}

bool psramSelfTest(uint32_t* base, uint32_t bytes, PsramHw& hw) {
  volatile uint32_t* m = base;
  const uint32_t words = bytes / 4;
  // Address lines: a stuck or shorted line makes two markers share a word.
  m[0] = 0x5A5A5A5Au;
  hw.flush(base, 4);
  for (uint32_t k = 1; k < words; k <<= 1) {
    m[k] = 0xA5000000u | k;
    hw.flush(base + k, 4);
  }
  hw.drop(base, 4);
  if (m[0] != 0x5A5A5A5Au) return false;
  for (uint32_t k = 1; k < words; k <<= 1) {
    hw.drop(base + k, 4);
    if (m[k] != (0xA5000000u | k)) return false;
  }
  // Patterns: every bit both ways, then a word-unique one.
  const uint32_t n = (kSelfTestBytes < bytes ? kSelfTestBytes : bytes) / 4;
  for (uint32_t pass = 0; pass < 3; ++pass) {
    auto want = [&](uint32_t i) { return pass == 0 ? 0x55555555u : (pass == 1 ? 0xAAAAAAAAu : mix(i, 1)); };
    for (uint32_t i = 0; i < n; ++i) m[i] = want(i);
    hw.flush(base, n * 4);
    hw.drop(base, n * 4);
    for (uint32_t i = 0; i < n; ++i)
      if (m[i] != want(i)) return false;
  }
  return true;
}

void PsramWatch::init(uint32_t* guard, PsramHw& hw) {
  g_ = guard;
  hw_ = &hw;
  last_ = seed_ = 0;
  started_ = failed_ = false;
}

bool PsramWatch::step(uint32_t nowMs) {
  if (failed_) return false;
  if (!g_ || (started_ && nowMs - last_ < kWatchMs)) return true;
  started_ = true;
  last_ = nowMs;
  ++seed_;
  for (int i = 0; i < kGuardWords; ++i) g_[i * kGuardStride] = mix(static_cast<uint32_t>(i), seed_);
  uint32_t* const g = const_cast<uint32_t*>(g_);
  hw_->flush(g, kPsramGuardBytes);
  hw_->drop(g, kPsramGuardBytes);
  for (int i = 0; i < kGuardWords; ++i)
    if (g_[i * kGuardStride] != mix(static_cast<uint32_t>(i), seed_)) {
      failed_ = true;
      return false;
    }
  return true;
}

}  // namespace mt
```

**Step 4: Повторный запуск**

Run: `pio test -e native -f test_psram_cache`
Expected: PASS (6 тестов).

### Task 17: `PsramFill` — наполнение от коротких к длинным с проверкой CRC (lib/core)

**Files:**
- Modify: `lib/core/src/psram_cache.h`, `lib/core/src/psram_cache.cpp` (`psramBytes`, `psramFillOrder`, `PsramFill`)
- Test: `test/test_psram_cache/test_main.cpp`

`PsramFill` — фоновая работа (`BgWork`) планировщика: одна загрузка (≤ `kLoadMax` блоков, ≤4 КБ) за порцию. Копия попадает в `StoreTable::setPsram` (и тем самым к голосам через `StoreSampleSource`) только после того, как CRC, пересчитанный по самой копии, совпал с ключом. Каждая скопированная порция сразу выталкивается в чип (`flush`), поэтому проверка читает чип, а не кэш. Без `pool.psramOk` наполнение не идёт.

**Step 1: Падающие тесты** (дописать в `test/test_psram_cache/test_main.cpp`)

В начало файла:

```cpp
#include "sample_set.h"
#include "sample_store.h"
#include "stream_pool.h"
```

Перед `main`:

```cpp
// Store files by crc, served block by block like the card.
struct Files : BlockReader {
  std::vector<std::vector<int16_t>> d;
  std::vector<uint32_t> crc;
  int reads = 0;
  bool fail = false;
  uint32_t add(uint32_t frames, int seed) {
    std::vector<int16_t> v(frames);
    for (uint32_t i = 0; i < frames; ++i) v[i] = static_cast<int16_t>(i * 7 + seed * 1000);
    d.push_back(v);
    crc.push_back(sampleCrc(v.data(), frames));
    return crc.back();
  }
  bool read(uint32_t key, uint32_t block, int n, int16_t* out) override {
    ++reads;
    if (fail) return false;
    for (size_t k = 0; k < crc.size(); ++k)
      if (crc[k] == key) {
        for (int i = 0; i < n * kStreamBlock; ++i) {
          const uint32_t f = block * kStreamBlock + i;
          out[i] = f < d[k].size() ? d[k][f] : 0;
        }
        return true;
      }
    return false;
  }
};

static StoreTable table;
static StreamPool fpool;
static Files files;
static PsramArena arena;

// Store entries for the files (sector 1: contiguous), the fill over the 1 MB fake chip.
static void prepare(uint32_t arenaBytes) {
  table.clear();
  for (size_t k = 0; k < files.crc.size(); ++k)
    table.add(files.crc[k], static_cast<uint32_t>(files.d[k].size()), 44100, 1);
  fpool.psramOk = true;
  arena.init(chip.data(), arenaBytes);
}
static int run(PsramFill& f) {
  int n = 0;
  while (f.step()) ++n;
  return n;
}
static const int16_t* copyOf(uint32_t crc) { return table.at(table.find(crc)).psram; }

void test_fill_order_shortest_first_until_full() {
  const uint32_t frames[] = {5000, 1000, 3000, 1000};
  int order[4];
  TEST_ASSERT_EQUAL_UINT32(2048, psramBytes(1000));  // whole blocks
  TEST_ASSERT_EQUAL(3, psramFillOrder(frames, 4, 2048 + 2048 + 6144, order));
  TEST_ASSERT_EQUAL(1, order[0]);
  TEST_ASSERT_EQUAL(3, order[1]);  // equal lengths keep their order
  TEST_ASSERT_EQUAL(2, order[2]);
}

void test_fill_copies_shortest_first_and_verifies() {
  files = Files();
  const uint32_t a = files.add(5000, 1), b = files.add(1000, 2), c = files.add(3000, 3);
  prepare(1u << 20);
  PsramFill f(table, fpool, files, arena, hw);
  const uint32_t list[] = {a, b, c, b};  // b twice: one copy
  f.restart(list, 4);
  TEST_ASSERT_EQUAL(3, f.planned());
  TEST_ASSERT_NULL(copyOf(b));  // nothing before the crc check
  run(f);
  TEST_ASSERT_EQUAL(3, f.ready());
  TEST_ASSERT_EQUAL(0, f.bad());
  TEST_ASSERT_TRUE(copyOf(b) < copyOf(c) && copyOf(c) < copyOf(a));
  TEST_ASSERT_EQUAL_INT16_ARRAY(files.d[0].data(), copyOf(a), 5000);
  TEST_ASSERT_EQUAL_UINT32(f.plannedBytes(), f.doneBytes());
  TEST_ASSERT_TRUE(hw.flushes > 0);
}

void test_fill_stops_at_capacity() {
  files = Files();
  const uint32_t a = files.add(5000, 1), b = files.add(1000, 2), c = files.add(3000, 3);
  prepare(9000);  // 2048 + 6144 fit, 10240 does not
  PsramFill f(table, fpool, files, arena, hw);
  const uint32_t list[] = {a, b, c};
  f.restart(list, 3);
  run(f);
  TEST_ASSERT_EQUAL(2, f.ready());
  TEST_ASSERT_NOT_NULL(copyOf(b));
  TEST_ASSERT_NOT_NULL(copyOf(c));
  TEST_ASSERT_NULL(copyOf(a));
}

void test_fill_rejects_a_copy_with_a_wrong_crc() {
  files = Files();
  const uint32_t a = files.add(3000, 1);
  files.d[0][2000] ^= 0x100;  // the file does not match its crc
  prepare(1u << 20);
  PsramFill f(table, fpool, files, arena, hw);
  f.restart(&a, 1);
  run(f);
  TEST_ASSERT_EQUAL(0, f.ready());
  TEST_ASSERT_EQUAL(1, f.bad());
  TEST_ASSERT_NULL(copyOf(a));
}

void test_fill_skips_unreadable_and_scattered_files() {
  files = Files();
  const uint32_t a = files.add(3000, 1), b = files.add(2000, 2);
  prepare(1u << 20);
  table.clear();
  table.add(a, 3000, 44100, 0);  // not contiguous: no block reads
  table.add(b, 2000, 44100, 1);
  PsramFill f(table, fpool, files, arena, hw);
  const uint32_t list[] = {a, b};
  f.restart(list, 2);
  TEST_ASSERT_EQUAL(1, f.planned());
  files.fail = true;  // the card went away
  run(f);
  TEST_ASSERT_EQUAL(0, f.ready());
  TEST_ASSERT_EQUAL(1, f.bad());
  TEST_ASSERT_NULL(copyOf(b));
}

void test_fill_idles_without_psram_and_restart_drops_copies() {
  files = Files();
  const uint32_t a = files.add(3000, 1);
  prepare(1u << 20);
  PsramFill f(table, fpool, files, arena, hw);
  f.restart(&a, 1);
  run(f);
  TEST_ASSERT_NOT_NULL(copyOf(a));
  f.restart(&a, 1);  // a new project load: copies go, the arena starts over
  TEST_ASSERT_NULL(copyOf(a));
  TEST_ASSERT_EQUAL_UINT32(0, arena.used());
  fpool.psramOk = false;  // the watchdog failed
  const int r = files.reads;
  TEST_ASSERT_FALSE(f.step());
  TEST_ASSERT_EQUAL(r, files.reads);
  TEST_ASSERT_NULL(copyOf(a));
}
```

В `main`:

```cpp
  RUN_TEST(test_fill_order_shortest_first_until_full);
  RUN_TEST(test_fill_copies_shortest_first_and_verifies);
  RUN_TEST(test_fill_stops_at_capacity);
  RUN_TEST(test_fill_rejects_a_copy_with_a_wrong_crc);
  RUN_TEST(test_fill_skips_unreadable_and_scattered_files);
  RUN_TEST(test_fill_idles_without_psram_and_restart_drops_copies);
```

**Step 2: Запуск, ожидается падение**

Run: `pio test -e native -f test_psram_cache`
Expected: FAIL (компиляция): `'psramBytes' was not declared in this scope`, `'PsramFill' was not declared`.

**Step 3: Реализация**

`psram_cache.h`: подключить `#include "sample_store.h"`, `#include "stream_pool.h"`, `#include "stream_sched.h"` и дописать перед `}  // namespace mt`:

```cpp
// Arena bytes for a copy: whole blocks (the loader reads blocks; the tail of the last is padding).
inline uint32_t psramBytes(uint32_t frames) {
  return (frames + kStreamBlock - 1) / kStreamBlock * kStreamBlock * 2;
}
// Indices of frames[0..n) shortest first (equal lengths keep their order), as many as fit in
// capBytes in that order. Returns how many.
int psramFillOrder(const uint32_t* frames, int n, uint32_t capBytes, int* order);

// Copies store samples into the arena, shortest first, one load (<= kLoadMax blocks) per step();
// a copy reaches the table (and the voices) only after the crc of the copy matches its key.
class PsramFill final : public BgWork {
 public:
  PsramFill(StoreTable& t, const StreamPool& p, BlockReader& r, PsramArena& a, PsramHw& hw)
      : t_(t), p_(p), r_(r), a_(a), hw_(hw) {}
  // The samples to hold (crcs, duplicates allowed; ones not in the store or not contiguous are
  // left out). The synth is parked: the old copies go, the arena starts over.
  void restart(const uint32_t* crcs, int n);
  bool step() override;  // false: nothing to do (done, or PSRAM off)
  int planned() const { return n_; }
  int ready() const { return ready_; }
  int bad() const { return bad_; }
  uint32_t plannedBytes() const { return total_; }
  uint32_t doneBytes() const { return done_; }

 private:
  static constexpr uint32_t kVerifyFrames = 4096;  // per step: 8 KB read back from the chip
  enum class Phase : uint8_t { Idle, Copy, Verify };
  void next(bool ok);
  StoreTable& t_;
  const StreamPool& p_;
  BlockReader& r_;
  PsramArena& a_;
  PsramHw& hw_;
  int item_[kStoreEntries];  // table indices in fill order
  int n_ = 0, cur_ = 0, ready_ = 0, bad_ = 0;
  Phase phase_ = Phase::Idle;
  int16_t* dst_ = nullptr;
  uint32_t blk_ = 0, vfr_ = 0, crc_ = 0;
  uint32_t total_ = 0, done_ = 0, finished_ = 0;
};
```

`psram_cache.cpp`: `#include "sample_set.h"` и дописать:

```cpp
int psramFillOrder(const uint32_t* frames, int n, uint32_t capBytes, int* order) {
  for (int i = 0; i < n; ++i) order[i] = i;
  for (int i = 1; i < n; ++i) {  // insertion sort: stable, n <= kStoreEntries, once per load
    const int x = order[i];
    int j = i;
    for (; j > 0 && frames[order[j - 1]] > frames[x]; --j) order[j] = order[j - 1];
    order[j] = x;
  }
  uint32_t used = 0;
  int k = 0;
  for (; k < n; ++k) {
    const uint32_t b = psramBytes(frames[order[k]]);
    if (b > capBytes - used) break;  // longer ones do not fit either
    used += b;
  }
  return k;
}

void PsramFill::restart(const uint32_t* crcs, int n) {
  for (int i = 0; i < t_.count(); ++i) t_.setPsram(i, nullptr);
  a_.reset();
  n_ = cur_ = ready_ = bad_ = 0;
  total_ = done_ = finished_ = 0;
  dst_ = nullptr;
  uint32_t frames[kStoreEntries];
  int idx[kStoreEntries];
  int m = 0;
  for (int k = 0; k < n && m < kStoreEntries; ++k) {
    const int i = t_.find(crcs[k]);
    if (i < 0 || !t_.at(i).sector) continue;  // not in the store, or not readable by blocks
    bool dup = false;
    for (int j = 0; j < m && !dup; ++j) dup = idx[j] == i;
    if (dup) continue;
    idx[m] = i;
    frames[m++] = t_.at(i).frames;
  }
  int order[kStoreEntries];
  n_ = psramFillOrder(frames, m, a_.size(), order);
  for (int k = 0; k < n_; ++k) {
    item_[k] = idx[order[k]];
    total_ += psramBytes(frames[order[k]]);
  }
  phase_ = n_ ? Phase::Copy : Phase::Idle;
}

bool PsramFill::step() {
  if (phase_ == Phase::Idle) return false;
  if (!p_.psramOk) {  // the watchdog failed: no more copies
    phase_ = Phase::Idle;
    return false;
  }
  const StoreEntry& e = t_.at(item_[cur_]);
  const uint32_t key = e.crc;
  if (!dst_) {
    dst_ = static_cast<int16_t*>(a_.alloc(psramBytes(e.frames)));  // planned to fit
    blk_ = vfr_ = crc_ = 0;
  }
  if (phase_ == Phase::Copy) {
    const uint32_t blocks = (e.frames + kStreamBlock - 1) / kStreamBlock;
    const int n = static_cast<int>(blocks - blk_ < kLoadMax ? blocks - blk_ : kLoadMax);
    int16_t* to = dst_ + blk_ * kStreamBlock;
    if (!r_.read(key, blk_, n, to)) {
      next(false);
      return true;
    }
    hw_.flush(to, static_cast<uint32_t>(n) * kStreamBlock * 2);  // on the chip, out of the cache
    blk_ += n;
    done_ += static_cast<uint32_t>(n) * kStreamBlock * 2;
    if (blk_ == blocks) phase_ = Phase::Verify;
    return true;
  }
  // Verify: the crc of the copy as the chip holds it (its lines were flushed, so reads miss).
  uint32_t k = e.frames - vfr_;
  if (k > kVerifyFrames) k = kVerifyFrames;
  crc_ = sampleCrc(dst_ + vfr_, k, crc_);
  vfr_ += k;
  if (vfr_ == e.frames) next(crc_ == key);
  return true;
}

void PsramFill::next(bool ok) {
  if (ok) {
    t_.setPsram(item_[cur_], dst_);
    ++ready_;
  } else {
    ++bad_;  // its arena space stays unused until the next restart
  }
  finished_ += psramBytes(t_.at(item_[cur_]).frames);
  done_ = finished_;
  dst_ = nullptr;
  phase_ = ++cur_ < n_ ? Phase::Copy : Phase::Idle;
}
```

`sampleCrc` — это `crc32` с продолжением (`prev`), поэтому CRC по кускам равен CRC целого.

**Step 4: Повторный запуск**

Run: `pio test -e native -f test_psram_cache`
Expected: PASS (12 тестов).

### Task 18: Teensy: модуль `psram` — инициализация CS1, самотест, сторож, наполнение

**Files:**
- Create: `src/teensy/psram.h`, `src/teensy/psram.cpp`
- Modify: `src/teensy/main.cpp` (`setup`: `psram::begin(model)` после `stream::begin`; `loop`: `psram::step()` после `stream::step()`)
- Modify: `src/teensy/stream.cpp` (`rescan`: в конце `psram::restart(m)`)

Нативного теста нет: регистры и кэш. Логика проверена в Task 16–17, здесь проверка desk.

**Step 1: `psram.h`**

```cpp
#pragma once
#include <stdint.h>

namespace mt {
struct SynthModel;
}

// The optional PSRAM (one 8 MB chip on FlexSPI2 CS1): own bring-up, boot self-test, a watchdog every
// 100 ms and the whole-sample cache of the project (PsramFill, background work of stream::step).
namespace psram {

enum class State : uint8_t { Off, On, Failed };  // Off: no chip / self-test failed; Failed: watchdog

// setup(), after stream::begin and before the audio starts; starts the fill for m.
void begin(const mt::SynthModel& m);
// loop(): the watchdog. A failure clears pool.psramOk for good (voices go to SD at the next block).
void step();
// After stream::rescan (synth parked): the copies of the new model, shortest first.
void restart(const mt::SynthModel& m);
State state();
uint8_t fillPct();     // of the planned bytes; 100 when nothing is planned
void injectFault();    // desk: the next watchdog check reads back a flipped bit
void hold(bool on);    // desk stream bench: no copies, every sample streams

}  // namespace psram
```

**Step 2: `psram.cpp`**

```cpp
#include "psram.h"
#include <Arduino.h>
#include "link_server.h"
#include "psram_cache.h"
#include "stream.h"
#include "synth_model.h"

extern "C" uint8_t external_psram_size;  // startup.c: 0 = nothing found on CS0

namespace psram {

namespace {

uint32_t* const chip = reinterpret_cast<uint32_t*>(0x70000000u);

class Hw final : public mt::PsramHw {
 public:
  volatile bool fault = false;
  void flush(const void* p, uint32_t n) override { arm_dcache_flush_delete(const_cast<void*>(p), n); }
  void drop(const void* p, uint32_t n) override {
    arm_dcache_delete(const_cast<void*>(p), n);
    if (fault) {  // desk: the chip "lost" a bit of what was just written
      fault = false;
      *static_cast<volatile uint32_t*>(const_cast<void*>(p)) ^= 1;
      arm_dcache_flush_delete(const_cast<void*>(p), 32);
    }
  }
} hw;

State st = State::Off;
bool held = false;
mt::PsramArena arena;
mt::PsramWatch watch;
mt::PsramFill* fill = nullptr;

FLASHMEM void ipCmd(uint32_t seq, uint32_t addr) {
  FLEXSPI2_IPCR0 = addr;
  FLEXSPI2_IPCR1 = FLEXSPI_IPCR1_ISEQID(seq);
  FLEXSPI2_IPCMD = FLEXSPI_IPCMD_TRG;
  while (!(FLEXSPI2_INTR & FLEXSPI_INTR_IPCMDDONE)) {
  }
  FLEXSPI2_INTR = FLEXSPI_INTR_IPCMDDONE;
}

FLASHMEM uint32_t readId(uint32_t addr) {
  FLEXSPI2_IPCR0 = addr;
  FLEXSPI2_IPCR1 = FLEXSPI_IPCR1_ISEQID(3) | FLEXSPI_IPCR1_IDATSZ(4);
  FLEXSPI2_IPCMD = FLEXSPI_IPCMD_TRG;
  while (!(FLEXSPI2_INTR & FLEXSPI_INTR_IPCMDDONE)) {
  }
  const uint32_t id = FLEXSPI2_RFDR0;
  FLEXSPI2_INTR = FLEXSPI_INTR_IPCMDDONE | FLEXSPI_INTR_IPRXWA;
  return id;
}

// Startup looks for a chip on CS0 and only then for one on CS1. This board has a single chip on CS1:
// startup has written the LUT (seq 0 exit QPI, 1 reset enable, 2 reset, 3 read ID, 4 enter QPI,
// 5 / 6 QPI read / write, the AHB sequences of FLSHA1CR2 / FLSHA2CR2) and found nothing. Here the
// chip is probed as the second device (at 8 MB), then mapped from 0x70000000 (CS0 size 0).
FLASHMEM bool initChip() {
  FLEXSPI2_FLSHA1CR0 = 8192;  // KB
  FLEXSPI2_FLSHA2CR0 = 8192;
  ipCmd(0, 0x800000);  // exit QPI (a warm reboot leaves the chip in QPI)
  ipCmd(1, 0x800000);  // reset enable
  ipCmd(2, 0x800000);  // reset
  const uint32_t id = readId(0x800000);
  if ((id & 0xFFFF) != 0x5D0D) {
    link::log("psram off: no chip on CS1 (id %08lx)", static_cast<unsigned long>(id));
    return false;
  }
  FLEXSPI2_FLSHA1CR0 = 0;  // CS0 empty: CS1 starts at 0x70000000
  FLEXSPI2_FLSHA2CR0 = 8192;
  ipCmd(4, 0);  // enter QPI
  FLEXSPI2_MCR0 |= FLEXSPI_MCR0_SWRESET;  // the AHB side takes the new map
  while (FLEXSPI2_MCR0 & FLEXSPI_MCR0_SWRESET) {
  }
  arm_dcache_delete(chip, mt::kPsramBytes);  // nothing of the old map stays in the cache
  return true;
}

}  // namespace

void begin(const mt::SynthModel& m) {
#ifdef MT_NO_PSRAM
  link::log("psram off (build)");
  return;
#endif
  // The MPU region 0x70000000 / 32 MB, write-back, is set by configure_cache() whatever was found.
  if (external_psram_size) {
    link::log("psram off: startup found %u MB on CS0, not this board", external_psram_size);
    return;
  }
  if (!initChip()) return;
  const uint32_t t0 = micros();
  if (!mt::psramSelfTest(chip, mt::kPsramBytes, hw)) {
    link::log("psram off: self-test failed");
    return;
  }
  const uint32_t arenaBytes = mt::kPsramBytes - mt::kPsramGuardBytes;
  arena.init(chip, arenaBytes);
  watch.init(chip + arenaBytes / 4, hw);
  static mt::PsramFill f(stream::table(), stream::pool(), stream::reader(), arena, hw);
  fill = &f;
  stream::addBackground(fill);
  stream::pool().psramOk = true;
  st = State::On;
  link::log("psram ok: 8 MB, self-test %lu us", static_cast<unsigned long>(micros() - t0));
  restart(m);
}

void step() {
  if (st != State::On || watch.step(millis())) return;
  stream::pool().psramOk = false;  // Synth drops PSRAM sources at the next block, the fill stops
  st = State::Failed;
  link::log("psram failed: the watchdog read back a wrong word; SD only until a reboot");
}

void restart(const mt::SynthModel& m) {
  if (st != State::On) return;
  uint32_t crcs[sizeof m.samples / sizeof m.samples[0]];
  int n = 0;
  if (!held)
    for (int i = 0; i < m.sampleCount; ++i) crcs[n++] = m.samples[i].crc;
  fill->restart(crcs, n);
  link::log("psram: %d samples planned, %lu KB", fill->planned(),
            static_cast<unsigned long>(fill->plannedBytes() / 1024));
}

State state() { return st; }

uint8_t fillPct() {
  if (!fill || !fill->plannedBytes()) return 100;
  return static_cast<uint8_t>(100ull * fill->doneBytes() / fill->plannedBytes());
}

void injectFault() { hw.fault = true; }

void hold(bool on) { held = on; }

}  // namespace psram
```

Если `SynthModel` объявлен не в `synth_model.h`, подключить заголовок, где он объявлен (тот же, что в `bank.cpp`).

**Step 3: Склейка**

- `main.cpp`, `setup`: сразу после `stream::begin(synth);` — `psram::begin(model);` (`model` — тот же `mt::SynthModel`, что передаётся в `bank::begin`). Звук ещё не запущен, никто не читает PSRAM.
- `main.cpp`, `loop`: после `stream::step();` — `psram::step();`.
- `stream.cpp`, `rescan`: последней строкой — `psram::restart(m);` (синт припаркован, `theTable.clear()` уже обнулил указатели PSRAM). `#include "psram.h"`.
- Когда `bank::begin` вызывает `stream::rescan` при загрузке, `psram::begin` ещё не было, и `restart` ничего не делает (`st == Off`). Наполнение для модели загрузки запускает сам `begin`.

**Step 4: Сборка**

Run: `pio run -e teensy41 -e teensy41-desk -e teensy41-bench`
Expected: SUCCESS. DTCM вырос примерно на 1 КБ (`PsramFill::item_`), RAM2 не изменился.

**Step 5: Desk-проверка** (голый Teensy с картой и PSRAM на CS1, прошивка `teensy41-desk`)

1. `LD status 2`. В логе: `psram ok: 8 MB, self-test NNNN us` (≈ десятки мс), затем `psram: 0 samples planned, 0 KB` (в desk-модели сэмплов нет).
2. Тёплая перезагрузка той же прошивкой: `pio run -e teensy41-desk -t upload`. Питание не снималось, чип остался в QPI. Снова `psram ok`: команда 0 (exit QPI) это покрывает.
3. Плата без PSRAM, если есть под рукой: `psram off: no chip on CS1 (id ...)`, `LD pnote 0 60 1000` звучит.
4. Наполнение и сторож проверяются в Task 23 и этапе 7: там бенч задаёт модели сэмплы.

### Проверка этапа 4

Run: `pio run -e wt32 -e teensy41 -e teensy41-desk -e teensy41-bench`
Expected: SUCCESS ×4.

Run: `pio test -e native`
Expected: PASS, добавился `test_psram_cache` (12).

---

## Этап 5 — упреждение: подсказка `0xF4` от секвенсора

После этапа секвенсор ESP за шаг до ноты SAMPLE-дорожки шлёт подсказку `{0xF4, note, ofs}` тем же `EvBatch`, что и ноты (отклонение 3). Teensy перехватывает её в `onEv`, до `EvQueue`, и сразу занимает слот `Prefetch` с точки, откуда начнётся нота. Загрузчик заполняет слот до ноты, нота его забирает (`acquire` отдаёт `Prefetch`-слот того же ключа). Подсказка нужна там, где «головы» не помогают: OFS, длинные начала на высокой высоте. Подсказки без ноты (нота не прошла CND/PRB, стоп) освобождает таймаут.

Где ESP шлёт события на линк: `Sequencer::scheduleStep` → `push` в кучу → `emitDue` / `dispatch` → `MidiSink::synth` (`src/audio/audio_link.cpp:165`, `audio::post`) → `slink::postEvent` → `flushEvents` (`src/link/link.cpp:89`, `EvBatch`). Подсказка проходит этот путь как обычное событие INT-дорожки, со своим временем.

### Task 19: Подсказка в секвенсоре и в протоколе (lib/core)

**Files:**
- Modify: `lib/core/src/model.h` (рядом с `kSynthStep`, стр. 281–285: `kSynthHint`)
- Modify: `lib/core/src/sequencer.h` (`kHintLeadUs`, `hintTime`, приватный `pushHint`)
- Modify: `lib/core/src/sequencer.cpp` (`scheduleStep` после `pushStepStart(..., true)` около стр. 441; `dispatch`, стр. 793)
- Modify: `lib/core/src/synth.cpp` (`Synth::event`, стр. 157: подсказка в очередь не идёт)
- Test: `test/test_sequencer/test_main.cpp`, `test/test_link_msg/test_main.cpp`, `test/test_sample_stream/test_main.cpp`

**Step 1: Падающие тесты**

`test/test_sequencer/test_main.cpp`, перед `main`:

```cpp
void test_hint_time_one_step_ahead() {
  TEST_ASSERT_EQUAL_UINT64(1000000 - 125000, hintTime(1000000, 125000, 0));
  TEST_ASSERT_EQUAL_UINT64(1000000 - kHintLeadUs, hintTime(1000000, 500000, 0));  // slow: capped
  TEST_ASSERT_EQUAL_UINT64(950000, hintTime(1000000, 125000, 950000));            // never in the past
  TEST_ASSERT_EQUAL_UINT64(0, hintTime(100000, 125000, 0));                      // no wrap below zero
}

void test_int_sample_steps_send_a_hint_one_step_ahead() {
  p->tracks[2].out = TrackOut::Int;
  p->tracks[2].instr = 0;
  instrSetType(p->instruments[0], InstrType::Sample);
  p->patterns[0].steps[2][0].note = 60;
  Step& s2 = p->patterns[0].steps[2][2];
  s2.note = 62;
  s2.fx[0].cmd = Fx::OFS;
  s2.fx[0].val = 0x40;
  p->tracks[4].out = TrackOut::Int;  // a CHIP instrument: no hint
  p->tracks[4].instr = 1;
  instrSetType(p->instruments[1], InstrType::Chip);
  p->patterns[0].steps[4][2].note = 64;
  p->patterns[0].steps[3][2].note = 65;  // a MIDI track: never a hint
  seq->start(0, *sink);
  run(0, 400000);
  const auto h = sink->synKind(kSynthHint);
  TEST_ASSERT_EQUAL(2, h.size());
  const uint64_t stepUs = 60000000ull / p->bpm / 4;
  const auto on = sink->synKind(0x90);
  const SynRec* h62 = nullptr;
  const SynRec* on62 = nullptr;
  for (const auto& r : h)
    if (r.b[1] == 62) h62 = &r;
  for (const auto& r : on)
    if (r.b[1] == 62) on62 = &r;
  TEST_ASSERT_NOT_NULL(h62);
  TEST_ASSERT_NOT_NULL(on62);
  TEST_ASSERT_EQUAL(2, h62->track);
  TEST_ASSERT_EQUAL(3, h62->len);
  TEST_ASSERT_EQUAL_UINT8(0x40, h62->b[2]);
  TEST_ASSERT_EQUAL_UINT64(on62->t - (stepUs < kHintLeadUs ? stepUs : kHintLeadUs), h62->t);
  for (const auto& r : h)
    if (r.b[1] == 60) TEST_ASSERT_EQUAL_UINT8(0, r.b[2]);  // no OFS: the start
  for (const auto& r : sink->log) TEST_ASSERT_NOT_EQUAL(kSynthHint, r.b[0]);
}
```

В `main`: `RUN_TEST(test_hint_time_one_step_ahead);`, `RUN_TEST(test_int_sample_steps_send_a_hint_one_step_ahead);`.

`test/test_link_msg/test_main.cpp` (подсказка — обычное событие с `len` 3, кодек не меняется; тест закрепляет это):

```cpp
void test_ev_batch_carries_a_hint() {
  EvBatch a;
  a.n = 2;
  a.ev[0] = {1000, 2, 3, {mt::kSynthHint, 62, 0x40}};
  a.ev[1] = {126000, 2, 3, {0x92, 62, 100}};
  const int n = put(a);
  EvBatch b;
  Reader r(buf, n);
  TEST_ASSERT_TRUE(decode(r, b));
  TEST_ASSERT_EQUAL(2, b.n);
  TEST_ASSERT_EQUAL_UINT64(1000, b.ev[0].tUs);
  TEST_ASSERT_EQUAL_UINT8(mt::kSynthHint, b.ev[0].b[0]);
  TEST_ASSERT_EQUAL_UINT8(0x40, b.ev[0].b[2]);
  TEST_ASSERT_EQUAL_UINT64(126000, b.ev[1].tUs);
}
```

С `#include "model.h"` и `RUN_TEST(test_ev_batch_carries_a_hint);`.

`test/test_sample_stream/test_main.cpp`:

```cpp
void test_hint_never_reaches_the_voices() {
  const uint8_t h[3] = {kSynthHint, 60, 0};
  TEST_ASSERT_FALSE(s->event(0, 0, h, 3));
  int16_t l[Synth::kBlock], r[Synth::kBlock];
  s->render(l, r);
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}
```

С `RUN_TEST(test_hint_never_reaches_the_voices);`.

**Step 2: Запуск, ожидается падение**

Run: `pio test -e native -f test_sequencer`
Expected: FAIL (компиляция): `'kSynthHint' was not declared in this scope`, `'hintTime' was not declared`.

**Step 3: Реализация**

`model.h`, после `kSynthArpChord`:

```cpp
// Synth message 0xF4 note ofs (INT tracks, ahead of a SAMPLE step): where the track's next note-on
// will start (note, OFS 0 = none), so the synth board loads it in advance. Never reaches MIDI.
constexpr uint8_t kSynthHint = 0xF4;
```

`sequencer.h`, рядом с `kTieOverlapUs`:

```cpp
// The prefetch hint (kSynthHint) of a SAMPLE step goes one step ahead of it, at most kHintLeadUs
// (the board's slot holds ~70 ms; an earlier hint only waits), never before earliest.
constexpr uint32_t kHintLeadUs = 150000;
inline uint64_t hintTime(uint64_t t, uint32_t stepUs, int64_t earliest) {
  const uint64_t lead = stepUs < kHintLeadUs ? stepUs : kHintLeadUs;
  const int64_t h = t > lead ? static_cast<int64_t>(t - lead) : 0;
  return static_cast<uint64_t>(h < earliest ? earliest : h);
}
```

В `class Sequencer`, рядом с `pushStepStart`:

```cpp
  void pushHint(const Step& s, const ExpandOut& ex, int first, uint64_t t, int64_t earliest, uint8_t track,
                uint32_t stepUs);
```

`sequencer.cpp`:

```cpp
// INT track with a SAMPLE instrument (its configured one: a PGM on the step is not followed): tells
// the synth where the note will start, one step ahead (see kSynthHint).
void Sequencer::pushHint(const Step& s, const ExpandOut& ex, int first, uint64_t t, int64_t earliest,
                         uint8_t track, uint32_t stepUs) {
  if (!internal(track) || first >= ex.count || ex.ev[first].kind != EvKind::NoteOn) return;
  const uint8_t ii = p_.tracks[track].instr;
  if (ii >= kInstruments || p_.instruments[ii].type != InstrType::Sample) return;
  const FxSlot* o = s.find(Fx::OFS);
  push(hintTime(t, stepUs, earliest), kSynthHint, ex.ev[first].note, o ? o->val : 0, 0, false, 3, track);
}
```

В `scheduleStep` (около стр. 442), сразу после `int i = pushControls(ex, t, earliest, static_cast<uint8_t>(tr));`:

```cpp
    pushHint(s, ex, i, t, earliest, static_cast<uint8_t>(tr), su);
```

`dispatch` (стр. 793):

```cpp
  if ((e.b[0] == 0xF5 || e.b[0] == 0xFF || e.b[0] == kSynthHint) && !toSynth) return;  // synth only
```

`synth.cpp`, `Synth::event`, первой проверкой после `track` / `len`:

```cpp
  if (b[0] == kSynthHint) return false;  // the board's loader takes hints (link_server), not the voices
```

**Step 4: Повторный запуск**

Run: `pio test -e native -f test_sequencer`, `pio test -e native -f test_link_msg`, `pio test -e native -f test_sample_stream`
Expected: PASS. Остальные тесты секвенсора не изменились: на дорожках с CHIP подсказок нет, а MIDI их никогда не получает.

### Task 20: `Synth::prefetch` и таймаут `Prefetch`-слотов (lib/core)

**Files:**
- Modify: `lib/core/src/synth.h`, `lib/core/src/synth.cpp` (`prefetch`)
- Modify: `lib/core/src/stream_pool.h`, `lib/core/src/stream_pool.cpp` (`expirePrefetch`)
- Test: `test/test_sample_stream/test_main.cpp`, `test/test_stream_pool/test_main.cpp`

**Step 1: Падающие тесты**

`test/test_sample_stream/test_main.cpp` (`#include "sample_region.h"`):

```cpp
void test_prefetch_fills_a_slot_the_note_adopts() {
  src.stream = true;  // no heads, no sync reader: without the hint the start would underrun
  s->reset();
  const int sl = s->prefetch(0, 60, 0);
  TEST_ASSERT_TRUE(sl >= 0);
  TEST_ASSERT_EQUAL(static_cast<int>(SlotOwner::Prefetch), static_cast<int>(pool.slot(sl).owner));
  while (streamLoad(pool, sl, rd)) {
  }  // the loader, before the note
  const auto b = play(60, 16);  // 2048 frames: inside the loaded blocks
  TEST_ASSERT_EQUAL(static_cast<int>(SlotOwner::Voice), static_cast<int>(pool.slot(sl).owner));
  TEST_ASSERT_EQUAL_UINT32(0, pool.underruns);
  const auto a = ram(60, 16);
  TEST_ASSERT_EQUAL_INT16_ARRAY(a.data(), b.data(), a.size());
}

void test_prefetch_starts_at_the_ofs_point() {
  src.stream = true;
  s->reset();
  const int sl = s->prefetch(0, 60, 0x80);
  TEST_ASSERT_TRUE(sl >= 0);
  PlaySnap ps;
  TEST_ASSERT_TRUE(pool.snap(sl, ps));
  const SampleRegion g = sampleRegion(p->instruments[0], -1, 30000, 0x80);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(g.pos >> 32), ps.pos);
}

void test_prefetch_skips_what_needs_no_load() {
  src.stream = false;  // whole (flash / PSRAM)
  TEST_ASSERT_EQUAL(-1, s->prefetch(0, 60, 0));
  src.stream = true;
  TEST_ASSERT_EQUAL(-1, s->prefetch(kTracks, 60, 0));
  Instrument& m = p->instruments[0];
  m.sliceCount = 2;
  m.sliceMode = static_cast<uint8_t>(SliceMode::Fx);  // SLC is not in the hint
  TEST_ASSERT_EQUAL(-1, s->prefetch(0, 60, 0));
  m.sliceMode = static_cast<uint8_t>(SliceMode::Note);
  TEST_ASSERT_EQUAL(-1, s->prefetch(0, 59, 0));  // below the root: no slice, silent
  m.sliceCount = 0;
  instrSetType(m, InstrType::Chip);
  TEST_ASSERT_EQUAL(-1, s->prefetch(0, 60, 0));
}
```

В `main`: три `RUN_TEST`.

`test/test_stream_pool/test_main.cpp`:

```cpp
void test_stale_prefetch_is_released() {
  const int a = get(5, SlotOwner::Prefetch);
  const int b = get(6, SlotOwner::Prefetch);
  const int v = get(7);
  pool.slotRef(a).stamp = 1000;
  pool.slotRef(b).stamp = 1900;
  pool.slotRef(v).stamp = 0;
  TEST_ASSERT_EQUAL(1, pool.expirePrefetch(2100, 1000));  // a: 1100 ms without its note
  TEST_ASSERT_EQUAL(static_cast<int>(SlotOwner::Free), static_cast<int>(pool.slot(a).owner));
  TEST_ASSERT_EQUAL(static_cast<int>(SlotOwner::Prefetch), static_cast<int>(pool.slot(b).owner));
  TEST_ASSERT_EQUAL(static_cast<int>(SlotOwner::Voice), static_cast<int>(pool.slot(v).owner));
}
```

В `main`: `RUN_TEST(test_stale_prefetch_is_released);`.

**Step 2: Запуск, ожидается падение**

Run: `pio test -e native -f test_sample_stream`
Expected: FAIL (компиляция): `'class mt::Synth' has no member named 'prefetch'`.

**Step 3: Реализация**

`synth.h`, в `public` рядом с `setPool`:

```cpp
  // Prefetch hint (kSynthHint note ofs, from the link ahead of the step): takes a Prefetch slot where
  // the track's next SAMPLE note-on would start, so the loader fills it before the note. loop() with
  // the audio interrupt masked (StreamPool::acquire). The slot, or -1: nothing to load (not a streamed
  // sample, a whole copy, FX slices, no slot free).
  int prefetch(uint8_t track, uint8_t note, uint8_t ofs);
```

`synth.cpp`:

```cpp
int Synth::prefetch(uint8_t track, uint8_t note, uint8_t ofs) {
  if (!pool_ || !bank_ || track >= kTracks) return -1;
  const Instrument& m = p_.instruments[trackInstr(track)];
  if (m.type != InstrType::Sample) return -1;
  char name[kSampleNameMax + 1];  // the UI may be editing it
  for (int i = 0; i < kSampleNameMax; ++i) name[i] = m.sample[i];
  name[kSampleNameMax] = 0;
  SampleData d;
  if (!bank_->find(name, d) || !d.streamable || d.whole || !d.frames) return -1;
  // As the note-on picks the slice: NOTE maps note - root, FX takes SLC (not in the hint: the
  // slice's head covers its start).
  int slice = -1;
  const int cnt = m.sliceCount > kMaxSlices ? kMaxSlices : m.sliceCount;
  if (cnt) {
    if (m.sliceMode == static_cast<uint8_t>(SliceMode::Note)) {
      slice = note - m.root;
      if (slice < 0 || slice >= cnt) return -1;
    } else if (m.sliceMode == static_cast<uint8_t>(SliceMode::Fx)) {
      return -1;
    }
  }
  const SampleRegion g = sampleRegion(m, slice, d.frames, ofs);
  return pool_->acquire(d.key, d.frames, d.rate, d.heads, d.nHeads, g, SlotOwner::Prefetch);
}
```

`stream_pool.h`, в `public`:

```cpp
  // Releases Prefetch slots asked (stamp) more than maxMs before nowMs: the note never came.
  // loop() with the audio interrupt masked (the interrupt adopts Prefetch slots). Returns how many.
  int expirePrefetch(uint32_t nowMs, uint32_t maxMs);
```

`stream_pool.cpp`:

```cpp
int StreamPool::expirePrefetch(uint32_t nowMs, uint32_t maxMs) {
  int n = 0;
  for (int s = 0; s < n_; ++s)
    if (slot_[s].owner == SlotOwner::Prefetch && nowMs - slot_[s].stamp > maxMs) {
      release(s);
      ++n;
    }
  return n;
}
```

**Step 4: Повторный запуск**

Run: `pio test -e native -f test_sample_stream`, `pio test -e native -f test_stream_pool`
Expected: PASS.

### Task 21: Teensy: перехват подсказки в `onEv`, таймаут, desk-команда `hint`

**Files:**
- Modify: `src/teensy/link_server.cpp:93-101` (`onEv`)
- Modify: `src/teensy/stream.h`, `src/teensy/stream.cpp` (`hint`, таймаут в `step`, указатель на синт)
- Modify: `scripts/link_desk.py` (команда `hint`)

**Step 1: `stream`**

`stream.h`:

```cpp
// A prefetch hint from the link (kSynthHint note ofs): a Prefetch slot for the track's next note-on.
// loop() (link::poll); counts in Stats::hints.
void hint(uint8_t track, uint8_t note, uint8_t ofs);
```

В `struct Stats` добавить `uint16_t hints = 0, hintSlots = 0;  // since the last take: hints, slots they took`.

`stream.cpp`: `mt::Synth* synthP = nullptr;` в безымянном пространстве, в `begin` — `synthP = &s;`.

```cpp
constexpr uint32_t kPrefetchMaxMs = 1000;  // a hint whose note never came (CND, PRB, stop)
uint16_t hintsSeen = 0, hintSlotsSeen = 0;

void hint(uint8_t track, uint8_t note, uint8_t ofs) {
  if (!synthP) return;
  AudioNoInterrupts();
  const int s = synthP->prefetch(track, note, ofs);
  if (s >= 0) thePool.slotRef(s).stamp = millis();
  AudioInterrupts();
  ++hintsSeen;
  hintSlotsSeen += s >= 0;
#ifdef LINK_DESK
  link::log("hint: track %u note %u ofs %u -> slot %d", track, note, ofs, s);
#endif
}
```

`step()`, первой строкой:

```cpp
  AudioNoInterrupts();
  thePool.expirePrefetch(millis(), kPrefetchMaxMs);
  AudioInterrupts();
```

`takeStats()`: `s.hints = hintsSeen; s.hintSlots = hintSlotsSeen; hintsSeen = hintSlotsSeen = 0;`. В `stream.cpp` нужен `#include <Audio.h>` (там `AudioNoInterrupts`), если `preview_stream.cpp` берёт его из другого заголовка — оттуда же.

**Step 2: `onEv`** (`link_server.cpp`)

```cpp
void onEv(Reader& r) {
  EvBatch b;
  if (!decode(r, b) || !clock.valid()) return;
  for (int i = 0; i < b.n; ++i) {
    const Ev& e = b.ev[i];
    // A hint is for now (the loader), not for its time: queued, it would hold the FIFO until then.
    if (e.len == 3 && e.b[0] == mt::kSynthHint) {
      stream::hint(e.track, e.b[1], e.b[2]);
      continue;
    }
    QEv q{clock.toLocal(e.tUs) + kPlayLatencyUs, e.track, e.len, {e.b[0], e.b[1], e.b[2]}};
    if (!out->queue().push(q)) lostTotal++;
  }
}
```

`#include "stream.h"`, `#include "model.h"` (если ещё нет).

Подсказка обрабатывается в момент прихода. До ноты остаётся (t_ноты − t_подсказки) + `kPlayLatencyUs`, то есть шаг плюс задержка воспроизведения.

**Step 3: `link_desk.py`, команда `hint`**

```python
def cmd_hint(link, args):
    """A prefetch hint (0xF4 note ofs) on a track, then after --lead ms the note with that OFS: as the
    sequencer sends them. The board logs 'hint: ... -> slot N' (desk build)."""
    cmd_hello(link, args)
    time.sleep(0.3)  # a few Time samples for the offset
    t = now_us()
    lead = args.lead * 1000
    evs = [(t, args.track, [0xF4, args.note, args.ofs])]
    if args.ofs:
        evs.append((t + lead, args.track, [0xF5, 20, args.ofs]))  # Fx::OFS = 20
    evs += [(t + lead, args.track, [0x90 | args.track, args.note, 100]),
            (t + lead + args.ms * 1000, args.track, [0x80 | args.track, args.note, 0])]
    link.send("Ev", ev_batch(evs))
    time.sleep((lead + args.ms * 1000) / 1e6 + 0.5)
    s = link.wait("Status", 0.5)
    if s:
        print_status(parse_status(s))
```

Парсер (в `main`): `hn = sub.add_parser("hint")`, аргументы `track` (int), `note` (int, по умолчанию 60), `--ofs` (int, 0), `--lead` (int, мс, 125), `--ms` (int, 1000). В словарь команд — `"hint": cmd_hint`.

OFS для синта отдельным событием `0xF5 20 val` ставит `ofsSet` сразу (`Synth::fx`, ветка `Fx::OFS`), начала шага не нужно.

**Step 4: Сборка**

Run: `pio run -e wt32 -e teensy41 -e teensy41-desk -e teensy41-bench`
Expected: SUCCESS ×4.

**Step 5: Desk-проверка** (прошивка `teensy41-desk`)

1. `LD hint 0 60`. В логе — `hint: track 0 note 60 ofs 0 -> slot -1`: в desk-модели нет стрим-сэмплов, но подсказка перехвачена. В строке Status `lost 0`, нота звучит, как без подсказки.
2. Полная проверка (слот занят, нота его забирает, `underruns 0` при OFS) — в этапе 7, после того как бенч (Task 23) задаст модели стрим-сэмплы.

### Проверка этапа 5

Run: `pio run -e wt32 -e teensy41 -e teensy41-desk -e teensy41-bench`
Expected: SUCCESS ×4.

Run: `pio test -e native`
Expected: PASS: `test_sequencer` +2, `test_link_msg` +1, `test_sample_stream` +4, `test_stream_pool` +1.

---

## Этап 6 — видимость: Status, строка Samples, значок, desk-команды

После этапа стрим виден везде, где о нём надо знать. В `Status`: занятые стрим-голоса, недогрузки, минимальный запас, состояние и заполнение PSRAM (отклонение 13: поля в конце, `kProtocol = 3`). На ESP: строка «Samples» в FILE → SYSTEM и значок `SD!` вместо `CPU` в строке статуса после недогрузки. В `link_desk.py`: новые поля в `status`, бенч стрима (`bench-stream`) и сбой PSRAM (`psram-fault`) через новое сообщение `Debug`, которое обрабатывает только desk-сборка.

### Task 22: Поля `Status`, сообщение `Debug`, `kProtocol = 3` (lib/core)

**Files:**
- Modify: `lib/core/src/link_msg.h` (`kProtocol`, `Msg::Debug`, `Status`, `Debug`, `DebugOp`)
- Modify: `lib/core/src/link_msg.cpp` (кодеки)
- Test: `test/test_link_msg/test_main.cpp`

**Step 1: Падающие тесты**

В `test_status_with_and_without_scope` перед первым `put(a)`:

```cpp
  a.streamVoices = 14;
  a.underruns = 3;
  a.minReserveMs = 41;
  a.psram = 1;
  a.psramFill = 57;
```

и после проверок первого декодирования (перед `checkTruncated<Status>(n)`):

```cpp
  TEST_ASSERT_EQUAL(14, b.streamVoices);
  TEST_ASSERT_EQUAL(3, b.underruns);
  TEST_ASSERT_EQUAL(41, b.minReserveMs);
  TEST_ASSERT_EQUAL(1, b.psram);
  TEST_ASSERT_EQUAL(57, b.psramFill);
```

и после второго декодирования (со scope):

```cpp
  TEST_ASSERT_EQUAL(41, b.minReserveMs);  // after the scope: still in place
  TEST_ASSERT_EQUAL(57, b.psramFill);
```

Новые тесты:

```cpp
void test_debug() {
  Debug a;
  a.op = static_cast<uint8_t>(DebugOp::StreamBench);
  a.a = 12;
  a.b = kBenchSdOnly;
  a.secs = 600;
  const int n = put(a);
  Debug b;
  Reader r(buf, n);
  TEST_ASSERT_TRUE(decode(r, b));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(DebugOp::StreamBench), b.op);
  TEST_ASSERT_EQUAL(12, b.a);
  TEST_ASSERT_EQUAL(kBenchSdOnly, b.b);
  TEST_ASSERT_EQUAL(600, b.secs);
  checkTruncated<Debug>(n);
}

void test_protocol_2_and_debug_id() {
  TEST_ASSERT_EQUAL(3, kProtocol);
  TEST_ASSERT_EQUAL(41, static_cast<int>(Msg::Debug));  // after Perf (40)
}
```

В `main`: `RUN_TEST(test_debug);`, `RUN_TEST(test_protocol_2_and_debug_id);`.

**Step 2: Запуск, ожидается падение**

Run: `pio test -e native -f test_link_msg`
Expected: FAIL (компиляция): `'struct mt::link::Status' has no member named 'streamVoices'`, `'Debug' was not declared`.

**Step 3: Реализация**

`link_msg.h`: `constexpr uint16_t kProtocol = 3;`. В `enum class Msg` после `Perf` — `Debug,`. В `struct Status` после `scope`:

```cpp
  // Sample streaming, since the previous Status (after the scope: protocol 3).
  uint8_t streamVoices = 0;        // pool slots voices hold now
  uint16_t underruns = 0;          // voices that ran out of data
  uint16_t minReserveMs = 0xFFFF;  // lowest reserve of a streaming voice, 0xFFFF = none streamed
  uint8_t psram = 0;               // 0 off (no chip / self-test), 1 on, 2 failed (watchdog)
  uint8_t psramFill = 0;           // % of the planned whole-sample copies made
```

После `struct Progress`:

```cpp
// Desk tools (link_desk.py; the synth board's desk build acts on them, others ignore them).
enum class DebugOp : uint8_t { PsramFault = 1, StreamBench = 2, StreamBenchStop = 3 };
constexpr uint8_t kBenchSdOnly = 1;     // StreamBench b: no PSRAM copies, every voice streams
constexpr uint8_t kBenchSetupOnly = 2;  // StreamBench b: samples and instruments, no notes
struct Debug {
  uint8_t op = 0;
  uint8_t a = 0, b = 0;  // StreamBench: a = semitones above the root, b = kBench* flags
  uint16_t secs = 0;     // StreamBench: how long the notes hold
};
```

И объявления `void encode(const Debug& m, Writer& w);`, `bool decode(Reader& r, Debug& m);` рядом с остальными.

`link_msg.cpp`, в `encode(const Status&)` после `w.bytes(m.scope, n);`:

```cpp
  w.u8(m.streamVoices);
  w.u16(m.underruns);
  w.u16(m.minReserveMs);
  w.u8(m.psram);
  w.u8(m.psramFill);
```

В `decode(Reader&, Status&)` после `r.bytes(m.scope, m.scopeN);`:

```cpp
  m.streamVoices = r.u8();
  m.underruns = r.u16();
  m.minReserveMs = r.u16();
  m.psram = r.u8();
  m.psramFill = r.u8();
```

```cpp
void encode(const Debug& m, Writer& w) {
  w.u8(m.op);
  w.u8(m.a);
  w.u8(m.b);
  w.u16(m.secs);
}

bool decode(Reader& r, Debug& m) {
  m.op = r.u8();
  m.a = r.u8();
  m.b = r.u8();
  m.secs = r.u16();
  return r.ok();
}
```

Status со scope: 28 + 256 + 7 = 291 Б < `kMaxPayload` (512).

**Step 4: Повторный запуск**

Run: `pio test -e native -f test_link_msg`
Expected: PASS.

Run: `pio run -e wt32 -e teensy41 -e teensy41-desk -e teensy41-bench`
Expected: SUCCESS ×4. Обе стороны собраны с протоколом 3. ESP с протоколом 2 покажет у синта `!` в FILE → SYSTEM (`versionOk`), пока обе платы не обновлены.

### Task 23: Teensy: Status со стримом, `Debug`, бенч стрима (desk)

**Files:**
- Modify: `src/teensy/stream.h`, `src/teensy/stream.cpp` (`takeStats(int who)`: два независимых читателя)
- Modify: `src/teensy/link_server.cpp` (`sendStatus`, `case Msg::Debug`)
- Create: `src/teensy/stream_bench.h`, `src/teensy/stream_bench.cpp` (только `LINK_DESK`)
- Modify: `src/teensy/main.cpp` (desk: `sbench::begin`, `sbench::step()` в `loop`)

Нативного теста нет: склейка и бенч на железе. Счётчики и кодек проверены в Task 6, 22.

**Step 1: Два читателя статистики**

`stream.h`: `constexpr int kStatReaders = 2;  // 0: Status, 1: the desk bench` и `Stats takeStats(int who = 0);`. В `stream.cpp` `underrunsSeen`, `minReserve`, `hintsSeen`, `hintSlotsSeen` становятся массивами `[kStatReaders]`. В `step()` минимальный запас обновляется в обоих, `hint()` увеличивает оба счётчика, `takeStats(who)` читает и сбрасывает только свои.

**Step 2: `sendStatus`** (`link_server.cpp`, перед `send(Msg::Status, s)`)

```cpp
  const stream::Stats ss = stream::takeStats(0);
  s.streamVoices = ss.voices;
  s.underruns = ss.underruns;
  s.minReserveMs = ss.minReserveMs;
  s.psram = static_cast<uint8_t>(psram::state());
  s.psramFill = psram::fillPct();
```

`#include "psram.h"`, `#include "stream.h"`. `psram::State` идёт по порядку `Off, On, Failed`, как 0 / 1 / 2 в `Status::psram`.

**Step 3: `Debug`** (`link_server.cpp`, в `switch` рядом с `Msg::Profile`)

```cpp
    case Msg::Debug: onDebug(r); break;
```

```cpp
// Desk tools; a non-desk build ignores them (nothing to inject faults into from the ESP).
void onDebug(Reader& r) {
#ifdef LINK_DESK
  Debug d;
  if (!decode(r, d)) return;
  switch (static_cast<DebugOp>(d.op)) {
    case DebugOp::PsramFault:
      psram::injectFault();
      link::log("psram: fault injected, the next watchdog check fails");
      break;
    case DebugOp::StreamBench: sbench::start(d.a, d.b, d.secs); break;
    case DebugOp::StreamBenchStop: sbench::stop(); break;
  }
#else
  (void)r;
#endif
}
```

С `#ifdef LINK_DESK #include "stream_bench.h" #endif`.

**Step 4: `stream_bench.h`**

```cpp
#pragma once
#include <stdint.h>

namespace mt {
struct SynthModel;
class Synth;
}
class SynthStream;

// Stream bench (desk build): the 16 longest files of /mt-store become SAMPLE instruments
// (kBenchInstr ..), looped from their start and held on tracks 0..15 at the root + semis. Every second
// a log line with the stream stats, at the end a summary. It overwrites the model's samples, those
// instruments and the tracks: on the desk there is no ESP mirror to keep.
namespace sbench {

void begin(mt::SynthModel& m, mt::Synth& s, SynthStream& out);
void start(uint8_t semis, uint8_t flags, uint16_t secs);  // flags: mt::link::kBench*
void stop();
void step();  // loop()

}  // namespace sbench
```

**Step 5: `stream_bench.cpp`**

```cpp
#ifdef LINK_DESK
#include "stream_bench.h"
#include <Arduino.h>
#include <Audio.h>
#include <SD.h>
#include <stdio.h>
#include <string.h>
#include "audio_out.h"
#include "fs_server.h"
#include "link_msg.h"
#include "link_server.h"
#include "psram.h"
#include "sample_store.h"
#include "stream.h"
#include "synth.h"

namespace sbench {

namespace {

constexpr int kN = 16;
constexpr int kBenchInstr = mt::kInstruments - 1 - kN;  // the top instrument stays as it is
constexpr uint32_t kSettleMs = 2000;                     // first notes start on heads / cold slots
static_assert(kBenchInstr >= 0, "stream bench layout");

mt::SynthModel* model = nullptr;
mt::Synth* synth = nullptr;
SynthStream* out = nullptr;

bool running = false;
uint8_t note = 60;
uint32_t t0 = 0, lastLog = 0, endMs = 0;
uint32_t underruns = 0;
uint16_t minRes = 0xFFFF;
bool settled = false;

struct Found {
  uint32_t crc = 0, frames = 0;
};

// The kN longest store files (by size), longest first. Returns how many.
int scanStore(Found* f) {
  int n = 0;
  FsFile dir = SD.sdfs.open(mt::kStoreDir);
  if (!dir) return 0;
  FsFile e;
  char name[16];
  while (e.openNext(&dir, O_RDONLY)) {
    e.getName(name, sizeof name);
    uint32_t crc = 0;
    char ext[5] = {};
    if (strlen(name) == 12 && sscanf(name, "%8lx.%3s", reinterpret_cast<unsigned long*>(&crc), ext) == 2 &&
        strcmp(ext, "wav") == 0 && e.fileSize() > mt::kStoreDataOffset) {
      Found x;
      x.crc = crc;
      x.frames = static_cast<uint32_t>((e.fileSize() - mt::kStoreDataOffset) / 2);
      int at = n < kN ? n++ : kN;  // insertion, longest first
      for (; at > 0 && f[at - 1].frames < x.frames; --at)
        if (at < kN) f[at] = f[at - 1];
      if (at < kN) f[at] = x;
    }
    e.close();
  }
  dir.close();
  return n;
}

void notes(bool on) {
  AudioNoInterrupts();
  for (int t = 0; t < kN; ++t) {
    const uint8_t m[3] = {static_cast<uint8_t>((on ? 0x90 : 0x80) | t), note, static_cast<uint8_t>(on ? 100 : 0)};
    synth->event(0, static_cast<uint8_t>(t), m, 3);
  }
  AudioInterrupts();
}

}  // namespace

void begin(mt::SynthModel& m, mt::Synth& s, SynthStream& o) {
  model = &m;
  synth = &s;
  out = &o;
}

void start(uint8_t semis, uint8_t flags, uint16_t secs) {
  if (running) stop();
  if (!card::ready()) {
    link::log("sbench: no card");
    return;
  }
  Found f[kN];
  const int n = scanStore(f);
  if (n < kN) link::log("sbench: only %d store files (want %d long ones: LD bank <wav>)", n, kN);
  if (!n) return;
  out->park(true);
  synth->reset();
  model->sampleCount = static_cast<uint8_t>(n);
  for (int i = 0; i < n; ++i) {
    mt::ProjSample& ps = model->samples[i];
    snprintf(ps.name, sizeof ps.name, "SB%02d", i);
    ps.crc = f[i].crc;
    ps.frames = f[i].frames;
    mt::Instrument& in = model->instruments[kBenchInstr + i];
    mt::instrSetType(in, mt::InstrType::Sample);
    strncpy(in.sample, ps.name, sizeof in.sample);
    in.root = 60;
    in.vol = 100;
    in.loop = static_cast<uint8_t>(mt::LoopMode::Forward);
    in.loopStart = 0;
  }
  for (int t = 0; t < kN; ++t) {
    model->tracks[t].out = mt::TrackOut::Int;
    model->tracks[t].instr = static_cast<uint8_t>(kBenchInstr + (t < n ? t : t % n));
    model->tracks[t].vol = 100;
  }
  psram::hold(flags & mt::link::kBenchSdOnly);
  stream::rescan(*model);  // store entries, heads, and the PSRAM plan (none if held)
  out->park(false);
  link::log("sbench: %d samples, %lu..%lu frames, %s", n, static_cast<unsigned long>(f[n - 1].frames),
            static_cast<unsigned long>(f[0].frames), (flags & mt::link::kBenchSdOnly) ? "SD only" : "PSRAM allowed");
  if (flags & mt::link::kBenchSetupOnly) return;
  note = static_cast<uint8_t>(60 + (semis > 24 ? 24 : semis));
  running = true;
  settled = false;
  t0 = lastLog = millis();
  endMs = t0 + kSettleMs + secs * 1000u;
  underruns = 0;
  minRes = 0xFFFF;
  notes(true);
}

void stop() {
  if (!running) return;
  notes(false);
  running = false;
  link::log("sbench: done, %lu underruns in %lu s, min reserve %u ms", static_cast<unsigned long>(underruns),
            static_cast<unsigned long>((millis() - t0 - kSettleMs) / 1000), minRes);
  psram::hold(false);
}

void step() {
  if (!running) return;
  const uint32_t now = millis();
  if (!settled) {
    if (now - t0 < kSettleMs) return;
    stream::takeStats(1);  // the cold start does not count
    settled = true;
    lastLog = now;
    return;
  }
  if (now - lastLog >= 1000) {
    lastLog = now;
    const stream::Stats s = stream::takeStats(1);
    underruns += s.underruns;
    if (s.minReserveMs < minRes) minRes = s.minReserveMs;
    link::log("sbench: %3lu s  voices %2u  underruns %u (%lu)  min reserve %u ms",
              static_cast<unsigned long>((now - t0 - kSettleMs) / 1000), s.voices, s.underruns,
              static_cast<unsigned long>(underruns), s.minReserveMs);
  }
  if (static_cast<int32_t>(now - endMs) >= 0) stop();
}

}  // namespace sbench
#endif  // LINK_DESK
```

Если имена полей `Instrument` (`loop`, `loopStart`, `vol`, `root`, `sample`) или `card::ready()` отличаются, взять их из `test/test_sample_stream` (Task 5) и `stream.cpp` (Task 9). Нота каждой дорожки — `0x9t` с каналом дорожки, как в `cmd_notes`.

**Step 6: `main.cpp`** (desk)

```cpp
#ifdef LINK_DESK
#include "stream_bench.h"
#endif
...
  // setup(), after link::begin(model, synth, out):
#ifdef LINK_DESK
  sbench::begin(model, synth, out);
#endif
...
  // loop(), after psram::step():
#ifdef LINK_DESK
  sbench::step();
#endif
```

(`model`, `synth`, `out` — те же объекты, что получает `link::begin`; после Task 9 объект выхода зовётся `out`.)

**Step 7: Сборка**

Run: `pio run -e teensy41 -e teensy41-desk -e teensy41-bench`
Expected: SUCCESS. В `teensy41` и `teensy41-bench` бенча нет (`#ifdef LINK_DESK`).

Desk-проверка — в Task 25, после команд `link_desk.py`.

### Task 24: ESP: поля стрима, строка FILE → SYSTEM «Samples», значок `SD!`

**Files:**
- Create: `lib/core/src/stream_info.h`, `lib/core/src/stream_info.cpp` (текст строки Samples)
- Test: `test/test_stream_info/test_main.cpp` (Create)
- Modify: `src/link/link.h`, `src/link/link.cpp` (`StatusAgg`, `StreamInfo`, `onStatus`)
- Modify: `src/audio/audio.h`, `src/audio/audio_link.cpp` (`Load::underruns`, `streamInfo()`)
- Modify: `src/ui/proj_screen.h` (`kSamples` в `enum Row` после `kCard`), `src/ui/proj_screen.cpp` (строка), `src/ui/app.h`, `src/ui/app.cpp` (`pollCpu`, поле CPU)

**Step 1: Падающий тест**

```cpp
#include <unity.h>
#include "stream_info.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static const char* line(uint8_t psram, uint8_t fill, uint32_t underruns) {
  static char b[40];
  streamLine(b, sizeof b, psram, fill, underruns);
  return b;
}

void test_stream_line() {
  TEST_ASSERT_EQUAL_STRING("SD, UNDERRUNS 0", line(0, 0, 0));
  TEST_ASSERT_EQUAL_STRING("SD+PSRAM 45%, UNDERRUNS 3", line(1, 45, 3));
  TEST_ASSERT_EQUAL_STRING("SD+PSRAM, UNDERRUNS 12", line(1, 100, 12));
  TEST_ASSERT_EQUAL_STRING("SD, PSRAM FAILED, UNDERRUNS 1", line(2, 80, 1));
}

void test_stream_line_fits_small_buffers() {
  char b[8];
  streamLine(b, sizeof b, 1, 45, 3);
  TEST_ASSERT_EQUAL_STRING("SD+PSRA", b);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_stream_line);
  RUN_TEST(test_stream_line_fits_small_buffers);
  return UNITY_END();
}
```

**Step 2: Запуск, ожидается падение**

Run: `pio test -e native -f test_stream_info`
Expected: FAIL: `fatal error: stream_info.h: No such file or directory`

**Step 3: Реализация `stream_info`**

`lib/core/src/stream_info.h`:

```cpp
#pragma once
#include <stdint.h>

namespace mt {

// FILE -> SYSTEM "Samples": where samples play from (Status::psram 0 off, 1 on, 2 failed; fill %) and the
// voices that ran out of data since the synth board came up.
void streamLine(char* out, int n, uint8_t psram, uint8_t fill, uint32_t underruns);

}  // namespace mt
```

`lib/core/src/stream_info.cpp`:

```cpp
#include "stream_info.h"
#include <stdio.h>

namespace mt {

void streamLine(char* out, int n, uint8_t psram, uint8_t fill, uint32_t underruns) {
  const unsigned long u = underruns;
  if (psram == 1 && fill < 100) snprintf(out, n, "SD+PSRAM %u%%, UNDERRUNS %lu", fill, u);
  else if (psram == 1) snprintf(out, n, "SD+PSRAM, UNDERRUNS %lu", u);
  else if (psram == 2) snprintf(out, n, "SD, PSRAM FAILED, UNDERRUNS %lu", u);
  else snprintf(out, n, "SD, UNDERRUNS %lu", u);
}

}  // namespace mt
```

Run: `pio test -e native -f test_stream_info` → PASS.

**Step 4: Линк ESP** (`src/link/link.h`, `link.cpp`)

В `StatusAgg`: `uint32_t underruns;  // voices that ran out of data` и `uint16_t minReserveMs;  // lowest, 0xFFFF = none`. Новая структура и функция:

```cpp
// Sample streaming as the newest Status tells it, and the underruns since the synth board came up.
struct StreamInfo {
  uint8_t voices, psram, psramFill;
  uint32_t underruns;
};
StreamInfo streamInfo();
```

`link.cpp`: `StreamInfo stream = {};` под `mux`. В `onStatus`, внутри критической секции:

```cpp
  agg.underruns += s.underruns;
  if (s.minReserveMs < agg.minReserveMs) agg.minReserveMs = s.minReserveMs;
  stream.voices = s.streamVoices;
  stream.psram = s.psram;
  stream.psramFill = s.psramFill;
  stream.underruns += s.underruns;
```

`takeStatus()` после копирования обнуляет `agg` так, чтобы `minReserveMs = 0xFFFF`. `streamInfo()` копирует `stream` под `mux`. Новое приветствие (`Hello` с новым `bootId`) обнуляет `stream.underruns` там же, где сбрасываются остальные счётчики синта.

**Step 5: `audio`** (`audio.h`, `audio_link.cpp`)

- `struct Load` += `uint32_t underruns;` (за окно). `Acc` += `underruns`, `absorb()`: `load.underruns += a.underruns;`, `takeLoad()`: `l.underruns = load.underruns;`. Инициализатор `Load l{0, 0, 0, 0}` → `{0, 0, 0, 0, 0}`.
- `struct StreamInfo { uint8_t voices, psram, psramFill; uint32_t underruns; }; StreamInfo streamInfo();` — обёртка над `slink::streamInfo()`, как `linkCounters()`.

**Step 6: FILE → SYSTEM** (`file_screen.h` `SysRow` после `kCard`, `file_screen.cpp` `sysParams_`; код ниже — по образцу соседних строк `sysParams_`)

```cpp
  // Samples: where they play from (the synth board's SD, plus PSRAM copies) and the voices that ran out
  // of data since it came up (an underrun: that voice faded out for a moment).
  params_[kSamples] = {"Samples",
                       [](char* o, int n) {
                         if (!audio::synthUp()) {
                           snprintf(o, n, "NO SYNTH");
                           return;
                         }
                         const audio::StreamInfo s = audio::streamInfo();
                         mt::streamLine(o, n, s.psram, s.psramFill, s.underruns);
                       },
                       [](int) {}};
```

`#include "stream_info.h"`. Строку `kSamples` добавить на страницу SYS рядом с `kCard`, там же, где перечислены строки страницы (по образцу `kCard`).

**Step 7: Значок** (`app.h`, `app.cpp`)

`app.h`: `uint32_t sdRedUntil_ = 0;  // millis() until which the CPU field shows SD! (a sample voice underran)` и `bool sdShown_ = false;  // the CPU field shows SD!`.

`pollCpu`, после `if (l.stalls) cpuRedUntil_ = ...`:

```cpp
  if (l.underruns) sdRedUntil_ = now + kCpuRedMs;
  const bool sd = static_cast<int32_t>(sdRedUntil_ - now) > 0;
```

Цвет: `red` также при `sd`. В условие перерисовки добавить `sd != sdShown_`, при перерисовке `sdShown_ = sd`. В отрисовке поля CPU:

```cpp
  // SD!: a sample voice ran out of data (the card fell behind) in the last kCpuRedMs.
  snprintf(buf, sizeof(buf), "%s %3d%%", sdShown_ ? "SD!" : "CPU", cpu_ > 999 ? 999 : cpu_);
```

Ширина поля не меняется (3 символа метки).

**Step 8: Сборка**

Run: `pio run -e wt32` и `pio test -e native -f test_stream_info`
Expected: SUCCESS, PASS.

### Task 25: `link_desk.py`: протокол 3, поля стрима, `bench-stream`, `psram-fault`

**Files:**
- Modify: `scripts/link_desk.py`

**Step 1: Падающий selftest** (`cmd_selftest`, перед `print("selftest ok")`)

```python
    assert PROTOCOL == 3 and MSG["Debug"] == 41
    st = struct.pack("<BHHBH", 50, 0, 0, 20, 1000) + bytes(16) + struct.pack("<HH", 0, 2) + b"\x01\x02" + \
        struct.pack("<BHHBB", 14, 3, 41, 1, 57)
    s = parse_status(st)
    assert s["smp"] == 14 and s["under"] == 3 and s["reserve"] == 41 and s["psram"] == "on" and s["fill"] == 57
    assert debug_payload(2, 12, 1, 600) == bytes([2, 12, 1]) + struct.pack("<H", 600)
```

Run: `python3 scripts/link_desk.py selftest`
Expected: FAIL: `AssertionError` (`PROTOCOL == 2`).

**Step 2: Реализация**

- `PROTOCOL = 3`; в строку имён `MSG` после `Perf` — `Debug`.
- `parse_status`: после scope.

```python
PSRAM_STATE = ["off", "on", "FAILED"]


def parse_status(p):
    cpu, stalls, late, voices, out_peak = struct.unpack("<BHHBH", p[:8])
    peaks = list(p[8:24])
    lost, scope_n = struct.unpack("<HH", p[24:28])
    i = 28 + scope_n
    smp, under, reserve, psram, fill = struct.unpack("<BHHBB", p[i:i + 7])
    return dict(cpu=cpu, stalls=stalls, late=late, voices=voices, out=out_peak, lost=lost,
                peaks=peaks, scope=scope_n, smp=smp, under=under, reserve=reserve,
                psram=PSRAM_STATE[psram] if psram < 3 else psram, fill=fill)
```

- `print_status`: перед `| peaks` добавить `smp %2d under %d res %s psram %s %d%%`, где `res` — `"-"` при `0xFFFF`, иначе `"%d ms"`.
- Команды:

```python
def debug_payload(op, a=0, b=0, secs=0):
    return struct.pack("<BBBH", op, a, b, secs)


def cmd_psram_fault(link, args):
    """Debug PsramFault: the next watchdog check reads back a flipped bit (desk build). Then Status for
    --secs: psram FAILED, the voices go on from SD."""
    cmd_hello(link, args)
    link.send("Debug", debug_payload(1))
    cmd_status(link, args)


def cmd_bench_stream(link, args):
    """Debug StreamBench (desk build): the 16 longest /mt-store files held on tracks 0..15 at the root
    (+ --semis), --secs long; the board logs a line a second and a summary. --sd: no PSRAM copies;
    --setup: samples and instruments only (for 'hint'); --fault-at S: PsramFault S s into the measurement.
    Ctrl-C stops the bench."""
    cmd_hello(link, args)
    flags = (1 if args.sd else 0) | (2 if args.setup else 0)
    link.send("Debug", debug_payload(2, args.semis, flags, args.secs))
    if args.setup:
        time.sleep(2.0)
        return
    t0 = time.monotonic()
    end = t0 + args.secs + 4
    last = 0
    fault = args.fault_at is not None
    try:
        while time.monotonic() < end:
            if fault and time.monotonic() - t0 >= args.fault_at + 2:  # + the bench's 2 s settling
                link.send("Debug", debug_payload(1))
                fault = False
            p = link.wait("Status", 1.0)
            if p and time.monotonic() - last >= 5:
                last = time.monotonic()
                print_status(parse_status(p))
    except KeyboardInterrupt:
        link.send("Debug", debug_payload(3))
        time.sleep(0.5)
```

- `main`: `pf = sub.add_parser("psram-fault")`, `pf.add_argument("secs", type=float, nargs="?", default=3.0)`; `bs = sub.add_parser("bench-stream")` с `--semis` (int, 0), `--secs` (int, 60), `--sd` (`store_true`), `--setup` (`store_true`), `--fault-at` (int, по умолчанию `None`). В словарь: `"psram-fault": cmd_psram_fault, "bench-stream": cmd_bench_stream`.
- Строки `[synth] sbench: ...` печатает уже существующий разбор `Log` в `_rx`.

**Step 3: Повторный selftest**

Run: `python3 scripts/link_desk.py selftest`
Expected: `selftest ok`.

**Step 4: Desk-проверка** (голый Teensy с картой Kingston и PSRAM, прошивка `teensy41-desk`)

Подготовка (один раз): 16 разных WAV длиной ≥ 60 с (любая частота и формат, импорт приведёт к канону).

```sh
for i in $(seq -w 1 16); do
  LD card put long$i.wav /desk/long$i.wav && LD bank /desk/long$i.wav
done
```

Каждый импорт заканчивается строкой `import: Ok crc XXXXXXXX, ... frames at 44100 Hz`.

1. `LD hello` → `protocol 3 ok`.
2. `LD status 3` → в строках `smp  0 under 0 res - psram on 100%` (сэмплов в модели нет, план пуст).
3. `LD bench-stream --secs 30`. В логе: `sbench: 16 samples, ... PSRAM allowed`, затем `psram: N samples planned, M KB` (N — сколько коротких влезло в 8 МБ). Раз в секунду `sbench:  K s  voices 1x  underruns 0 (0)  min reserve R ms`. `voices` = 16 − N: копии из PSRAM слот не занимают. В конце `sbench: done, 0 underruns in 30 s`.
4. `LD bench-stream --sd --secs 30` → `voices 16`, `underruns 0`.
5. `LD bench-stream --secs 60 --fault-at 20`. Через 20 с в логе `psram: fault injected`, затем `psram failed: the watchdog read back a wrong word; SD only until a reboot`, в Status `psram FAILED`. Бенч доигрывает, `voices` вырастает до 16. Недогрузки допустимы только в момент перехода: голос, игравший из PSRAM, начинает со слота без загруженных блоков. Без бенча то же даёт `LD psram-fault`: Status 3 с с `psram FAILED`.

### Проверка этапа 6

Run: `pio run -e wt32 -e teensy41 -e teensy41-desk -e teensy41-bench`
Expected: SUCCESS ×4.

Run: `pio test -e native`
Expected: PASS, `test_link_msg` +2, добавился `test_stream_info` (2).

Run: `python3 scripts/link_desk.py selftest`
Expected: `selftest ok`.

---

## Этап 7 — проверка на железе и документация

Кода в этапе нет, кроме правок документации. Проверка повторяет критерии дизайна («Проверка», «Готово, когда»). Если пункт не проходит, нужно вернуться в задачу, к которой он относится (указана в скобках), и чинить через `superpowers:systematic-debugging`.

### Task 26: Desk- и аппаратная проверка по критериям дизайна

**Files:** нет (только прогоны). Результаты (числа из логов) записать в итоговую сводку пользователю.

**Сборки** (свежие, из чистого дерева):

Run: `pio run -e wt32 -e teensy41 -e teensy41-desk -e teensy41-bench`
Expected: SUCCESS ×4.

**1. Замер RAM2** (отклонение 5, «Числа»)

Run: `pio run -e teensy41 -v 2>&1 | grep -E "RAM1|RAM2"`
Expected: строка `teensy_size`, где `RAM2: variables:` около 130 752 (было 32 448) и `free for malloc/new:` — записать (оценка до мержа main ≈393 536). После загрузки (`LD status 2` на `teensy41-desk`, или лог через ESP на `teensy41`): `synth board up, ... delay 2000 ms, PERF 6 s, RAM2 free N`, N ≥ 90 000 (оценка ≈104 КБ; записать фактическое). Если delay 1500 ms — что-то ещё съело RAM2 (проверить, что PERF не взят из RAM2, отклонение 14). Пул в `DMAMEM` проверяется по `RAM2: variables:` выше.

**2. Подготовка desk** (голый Teensy, карта Kingston, PSRAM на CS1, прошивка `teensy41-desk`): 16 длинных WAV в хранилище, как в Task 25 Step 4.

**3. 16 голосов на исходной высоте, Kingston, 10 минут** (критерий «Готово»)

Run: `LD bench-stream --sd --secs 600`
Expected: каждую секунду `voices 16`, в конце `sbench: done, 0 underruns in 600 s, min reserve R ms`, R > 30 (ниже 30 мс фоновые задачи уже не получали бы порций). В строках Status `stalls 0`, `cpu` — как у пула-бенча с 16 сэмплами из flash ± несколько процентов (копирование секторов идёт в `loop()`, а не в прерывании).

**4. Октава выше, Kingston**

Run: `LD bench-stream --sd --semis 12 --secs 120`
Expected: без зависаний, линк жив (Status каждую секунду), `min reserve` меньше, чем в п. 3. Цель — 0 недогрузок; если они есть, записать число.

**5. SmartBuy** (та же подготовка на карте SmartBuy Blue, см. `memory/sd-card-dropouts.md`)

Run: `LD bench-stream --sd --secs 120`, затем `LD bench-stream --sd --semis 12 --secs 120`
Expected: на исходной высоте записать число недогрузок (цель — 0). Октава выше — недогрузки допустимы. Без зависаний и сбросов: `stalls` не растёт, в логе нет `card: reset` чаще, чем без бенча.

**6. С PSRAM**

Run: `LD bench-stream --secs 120`
Expected: `psram: N samples planned`, через секунды наполнения `voices` = 16 − (число готовых копий), `fill` в Status доходит до 100 %. 0 недогрузок.

**7. Сторож PSRAM**

Run: `LD bench-stream --secs 60 --fault-at 20`
Expected: как в Task 25 Step 4 п. 5. После `psram failed` до перезагрузки `psram FAILED` в каждом Status, наполнение не возобновляется (`LD bench-stream --secs 10` после этого: `psram: 0 samples planned` не печатается, `fill` не растёт). После перезагрузки (`pio run -e teensy41-desk -t upload`) снова `psram ok`.

**8. Вынимание карты**

Run: `LD bench-stream --sd --secs 120`, на 30-й секунде вынуть карту, на 60-й вставить.
Expected: после вынимания недогрузки растут (голоса без «голов» уходят в фейд), Status продолжает приходить (линк жив), `lost 0`. После вставки в логе автомонтирование, через 1–2 с недогрузки прекращаются, голоса звучат снова (петля возвращает их к началу, которое есть в «головах» и слотах). Teensy не зависает и не перезагружается (`boot` в `LD hello` не меняется).

**9. Подсказка** (этап 5)

Run: `LD bench-stream --sd --setup`, затем `LD hint 0 60 --ofs 128 --lead 125`
Expected: `hint: track 0 note 60 ofs 128 -> slot S`, S ≥ 0. В Status `under 0`, нота звучит с середины сэмпла без фейда. Для сравнения `LD hint 0 60 --ofs 128 --lead 0`: подсказка приходит вместе с нотой, возможна одна недогрузка (начало из «головы» есть только у старта и петли).

**10. Рендер со стримом = рендер из RAM** (с ESP, прошивка `teensy41`, ESP на проводах)

Проект с 2–4 сэмплами общей длиной < 8 МБ (все влезут в PSRAM), 8 тактов.

1. Загрузить проект, дождаться `SD+PSRAM, UNDERRUNS 0` в FILE → SYSTEM → Samples (заполнение 100 %). FILE → Render WAV → `/renders/a.wav`.
2. Прошить `teensy41` без PSRAM: `PLATFORMIO_BUILD_FLAGS=-DMT_NO_PSRAM pio run -e teensy41 -t upload`. В FILE → SYSTEM → Samples — `SD, UNDERRUNS 0`. Тот же рендер → `/renders/b.wav`.
3. Сравнить файлы на компьютере (карта Teensy через кардридер): `cmp a.wav b.wav` — без вывода. Рендер грузит синхронно (`pool.sync`, отклонение 9), поэтому стрим даёт бит-в-бит то же, что целые копии. Вернуть обычную прошивку: `pio run -e teensy41 -t upload`.

**11. С ESP: живое исполнение** (прошивка `teensy41`)

- Проект с нарезанным лупом (SLICE NOTE, 16 слайсов) на 16-х, BPM 180, и вторая дорожка с длинным сэмплом и OFS на каждом шаге. 5 минут игры: `SAMPLES ... UNDERRUNS 0`, значок `SD!` не появляется.
- Тот же проект на SmartBuy при BPM 300: если `SD!` появляется, транспорт не останавливается, линк жив (`LOST 0` в строке Link).
- Импорт длинного WAV (BANK → Import) во время игры: звук не прерывается (импорт без `park`, Task 15). Загрузка проекта (Load): короткая пауза на `park`, как раньше.

### Task 27: Документация

**Files:**
- Modify: `docs/manual.md` (строка состояния ≈ стр. 94–118; FILE → SYSTEM ≈ стр. 847; `### SAMPLES: project samples` ≈ стр. 999–1040; `## Internal sound` ≈ стр. 1103; `## Limits` ≈ стр. 1128)
- Modify: `docs/manual_ru.md` (те же места, те же номера строк ± несколько)
- Modify: `docs/plans/future-teensy.md` (раздел «ОБЯЗАТЕЛЬНО ПОЗЖЕ: стриминг длинных сэмплов с SD», стр. 82)

Перед правкой каждого места читать только окрестность (Read с `offset` / `limit`), а не файл целиком.

**Step 1: `manual.md`**

- Строка состояния, таблица полей (рядом со строкой `SYNTH FW`):

  | `SD!` | Red, in place of `CPU` for 2 s: a sample voice ran out of data (the synth board's card fell behind) and faded out for a moment. Playback goes on. Often: a slow card, many long samples pitched up. See [Samples and SD cards](#streaming). |

- FILE → SYSTEM, таблица (после строки `Card`):

  | Samples | SD, SD+PSRAM n%, UNDERRUNS n | display only: where samples play from (the synth board's card; with PSRAM, short ones from memory once the copy is n % done; `PSRAM FAILED` — it failed a check, SD only until the synth board restarts) and how many times a sample voice ran out of data since it came up |

- `### SAMPLES: project samples`, таблица «Where / What»: строку `synth board's flash, sample bank` заменить двумя:

  | `/mt-store/` on the synth board's card | where samples play from: one file per sample (named by its checksum), shared by all projects. Import writes here, Load fills it from the project folder, Save copies from it to the folder. Do not edit by hand; Clear cache removes files no project in use needs. |
  | synth board's flash | wavetables and the first 100 ms of every sample (the start, the loop start, 25 ms at each slice), so a note starts at once while the rest loads from the card |

  Таблица WAV: `Format` → `PCM 8, 16, 24-bit, float 32-bit.`; `Size` → `up to the free space on the synth board's card`. Верхняя строка экрана (`FREE n / m KB CACHE k KB`) теперь про flash: таблицы и начала сэмплов.

- Новый подраздел после `### SAMPLES: project samples` (перед `<a id="wifi"></a>`):

```markdown
<a id="streaming"></a>

### Samples and SD cards

Samples play from the synth board's card as they go (streaming), so their length is limited only by the card. The first 100 ms of the start, the loop start and every slice are kept in the synth board's flash: a note starts at once, and the card has that long to deliver the rest. The sequencer also tells the synth board one step ahead where the next sample note will start (OFS included), so even a start in the middle of a long sample is ready in time.

- **Up to 16 sample voices stream at once.** A 17th takes the place of the oldest one (it fades out in 4 ms, as when voices run out).
- **PSRAM** (an 8 MB chip on the synth board, optional): after a project loads, its samples are copied there in the background, shortest first, until it is full. A copied sample plays from memory and does not count towards the 16. The synth board tests the chip at power-on and checks it every 100 ms; if it fails, samples go on from the card until the next restart (FILE → SYSTEM → Samples: `PSRAM FAILED`).
- **Underrun:** if the card falls behind, that voice fades out for a moment and comes back when the data arrives; playback does not stop. The status bar shows `SD!`, FILE → SYSTEM → Samples counts them.
- **Cards:** use a fast, known-good card (tested: Kingston Canvas Select Plus — 16 voices at the original pitch with no underruns). Cheap cards (SmartBuy) can underrun with many long samples pitched up. FAT32, one partition (MBR).
- **Taking the card out** while playing: samples keep playing as far as their starts in flash go, then fade; nothing hangs. Put the card back and they come back.
- Old projects whose samples were in the synth board's flash are moved to the card (`/mt-store/`) on the first start after the update; then flash keeps only the starts.
```

- `## Internal sound`: в пункте `**Voices:**` добавить предложение `Sample voices stream from the card, up to 16 at once (samples copied to PSRAM do not count), see [Samples and SD cards](#streaming).`. Пункт про `only when stopped` (последний в списке): `Wavetable import, Compact and Clear cache are available only when stopped: there is no sound while the synth board writes to its flash. Sample import works while playing.` — только если ESP после этапа 3 действительно пускает импорт сэмпла во время игры (`STOP PLAYBACK FIRST` в `src/ui/bank_screen.cpp` / `samples_screen.cpp`). Если UI по-прежнему требует стоп, пункт не трогать.
- `## Limits`: после строки `Instruments / voices`:

  | Sample voices streaming at once | 16 (samples in PSRAM do not count) |
  | Sample length | up to the card's free space (mono, 16-bit, up to 44.1 kHz after import) |

**Step 2: `manual_ru.md`** — те же места, те же правки по-русски:

- Строка состояния: `| \`SD!\` | Красным вместо \`CPU\` на 2 с: сэмпл-голосу не хватило данных (карта синт-платы не успела), он на миг затих. Воспроизведение идёт дальше. Чаще всего — медленная карта, много длинных сэмплов выше исходной высоты. См. [Сэмплы и SD-карты](#streaming). |`
- FILE → SYSTEM: `| Samples | SD, SD+PSRAM n%, UNDERRUNS n | только показ: откуда играют сэмплы (карта синт-платы; с PSRAM короткие — из памяти, когда копия готова на n %; \`PSRAM FAILED\` — не прошла проверку, до перезапуска синт-платы только SD) и сколько раз сэмпл-голосу не хватило данных с её включения |`
- «Где / Что» в `### SAMPLES: сэмплы проекта`: строку `flash синт-платы, банк` заменить на

  | `/mt-store/` на карте синт-платы | отсюда играют сэмплы: по файлу на сэмпл (имя — контрольная сумма), общие для всех проектов. Import пишет сюда, Load докладывает из папки проекта, Save копирует отсюда в папку. Руками не править; Clear cache удаляет файлы, не нужные текущему проекту. |
  | flash синт-платы | wavetable-таблицы и первые 100 мс каждого сэмпла (начало, начало петли, по 25 мс у каждого слайса): нота начинается сразу, пока остальное читается с карты |

  WAV: `Формат` → `PCM 8, 16, 24 бит, float 32 бит.`; `Размер` → `сколько свободно на карте синт-платы`.
- Новый подраздел `### Сэмплы и SD-карты` с якорем `<a id="streaming"></a>`, перевод английского текста выше, теми же пунктами: стриминг с карты и «головы» во flash 100 мс; подсказка секвенсора на шаг вперёд (и для OFS); до 16 стрим-голосов, 17-й вытесняет старейший (фейд 4 мс); PSRAM — копии от коротких к длинным, в лимит не входят, тест при включении и проверка раз в 100 мс, при сбое до перезапуска только SD (`PSRAM FAILED`); недогрузка — голос на миг затихает, воспроизведение не останавливается, `SD!` и счётчик в FILE → SYSTEM → Samples; карты — Kingston Canvas Select Plus без недогрузок на 16 голосах, дешёвые (SmartBuy) могут не успевать на длинных сэмплах выше исходной высоты, FAT32 + MBR; вынутая карта — доигрывают начала, остальное затихает, ничего не зависает; старые проекты переносятся из flash на карту при первом запуске.
- `## Внутренний звук`, `**Голоса:**` и последний пункт, `## Лимиты` — как в английском: `| Сэмпл-голосов стримом одновременно | 16 (сэмплы в PSRAM не считаются) |`, `| Длина сэмпла | сколько свободно на карте (моно, 16 бит, до 44,1 кГц после импорта) |`.

**Step 3: `future-teensy.md`**

Заголовок `## ОБЯЗАТЕЛЬНО ПОЗЖЕ: стриминг длинных сэмплов с SD` → `## Сделано: стриминг сэмплов с SD`. Текст раздела заменить на:

```markdown
Сделано 2026-10 (дизайн [2026-10-10-sample-streaming-design.md](2026-10-10-sample-streaming-design.md), план [2026-10-10-sample-streaming.md](2026-10-10-sample-streaming.md)): все сэмплы играют стримом с SD Teensy (хранилище `/mt-store/<crc>.wav`, пул 16 слотов × 6 КБ в RAM2, планировщик по запасу в `loop()`), «головы» во flash, упреждение `0xF4` от секвенсора, PSRAM (8 МБ на CS1, своя инициализация) — кэш целых сэмплов со сторожем. Лимит банка и запись сэмплов во flash ушли. Открыто: асинхронный DMA-ридер SDIO (если SmartBuy-класс карт понадобится на октаву выше), стерео-сэмплы.
```

Ссылку на якорь `#обязательно-позже-стриминг-длинных-сэмплов-с-sd` в `2026-10-10-sample-streaming-design.md` (стр. 3) заменить на `#сделано-стриминг-сэмплов-с-sd`. Если в «Порядок работ» (стр. 86) есть пункт про стриминг, отметить его сделанным.

**Step 4: Проверка ссылок**

Run: `grep -n "streaming\|stream" docs/manual.md | head` и `grep -n "#streaming\|id=\"streaming\"" docs/manual_ru.md`
Expected: якорь `streaming` есть в обоих файлах, ссылки из строки состояния и `Internal sound` / `Внутренний звук` на него указывают.

### Проверка этапа 7

Run: `pio run -e wt32 -e teensy41 -e teensy41-desk -e teensy41-bench`
Expected: SUCCESS ×4.

Run: `pio test -e native`
Expected: PASS (весь набор).

Run: `python3 scripts/link_desk.py selftest`
Expected: `selftest ok`.

Все пункты Task 26 пройдены, числа (min reserve, недогрузки по картам, RAM2) — в сводке пользователю. Обновить `memory/project-status.md`: стриминг сэмплов с SD, PSRAM — сделано, проверено на desk / с ESP.

---

### Замечания к этапам 1–3

1. `sample_region.h` (Task 1) в комментарии ссылается на `Synth::prefetchPoint`, которой нигде нет. Функция подсказки — `Synth::prefetch` (Task 20). При реализации Task 1 исправить комментарий на `Synth::prefetch`.
2. `stream::Stats` / `takeStats()` (Task 9) с одним читателем. Task 21 добавляет `hints`, Task 23 — второго читателя (`takeStats(int who)`). Если Task 9 уже реализован, это правка, а не новый код.
3. Порядок `setup` (Task 9): `bank::begin` вызывает `stream::rescan` раньше `stream::begin`. Это работает, потому что таблица — глобальный объект. `psram::begin(model)` (Task 18) идёт после `stream::begin` и сам запускает наполнение для модели загрузки: ранний `rescan` при `st == Off` ничего в PSRAM не планирует.
4. `bank::sampleAccess(..., whole)` (Task 15 Step 7) должен отдавать указатель PSRAM только при `stream::pool().psramOk`: после сбоя сторожа таблица ещё держит указатели до следующего `rescan`. `StoreSampleSource::find` (Task 8) так и делает, инструменты (пики, онсеты) должны так же.
5. `kBgMax = 6` (Task 9): фоновых работ теперь три (банк / хранилище, инструменты, `PsramFill`), запас есть.
6. Подсказка (Task 19) смотрит на инструмент из `TrackCfg`. PGM на шаге и KIT-дорожки подсказку не получают: их начала идут из «голов».
