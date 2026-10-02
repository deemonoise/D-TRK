# Корпус MIDI Tracker — дизайн и план

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** параметрическая модель корпуса для 3D-печати (OpenSCAD) + STL.

**Architecture:** один файл `enclosure/case.scad`, все размеры — переменные в начале. Переменная `part` выбирает деталь (`top`, `bottom`, `clamp`, `assembly`). STL рендерятся OpenSCAD CLI.

**Tech Stack:** OpenSCAD 2021.01 (`/Applications/OpenSCAD-2021.01.app/Contents/MacOS/OpenSCAD`).

---

## Дизайн (утверждён 2026-10-02)

- Плоская коробка ~98×105×27 мм. Сверху модуль WT32-SC01 Plus (92×60) целиком: окно 90×58, бортик 1 мм по краю рамки (`lip`, `lip_t` — переменные), снизу карман 92,4×60,4.
- Под экраном ряд: Shift (MX), Play (MX), энкодер EC11. MX — вырез 14×14, панель локально 1,5 мм. EC11 — отверстие 7 мм.
- Модуль прижат снизу 2 планками (поперёк коротких краёв) на винтах M3 в вплавляемые гайки в стойках верхней части; под планками пористый скотч (`foam_t`).
- Верх — ванна со стенками; низ — плоская крышка на 4 винтах M3 с потайной головкой в вплавляемые гайки в угловых стойках.
- Задняя стенка: Type-C модуля IP5306 (прорезь + раззенковка под штекер), mini jack панельный M6, мини-тумблер MTS-102 (M6).
- На крышке: бортик под аккумулятор 103450 (на скотче), ложемент IP5306 у задней стенки.
- Печать без поддержек: верх лицом на стол, крышка и планки плашмя.
- Питание: LiPo → IP5306 (зарядка + 5 В, USB-A выпаян) → тумблер → 5V/GND платы. Изначально было TP4056 + MT3608, заменено 2026-10-02.

## Task 1: модель

**Files:** Create `enclosure/case.scad`.

Параметры, модули `top_shell()`, `bottom_lid()`, `clamp()`, `assembly()`; `part` = переключатель.

## Task 2: рендер и проверка

Run:
```
OS=/Applications/OpenSCAD-2021.01.app/Contents/MacOS/OpenSCAD
$OS -o enclosure/stl/case_top.stl    -D 'part="top"'    enclosure/case.scad
$OS -o enclosure/stl/case_bottom.stl -D 'part="bottom"' enclosure/case.scad
$OS -o enclosure/stl/clamp.stl       -D 'part="clamp"'  enclosure/case.scad
```
Expected: без ошибок и предупреждений CGAL, STL не пустые, габариты совпадают с расчётом. PNG-превью сборки — визуальная проверка коллизий.

## Task 3: README

**Files:** Create `enclosure/README.md` — крепёж, порядок сборки, что замерить перед печатью, схема питания. Ссылка из корневого README.

Коммитов нет (по просьбе пользователя).
