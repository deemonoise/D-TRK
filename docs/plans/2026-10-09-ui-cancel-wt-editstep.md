# UI: отмена правки, выбор вейвтейблов, Edit step — план

> Дизайн: docs/plans/2026-10-09-ui-cancel-wt-editstep-design.md. Коммиты — только по просьбе.

**Goal:** Shift+клик отменяет правку значения; WtPicker выглядит и листается как импорт файлов,
без автопрослушки; Edit step в GRID.

**Architecture:** ParamList получает отмену (снимок области или откат шагами по тексту), экраны
задают область; GRID/BANK/FILL — свои снимки. WtPicker переписан на модель списка FileScreen
(строка 0 = Back/Up). GridScreen — `editStep_` + пункт меню + метка в шапке.

**Tech Stack:** C++ / Arduino / LovyanGFX, PlatformIO (`pio test -e native`, `pio run -e wt32`).

### Task 1: ParamList — cancel
- `src/ui/param_list.h/.cpp`: `setEditScope(void*, size_t)`, `cancelEdit()`; при входе в правку
  (клик, тап) — `beginEdit()`: текст значения в `orig_`, снимок области в буфер (PSRAM, по размеру).
  `onInput`: EncClick + shift при `edit_` — `cancelEdit()`. Откат шагами: направление по знаку
  накопленной дельты, до 4096 вызовов `edit(±1)` пока `format()` != `orig_`.

### Task 2: Экраны с ParamList
- INST: область = текущий инструмент; TRACK: `TrackCfg`; PROJ и диалоги — без области.
- Обработчики, перехватывающие Shift+клик до `list_.onInput` (FILL reseed), — пропускать при правке.
- Тост `CANCEL` из экранов (у ParamList нет App) — через `setOnCancel`.

### Task 3: GRID — cancel ячейки
- `cell_` (pat, tr, step, Step), `cellOnly_`; снимок при `setEdit(true)` и в `cursorMoved()` в правке.
  Shift+клик в правке: восстановить, `dropUndo()` если `cellOnly_` и снимок брался, выйти из правки.

### Task 4: BANK chain row cancel
- Снимок строки chain при `rowEdit_ = true`; Shift+клик в правке — восстановить.

### Task 5: GRID — Edit step
- `editStep_` (0–16, 1); `enterDegree` → `moveStep(editStep_)`; меню `Edit step: N` → подменю 0–16;
  метка `STEP N` в шапке.

### Task 6: WtPicker
- Модель FileScreen: строка 0 Back/Up, кольцо, без кнопок шапки, без `pick()` на поворот/тап,
  без `audio::preview`; выбор ставит таблицу и закрывает; EncLong — отмена/назад.

### Task 7: Проверка
- `pio test -e native`, `pio run -e wt32`.

## Чек-лист на железе
1. INST: правка значения, поворот, Shift+клик — значение вернулось, тост CANCEL. Смена типа + отмена — всё как было.
2. PROJ: BPM, громкость, тема — отмена возвращает.
3. GRID: правка ноты/FX, Shift+клик — ячейка вернулась; вне правки Shift+клик — смена вида.
4. GRID: меню → Edit step 3, ввод нот кнопками — курсор прыгает на 3; 0 — стоит.
5. BANK chain: правка строки, Shift+клик — откат. FILL: Shift+клик в правке — отмена, вне — reseed.
6. INST OSC → таблицы: кольцо, без звука при прокрутке, клик ставит, `< Back`/EncLong — отмена.
7. IMPORT...: вид как Import WAV, `< Up (..)`, кольцо, импорт файла.
