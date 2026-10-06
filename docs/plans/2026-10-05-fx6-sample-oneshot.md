# 6 FX / detail одной дорожки / сэмплы до OFF / FX OFF — план

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** баг Transpose, 6 FX-слотов на шаг, detail view одной дорожки, сэмплы игнорируют note off + choke, FX OFF.

**Architecture:** дизайн — `docs/plans/2026-10-05-fx6-sample-oneshot-design.md`. Ядро (`lib/core`) под
native-тестами (Unity), UI (`src/ui`) проверяется сборкой wt32. Коммиты не делаем (память no-commits).

**Tech Stack:** C++17, PlatformIO (`pio test -e native`, `pio run -e wt32`), LovyanGFX.

---

### Task 1: Transpose — сброс в 0
**Files:** `src/ui/transpose_dialog.cpp` (`open`), `src/ui/transpose_dialog.h` (комментарий).
- В `open()` после `clampAmount()` ставить `amount_ = 0;` (clampAmount убрать — не нужен).
- Комментарий класса: «Amount starts at 0 on every open; the mode is kept.»
- Проверка: сборка wt32 (Task 9).

### Task 2: Model — kFxSlots = 6
**Files:** `lib/core/src/model.h`, `test/test_model/test_main.cpp`.
1. Тест: `sizeof(Step) == 14`; `Step s; s.fx[5] = {Fx::GAT, 10}; find(Fx::GAT) == &s.fx[5]`;
   `isEmpty()` false; `hasFx()` true; пустой — `hasFx()` false.
2. `pio test -e native -f test_model` — FAIL (компиляция).
3. Реализация:
```cpp
constexpr int kFxSlots = 6;
struct Step {
  uint8_t note = kNoteEmpty;
  uint8_t vel = kVelDefault;
  FxSlot fx[kFxSlots];
  bool hasFx() const {
    for (const FxSlot& f : fx)
      if (f.cmd != Fx::None) return true;
    return false;
  }
  bool isEmpty() const { return note == kNoteEmpty && vel == kVelDefault && !hasFx(); }
  bool hasNote() const { return note < 128; }
  const FxSlot* find(Fx f) const {
    for (const FxSlot& s : fx)
      if (s.cmd == f) return &s;
    return nullptr;
  }
};
static_assert(sizeof(Step) == 2 + 2 * kFxSlots, "Step layout (file format)");
```
4. Заменить `fx[0].cmd != None || fx[1].cmd != None` на `hasFx()`: `sequencer.cpp` (2), `grid_screen.cpp` overview.
5. `midi_import.cpp`: `slot < 2` -> `slot < kFxSlots`.
6. `step_expand.h`: `kMaxStepEvents = 80` (8 RAT x 4 ноты x 2 + 6 контролов + запас), комментарий.
7. `pio test -e native` — всё зелёное (поправить тесты, завязанные на 6-байтный шаг).

### Task 3: Project IO — PATN 14 байт, чтение 6
**Files:** `lib/core/src/project_io.cpp` (`readPatn`, запись PATN), `test/test_project_io/test_main.cpp`.
1. Тесты: round-trip шага с 6 FX; загрузка вручную собранного старого PATN (шаг 6 байт: note, vel,
   cmd0, val0, cmd1, val1) — fx[2..5] пустые. Взять существующий helper сборки чанков в тесте.
2. `readPatn`: `const uint32_t body = size - kPatHeader; const uint32_t cells = kTracks * len;`
   `stepBytes = body == cells * 6 ? 6 : (body == cells * sizeof(Step) ? sizeof(Step) : 0)`, 0 -> BadValue.
   Читать `uint8_t b[sizeof(Step)]` по `stepBytes`, слотов `(stepBytes - 2) / 2`.
3. Запись: `uint8_t b[sizeof(Step)]`, note, vel, затем все слоты.
4. `pio test -e native -f test_project_io` — PASS.

### Task 4: Fx::OFF — описание
**Files:** `lib/core/src/model.h` (enum: `..., SLC, OFF, Count`), `lib/core/src/fx_info.cpp`
(таблица: `{"OFF", "NOTE OFF", 0, 96, 0, false}` — не synth-only), `test/test_fx_info`.
- Тест: `fxName(Fx::OFF) == "OFF"`, диапазон 0..96, default 0, `!fxSynthOnly(Fx::OFF)`.
- Проверить, что таблица индексируется по enum (static_assert на размер).

### Task 5: expandStep — OFF
**Files:** `lib/core/src/step_expand.h/.cpp`, `test/test_expand/test_main.cpp`.
- `ExpandCtx`: добавить `uint16_t tps = 24;` в конец (агрегатная инициализация в тестах не ломается).
- `ExpandOut`: `int32_t offUs = -1;` (сбрасывать в начале expandStep).
- Логика после CND/PRB, nudge:
```cpp
const FxSlot* offFx = s.find(Fx::OFF);
const uint32_t tickUs = c.tps ? c.stepUs / c.tps : c.stepUs / 24;
if (offFx) out.offUs = nudge + static_cast<int32_t>(tickUs * offFx->val);
if (!s.hasNote()) return out.count > 0 || offFx;
...
if (offFx) {
  out.tie = false;
  if (out.offUs < nudge + static_cast<int32_t>(kMinGateUs)) out.offUs = nudge + kMinGateUs;
}
// в цикле: if (offFx && on >= out.offUs) continue;  off = min(on + gateUs, out.offUs), но >= on + kMinGateUs
```
- Тесты: OFF 0 на пустом шаге -> true, count 0, offUs 0; OFF 12 при tps 24 -> offUs = stepUs/2;
  нота + GAT 200 + OFF 6 -> NoteOff на 6 тиках; RAT 4 + OFF 12 -> 2 удара; TIE + OFF -> tie false;
  нота + OFF 0 -> NoteOff на kMinGateUs.
- Sequencer передаёт `tps = ticks()` (оба места создания ExpandCtx).

### Task 6: Sequencer — OFF и 0xFF
**Files:** `lib/core/src/sequencer.cpp/.h`, `test/test_sequencer/test_main.cpp`.
- Helper `void pushOff(int tr, uint64_t t)`: `releaseTie(tr, t)`; на INT — `push(t, 0xFF, 0, 0, 0, false, 1, tr)`.
- `scheduleStep`, шаг без ноты: `if (s.note == kNoteOff) pushOff(tr, t)` (вместо releaseTie);
  после expand: `if (ex.offUs >= 0) pushOff(tr, max(t + offUs, earliest))`.
  Условие expand: `s.hasFx()` как раньше.
- Шаг с нотой: после цикла нот `if (ex.offUs >= 0) pushOff(tr, at-время t + offUs (+shift))`.
- `skipStep`: OFF-нота -> `pushOff(tr, now)`; OFF FX -> `pushOff(tr, now)`.
- `dispatch`: `if (e.b[0] == 0xFF && !toSynth) return;`.
- Тесты (по образцу существующих с фейковым sink): INT-трек OFF-нота -> synth получает 0xFF;
  MIDI-трек OFF-нота -> в MIDI 0xFF нет; FX OFF 12 на INT -> 0xFF через полшага; tie + FX OFF -> NoteOff в OFF.

### Task 7: Synth — сэмплы до OFF, choke
**Files:** `lib/core/src/synth_env.h` (`fade`), `synth_voice.h` (`uint8_t gen`), `synth.h` (`TrackRt::gen`),
`synth.cpp`, `test/test_synth_env`, `test/test_synth/test_main.cpp`.
- `Env::fade(uint16_t ms)`: `rLen_ = samples(ms); gate(false);` (тест в test_synth_env).
- `fx()` на kSynthStep с нотой: `++r.gen`.
- `noteOff`: пропускать `v.sample && v.track < kTracks`.
- `noteOn` для SAMPLE на дорожке `< kTracks` до allocVoice (и не при SLD-продолжении):
```cpp
for (auto& x : voices_)
  if (x.on && x.track == track && x.sample && x.env.stage() != Env::Stage::Release &&
      (x.gen != r.gen || x.note == note))
    x.env.fade(kChokeMs);  // 3 ms
```
  затем `v.gen = r.gen`.
- overlap для сэмпла только при SLD: `const bool sldNote = r.sld != 0;` в начале; `overlap = ... && (!sample || sldNote)`.
- `releaseTrack` гасит и сэмплы (как есть).
- Тесты: note off не гасит сэмпл (Loop OFF и FWD), на kPreviewTrack гасит; следующий шаг (stepStart с нотой)
  + нота -> старый голос в Release; аккорд в одном шаге (2 ноты) -> 2 голоса держатся; та же нота в шаге
  -> старый в Release; 0xFF -> все в Release; моно-сэмпл, вторая нота -> позиция с начала.
- Поправить старые тесты, ожидавшие освобождение сэмпл-голоса по note off.

### Task 8: GRID — 6 FX и detail одной дорожки
**Files:** `src/ui/grid_screen.h/.cpp`.
- `enum Field : uint8_t { kNote, kVel, kFx1, kFields = kFx1 + 2 * mt::kFxSlots };` — fx-поле `f`:
  слот `(f - kFx1) / 2`, cmd если `(f - kFx1) % 2 == 0`.
- Константы: `kDetW = kScreenW - kNumW` (448), `kFieldW = 32`, `kFieldX = 0`.
- `fieldText`: `default` уже через `(field - 2) / 2` — ок.
- `editTurn`: `isCmd = (curField_ - kFx1) % 2 == 0` вместо `kFx1 || kFx2`.
- `lastFx_[kTracks][mt::kFxSlots]`.
- `drawDetail`: одна колонка `tr = track()`, шапка «N NAME» + «FX1..FX6» над cmd-полями (dim);
  текст поля `x0 + f * kFieldW + 4`; dim vline перед каждым `kFx1 + 2k` и после VEL; курсор `drawRect` на поле.
- `hit()`: detail — `tr = track()`, `field = clampi((x - kNumW) / kFieldW, 0, kFields - 1)`.
- `onTouch` строка имён: в detail — return.
- Убрать `pair`, вертикальную линию между двумя колонками.

### Task 9: Проверка и документация
- `pio test -e native` — все зелёные; `pio run -e wt32` — собирается.
- `docs/manual.html`: 6 FX, detail одной дорожки, FX OFF, сэмплы до OFF/choke, Transpose с 0.
- Обновить память project-status.
