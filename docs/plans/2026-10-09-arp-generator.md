# FILL: генератор арпеджио (ARP) — план реализации

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** В Fill-диалоге режим ARP: арп в духе Access Virus записывается нотами (note/vel + GAT,
TIE, SLD, NDG, RAT, PRB, CHD) на дорожку Dest; ~40 factory-паттернов, свои паттерны через Capture.

**Architecture:** Чистое ядро `lib/core/src/arp_gen.{h,cpp}` (паттерн, текстовый формат, factory,
`applyArp`, `captureArp`) с native-тестами `test/test_arp`. Хранение user-паттернов — текстовые
файлы `/presets/ARP/NAME.arp` (`src/storage/arp_store.*`). UI — первая строка `Type: FILL / ARP`
в `FillDialog`, общий preview/OK/Cancel/undo. Дизайн: `docs/plans/2026-10-09-arp-generator-design.md`.

**Tech Stack:** C++17, PlatformIO (`native` для тестов, `wt32` прошивка), Unity, LovyanGFX.

Команды:
- тесты ядра: `pio test -e native -f test_arp` (все: `pio test -e native`)
- прошивка: `pio run -e wt32`

---

## Формат паттерна (справка для всех задач)

Токены через пробел/перевод строки, один токен = шаг (до 32):
- первый символ: `x` нота, `X` акцент, `o` ghost, `.` пауза, `-` продление (tie)
- модификаторы ноты (любой порядок): `s` short, `l` long, `~` slide, `r` repeat (та же высота),
  `p` root (нижняя нота аккорда), `^` +12, `v` −12 (`r^` = та же нота октавой выше)
- неизвестный первый символ — токен пропускается.

Запись в Dest: GHOST = Vel Lo, NORM = середина, ACC = Vel Hi; GAT: NORM = Gate %, SHORT = Gate/2
(не меньше 5), LONG = 100 %, всё × Rate; slide: SLD на ноте + GAT 100×Rate+10 % у предыдущей
ноты; tie: TIE на предыдущей ноте, OFF на первой паузе после; свинг: NDG = Swing/2 на нечётных
шагах арпа; Ghost PRB < 100 — PRB на ghost-нотах; Roll % — RAT 2..4.

---

### Task 1: Паттерн и текстовый формат

**Files:**
- Create: `lib/core/src/arp_gen.h`
- Create: `lib/core/src/arp_gen.cpp`
- Test: `test/test_arp/test_arp.cpp`

**Step 1: Failing test**

```cpp
#include <string.h>
#include <unity.h>
#include "arp_gen.h"
#include "model.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_parse_tokens() {
  ArpPattern p;
  TEST_ASSERT_TRUE(parseArpPattern("Xs~ xl o xr^ xpv . - ?z x", p));
  TEST_ASSERT_EQUAL(8, p.len);  // "?z" skipped
  TEST_ASSERT_EQUAL(ArpKind::Note, p.steps[0].kind);
  TEST_ASSERT_EQUAL(ArpAcc::Accent, p.steps[0].acc);
  TEST_ASSERT_EQUAL(ArpLen::Short, p.steps[0].len);
  TEST_ASSERT_TRUE(p.steps[0].slide);
  TEST_ASSERT_EQUAL(ArpLen::Long, p.steps[1].len);
  TEST_ASSERT_EQUAL(ArpAcc::Ghost, p.steps[2].acc);
  TEST_ASSERT_EQUAL(ArpPitch::Repeat, p.steps[3].pitch);
  TEST_ASSERT_EQUAL(1, p.steps[3].oct);
  TEST_ASSERT_EQUAL(ArpPitch::Root, p.steps[4].pitch);
  TEST_ASSERT_EQUAL(-1, p.steps[4].oct);
  TEST_ASSERT_EQUAL(ArpKind::Rest, p.steps[5].kind);
  TEST_ASSERT_EQUAL(ArpKind::Tie, p.steps[6].kind);
  TEST_ASSERT_FALSE(parseArpPattern("  ", p));
}

void test_format_round_trip() {
  ArpPattern p, q;
  TEST_ASSERT_TRUE(parseArpPattern("Xs~ xl o xr^ xpv . -", p));
  char buf[256];
  TEST_ASSERT_TRUE(formatArpPattern(p, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("Xs~ xl o xr^ xpv . -", buf);
  TEST_ASSERT_TRUE(parseArpPattern(buf, q));
  TEST_ASSERT_EQUAL(0, memcmp(&p, &q, sizeof(p)));
  TEST_ASSERT_FALSE(formatArpPattern(p, buf, 4));
}

void test_parse_caps_at_32() {
  char text[200] = "";
  for (int i = 0; i < 40; ++i) strcat(text, "x ");
  ArpPattern p;
  TEST_ASSERT_TRUE(parseArpPattern(text, p));
  TEST_ASSERT_EQUAL(kArpPatMax, p.len);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_tokens);
  RUN_TEST(test_format_round_trip);
  RUN_TEST(test_parse_caps_at_32);
  return UNITY_END();
}
```

`memcmp` по `ArpPattern` требует, чтобы `ArpStep` не имел мусора в паддинге и неиспользуемые шаги
были одинаковы: `parseArpPattern` сначала делает `out = ArpPattern{}`.

**Step 2:** `pio test -e native -f test_arp` — FAIL (нет `arp_gen.h`).

**Step 3: Implementation**

`lib/core/src/arp_gen.h`:

```cpp
#pragma once
#include <stdint.h>
#include "edit_ops.h"
#include "model.h"
#include "scale.h"

namespace mt {

// Arp generator of FILL: a rhythm pattern (factory or user) run over held notes, written as notes
// and fx on one track. Text form of a pattern, one token per step: 'x' note, 'X' accent, 'o' ghost,
// '.' rest, '-' tie (the previous note holds); after a note: 's' short, 'l' long, '~' slide,
// 'r' repeat the previous note, 'p' the lowest held note, '^' +12, 'v' -12.
constexpr int kArpPatMax = 32;

enum class ArpKind : uint8_t { Rest, Note, Tie };
enum class ArpAcc : uint8_t { Ghost, Norm, Accent };
enum class ArpLen : uint8_t { Short, Norm, Long };
enum class ArpPitch : uint8_t { Next, Repeat, Root };

struct ArpStep {
  ArpKind kind = ArpKind::Rest;
  ArpAcc acc = ArpAcc::Norm;
  ArpLen len = ArpLen::Norm;
  ArpPitch pitch = ArpPitch::Next;
  int8_t oct = 0;  // -1, 0, +1
  bool slide = false;
};

struct ArpPattern {
  uint8_t len = 0;
  ArpStep steps[kArpPatMax];
};

// False when the text has no step. Steps past kArpPatMax are ignored.
bool parseArpPattern(const char* text, ArpPattern& out);
// Canonical text (tokens joined by ' '). False when cap is too small.
bool formatArpPattern(const ArpPattern& p, char* out, int cap);

}  // namespace mt
```

`lib/core/src/arp_gen.cpp`:

```cpp
#include "arp_gen.h"

namespace mt {
namespace {

bool blank(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ','; }

}  // namespace

bool parseArpPattern(const char* text, ArpPattern& out) {
  out = ArpPattern{};
  const char* p = text;
  while (*p && out.len < kArpPatMax) {
    while (*p && blank(*p)) ++p;
    if (!*p) break;
    ArpStep a;
    const char c = *p++;
    switch (c) {
      case 'x': a.kind = ArpKind::Note; break;
      case 'X': a.kind = ArpKind::Note; a.acc = ArpAcc::Accent; break;
      case 'o': a.kind = ArpKind::Note; a.acc = ArpAcc::Ghost; break;
      case '.': break;
      case '-': a.kind = ArpKind::Tie; break;
      default:
        while (*p && !blank(*p)) ++p;
        continue;
    }
    for (; *p && !blank(*p); ++p) {
      switch (*p) {
        case 's': a.len = ArpLen::Short; break;
        case 'l': a.len = ArpLen::Long; break;
        case '~': a.slide = true; break;
        case 'r': a.pitch = ArpPitch::Repeat; break;
        case 'p': a.pitch = ArpPitch::Root; break;
        case '^': a.oct = 1; break;
        case 'v': a.oct = -1; break;
        default: break;
      }
    }
    out.steps[out.len++] = a;
  }
  return out.len > 0;
}

bool formatArpPattern(const ArpPattern& p, char* out, int cap) {
  int n = 0;
  auto put = [&](char c) {
    if (n + 1 >= cap) return false;
    out[n++] = c;
    return true;
  };
  for (int i = 0; i < p.len && i < kArpPatMax; ++i) {
    const ArpStep& a = p.steps[i];
    if (i > 0 && !put(' ')) return false;
    if (a.kind == ArpKind::Rest) {
      if (!put('.')) return false;
      continue;
    }
    if (a.kind == ArpKind::Tie) {
      if (!put('-')) return false;
      continue;
    }
    if (!put(a.acc == ArpAcc::Accent ? 'X' : (a.acc == ArpAcc::Ghost ? 'o' : 'x'))) return false;
    if (a.len == ArpLen::Short && !put('s')) return false;
    if (a.len == ArpLen::Long && !put('l')) return false;
    if (a.slide && !put('~')) return false;
    if (a.pitch == ArpPitch::Repeat && !put('r')) return false;
    if (a.pitch == ArpPitch::Root && !put('p')) return false;
    if (a.oct > 0 && !put('^')) return false;
    if (a.oct < 0 && !put('v')) return false;
  }
  if (cap < 1) return false;
  out[n] = '\0';
  return true;
}

}  // namespace mt
```

Порядок модификаторов при форматировании: длина, slide, высота, октава — тест ждёт `Xs~`,
`xr^`, `xpv`.

**Step 4:** `pio test -e native -f test_arp` — PASS.

**Step 5:**
```bash
git add lib/core/src/arp_gen.h lib/core/src/arp_gen.cpp test/test_arp/test_arp.cpp
git commit -m "feat(arp): arp pattern model and text format"
```

---

### Task 2: Factory-паттерны

**Files:** Modify `lib/core/src/arp_gen.{h,cpp}`, `test/test_arp/test_arp.cpp`

**Step 1: Failing test** (добавить и в `main`)

```cpp
void test_factory_patterns_parse() {
  TEST_ASSERT_TRUE(arpFactoryCount() >= 40);
  for (int i = 0; i < arpFactoryCount(); ++i) {
    ArpPattern p;
    TEST_ASSERT_TRUE_MESSAGE(parseArpPattern(arpFactoryText(i), p), arpFactoryName(i));
    TEST_ASSERT_TRUE(strlen(arpFactoryName(i)) <= 10);
    bool note = false;
    for (int k = 0; k < p.len; ++k) note |= p.steps[k].kind == ArpKind::Note;
    TEST_ASSERT_TRUE_MESSAGE(note, arpFactoryName(i));
    char back[256];
    TEST_ASSERT_TRUE(formatArpPattern(p, back, sizeof(back)));
  }
  TEST_ASSERT_EQUAL_STRING("16THS", arpFactoryName(0));
  TEST_ASSERT_NULL(arpFactoryName(arpFactoryCount()));
}
```

**Step 2:** FAIL (нет функций).

**Step 3:** в `arp_gen.h`:

```cpp
// Built-in patterns, grouped by style (BASIC, TR, PSY, TE, HO, DNB, ACID, EL, BR, SW, DUB, CHIP).
int arpFactoryCount();
const char* arpFactoryName(int i);  // up to 10 chars; nullptr out of range
const char* arpFactoryText(int i);  // nullptr out of range
```

В `arp_gen.cpp` (в анонимном namespace) таблица и функции:

```cpp
struct Factory {
  const char* name;
  const char* text;
};
constexpr Factory kFactory[] = {
    {"16THS", "x x x x x x x x x x x x x x x x"},
    {"8THS", "x . x . x . x . x . x . x . x ."},
    {"TRIPLET", "X x x X x x X x x X x x"},
    {"DOTTED", "x . . x . . x . . x . . x . x ."},
    {"QUARTERS", "xl . . . xl . . . xl . . . xl . . ."},
    {"OFFBEAT", ". . x . . . x . . . x . . . x ."},
    {"ACCENT 4", "X x x x X x x x X x x x X x x x"},
    {"TR GATE", "X xs xs x X xs xs x X xs xs x X xs x xs"},
    {"TR OFFBT", ". . Xl . . . Xl . . . Xl . . . Xl ."},
    {"TR ROLL", "X o x o X o x o X o x o X o x o"},
    {"TR PEDAL", "Xp x xp x Xp x xp x Xp x xp x Xp x xp x"},
    {"TR UPLIFT", "x x x x x^ x^ x^ x^ X X X X X^ X^ X^ X^"},
    {"TR 332", "X - x X - x X - X - x X - x X -"},
    {"TR CHUG", "X o x o x o X o x o x o X o x x"},
    {"PSY GALOP", ". Xp x xp . Xp xp x . Xp x xp . Xp xp x"},
    {"PSY TRIPL", ". Xp xp . Xp x . Xp xp . Xp x"},
    {"TE STAB", "Xs . . Xs . . Xs . . . Xs . . Xs . ."},
    {"TE HYPNO3", "X x . x x . X x . x x ."},
    {"TE MINIMAL", "x . . o . . x . . . o . x . . ."},
    {"TE RUMBLE", ". o o o . o o o . o o o . o o o"},
    {"TE ROLLER", "xp . xr xr xp . xr . xp . xr xr xp . x ."},
    {"HO OFFBT", ". . Xs . . . Xs . . . Xs . . . Xs ."},
    {"HO ORGAN", "Xl . . xl . . Xl . . xl . . Xl . xl ."},
    {"HO PIANO", "X . x . . x . x X . x . . x . ."},
    {"HO SHUFFLE", "x . o x . o x . o x . o x . o x"},
    {"DNB ROLL", "X o x o o x o x X o x o o x o x"},
    {"DNB STAB", "Xs . . . . . Xs . . . Xs . . . . ."},
    {"DNB AMEN", "X . x . . x . x . x X . . x . ."},
    {"DNB REESE", "Xl - - - . . xl - Xl - - . . . x ."},
    {"DNB 2STEP", "X . . x . . . . . . X . . x . ."},
    {"ACID 1", "X x~ x . X x~ X x x . X~ x . x X~ x"},
    {"ACID 2", "x^ x X~ x . x x^~ x X . x~ x x X x^ ."},
    {"ACID 3", "Xr x~ xr . X~ xv x . Xr x xr~ x . X x~ x"},
    {"EL FUNK", "X . . x . . X . . x . x X . x ."},
    {"EL ROBOT", "Xs . xs . Xs xs . xs Xs . xs . Xs xs xs ."},
    {"BR BREAK", "X . x . . x X . . x . x X . . x"},
    {"SW 8THS", "X . x^ . x . x^ . X . x^ . x . x^ ."},
    {"SW DRIVE", "X x x x X x x x X x x x X x x^ x^"},
    {"DUB STAB", "Xs . . . . . o . . . . . Xs . o ."},
    {"DUB ECHO", ". . Xs . . o . o . . Xs . . o . o"},
    {"CHIP OCT", "x xr^ x xr^ x xr^ x xr^ x xr^ x xr^ x xr^ x xr^"},
    {"CHIP RUN", "xs xs xs xs xs xs xs xs xs xs xs xs xs xs xs xs"},
};
constexpr int kFactoryN = sizeof(kFactory) / sizeof(kFactory[0]);
```

```cpp
int arpFactoryCount() { return kFactoryN; }
const char* arpFactoryName(int i) { return i >= 0 && i < kFactoryN ? kFactory[i].name : nullptr; }
const char* arpFactoryText(int i) { return i >= 0 && i < kFactoryN ? kFactory[i].text : nullptr; }
```

Перед коммитом пересчитать токены: каждая строка 16 шагов, кроме TRIPLET / TE HYPNO3 / PSY TRIPL
(12). Тест проверяет только парсинг и наличие нот, длину — глазами.

**Step 4:** PASS. **Step 5:** `git commit -m "feat(arp): factory arp patterns"`.

---

### Task 3: Порядок нот (режимы, октавы) — `applyArp`, CHORD source

**Files:** Modify `lib/core/src/arp_gen.{h,cpp}`, `test/test_arp/test_arp.cpp`

**Step 1: Failing tests**

```cpp
static Pattern S, D;

static void reset() {
  S.clear();
  S.length = 16;
  D = S;
}

static ArpPattern pat(const char* t) {
  ArpPattern p;
  parseArpPattern(t, p);
  return p;
}

// Notes of dest track 0 on the first n steps into out (kNoteEmpty kept).
static void notes(int n, int* out) {
  for (int i = 0; i < n; ++i) out[i] = D.steps[0][i].note;
}

static void expectNotes(ArpMode m, uint8_t chord, int oct, const int* want, int n) {
  reset();
  ArpSpec a;
  a.mode = m;
  a.chord = chord;
  a.octaves = static_cast<uint8_t>(oct);
  TEST_ASSERT_TRUE(applyArp(S, D, makeSel(0, 0, 0, 15), a, pat("x x x x x x x x x x x x x x x x"), 0,
                            ScaleType::Major));
  int got[16];
  notes(n, got);
  TEST_ASSERT_EQUAL_INT_ARRAY(want, got, n);
}

void test_modes_triad() {
  const int up[] = {60, 64, 67, 60};
  expectNotes(ArpMode::Up, kChordTriad, 1, up, 4);
  const int down[] = {67, 64, 60, 67};
  expectNotes(ArpMode::Down, kChordTriad, 1, down, 4);
  const int ud[] = {60, 64, 67, 64, 60, 64};
  expectNotes(ArpMode::UpDown, kChordTriad, 1, ud, 6);
  const int du[] = {67, 64, 60, 64, 67, 64};
  expectNotes(ArpMode::DownUp, kChordTriad, 1, du, 6);
  const int ped[] = {60, 64, 60, 67, 60, 64};
  expectNotes(ArpMode::Pedal, kChordTriad, 1, ped, 6);
  const int oct2[] = {60, 64, 67, 72, 76, 79, 60};
  expectNotes(ArpMode::Up, kChordTriad, 2, oct2, 7);
}

void test_modes_seventh() {
  const int conv[] = {60, 71, 64, 67, 60};
  expectNotes(ArpMode::Converge, kChordSeventh, 1, conv, 5);
  const int div[] = {67, 64, 71, 60, 67};
  expectNotes(ArpMode::Diverge, kChordSeventh, 1, div, 5);
}

void test_chord_mode_writes_chd() {
  reset();
  ArpSpec a;
  a.mode = ArpMode::Chord;
  a.chord = kChordSeventh;
  applyArp(S, D, makeSel(0, 0, 0, 15), a, pat("x x"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(60, D.steps[0][0].note);
  TEST_ASSERT_EQUAL(60, D.steps[0][1].note);
  const FxSlot* c = D.steps[0][0].find(Fx::CHD);
  TEST_ASSERT_NOT_NULL(c);
  TEST_ASSERT_EQUAL(kChordSeventh, c->val);
}

void test_random_mode_deterministic() {
  reset();
  ArpSpec a;
  a.mode = ArpMode::Random;
  a.seed = 5;
  const ArpPattern p = pat("x x x x x x x x x x x x x x x x");
  applyArp(S, D, makeSel(0, 0, 0, 15), a, p, 0, ScaleType::Major);
  Pattern first = D;
  reset();
  applyArp(S, D, makeSel(0, 0, 0, 15), a, p, 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(0, memcmp(first.steps[0], D.steps[0], sizeof(D.steps[0])));
  for (int i = 0; i < 16; ++i) {
    const int n = D.steps[0][i].note;
    TEST_ASSERT_TRUE(n == 60 || n == 64 || n == 67);
  }
}
```

(`ArpSpec` по умолчанию: source CHORD, root 60, chord triad, dest 0, rate 1, octaves 1.
Major root 0: триада C = 60 64 67, септ = 60 64 67 71.)

**Step 2:** FAIL.

**Step 3: Implementation**

`arp_gen.h` — добавить:

```cpp
enum class ArpSource : uint8_t { Chord, Selection, Count };
enum class ArpMode : uint8_t { Up, Down, UpDown, DownUp, Played, Random, Converge, Diverge, Pedal, Chord, Count };
const char* arpModeName(ArpMode m);  // "UP", "UP/DN", ...

struct ArpSpec {
  ArpSource source = ArpSource::Chord;
  uint8_t root = 60;             // CHORD: the chord's note
  uint8_t chord = kChordTriad;   // CHORD: CHD value, built in the project's scale
  uint8_t dest = 0;              // track written
  ArpMode mode = ArpMode::Up;
  uint8_t octaves = 1;           // 1..4
  uint8_t pattern = 0;           // UI: factory index, then user patterns
  uint8_t rate = 1;              // 1..4 track steps per arp step
  uint8_t rotate = 0;            // pattern start offset
  uint8_t gate = 50;             // NORM gate %; SHORT half, LONG 100 (x rate)
  uint8_t swing = 0;             // 0..100: NDG swing / 2 on the odd arp steps
  uint8_t velLo = 60, velHi = 120;  // ghost, accent; normal = the middle
  uint8_t slide = 16;            // SLD value of slide steps
  uint8_t roll = 0;              // % of notes with RAT 2..4
  uint8_t ghostPrb = 100;        // PRB of ghost notes, 100 = none written
  uint8_t mutate = 0;            // % of pattern changes (seeded)
  uint32_t seed = 1;
};

// Writes the arp into track spec.dest of dst over steps sel.s0..s1 (clamped to the pattern length):
// note, velocity and the arp's fx (GAT TIE SLD NDG RAT PRB CHD ARS ARP) of the range are replaced,
// other fx stay. Held notes: the chord (CHORD) or, per step, the notes of src tracks sel.t0..t1 with
// their CHD (SELECTION; a step with notes replaces the held chord, OFF alone releases it). src may
// be dst's pre-fill copy. drumTracks[t]: never a source; a drum dest writes nothing (false).
bool applyArp(const Pattern& src, Pattern& dst, const Sel& sel, const ArpSpec& spec, const ArpPattern& pat,
              uint8_t root, ScaleType scale, const bool* drumTracks = nullptr);

// GAT value of a gate percentage (see gatePercent): 1..100 exact, above in 7 % units, max 800 %.
uint8_t gateValue(int pct);
```

`arp_gen.cpp` — добавить `#include <string.h>`, `#include "rng.h"`, в анонимный namespace:

```cpp
const char* const kModeNames[] = {"UP", "DOWN", "UP/DN", "DN/UP", "PLAYED", "RANDOM", "CONVERGE",
                                  "DIVERGE", "PEDAL", "CHORD"};
static_assert(sizeof(kModeNames) / sizeof(kModeNames[0]) == static_cast<size_t>(ArpMode::Count), "mode names");

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

constexpr int kHeldMax = 16;
constexpr int kSeqMax = kHeldMax * 4;

struct Held {
  uint8_t n = 0;
  uint8_t notes[kHeldMax];  // played order, no duplicates
};

void addNote(Held& h, uint8_t note) {
  for (int i = 0; i < h.n; ++i)
    if (h.notes[i] == note) return;
  if (h.n < kHeldMax) h.notes[h.n++] = note;
}

// One octave after another; ascending within an octave, PLAYED keeps the held order.
int buildSeq(const Held& h, ArpMode mode, int octaves, uint8_t* seq) {
  uint8_t base[kHeldMax];
  memcpy(base, h.notes, h.n);
  if (mode != ArpMode::Played)
    for (int i = 1; i < h.n; ++i)
      for (int j = i; j > 0 && base[j - 1] > base[j]; --j) {
        const uint8_t t = base[j];
        base[j] = base[j - 1];
        base[j - 1] = t;
      }
  int n = 0;
  for (int o = 0; o < octaves; ++o)
    for (int i = 0; i < h.n; ++i)
      if (base[i] + 12 * o <= 127) seq[n++] = static_cast<uint8_t>(base[i] + 12 * o);
  return n;
}

// Index into seq of n notes at cycle position k (Random is drawn by the caller).
int orderAt(ArpMode m, int n, int k) {
  if (n <= 1) return 0;
  switch (m) {
    case ArpMode::Down: return n - 1 - k % n;
    case ArpMode::UpDown: {
      const int len = 2 * n - 2, i = k % len;
      return i < n ? i : len - i;
    }
    case ArpMode::DownUp: {
      const int len = 2 * n - 2, i = k % len;
      return i < n ? n - 1 - i : i - (n - 1);
    }
    case ArpMode::Converge: {
      const int i = k % n;
      return i % 2 == 0 ? i / 2 : n - 1 - i / 2;
    }
    case ArpMode::Diverge: {
      const int i = n - 1 - k % n;
      return i % 2 == 0 ? i / 2 : n - 1 - i / 2;
    }
    case ArpMode::Pedal: {
      const int len = 2 * (n - 1), i = k % len;
      return i % 2 == 0 ? 0 : 1 + i / 2;
    }
    default: return k % n;
  }
}

bool arpFx(Fx f) {
  switch (f) {
    case Fx::GAT: case Fx::TIE: case Fx::SLD: case Fx::NDG: case Fx::RAT: case Fx::PRB:
    case Fx::CHD: case Fx::ARS: case Fx::ARP: return true;
    default: return false;
  }
}

// The slot already holding f, else the first free one. False: no slot (fx skipped).
bool putFx(Step& s, Fx f, uint8_t v) {
  for (FxSlot& sl : s.fx)
    if (sl.cmd == f) {
      sl.val = v;
      return true;
    }
  for (FxSlot& sl : s.fx)
    if (sl.cmd == Fx::None) {
      sl.cmd = f;
      sl.val = v;
      return true;
    }
  return false;
}
```

Публичные:

```cpp
const char* arpModeName(ArpMode m) {
  const int i = static_cast<int>(m);
  return i >= 0 && i < static_cast<int>(ArpMode::Count) ? kModeNames[i] : "";
}

uint8_t gateValue(int pct) {
  if (pct < 1) pct = 1;
  if (pct <= 100) return static_cast<uint8_t>(pct);
  const int v = 100 + (pct - 100 + 6) / 7;
  return static_cast<uint8_t>(v > 200 ? 200 : v);
}

bool applyArp(const Pattern& src, Pattern& dst, const Sel& sel, const ArpSpec& spec, const ArpPattern& pat,
              uint8_t root, ScaleType scale, const bool* drumTracks) {
  if (pat.len == 0 || spec.dest >= kTracks) return false;
  if (drumTracks && drumTracks[spec.dest]) return false;
  const int plen = clampInt(dst.length, 1, kMaxSteps);
  const int s0 = clampInt(sel.s0, 0, plen - 1), s1 = clampInt(sel.s1, s0, plen - 1);
  Step* out = dst.steps[spec.dest];
  for (int s = s0; s <= s1; ++s) {
    Step& st = out[s];
    st.note = kNoteEmpty;
    st.vel = kVelDefault;
    for (FxSlot& sl : st.fx)
      if (arpFx(sl.cmd)) sl = FxSlot{};
  }

  const int octaves = clampInt(spec.octaves, 1, 4), rate = clampInt(spec.rate, 1, 4);
  Held held;
  if (spec.source == ArpSource::Chord) {
    uint8_t c[4];
    const int n = chordNotes(spec.root, spec.chord, root, scale, c);
    for (int i = 0; i < n; ++i) addNote(held, c[i]);
  }
  uint8_t seq[kSeqMax];
  int seqN = buildSeq(held, spec.mode, octaves, seq);
  const uint8_t chd = spec.source == ArpSource::Chord ? spec.chord : kChordTriad;

  Rng rng(spec.seed ^ 0xA5A5F00Du);
  int k = 0;           // cycle position
  int prevNote = -1;   // pitch of the last note written
  int prevStep = -1;   // its step, -1 after a rest
  bool tied = false;   // prevStep carries TIE
  for (int j = 0;; ++j) {
    const int s = s0 + j * rate;
    if (s > s1) break;
    ArpStep a = pat.steps[(j + spec.rotate) % pat.len];
    Step& st = out[s];
    if (a.kind == ArpKind::Tie) {
      if (prevStep >= 0 && !tied) tied = putFx(out[prevStep], Fx::TIE, 0);
      continue;
    }
    if (a.kind == ArpKind::Rest || seqN == 0) {
      if (tied) st.note = kNoteOff;
      tied = false;
      prevStep = -1;
      continue;
    }
    int note;
    if (spec.mode == ArpMode::Chord || a.pitch == ArpPitch::Root || (a.pitch == ArpPitch::Repeat && prevNote < 0)) {
      if (spec.mode != ArpMode::Chord && a.pitch != ArpPitch::Root) {
        note = seq[orderAt(spec.mode, seqN, k++)];
      } else {
        note = seq[0];
        for (int i = 1; i < seqN; ++i)
          if (seq[i] < note) note = seq[i];
      }
    } else if (a.pitch == ArpPitch::Repeat) {
      note = prevNote;
    } else {
      const int idx = spec.mode == ArpMode::Random ? static_cast<int>(rng.below(static_cast<uint32_t>(seqN)))
                                                   : orderAt(spec.mode, seqN, k);
      ++k;
      note = seq[idx];
    }
    note = clampInt(note + 12 * a.oct, 0, 127);
    st.note = static_cast<uint8_t>(note);
    const int lo = clampInt(spec.velLo, 1, 127), hi = clampInt(spec.velHi, 1, 127);
    st.vel = static_cast<uint8_t>(a.acc == ArpAcc::Ghost ? lo : (a.acc == ArpAcc::Accent ? hi : (lo + hi) / 2));
    tied = false;
    const int g = clampInt(spec.gate, 5, 100);
    const int pct = (a.len == ArpLen::Short ? (g / 2 < 5 ? 5 : g / 2) : (a.len == ArpLen::Long ? 100 : g)) * rate;
    putFx(st, Fx::GAT, gateValue(pct));
    if (a.slide && prevStep >= 0) {
      putFx(out[prevStep], Fx::GAT, gateValue(100 * rate + 10));
      putFx(st, Fx::SLD, spec.slide ? spec.slide : 1);
    }
    if (spec.mode == ArpMode::Chord) putFx(st, Fx::CHD, chd);
    prevNote = note;
    prevStep = s;
  }
  return true;
}
```

Логика SELECTION, свинга, ghost/roll и mutate — следующие задачи (пока не используются).

**Step 4:** PASS. **Step 5:** `git commit -m "feat(arp): applyArp with chord source and note orders"`.

---

### Task 4: Ритм — rest, tie, slide, gate, rate, velocity, сохранение чужих fx, drum dest

**Files:** `test/test_arp/test_arp.cpp` (код уже написан в Task 3 — тесты фиксируют поведение;
падения чинить в `arp_gen.cpp`)

**Step 1: Tests**

```cpp
static ArpSpec spec() { return ArpSpec{}; }

void test_tie_then_rest_writes_off() {
  reset();
  applyArp(S, D, makeSel(0, 0, 0, 15), spec(), pat("x - - . x"), 0, ScaleType::Major);
  TEST_ASSERT_NOT_NULL(D.steps[0][0].find(Fx::TIE));
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[0][1].note);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[0][2].note);
  TEST_ASSERT_EQUAL(kNoteOff, D.steps[0][3].note);
  TEST_ASSERT_EQUAL(64, D.steps[0][4].note);
}

void test_tie_after_rest_is_silent() {
  reset();
  applyArp(S, D, makeSel(0, 0, 0, 15), spec(), pat(". - x"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[0][0].note);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[0][1].note);
  TEST_ASSERT_EQUAL(60, D.steps[0][2].note);
}

void test_slide_and_gates() {
  reset();
  ArpSpec a = spec();
  a.slide = 20;
  applyArp(S, D, makeSel(0, 0, 0, 15), a, pat("x xs xl x~"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(50, D.steps[0][0].find(Fx::GAT)->val);
  TEST_ASSERT_EQUAL(25, D.steps[0][1].find(Fx::GAT)->val);
  TEST_ASSERT_EQUAL(gateValue(110), D.steps[0][2].find(Fx::GAT)->val);  // LONG raised by the slide
  TEST_ASSERT_EQUAL(20, D.steps[0][3].find(Fx::SLD)->val);
  TEST_ASSERT_NULL(D.steps[0][2].find(Fx::SLD));
}

void test_rate_spreads_and_scales_gate() {
  reset();
  ArpSpec a = spec();
  a.rate = 2;
  applyArp(S, D, makeSel(0, 0, 0, 15), a, pat("x x"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(60, D.steps[0][0].note);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[0][1].note);
  TEST_ASSERT_EQUAL(64, D.steps[0][2].note);
  TEST_ASSERT_EQUAL(100, D.steps[0][0].find(Fx::GAT)->val);
  TEST_ASSERT_EQUAL(60, D.steps[0][4].note);  // the pattern repeats
}

void test_velocity_levels() {
  reset();
  ArpSpec a = spec();
  a.velLo = 40;
  a.velHi = 120;
  applyArp(S, D, makeSel(0, 0, 0, 15), a, pat("o x X"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(40, D.steps[0][0].vel);
  TEST_ASSERT_EQUAL(80, D.steps[0][1].vel);
  TEST_ASSERT_EQUAL(120, D.steps[0][2].vel);
}

void test_keeps_other_fx_replaces_own() {
  reset();
  D.steps[0][0].fx[0] = FxSlot{Fx::FLT, 33};
  D.steps[0][0].fx[1] = FxSlot{Fx::RAT, 4};
  D.steps[0][1].note = 50;  // a rest step of the arp loses its old note
  applyArp(S, D, makeSel(0, 0, 0, 15), spec(), pat("x ."), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(33, D.steps[0][0].find(Fx::FLT)->val);
  TEST_ASSERT_NULL(D.steps[0][0].find(Fx::RAT));
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[0][1].note);
}

void test_range_and_pattern_length() {
  reset();
  D.length = 8;
  applyArp(S, D, makeSel(0, 2, 0, 15), spec(), pat("x"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[0][1].note);
  TEST_ASSERT_EQUAL(60, D.steps[0][2].note);
  TEST_ASSERT_EQUAL(67, D.steps[0][4].note);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[0][8].note);  // past the pattern length
}

void test_drum_dest_untouched() {
  reset();
  bool drum[kTracks] = {};
  drum[0] = true;
  D.steps[0][0].note = 0;
  D.steps[0][0].vel = 1;
  TEST_ASSERT_FALSE(applyArp(S, D, makeSel(0, 0, 0, 15), spec(), pat("x"), 0, ScaleType::Major, drum));
  TEST_ASSERT_EQUAL(0, D.steps[0][0].note);
  TEST_ASSERT_EQUAL(1, D.steps[0][0].vel);
}
```

**Step 2:** `pio test -e native -f test_arp` — ожидаемо PASS (поведение реализовано в Task 3);
падение — баг в `applyArp`, чинить, не тест (если тест противоречит дизайну — сверить с
дизайн-доком, разделы 2 и 5).

**Step 3:** `git commit -m "test(arp): rhythm, tie, slide, gate and range behaviour"`.

---

### Task 5: SELECTION source, Dest внутри выделения

**Files:** `lib/core/src/arp_gen.cpp`, `test/test_arp/test_arp.cpp`

**Step 1: Failing tests**

```cpp
void test_selection_played_order_into_dest() {
  reset();
  S.steps[0][0].note = 67;
  S.steps[1][0].note = 60;
  S.steps[2][0].note = 64;
  D = S;
  ArpSpec a = spec();
  a.source = ArpSource::Selection;
  a.mode = ArpMode::Played;
  a.dest = 3;
  applyArp(S, D, makeSel(0, 0, 2, 15), a, pat("x x x x"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(67, D.steps[3][0].note);
  TEST_ASSERT_EQUAL(60, D.steps[3][1].note);
  TEST_ASSERT_EQUAL(64, D.steps[3][2].note);
  TEST_ASSERT_EQUAL(67, D.steps[3][3].note);
  TEST_ASSERT_EQUAL(67, D.steps[0][0].note);  // sources untouched
}

void test_selection_chord_change_and_off() {
  reset();
  S.steps[0][0].note = 60;
  S.steps[0][0].fx[0] = FxSlot{Fx::CHD, kChordTriad};  // C E G
  S.steps[0][8].note = 65;
  S.steps[0][8].fx[0] = FxSlot{Fx::CHD, kChordTriad};  // F A C
  S.steps[0][12].note = kNoteOff;
  D = S;
  ArpSpec a = spec();
  a.source = ArpSource::Selection;
  a.dest = 1;
  applyArp(S, D, makeSel(0, 0, 0, 15), a, pat("x x x x x x x x x x x x x x x x"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(60, D.steps[1][0].note);
  TEST_ASSERT_EQUAL(67, D.steps[1][2].note);
  // step 8: cycle position 8 over F A C (65 69 72) -> 8 % 3 = 2 -> 72
  TEST_ASSERT_EQUAL(72, D.steps[1][8].note);
  TEST_ASSERT_EQUAL(65, D.steps[1][9].note);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[1][12].note);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[1][15].note);
}

void test_selection_dest_inside_reads_snapshot() {
  reset();
  S.steps[0][0].note = 60;
  S.steps[1][0].note = 64;
  D = S;
  ArpSpec a = spec();
  a.source = ArpSource::Selection;
  a.dest = 0;
  applyArp(S, D, makeSel(0, 0, 1, 15), a, pat("x x x x"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(60, D.steps[0][0].note);
  TEST_ASSERT_EQUAL(64, D.steps[0][1].note);
  TEST_ASSERT_EQUAL(60, D.steps[0][2].note);
  TEST_ASSERT_EQUAL(64, D.steps[1][0].note);  // other source track untouched
}

void test_selection_skips_drum_sources() {
  reset();
  S.steps[0][0].note = 0;  // drum velocity field, not a note
  S.steps[0][0].vel = 1;
  S.steps[1][0].note = 62;
  D = S;
  bool drum[kTracks] = {};
  drum[0] = true;
  ArpSpec a = spec();
  a.source = ArpSource::Selection;
  a.dest = 2;
  applyArp(S, D, makeSel(0, 0, 1, 15), a, pat("x x"), 0, ScaleType::Major, drum);
  TEST_ASSERT_EQUAL(62, D.steps[2][0].note);
  TEST_ASSERT_EQUAL(62, D.steps[2][1].note);
}
```

**Step 2:** FAIL (SELECTION не читается — арп пустой).

**Step 3:** в `applyArp` после `const uint8_t chd = ...` добавить `int nextSrc = s0;`, а в цикле
сразу после `if (s > s1) break;`:

```cpp
    if (spec.source == ArpSource::Selection) {
      // Chord changes up to s: the latest step with notes wins, an OFF alone releases.
      for (; nextSrc <= s; ++nextSrc) {
        Held h;
        bool off = false;
        for (int t = sel.t0; t <= sel.t1 && t < kTracks; ++t) {
          if (drumTracks && drumTracks[t]) continue;
          const Step& ss = src.steps[t][nextSrc];
          if (ss.hasNote()) {
            uint8_t cn[4];
            const FxSlot* c = ss.find(Fx::CHD);
            const int n = c ? chordNotes(ss.note, c->val, root, scale, cn) : 1;
            if (!c) cn[0] = ss.note;
            for (int i = 0; i < n; ++i) addNote(h, cn[i]);
          } else if (ss.note == kNoteOff) {
            off = true;
          }
        }
        if (h.n) {
          held = h;
          seqN = buildSeq(held, spec.mode, octaves, seq);
        } else if (off) {
          held.n = 0;
          seqN = 0;
        }
      }
    }
```

**Step 4:** PASS (весь `test_arp`). **Step 5:**
`git commit -m "feat(arp): selection source with chord changes"`.

---

### Task 6: Swing, Ghost PRB, Roll, Mutate

**Files:** `lib/core/src/arp_gen.cpp`, `test/test_arp/test_arp.cpp`

**Step 1: Failing tests**

```cpp
void test_swing_on_odd_arp_steps() {
  reset();
  ArpSpec a = spec();
  a.swing = 40;
  applyArp(S, D, makeSel(0, 0, 0, 15), a, pat("x x x x"), 0, ScaleType::Major);
  TEST_ASSERT_NULL(D.steps[0][0].find(Fx::NDG));
  TEST_ASSERT_EQUAL(20, fxSigned(D.steps[0][1].find(Fx::NDG)->val));
  TEST_ASSERT_NULL(D.steps[0][2].find(Fx::NDG));
}

void test_ghost_prb_and_roll() {
  reset();
  ArpSpec a = spec();
  a.ghostPrb = 30;
  a.roll = 100;
  applyArp(S, D, makeSel(0, 0, 0, 15), a, pat("o x x~"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(30, D.steps[0][0].find(Fx::PRB)->val);
  TEST_ASSERT_NULL(D.steps[0][1].find(Fx::PRB));
  const FxSlot* r = D.steps[0][1].find(Fx::RAT);
  TEST_ASSERT_NOT_NULL(r);
  TEST_ASSERT_TRUE(r->val >= 2 && r->val <= 4);
  TEST_ASSERT_NULL(D.steps[0][2].find(Fx::RAT));  // no roll on a slide
}

void test_mutate_seeded() {
  const ArpPattern p = pat("x x x x x x x x x x x x x x x x");
  ArpSpec a = spec();
  a.mutate = 100;
  a.seed = 9;
  reset();
  applyArp(S, D, makeSel(0, 0, 0, 15), a, p, 0, ScaleType::Major);
  Pattern first = D;
  reset();
  applyArp(S, D, makeSel(0, 0, 0, 15), a, p, 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(0, memcmp(first.steps[0], D.steps[0], sizeof(D.steps[0])));
  int notesN = 0;
  for (int i = 0; i < 16; ++i) notesN += D.steps[0][i].hasNote();
  TEST_ASSERT_TRUE(notesN < 16);  // mutate 100 % drops some hits
  a.mutate = 0;
  reset();
  applyArp(S, D, makeSel(0, 0, 0, 15), a, p, 0, ScaleType::Major);
  for (int i = 0; i < 16; ++i) TEST_ASSERT_TRUE(D.steps[0][i].hasNote());
}
```

**Step 2:** FAIL.

**Step 3:** в анонимный namespace:

```cpp
// Seeded variation of one arp step: hits toggled, accents and pitches redrawn. Always draws the
// same count of numbers so a step's change does not shift the next ones.
void mutateStep(ArpStep& a, int amount, Rng& r) {
  const int hit = static_cast<int>(r.below(200)), acc = static_cast<int>(r.below(300));
  const int pitch = static_cast<int>(r.below(400));
  const uint32_t accV = r.below(3), pitchV = r.below(4);
  if (a.kind != ArpKind::Tie && hit < amount) a.kind = a.kind == ArpKind::Note ? ArpKind::Rest : ArpKind::Note;
  if (acc < amount) a.acc = static_cast<ArpAcc>(accV);
  if (pitch < amount) {
    a.pitch = pitchV == 3 ? ArpPitch::Next : static_cast<ArpPitch>(pitchV);
    a.oct = pitchV == 3 ? 1 : 0;
  }
}
```

В `applyArp`: `Rng mut(spec.seed * 2654435761u + 1u);` рядом с `rng`; после
`ArpStep a = pat.steps[...]` — `if (spec.mutate) mutateStep(a, clampInt(spec.mutate, 0, 100), mut);`.
После записи CHD (до `prevNote = note;`):

```cpp
    if (spec.swing && (j & 1)) putFx(st, Fx::NDG, static_cast<uint8_t>(static_cast<int8_t>(clampInt(spec.swing, 0, 100) / 2)));
    if (a.acc == ArpAcc::Ghost && spec.ghostPrb < 100) putFx(st, Fx::PRB, spec.ghostPrb);
    if (spec.roll && !a.slide && static_cast<int>(rng.below(100)) < spec.roll)
      putFx(st, Fx::RAT, static_cast<uint8_t>(2 + rng.below(3)));
```

**Step 4:** PASS. **Step 5:** `git commit -m "feat(arp): swing, ghost probability, rolls, mutate"`.

---

### Task 7: Capture

**Files:** `lib/core/src/arp_gen.{h,cpp}`, `test/test_arp/test_arp.cpp`

**Step 1: Failing tests**

```cpp
void test_capture_round_trip_rhythm() {
  reset();
  ArpSpec a = spec();
  a.velLo = 40;
  a.velHi = 120;
  const ArpPattern p = pat("X xs o xl . x - . x");
  applyArp(S, D, makeSel(0, 0, 0, 15), a, p, 0, ScaleType::Major);
  ArpPattern c;
  TEST_ASSERT_TRUE(captureArp(D, 0, 0, 8, c));
  TEST_ASSERT_EQUAL(9, c.len);
  for (int i = 0; i < 9; ++i) {
    TEST_ASSERT_EQUAL_MESSAGE(p.steps[i].kind, c.steps[i].kind, "kind");
    if (p.steps[i].kind != ArpKind::Note) continue;
    TEST_ASSERT_EQUAL_MESSAGE(p.steps[i].acc, c.steps[i].acc, "acc");
    TEST_ASSERT_EQUAL_MESSAGE(p.steps[i].len, c.steps[i].len, "len");
  }
}

void test_capture_pitch_and_slide() {
  reset();
  const int n[] = {60, 60, 67, 60, 64};
  for (int i = 0; i < 5; ++i) D.steps[2][i].note = static_cast<uint8_t>(n[i]);
  D.steps[2][4].fx[0] = FxSlot{Fx::SLD, 16};
  ArpPattern c;
  TEST_ASSERT_TRUE(captureArp(D, 2, 0, 4, c));
  TEST_ASSERT_EQUAL(ArpPitch::Next, c.steps[0].pitch);
  TEST_ASSERT_EQUAL(ArpPitch::Repeat, c.steps[1].pitch);
  TEST_ASSERT_EQUAL(ArpPitch::Next, c.steps[2].pitch);
  TEST_ASSERT_EQUAL(ArpPitch::Root, c.steps[3].pitch);
  TEST_ASSERT_TRUE(c.steps[4].slide);
  TEST_ASSERT_EQUAL(ArpAcc::Norm, c.steps[0].acc);  // default velocity
}

void test_capture_empty_and_cap() {
  reset();
  ArpPattern c;
  TEST_ASSERT_FALSE(captureArp(D, 0, 0, 15, c));
  D.length = 64;
  D.steps[0][0].note = 60;
  TEST_ASSERT_TRUE(captureArp(D, 0, 0, 63, c));
  TEST_ASSERT_EQUAL(kArpPatMax, c.len);
}
```

**Step 2:** FAIL.

**Step 3:** `arp_gen.h`:

```cpp
// A track's steps s0..s1 (at most kArpPatMax) as a pattern: notes (velocity in thirds of the
// range's spread, NORM when it is under 6 or the velocity is the track's), GAT under 35 % short,
// 90 % and up long, SLD slide; empty steps after a TIE note tie, everything else rests. The same
// pitch as before repeats, the range's lowest note is the root. False: no note in the range.
bool captureArp(const Pattern& p, int track, int s0, int s1, ArpPattern& out);
```

`arp_gen.cpp`:

```cpp
bool captureArp(const Pattern& p, int track, int s0, int s1, ArpPattern& out) {
  if (track < 0 || track >= kTracks) return false;
  const int plen = clampInt(p.length, 1, kMaxSteps);
  s0 = clampInt(s0, 0, plen - 1);
  s1 = clampInt(s1, s0, plen - 1);
  const int n = s1 - s0 + 1 < kArpPatMax ? s1 - s0 + 1 : kArpPatMax;
  const Step* tr = p.steps[track];
  int lo = 128, vMin = 128, vMax = 0;
  for (int i = 0; i < n; ++i) {
    const Step& st = tr[s0 + i];
    if (!st.hasNote()) continue;
    if (st.note < lo) lo = st.note;
    if (st.vel != kVelDefault) {
      if (st.vel < vMin) vMin = st.vel;
      if (st.vel > vMax) vMax = st.vel;
    }
  }
  if (lo == 128) return false;
  out = ArpPattern{};
  out.len = static_cast<uint8_t>(n);
  const int spread = vMax - vMin;
  int prev = -1;
  bool tie = false;
  for (int i = 0; i < n; ++i) {
    const Step& st = tr[s0 + i];
    ArpStep a;
    if (st.hasNote()) {
      a.kind = ArpKind::Note;
      if (st.vel != kVelDefault && spread >= 6) {
        const int rel = (st.vel - vMin) * 3;
        a.acc = rel < spread ? ArpAcc::Ghost : (rel >= 2 * spread ? ArpAcc::Accent : ArpAcc::Norm);
      }
      if (const FxSlot* g = st.find(Fx::GAT)) {
        const int pct = gatePercent(g->val);
        a.len = pct < 35 ? ArpLen::Short : (pct >= 90 ? ArpLen::Long : ArpLen::Norm);
      }
      a.slide = st.find(Fx::SLD) != nullptr;
      if (prev >= 0 && st.note == prev) a.pitch = ArpPitch::Repeat;
      else if (prev >= 0 && st.note == lo) a.pitch = ArpPitch::Root;
      prev = st.note;
      tie = st.find(Fx::TIE) != nullptr;
    } else if (tie && st.note == kNoteEmpty) {
      a.kind = ArpKind::Tie;
    } else {
      tie = false;
    }
    out.steps[i] = a;
  }
  return true;
}
```

В `test_capture_round_trip_rhythm` velocities 40/80/120: ghost rel 0 < 80, norm rel 120 —
не < 80 и не ≥ 160, acc rel 240 ≥ 160. Gate: NORM 50, SHORT 25, LONG 100.

**Step 4:** `pio test -e native` (все native-тесты — убедиться, что ничего не сломано). PASS.
**Step 5:** `git commit -m "feat(arp): capture a track range into an arp pattern"`.

---

### Task 8: Хранилище user-паттернов `/presets/ARP`

**Files:**
- Create: `src/storage/arp_store.h`, `src/storage/arp_store.cpp`

Нет native-тестов (SD); проверка — сборка + железо (Task 11).

**Step 1:** `src/storage/arp_store.h`:

```cpp
#pragma once
#include "arp_gen.h"
#include "hw/sdcard.h"
#include "storage.h"

namespace storage {

// User arp patterns: /presets/ARP/NAME.arp, the pattern's text form (arp_gen.h). UI task only.
constexpr const char* kArpDir = "/presets/ARP";
// Names (no extension), sorted; 0 without a card or folder.
int listArps(char (*names)[hw::kNameMax], int max);
Result loadArp(const char* name, mt::ArpPattern& out);
// name: validName. Creates the folder; writes NAME.tmp, then replaces NAME.arp.
Result saveArp(const char* name, const mt::ArpPattern& p);

}  // namespace storage
```

**Step 2:** `src/storage/arp_store.cpp`:

```cpp
#include "arp_store.h"
#include <stdio.h>
#include <string.h>

namespace storage {
namespace {

constexpr int kTextMax = mt::kArpPatMax * 7 + 2;  // longest token "Xs~r^" + blank

bool arpPath(char* out, size_t cap, const char* name, const char* ext) {
  const int n = snprintf(out, cap, "%s/%s%s", kArpDir, name, ext);
  return n > 0 && static_cast<size_t>(n) < cap;
}

}  // namespace

int listArps(char (*names)[hw::kNameMax], int max) {
  if (!hw::sdReady() || !hw::sdFs().exists(kArpDir)) return 0;
  return hw::sdList(kArpDir, ".arp", names, max, validName);
}

Result loadArp(const char* name, mt::ArpPattern& out) {
  if (!hw::sdReady()) return Result::NoSd;
  char path[192];
  if (!arpPath(path, sizeof(path), name, ".arp")) return Result::BadFile;
  fs::File f = hw::sdFs().open(path, FILE_READ);
  if (!f) return Result::NotFound;
  char text[kTextMax + 1];
  const size_t n = f.read(reinterpret_cast<uint8_t*>(text), kTextMax);
  f.close();
  text[n] = '\0';
  return mt::parseArpPattern(text, out) ? Result::Ok : Result::BadFile;
}

Result saveArp(const char* name, const mt::ArpPattern& p) {
  if (!hw::sdReady()) return Result::NoSd;
  if (!validName(name)) return Result::BadFile;
  fs::FS& fs = hw::sdFs();
  if (!fs.exists("/presets")) fs.mkdir("/presets");
  if (!fs.exists(kArpDir) && !fs.mkdir(kArpDir)) return Result::WriteFail;
  char tmp[192], arp[192], text[kTextMax + 1];
  if (!arpPath(tmp, sizeof(tmp), name, ".tmp") || !arpPath(arp, sizeof(arp), name, ".arp")) return Result::BadFile;
  if (!mt::formatArpPattern(p, text, sizeof(text))) return Result::BadFile;
  if (fs.exists(tmp)) fs.remove(tmp);
  fs::File f = fs.open(tmp, FILE_WRITE);
  if (!f) return Result::WriteFail;
  const size_t len = strlen(text);
  bool ok = f.write(reinterpret_cast<const uint8_t*>(text), len) == len && f.write('\n') == 1;
  f.close();
  if (ok && fs.exists(arp)) ok = fs.remove(arp);
  ok = ok && fs.rename(tmp, arp);
  if (!ok) {
    if (fs.exists(tmp)) fs.remove(tmp);
    return Result::WriteFail;
  }
  return Result::Ok;
}

}  // namespace storage
```

Сверить имена `Result::*` и `fs::File`-API с `src/storage/presets.cpp` (там те же паттерны).

**Step 3:** `pio run -e wt32` — сборка ОК. **Step 4:**
`git commit -m "feat(storage): user arp patterns in /presets/ARP"`.

---

### Task 9: FillDialog — режим ARP

**Files:**
- Modify: `lib/core/src/fill.h` (поле `bool arp = false;` в `FillSpec`, комментарий: «FILL dialog
  shows the arp generator (ArpSpec) instead»)
- Modify: `src/ui/fill_dialog.h`, `src/ui/fill_dialog.cpp`

**Step 1: заголовок.** В `fill_dialog.h`:
- `#include "arp_gen.h"`, `#include "keyboard.h"`, `#include "hw/sdcard.h"`.
- `open(int pattern, const mt::Sel& sel, mt::FillSpec* f, mt::ArpSpec* a, bool drum)`.
- `enum Row`: `kType` первой; после `kSeed`/перед `kReseed` FILL-строк добавить ARP-строки:
  `kASource, kARoot, kAChord, kADest, kAMode, kAOct, kAPattern, kARate, kARotate, kAGate, kASwing,
  kAVelLo, kAVelHi, kASlide, kARoll, kAGhost, kAMutate, kACapture` (порядок показа задаёт
  `buildRows`, не enum).
- Члены: `mt::ArpSpec* a_ = nullptr; mt::ArpPattern arpPat_; Keyboard kb_;`
  `static constexpr int kUserMax = 32; char (*userNames_)[hw::kNameMax] = nullptr; int userCount_ = 0;`
  (буфер имён — `new (std::nothrow)` при первом open, 3 КБ, не освобождается).
- Методы: `int patternCount() const;` `void resolvePattern();` `void capture(const char* name);`
  `const char* patternName() const;`.
- Обновить комментарий класса: Type FILL / ARP, Capture.

**Step 2: строки ARP** в конструкторе (`fill_dialog.cpp`), стиль как у существующих:

```cpp
  params_[kType] = {"Type", [this](char* o, int n) { snprintf(o, n, "%s", f_->arp ? "ARP" : "FILL"); },
                    [this](int d) { if (d) f_->arp = d > 0; }};
  params_[kASource] = {"Source", [this](char* o, int n) { snprintf(o, n, "%s", a_->source == mt::ArpSource::Chord ? "CHORD" : "SELECTION"); },
                       [this](int d) { a_->source = stepEnum(a_->source, d); }};
  params_[kARoot] = {"Root", [this](char* o, int n) { char nn[4]; mt::noteName(a_->root, nn); snprintf(o, n, "%s", nn); },
                     [this](int d) { a_->root = clampu8(a_->root + d, 0, 127); }};
  params_[kAChord] = {"Chord", [this](char* o, int n) { snprintf(o, n, "%s", mt::chordName(a_->chord)); },
                      [this](int d) { a_->chord = clampu8(a_->chord + d, 0, mt::kChordCount - 1); }};
  params_[kADest] = {"Dest", [this](char* o, int n) { snprintf(o, n, "T%d %s", a_->dest + 1, app_.project().tracks[a_->dest].name); },
                     [this](int d) { a_->dest = clampu8(a_->dest + d, 0, mt::kTracks - 1); },
                     nullptr, [this] { return app_.project().trackIsDrum(a_->dest); }};
  params_[kAMode] = {"Mode", [this](char* o, int n) { snprintf(o, n, "%s", mt::arpModeName(a_->mode)); },
                     [this](int d) { a_->mode = stepEnum(a_->mode, d); }};
  params_[kAOct] = {"Octaves", [this](char* o, int n) { snprintf(o, n, "%u", a_->octaves); },
                    [this](int d) { a_->octaves = clampu8(a_->octaves + d, 1, 4); }};
  params_[kAPattern] = {"Pattern", [this](char* o, int n) { snprintf(o, n, "%s", patternName()); },
                        [this](int d) {
                          a_->pattern = clampu8(a_->pattern + d, 0, patternCount() - 1);
                          resolvePattern();
                        }};
  params_[kARate] = {"Rate", [this](char* o, int n) { snprintf(o, n, "x%u", a_->rate); },
                     [this](int d) { a_->rate = clampu8(a_->rate + d, 1, 4); }};
  params_[kARotate] = {"Rotate", [this](char* o, int n) { snprintf(o, n, "%u", a_->rotate); },
                       [this](int d) { a_->rotate = clampu8(a_->rotate + d, 0, arpPat_.len ? arpPat_.len - 1 : 0); }};
  params_[kAGate] = {"Gate", [this](char* o, int n) { snprintf(o, n, "%u %%", a_->gate); },
                     [this](int d) { a_->gate = clampu8(a_->gate + d, 5, 100); }};
  params_[kASwing] = {"Swing", [this](char* o, int n) { snprintf(o, n, "%u %%", a_->swing); },
                      [this](int d) { a_->swing = clampu8(a_->swing + d, 0, 100); }};
  params_[kAVelLo] = {"Vel Lo", [this](char* o, int n) { snprintf(o, n, "%u", a_->velLo); },
                      [this](int d) { a_->velLo = clampu8(a_->velLo + d, 1, 127); }};
  params_[kAVelHi] = {"Vel Hi", [this](char* o, int n) { snprintf(o, n, "%u", a_->velHi); },
                      [this](int d) { a_->velHi = clampu8(a_->velHi + d, 1, 127); }};
  params_[kASlide] = {"Slide", [this](char* o, int n) { char s[5]; mt::fxFormat(mt::Fx::SLD, a_->slide, s); snprintf(o, n, "%s", s); },
                      [this](int d) { a_->slide = mt::fxStep(mt::Fx::SLD, a_->slide, d); }};
  params_[kARoll] = {"Roll", [this](char* o, int n) { snprintf(o, n, "%u %%", a_->roll); },
                     [this](int d) { a_->roll = clampu8(a_->roll + d, 0, 100); }};
  params_[kAGhost] = {"Ghost PRB", [this](char* o, int n) { if (a_->ghostPrb >= 100) snprintf(o, n, "OFF"); else snprintf(o, n, "%u %%", a_->ghostPrb); },
                      [this](int d) { a_->ghostPrb = clampu8(a_->ghostPrb + d, 0, 100); }};
  params_[kAMutate] = {"Mutate", [this](char* o, int n) { snprintf(o, n, "%u %%", a_->mutate); },
                       [this](int d) { a_->mutate = clampu8(a_->mutate + d, 0, 100); }};
  params_[kACapture] = {"Capture", [](char* o, int n) { snprintf(o, n, "SAVE DEST AS PATTERN"); }, nullptr};
```

`kSeed` в ARP пишет `a_->seed`: формат/edit проверяют `f_->arp` и берут `a_->seed` или `f_->seed`;
`reseed()` — так же.

**Step 3: buildRows.** В начале `shownCount_ = 0;` всегда добавлять `kType`. Если `f_->arp`:
показать по порядку `kASource`, `kARoot`/`kAChord` (только Source = CHORD), `kADest`, `kAMode`,
`kAOct`, `kAPattern`, `kARate`, `kARotate`, `kAGate`, `kASwing`, `kAVelLo`, `kAVelHi`, `kASlide`,
`kARoll`, `kAGhost`, `kAMutate`, `kSeed`+`kReseed` (если `mutate || roll || mode == Random`),
`kACapture`, `kOk`, `kCancel`. Иначе — прежняя логика FILL (без ARP-строк). Реализация: два
массива id в порядке показа + лямбда `show(id)`; FILL-ветка оставляет существующий `switch`, но в
`default` ARP-id скрыты.

**Step 4: паттерны.**

```cpp
int FillDialog::patternCount() const { return mt::arpFactoryCount() + userCount_; }

const char* FillDialog::patternName() const {
  const int i = a_->pattern;
  if (i < mt::arpFactoryCount()) return mt::arpFactoryName(i);
  return userNames_[i - mt::arpFactoryCount()];
}

// arpPat_ from a_->pattern: factory text, else the user file (a bad file plays the first factory one).
void FillDialog::resolvePattern() {
  if (a_->pattern >= patternCount()) a_->pattern = 0;
  const int i = a_->pattern;
  bool ok;
  if (i < mt::arpFactoryCount()) {
    ok = mt::parseArpPattern(mt::arpFactoryText(i), arpPat_);
  } else {
    const storage::Result r = storage::loadArp(userNames_[i - mt::arpFactoryCount()], arpPat_);
    ok = r == storage::Result::Ok;
    if (!ok) app_.toast(storage::resultText(r));
  }
  if (!ok) mt::parseArpPattern(mt::arpFactoryText(0), arpPat_);
  if (arpPat_.len && a_->rotate >= arpPat_.len) a_->rotate = 0;
}
```

В `open()`: принять `a`, `a_ = a`; `userNames_` выделить при `nullptr` (если не вышло —
`userCount_ = 0`); `userCount_ = storage::listArps(userNames_, kUserMax)`; `a_->dest` ограничить
`kTracks - 1`; `resolvePattern()`.

**Step 5: preview / restore / ok.**
- `restore()`: копировать все дорожки (`memcpy(pt.steps, saved_->steps, sizeof(pt.steps))`) —
  Dest может быть вне выделения и меняться, пока диалог открыт.
- `preview()`: после `restore()`:
  ```cpp
  if (f_->arp) {
    if (!mt::applyArp(*saved_, p.patterns[pattern_], sel_, *a_, arpPat_, p.scaleRoot,
                      static_cast<mt::ScaleType>(p.scaleType), drumTr))
      ;  // drum dest: Dest row shows red, nothing written
  } else {
    mt::applyFill(...);  // как было
  }
  ```
  (без пустого `if` — просто вызвать и игнорировать результат: `(void)mt::applyArp(...)`.)
- `ok()`: swap всех дорожек (`for (int t = 0; t < mt::kTracks; ++t)`) вместо `sel_.t0..t1`;
  тост `f_->arp ? "ARP" : "FILL"`.

**Step 6: Capture + клавиатура.**
- `action()`: `kACapture` → `kb_.open("ARP NAME:", "", [this](const char* t) { capture(t); });`
  `kReseed` в ARP — `a_->seed = esp_random(); preview();`.
- ```cpp
  // Dest's range as it was at open() (the hand-made line, not the preview) into /presets/ARP.
  void FillDialog::capture(const char* name) {
    if (!name || !*name) return;
    char clean[Keyboard::kMaxLen + 1];
    storage::sanitize(name, clean);  // сверить сигнатуру в storage.h
    mt::ArpPattern cp;
    if (!mt::captureArp(*saved_, a_->dest, sel_.s0, sel_.s1, cp)) {
      app_.toast("NO NOTES");
      return;
    }
    const storage::Result r = storage::saveArp(clean, cp);
    if (r != storage::Result::Ok) {
      app_.toast(storage::resultText(r));
      return;
    }
    if (userNames_) userCount_ = storage::listArps(userNames_, kUserMax);
    for (int i = 0; i < userCount_; ++i)
      if (strcmp(userNames_[i], clean) == 0) a_->pattern = static_cast<uint8_t>(mt::arpFactoryCount() + i);
    resolvePattern();
    preview();
    app_.toast("SAVED");
  }
  ```
- `onInput`: в начале `if (kb_.isOpen()) { kb_.onInput(ev); app_.invalidate(); return; }`.
- `onTouch`: `if (kb_.isOpen()) { kb_.onTouch(ev, app_.shift()); app_.invalidate(); return; }`.
- `draw`: `if (kb_.isOpen()) { kb_.draw(s, y0); return; }`.
- `cancel()`/`ok()`/`abandon()`: `kb_.close()`.
- Заголовок в ARP: `"ARP T%d %d-%d"` (Dest и шаги), при SELECTION добавить `" < T%d-%d"`.

**Step 7:** `pio run -e wt32` — сборка (пока GridScreen не передаёт `ArpSpec` — следующая задача;
допустимо сделать Task 9 и 10 одним коммитом). Сверить `storage::sanitize`, `app_.shift()`,
`storage::resultText` по `preset_browser.cpp`.

---

### Task 10: GridScreen — ArpSpec

**Files:** `src/ui/grid_screen.h:144-146`, `src/ui/grid_screen.cpp` (`openFill`, ~639)

**Step 1:** в `grid_screen.h` рядом с `fillSpec_`:
```cpp
  mt::ArpSpec arpSpec_;   // RAM only, FILL's arp page
  bool arpInit_ = false;  // root set from the track once
```
и `#include "arp_gen.h"` (если не тянется через `fill_dialog.h`).

**Step 2:** в `openFill()` перед `setEdit(false);`:
```cpp
  if (!arpInit_) {
    arpInit_ = true;
    arpSpec_.root = lastNote_[tr];
  }
  arpSpec_.dest = sel.t0;  // default: the first track of the selection
```
и вызов `fill_.open(app_.editPattern(), sel, &f, &arpSpec_, drum())`.

Важно: при Type = ARP курсор на колонке VEL/FX всё равно переключает `f.target` — не мешает
(target используется только FILL).

**Step 3:** `pio run -e wt32` — OK; `pio test -e native` — OK.

**Step 4:** коммит Task 9 + 10:
```bash
git add lib/core/src/fill.h src/ui/fill_dialog.h src/ui/fill_dialog.cpp src/ui/grid_screen.h src/ui/grid_screen.cpp
git commit -m "feat(ui): FILL arp page - live preview, patterns, capture"
```

---

### Task 11: Документация и проверка на железе

**Files:** `docs/manual_ru.md`, `docs/manual.md` (раздел Fill в GRID — найти `grep -n "Fill" docs/manual_ru.md`)

**Step 1:** Подраздел «Арп (Type: ARP)» в обоих мануалах: строки диалога (раздел 3 дизайна),
SELECTION и смена аккордов, что пишется в Dest (раздел 5), формат `.arp` (токены, пример
`X x~ x . X x~ X x`), Capture, список factory-групп. Упомянуть: TIE на конце диапазона держит ноту
дальше; Slide/Roll — INT (SLD на MIDI серый); drum Dest не пишется (строка Dest красная).

**Step 2:** Прошить `pio run -e wt32 -t upload` (или `-e wt32-ota`), чек-лист:
- [ ] Fill → Type ARP: дефолт CHORD, C-триада, UP, 16THS — 16 нот на дорожке, слышно
- [ ] Перебор Pattern / Mode / Octaves / Rate — preview мгновенный, без щелчков и зависаний
- [ ] Cancel и долгий энкодер — дорожка как до открытия; OK — одна запись undo, undo возвращает
- [ ] SELECTION: аккорды на T1–T3 (+ CHD), Dest T4 — арп следует сменам аккордов; Dest = T1 внутри выделения
- [ ] ACID 1 на SYNTH MONO — слышны слайды и акценты; TR GATE / DNB ROLL / PSY GALOP звучат по жанру
- [ ] Roll 50 %, Ghost PRB 50 %, Swing 50 %, Mutate + Reseed (Shift+клик)
- [ ] Capture: имя → файл `/presets/ARP/NAME.arp`, паттерн выбран и звучит так же; файл виден по Wi-Fi
- [ ] Drum Dest — строка красная, ничего не пишется; без SD — Capture даёт тост ошибки

**Step 3:** `git commit -m "docs: FILL arp generator"`.
