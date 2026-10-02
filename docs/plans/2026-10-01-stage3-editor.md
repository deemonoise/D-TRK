# Этап 3: полноценный редактор — план реализации

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Лады и scale lock, все модификаторы шага, Detail-вид, редактор полей шага, undo, copy/paste/clear/transpose блоков, follow, контекстное меню, экраны TRACK / BANK / PROJ, тач-жесты (тап, долгий тап, свайп).

**Architecture:** Логика (лады, описание fx, развёртка всех fx, операции редактирования, undo) — в `lib/core`, с native-тестами. UI переделывается из одного `GridView` в `App` (статус-бар, таб-бар, модальное меню, менеджер экранов) + отдельные экраны. Все записи в `Project` из UI — под `engine::lockProject()`.

**Tech Stack:** как на этапах 1–2.

Дизайн: `docs/plans/2026-10-01-midi-tracker-design.md`, секции 2 и 4. Предыдущий план: `docs/plans/2026-10-01-stage1-2-core-engine.md`.

Соглашения:
- путь с пробелом — в кавычках: `"/Users/deemonoise/Documents/PlatformIO/Projects/MIDI -tracker"`;
- `pio` = `~/.platformio/penv/bin/pio`; тесты `pio test -e native`, сборка `pio run -e wt32`;
- **коммитов не делать** (просьба пользователя) — шагов Commit в плане нет;
- TDD для `lib/core`: тест → убедиться, что падает → код → тесты зелёные.

Изменение порядка этапов: лады перенесены сюда из этапа 5 (нужны для `CHD`, `NRN`, scale lock). В этапе 5 остаётся Euclid.

---

### Task 1: Лады

**Files:**
- Create: `lib/core/src/scale.h`, `lib/core/src/scale.cpp`
- Modify: `lib/core/src/model.h` (тип `scaleType` остаётся `uint8_t`, значение = `ScaleType`; по умолчанию 0 = Chromatic)
- Test: `test/test_scale/test_main.cpp`

**Step 1: Падающий тест**

```cpp
#include <unity.h>
#include "scale.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_masks() {
  TEST_ASSERT_EQUAL_HEX16(0x0FFF, scaleMask(ScaleType::Chromatic));
  TEST_ASSERT_EQUAL_HEX16(0x0AB5, scaleMask(ScaleType::Major));  // 0 2 4 5 7 9 11
  TEST_ASSERT_EQUAL_HEX16(0x05AD, scaleMask(ScaleType::Minor));  // 0 2 3 5 7 8 10
}

void test_in_scale_with_root() {
  // D major: D E F# G A B C#
  TEST_ASSERT_TRUE(inScale(62, 2, ScaleType::Major));
  TEST_ASSERT_TRUE(inScale(66, 2, ScaleType::Major));
  TEST_ASSERT_FALSE(inScale(65, 2, ScaleType::Major));
  TEST_ASSERT_TRUE(inScale(65, 2, ScaleType::Chromatic));
}

void test_move_degrees() {
  // C major
  TEST_ASSERT_EQUAL(62, moveDegrees(60, 1, 0, ScaleType::Major));
  TEST_ASSERT_EQUAL(59, moveDegrees(60, -1, 0, ScaleType::Major));
  TEST_ASSERT_EQUAL(72, moveDegrees(60, 7, 0, ScaleType::Major));
  // out-of-scale start: C# +1 -> D, C# -1 -> C
  TEST_ASSERT_EQUAL(62, moveDegrees(61, 1, 0, ScaleType::Major));
  TEST_ASSERT_EQUAL(60, moveDegrees(61, -1, 0, ScaleType::Major));
  // chromatic = semitones
  TEST_ASSERT_EQUAL(63, moveDegrees(60, 3, 0, ScaleType::Chromatic));
  // clamped to MIDI range
  TEST_ASSERT_EQUAL(127, moveDegrees(126, 5, 0, ScaleType::Chromatic));
  TEST_ASSERT_EQUAL(0, moveDegrees(1, -5, 0, ScaleType::Chromatic));
}

void test_chords() {
  uint8_t n[4];
  // C major triad from C4 in C major
  TEST_ASSERT_EQUAL(3, chordNotes(60, kChordTriad, 0, ScaleType::Major, n));
  TEST_ASSERT_EQUAL(60, n[0]); TEST_ASSERT_EQUAL(64, n[1]); TEST_ASSERT_EQUAL(67, n[2]);
  // D in C major -> D minor triad
  chordNotes(62, kChordTriad, 0, ScaleType::Major, n);
  TEST_ASSERT_EQUAL(65, n[1]); TEST_ASSERT_EQUAL(69, n[2]);
  // seventh: 4 notes
  TEST_ASSERT_EQUAL(4, chordNotes(60, kChordSeventh, 0, ScaleType::Major, n));
  TEST_ASSERT_EQUAL(71, n[3]);
  // power / octave are fixed intervals
  TEST_ASSERT_EQUAL(2, chordNotes(60, kChordPower, 0, ScaleType::Minor, n));
  TEST_ASSERT_EQUAL(67, n[1]);
  chordNotes(60, kChordOctave, 0, ScaleType::Minor, n);
  TEST_ASSERT_EQUAL(72, n[1]);
  // chromatic scale: chords use major intervals relative to the note
  chordNotes(61, kChordTriad, 0, ScaleType::Chromatic, n);
  TEST_ASSERT_EQUAL(65, n[1]); TEST_ASSERT_EQUAL(68, n[2]);
  // notes above 127 are dropped
  TEST_ASSERT_EQUAL(1, chordNotes(126, kChordOctave, 0, ScaleType::Chromatic, n));
}

void test_names() {
  TEST_ASSERT_EQUAL_STRING("Major", scaleName(ScaleType::Major));
  TEST_ASSERT_EQUAL_STRING("tri", chordName(kChordTriad));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_masks);
  RUN_TEST(test_in_scale_with_root);
  RUN_TEST(test_move_degrees);
  RUN_TEST(test_chords);
  RUN_TEST(test_names);
  return UNITY_END();
}
```

**Step 2:** `pio test -e native -f test_scale` — FAIL (нет `scale.h`).

**Step 3: Реализация**

`lib/core/src/scale.h`:

```cpp
#pragma once
#include <stdint.h>

namespace mt {

enum class ScaleType : uint8_t {
  Chromatic, Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian,
  HarmonicMinor, MelodicMinor, PentatonicMajor, PentatonicMinor, Blues, Count
};

// CHD values.
enum : uint8_t {
  kChordTriad, kChordSeventh, kChordSus2, kChordSus4, kChordSixth, kChordAdd9,
  kChordPower, kChordOctave, kChordCount
};

uint16_t scaleMask(ScaleType t);  // bit i set = semitone i above the root is in the scale
const char* scaleName(ScaleType t);
const char* chordName(uint8_t chord);  // 3 chars
bool inScale(int note, uint8_t root, ScaleType t);
// Moves by scale degrees. An out-of-scale start counts its nearest lower scale note as one
// degree down. Result is clamped to 0..127.
int moveDegrees(int note, int degrees, uint8_t root, ScaleType t);
// Fills up to 4 notes (root first), drops notes above 127. Returns the count.
int chordNotes(uint8_t note, uint8_t chord, uint8_t root, ScaleType t, uint8_t out[4]);

}  // namespace mt
```

`lib/core/src/scale.cpp`:

```cpp
#include "scale.h"

namespace mt {
namespace {

constexpr uint16_t kMasks[] = {
    0x0FFF,  // Chromatic
    0x0AB5,  // Major            0 2 4 5 7 9 11
    0x05AD,  // Minor            0 2 3 5 7 8 10
    0x06AD,  // Dorian           0 2 3 5 7 9 10
    0x05AB,  // Phrygian         0 1 3 5 7 8 10
    0x0AD5,  // Lydian           0 2 4 6 7 9 11
    0x06B5,  // Mixolydian       0 2 4 5 7 9 10
    0x056B,  // Locrian          0 1 3 5 6 8 10
    0x09AD,  // Harmonic minor   0 2 3 5 7 8 11
    0x0AAD,  // Melodic minor    0 2 3 5 7 9 11
    0x0295,  // Pentatonic major 0 2 4 7 9
    0x04A9,  // Pentatonic minor 0 3 5 7 10
    0x04E9,  // Blues            0 3 5 6 7 10
};
constexpr const char* kScaleNames[] = {"Chrom", "Major", "Minor", "Dorian", "Phryg", "Lydian", "Mixo",
                                       "Locr", "HarmMin", "MelMin", "PentMaj", "PentMin", "Blues"};
constexpr const char* kChordNames[] = {"tri", "7th", "su2", "su4", "6th", "ad9", "pwr", "oct"};
// Scale degrees of each degree-based chord, -1 terminates.
constexpr int8_t kChordDegrees[][4] = {
    {0, 2, 4, -1}, {0, 2, 4, 6}, {0, 1, 4, -1}, {0, 3, 4, -1}, {0, 2, 4, 5}, {0, 2, 4, 8},
};

int clampNote(int n) { return n < 0 ? 0 : (n > 127 ? 127 : n); }

}  // namespace

uint16_t scaleMask(ScaleType t) {
  const auto i = static_cast<uint8_t>(t);
  return i < static_cast<uint8_t>(ScaleType::Count) ? kMasks[i] : kMasks[0];
}

const char* scaleName(ScaleType t) {
  const auto i = static_cast<uint8_t>(t);
  return i < static_cast<uint8_t>(ScaleType::Count) ? kScaleNames[i] : kScaleNames[0];
}

const char* chordName(uint8_t chord) { return chord < kChordCount ? kChordNames[chord] : "???"; }

bool inScale(int note, uint8_t root, ScaleType t) {
  const int pc = ((note - root) % 12 + 12) % 12;
  return (scaleMask(t) >> pc) & 1;
}

int moveDegrees(int note, int degrees, uint8_t root, ScaleType t) {
  int n = clampNote(note);
  if (!inScale(n, root, t)) {
    while (n > 0 && !inScale(n, root, t)) --n;
    if (degrees < 0) ++degrees;  // stepping onto the lower scale note already moved one degree down
  }
  const int dir = degrees > 0 ? 1 : -1;
  for (int k = degrees > 0 ? degrees : -degrees; k > 0; --k) {
    int m = n + dir;
    while (m >= 0 && m <= 127 && !inScale(m, root, t)) m += dir;
    if (m < 0 || m > 127) break;
    n = m;
  }
  return n;
}

int chordNotes(uint8_t note, uint8_t chord, uint8_t root, ScaleType t, uint8_t out[4]) {
  int count = 0;
  auto add = [&](int n) {
    if (n >= 0 && n <= 127 && count < 4) out[count++] = static_cast<uint8_t>(n);
  };
  if (chord == kChordPower) {
    add(note);
    add(note + 7);
    return count;
  }
  if (chord == kChordOctave) {
    add(note);
    add(note + 12);
    return count;
  }
  if (chord >= kChordPower) {
    add(note);
    return count;
  }
  // Chromatic has no harmony of its own: build the chord as if the note were a major tonic.
  const bool chromatic = t == ScaleType::Chromatic;
  const uint8_t r = chromatic ? note % 12 : root;
  const ScaleType s = chromatic ? ScaleType::Major : t;
  for (int8_t d : kChordDegrees[chord]) {
    if (d < 0) break;
    if (d == 0) add(note);
    else {
      const int n = moveDegrees(note, d, r, s);
      if (n > note) add(n);  // moveDegrees clamps at 127: ignore notes that didn't move up
    }
  }
  return count;
}

}  // namespace mt
```

**Step 4:** `pio test -e native -f test_scale` — PASS. Полный `pio test -e native` — зелёный.

---

### Task 2: Описание модификаторов (имена, диапазоны, форматирование, шаг значения)

**Files:**
- Create: `lib/core/src/fx_info.h`, `lib/core/src/fx_info.cpp`
- Test: `test/test_fx_info/test_main.cpp`

Кодирование `CND`: `0` = `FST` (только первый проход), иначе `(A << 4) | B`, B = 2..8, A = 1..B. Порядок перебора энкодером: `FST, 1:2, 2:2, 1:3, 2:3, 3:3, 1:4 … 8:8` (36 значений).

**Step 1: Падающий тест**

```cpp
#include <unity.h>
#include <string.h>
#include "fx_info.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static const char* fmt(Fx f, uint8_t v) {
  static char b[5];
  fxFormat(f, v, b);
  return b;
}

void test_names_and_defaults() {
  TEST_ASSERT_EQUAL_STRING("RAT", fxName(Fx::RAT));
  TEST_ASSERT_EQUAL_STRING("...", fxName(Fx::None));
  TEST_ASSERT_EQUAL(2, fxDefault(Fx::RAT));
  TEST_ASSERT_EQUAL(0x12, fxDefault(Fx::CND));
}

void test_format() {
  TEST_ASSERT_EQUAL_STRING("  4", fmt(Fx::RAT, 4));
  TEST_ASSERT_EQUAL_STRING("-25", fmt(Fx::NDG, static_cast<uint8_t>(-25)));
  TEST_ASSERT_EQUAL_STRING("+10", fmt(Fx::NDG, 10));
  TEST_ASSERT_EQUAL_STRING("800", fmt(Fx::GAT, 200));
  TEST_ASSERT_EQUAL_STRING("3:4", fmt(Fx::CND, 0x34));
  TEST_ASSERT_EQUAL_STRING("FST", fmt(Fx::CND, 0));
  TEST_ASSERT_EQUAL_STRING("7th", fmt(Fx::CHD, 1));
  TEST_ASSERT_EQUAL_STRING(" --", fmt(Fx::TIE, 0));
  TEST_ASSERT_EQUAL_STRING("   ", fmt(Fx::None, 0));
}

void test_step_clamps_and_signed() {
  TEST_ASSERT_EQUAL(8, fxStep(Fx::RAT, 7, 5));
  TEST_ASSERT_EQUAL(2, fxStep(Fx::RAT, 3, -9));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(-50), fxStep(Fx::NDG, 0, -80));
  TEST_ASSERT_EQUAL(50, fxStep(Fx::NDG, static_cast<uint8_t>(-1), 100));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(-64), fxStep(Fx::PBN, 0, -100));
}

void test_cnd_order() {
  TEST_ASSERT_EQUAL(0x12, fxStep(Fx::CND, 0, 1));
  TEST_ASSERT_EQUAL(0x22, fxStep(Fx::CND, 0x12, 1));
  TEST_ASSERT_EQUAL(0x13, fxStep(Fx::CND, 0x22, 1));
  TEST_ASSERT_EQUAL(0, fxStep(Fx::CND, 0x12, -1));
  TEST_ASSERT_EQUAL(0x88, fxStep(Fx::CND, 0x78, 100));
}

void test_cmd_cycle() {
  TEST_ASSERT_TRUE(fxNextCmd(Fx::None, 1) == Fx::CHN);
  TEST_ASSERT_TRUE(fxNextCmd(Fx::None, -1) == Fx::PGM);
  TEST_ASSERT_TRUE(fxNextCmd(Fx::PGM, 1) == Fx::None);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_names_and_defaults);
  RUN_TEST(test_format);
  RUN_TEST(test_step_clamps_and_signed);
  RUN_TEST(test_cnd_order);
  RUN_TEST(test_cmd_cycle);
  return UNITY_END();
}
```

**Step 2:** `pio test -e native -f test_fx_info` — FAIL.

**Step 3: Реализация**

`lib/core/src/fx_info.h`:

```cpp
#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

const char* fxName(Fx f);                    // 3 chars, "..." for None
uint8_t fxDefault(Fx f);                     // value set when the command is chosen
void fxFormat(Fx f, uint8_t v, char out[5]);  // 3 chars, right-aligned
uint8_t fxStep(Fx f, uint8_t v, int delta);  // encoder step, clamped, signed-aware
Fx fxNextCmd(Fx f, int delta);               // cycles None..PGM

}  // namespace mt
```

`lib/core/src/fx_info.cpp`:

```cpp
#include "fx_info.h"
#include <stdio.h>
#include "scale.h"

namespace mt {
namespace {

struct Info {
  const char* name;
  int16_t min, max, def;
  bool isSigned;
};

constexpr Info kInfo[] = {
    {"...", 0, 0, 0, false},     // None
    {"CHN", 1, 16, 1, false},    // CHN
    {"RAT", 2, 8, 2, false},     // RAT
    {"PRB", 0, 100, 50, false},  // PRB
    {"GAT", 1, 200, 100, false}, // GAT (see gatePercent)
    {"TIE", 0, 0, 0, false},     // TIE
    {"NDG", -50, 50, 0, true},   // NDG
    {"CHD", 0, kChordCount - 1, 0, false},
    {"STR", 1, 50, 10, false},   // STR
    {"CND", 0, 0, 0x12, false},  // CND: own ordering
    {"VRN", 0, 64, 16, false},   // VRN
    {"NRN", 1, 7, 1, false},     // NRN
    {"CCA", 0, 127, 64, false},  // CCA
    {"CCB", 0, 127, 64, false},  // CCB
    {"PBN", -64, 63, 0, true},   // PBN
    {"PGM", 0, 127, 0, false},   // PGM
};
static_assert(sizeof(kInfo) / sizeof(kInfo[0]) == static_cast<int>(Fx::Count), "kInfo must cover Fx");

const Info& info(Fx f) {
  const auto i = static_cast<uint8_t>(f);
  return kInfo[i < static_cast<uint8_t>(Fx::Count) ? i : 0];
}

// CND values in encoder order: FST, then A:B for B = 2..8, A = 1..B.
int cndIndex(uint8_t v) {
  if (v == 0) return 0;
  const int a = v >> 4, b = v & 15;
  if (b < 2 || b > 8 || a < 1 || a > b) return 0;
  int idx = 1;
  for (int bb = 2; bb < b; ++bb) idx += bb;
  return idx + a - 1;
}

uint8_t cndValue(int idx) {
  if (idx <= 0) return 0;
  int rest = idx - 1;
  for (int b = 2; b <= 8; ++b) {
    if (rest < b) return static_cast<uint8_t>(((rest + 1) << 4) | b);
    rest -= b;
  }
  return 0x88;
}

constexpr int kCndCount = 36;

}  // namespace

const char* fxName(Fx f) { return info(f).name; }
uint8_t fxDefault(Fx f) { return static_cast<uint8_t>(info(f).def); }

void fxFormat(Fx f, uint8_t v, char out[5]) {
  switch (f) {
    case Fx::None: snprintf(out, 5, "   "); return;
    case Fx::TIE: snprintf(out, 5, " --"); return;
    case Fx::GAT: snprintf(out, 5, "%3u", gatePercent(v)); return;
    case Fx::NDG:
    case Fx::PBN: snprintf(out, 5, "%+3d", fxSigned(v)); return;
    case Fx::CHD: snprintf(out, 5, "%s", chordName(v)); return;
    case Fx::CND:
      if (v == 0) snprintf(out, 5, "FST");
      else snprintf(out, 5, "%d:%d", v >> 4, v & 15);
      return;
    default: snprintf(out, 5, "%3u", v); return;
  }
}

uint8_t fxStep(Fx f, uint8_t v, int delta) {
  if (f == Fx::CND) {
    int i = cndIndex(v) + delta;
    i = i < 0 ? 0 : (i >= kCndCount ? kCndCount - 1 : i);
    return cndValue(i);
  }
  const Info& in = info(f);
  int cur = in.isSigned ? fxSigned(v) : v;
  cur += delta;
  cur = cur < in.min ? in.min : (cur > in.max ? in.max : cur);
  return static_cast<uint8_t>(cur);
}

Fx fxNextCmd(Fx f, int delta) {
  const int n = static_cast<int>(Fx::Count);
  const int i = ((static_cast<int>(f) + delta) % n + n) % n;
  return static_cast<Fx>(i);
}

}  // namespace mt
```

**Step 4:** `pio test -e native -f test_fx_info` — PASS; полный набор зелёный.

---

### Task 3: Развёртка всех модификаторов

Новая сигнатура: контекст вместо `stepUs`, плюс управляющие события.

**Files:**
- Modify: `lib/core/src/step_expand.h`, `lib/core/src/step_expand.cpp`
- Modify: `lib/core/src/sequencer.cpp` (вызов `expandStep`, проталкивание CC/PB/PGM)
- Modify: `test/test_expand/test_main.cpp` (перевести на `ExpandCtx`, добавить тесты)

**Новый API** (`step_expand.h`):

```cpp
enum class EvKind : uint8_t { NoteOn, NoteOff, Cc, PitchBend, Program };

struct StepEvent {
  int32_t offsetUs;
  EvKind kind;
  uint8_t ch;
  uint8_t note;  // Cc: controller; PitchBend: LSB; Program: program
  uint8_t vel;   // Cc: value; PitchBend: MSB
};

constexpr int kMaxStepEvents = 72;  // 8 ratchets x 4 chord notes x 2 + controls

struct ExpandCtx {
  uint32_t stepUs;
  uint32_t loop;         // pass counter of the pattern, for CND
  uint8_t scaleRoot;
  ScaleType scale;
};

bool expandStep(const Step& s, const TrackCfg& t, const ExpandCtx& c, Rng& rng, ExpandOut& out);
```

**Правила развёртки (порядок важен):**
1. `CND`: `0` (FST) — играет только при `loop == 0`; `A:B` — играет, если `loop % B == A - 1`. Не прошло — `false`, ничего.
2. `PRB` — как раньше. Не прошло — `false`.
3. Управляющие события (даже на шаге без ноты и на `OFF`), со смещением `NDG`, на канале шага (`CHN` или канал дорожки), идут в `out` **перед** нотами:
   - `CCA`/`CCB`: `Cc`, controller = `t.ccA`/`t.ccB`, value = `min(val,127)`;
   - `PBN`: значение `v = fxSigned(val)` (−64..63) → 14 бит `8192 + v * 128`, LSB = `& 0x7F`, MSB = `>> 7`;
   - `PGM`: `Program`, program = `val & 127`.
4. Нет ноты (`!hasNote()`) — вернуть `out.count > 0`.
5. `NRN`: `d = rng.below(2*val+1) - val`; нота = `moveDegrees(note, d, root, scale)`.
6. `VRN`: velocity += `rng.below(2*val+1) - val`, затем clamp 1..127.
7. Ноты: `CHD` → `chordNotes(...)`, иначе одна нота.
8. `STR`: смещение i-й ноты аккорда внутри каждого удара = `i * stepUs * val / 100`.
9. Порядок событий: для каждого удара ratchet, для каждой ноты аккорда — `NoteOn`, сразу за ним его `NoteOff` (секвенсор полагается на эту парность).
10. `TIE` действует только при одной ноте (без `CHD`): последний `NoteOn` без `NoteOff`, `out.tie = true`. С аккордом `TIE` игнорируется.

**Изменения в секвенсоре:**
- `ExpandCtx` собирается в `scheduleStep`: `stepUs()`, `loop_`, `p_.scaleRoot`, `static_cast<ScaleType>(p_.scaleType)`.
- События `Cc` → `0xB0|ch, cc, val` (len 3), `PitchBend` → `0xE0|ch, lsb, msb` (len 3), `Program` → `0xC0|ch, prog` (len 2); `id = 0`. Их **не** засчитывать как «первый NoteOn» в логике продления `TIE` — искать первое событие с `kind == NoteOn`.
- Шаг `OFF` с управляющими fx: сначала `releaseTie`, затем отправить управляющие события.
- Шаг без ноты, но с управляющими fx: отправить их (mute дорожки их тоже глушит).

**Тесты** (добавить в `test/test_expand`, существующие перевести на `ExpandCtx{125000, 0, 0, ScaleType::Chromatic}`):
- `CND 1:2`: играет при loop 0, 2; не играет при 1, 3. `CND 2:4`: только loop 1, 5. `FST`: только loop 0.
- `CCA 100` на пустом шаге: одно событие `Cc`, controller 74, value 100.
- `PBN -64` → LSB 0, MSB 0; `PBN 0` → LSB 0, MSB 64; `PBN 63` → 8192+8064=16256 → LSB 0, MSB 127.
- `PGM 5` на шаге с нотой: `Program` первым событием, затем ноты.
- `CHD tri` в C major от C4: 3 пары on/off, ноты 60/64/67, все `on` в 0.
- `CHD tri` + `STR 10`: `on` в 0, 12500, 25000.
- `CHD 7th` + `RAT 8`: 64 события, переполнения нет.
- `NRN 1` в C major от C4, 1000 прогонов: результат всегда ∈ {59, 60, 62}, встречаются все три.
- `VRN 64` при vel 100: всегда 36..127 и есть разброс.
- `CHD` + `TIE`: `tie == false`, у каждой ноты есть `NoteOff`.

В `test/test_sequencer` добавить:
- `CCA` на шаге 0 → в логе `B0 4A <val>` в момент шага;
- `CND 1:2` на шаге 0, длина 4: нота звучит на проходах 0 и 2, но не на 1 и 3;
- tie extension не ломается, если на шаге есть `PGM` (первое событие — `Program`).

**Step N:** `pio test -e native` — всё зелёное; `pio run -e wt32` — SUCCESS.

---

### Task 4: Операции редактирования и undo

**Files:**
- Create: `lib/core/src/edit_ops.h`, `lib/core/src/edit_ops.cpp`, `lib/core/src/undo.h`
- Test: `test/test_edit/test_main.cpp`

**API:**

```cpp
// edit_ops.h
namespace mt {

struct Sel {
  uint8_t t0, t1;  // tracks, inclusive, t0 <= t1
  uint8_t s0, s1;  // steps, inclusive, s0 <= s1
};
Sel makeSel(int trackA, int stepA, int trackB, int stepB);  // normalizes order and clamps

struct Clipboard {
  uint8_t tracks = 0, steps = 0;  // 0 = empty
  Step data[kTracks][kMaxSteps];
};

void copySel(const Pattern& p, const Sel& s, Clipboard& cb);
// Pastes with the clipboard's top-left at (track, step); clipped to kTracks and p.length.
void pasteAt(Pattern& p, const Clipboard& cb, int track, int step);
void clearSel(Pattern& p, const Sel& s);
// Notes only (OFF and empty steps untouched). degrees=true moves by scale degrees.
void transposeSel(Pattern& p, const Sel& s, int amount, bool degrees, uint8_t root, ScaleType t);

}  // namespace mt
```

```cpp
// undo.h — ring of pattern snapshots; storage supplied by the caller (PSRAM on device)
namespace mt {

class Undo {
 public:
  static constexpr int kDepth = 32;
  struct Entry {
    uint8_t pattern;
    Pattern data;
  };
  explicit Undo(Entry* storage) : buf_(storage) {}
  void push(uint8_t pattern, const Pattern& p);  // overwrites the oldest when full
  bool pop(uint8_t& pattern, Pattern& out);      // false when empty
  int size() const { return count_; }
  void clear() { count_ = 0; }

 private:
  Entry* buf_;
  int head_ = 0;  // next write slot
  int count_ = 0;
};

}  // namespace mt
```

`push`: `buf_[head_] = {pattern, p}`; `head_ = (head_ + 1) % kDepth`; `count_ = min(count_ + 1, kDepth)`.
`pop`: если `count_ == 0` → false; `head_ = (head_ - 1 + kDepth) % kDepth`; выдать `buf_[head_]`; `--count_`.

**Тесты:**
- `makeSel(5, 10, 2, 3)` → t0 2, t1 5, s0 3, s1 10; значения за пределами клампятся (трек 9 → 7, шаг 200 → 127).
- copy 2×3 блока, paste в (6, 14) при длине 16: вставились только трек 6–7 и шаги 14–15, остальное не тронуто.
- `clearSel` очищает только выделение.
- `transposeSel` +12 полутонов: ноты сдвинуты, `OFF` и пустые не тронуты, 120 + 12 клампится до 127; `degrees=true` в C major: 60 → 62 при +1.
- Undo: push 3 разных паттернов, pop возвращает их в обратном порядке; push 40 штук → `size() == 32`, первым pop выходит последний, 33-й pop → false.

Undo-хранилище в тестах: `new Undo::Entry[Undo::kDepth]`.

**Step N:** `pio test -e native` — зелёный.

---

### Task 5: Каркас UI — App, экраны, тач-жесты, меню

Заменяет `GridView` на `App`. Сетка переезжает в `GridScreen` (Task 6), экраны TRACK/BANK/PROJ — в Task 7. На этом шаге `GridScreen` — перенос текущего `GridView` без новых функций, чтобы всё собиралось.

**Files:**
- Create: `src/ui/theme.h`, `src/ui/screen.h`, `src/ui/touch.h`, `src/ui/touch.cpp`, `src/ui/menu.h`, `src/ui/menu.cpp`, `src/ui/app.h`, `src/ui/app.cpp`, `src/ui/grid_screen.h`, `src/ui/grid_screen.cpp`
- Delete: `src/ui/grid_view.h`, `src/ui/grid_view.cpp`
- Modify: `src/main.cpp` (`ui::App app;` вместо `GridView`)

**Раскладка экрана 480×320:**
- статус-бар 0–23 (рисует `App`);
- рабочая область экрана 24–295 (272 px; `Screen::draw` получает `y0 = 24`, `h = 272`);
- таб-бар 296–319: 5 вкладок по 96 px: `GRID TRACK BANK PROJ FILE` (FILE серая, неактивна до этапа 4), активная подсвечена, справа поверх — индикатор `SHIFT`.

**`theme.h`:** цвета из текущего `grid_view.cpp` (kBg, kBeatBg, kPlayBg, kCursor, kEditCursor, kText, kDim, kStatusBg) + `kSelBg = 0x2945`, `kMenuBg = 0x2104`, `kMenuBorder = kCursor`; константы раскладки `kStatusH = 24`, `kTabH = 24`, `kAreaY = 24`, `kAreaH = 272`, шрифт `fonts::AsciiFont8x16` (8×16).

**`touch.h`** — распознавание жестов поверх `lcd.getTouch`:

```cpp
namespace ui {
enum class TouchType : uint8_t { Tap, LongPress, Drag };
struct TouchEvent { TouchType type; int16_t x, y; int16_t dy; };  // dy for Drag, px since last Drag
class TouchTracker {
 public:
  // Call every loop; returns true and fills ev when a gesture event is ready.
  bool poll(LGFX& lcd, TouchEvent& ev);
 private:
  bool down_ = false, longSent_ = false, dragging_ = false;
  int16_t x0_ = 0, y0_ = 0, lastY_ = 0;
  uint32_t t0_ = 0;
};
}
```

Правила: касание началось → запомнить точку и время. Сдвиг по Y > 12 px от начала → режим Drag: события `Drag` с `dy` (накопленное смещение с прошлого Drag, отдавать, когда |dy| ≥ 16 — одна строка сетки). Удержание ≥ 500 мс без Drag → один `LongPress`. Отпускание без Drag и без LongPress → `Tap` с координатами начала.

**`screen.h`:**

```cpp
namespace ui {
class App;
class Screen {
 public:
  virtual ~Screen() = default;
  virtual void onEnter() {}
  virtual void onInput(const hw::InputEvent& ev) = 0;  // EncTurn/EncClick/EncLong (PlayPress и Shift обрабатывает App)
  virtual void onTouch(const TouchEvent& ev) = 0;     // y уже в координатах экрана (абсолютные)
  virtual void draw(LGFX_Sprite& s, int y0, int h) = 0;
  virtual bool wantsRedraw(const engine::Status& st) { return false; }  // например плейхед в GRID
};
}
```

**`menu.h`** — модальное меню (контекстное и для BANK):

```cpp
namespace ui {
struct MenuItem { const char* label; int id; bool enabled = true; };
class Menu {
 public:
  void open(const char* title, const MenuItem* items, int count, std::function<void(int id)> onChoose);
  bool isOpen() const { return open_; }
  void close() { open_ = false; }
  // Энкодер: поворот — выбор, клик — выбрать, долгое — закрыть. Тап по пункту — выбрать, тап вне — закрыть.
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s);  // по центру, ширина 240, строка 20 px, макс. 12 пунктов, рамка kMenuBorder
 private: ...
};
}
```

Пункты копируются внутрь меню (массив до 16 элементов), `onChoose` вызывается после закрытия.

**`App`:**

```cpp
namespace ui {
enum class Tab : uint8_t { Grid, Track, Bank, Proj, File, Count };
class App {
 public:
  void begin(LGFX* lcd, mt::Project* p);
  void onInput(const hw::InputEvent& ev);
  void tick();                  // опрос тача, статуса движка, перерисовка (не чаще 1/25 мс)
  void setTab(Tab t);
  Menu& menu() { return menu_; }
  mt::Project& project() { return *p_; }
  const engine::Status& status() const { return status_; }
  bool shift() const { return shift_; }
  void invalidate() { dirty_ = true; }
  uint8_t editPattern() const;  // паттерн, который редактируется = status().pattern
  // Undo и буфер обмена общие для экранов (выделяются в PSRAM в begin):
  mt::Undo& undo();
  mt::Clipboard& clipboard();
  void pushUndo();              // снимок текущего editPattern() перед правкой
  void doUndo();                // pop + запись в проект под lockProject
  void toast(const char* msg);  // сообщение в статус-баре на 1.5 с
 private: ...
};
}
```

Обработка в `App::onInput`: `PlayPress` → `StartStop` (play/stop с начала) / с Shift → `TogglePlay` (пауза/продолжение); `ShiftDown/Up` → флаг; остальное → в `menu_`, если открыто, иначе активному экрану.
Тач: таб-бар обрабатывает `App` (тап — переключить вкладку); статус-бар: тап по транспорту (x 296..359) = play/stop, Shift+тап = пауза/продолжение; тап по BPM (x 64..175) — переключает режим правки BPM энкодером (обрабатывается в `App`, пока режим включён — `EncTurn` меняет BPM, клик выключает); всё остальное — меню или активный экран.
Статус-бар: `P01 >P03  120 BPM  5/16  PLAY  L3`, `*` после номера паттерна не нужен (сохранения пока нет); при активном toast — текст toast вместо правой части.

**Проверка:** `pio run -e wt32` — SUCCESS. Поведение сетки то же, что на этапе 2, плюс таб-бар (вкладки кроме GRID пока пустые заглушки «TODO»).

---

### Task 6: GRID — Detail-вид, поля, scale lock, выделение, меню, undo, follow

**Files:**
- Modify: `src/ui/grid_screen.h`, `src/ui/grid_screen.cpp`

**Два вида** (переключение: Shift+клик энкодера, или пункт меню):
- **Overview**: как сейчас — номер шага 32 px + 8 колонок по 56 px; строка имён дорожек 16 px; 16 строк по 16 px.
- **Detail**: 2 дорожки по 224 px (номер шага 32 px слева). Поля в колонке дорожки (ширина 36 px каждое, отступ 4): `нота | vel | fx1 | v1 | fx2 | v2`. Пустая velocity показывается `...`, пустой fx — `...` и пустое значение. Пара видимых дорожек = `curTrack_ & ~1` и `+1`.

**Курсор:** `curStep_`, `curTrack_`, `curField_` (0 нота, 1 vel, 2 fx1 cmd, 3 fx1 val, 4 fx2 cmd, 5 fx2 val). В Overview поле всегда 0.

**Навигация (не в режиме правки):**
- поворот — шаг ±1 (циклично по длине);
- Shift+поворот — Overview: дорожка ±1; Detail: поле ±1 с переходом на соседнюю дорожку после v2 / до ноты;
- клик — режим правки поля; повторный клик — выход;
- Shift+клик — Overview ↔ Detail;
- долгое — контекстное меню; Shift+долгое — undo (`app.doUndo()`, toast `UNDO` или `NOTHING TO UNDO`).

**Правка (`edit_`), поворот:**
- нота: пустая → 60 (или последняя введённая нота этой дорожки); иначе `moveDegrees(note, delta, root, scale)`; Shift — ±12 полутонов; `OFF` → при повороте становится 60;
- vel: 0(«...») ↔ 1..127, шаг 1, Shift — ×10;
- fx cmd: `fxNextCmd`; при смене команды значение = `fxDefault`; `None` обнуляет значение;
- fx val: `fxStep(cmd, val, delta)`, Shift — ×10 (для CND — ×1).
- Первая правка в сеансе (вход в режим правки и первый поворот) делает `app.pushUndo()` — один снимок на сеанс правки, а не на каждый щелчок.

**Тач:**
- тап по ячейке — курсор туда (в Detail — и поле по x); повторный тап по той же ячейке — режим правки;
- Shift+тап — выделение: первый — якорь, второй — конец (подсветка `kSelBg`); тап без Shift сбрасывает выделение;
- долгий тап по ячейке — курсор туда + контекстное меню;
- тап по имени дорожки — mute, Shift+тап — solo (как сейчас);
- Drag по сетке — прокрутка на `dy / 16` строк (отключает follow до следующего Play);
- **мини-клавиатура**: в режиме правки ноты нижние 48 px рабочей области заменяются клавиатурой на октаву (12 клавиш по 40 px, белые/чёрные цветом, ноты лада подсвечены `kCursor`, октава = октава текущей ноты); тап по клавише — ввести эту ноту; сетка при этом показывает 13 строк. Кнопки `◀ OCT` / `OCT ▶` не нужны — октава меняется Shift+поворотом.

**Follow:** включён по умолчанию; когда идёт воспроизведение, нет правки и нет Drag — `top_` = страница с плейхедом (`(pos / rows) * rows`). Переключается пунктом меню. При ручной навигации курсором follow не выключается, но страница следует за плейхедом только пока курсор не трогали 2 с.

**Контекстное меню** (`app.menu().open(...)`):
- без выделения: `Copy step`, `Paste`, `Clear step`, `Copy track`, `Clear track`, `Transpose +1`, `Transpose -1`, `Transpose +12`, `Transpose -12`, `Detail/Overview`, `Follow on/off`, `Undo`;
- с выделением: `Copy sel`, `Paste`, `Clear sel`, четыре Transpose, `Undo`, `Clear selection`.
Transpose ±1 — ступени лада (`degrees = true`), ±12 — полутона. `Paste` неактивен при пустом буфере. Все изменяющие операции: `app.pushUndo()` → `engine::lockProject()` → операция из `edit_ops` → `unlockProject()`. «Track» = вся дорожка на длину паттерна.

**Удаление шага** долгим нажатием (этап 2) заменяется меню; `Clear step` в меню первый после Copy/Paste.

**Подсказка внизу** больше не рисуется (таб-бар); краткая подсказка — в статус-баре при входе в правку (toast с полем: `NOTE`, `VEL`, `FX1`, …).

**Проверка:** `pio run -e wt32` — SUCCESS.

---

### Task 7: Экраны TRACK, PROJ, BANK

**Files:**
- Create: `src/ui/param_list.h`, `src/ui/param_list.cpp`, `src/ui/track_screen.*`, `src/ui/proj_screen.*`, `src/ui/bank_screen.*`
- Modify: `src/ui/app.cpp` (подключить экраны)

**`ParamList`** — виджет списка параметров: строки по 24 px, слева имя, справа значение; курсор; поворот — выбор строки, клик — правка (значение красным), поворот в правке — `edit(delta)`, Shift — ×10; тап по строке — выбор, повторный тап — правка.

```cpp
struct Param {
  const char* label;
  std::function<void(char* out, int n)> format;
  std::function<void(int delta)> edit;  // вызывается уже под lockProject
};
```

**TRACK** (дорожка = текущая `curTrack_` из GRID; заголовок `TRACK 3`, Shift+поворот — дорожка ±1; тап по `◀ ▶` в заголовке тоже):
- Name — 8 символов, правка посимвольно: поле выбирается кликом, поворот меняет символ `A–Z 0–9 - _ пробел`, Shift+поворот — позиция курсора в имени;
- Channel 1–16; Def vel 1–127; Def gate 1–200 (как `GAT`, показ в %); CC A 0–127; CC B 0–127; Program `---`/0–127 (`kNoProgram` ниже 0); Mute; Solo.

**PROJ:**
- BPM (через `engine::post(SetBpm)`), Scale root C..B, Scale type (`scaleName`), Pattern length 4–128 (текущего паттерна), Resolution (`1/4 1/8 1/16 1/32 1/8T 1/16T`), Swing 50–75 %.
- Длина/разрешение/swing — к `status().pattern`. Изменение длины не трогает данные шагов за пределами (они сохраняются и вернутся при увеличении).

**BANK:** сетка 4×4 плиток (по 112×64 px с отступами) в рабочей области. На плитке: `P01`, длина, мини-индикатор непустых дорожек (8 точек). Цвета: играющий — `kPlayBg` с рамкой `kCursor`, в очереди — мигающая рамка (по чётности `millis()/250`), пустой — тусклый текст.
- тап — `QueuePattern` (если стоп — выбирается сразу, это делает секвенсор); Shift+тап — `SelectPattern`;
- долгий тап — меню: `Copy to...` (затем тап по плитке-приёмнику; копируется весь Pattern под lock, с `pushUndo` приёмника), `Clear` (с подтверждением вторым пунктом `Clear! (confirm)`), `Length 16/32/64` быстрые пресеты;
- энкодер: поворот — выбор плитки (рамка), клик — Queue, Shift+клик — Select, долгое — то же меню.

**Проверка:** `pio run -e wt32` — SUCCESS.

---

### Task 8: Проверка на железе

Прошить: `pio run -e wt32 -t upload`. Без кнопок управление тачем: Play — тап по `PLAY/STOP` в статус-баре.

Чек-лист:
- [ ] вкладки переключаются тапом;
- [ ] GRID: Overview/Detail, курсор и поля, правка ноты по ладу (PROJ: C Minor — шагает по минору), velocity, fx-команды и значения, мини-клавиатура;
- [ ] контекстное меню по долгому тапу: copy/paste/clear/transpose шага, дорожки, выделения; undo возвращает;
- [ ] Drag прокручивает длинный паттерн (PROJ: длина 64);
- [ ] follow ведёт страницу за плейхедом при 64 шагах;
- [ ] TRACK: канал/CC/имя меняются, имя видно в GRID;
- [ ] PROJ: смена разрешения/длины/swing на лету без сбоя;
- [ ] BANK: очередь паттернов, немедленное переключение, копирование, очистка;
- [ ] MIDI (когда будет железо): `CHD`, `RAT`, `CND`, `CCA`, `PBN`, `PGM` в MIDI Monitor.
