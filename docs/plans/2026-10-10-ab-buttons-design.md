# Кнопки A и B — дизайн

Дата: 2026-10-10.

## Железо

A — P16 (`pins::kBtnABit` = 14), B — P17 (`pins::kBtnBBit` = 15) на PCF8575, на GND. Опрос и антидребезг — как у Shift/Play (`src/hw/input.cpp`). События `ADown`/`AUp`/`BDown`/`BUp`; отпускания не теряются (ждут места в очереди).

## Аккорды (`lib/core/src/ab_keys.h`, native-тесты)

Автомат `mt::AbKeys` без железа: вход — нажатия/отпускания A/B, поворот энкодера, кнопка дорожки; выход — действие.

| Ввод | Действие |
|---|---|
| A + поворот | `EditTurn(delta, shift)` |
| отпустить A после поворота | `EditEnd` |
| A зажата + нажать B | `EditCancel` (если A уже правила) |
| A нажата-отпущена без ничего | `ATap(shift)` |
| B + поворот | `TabTurn(delta)` |
| B + Shift + поворот | `PageTurn(delta)` |
| B нажата-отпущена без ничего | Shift ? `Undo` : `Back` |
| A + кнопка N | `Solo(N)` |
| B + кнопка N | `QueuePattern(N, shift)` |

B важнее A, если зажаты обе (поворот/кнопка дорожки). Тап — только на отпускании и если за время удержания ничего не было.

## App

- `TabTurn`: вкладки по кругу GRID → TRACK → MIX → BANK → INST → PROJ → FILE.
- `Undo`: GRID — `GridScreen::undo()`, иначе `App::doUndo()` + toast.
- `Solo(N)`: дорожка `(curTrack/8)*8 + N`, toast `TRACK n SOLO/ON`.
- `QueuePattern(N)`: паттерн `(editPattern/8)*8 + N`; без Shift — `Cmd::QueuePattern`, с Shift — `Cmd::SelectPattern`.
- `Back`: открыто меню — закрыть; иначе `Screen::onBack()`.
- `EditTurn`/`EditEnd`/`EditCancel` — новые `InputType`, идут в `screen()->onInput()`. Меню открыто — игнор; правка BPM — `EditTurn` как поворот.
- `Screen`: новые виртуальные `onBack()`, `onPage(int)`, `onATap(bool)` — по умолчанию ничего.

## Экраны

- **ParamList**: `EditTurn` — нет правки → войти (`beginEdit()`, флаг `hold_`), затем `edit(delta × (shift ? 10 : 1))`; строки без `edit` и page bar — игнор. `EditEnd` — выйти, если вошла A. `EditCancel` — `cancelEdit()`.
- **Строка имени** (TRACK/INST): `EditTurn` — символ, Shift — позиция.
- **GRID**: `EditTurn` — `setEdit(true)` при необходимости (hold), затем `editTurn(delta, shift)`. `EditEnd` — `setEdit(false)`, если вошла A. `EditCancel` — `cancelCell()`. `ATap` — пустой шаг: `lastNote_` дорожки; непустой: прослушать; Shift+A — очистить шаг. `Back` — закрыть fill/transpose → снять выделение → выключить edit/rec/perf.
  Риск: вход в edit показывает клавиатуру (меняется число строк) — проверить на железе.
- **BANK chain**: `EditTurn` как `rowEdit_` (Shift — поле).
- **SampleEditor**, строка маркера: `EditTurn` двигает маркер (Shift — к onset).
- **MIX**: `EditTurn` — master vol.
- **onBack**: закрыть открытый оверлей (keyboard, presets, wt picker, список файлов, диалоги, copy-режим BANK) — тем же путём, что их `EncLong`.
- **onPage**: `showPage(page ± 1)` в TRACK, INST, PROJ.

Клик энкодером остаётся как был.

## Доки

`docs/manual.md`, `docs/manual_ru.md` — раздел про A/B; README — пины P16/P17; комментарий в `pins.h`.
