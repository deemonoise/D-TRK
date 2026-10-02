# Этапы 1–2: каркас, железо, модель, движок — план реализации

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Рабочая прошивка WT32-SC01 Plus: модель проекта, движок секвенсора с точным таймингом, MIDI OUT с clock, энкодер/кнопки/тач, простая сетка 8×N с правкой нот и транспортом.

**Architecture:** Вся логика (модель, развёртка шага, очередь событий, секвенсор) — чистый C++ в `lib/core`, без Arduino, покрыта native-тестами (Unity). Секвенсор не знает о времени железа: получает `now` в мкс и возвращает время следующего вызова. Прошивка: задача движка на ядре 0, будится аппаратным `gptimer`; UI/ввод на ядре 1; доступ к проекту под мьютексом.

**Tech Stack:** PlatformIO, pioarduino (Arduino-ESP32 3.x / ESP-IDF 5), LovyanGFX, IDF `pulse_cnt`, `gptimer`, `uart`; Unity для native-тестов.

Дизайн: `docs/plans/2026-10-01-midi-tracker-design.md`.

Соглашения:
- путь проекта содержит пробел — всегда в кавычках: `cd "/Users/deemonoise/Documents/PlatformIO/Projects/MIDI -tracker"`;
- `pio` = `~/.platformio/penv/bin/pio`;
- native-тесты: `pio test -e native`; сборка прошивки: `pio run -e wt32`.

---

### Task 0: Каркас проекта

**Files:**
- Modify: `platformio.ini`
- Delete: `src/main.cpp` (шаблон)
- Create: `src/main.cpp` (заглушка), `lib/core/src/.keep`

**Step 1: Заменить `platformio.ini`**

```ini
[platformio]
default_envs = wt32

[env:wt32]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
board = esp32-s3-devkitc-1
framework = arduino
board_build.arduino.memory_type = qio_qspi
board_build.flash_mode = qio
board_build.flash_size = 16MB
board_upload.flash_size = 16MB
board_build.partitions = default_16MB.csv
build_flags =
  -DBOARD_HAS_PSRAM
  -DARDUINO_USB_CDC_ON_BOOT=1
lib_deps =
  lovyan03/LovyanGFX@^1.2.7
monitor_speed = 115200
test_ignore = *

[env:native]
platform = native
test_framework = unity
build_flags = -std=c++17 -Wall -Wextra
```

**Step 2: Заглушка `src/main.cpp`**

```cpp
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
}

void loop() {
  delay(1000);
  Serial.println("alive");
}
```

**Step 3: Проверить сборку**

Run: `pio run -e wt32`
Expected: `SUCCESS` (первый раз скачает платформу pioarduino, несколько минут).

**Step 4: Commit**

```bash
git add platformio.ini src/main.cpp
git commit -m "chore: switch to pioarduino, 16MB flash, PSRAM, native env"
```

---

### Task 1: Модель данных

**Files:**
- Create: `lib/core/src/model.h`, `lib/core/src/model.cpp`
- Test: `test/test_model/test_main.cpp`

**Step 1: Написать падающий тест**

```cpp
#include <unity.h>
#include "model.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_step_is_six_bytes_and_empty_by_default() {
  TEST_ASSERT_EQUAL(6, sizeof(Step));
  Step s;
  TEST_ASSERT_TRUE(s.isEmpty());
  TEST_ASSERT_FALSE(s.hasNote());
  s.note = 60;
  TEST_ASSERT_TRUE(s.hasNote());
  s.note = kNoteOff;
  TEST_ASSERT_FALSE(s.hasNote());
}

void test_find_fx() {
  Step s;
  s.fx[1] = {Fx::RAT, 4};
  TEST_ASSERT_NULL(s.find(Fx::PRB));
  TEST_ASSERT_NOT_NULL(s.find(Fx::RAT));
  TEST_ASSERT_EQUAL(4, s.find(Fx::RAT)->val);
}

void test_ticks_per_step() {
  TEST_ASSERT_EQUAL(96, ticksPerStep(Resolution::Quarter));
  TEST_ASSERT_EQUAL(24, ticksPerStep(Resolution::Sixteenth));
  TEST_ASSERT_EQUAL(12, ticksPerStep(Resolution::ThirtySecond));
  TEST_ASSERT_EQUAL(32, ticksPerStep(Resolution::EighthTriplet));
  TEST_ASSERT_EQUAL(16, ticksPerStep(Resolution::SixteenthTriplet));
}

void test_gate_percent_encoding() {
  TEST_ASSERT_EQUAL(1, gatePercent(1));
  TEST_ASSERT_EQUAL(100, gatePercent(100));
  TEST_ASSERT_EQUAL(107, gatePercent(101));
  TEST_ASSERT_EQUAL(800, gatePercent(200));
  TEST_ASSERT_EQUAL(800, gatePercent(255));
}

void test_project_reset_defaults() {
  Project* p = new Project();
  TEST_ASSERT_EQUAL(120, p->bpm);
  for (int i = 0; i < kTracks; ++i) TEST_ASSERT_EQUAL(i, p->tracks[i].channel);
  TEST_ASSERT_EQUAL_STRING("TRK1", p->tracks[0].name);
  TEST_ASSERT_EQUAL(16, p->patterns[3].length);
  TEST_ASSERT_TRUE(p->patterns[3].isEmpty());
  delete p;
}

void test_track_audible_mute_solo() {
  Project* p = new Project();
  TEST_ASSERT_TRUE(p->trackAudible(0));
  p->tracks[0].mute = true;
  TEST_ASSERT_FALSE(p->trackAudible(0));
  p->tracks[2].solo = true;
  TEST_ASSERT_TRUE(p->trackAudible(2));
  TEST_ASSERT_FALSE(p->trackAudible(1));
  delete p;
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_step_is_six_bytes_and_empty_by_default);
  RUN_TEST(test_find_fx);
  RUN_TEST(test_ticks_per_step);
  RUN_TEST(test_gate_percent_encoding);
  RUN_TEST(test_project_reset_defaults);
  RUN_TEST(test_track_audible_mute_solo);
  return UNITY_END();
}
```

**Step 2: Запустить, убедиться что падает**

Run: `pio test -e native -f test_model`
Expected: FAIL — `model.h: No such file or directory`.

**Step 3: Реализация**

`lib/core/src/model.h`:

```cpp
#pragma once
#include <stdint.h>

namespace mt {

constexpr int kTracks = 8;
constexpr int kMaxSteps = 128;
constexpr int kMinSteps = 4;
constexpr int kDefaultSteps = 16;
constexpr int kPatterns = 16;
constexpr int kChainMax = 64;
constexpr int kPpqn = 96;

constexpr uint8_t kNoteEmpty = 0xFF;
constexpr uint8_t kNoteOff = 0xFE;
constexpr uint8_t kVelDefault = 0;
constexpr uint8_t kNoProgram = 0xFF;

enum class Fx : uint8_t {
  None = 0, CHN, RAT, PRB, GAT, TIE, NDG, CHD, STR, CND, VRN, NRN, CCA, CCB, PBN, PGM, Count
};

struct FxSlot {
  Fx cmd = Fx::None;
  uint8_t val = 0;
};

struct Step {
  uint8_t note = kNoteEmpty;
  uint8_t vel = kVelDefault;
  FxSlot fx[2];

  bool isEmpty() const {
    return note == kNoteEmpty && vel == kVelDefault &&
           fx[0].cmd == Fx::None && fx[1].cmd == Fx::None;
  }
  bool hasNote() const { return note < 128; }
  const FxSlot* find(Fx f) const {
    if (fx[0].cmd == f) return &fx[0];
    if (fx[1].cmd == f) return &fx[1];
    return nullptr;
  }
};
static_assert(sizeof(Step) == 6, "Step must stay 6 bytes (file format)");

enum class Resolution : uint8_t {
  Quarter, Eighth, Sixteenth, ThirtySecond, EighthTriplet, SixteenthTriplet, Count
};

inline uint16_t ticksPerStep(Resolution r) {
  switch (r) {
    case Resolution::Quarter: return 96;
    case Resolution::Eighth: return 48;
    case Resolution::Sixteenth: return 24;
    case Resolution::ThirtySecond: return 12;
    case Resolution::EighthTriplet: return 32;
    case Resolution::SixteenthTriplet: return 16;
    default: return 24;
  }
}

// GAT value: 1..100 -> 1..100 %, 101..200 -> 107..800 % (7 % per unit).
inline uint16_t gatePercent(uint8_t v) {
  if (v == 0) return 1;
  if (v <= 100) return v;
  if (v > 200) v = 200;
  return 100 + (v - 100) * 7;
}

// NDG and PBN keep a signed value in the uint8 slot.
inline int8_t fxSigned(uint8_t v) { return static_cast<int8_t>(v); }

struct Pattern {
  uint8_t length = kDefaultSteps;
  Resolution res = Resolution::Sixteenth;
  uint8_t swing = 50;  // 50..75 %
  Step steps[kTracks][kMaxSteps];

  void clear();
  bool isEmpty() const;
};

struct TrackCfg {
  char name[9] = {0};
  uint8_t channel = 0;  // 0..15
  uint8_t defVel = 100;
  uint8_t defGate = 50;  // %
  uint8_t ccA = 74;
  uint8_t ccB = 71;
  uint8_t program = kNoProgram;
  bool mute = false;
  bool solo = false;
};

struct Project {
  char name[17] = {0};
  uint16_t bpm = 120;
  uint8_t scaleRoot = 0;
  uint8_t scaleType = 0;
  TrackCfg tracks[kTracks];
  Pattern patterns[kPatterns];
  uint8_t chain[kChainMax] = {0};
  uint8_t chainLen = 0;

  Project() { reset(); }
  void reset();
  bool anySolo() const;
  bool trackAudible(int t) const { return !tracks[t].mute && (!anySolo() || tracks[t].solo); }
};

}  // namespace mt
```

`lib/core/src/model.cpp`:

```cpp
#include "model.h"
#include <stdio.h>
#include <string.h>

namespace mt {

void Pattern::clear() {
  length = kDefaultSteps;
  res = Resolution::Sixteenth;
  swing = 50;
  for (auto& tr : steps)
    for (auto& s : tr) s = Step();
}

bool Pattern::isEmpty() const {
  for (const auto& tr : steps)
    for (const auto& s : tr)
      if (!s.isEmpty()) return false;
  return true;
}

void Project::reset() {
  strncpy(name, "untitled", sizeof(name) - 1);
  name[sizeof(name) - 1] = 0;
  bpm = 120;
  scaleRoot = 0;
  scaleType = 0;
  for (int i = 0; i < kTracks; ++i) {
    tracks[i] = TrackCfg();
    tracks[i].channel = i;
    snprintf(tracks[i].name, sizeof(tracks[i].name), "TRK%d", i + 1);
  }
  for (auto& p : patterns) p.clear();
  memset(chain, 0, sizeof(chain));
  chainLen = 0;
}

bool Project::anySolo() const {
  for (const auto& t : tracks)
    if (t.solo) return true;
  return false;
}

}  // namespace mt
```

**Step 4: Запустить тесты**

Run: `pio test -e native -f test_model`
Expected: `6 Tests 0 Failures 0 Ignored`, PASSED.

**Step 5: Commit**

```bash
git add lib/core test/test_model
git commit -m "feat(core): project data model"
```

---

### Task 2: Имена нот и RNG

**Files:**
- Create: `lib/core/src/note_name.h`, `lib/core/src/rng.h`
- Test: `test/test_util/test_main.cpp`

**Step 1: Падающий тест**

```cpp
#include <unity.h>
#include "note_name.h"
#include "rng.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_note_names() {
  char b[4];
  noteName(60, b); TEST_ASSERT_EQUAL_STRING("C-4", b);
  noteName(61, b); TEST_ASSERT_EQUAL_STRING("C#4", b);
  noteName(127, b); TEST_ASSERT_EQUAL_STRING("G-9", b);
  noteName(0, b); TEST_ASSERT_EQUAL_STRING("C-m", b);
  noteName(kNoteEmpty, b); TEST_ASSERT_EQUAL_STRING("---", b);
  noteName(kNoteOff, b); TEST_ASSERT_EQUAL_STRING("OFF", b);
}

void test_rng_deterministic_and_bounded() {
  Rng a(42), b(42);
  for (int i = 0; i < 100; ++i) TEST_ASSERT_EQUAL_UINT32(a.next(), b.next());
  Rng c(0);
  TEST_ASSERT_NOT_EQUAL(0, c.next());
  for (int i = 0; i < 1000; ++i) TEST_ASSERT_TRUE(c.below(100) < 100);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_note_names);
  RUN_TEST(test_rng_deterministic_and_bounded);
  return UNITY_END();
}
```

**Step 2: Run** `pio test -e native -f test_util` — Expected: FAIL (нет заголовков).

**Step 3: Реализация**

`lib/core/src/note_name.h`:

```cpp
#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

// Tracker-style name, 60 = "C-4". Octave -1 is shown as 'm'. out must hold 4 chars.
inline void noteName(uint8_t note, char out[4]) {
  if (note == kNoteEmpty || note > kNoteOff) { out[0] = out[1] = out[2] = '-'; out[3] = 0; return; }
  if (note == kNoteOff) { out[0] = 'O'; out[1] = 'F'; out[2] = 'F'; out[3] = 0; return; }
  static const char kNames[] = "C-C#D-D#E-F-F#G-G#A-A#B-";
  int pc = note % 12;
  int oct = note / 12 - 1;
  out[0] = kNames[pc * 2];
  out[1] = kNames[pc * 2 + 1];
  out[2] = oct < 0 ? 'm' : char('0' + oct);
  out[3] = 0;
}

}  // namespace mt
```

`lib/core/src/rng.h`:

```cpp
#pragma once
#include <stdint.h>

namespace mt {

// xorshift32: small, deterministic with a fixed seed (tests), never yields 0.
struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed = 0x12345678u) : s(seed ? seed : 0x12345678u) {}
  uint32_t next() {
    uint32_t x = s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return s = x;
  }
  uint32_t below(uint32_t n) { return n ? next() % n : 0; }
};

}  // namespace mt
```

**Step 4: Run** `pio test -e native -f test_util` — Expected: PASSED.

**Step 5: Commit**

```bash
git add lib/core/src/note_name.h lib/core/src/rng.h test/test_util
git commit -m "feat(core): note names and xorshift rng"
```

---

### Task 3: Развёртка шага (CHN, RAT, PRB, GAT, TIE, NDG)

Остальные модификаторы (CHD, STR, CND, VRN, NRN, CC, PB, PGM) — в плане этапа 3.

**Files:**
- Create: `lib/core/src/step_expand.h`, `lib/core/src/step_expand.cpp`
- Test: `test/test_expand/test_main.cpp`

**Step 1: Падающий тест**

```cpp
#include <unity.h>
#include "step_expand.h"

using namespace mt;

static const uint32_t kStepUs = 125000;  // 1/16 at 120 BPM
static TrackCfg track;
static Rng rng(7);

void setUp() {
  track = TrackCfg();
  track.channel = 2;
  track.defVel = 100;
  track.defGate = 50;
}
void tearDown() {}

static Step note(uint8_t n) { Step s; s.note = n; return s; }

void test_empty_and_off_produce_nothing() {
  ExpandOut out;
  TEST_ASSERT_FALSE(expandStep(Step(), track, kStepUs, rng, out));
  TEST_ASSERT_EQUAL(0, out.count);
  TEST_ASSERT_FALSE(expandStep(note(kNoteOff), track, kStepUs, rng, out));
}

void test_plain_note() {
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(note(60), track, kStepUs, rng, out));
  TEST_ASSERT_EQUAL(2, out.count);
  TEST_ASSERT_EQUAL(0, out.ev[0].offsetUs);
  TEST_ASSERT_TRUE(out.ev[0].kind == EvKind::NoteOn);
  TEST_ASSERT_EQUAL(2, out.ev[0].ch);
  TEST_ASSERT_EQUAL(60, out.ev[0].note);
  TEST_ASSERT_EQUAL(100, out.ev[0].vel);
  TEST_ASSERT_TRUE(out.ev[1].kind == EvKind::NoteOff);
  TEST_ASSERT_EQUAL(62500, out.ev[1].offsetUs);
  TEST_ASSERT_FALSE(out.tie);
}

void test_step_velocity_overrides_default() {
  Step s = note(60);
  s.vel = 80;
  ExpandOut out;
  expandStep(s, track, kStepUs, rng, out);
  TEST_ASSERT_EQUAL(80, out.ev[0].vel);
}

void test_chn_overrides_channel() {
  Step s = note(60);
  s.fx[0] = {Fx::CHN, 10};
  ExpandOut out;
  expandStep(s, track, kStepUs, rng, out);
  TEST_ASSERT_EQUAL(9, out.ev[0].ch);
  TEST_ASSERT_EQUAL(9, out.ev[1].ch);
}

void test_ratchet_four() {
  Step s = note(60);
  s.fx[0] = {Fx::RAT, 4};
  ExpandOut out;
  expandStep(s, track, kStepUs, rng, out);
  TEST_ASSERT_EQUAL(8, out.count);
  const int32_t ons[] = {0, 31250, 62500, 93750};
  for (int i = 0; i < 4; ++i) {
    TEST_ASSERT_TRUE(out.ev[i * 2].kind == EvKind::NoteOn);
    TEST_ASSERT_EQUAL(ons[i], out.ev[i * 2].offsetUs);
    TEST_ASSERT_EQUAL(ons[i] + 15625, out.ev[i * 2 + 1].offsetUs);
  }
}

void test_probability_bounds() {
  Step s = note(60);
  s.fx[0] = {Fx::PRB, 0};
  ExpandOut out;
  for (int i = 0; i < 100; ++i) TEST_ASSERT_FALSE(expandStep(s, track, kStepUs, rng, out));
  s.fx[0].val = 100;
  for (int i = 0; i < 100; ++i) TEST_ASSERT_TRUE(expandStep(s, track, kStepUs, rng, out));
  s.fx[0].val = 50;
  int hits = 0;
  for (int i = 0; i < 1000; ++i) hits += expandStep(s, track, kStepUs, rng, out);
  TEST_ASSERT_INT_WITHIN(100, 500, hits);
}

void test_negative_nudge() {
  Step s = note(60);
  s.fx[1] = {Fx::NDG, static_cast<uint8_t>(-25)};
  ExpandOut out;
  expandStep(s, track, kStepUs, rng, out);
  TEST_ASSERT_EQUAL(-31250, out.ev[0].offsetUs);
  TEST_ASSERT_EQUAL(-31250 + 62500, out.ev[1].offsetUs);
}

void test_long_gate() {
  Step s = note(60);
  s.fx[0] = {Fx::GAT, 150};  // 450 %
  ExpandOut out;
  expandStep(s, track, kStepUs, rng, out);
  TEST_ASSERT_EQUAL(562500, out.ev[1].offsetUs);
}

void test_tie_has_no_note_off() {
  Step s = note(60);
  s.fx[0] = {Fx::TIE, 0};
  ExpandOut out;
  expandStep(s, track, kStepUs, rng, out);
  TEST_ASSERT_EQUAL(1, out.count);
  TEST_ASSERT_TRUE(out.tie);
}

void test_ratchet_with_tie_holds_last() {
  Step s = note(60);
  s.fx[0] = {Fx::RAT, 4};
  s.fx[1] = {Fx::TIE, 0};
  ExpandOut out;
  expandStep(s, track, kStepUs, rng, out);
  TEST_ASSERT_EQUAL(7, out.count);
  TEST_ASSERT_TRUE(out.ev[6].kind == EvKind::NoteOn);
  TEST_ASSERT_TRUE(out.tie);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_and_off_produce_nothing);
  RUN_TEST(test_plain_note);
  RUN_TEST(test_step_velocity_overrides_default);
  RUN_TEST(test_chn_overrides_channel);
  RUN_TEST(test_ratchet_four);
  RUN_TEST(test_probability_bounds);
  RUN_TEST(test_negative_nudge);
  RUN_TEST(test_long_gate);
  RUN_TEST(test_tie_has_no_note_off);
  RUN_TEST(test_ratchet_with_tie_holds_last);
  return UNITY_END();
}
```

**Step 2: Run** `pio test -e native -f test_expand` — Expected: FAIL (нет `step_expand.h`).

**Step 3: Реализация**

`lib/core/src/step_expand.h`:

```cpp
#pragma once
#include <stdint.h>
#include "model.h"
#include "rng.h"

namespace mt {

enum class EvKind : uint8_t { NoteOn, NoteOff };

struct StepEvent {
  int32_t offsetUs;  // relative to the step start, may be negative (NDG)
  EvKind kind;
  uint8_t ch;
  uint8_t note;
  uint8_t vel;
};

constexpr int kMaxStepEvents = 32;
constexpr uint32_t kMinGateUs = 1000;

struct ExpandOut {
  int count = 0;
  bool tie = false;  // last NoteOn has no NoteOff: the engine releases it later
  StepEvent ev[kMaxStepEvents];
};

// Expands one step of one track into note events. Every NoteOn is directly
// followed by its NoteOff (except a tied last note). Returns false if nothing plays.
bool expandStep(const Step& s, const TrackCfg& t, uint32_t stepUs, Rng& rng, ExpandOut& out);

}  // namespace mt
```

`lib/core/src/step_expand.cpp`:

```cpp
#include "step_expand.h"

namespace mt {

bool expandStep(const Step& s, const TrackCfg& t, uint32_t stepUs, Rng& rng, ExpandOut& out) {
  out.count = 0;
  out.tie = false;
  if (!s.hasNote()) return false;

  if (const FxSlot* p = s.find(Fx::PRB)) {
    if (rng.below(100) >= p->val) return false;
  }

  uint8_t ch = t.channel & 0x0F;
  if (const FxSlot* c = s.find(Fx::CHN)) {
    if (c->val >= 1 && c->val <= 16) ch = c->val - 1;
  }

  uint8_t vel = s.vel ? s.vel : t.defVel;
  if (vel > 127) vel = 127;
  if (vel == 0) vel = 1;

  int32_t nudge = 0;
  if (const FxSlot* n = s.find(Fx::NDG)) {
    int v = fxSigned(n->val);
    if (v < -50) v = -50;
    if (v > 50) v = 50;
    nudge = static_cast<int32_t>(static_cast<int64_t>(stepUs) * v / 100);
  }

  int rat = 1;
  if (const FxSlot* r = s.find(Fx::RAT)) rat = r->val < 2 ? 2 : (r->val > 8 ? 8 : r->val);

  uint32_t gatePct = t.defGate;
  if (const FxSlot* g = s.find(Fx::GAT)) gatePct = gatePercent(g->val);

  out.tie = s.find(Fx::TIE) != nullptr;

  const uint32_t sub = stepUs / rat;
  uint32_t gateUs;
  if (rat > 1) {
    // Ratchet hits must not overlap: cap the gate below the sub-step.
    gateUs = static_cast<uint32_t>(static_cast<uint64_t>(sub) * (gatePct > 95 ? 95 : gatePct) / 100);
  } else {
    gateUs = static_cast<uint32_t>(static_cast<uint64_t>(stepUs) * gatePct / 100);
  }
  if (gateUs < kMinGateUs) gateUs = kMinGateUs;

  for (int i = 0; i < rat; ++i) {
    const int32_t on = nudge + static_cast<int32_t>(sub * i);
    out.ev[out.count++] = {on, EvKind::NoteOn, ch, s.note, vel};
    const bool last = i == rat - 1;
    if (!(last && out.tie)) {
      out.ev[out.count++] = {on + static_cast<int32_t>(gateUs), EvKind::NoteOff, ch, s.note, 0};
    }
  }
  return true;
}

}  // namespace mt
```

**Step 4: Run** `pio test -e native -f test_expand` — Expected: `10 Tests 0 Failures`.

**Step 5: Commit**

```bash
git add lib/core/src/step_expand.* test/test_expand
git commit -m "feat(core): step expansion with CHN RAT PRB GAT TIE NDG"
```

---

### Task 4: Очередь событий (min-heap)

**Files:**
- Create: `lib/core/src/event_heap.h`, `lib/core/src/event_heap.cpp`
- Test: `test/test_heap/test_main.cpp`

**Step 1: Падающий тест**

```cpp
#include <unity.h>
#include "event_heap.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static SchedEvent ev(uint64_t t, uint8_t status, uint8_t note = 60) {
  SchedEvent e{};
  e.t = t;
  e.b[0] = status;
  e.b[1] = note;
  e.len = 3;
  return e;
}

static EventHeap heap;  // 8 KB, keep off the stack

void test_pops_in_time_order() {
  heap.clear();
  const uint64_t ts[] = {500, 100, 900, 300, 700, 200, 800, 400, 600};
  for (uint64_t t : ts) TEST_ASSERT_TRUE(heap.push(ev(t, 0x90)));
  uint64_t prev = 0;
  int n = 0;
  while (!heap.empty()) {
    TEST_ASSERT_TRUE(heap.top().t >= prev);
    prev = heap.top().t;
    heap.pop();
    ++n;
  }
  TEST_ASSERT_EQUAL(9, n);
}

void test_note_off_before_note_on_at_same_time() {
  heap.clear();
  heap.push(ev(100, 0x90));
  heap.push(ev(100, 0x80));
  TEST_ASSERT_TRUE(heap.top().isNoteOff());
}

void test_note_on_rejected_when_reserve_reached_but_off_accepted() {
  heap.clear();
  const int onLimit = EventHeap::kCap - EventHeap::kOffReserve;
  for (int i = 0; i < onLimit; ++i) TEST_ASSERT_TRUE(heap.push(ev(i, 0x90)));
  TEST_ASSERT_FALSE(heap.push(ev(1, 0x90)));
  for (int i = 0; i < EventHeap::kOffReserve; ++i) TEST_ASSERT_TRUE(heap.push(ev(i, 0x80)));
  TEST_ASSERT_FALSE(heap.push(ev(1, 0x80)));
  TEST_ASSERT_EQUAL(EventHeap::kCap, heap.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pops_in_time_order);
  RUN_TEST(test_note_off_before_note_on_at_same_time);
  RUN_TEST(test_note_on_rejected_when_reserve_reached_but_off_accepted);
  return UNITY_END();
}
```

**Step 2: Run** `pio test -e native -f test_heap` — Expected: FAIL.

**Step 3: Реализация**

`lib/core/src/event_heap.h`:

```cpp
#pragma once
#include <stdint.h>

namespace mt {

struct SchedEvent {
  uint64_t t;    // absolute time, us
  uint32_t id;   // pairs a NoteOn with its NoteOff; 0 for other messages
  uint8_t b[3];
  uint8_t len;
  bool isNoteOff() const { return (b[0] & 0xF0) == 0x80; }
};

// Fixed-capacity min-heap by time. NoteOffs win ties and keep a reserved
// share of the capacity so a flood of NoteOns can never strand a note.
class EventHeap {
 public:
  static constexpr int kCap = 512;
  static constexpr int kOffReserve = 128;

  bool push(const SchedEvent& e);
  void pop();
  const SchedEvent& top() const { return buf_[0]; }
  bool empty() const { return n_ == 0; }
  int size() const { return n_; }
  void clear() { n_ = 0; }

 private:
  static bool before(const SchedEvent& a, const SchedEvent& b) {
    if (a.t != b.t) return a.t < b.t;
    return a.isNoteOff() && !b.isNoteOff();
  }
  SchedEvent buf_[kCap];
  int n_ = 0;
};

}  // namespace mt
```

`lib/core/src/event_heap.cpp`:

```cpp
#include "event_heap.h"
#include <utility>

namespace mt {

bool EventHeap::push(const SchedEvent& e) {
  const int limit = e.isNoteOff() ? kCap : kCap - kOffReserve;
  if (n_ >= limit) return false;
  int i = n_++;
  buf_[i] = e;
  while (i > 0) {
    const int p = (i - 1) / 2;
    if (!before(buf_[i], buf_[p])) break;
    std::swap(buf_[i], buf_[p]);
    i = p;
  }
  return true;
}

void EventHeap::pop() {
  if (n_ == 0) return;
  buf_[0] = buf_[--n_];
  int i = 0;
  for (;;) {
    const int l = 2 * i + 1, r = l + 1;
    int m = i;
    if (l < n_ && before(buf_[l], buf_[m])) m = l;
    if (r < n_ && before(buf_[r], buf_[m])) m = r;
    if (m == i) break;
    std::swap(buf_[i], buf_[m]);
    i = m;
  }
}

}  // namespace mt
```

**Step 4: Run** `pio test -e native -f test_heap` — Expected: PASSED.

**Step 5: Commit**

```bash
git add lib/core/src/event_heap.* test/test_heap
git commit -m "feat(core): event min-heap with note-off reserve"
```

---

### Task 5: Таблица звучащих нот

**Files:**
- Create: `lib/core/src/voices.h`
- Test: `test/test_voices/test_main.cpp`

**Step 1: Падающий тест**

```cpp
#include <unity.h>
#include "voices.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static Voices v;

void test_retrigger_and_stale_off() {
  v.clear();
  TEST_ASSERT_FALSE(v.noteOn(0, 60, 1));   // fresh note
  TEST_ASSERT_TRUE(v.noteOn(0, 60, 2));    // retrigger: caller must send off first
  TEST_ASSERT_FALSE(v.noteOff(0, 60, 1));  // stale off of the first note is ignored
  TEST_ASSERT_TRUE(v.active(0, 60));
  TEST_ASSERT_TRUE(v.noteOff(0, 60, 2));
  TEST_ASSERT_FALSE(v.active(0, 60));
}

void test_release_all() {
  v.clear();
  v.noteOn(0, 60, 1);
  v.noteOn(9, 36, 2);
  int n = 0;
  v.releaseAll([&](uint8_t ch, uint8_t note) {
    ++n;
    TEST_ASSERT_TRUE((ch == 0 && note == 60) || (ch == 9 && note == 36));
  });
  TEST_ASSERT_EQUAL(2, n);
  TEST_ASSERT_FALSE(v.active(0, 60));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_retrigger_and_stale_off);
  RUN_TEST(test_release_all);
  return UNITY_END();
}
```

**Step 2: Run** `pio test -e native -f test_voices` — Expected: FAIL.

**Step 3: Реализация** `lib/core/src/voices.h`:

```cpp
#pragma once
#include <stdint.h>
#include <string.h>

namespace mt {

// Which note is sounding on which channel, tagged with the id of its NoteOn.
// A NoteOff only goes out if its id still owns the note.
class Voices {
 public:
  // Returns true if the note was already sounding: send a NoteOff before this NoteOn.
  bool noteOn(uint8_t ch, uint8_t note, uint32_t id) {
    const bool was = id_[ch & 15][note & 127] != 0;
    id_[ch & 15][note & 127] = id;
    return was;
  }
  // Returns true if this NoteOff must be sent.
  bool noteOff(uint8_t ch, uint8_t note, uint32_t id) {
    uint32_t& cur = id_[ch & 15][note & 127];
    if (cur == 0 || cur != id) return false;
    cur = 0;
    return true;
  }
  bool active(uint8_t ch, uint8_t note) const { return id_[ch & 15][note & 127] != 0; }
  template <class F>
  void releaseAll(F&& f) {
    for (uint8_t ch = 0; ch < 16; ++ch)
      for (uint8_t n = 0; n < 128; ++n)
        if (id_[ch][n]) {
          id_[ch][n] = 0;
          f(ch, n);
        }
  }
  void clear() { memset(id_, 0, sizeof(id_)); }

 private:
  uint32_t id_[16][128] = {};
};

}  // namespace mt
```

**Step 4: Run** `pio test -e native -f test_voices` — Expected: PASSED.

**Step 5: Commit**

```bash
git add lib/core/src/voices.h test/test_voices
git commit -m "feat(core): voice table with note-on ids"
```

---

### Task 6: Секвенсор

Чистый класс: время приходит снаружи, MIDI уходит в `MidiSink`. Сетка в нс-арифметике без накопления ошибки: время шага n = `base + n * 625000000 * ticks / bpm / 1000` мкс, clock k = `base + k * 2500000000 / bpm / 1000` мкс.

Замечание: смена паттерна по очереди фиксируется при планировании последнего шага прохода (за один шаг до границы), поэтому `pattern()` переключается чуть раньше, чем звучит новый паттерн. Для UI это приемлемо.

**Files:**
- Create: `lib/core/src/sequencer.h`, `lib/core/src/sequencer.cpp`
- Test: `test/test_sequencer/test_main.cpp`

**Step 1: Падающий тест**

```cpp
#include <unity.h>
#include <string.h>
#include <vector>
#include "sequencer.h"

using namespace mt;

struct Rec {
  uint64_t t;
  uint8_t b[3];
  uint8_t len;
};

struct FakeSink : MidiSink {
  std::vector<Rec> log;
  uint64_t now = 0;
  void send(const uint8_t* b, uint8_t len) override {
    Rec r{now, {0, 0, 0}, len};
    memcpy(r.b, b, len);
    log.push_back(r);
  }
  std::vector<uint64_t> times(uint8_t status, int note = -1) const {
    std::vector<uint64_t> out;
    for (const auto& r : log)
      if (r.b[0] == status && (note < 0 || r.b[1] == note)) out.push_back(r.t);
    return out;
  }
};

static Project* p;
static Sequencer* seq;
static FakeSink* sink;

// Event-driven loop: jumps to the time the sequencer asks for.
static void run(uint64_t from, uint64_t to) {
  uint64_t t = from;
  while (t <= to) {
    sink->now = t;
    const uint64_t next = seq->process(t, *sink);
    if (next == kNever) break;
    t = next > t ? next : t + 1;
  }
}

void setUp() {
  p = new Project();
  seq = new Sequencer(*p);
  seq->seed(1);
  sink = new FakeSink();
}
void tearDown() {
  delete sink;
  delete seq;
  delete p;
}

void test_start_sends_start_then_clock() {
  sink->now = 1000;
  seq->start(1000, *sink);
  run(1000, 1000);
  TEST_ASSERT_EQUAL_HEX8(0xFA, sink->log[0].b[0]);
  TEST_ASSERT_EQUAL_HEX8(0xF8, sink->log[1].b[0]);
  TEST_ASSERT_EQUAL(1000, sink->log[1].t);
}

void test_clock_24ppqn_at_120() {
  seq->start(0, *sink);
  run(0, 500000);
  TEST_ASSERT_EQUAL(25, sink->times(0xF8).size());
}

void test_sixteenths_at_120() {
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][1].note = 60;
  seq->start(0, *sink);
  run(0, 200000);
  auto on = sink->times(0x90, 60);
  TEST_ASSERT_EQUAL(2, on.size());
  TEST_ASSERT_EQUAL(0, on[0]);
  TEST_ASSERT_EQUAL(125000, on[1]);
  auto off = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(62500, off[0]);
}

void test_swing_delays_odd_steps() {
  p->patterns[0].swing = 75;
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][1].note = 60;
  seq->start(0, *sink);
  run(0, 200000);
  TEST_ASSERT_EQUAL(187500, sink->times(0x90, 60)[1]);
}

void test_queued_pattern_switches_at_loop_end() {
  p->patterns[0].length = 4;
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[1].length = 4;
  p->patterns[1].steps[0][0].note = 62;
  seq->start(0, *sink);
  seq->queuePattern(1);
  run(0, 510000);
  TEST_ASSERT_EQUAL(1, sink->times(0x90, 60).size());
  auto on62 = sink->times(0x90, 62);
  TEST_ASSERT_EQUAL(1, on62.size());
  TEST_ASSERT_EQUAL(500000, on62[0]);
  TEST_ASSERT_EQUAL(1, seq->pattern());
}

void test_pause_silences_and_stops_output() {
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][0].fx[0] = {Fx::GAT, 200};
  seq->start(0, *sink);
  run(0, 100000);
  sink->now = 100000;
  seq->pause(100000, *sink);
  const size_t n = sink->log.size();
  TEST_ASSERT_EQUAL_HEX8(0xFC, sink->log[n - 1].b[0]);
  TEST_ASSERT_EQUAL_HEX8(0x80, sink->log[n - 2].b[0]);
  TEST_ASSERT_EQUAL(60, sink->log[n - 2].b[1]);
  run(100000, 3000000);
  TEST_ASSERT_EQUAL(n, sink->log.size());
}

void test_resume_sends_song_position_and_continue() {
  seq->start(0, *sink);
  run(0, 300000);
  sink->now = 300000;
  seq->pause(300000, *sink);  // step 3 was scheduled for 375000 but not reached
  sink->now = 400000;
  seq->resume(400000, *sink);
  const size_t n = sink->log.size();
  TEST_ASSERT_EQUAL_HEX8(0xF2, sink->log[n - 2].b[0]);
  TEST_ASSERT_EQUAL(3, sink->log[n - 2].b[1]);
  TEST_ASSERT_EQUAL(0, sink->log[n - 2].b[2]);
  TEST_ASSERT_EQUAL_HEX8(0xFB, sink->log[n - 1].b[0]);
}

void test_retrigger_ignores_stale_note_off() {
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][0].fx[0] = {Fx::GAT, 200};  // off would land at 1 000 000
  p->patterns[0].steps[0][1].note = 60;
  seq->start(0, *sink);
  run(0, 1100000);
  auto off = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(2, off.size());
  TEST_ASSERT_EQUAL(125000, off[0]);  // retrigger
  TEST_ASSERT_EQUAL(187500, off[1]);  // second note's own gate
}

void test_muted_track_is_silent() {
  p->tracks[0].mute = true;
  p->patterns[0].steps[0][0].note = 60;
  seq->start(0, *sink);
  run(0, 200000);
  TEST_ASSERT_EQUAL(0, sink->times(0x90).size());
}

void test_tie_holds_until_next_note_with_overlap() {
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][0].fx[0] = {Fx::TIE, 0};
  p->patterns[0].steps[0][2].note = 64;
  seq->start(0, *sink);
  run(0, 300000);
  auto off60 = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(1, off60.size());
  TEST_ASSERT_EQUAL(251000, off60[0]);
  TEST_ASSERT_EQUAL(250000, sink->times(0x90, 64)[0]);
}

void test_set_bpm_while_playing() {
  p->patterns[0].steps[0][0].note = 60;
  p->patterns[0].steps[0][1].note = 60;
  seq->start(0, *sink);
  seq->setBpm(240);
  run(0, 100000);
  TEST_ASSERT_EQUAL(62500, sink->times(0x90, 60)[1]);
}

void test_stop_rewinds() {
  seq->start(0, *sink);
  run(0, 300000);
  seq->stop(300000, *sink);
  TEST_ASSERT_FALSE(seq->playing());
  TEST_ASSERT_EQUAL_HEX8(0xFC, sink->log.back().b[0]);
  sink->log.clear();
  p->patterns[0].steps[0][0].note = 60;
  seq->start(400000, *sink);
  run(400000, 400000);
  TEST_ASSERT_EQUAL(400000, sink->times(0x90, 60)[0]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_start_sends_start_then_clock);
  RUN_TEST(test_clock_24ppqn_at_120);
  RUN_TEST(test_sixteenths_at_120);
  RUN_TEST(test_swing_delays_odd_steps);
  RUN_TEST(test_queued_pattern_switches_at_loop_end);
  RUN_TEST(test_pause_silences_and_stops_output);
  RUN_TEST(test_resume_sends_song_position_and_continue);
  RUN_TEST(test_retrigger_ignores_stale_note_off);
  RUN_TEST(test_muted_track_is_silent);
  RUN_TEST(test_tie_holds_until_next_note_with_overlap);
  RUN_TEST(test_set_bpm_while_playing);
  RUN_TEST(test_stop_rewinds);
  return UNITY_END();
}
```

**Step 2: Run** `pio test -e native -f test_sequencer` — Expected: FAIL (нет `sequencer.h`).

**Step 3: Реализация**

`lib/core/src/sequencer.h`:

```cpp
#pragma once
#include <stdint.h>
#include "event_heap.h"
#include "model.h"
#include "rng.h"
#include "step_expand.h"
#include "voices.h"

namespace mt {

class MidiSink {
 public:
  virtual ~MidiSink() = default;
  virtual void send(const uint8_t* b, uint8_t len) = 0;
};

constexpr uint64_t kNever = UINT64_MAX;
constexpr uint32_t kTieOverlapUs = 1000;  // tied note ends this long after the next note starts

// Hardware-free sequencer. The caller passes the current time in us and calls
// process() again no later than the time it returns.
class Sequencer {
 public:
  explicit Sequencer(Project& p) : p_(p) {}

  void start(uint64_t now, MidiSink& out);
  void pause(uint64_t now, MidiSink& out);
  void resume(uint64_t now, MidiSink& out);
  void stop(uint64_t now, MidiSink& out);
  void togglePlay(uint64_t now, MidiSink& out);

  void queuePattern(uint8_t idx);   // switch at the end of the current pass
  void selectPattern(uint8_t idx);  // switch from the next step
  void setBpm(uint16_t bpm);

  uint64_t process(uint64_t now, MidiSink& out);

  bool playing() const { return state_ == State::Playing; }
  bool paused() const { return state_ == State::Paused; }
  uint8_t pattern() const { return cur_; }
  int queued() const { return queued_; }
  uint8_t playPos() const { return heardPos_; }
  uint32_t loopCount() const { return loop_; }
  void seed(uint32_t s) { rng_ = Rng(s); }

 private:
  enum class State : uint8_t { Stopped, Playing, Paused };
  struct Tie {
    bool on = false;
    uint8_t ch = 0, note = 0;
    uint32_t id = 0;
  };

  uint16_t ticks() const { return ticksPerStep(p_.patterns[cur_].res); }
  uint32_t stepUs() const { return 625000u * ticks() / p_.bpm; }
  uint64_t stepGrid(uint32_t n) const {
    return stepBase_ + static_cast<uint64_t>(n) * 625000000ull * ticks() / p_.bpm / 1000;
  }
  uint64_t clockTime(uint32_t k) const {
    return clockBase_ + static_cast<uint64_t>(k) * 2500000000ull / p_.bpm / 1000;
  }
  void rebase(uint64_t t);
  void scheduleStep();
  void releaseTie(int track, uint64_t t);
  void releaseAllTies(uint64_t t);
  void silence(MidiSink& out);
  void push(uint64_t t, uint8_t s, uint8_t d1, uint8_t d2, uint32_t id);
  void dispatch(const SchedEvent& e, MidiSink& out);
  uint32_t newId();

  Project& p_;
  EventHeap heap_;
  Voices voices_;
  Rng rng_;
  State state_ = State::Stopped;
  uint8_t cur_ = 0;
  int queued_ = -1;
  uint8_t pos_ = 0;  // next step to schedule
  uint32_t loop_ = 0;
  uint64_t stepBase_ = 0;
  uint32_t stepN_ = 0;
  uint64_t clockBase_ = 0;
  uint32_t clockK_ = 0;
  uint64_t lastStepT_ = 0, prevStepT_ = 0;
  uint8_t lastStepPos_ = 0, prevStepPos_ = 0;
  uint8_t heardPos_ = 0;
  uint32_t nextId_ = 1;
  Tie ties_[kTracks];
};

}  // namespace mt
```

`lib/core/src/sequencer.cpp`:

```cpp
#include "sequencer.h"

namespace mt {

uint32_t Sequencer::newId() {
  const uint32_t id = nextId_++;
  if (nextId_ == 0) nextId_ = 1;
  return id;
}

void Sequencer::rebase(uint64_t t) {
  stepBase_ = clockBase_ = t;
  stepN_ = clockK_ = 0;
  lastStepT_ = prevStepT_ = t;
  lastStepPos_ = prevStepPos_ = heardPos_ = pos_;
}

void Sequencer::start(uint64_t now, MidiSink& out) {
  if (state_ == State::Playing) silence(out);
  heap_.clear();
  if (queued_ >= 0) {
    cur_ = queued_;
    queued_ = -1;
  }
  pos_ = 0;
  loop_ = 0;
  for (const auto& t : p_.tracks) {
    if (t.program != kNoProgram) {
      const uint8_t m[2] = {static_cast<uint8_t>(0xC0 | (t.channel & 15)), static_cast<uint8_t>(t.program & 127)};
      out.send(m, 2);
    }
  }
  const uint8_t s = 0xFA;
  out.send(&s, 1);
  rebase(now);
  state_ = State::Playing;
}

void Sequencer::pause(uint64_t now, MidiSink& out) {
  if (state_ != State::Playing) return;
  // A step scheduled ahead but not yet heard must be replayed on resume.
  const uint8_t resumePos = lastStepT_ > now ? lastStepPos_ : pos_;
  silence(out);
  const uint8_t s = 0xFC;
  out.send(&s, 1);
  pos_ = resumePos;
  heardPos_ = resumePos;
  state_ = State::Paused;
}

void Sequencer::resume(uint64_t now, MidiSink& out) {
  if (state_ != State::Paused) return;
  // Song Position Pointer counts sixteenths (24 ticks at 96 PPQN).
  const uint16_t spp = static_cast<uint16_t>(static_cast<uint32_t>(pos_) * ticks() / 24);
  const uint8_t m[3] = {0xF2, static_cast<uint8_t>(spp & 0x7F), static_cast<uint8_t>((spp >> 7) & 0x7F)};
  out.send(m, 3);
  const uint8_t c = 0xFB;
  out.send(&c, 1);
  rebase(now);
  state_ = State::Playing;
}

void Sequencer::stop(uint64_t now, MidiSink& out) {
  (void)now;
  if (state_ == State::Playing) silence(out);
  const uint8_t s = 0xFC;
  out.send(&s, 1);
  state_ = State::Stopped;
  pos_ = 0;
  heardPos_ = 0;
  loop_ = 0;
}

void Sequencer::togglePlay(uint64_t now, MidiSink& out) {
  switch (state_) {
    case State::Stopped: start(now, out); break;
    case State::Playing: pause(now, out); break;
    case State::Paused: resume(now, out); break;
  }
}

void Sequencer::queuePattern(uint8_t idx) {
  if (idx >= kPatterns) return;
  if (state_ == State::Playing) {
    queued_ = idx;
  } else {
    cur_ = idx;
    queued_ = -1;
    pos_ = 0;
    heardPos_ = 0;
  }
}

void Sequencer::selectPattern(uint8_t idx) {
  if (idx >= kPatterns) return;
  if (state_ != State::Playing) {
    queuePattern(idx);
    return;
  }
  const uint64_t edge = stepGrid(stepN_);
  releaseAllTies(edge);
  cur_ = idx;
  queued_ = -1;
  loop_ = 0;
  stepBase_ = edge;
  stepN_ = 0;
}

void Sequencer::setBpm(uint16_t bpm) {
  if (bpm < 20) bpm = 20;
  if (bpm > 300) bpm = 300;
  if (state_ == State::Playing) {
    const uint64_t ns = stepGrid(stepN_);
    const uint64_t nc = clockTime(clockK_);
    p_.bpm = bpm;
    stepBase_ = ns;
    stepN_ = 0;
    clockBase_ = nc;
    clockK_ = 0;
  } else {
    p_.bpm = bpm;
  }
}

uint64_t Sequencer::process(uint64_t now, MidiSink& out) {
  if (state_ != State::Playing) return kNever;

  // Plan one step ahead so a negative nudge (up to -50 %) still lands in time.
  while (stepGrid(stepN_) <= now + stepUs()) scheduleStep();

  while (clockTime(clockK_) <= now) {
    const uint8_t c = 0xF8;
    out.send(&c, 1);
    ++clockK_;
  }

  while (!heap_.empty() && heap_.top().t <= now) {
    const SchedEvent e = heap_.top();
    heap_.pop();
    dispatch(e, out);
  }

  heardPos_ = lastStepT_ <= now ? lastStepPos_ : prevStepPos_;

  uint64_t next = clockTime(clockK_);
  const uint64_t grid = stepGrid(stepN_);
  const uint64_t planAt = grid > stepUs() ? grid - stepUs() : 0;
  if (planAt < next) next = planAt;
  if (!heap_.empty() && heap_.top().t < next) next = heap_.top().t;
  return next;
}

void Sequencer::scheduleStep() {
  Pattern& pat = p_.patterns[cur_];
  if (pos_ >= pat.length) pos_ = 0;
  const uint32_t su = stepUs();
  uint64_t t = stepGrid(stepN_);
  if (pos_ & 1) {
    const uint8_t sw = pat.swing < 50 ? 50 : (pat.swing > 75 ? 75 : pat.swing);
    t += static_cast<uint64_t>(su) * (sw - 50) / 50;
  }

  for (int tr = 0; tr < kTracks; ++tr) {
    const Step& s = pat.steps[tr][pos_];
    if (s.note == kNoteOff) {
      releaseTie(tr, t);
      continue;
    }
    if (!s.hasNote() || !p_.trackAudible(tr)) continue;
    ExpandOut ex;
    if (!expandStep(s, p_.tracks[tr], su, rng_, ex)) continue;
    releaseTie(tr, t + kTieOverlapUs);
    uint32_t id = 0;
    for (int i = 0; i < ex.count; ++i) {
      const StepEvent& e = ex.ev[i];
      int64_t et = static_cast<int64_t>(t) + e.offsetUs;
      if (et < 0) et = 0;
      if (e.kind == EvKind::NoteOn) {
        id = newId();
        push(et, 0x90 | e.ch, e.note, e.vel, id);
      } else {
        push(et, 0x80 | e.ch, e.note, 0, id);
      }
    }
    if (ex.tie) {
      const StepEvent& last = ex.ev[ex.count - 1];
      ties_[tr] = {true, last.ch, last.note, id};
    }
  }

  prevStepT_ = lastStepT_;
  prevStepPos_ = lastStepPos_;
  lastStepT_ = t;
  lastStepPos_ = pos_;
  ++stepN_;

  if (++pos_ >= pat.length) {
    pos_ = 0;
    ++loop_;
    if (queued_ >= 0) {
      const uint64_t edge = stepGrid(stepN_);
      releaseAllTies(edge);
      cur_ = queued_;
      queued_ = -1;
      loop_ = 0;
      stepBase_ = edge;  // the new pattern may use another resolution
      stepN_ = 0;
    }
  }
}

void Sequencer::releaseTie(int track, uint64_t t) {
  Tie& tie = ties_[track];
  if (!tie.on) return;
  push(t, 0x80 | tie.ch, tie.note, 0, tie.id);
  tie.on = false;
}

void Sequencer::releaseAllTies(uint64_t t) {
  for (int tr = 0; tr < kTracks; ++tr) releaseTie(tr, t);
}

void Sequencer::silence(MidiSink& out) {
  heap_.clear();
  voices_.releaseAll([&](uint8_t ch, uint8_t note) {
    const uint8_t m[3] = {static_cast<uint8_t>(0x80 | ch), note, 0};
    out.send(m, 3);
  });
  for (auto& t : ties_) t.on = false;
}

void Sequencer::push(uint64_t t, uint8_t s, uint8_t d1, uint8_t d2, uint32_t id) {
  SchedEvent e{};
  e.t = t;
  e.id = id;
  e.b[0] = s;
  e.b[1] = d1;
  e.b[2] = d2;
  e.len = 3;
  heap_.push(e);  // a full heap drops NoteOns only; NoteOffs have a reserve
}

void Sequencer::dispatch(const SchedEvent& e, MidiSink& out) {
  const uint8_t kind = e.b[0] & 0xF0;
  const uint8_t ch = e.b[0] & 0x0F;
  if (kind == 0x90) {
    if (voices_.noteOn(ch, e.b[1], e.id)) {
      const uint8_t off[3] = {static_cast<uint8_t>(0x80 | ch), e.b[1], 0};
      out.send(off, 3);
    }
  } else if (kind == 0x80) {
    if (!voices_.noteOff(ch, e.b[1], e.id)) return;
  }
  out.send(e.b, e.len);
}

}  // namespace mt
```

**Step 4: Run** `pio test -e native -f test_sequencer` — Expected: `12 Tests 0 Failures`. Если тест падает — разбираться через @systematic-debugging, не подгонять ожидания.

**Step 5: Полный прогон** `pio test -e native` — Expected: все наборы PASSED.

**Step 6: Commit**

```bash
git add lib/core/src/sequencer.* test/test_sequencer
git commit -m "feat(core): sequencer with clock, swing, ties, pause/resume"
```

---

### Task 7: Дисплей и тач (железо)

**Files:**
- Create: `src/hw/pins.h`, `src/hw/lgfx_config.h`
- Modify: `src/main.cpp`

**Step 1: `src/hw/pins.h`**

```cpp
#pragma once

namespace pins {
constexpr int kMidiTx = 10;
constexpr int kEncA = 11;
constexpr int kEncB = 12;
constexpr int kEncSw = 13;
constexpr int kPlay = 14;
constexpr int kShift = 21;
}  // namespace pins
```

**Step 2: `src/hw/lgfx_config.h`**

```cpp
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// WT32-SC01 Plus: ST7796 on an 8-bit 8080 bus, FT6336U touch.
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796 panel_;
  lgfx::Bus_Parallel8 bus_;
  lgfx::Light_PWM light_;
  lgfx::Touch_FT5x06 touch_;

 public:
  LGFX() {
    {
      auto cfg = bus_.config();
      cfg.freq_write = 40000000;
      cfg.pin_wr = 47;
      cfg.pin_rd = -1;
      cfg.pin_rs = 0;
      cfg.pin_d0 = 9;
      cfg.pin_d1 = 46;
      cfg.pin_d2 = 3;
      cfg.pin_d3 = 8;
      cfg.pin_d4 = 18;
      cfg.pin_d5 = 17;
      cfg.pin_d6 = 16;
      cfg.pin_d7 = 15;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs = -1;
      cfg.pin_rst = 4;
      cfg.pin_busy = -1;
      cfg.panel_width = 320;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      panel_.config(cfg);
    }
    {
      auto cfg = light_.config();
      cfg.pin_bl = 45;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      light_.config(cfg);
      panel_.setLight(&light_);
    }
    {
      auto cfg = touch_.config();
      cfg.i2c_port = 1;
      cfg.i2c_addr = 0x38;
      cfg.pin_sda = 6;
      cfg.pin_scl = 5;
      cfg.pin_int = 7;
      cfg.freq = 400000;
      cfg.x_min = 0;
      cfg.x_max = 319;
      cfg.y_min = 0;
      cfg.y_max = 479;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      touch_.config(cfg);
      panel_.setTouch(&touch_);
    }
    setPanel(&panel_);
  }
};
```

**Step 3: Тестовый `src/main.cpp`** (временный, заменится в Task 12)

```cpp
#include <Arduino.h>
#include "hw/lgfx_config.h"

static LGFX lcd;

void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.setRotation(1);
  lcd.setBrightness(200);
  lcd.fillScreen(TFT_BLACK);
  lcd.fillRect(0, 0, 160, 40, TFT_RED);
  lcd.fillRect(160, 0, 160, 40, TFT_GREEN);
  lcd.fillRect(320, 0, 160, 40, TFT_BLUE);
  lcd.setFont(&fonts::AsciiFont8x16);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString("WT32-SC01 Plus 480x320 - touch me", 8, 60);
  Serial.printf("PSRAM: %u bytes free\n", ESP.getFreePsram());
}

void loop() {
  int32_t x, y;
  if (lcd.getTouch(&x, &y)) {
    lcd.fillCircle(x, y, 4, TFT_YELLOW);
    Serial.printf("touch %d,%d\n", x, y);
  }
  delay(10);
}
```

**Step 4: Собрать и прошить**

Run: `pio run -e wt32 -t upload && pio device monitor -e wt32`
Expected (ручная проверка):
- полосы слева направо красная, зелёная, синяя (если цвета инвертированы — `cfg.invert = false`; если красный и синий перепутаны — `cfg.rgb_order = true`);
- текст горизонтально, читается (если перевёрнут — `setRotation(3)`);
- точка рисуется под пальцем во всех углах экрана;
- в мониторе `PSRAM: ~2000000 bytes free` (если 0 — проверить `memory_type`/`BOARD_HAS_PSRAM`);
- если монитор пуст — поменять `-DARDUINO_USB_CDC_ON_BOOT=1` на `0`.

**Step 5: Commit**

```bash
git add src/hw/pins.h src/hw/lgfx_config.h src/main.cpp
git commit -m "feat(hw): display and touch bring-up"
```

---

### Task 8: Энкодер и кнопки

**Files:**
- Create: `src/hw/input.h`, `src/hw/input.cpp`
- Modify: `src/main.cpp` (временная проверка)

**Step 1: `src/hw/input.h`**

```cpp
#pragma once
#include <Arduino.h>

namespace hw {

enum class InputType : uint8_t { EncTurn, EncClick, EncLong, PlayPress, ShiftDown, ShiftUp };

struct InputEvent {
  InputType type;
  int8_t delta;  // detents for EncTurn
  bool shift;    // Shift held when the event happened
};

constexpr uint32_t kLongPressMs = 500;

void inputBegin();
bool inputPoll(InputEvent& ev, TickType_t wait);

}  // namespace hw
```

**Step 2: `src/hw/input.cpp`**

```cpp
#include "input.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "pins.h"

namespace hw {
namespace {

QueueHandle_t queue;
pcnt_unit_handle_t unit;
int encAcc = 0;
bool shiftHeld = false;

// Integrating debouncer: state flips after 5 equal 1 ms samples.
struct Debounce {
  uint8_t pin;
  bool state = false;
  uint8_t cnt = 0;
  // +1 on press, -1 on release, 0 otherwise.
  int update() {
    const bool raw = digitalRead(pin) == LOW;
    if (raw == state) {
      cnt = 0;
      return 0;
    }
    if (++cnt < 5) return 0;
    cnt = 0;
    state = raw;
    return raw ? 1 : -1;
  }
};

Debounce encBtn{pins::kEncSw};
Debounce playBtn{pins::kPlay};
Debounce shiftBtn{pins::kShift};
uint32_t encDownAt = 0;
bool longSent = false;

void emit(InputType t, int8_t d = 0) {
  InputEvent e{t, d, shiftHeld};
  xQueueSend(queue, &e, 0);
}

void setupEncoder() {
  pcnt_unit_config_t ucfg = {};
  ucfg.low_limit = -1000;
  ucfg.high_limit = 1000;
  ESP_ERROR_CHECK(pcnt_new_unit(&ucfg, &unit));
  pcnt_glitch_filter_config_t fcfg = {};
  fcfg.max_glitch_ns = 1000;
  ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(unit, &fcfg));

  pcnt_chan_config_t a = {};
  a.edge_gpio_num = pins::kEncA;
  a.level_gpio_num = pins::kEncB;
  pcnt_chan_config_t b = {};
  b.edge_gpio_num = pins::kEncB;
  b.level_gpio_num = pins::kEncA;
  pcnt_channel_handle_t ca, cb;
  ESP_ERROR_CHECK(pcnt_new_channel(unit, &a, &ca));
  ESP_ERROR_CHECK(pcnt_new_channel(unit, &b, &cb));
  pcnt_channel_set_edge_action(ca, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
  pcnt_channel_set_level_action(ca, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
  pcnt_channel_set_edge_action(cb, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
  pcnt_channel_set_level_action(cb, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
  gpio_pullup_en(static_cast<gpio_num_t>(pins::kEncA));
  gpio_pullup_en(static_cast<gpio_num_t>(pins::kEncB));

  ESP_ERROR_CHECK(pcnt_unit_enable(unit));
  ESP_ERROR_CHECK(pcnt_unit_clear_count(unit));
  ESP_ERROR_CHECK(pcnt_unit_start(unit));
}

// EC11: 4 counts per detent. Keeps the sub-detent remainder.
int readDetents() {
  int c = 0;
  pcnt_unit_get_count(unit, &c);
  const int d = (c - encAcc) / 4;
  encAcc += d * 4;
  if (c == encAcc && (encAcc > 800 || encAcc < -800)) {
    pcnt_unit_clear_count(unit);
    encAcc = 0;
  }
  return d;
}

void task(void*) {
  for (;;) {
    const int d = readDetents();
    if (d) emit(InputType::EncTurn, static_cast<int8_t>(d > 127 ? 127 : (d < -127 ? -127 : d)));

    const int s = shiftBtn.update();
    if (s == 1) {
      shiftHeld = true;
      emit(InputType::ShiftDown);
    } else if (s == -1) {
      shiftHeld = false;
      emit(InputType::ShiftUp);
    }

    if (playBtn.update() == 1) emit(InputType::PlayPress);

    const int e = encBtn.update();
    const uint32_t now = millis();
    if (e == 1) {
      encDownAt = now;
      longSent = false;
    }
    if (encBtn.state && !longSent && now - encDownAt >= kLongPressMs) {
      longSent = true;
      emit(InputType::EncLong);
    }
    if (e == -1 && !longSent) emit(InputType::EncClick);

    vTaskDelay(1);
  }
}

}  // namespace

void inputBegin() {
  pinMode(pins::kEncSw, INPUT_PULLUP);
  pinMode(pins::kPlay, INPUT_PULLUP);
  pinMode(pins::kShift, INPUT_PULLUP);
  queue = xQueueCreate(32, sizeof(InputEvent));
  setupEncoder();
  xTaskCreatePinnedToCore(task, "input", 3072, nullptr, 5, nullptr, 1);
}

bool inputPoll(InputEvent& ev, TickType_t wait) { return xQueueReceive(queue, &ev, wait) == pdTRUE; }

}  // namespace hw
```

**Step 3: Временная проверка в `src/main.cpp`** — добавить в `setup()` вызов `hw::inputBegin();` и `#include "hw/input.h"`, в `loop()`:

```cpp
  hw::InputEvent ev;
  while (hw::inputPoll(ev, 0)) {
    Serial.printf("input type=%d delta=%d shift=%d\n", int(ev.type), ev.delta, ev.shift);
  }
```

**Step 4: Прошить и проверить**

Run: `pio run -e wt32 -t upload && pio device monitor -e wt32`
Expected:
- один щелчок энкодера по часовой = одно событие `type=0 delta=1`; против часовой `delta=-1` (если наоборот — поменять местами `kEncA`/`kEncB` в `pins.h`);
- быстрое вращение не теряет щелчки (оборот = 20 или 24 события в сумме);
- короткое нажатие = `type=1`, удержание 0.5 с = `type=2` и без `type=1` при отпускании;
- Play = `type=3`; Shift = `type=4`/`type=5`; при удержании Shift у событий `shift=1`.

**Step 5: Commit**

```bash
git add src/hw/input.* src/main.cpp
git commit -m "feat(hw): encoder via PCNT and debounced buttons"
```

---

### Task 9: MIDI OUT и «hello note»

**Files:**
- Create: `src/hw/midi_uart.h`, `src/hw/midi_uart.cpp`
- Modify: `src/main.cpp` (временная проверка)

**Step 1: `src/hw/midi_uart.h`**

```cpp
#pragma once
#include "sequencer.h"

namespace hw {

// UART1 TX only on GPIO10, 31250 baud. RX is never routed, so no pin conflicts.
class MidiUart : public mt::MidiSink {
 public:
  void begin();
  void send(const uint8_t* b, uint8_t len) override;
};

}  // namespace hw
```

**Step 2: `src/hw/midi_uart.cpp`**

```cpp
#include "midi_uart.h"
#include "driver/uart.h"
#include "pins.h"

namespace hw {

void MidiUart::begin() {
  uart_config_t cfg = {};
  cfg.baud_rate = 31250;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;
  ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 256, 1024, 0, nullptr, 0));
  ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &cfg));
  ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, pins::kMidiTx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                               UART_PIN_NO_CHANGE));
}

void MidiUart::send(const uint8_t* b, uint8_t len) {
  uart_write_bytes(UART_NUM_1, reinterpret_cast<const char*>(b), len);
}

}  // namespace hw
```

**Step 3: Собрать схему MIDI OUT** по секции 1 дизайна (33 Ω к +3.3 V на Ring, 10 Ω от GPIO10 на Tip, GND на Sleeve). Подключить через переходник TRS-A → DIN к USB-MIDI интерфейсу, на Mac открыть MIDI Monitor (snoize.com).

**Step 4: Временная проверка** — в `src/main.cpp` добавить `#include "hw/midi_uart.h"`, `static hw::MidiUart midi;`, в `setup()` `midi.begin();`, в `loop()` по событию `PlayPress`:

```cpp
    if (ev.type == hw::InputType::PlayPress) {
      const uint8_t on[3] = {0x90, 60, 100};
      const uint8_t off[3] = {0x80, 60, 0};
      midi.send(on, 3);
      delay(200);
      midi.send(off, 3);
    }
```

**Step 5: Прошить и проверить**

Run: `pio run -e wt32 -t upload`
Expected: в MIDI Monitor на каждое нажатие Play — `Note On C3 (60) vel 100 ch 1`, через 200 мс `Note Off`. Нет сообщений — проверить, не перепутаны ли Tip/Ring (бывают переходники type B).

**Step 6: Commit**

```bash
git add src/hw/midi_uart.* src/main.cpp
git commit -m "feat(hw): MIDI out on UART1"
```

---

### Task 10: Задача движка (gptimer + секвенсор)

**Files:**
- Create: `src/engine/engine.h`, `src/engine/engine.cpp`

**Step 1: `src/engine/engine.h`**

```cpp
#pragma once
#include <stdint.h>
#include "model.h"

namespace engine {

enum class Cmd : uint8_t { TogglePlay, Stop, QueuePattern, SelectPattern, SetBpm };

struct Command {
  Cmd cmd;
  uint16_t arg;
};

struct Status {
  bool playing;
  bool paused;
  uint8_t pattern;
  int8_t queued;
  uint8_t pos;
  uint32_t loop;
  bool operator==(const Status& o) const {
    return playing == o.playing && paused == o.paused && pattern == o.pattern && queued == o.queued &&
           pos == o.pos && loop == o.loop;
  }
};

void begin(mt::Project* p);
void post(Cmd c, uint16_t arg = 0);
Status status();

// Hold while writing project data from the UI. Keep it short: the engine waits on it.
void lockProject();
void unlockProject();

}  // namespace engine
```

**Step 2: `src/engine/engine.cpp`**

```cpp
#include "engine.h"
#include <Arduino.h>
#include "driver/gptimer.h"
#include "esp_random.h"
#include "hw/midi_uart.h"
#include "sequencer.h"

namespace engine {
namespace {

mt::Sequencer* seq;
hw::MidiUart midi;
gptimer_handle_t timer;
TaskHandle_t task;
QueueHandle_t cmds;
SemaphoreHandle_t projMutex;
portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;
Status st{};

bool IRAM_ATTR onAlarm(gptimer_handle_t, const gptimer_alarm_event_data_t*, void*) {
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(task, &woken);
  return woken == pdTRUE;
}

uint64_t nowUs() {
  uint64_t v = 0;
  gptimer_get_raw_count(timer, &v);
  return v;
}

void handle(const Command& c) {
  const uint64_t now = nowUs();
  switch (c.cmd) {
    case Cmd::TogglePlay: seq->togglePlay(now, midi); break;
    case Cmd::Stop: seq->stop(now, midi); break;
    case Cmd::QueuePattern: seq->queuePattern(c.arg); break;
    case Cmd::SelectPattern: seq->selectPattern(c.arg); break;
    case Cmd::SetBpm: seq->setBpm(c.arg); break;
  }
}

void publish() {
  const Status s{seq->playing(), seq->paused(), seq->pattern(), static_cast<int8_t>(seq->queued()),
                 seq->playPos(), seq->loopCount()};
  portENTER_CRITICAL(&statusMux);
  st = s;
  portEXIT_CRITICAL(&statusMux);
}

void run(void*) {
  for (;;) {
    xSemaphoreTake(projMutex, portMAX_DELAY);
    Command c;
    while (xQueueReceive(cmds, &c, 0) == pdTRUE) handle(c);
    const uint64_t next = seq->process(nowUs(), midi);
    xSemaphoreGive(projMutex);
    publish();

    if (next != mt::kNever) {
      if (next <= nowUs() + 30) continue;  // due almost now: spin once more
      gptimer_alarm_config_t a = {};
      a.alarm_count = next;
      gptimer_set_alarm_action(timer, &a);
    }
    // Woken by the alarm or by post(); the timeout is only a safety net.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
  }
}

}  // namespace

void begin(mt::Project* p) {
  seq = new mt::Sequencer(*p);
  seq->seed(esp_random());
  midi.begin();
  cmds = xQueueCreate(16, sizeof(Command));
  projMutex = xSemaphoreCreateMutex();

  gptimer_config_t cfg = {};
  cfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  cfg.direction = GPTIMER_COUNT_UP;
  cfg.resolution_hz = 1000000;
  ESP_ERROR_CHECK(gptimer_new_timer(&cfg, &timer));
  gptimer_event_callbacks_t cbs = {};
  cbs.on_alarm = onAlarm;
  ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer, &cbs, nullptr));
  ESP_ERROR_CHECK(gptimer_enable(timer));
  ESP_ERROR_CHECK(gptimer_start(timer));

  xTaskCreatePinnedToCore(run, "engine", 6144, nullptr, configMAX_PRIORITIES - 2, &task, 0);
}

void post(Cmd c, uint16_t arg) {
  const Command cmd{c, arg};
  xQueueSend(cmds, &cmd, 0);
  xTaskNotifyGive(task);
}

Status status() {
  portENTER_CRITICAL(&statusMux);
  const Status s = st;
  portEXIT_CRITICAL(&statusMux);
  return s;
}

void lockProject() { xSemaphoreTake(projMutex, portMAX_DELAY); }
void unlockProject() { xSemaphoreGive(projMutex); }

}  // namespace engine
```

**Step 3: Собрать**

Run: `pio run -e wt32`
Expected: SUCCESS (проверка на железе — в Task 12 вместе с UI).

**Step 4: Commit**

```bash
git add src/engine
git commit -m "feat(engine): gptimer-driven sequencer task on core 0"
```

---

### Task 11: Простая сетка (UI)

Overview 8 дорожек; курсор, правка нот, mute по тапу, BPM, транспорт. Detail-вид, модификаторы, follow, undo — этап 3.

Раскладка (480×320): статус 0–23, имена дорожек 24–39, сетка 16 строк × 16 px (40–295), подсказка 296–319. Колонка номеров 32 px, дорожки 8 × 56 px.

**Files:**
- Create: `src/ui/grid_view.h`, `src/ui/grid_view.cpp`

**Step 1: `src/ui/grid_view.h`**

```cpp
#pragma once
#include "engine/engine.h"
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "model.h"

namespace ui {

class GridView {
 public:
  void begin(LGFX* lcd, mt::Project* p);
  void onInput(const hw::InputEvent& ev);
  void pollTouch();
  void tick();

 private:
  static constexpr int kStatusH = 24;
  static constexpr int kNamesH = 16;
  static constexpr int kGridY = kStatusH + kNamesH;
  static constexpr int kRowH = 16;
  static constexpr int kRows = 16;
  static constexpr int kNumW = 32;
  static constexpr int kColW = 56;

  mt::Pattern& pat() { return p_->patterns[status_.pattern]; }
  void draw();
  void moveCursor(int dStep, int dTrack);
  void editNote(int delta);
  void clearStep();
  void onTap(int x, int y);
  void ensureVisible();

  LGFX* lcd_ = nullptr;
  LGFX_Sprite* spr_ = nullptr;
  mt::Project* p_ = nullptr;
  engine::Status status_{};
  int curStep_ = 0;
  int curTrack_ = 0;
  int top_ = 0;
  bool edit_ = false;
  bool bpmEdit_ = false;
  bool shift_ = false;
  bool touching_ = false;
  bool dirty_ = true;
  uint32_t lastDraw_ = 0;
};

}  // namespace ui
```

**Step 2: `src/ui/grid_view.cpp`**

```cpp
#include "grid_view.h"
#include <stdio.h>
#include "note_name.h"

namespace ui {
namespace {
constexpr uint16_t kBg = 0x0000;
constexpr uint16_t kBeatBg = 0x10A2;
constexpr uint16_t kPlayBg = 0x3186;
constexpr uint16_t kCursor = 0xFFE0;
constexpr uint16_t kEditCursor = 0xF800;
constexpr uint16_t kText = 0xFFFF;
constexpr uint16_t kDim = 0x7BEF;
constexpr uint16_t kStatusBg = 0x18C3;
}  // namespace

void GridView::begin(LGFX* lcd, mt::Project* p) {
  lcd_ = lcd;
  p_ = p;
  spr_ = new LGFX_Sprite(lcd_);
  spr_->setPsram(true);
  spr_->setColorDepth(16);
  spr_->createSprite(480, 320);
  spr_->setFont(&fonts::AsciiFont8x16);
  status_ = engine::status();
  dirty_ = true;
}

void GridView::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  shift_ = ev.shift;
  switch (ev.type) {
    case InputType::EncTurn:
      if (bpmEdit_) {
        const int bpm = p_->bpm + ev.delta * (ev.shift ? 10 : 1);
        engine::post(engine::Cmd::SetBpm, bpm < 20 ? 20 : (bpm > 300 ? 300 : bpm));
      } else if (edit_) {
        editNote(ev.shift ? ev.delta * 12 : ev.delta);
      } else if (ev.shift) {
        moveCursor(0, ev.delta);
      } else {
        moveCursor(ev.delta, 0);
      }
      break;
    case InputType::EncClick:
      if (bpmEdit_) bpmEdit_ = false;
      else edit_ = !edit_;
      break;
    case InputType::EncLong:
      clearStep();
      break;
    case InputType::PlayPress:
      engine::post(ev.shift ? engine::Cmd::Stop : engine::Cmd::TogglePlay);
      break;
    case InputType::ShiftDown: shift_ = true; break;
    case InputType::ShiftUp: shift_ = false; break;
  }
  dirty_ = true;
}

void GridView::moveCursor(int dStep, int dTrack) {
  const int len = pat().length;
  curStep_ = ((curStep_ + dStep) % len + len) % len;
  curTrack_ = ((curTrack_ + dTrack) % mt::kTracks + mt::kTracks) % mt::kTracks;
  ensureVisible();
}

void GridView::ensureVisible() {
  if (curStep_ < top_) top_ = curStep_;
  if (curStep_ >= top_ + kRows) top_ = curStep_ - kRows + 1;
}

void GridView::editNote(int delta) {
  engine::lockProject();
  mt::Step& s = pat().steps[curTrack_][curStep_];
  if (!s.hasNote()) {
    s.note = 60;
  } else {
    const int n = s.note + delta;
    s.note = n < 0 ? 0 : (n > 127 ? 127 : n);
  }
  engine::unlockProject();
}

void GridView::clearStep() {
  engine::lockProject();
  pat().steps[curTrack_][curStep_] = mt::Step();
  engine::unlockProject();
  edit_ = false;
}

void GridView::pollTouch() {
  int32_t x, y;
  const bool t = lcd_->getTouch(&x, &y);
  if (t && !touching_) onTap(x, y);
  touching_ = t;
}

void GridView::onTap(int x, int y) {
  if (y < kStatusH) {
    if (x >= 64 && x < 176) {  // BPM field
      bpmEdit_ = !bpmEdit_;
      edit_ = false;
    }
  } else if (y < kGridY) {
    if (x >= kNumW) {
      const int tr = (x - kNumW) / kColW;
      engine::lockProject();
      if (shift_) p_->tracks[tr].solo = !p_->tracks[tr].solo;
      else p_->tracks[tr].mute = !p_->tracks[tr].mute;
      engine::unlockProject();
    }
  } else if (y < kGridY + kRows * kRowH && x >= kNumW) {
    const int step = top_ + (y - kGridY) / kRowH;
    const int tr = (x - kNumW) / kColW;
    if (step < pat().length) {
      if (step == curStep_ && tr == curTrack_) {
        edit_ = !edit_;
      } else {
        curStep_ = step;
        curTrack_ = tr;
        edit_ = false;
      }
      bpmEdit_ = false;
    }
  }
  dirty_ = true;
}

void GridView::tick() {
  const engine::Status s = engine::status();
  if (!(s == status_)) {
    if (s.pattern != status_.pattern) {
      curStep_ = 0;
      top_ = 0;
    }
    status_ = s;
    dirty_ = true;
  }
  const uint32_t now = millis();
  if (dirty_ && now - lastDraw_ >= 25) {
    draw();
    dirty_ = false;
    lastDraw_ = now;
  }
}

void GridView::draw() {
  mt::Pattern& pt = pat();
  if (curStep_ >= pt.length) curStep_ = pt.length - 1;
  ensureVisible();
  char buf[64];
  spr_->fillScreen(kBg);

  // Status line.
  spr_->fillRect(0, 0, 480, kStatusH, kStatusBg);
  spr_->setTextColor(kText);
  snprintf(buf, sizeof(buf), "P%02d", status_.pattern + 1);
  spr_->drawString(buf, 8, 4);
  if (status_.queued >= 0) {
    snprintf(buf, sizeof(buf), ">P%02d", status_.queued + 1);
    spr_->drawString(buf, 32, 4);
  }
  spr_->setTextColor(bpmEdit_ ? kEditCursor : kText);
  snprintf(buf, sizeof(buf), "%3u BPM", p_->bpm);
  spr_->drawString(buf, 80, 4);
  spr_->setTextColor(kText);
  snprintf(buf, sizeof(buf), "%u/%u", status_.pos + 1, pt.length);
  spr_->drawString(buf, 200, 4);
  spr_->drawString(status_.playing ? "PLAY" : (status_.paused ? "PAUSE" : "STOP"), 300, 4);
  snprintf(buf, sizeof(buf), "L%lu", static_cast<unsigned long>(status_.loop));
  spr_->drawString(buf, 400, 4);

  // Track names, coloured by mute/solo.
  for (int tr = 0; tr < mt::kTracks; ++tr) {
    const mt::TrackCfg& t = p_->tracks[tr];
    spr_->setTextColor(t.solo ? kCursor : (p_->trackAudible(tr) ? kText : kDim));
    spr_->drawString(t.name, kNumW + tr * kColW + 4, kStatusH);
  }

  // Grid.
  for (int row = 0; row < kRows; ++row) {
    const int step = top_ + row;
    if (step >= pt.length) break;
    const int y = kGridY + row * kRowH;
    if ((status_.playing || status_.paused) && step == status_.pos) spr_->fillRect(0, y, 480, kRowH, kPlayBg);
    else if (step % 4 == 0) spr_->fillRect(0, y, 480, kRowH, kBeatBg);

    spr_->setTextColor(kDim);
    snprintf(buf, sizeof(buf), "%3d", step + 1);
    spr_->drawString(buf, 2, y);

    for (int tr = 0; tr < mt::kTracks; ++tr) {
      const mt::Step& s = pt.steps[tr][step];
      const int x = kNumW + tr * kColW;
      char nn[4];
      mt::noteName(s.note, nn);
      spr_->setTextColor(s.hasNote() && p_->trackAudible(tr) ? kText : kDim);
      spr_->drawString(nn, x + 4, y);
      if (s.hasNote()) {
        const uint8_t vel = s.vel ? s.vel : p_->tracks[tr].defVel;
        spr_->fillRect(x + 32, y + 12, (vel * 20) / 127, 2, kDim);
      }
      if (s.fx[0].cmd != mt::Fx::None || s.fx[1].cmd != mt::Fx::None) spr_->fillRect(x + 48, y + 6, 3, 3, kCursor);
      if (step == curStep_ && tr == curTrack_) spr_->drawRect(x, y, kColW, kRowH, edit_ ? kEditCursor : kCursor);
    }
  }

  // Hint line.
  spr_->setTextColor(kDim);
  spr_->drawString(edit_ ? "EDIT: turn=note  shift+turn=oct  click=done"
                         : "turn=step  shift+turn=track  click=edit  hold=clear",
                   4, 300);
  if (shift_) {
    spr_->setTextColor(kCursor);
    spr_->drawString("SHIFT", 430, 300);
  }

  spr_->pushSprite(0, 0);
}

}  // namespace ui
```

**Step 3: Собрать** — `pio run -e wt32`, Expected: SUCCESS.

**Step 4: Commit**

```bash
git add src/ui
git commit -m "feat(ui): overview grid with note editing and transport"
```

---

### Task 12: Интеграция и проверка на железе

**Files:**
- Modify: `src/main.cpp` (финальная версия этапа)

**Step 1: `src/main.cpp`**

```cpp
#include <Arduino.h>
#include <new>
#include "engine/engine.h"
#include "esp_heap_caps.h"
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "model.h"
#include "ui/grid_view.h"

static LGFX lcd;
static mt::Project* project;
static ui::GridView grid;

// Simple beat so Play produces something right after flashing.
static void loadDemo(mt::Project& p) {
  mt::Pattern& pat = p.patterns[0];
  for (int i = 0; i < 16; i += 4) pat.steps[0][i].note = 36;
  pat.steps[1][4].note = 38;
  pat.steps[1][12].note = 38;
  for (int i = 2; i < 16; i += 4) pat.steps[2][i].note = 42;
}

void setup() {
  Serial.begin(115200);
  void* mem = heap_caps_malloc(sizeof(mt::Project), MALLOC_CAP_SPIRAM);
  project = new (mem) mt::Project();
  loadDemo(*project);

  lcd.init();
  lcd.setRotation(1);
  lcd.setBrightness(200);

  hw::inputBegin();
  engine::begin(project);
  grid.begin(&lcd, project);
}

void loop() {
  hw::InputEvent ev;
  while (hw::inputPoll(ev, 0)) grid.onInput(ev);
  grid.pollTouch();
  grid.tick();
  vTaskDelay(1);
}
```

**Step 2: Native-тесты ещё раз** — `pio test -e native`, Expected: все PASSED.

**Step 3: Прошить** — `pio run -e wt32 -t upload && pio device monitor -e wt32`.

**Step 4: Ручной чек-лист (MIDI Monitor на Mac)**
- [ ] на экране сетка 16 шагов, демо-бит на дорожках 1–3;
- [ ] Play: в мониторе `Start`, поток `Clock`, ноты 36/38/42 на каналах 1/2/3; плейхед бежит;
- [ ] темп в мониторе 120 BPM (MIDI Monitor показывает интервал clock ≈ 20.8 мс);
- [ ] Play ещё раз: `Stop`, все ноты закрыты; ещё раз: `Song Position` + `Continue`, продолжает с того же шага;
- [ ] Shift+Play: `Stop`, плейхед на шаге 1;
- [ ] энкодер двигает курсор, Shift+энкодер переключает дорожку, клик — правка (красная рамка), поворот меняет ноту, Shift+поворот — октаву, во время игры правка слышна на следующем проходе;
- [ ] удержание энкодера очищает шаг;
- [ ] тап по ячейке переносит курсор, повторный тап — правка;
- [ ] тап по имени дорожки — mute (серая), Shift+тап — solo (жёлтая);
- [ ] тап по BPM, вращение меняет темп на лету без сбоя ритма;
- [ ] джиттер: в MIDI Monitor интервалы между нотами стабильны (±1 мс — предел точности монитора); при активном тапе/вращении ритм не плывёт.

**Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat: integrate engine, input and grid UI (stage 1-2)"
```

---

## Дальше

Отдельные планы после прохождения чек-листа на железе:
- этап 3: Detail-вид, редактор модификаторов, оставшиеся fx (CHD, STR, CND, VRN, NRN, CC, PB, PGM), смена длины/разрешения/swing, follow, undo, copy/paste, экраны TRACK/BANK/PROJ;
- этап 4: SD, формат `.mtp`, FILE;
- этап 5: лады, scale lock, Euclid;
- этап 6: импорт MIDI.
