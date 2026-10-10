# LFO Retrig — план реализации

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** У каждого LFO инструмента строка Retrig ON/OFF; OFF — общая фаза на инструмент
(FREE — непрерывно, TEMPO — от старта транспорта).

**Architecture:** Бит 1 байта sync (`LfoCfg::sync`, `Instrument::lfoSync`) = Retrig OFF, бит 0 =
TEMPO. `Synth` держит фазы инструментов, двигает их раз за блок; `Sequencer::start` шлёт синту
0xFA — сброс TEMPO-фаз. Дизайн: `docs/plans/2026-10-09-lfo-retrig-design.md`.

**Tech Stack:** C++17, PlatformIO (`pio test -e native`, `pio run -e wt32`), Unity.

Коммитов нет — всё коммитится после проверки на железе.

---

### Task 1: Хелперы и кодек
**Files:** `lib/core/src/model.h` (около `lfoSyncHz`, `LfoCfg`), `lib/core/src/inst_codec.cpp:152-178`,
тесты в существующем тесте кодека/модели (найти `grep -rn "packLfo\|unpackLfo" test`).
- `constexpr uint8_t kLfoTempo = 1, kLfoFree = 2;` `inline bool lfoTempo(uint8_t s) { return s & kLfoTempo; }`
  `inline bool lfoFree(uint8_t s) { return s & kLfoFree; }`; комментарии полей sync — про биты.
- Все места `sync ?` / `lfoSync ?` / `sync = on` (synth.cpp `lfo()`, inst_screen.cpp ~416-433,
  inst_codec.cpp, demos.cpp, presets_factory, project_io readLfox) — через хелперы; переключение
  TEMPO не должно терять бит Free.
- pack: `b[0] = m.lfoSync & 3`, `r[4] = l.sync & 3`; unpack: `& 3`, clamp rate по `lfoTempo`.
- TDD: round-trip 0..3 для LFO 1 и 2..4; старые значения 0/1 читаются как раньше.

### Task 2: Общая фаза в Synth
**Files:** `lib/core/src/synth.{h,cpp}` (`lfo()` ~812, `resetLfos` ~804, `render`), тест
`test/test_synth` (посмотреть, как там создают Project/Instrument и читают голос/значение).
- Члены `float instPhase_[kInstruments][kLfos]`, `float instRnd_[kInstruments][kLfos]`; в
  `reset()` — нули.
- Раз за блок рендера (до голосов): для каждого инструмента и LFO с depth != 0 и `lfoFree(sync)` —
  `ph += hz * kBlock / kSynthRate`, на переходе через 1 — новый rnd. Выяснить, как голоса получают
  `dt` в `control()`, и сделать так, чтобы общая фаза не двигалась N раз за блок.
- `lfo(v, c, i, dt)`: при `lfoFree(c.sync)` брать фазу/rnd инструмента голоса (`v.instr` или как
  там зовётся), а не `v.lfoPhase`; волну считать той же функцией (вынести расчёт формы в хелпер).
- TDD: OFF — два голоса одного инструмента, взятые в разные блоки, на одном блоке дают одинаковое
  значение LFO (через публичный путь или тестовый доступ, как принято в test_synth); ON — голос,
  взятый позже, стартует с фазы 0 (как раньше).

### Task 3: Транспорт 0xFA
**Files:** `lib/core/src/sequencer.cpp:52` (`start`), `lib/core/src/synth.cpp:151` (`apply`),
`isOff`-подобная проверка в synth.cpp:29 (0xFA не должен считаться note-off), тесты test_sequencer /
test_synth.
- `start`: после цикла 0xFE — один `out.synth(now, 0, &kTransportStart, 1)` с `0xFA` (только если
  есть INT-дорожки — не обязательно, синт и так игнорирует). Не слать в MIDI out (там уже свой 0xFA).
- `apply`: 0xFA → обнулить `instPhase_` у LFO с `lfoTempo && lfoFree` (FREE+OFF не трогать).
- Проверить `event()`: очередь принимает однобайтные сообщения (0xFE уже такой).
- TDD: sequencer — при start синт получает 0xFA; synth — после 0xFA фаза TEMPO+OFF 0, FREE+OFF нет.

### Task 4: UI
**Files:** `src/ui/inst_screen.cpp` (~410-440, строки LFO), enum строк в `inst_screen.h`.
- Строка `Retrig` после `Sync`: `ON` / `OFF`, edit переключает бит kLfoFree; серая (dim), как
  остальные строки LFO, когда depth 0.
- Проверить, что строки INST помещаются / прокрутка работает, page bar и счётчик строк.
- `pio run -e wt32`.

### Task 5: Мануалы
**Files:** `docs/manual_ru.md` (~822, таблица LFO и абзац «фаза LFO перезапускаются…»), `docs/manual.md`.
- Строка Retrig: ON (по умолчанию) — фаза с каждой новой нотой (кроме легато); OFF — одна фаза на
  инструмент для всех голосов и дорожек; FREE — непрерывно, TEMPO — от старта воспроизведения, с
  начала такта. Поправить абзац про сброс фазы.

### Проверка на железе
- [ ] SYNTH, LFO CUTOFF 1 BAR TEMPO Retrig OFF + арп 16-ми — фильтр «дышит» поверх нот, каждый
      старт с начала одинаков
- [ ] Retrig ON — как раньше (каждая нота с начала LFO)
- [ ] FREE + OFF — медленный LFO не сбрасывается между нотами
- [ ] Сохранение/загрузка проекта и пресета сохраняют Retrig
