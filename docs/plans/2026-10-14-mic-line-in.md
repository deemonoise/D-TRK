# Микрофон и линейный вход — план

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** два общих язычка нижней детали — сзади MIC + тумблер (MIDI на верхе, x = 43,5), спереди PHONES + LINE IN; вырезы с пазами в верхе; доки.

**Architecture:** `enclosure/case.scad`, `trk = true`. Общий модуль выреза/язычка в локальной системе стенки (y = 0 — наружная грань), для задней стенки — зеркало по Y. Дизайн: `2026-10-14-mic-line-in-design.md`.

**Tech Stack:** OpenSCAD 2021.01 (`/Applications/OpenSCAD-2021.01.app/Contents/MacOS/OpenSCAD`).

---

### Task 1: параметры и проверки
`case.scad`, блок «Замерить»: `mic_*`, `lin_*`, `tab_*`; производные; `assert` на стакан против реек IP5306 и отверстия MIDI, язычок LINE против гнезда PHONES и угловой стойки; `echo` положений.

### Task 2: верх
`top_shell()`: вырез + пазы на внутренней половине стенки под оба язычка, гравировки `MIC` (сзади) и `LINE` (спереди) на высоте подписи MIDI.

### Task 3: низ
`bottom_lid()`: язычки (наружная часть уже на 2 × зазор, гребни на внутренней половине), стакан и решётка MIC, отверстие M6 и рёбра LINE.

### Task 4: сборка и STL
Макеты капсюля и гнезда в `assembly()`. Рендер `case8_top.stl`, `case8_bottom.stl` (и `case_*` — проверить, что вариант без кнопок не изменился по echo). Без ошибок CGAL, assert-ы проходят.

### Task 5: доки
`enclosure/README*.md`, `docs/wiring*.md`, `docs/bom*.md`: капсюль 9767, PJ-392 LINE IN, пады шилда, экранированный провод, 33–100 пФ, пористый скотч, ткань.

Коммитов нет (пользователь коммитит сам).
