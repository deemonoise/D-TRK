# Кнопки дорожек со светодиодами — план реализации

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 8 кнопок дорожек на PCF8575 (выбор / mute / ввод ступеней лада) и 8 светодиодов в свичах (вспышки нот, выбранная дорожка).

**Architecture:** Чистая логика (ступень → нота, маска LED, активность дорожек в секвенсоре) — в `lib/core` под native-тестами. Драйвер PCF8575 — своя задача FreeRTOS, кладёт события в общую очередь ввода и пишет маску LED. UI: App раздаёт `TrackPress`, GridScreen вводит ноты, App::tick считает маску LED.

**Tech Stack:** C++17, Arduino-ESP32 3.x (pioarduino), FreeRTOS, Unity (`pio test -e native`).

Дизайн: [2026-10-04-track-buttons-design.md](2026-10-04-track-buttons-design.md). Коммиты — только по просьбе пользователя.

**Важно:** тач LovyanGFX занимает I2C-порт 1 (`lgfx_config.h`, `cfg.i2c_port = 1`), а это `Wire1`. Расширитель — на `Wire` (порт 0), пины GPIO 1/2.

---

### Task 1: Активность дорожек в секвенсоре

**Files:** `lib/core/src/event_heap.h`, `lib/core/src/sequencer.h`, `lib/core/src/sequencer.cpp`, `test/test_sequencer/test_main.cpp`

1. Тест:
```cpp
void test_activity_marks_sounding_tracks() {
  p->patterns[0].steps[2][0].note = 60;
  p->patterns[0].steps[5][0].note = 62;
  p->tracks[5].mute = true;
  seq->start(0, *sink);
  TEST_ASSERT_EQUAL_HEX8(0, seq->takeActivity());
  run(0, 1000);
  TEST_ASSERT_EQUAL_HEX8(1 << 2, seq->takeActivity());
  TEST_ASSERT_EQUAL_HEX8(0, seq->takeActivity());  // cleared by take
}
```
2. `pio test -e native -f test_sequencer` — FAIL (нет `takeActivity`).
3. `SchedEvent`: поле `uint8_t track;` после `cont` (занимает байт выравнивания, `sizeof == 24` остаётся). `constexpr uint8_t kNoTrack = 0xFF;`.
   `push(..., uint8_t len = 3, uint8_t track = kNoTrack)` пишет `e.track`. NoteOn в `scheduleStep` (продолжение тая и обычный) передают `tr`.
   `dispatch`: когда NoteOn реально уходит в `out` и `e.track < kTracks` — `activity_ |= 1 << e.track`.
   `uint8_t takeActivity() { const uint8_t a = activity_; activity_ = 0; return a; }`.
4. Тест PASS, весь `pio test -e native` зелёный.

### Task 2: Ступень лада → нота

**Files:** `lib/core/src/scale.h`, `lib/core/src/scale.cpp`, `test/test_scale/test_main.cpp`

`int degreeNote(int button, uint8_t root, ScaleType t, int base);` — `button` 0..7, `base` — C октавы (кратно 12). Ступени из маски (Chromatic → маска Major); `k` — число ступеней; нота = `base + root + deg[button % k] + 12 * (button / k)`; выше 127 — вниз октавами.

Тесты: C major base 60 → 60,62,64,65,67,69,71,72; PentMin root 0 → кн. 5,6,7 = 72,75,77; D major кн. 0 = 62; Chromatic = мажор; base 120 кн. 7 → ≤127 и та же ступень.

### Task 3: Маска светодиодов

**Files:** создать `lib/core/src/track_leds.h`, `test/test_track_leds/test_main.cpp`

```cpp
// Bit = LED on. Flashing tracks light up; the selected track is lit and goes dark on a flash.
inline uint8_t trackLedMask(int selected, uint8_t flash) {
  const uint8_t sel = selected >= 0 && selected < 8 ? 1u << selected : 0;
  return static_cast<uint8_t>((flash & ~sel) | (sel & ~flash));
}
```
Заглушённые дорожки не вспыхивают сами (секвенсор их не играет), отдельного аргумента не нужно. Тесты: пустая вспышка → только выбранная; вспышка на выбранной → гаснет; вспышка на других → горят вместе с выбранной.

### Task 4: Драйвер PCF8575

**Files:** `src/hw/pins.h`, `src/hw/input.h`, `src/hw/input.cpp`, создать `src/hw/trackio.h`, `src/hw/trackio.cpp`

- `pins`: `kXSda = 1, kXScl = 2, kXInt = 42`.
- `InputType::TrackPress` (`delta` = номер 0..7). `void inputPush(InputType t, int8_t d);` — для других драйверов, `shift` берётся из `shiftHeld`.
- `trackio`: `bool trackioBegin();` (false — расширителя нет), `void trackLeds(uint8_t mask);`.
  - `Wire.begin(1, 2, 400000)`, проверка `beginTransmission(0x20)/endTransmission() == 0`, запись 0xFFFF.
  - INT: `attachInterrupt(42, isr, FALLING)` ставит флаг.
  - Задача 5 мс: при флаге или раз в 20 мс — `requestFrom(0x20, 2)`; антидребезг по 5 мс (одно чтение в 5 мс: состояние меняется, если два чтения подряд совпали и отличаются от текущего); нажатие → `inputPush(TrackPress, i)`.
  - Маска LED изменилась — запись `{0xFF, ~mask}`.

### Task 5: Активность в engine

**Files:** `src/engine/engine.h`, `src/engine/engine.cpp`

`std::atomic<uint8_t> activity`; в `run()` после `process` — `activity.fetch_or(seq->takeActivity())`. `uint8_t takeActivity()` — `activity.exchange(0)`.

### Task 6: UI

**Files:** `src/ui/app.h`, `src/ui/app.cpp`, `src/ui/grid_screen.h`, `src/ui/grid_screen.cpp`, `src/main.cpp`

- `App::onInput`: `TrackPress` до меню/bpmEdit: если открыто меню — игнор; `tab_ == Grid && grid_.trackKey(n, shift)` — обработано; иначе Shift → `toggleMute(n)`, без Shift → `setCurTrack(n)`.
- `GridScreen::trackKey(n, shift)`: Euclid открыт → true (игнор). edit → ввод ступени (`degreeNote`, base из ноты под курсором / `lastNote_`, Shift +12), курсор на поле kNote не обязателен — пишем ноту; `moveStep(1)`; true. Не edit и без Shift — `setCurTrack(n)`, `cursorMoved()`, true. Иначе false.
- `App::toggleMute(n)` — как в `grid_screen.cpp` (lock, `t.mute = !t.mute`, `markDirty`).
- `App::tick`: `engine::takeActivity()` → `flashUntil_[i] = now + 50`; маска вспышек; `hw::trackLeds(trackLedMask(curTrack_, flash))`.
- `main.cpp`: `hw::trackioBegin()` после `inputBegin()`.

### Task 7: Проверка

`pio test -e native`, `pio run -e wt32` — без ошибок.

### Task 8: Документация

README (пины 1/2/42, схема кнопок и LED), `docs/manual.html` (раздел «Кнопки дорожек»), `enclosure/README.md` (LED в свичах), `future-track-buttons.md` — «реализовано», ссылка на дизайн.
