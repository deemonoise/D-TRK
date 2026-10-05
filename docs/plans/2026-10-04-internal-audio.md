# Встроенный звук (chiptune + сэмплер) — план реализации

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** дорожки трекера играют встроенным синтом (chiptune и сэмплы из банка во flash) через усилитель NS4168 на разъёме SPK.

**Architecture:** секвенсор не меняет тайминг: события INT-дорожек уходят не в UART, а через `MidiSink::synth()` в очередь синта с меткой времени. Синт (`lib/core`, без железа, под native-тестами) раскладывает события в блок 128 сэмплов с точностью до сэмпла и рендерит голоса. `src/audio` — только I2S, задача и flash. Сэмплы — отдельный раздел flash, читаются через `esp_partition_mmap`.

**Tech Stack:** C++17, Arduino-ESP32 3.x (pioarduino), ESP-IDF 5 (`driver/i2s_std.h`, `esp_partition.h`), FreeRTOS, LovyanGFX, Unity (`pio test -e native`).

Дизайн: [2026-10-04-internal-audio-design.md](2026-10-04-internal-audio-design.md). **Коммиты — только по просьбе пользователя** (шаги Commit пропускать). Этапы выполнять подряд без остановок на проверку железа; в конце — сводка, что проверить на устройстве.

**Общие правила:**
- После каждой задачи: `pio test -e native` зелёный, `pio run -e wt32` собирается.
- Стиль — как в окружающем коде: `namespace mt` в ядре, короткие комментарии на английском, `clampi`-подобные хелперы локально.
- Всё, что трогает `Project` из UI, — под `engine::lockProject()`.
- Синт читает `Project` (инструменты, громкости) без блокировки: значения однобайтовые, рваное чтение безопасно. Имя сэмпла копируется синтом при note-on.

---

## Этап 1. Аудио-путь и замер

### Task 1: Таблица разделов

**Files:** создать `partitions.csv`, изменить `platformio.ini`.

```
# Name,    Type, SubType,  Offset,   Size
nvs,       data, nvs,      0x9000,   0x5000
otadata,   data, ota,      0xe000,   0x2000
app0,      app,  ota_0,    0x10000,  0x300000
app1,      app,  ota_1,    0x310000, 0x300000
samples,   data, 0x40,     0x610000, 0x9E0000
coredump,  data, coredump, 0xFF0000, 0x10000
```

1. Сверить с `default_16MB.csv` из пакета платформы (`~/.platformio/packages/framework-arduinoespressif32*/tools/partitions/default_16MB.csv`): nvs и otadata должны совпасть по смещению и размеру (сохраняются настройки Wi-Fi в NVS). Если не совпадают — подогнать новую таблицу под старые смещения.
2. `platformio.ini`, `[env:wt32]`: `board_build.partitions = partitions.csv`.
3. `pio run -e wt32` — в выводе `Flash:` меньше 100 % от 3 МБ.
4. В README (раздел «Сборка и прошивка») пометка: после этого изменения один раз прошить по USB (`-e wt32 -t upload`), OTA таблицу разделов не меняет.

### Task 2: Драйвер I2S и аудио-задача с тестовым тоном

**Files:** `src/hw/pins.h`, создать `src/audio/audio.h`, `src/audio/audio.cpp`, изменить `src/main.cpp`, `src/engine/engine.h`, `src/engine/engine.cpp`.

1. Пины NS4168 сверить со схемой WT32-SC01 Plus (Wireless-Tag, раздел I2S/Speaker). Ожидаемо: BCLK 36, LRCK (WS) 35, DOUT 37. В `pins.h`:
```cpp
// Onboard NS4168 class-D amp (SPK connector). Bridge output: neither SPK pin is ground.
constexpr int kI2sBclk = 36;
constexpr int kI2sWs = 35;
constexpr int kI2sDout = 37;
```
2. `engine.h`: `uint64_t nowUs();` — время таймера движка (тот же `gptimer`), для меток событий синта. Реализация — существующая локальная `nowUs()` в `engine.cpp`, вынести в публичную.
3. `audio.h`:
```cpp
#pragma once
#include <stdint.h>
namespace mt { struct Project; }
namespace audio {
constexpr int kRate = 32000;
constexpr int kBlock = 128;  // samples, 4 ms
void begin(mt::Project* p);
// Event for an INT track, stamped with engine::nowUs() time. Called from the engine task.
void post(uint64_t t, uint8_t track, const uint8_t* b, uint8_t len);
// Render time of the last block, us (for the CPU readout).
uint32_t lastRenderUs();
}  // namespace audio
```
4. `audio.cpp` (на этом шаге — только тон 440 Гц, громкость −20 дБ, флаг `kTestTone`):
   - `i2s_new_channel` (I2S_NUM_0, master), `dma_desc_num = 4`, `dma_frame_num = kBlock`; `i2s_channel_init_std_mode` с `I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)`, `I2S_STD_CLK_DEFAULT_CONFIG(kRate)`, пины из `pins.h`, MCLK не используется (`I2S_GPIO_UNUSED`).
   - Задача `audio` на ядре 0, приоритет `configMAX_PRIORITIES - 3` (ниже движка), стек 4096: цикл `render(mono[kBlock])` → дублировать в L/R (`int16_t lr[kBlock * 2]`) → `i2s_channel_write(..., portMAX_DELAY)`. Время `render` мерить `esp_timer_get_time()` и хранить в `lastRenderUs`.
   - Буферы — `static`, во внутренней RAM.
5. `main.cpp`: `audio::begin(project)` после `engine::begin`.
6. `pio run -e wt32`. Пользователь на железе: тон в динамике. Если тишина — поменять местами BCLK и WS.

### Task 3: Замер и проверка mmap

**Files:** `src/audio/audio.cpp`, `src/ui/proj_screen.cpp` (временно — строка CPU).

1. Временный бенчмарк (под `#ifdef AUDIO_BENCH`, флаг в `build_flags`): 16 «голосов» — фазовый аккумулятор `float` + линейная интерполяция из массива во flash + умножение на огибающую. Печатать в Serial `render us / 4000 us` раз в секунду.
2. `esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "samples")` + `esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, &ptr, &handle)` — напечатать `ok`/ошибку. Если весь раздел не отображается — уменьшить окно (отображать по 4 МБ по требованию) и записать решение в дизайн.
3. В PROJ строка `CPU  NN%` (из `lastRenderUs() * 100 / 4000`) — оставить насовсем: полезно пользователю.
4. Записать результат замера в раздел «Риски» дизайна. Если 16 голосов сэмплера > 60 % — снизить пул до 12 (константа `kVoices`).

---

## Этап 2. Chiptune

### Task 4: Модель — инструменты, выход дорожки, громкости

**Files:** `lib/core/src/model.h`, `lib/core/src/model.cpp`, `test/test_model/test_main.cpp`.

1. Тест:
```cpp
void test_reset_audio_defaults() {
  Project p;
  TEST_ASSERT_EQUAL(static_cast<int>(TrackOut::Midi), static_cast<int>(p.tracks[3].out));
  TEST_ASSERT_EQUAL(3, p.tracks[3].instr);  // track N defaults to instrument N
  TEST_ASSERT_EQUAL(100, p.tracks[3].vol);
  TEST_ASSERT_EQUAL(40, p.masterVol);
  TEST_ASSERT_TRUE(p.preview);
  TEST_ASSERT_EQUAL_STRING("INS1", p.instruments[0].name);
  TEST_ASSERT_EQUAL(static_cast<int>(InstrType::Chip), static_cast<int>(p.instruments[0].type));
}
void test_env_time_curve() {
  TEST_ASSERT_EQUAL(0, envTimeMs(0));
  TEST_ASSERT_EQUAL(10000, envTimeMs(127));
  for (int v = 1; v < 127; ++v) TEST_ASSERT_TRUE(envTimeMs(v) <= envTimeMs(v + 1));
}
```
2. FAIL. 3. Код:
```cpp
constexpr int kInstruments = 16;
constexpr int kWavetables = 16;
constexpr int kSampleNameMax = 16;

enum class TrackOut : uint8_t { Midi, Int, Count };
enum class InstrType : uint8_t { Chip, Sample, Count };
// Wt1..Wt16 follow Metal: wave = Wave::Wt1 + n.
enum class Wave : uint8_t { Pulse, Triangle, Saw, Noise, Metal, Wt1 };
constexpr int kWaveCount = static_cast<int>(Wave::Wt1) + kWavetables;
enum class LoopMode : uint8_t { Off, Forward, PingPong, Count };

struct Instrument {
  char name[9] = {0};
  InstrType type = InstrType::Chip;
  uint8_t vol = 100;      // 0..127
  int8_t transpose = 0;   // -24..24 semitones
  int8_t fine = 0;        // -50..50 cents
  uint8_t attack = 0, decay = 40, sustain = 100, release = 30;  // times via envTimeMs, sustain 0..127
  bool mono = false;
  uint8_t glide = 0;      // MONO portamento, SLD units (x4 ms), 0 = off
  uint8_t wave = 0;       // Wave
  uint8_t duty = 50;      // pulse width 1..99 %
  uint8_t pwmRate = 0;    // 0..127, sweep speed
  uint8_t pwmDepth = 0;   // 0..49 %
  char sample[kSampleNameMax + 1] = {0};
  uint8_t root = 60;
  uint16_t start = 0, end = 0xFFFF;  // fraction of the sample, /0xFFFF
  uint8_t loop = 0;                  // LoopMode
  uint16_t loopStart = 0;
  bool reverse = false;
};

// Envelope stage time: 0 -> 0 ms, 1..127 -> 1..10000 ms exponentially.
uint16_t envTimeMs(uint8_t v);
```
   `TrackCfg`: `TrackOut out = TrackOut::Midi; uint8_t instr = 0; uint8_t vol = 100;`
   `Project`: `Instrument instruments[kInstruments]; uint8_t masterVol = 40; bool preview = true;` и `bool trackInternal(int t) const { return tracks[t].out == TrackOut::Int; }`.
   `reset()`: `tracks[i].instr = i`; `instruments[i] = Instrument()`, имя `INS<i+1>`; `masterVol = 40; preview = true;`.
   `envTimeMs`: `v == 0 ? 0 : (uint16_t)lroundf(powf(10000.f, v / 127.f))` (`<math.h>`).
4. PASS, весь native зелёный.

### Task 5: Сохранение — чанки INST, TOUT, AUDI

**Files:** `lib/core/src/project_io.cpp`, `test/test_project_io/test_main.cpp`.

Формат (little-endian, байты явно, не `memcpy` структуры):
- `TOUT`: `u8 count`, затем `count × {u8 out, u8 instr, u8 vol}`.
- `AUDI`: `u8 masterVol, u8 preview`.
- `INST`: `u8 count`, затем `count × 48 байт`: name[8], type, vol, transpose, fine, attack, decay, sustain, release, mono, glide, wave, duty, pwmRate, pwmDepth, sample[16], root, start u16, end u16, loop, loopStart u16, reverse — проверить сумму (8+14+16+1+4+1+2+1 = 47) и добить 1 байтом резерва до 48. Константа `kInstSize = 48`.

1. Тесты:
   - круговой: заполнить нестандартные значения во всех полях 2 инструментов и 2 дорожек → save → load → поля равны;
   - старый файл: сохранить проект, вырезать чанки `INST`/`TOUT`/`AUDI` из байтов (или собрать файл старым путём — сохранить с флагом теста) → load → значения по умолчанию из `reset()`, `LoadErr::Ok`;
   - кламп: `out = 7`, `wave = 200`, `loop = 9`, `vol = 255` → после загрузки `Midi`, `0`, `Off`, `127`;
   - `INST` с `count = 20` и размером на 20 записей: читаются первые 16, остальное пропускается.
2. FAIL. 3. Запись после `TRKS`; чтение — новые ветки в цикле чанков (`project_io.cpp:238`), функции `readTout`, `readAudi`, `readInst` по образцу `readTrks`. Размер чанка меньше ожидаемого → `Truncated`. Строки — всегда с нулём в конце.
4. PASS.

### Task 6: Генераторы и wavetable

**Files:** создать `lib/core/src/synth_osc.h`, `lib/core/src/synth_osc.cpp`, `lib/core/src/wavetables.cpp`, `test/test_synth_osc/test_main.cpp`.

```cpp
namespace mt {
constexpr int kSynthRate = 32000;
constexpr int kWtLen = 32;
extern const int8_t kWavetable[kWavetables][kWtLen];  // -127..127

// Frequency of a (fractional) MIDI note, Hz. 69 = 440.
float noteHz(float note);

// One chip oscillator; phase in [0, 1).
struct ChipOsc {
  float phase = 0;
  uint32_t lfsr = 0x7FFF;
  float noiseVal = 0;
  // Next sample, -1..1. duty 0..0.5 for Pulse; inc = hz / kSynthRate.
  float next(Wave w, float inc, float duty);
};
}
```
- Pulse: `phase < duty ? 1 : -1`. Triangle: `4·|phase − 0.5| − 1`. Saw: `2·phase − 1`.
- Noise / Metal: LFSR 15 бит, шаг при переходе фазы через 1 (частота задаёт скорость шума, как в NES); Metal — обратная связь с бита 6 (короткий период 93). `noiseVal = (lfsr & 1) ? 1 : -1`.
- Wt: `kWavetable[n][(int)(phase * 32)] / 127.f` (без интерполяции — характер чиптюна).
- `wavetables.cpp`: 16 таблиц, генерировать формулами в `constexpr`-массиве или заранее посчитанными числами (скрипт в комментарии): sine; sine+2-я гармоника; sine+3-я; «орган» (1, 2, 4 гармоники); полусинус; квадрат-синус; «вокал A/O» (форманты); ступенчатый синус 4 бит; пила с мягким срезом; FM-подобные `sin(x + k·sin(x))` для k = 1, 2, 3; импульсный 25 %; «колокол» (1, 2.76 частичная, округлённо в 32 точки); шум фиксированный (seed). Важно: каждый массив нормирован в −127..127.

Тесты:
- `noteHz(69) == 440` (±0.01), `noteHz(81) == 880`.
- Pulse duty 0.25: из 32000 сэмплов при 1000 Гц доля `+1` = 25 % ± 1 %.
- Saw 1000 Гц: число переходов через 0 вниз за 1 с = 1000 ± 1.
- Triangle: max 1, min −1, среднее ≈ 0.
- Noise: сумма за 32000 сэмплов по модулю < 5 % (нет постоянной составляющей); Metal повторяется с периодом 93 переходов.
- Все wavetable: max ≤ 127, min ≥ −127, хотя бы одно значение ≥ 100 по модулю.

### Task 7: Огибающая ADSR

**Files:** создать `lib/core/src/synth_env.h`, `test/test_synth_env/test_main.cpp`.

```cpp
// Linear ADSR, advanced once per sample.
class Env {
 public:
  enum class Stage : uint8_t { Idle, Attack, Decay, Sustain, Release };
  void set(uint16_t aMs, uint16_t dMs, float sustain, uint16_t rMs);
  void gate(bool on);  // on: from the current level to Attack (no click); off: Release
  void kill() { stage_ = Stage::Idle; level_ = 0; }
  float next();
  bool idle() const { return stage_ == Stage::Idle; }
  Stage stage() const { return stage_; }
 private: ...
};
```
Время 0 мс — мгновенный переход. Шаг = `1 / (ms · kSynthRate / 1000)`.
Тесты: A=10 мс → уровень 1 через 320 сэмплов (±1); D=10 мс, S=0.5 → 0.5 через ещё 320; держит 0.5; release 10 мс → Idle через 320 (±1) после `gate(false)`; повторный `gate(true)` из release начинается с текущего уровня (нет скачка вниз); все нули — сразу 1, `gate(false)` — сразу Idle.

### Task 8: Голос и распределитель голосов

**Files:** создать `lib/core/src/synth_voice.h`, `lib/core/src/synth_voice.cpp`, `test/test_synth_voice/test_main.cpp`.

```cpp
constexpr int kVoices = 16;
constexpr int kPolyPerTrack = 4;

struct Voice {
  bool on = false;          // allocated (until the envelope goes idle)
  uint8_t track = 0, note = 0;
  uint32_t age = 0;         // allocation order, for stealing
  float pitch = 0;          // current note incl. slide, fractional
  float target = 0;         // slide target
  float slideStep = 0;      // semitones per sample, 0 = none
  float gain = 1;           // velocity * VSL state
  ChipOsc osc;
  Env env;
  // sampler fields come in Task 19
};

// Picks a voice for track/instrument. Mono: the track's voice if any (legato), else a free one.
// Poly: a free voice if the track has < kPolyPerTrack, else the track's oldest.
// No free voice: the globally oldest releasing voice, else the globally oldest.
int allocVoice(Voice (&v)[kVoices], uint8_t track, bool mono, uint32_t& ageCounter, bool& legato);
```
Тесты:
- poly: 4 ноты на дорожке 0 → 4 разных голоса; 5-я забирает самый старый голос дорожки 0, не трогая голос дорожки 1;
- mono: две ноты подряд на дорожке 2 → тот же голос, `legato == true`; после того как огибающая стала Idle (`on = false`) — новый голос, `legato == false`;
- пул заполнен 16 голосами 8 дорожек (по 2) → новая нота дорожки 7 при 2 < 4 берёт самый старый голос в Release, если такой есть, иначе самый старый вообще.

### Task 9: Синт — события, блок, рендер

**Files:** создать `lib/core/src/synth.h`, `lib/core/src/synth.cpp`, `test/test_synth/test_main.cpp`.

```cpp
// Hardware-free synth for INT tracks. Messages are MIDI-like, channel nibble ignored:
// 0x90 note vel (vel 0 = off), 0x80 note, 0xE0 lsb msb (bend, +-2 semitones), 0xC0 prog (instrument),
// 0xF5 fx val (synth fx, Task 12), 0xFF (all off: release every voice of the track).
class Synth {
 public:
  explicit Synth(const Project& p) : p_(p) {}
  // Sample source for SAMPLE instruments (Task 19); nullptr = sampler silent.
  void setBank(const class SampleSource* b) { bank_ = b; }
  // offset: sample index inside the next render() block, 0..kBlock-1.
  void event(int offset, uint8_t track, const uint8_t* b, uint8_t len);
  void startTrack(uint8_t track);  // reset runtime state (instrument from TrackCfg, bend, vsl)
  // Renders kBlock mono samples, mixes in pending events at their offsets.
  void render(int16_t* out);
  static constexpr int kBlock = 128;
  ...
};

// Where an event stamped t lands in the block that starts playing at blockT: sample offset,
// clamped to [0, kBlock - 1]. Late events go to 0, events past the block return -1 (keep for later).
int eventOffset(uint64_t t, uint64_t blockT);
```
- Внутри: очередь событий на блок (массив 64, отсортирован по offset при вставке; переполнение — note-off сохраняются вытеснением других, как в `EventHeap`).
- `render`: идти отрезками между событиями; управление (огибающая уже per-sample; питч, PWM, VIB, ARP, slide) обновлять раз в 32 сэмпла (1 мс) и на событиях.
- Громкость: `osc * env * gain * instr.vol/127 * track.vol/127`, сумма голосов `* masterVol/100 * 0.25` (запас на 16 голосов), soft clip `x / (1 + |x|)` масштабированный так, что ±0.5 проходит почти линейно (`y = 1.5x − 0.5x³` при |x| ≤ 1, иначе ±1), затем `* 32767`.
- Note-on: инструмент — runtime-инструмент дорожки (после PGM) или `TrackCfg::instr`; нота = note + transpose + fine/100; `allocVoice`; в mono-legato — не перезапускать огибающую и фазу, а установить target со скоростью `glide` (если 0 — прыжок).
- Note-off: голос с `track, note`, `on`, не в Release → `env.gate(false)`.
- `0xFF`: все голоса дорожки в Release. Голос с Idle-огибающей → `on = false`.

Тесты (рендер в буфер, анализ):
- `eventOffset`: t = blockT → 0; +31.25 мкс·10 → 10; раньше blockT → 0; ≥ blockT + 4000 мкс → −1.
- Note-on на offset 64, инструмент Saw, A=0: первые 64 сэмпла — ноль, на 64-м уже не ноль.
- Note-off → через release-время голос свободен (`activeVoices() == 0`).
- Дорожка `vol = 0` → тишина; `masterVol = 0` → тишина.
- 16 голосов Pulse duty 50 % на полной громкости → ни одного сэмпла за пределами int16, клип мягкий (не все значения ±32767).
- PGM (`0xC0, 5`) → следующая нота дорожки звучит инструментом 5 (проверить через `voiceInstr(trackVoice)` — тестовый геттер).
- Bend `0xE0` на +2 полутона → частота Saw ×1.122 (по переходам через 0, ±1 %).

### Task 10: Маршрутизация в секвенсоре

**Files:** `lib/core/src/sequencer.h`, `lib/core/src/sequencer.cpp`, `test/test_sequencer/test_main.cpp`.

1. `MidiSink`: добавить `virtual void synth(uint8_t track, const uint8_t* b, uint8_t len) {}` (по умолчанию — ничего; существующие тестовые sink не меняются).
2. В `scheduleStep` для INT-дорожки передавать в `expandStep` копию `TrackCfg` с `channel = tr` (у INT-дорожек канал = номер дорожки: ключ для отдельной таблицы голосов и для ties). Все `push` этой дорожки (NoteOn, NoteOff, контролы через `pushControls`) получают `track = tr` — добавить параметр `track` в `pushControls` и передать его в NoteOff-`push` (сейчас там `kNoTrack`). Активность (`activity_`) по-прежнему только для NoteOn.
3. `Voices intVoices_;` рядом с `voices_`. В `dispatch`:
```cpp
const bool internal = e.track < kTracks && p_.trackInternal(e.track);
Voices& vs = internal ? intVoices_ : voices_;
auto send = [&](const uint8_t* b, uint8_t len) { internal ? out.synth(e.track, b, len) : out.send(b, len); };
```
   и использовать `vs` / `send` вместо `voices_` / `out.send`. Сообщения `0xF5` (синт-fx) на MIDI-дорожке не отправлять никогда.
4. `silence`: для `intVoices_` — `out.synth(ch, {0x80|ch, note, 0}, 3)` (ch = номер дорожки), затем для каждой INT-дорожки `out.synth(t, {0xFF}, 1)`.
5. `sendProgram(track)`: INT-дорожка → `out.synth(track, {0xC0, instr}, 2)` с `instr = TrackCfg::instr`; MIDI — как сейчас.
6. `start()`: для каждой INT-дорожки `out.synth(t, {0xFE}, 1)` — сброс runtime-состояния (`Synth::startTrack`). Добавить `0xFE` в описание сообщений синта.
7. Переключение OUT дорожки во время игры: UI после записи `out` шлёт `engine::post(Cmd::ReleaseTies)` и новую команду `Cmd::TrackOut` (arg = track): движок вызывает `seq->trackOutChanged(track, out)` — отпускает все ноты этой дорожки в обеих таблицах голосов (NoteOff в UART и в синт).

Тесты (`FakeSink` получает `synth()` в отдельный вектор):
- MIDI-дорожка: события только в `send`, `synth` пуст.
- INT-дорожка 2: NoteOn/NoteOff только в `synth` с `track == 2`; в `send` нет нот этой дорожки (clock идёт как раньше).
- INT-дорожка и MIDI-дорожка на одном канале и ноте → обе ноты звучат (таблицы голосов раздельны).
- mute INT-дорожки → в `synth` ничего; `takeActivity()` ставит бит INT-дорожки.
- `stop()` во время ноты INT-дорожки → в `synth` NoteOff и `0xFF`.
- PGM на INT-дорожке → `synth` получает `0xC0`; на MIDI — `send` как раньше.
- Существующие тесты секвенсора без изменений.

### Task 11: Движок и аудио-задача вместе

**Files:** `src/engine/engine.cpp`, `src/engine/engine.h`, `src/audio/audio.cpp`, `src/audio/audio.h`.

1. В движке sink-обёртка вокруг `hw::MidiUart`: `send` → UART, `synth(track, b, len)` → `audio::post(nowUs(), track, b, len)`. Передавать её везде, где сейчас `midi`.
2. `audio::post`: кольцевой буфер SPSC (движок пишет, аудио-задача читает) на 256 записей `{uint64_t t; uint8_t track, len, b[3];}` с `std::atomic` индексами. Переполнение — запись теряется, счётчик потерь (видно в Serial).
3. Аудио-задача: `Synth` во внутренней RAM (`heap_caps_malloc(MALLOC_CAP_INTERNAL)`, placement new). Цикл:
```cpp
const uint64_t blockT = engine::nowUs() + kLatencyUs;  // kLatencyUs = queued DMA blocks * 4000
// take every queued event with eventOffset(t, blockT) >= 0, stop at the first -1 (FIFO = time order per producer)
synth.render(mono);
```
   `kLatencyUs` = 2 блока (8 мс); подобрать на этапе так, чтобы события не опаздывали: если `t < blockT − 4000` — событие опоздало, счётчик опозданий в Serial.
4. Убрать тестовый тон (`kTestTone`), оставить `AUDIO_BENCH` выключенным.
5. `pio run -e wt32`.

### Task 12: UI — вкладки, TRACK, PROJ

**Files:** `src/ui/app.h`, `src/ui/app.cpp`, `src/ui/track_screen.h`, `src/ui/track_screen.cpp`, `src/ui/proj_screen.cpp`, `src/engine/engine.h`, `src/engine/engine.cpp`.

1. `Tab { Grid, Track, Bank, Inst, Proj, File, Count }`, `screens_` с новым `inst_` (Task 13). Ширина вкладки считается от `Tab::Count` — проверить, что подписи влезают в 80 px (шрифт вкладок); если нет — `INST`, `PROJ` сокращения уже короткие.
2. TRACK: строки после `Name`: `Out` (`MIDI`/`INT`, edit ±1 → переключение + `engine::post(Cmd::TrackOut, track)`), `Instr` (1–16, `INSi` + имя инструмента; на INT-дорожке — `engine::post(Cmd::SendProgram, track)`), `Volume` (0–127). На INT-дорожке значения `Channel`, `CC A`, `CC B`, `Program` рисовать цветом `theme::kDim` (в `ParamList` добавить `std::function<bool()> dim` в `Param`, по умолчанию пусто = не тусклый).
3. PROJ: `Volume` (0–100 %, `masterVol`), `Preview` (ON/OFF), строка `CPU NN%` (из Task 3, только чтение).
4. `pio run -e wt32`.

### Task 13: Вкладка INST (CHIP)

**Files:** создать `src/ui/inst_screen.h`, `src/ui/inst_screen.cpp`, изменить `src/ui/app.h`, `src/ui/app.cpp`, `src/ui/names.h` (если там подписи), `src/audio/audio.h`.

- Шапка: `< INS3 BASS >` — выбор инструмента касанием стрелок или первой строкой списка. По умолчанию выбран инструмент текущей дорожки.
- `ParamList` с прокруткой (`setVisibleRows`), строки: Name (как `TrackScreen::editName`), Type, Volume, Transpose, Fine, Attack, Decay, Sustain, Release (время показывать в мс/с через `envTimeMs`), Mode (POLY/MONO), Glide, Wave (`PULSE`, `TRI`, `SAW`, `NOISE`, `METAL`, `WT1`…`WT16`), Duty (%), PWM rate, PWM depth. Для типа SAMPLE строки CHIP скрыты (отдельный массив `Param` на тип, `setParams` при смене типа); строки SAMPLE — Task 20.
- Кнопка PREVIEW (касание, правый верх): `audio::preview(instr, 60)` — нота 300 мс на выделенном «голосе предпрослушки» (дорожка `kPreviewTrack = 8` внутри синта: синт держит runtime-инструмент для 9 «дорожек», громкость дорожки 100).
- `audio.h`: `void preview(uint8_t instr, uint8_t note);` — кладёт в очередь `0xC0 instr`, `0x90 note 100`, отложенный `0x80` (аудио-задача сама шлёт off через 300 мс по `nowUs`). Это вызов из UI-задачи: очередь SPSC — значит, нужна вторая очередь для UI (или мьютекс на запись). Сделать вторую SPSC-очередь `uiQ`.
- `pio run -e wt32`.

---

## Этап 3. Эффекты и предпрослушка

### Task 14: Новые fx в модели, редакторе и раскрытии шага

**Files:** `lib/core/src/model.h`, `lib/core/src/fx_info.cpp`, `lib/core/src/fx_info.h`, `lib/core/src/step_expand.h`, `lib/core/src/step_expand.cpp`, `test/test_fx_info/test_main.cpp`, `test/test_expand/test_main.cpp`.

1. `Fx`: после `PGM` — `SLD, VIB, ARP, VSL, OFS, CUT`, затем `Count`. Старые файлы совместимы (значения < старого `Count` те же).
2. `fx_info`: `{"SLD",1,255,16,false}`, `{"VIB",0,255,0x44,false}`, `{"ARP",0,255,0x37,false}`, `{"VSL",-64,63,-8,true}`, `{"OFS",0,255,0,false}`, `{"CUT",1,96,6,false}`. VIB и ARP форматировать hex `%02X` (по образцу форматирования CND). Функция `bool fxSynthOnly(Fx f)` — true для шести новых.
3. `EvKind::SynthFx`; в `StepEvent` для неё `note = cmd`, `vel = val`. `expandStep`: для новых fx — если `t.out == TrackOut::Int`, событие `SynthFx` в блок контролов (до нот, offset 0, как CC); на MIDI-дорожке — ничего.
4. `Sequencer::pushControls`: `EvKind::SynthFx` → `push(t, 0xF5, cmd, val, 0, false, 3, track)`.
5. Тесты: имена и диапазоны новых fx; `fxSynthOnly`; `expandStep` на INT-дорожке с `VIB 0x44` → первое событие `SynthFx` с cmd VIB, затем нота; на MIDI-дорожке — только нота; в секвенсоре INT-дорожка с `ARP` → `synth` получает `0xF5 ARP val` раньше `0x90`.
6. GRID: на MIDI-дорожке имена новых fx рисовать `theme::kDim` (в месте отрисовки fx-колонки, `grid_screen.cpp`).

### Task 15: Эффекты в синте

**Files:** `lib/core/src/synth.h`, `lib/core/src/synth.cpp`, `test/test_synth/test_main.cpp`.

Состояние на дорожку (`TrackRt`): instr, bend, pending fx (применяются к следующему note-on того же события-времени), vsl, last note.

| fx | Реализация |
|---|---|
| SLD v | при следующем note-on дорожки: голос не перезапускается, если у дорожки есть звучащий голос (как legato), `target = note`, скорость = `|target − pitch| / (v · 4 мс · 32)` сэмплов. Без ноты в шаге (fx на пустом шаге) — slide текущего голоса к последней ноте дорожки |
| VIB xy | LFO синус, частота `x · 0.5 Гц` (x = 0 — выкл.), глубина `y / 15 · 2` полутона, для голосов дорожки до следующего VIB или note-on без VIB (сбрасывается на новом шаге с нотой) |
| ARP xy | питч голоса = note + {0, x, y}[k], k меняется каждые `stepUs / 3` — синт не знает шаг: секвенсор передаёт длину шага вторым сообщением `0xF5, 0xF0, stepUs/256` (служебный cmd 0xF0) перед ARP; при отсутствии — 1/3 от 125 мс |
| VSL v | `gain += v/64 · 1` за шаг, линейно на протяжении шага (та же длина шага, что для ARP); кламп 0..1; сохраняется до следующего note-on |
| OFS v | следующий note-on SAMPLE стартует с `start + v/256 · (end − start)` (Task 19) |
| CUT n | через `n · tickUs` (тик = шаг / ticksPerStep: служебное сообщение с `tickUs`) — голоса дорожки `env.kill()` |

Служебное сообщение длины шага (`0xF5, 0xF0, stepUs >> 8`) и тика (`0xF5, 0xF1, tickUs >> 4`): секвенсор шлёт их INT-дорожке в `scheduleStep`, только если на шаге есть ARP, VSL или CUT. Добавить тест в `test_sequencer`.

Тесты (`test_synth`): по каждому fx — частота/громкость в нужный момент:
- SLD 25 (100 мс) от 60 к 72 → через 50 мс частота между, через 110 мс = noteHz(72) ± 1 %;
- VIB 0x8F → частота колеблется в пределах ±2 полутона, период 250 мс ± 5 %;
- ARP 0x47 при stepUs 120000 → три частоты по 40 мс: 60, 64, 67;
- VSL −64 за шаг 100 мс → к концу шага громкость ≈ 0;
- CUT 3 при tickUs 5208 → тишина через 15.6 мс ± 1 блок управления.

### Task 16: Предпрослушка в GRID

**Files:** `src/ui/grid_screen.cpp`, `src/audio/audio.h`.

При вводе ноты (энкодер в колонке ноты, `trackKey` в edit) на INT-дорожке и `project.preview` — `audio::preview(trackInstr, note)`. MIDI-дорожки — как сейчас (никакой предпрослушки). Отдельно не тестируется (UI), проверка сборкой.

---

## Этап 4. Банк сэмплов

### Task 17: Парсер WAV

**Files:** создать `lib/core/src/wav.h`, `lib/core/src/wav.cpp`, `test/test_wav/test_main.cpp`.

```cpp
struct WavInfo {
  uint16_t channels, bits;
  uint32_t rate;
  uint32_t dataOffset, dataBytes;
  uint32_t frames() const { return dataBytes / (channels * bits / 8); }
};
enum class WavErr : uint8_t { Ok, NotWav, Unsupported, Truncated };
// Reads RIFF/WAVE header chunks from src until "data"; skips LIST, fact, cue, etc.
WavErr wavParse(ByteSource& src, WavInfo& out);
// Converts n frames of raw data (any channels, 8/16/24 bit PCM) to mono int16.
void wavToMono(const uint8_t* raw, uint32_t frames, const WavInfo& w, int16_t* out);
// Streaming resampler to <= 32 kHz: linear interpolation with a 2-tap average when rate > 32000.
class Downsampler { ... };
```
Тесты на собранных в памяти WAV (хелпер, строящий байты заголовка): 16 бит моно 32 кГц; 8 бит (беззнаковые, 128 = 0); 24 бит; стерео сводится как среднее; `LIST` перед `data` пропускается; `fmt` с `audioFormat = 3` (float) → `Unsupported`; `WAVE_FORMAT_EXTENSIBLE` с PCM-подтипом — поддерживается; обрезанный файл → `Truncated`; ресемплер 48000 → 32000: 4800 входных кадров дают 3200 ± 1 выходных, синус 1 кГц остаётся 1 кГц.

### Task 18: Аллокатор банка

**Files:** создать `lib/core/src/sample_bank.h`, `lib/core/src/sample_bank.cpp`, `test/test_sample_bank/test_main.cpp`.

```cpp
// Flash access behind an interface: RAM in tests, esp_partition on the device.
struct BankFlash {
  virtual uint32_t size() const = 0;
  virtual bool read(uint32_t off, void* d, uint32_t n) = 0;
  virtual bool erase(uint32_t off, uint32_t n) = 0;  // 4 KB aligned
  virtual bool write(uint32_t off, const void* d, uint32_t n) = 0;
  virtual const uint8_t* mapped() const = 0;  // whole partition, or nullptr
};

constexpr int kBankEntries = 128;
constexpr uint32_t kBankAlign = 4096;
constexpr uint32_t kBankHeader = 8192;  // header + table, 2 sectors

struct BankEntry {
  char name[kSampleNameMax + 1];
  uint32_t offset;   // bytes from partition start, kBankAlign aligned, 0 = free slot
  uint32_t frames;   // int16 mono
  uint32_t rate;
  uint8_t root;
  uint8_t loop;      // LoopMode default
};

class SampleBank {
 public:
  explicit SampleBank(BankFlash& f) : f_(f) {}
  bool mount();   // reads the table; formats an empty bank if the magic is wrong
  int count() const;
  const BankEntry* entry(int i) const;
  int find(const char* name) const;  // -1 if absent
  uint32_t freeBytes() const;        // total, may be fragmented
  // Streaming add: begin reserves the first hole that fits, write appends, commit stores the entry.
  bool begin(const char* name, uint32_t frames, uint32_t rate, uint8_t root);
  bool write(const int16_t* d, uint32_t frames);
  bool commit();
  void abort();
  bool remove(int i);
  bool compact();   // moves data down, rewrites offsets
  const int16_t* data(int i) const;  // via mapped()
};
```
- Таблица: magic `"MTSB"`, версия 1, затем 128 записей фиксированного размера (имя 17 + 4·3 + 2 → 32 байта с выравниванием). Записывается целиком (стереть 2 сектора, записать).
- Имя уже есть → `begin` возвращает false (UI предложит перезаписать: `remove` + `begin`).
- `compact`: по записям в порядке смещения, данные копируются блоками по 4 КБ через RAM-буфер вниз; таблица переписывается после каждого перемещённого сэмпла (сбой питания теряет максимум один сэмпл).

Тесты (`RamFlash` на 1 МБ, проверяет, что запись идёт только в стёртое — `0xFF`):
- mount пустого → формат, 0 записей, `freeBytes == size − kBankHeader`;
- add 3 сэмпла → данные читаются, смещения выровнены, `find` работает;
- remove средний → add меньший встаёт в дыру; больший — в конец;
- переполнение → `begin` false;
- compact → все данные целы, дыр нет, `freeBytes` не меньше прежнего;
- после `mount` заново (новый объект над той же памятью) — та же таблица;
- `abort` после частичной записи — место не занято.

### Task 19: Банк на устройстве и импорт

**Files:** создать `src/audio/bank.h`, `src/audio/bank.cpp`, изменить `src/hw/sdcard.cpp` (папка `/samples`), `src/audio/audio.cpp`.

- `PartitionFlash : mt::BankFlash` над `esp_partition_*`; `mapped()` — указатель из `esp_partition_mmap` (Task 3). После `erase/write` mmap остаётся валидным, но кэш нужно сбросить: перемапить (`esp_partition_munmap` + `mmap`) после `commit`/`remove`/`compact`.
- `audio::bank()` — глобальный `SampleBank`, `mount()` в `audio::begin`.
- `sdBegin()` создаёт `/samples`.
- `bool importWav(const char* path, const char* name, ImportProgress cb)` — только при остановленном движке (проверка `engine::status().playing` — иначе ошибка `Busy`), на время записи аудио-задача приостанавливается (флаг, рендерит тишину и не читает банк). Чтение WAV блоками 4 КБ с SD, `wavToMono`, `Downsampler`, `bank.write`. Корневая нота — 60 (или из чанка `smpl`, если есть: `smpl.MIDIUnityNote`, добавить в `wavParse` как опциональное поле — тест в Task 17 дополнить).
- Синт: `SampleSource` (интерфейс в `synth.h`): `const int16_t* find(const char* name, uint32_t& frames, uint32_t& rate)`. Адаптер над `SampleBank`.

### Task 20: FILE → SAMPLES

**Files:** `src/ui/file_screen.h`, `src/ui/file_screen.cpp`.

- Переключатель раздела вверху FILE: `PROJECTS | SAMPLES` (по образцу существующих элементов экрана).
- SAMPLES: список банка (имя, `m:ss`, КБ), полоса свободного места (`freeBytes / size`), команды в контекстном меню (`ui/menu.h`): Import (список `.wav` из `/samples`, выбор → имя по умолчанию = имя файла без расширения, обрезанное до 16, `[A-Za-z0-9_-]`, правка экранной клавиатурой `keyboard.h`), Delete (если имя используется в инструментах проекта — подтверждение «USED BY INS3. DELETE?»), Compact.
- Во время импорта — прогресс на экране; при игре — сообщение `STOP PLAYBACK FIRST`.

---

## Этап 5. Сэмплер

### Task 21: Голос SAMPLE

**Files:** `lib/core/src/synth_voice.h`, `lib/core/src/synth.cpp`, `test/test_synth/test_main.cpp`.

- В `Voice`: `const int16_t* smp; uint32_t smpLen; double pos; double inc; int dir; uint32_t from, to, loopFrom; uint8_t loopMode;`.
- Note-on SAMPLE: сэмпл по имени через `SampleSource` (нет — голос не стартует); `inc = rate / 32000 · 2^((note + transpose + fine/100 − root) / 12)`; `from = start/0xFFFF · len`, `to = end/0xFFFF · len`, `loopFrom = from + loopStart/0xFFFF · (to − from)`; reverse — старт с `to − 1`, `dir = −1`; OFS сдвигает старт.
- Чтение: линейная интерполяция между `smp[i]` и `smp[i+dir]`. Конец: Off — голос в `env.kill()`; Forward — `pos = loopFrom + (pos − to)`; PingPong — разворот `dir`.
- Питч (bend, VIB, ARP, SLD) меняет `inc` на шаге управления.

Тесты (`SampleSource` над массивом): сэмпл 32 кГц, нота = root → выход повторяет данные (с учётом огибающей A=0, S=1); нота root+12 → вдвое быстрее (длина звучания / 2); 16 кГц сэмпл на root → длительность вдвое больше кадров/32000; Forward-loop звучит дольше длины; PingPong — после конца значения идут в обратном порядке; reverse — первый сэмпл = последний в данных; OFS 128 — старт с середины; отсутствующий сэмпл — тишина без падения.

### Task 22: INST для SAMPLE и вид волны

**Files:** `src/ui/inst_screen.h`, `src/ui/inst_screen.cpp`.

- Строки SAMPLE: Sample (выбор из банка энкодером по списку имён; отсутствующее имя — красным, `theme::kRed`), Root (нота, `note_name.h`), Start, End, Loop (OFF/FWD/PING), Loop start, Reverse. Start/End/Loop start — в % с шагом 0.1 % (Shift ×10).
- Вид волны над списком (высота ~60 px): min/max по колонкам из `bank.data()`, маркеры start (зелёный), end (красный), loop (жёлтый). Перерисовка только при изменении (кэш колонок в массиве 480 × 2 байта).

### Task 23: Загрузка .wav через Wi-Fi

**Files:** `lib/core/src/file_rules.h`, `lib/core/src/file_rules.cpp`, `test/test_file_rules/test_main.cpp`, `src/net/web.cpp`.

- `WebDir::Samples` (`"samples"` ↔ `/samples`), имена `<base>.wav` (любой регистр), лимит `kWavMaxBytes = 4 * 1024 * 1024`.
- Тесты: `parseWebDir("samples")`, `webFileAllowed(Samples, "kick.WAV")` true, `"kick.mid"` false, `webMaxBytes(Samples) == 4 МБ`, rename сохраняет `.wav`.
- `web.cpp`: третья вкладка/секция на странице «Samples (SD)» по образцу MIDI; подсказка «импорт в банк — на трекере: FILE → SAMPLES».

---

## Этап 6. Документация и проверка

### Task 24: Документация

**Files:** `README.md`, `docs/manual.html`, `docs/plans/future-audio.md`.

- README: раздел «Звук»: разъём SPK, моно, **предупреждение про мостовой выход** (наушники только через изолированное гнездо и 100–220 Ом, не соединять с GND и с другими устройствами), однократная прошивка по USB из-за таблицы разделов, папка `/samples`, форматы WAV, объём банка.
- Руководство: вкладка INST (все параметры), OUT/INSTR/VOLUME в TRACK, Volume/Preview/CPU в PROJ, FILE → SAMPLES, новые fx (таблица из дизайна), PGM и PBN на INT-дорожках.
- `future-audio.md`: «вариант 1, этапы chiptune и сэмплер — реализованы», ссылка на дизайн; FM / VA / reverb / рисование wavetable — по-прежнему в планах.

### Task 25: Проверка

1. `pio test -e native` — все зелёные.
2. `pio run -e wt32` — без ошибок и новых предупреждений в своих файлах.
3. Сводка пользователю — что проверить на железе:
   - один раз прошить по USB; настройки Wi-Fi сохранились;
   - тон / звук INT-дорожки в динамике; CPU в PROJ при 16 голосах;
   - щелчки при перерисовке экрана и при прокрутке GRID;
   - задержка внутреннего звука относительно MIDI-дорожки (на слух);
   - импорт WAV с SD и через Wi-Fi, удаление, уплотнение;
   - громкость по умолчанию в наушниках (через резисторы!).
