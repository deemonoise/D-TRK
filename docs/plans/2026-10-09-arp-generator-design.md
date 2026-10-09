# FILL: генератор арпеджио (ARP) — дизайн

Арп в духе Access Virus, но не живой, а записанный нотами на дорожку: режимы, октавы,
ритмические паттерны (factory + свои), velocity/длина/слайд по шагам. Данные паттернов Virus
не документированы — свой набор рисунков для trance/techno/dnb/house/acid и т.д.

## 1. Встраивание
Fill-диалог получает первую строку `Type: FILL / ARP`; ARP показывает свой набор строк.
Live preview, OK/Cancel, undo — общие. Ядро — чистый модуль `lib/core/src/arp_gen.{h,cpp}`
(native-тесты `test/test_arp`). `ArpSpec` живёт в GridScreen рядом с `fillSpec_` (RAM).

## 2. Паттерн
Длина 1–32, шаг 16 бит:
- тип: NOTE / REST / TIE (продление предыдущей ноты)
- акцент: GHOST / NORM / ACC
- длина: SHORT / NORM / LONG
- slide-флаг
- высота: NEXT / REPEAT (та же нота) / ROOT (нижняя нота аккорда, «педаль») / +OCT / −OCT

## 3. Параметры ARP
- **Source**: CHORD (Root + тип из таблицы CHD, диатонично ладу проекта) / SELECTION
- **Dest**: дорожка, по умолчанию первая дорожка выделения
- **Mode**: UP, DOWN, UP/DN, DN/UP, PLAYED, RANDOM, CONVERGE, DIVERGE, PEDAL, CHORD (нота + CHD)
- **Octaves** 1–4, **Pattern**, **Rate** ×1–×4 (шагов дорожки на шаг арпа), **Rotate**
- **Gate %** (масштаб SHORT/LONG), **Swing** (NDG на нечётных шагах арпа)
- **Vel Lo / Vel Hi**: GHOST = Lo, ACC = Hi, NORM = середина
- **Slide** — время SLD
- **Roll %** — случайный RAT 2–4 на нотах; **Ghost PRB** — PRB на ghost-нотах
- **Mutate %** + Seed / Reseed — гасит/добавляет удары, меняет акценты и высоты
- **Capture...**, OK / Cancel

## 4. SELECTION
На каждом шаге диапазона ноты всех дорожек выделения (+ раскрытый CHD) задают новый «зажатый»
аккорд до следующего шага с нотами; OFF в исходнике — арп молчит до новых нот. Порядок PLAYED —
по дорожкам, затем по CHD. Источник читается из снимка на момент open(), поэтому Dest внутри
выделения перезаписывается корректно. Индекс арпа идёт непрерывно сквозь смены аккордов.
CHORD-режим при SELECTION: нижняя нота + CHD триада.

## 5. Запись в Dest
На шагах диапазона заменяются note/vel и «арповые» fx (GAT, TIE, SLD, NDG, RAT, PRB, CHD, ARS,
ARP); остальные локи (FLT, DLY…) остаются. Длина: SHORT/LONG → GAT, NORM → без fx (Gate
дорожки). TIE-шаги → TIE на ноте + OFF на первой паузе после. Slide → SLD на ноте и LONG у
предыдущей. Нет свободного слота — fx пропускается. Preview/restore/undo покрывают выделение
и Dest.

## 6. Capture
Снимает Dest-диапазон (≤ 32 шагов) в паттерн: нота → NOTE, velocity → 3 уровня, GAT → длина,
SLD → slide, TIE/пустые после TIE → TIE, OFF/пусто → REST; та же высота → REPEAT, нижняя нота
диапазона → ROOT, иначе NEXT. Имя с клавиатуры, `/presets/ARP/NAME.arp`; в списке Pattern
user-паттерны идут после factory.

## 7. Factory (~40)
BASIC (16ths, 8ths, triplets, dotted), TRANCE (gate 16ths, offbeat, rolling, uplifter, psy с
педалью), TECHNO (stab, hypnotic 3-над-4, minimal), DNB (roll, stab, синкопы), HOUSE (offbeat,
organ), ACID (3 рисунка со slide/acc), ELECTRO / BREAKS, SYNTHWAVE (8ths с октавой), DUB
(stab + ghost-эхо), CHIP (быстрые октавы).

## Проверка
native-тесты: порядок нот всех режимов, TIE/OFF/SLD/GAT, смены аккордов SELECTION, Dest внутри
и вне выделения, детерминизм по seed, round-trip Capture → apply. Сборка wt32; на железе —
чек-лист в плане.
