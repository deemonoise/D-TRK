# Больше LFO dest + LFO на LFO — дизайн

Сейчас у 4 LFO 9 целей: PITCH, 5 макро (DECAY..CONTOUR / SHP1..SENV), VOL, CUTOFF, DRIVE. Нужно —
почти любой параметр синта и модуляция одного LFO другим (rate, depth, retrig).

## Подход
Расширяем `LfoDest`: новые цели дописываются в конец enum (перед Count), диспетчер в `Synth::control`
растёт. Формат файлов не меняется (dest — байт, кодек отсекает `>= Count`; старая прошивка прочтёт
новые dest как PITCH — только при откате). Универсальная матрица (смещение поля + диапазон) отвергнута:
параметры читаются во многих местах с учётом locks, понадобилась бы копия `Instrument` на голос.

Модулируются только непрерывные параметры, слышимые посреди ноты. ADSR-времена, sample start, glide —
нет (читаются на note-on).

## Цели (после DRIVE, по порядку)
| Dest | Типы | Полная глубина (depth ±63 ≈ ±1) |
|---|---|---|
| RESO | все | ±64 к 0..127 (после RES lock), clamp |
| FENV | все | ±64 к fenv (-64..63), clamp |
| DLY | все | ±64 к delay send (после DLY lock) |
| RVB | все | ±64 к reverb send (после RVB lock) |
| BIT | все | ±64 к crush bits (после BIT lock) |
| SRR | все | ±64 к crush rate (после SRR lock) |
| FINE | все | ±1 полутон |
| DUTY | CHIP | ±49 % ширины импульса |
| SUB | SYNTH | ±64 к synSub |
| NOISE | SYNTH | ±64 к synNoise |
| SEMI2 | SYNTH | ±24 полутона к osc 2 |
| L1..L4 RATE | все | частота цели × 2^(±4) (FREE и TEMPO) |
| L1..L4 DEPTH | все | глубина цели × clamp(1 + l, 0, 2), как VOL |
| L1..L4 RTRG | все | цикл источника (фаза через 1) сбрасывает фазу цели в 0 и даёт новое RND; важно лишь depth ≠ 0 |

Чужие типу цели энкодер Dest пропускает: `lfoDestStep(dest, d, type)` вместо флага `macros`
(DECAY..CONTOUR — FM / DRUM / SYNTH, DUTY — CHIP, SUB / NOISE / SEMI2 — SYNTH). LFO не выбирает
L-цели самого себя для RTRG (бессмысленно); RATE / DEPTH на себя — можно. `instrSetType` переводит
невалидный для нового типа dest в PITCH (обобщение нынешнего правила для макро).

## LFO на LFO
Голос хранит выходы своих LFO с прошлого control tick (`lfoOut[kLfos]`, -1..1 × depth/64). Rate- и
depth-модуляции считаются из них: задержка 1 мс, зато порядок не важен — циклы и самомодуляция по RATE
работают. Несколько источников на одну цель суммируются (RATE — сумма в октавах, DEPTH — сумма в
множителе до clamp). RTRG: флаги wrap источников этого tick применяются к целям.

Retrig OFF (общая фаза инструмента, `instLfoPhase_`):
- DEPTH-модуляция применяется к выходу — работает всегда.
- RATE и RTRG общей цели — только от общих (Retrig OFF) источников; считаются в `advanceInstLfos` по
  хранимым выходам инструмента (`instLfoOut_`). Per-voice источники для общей цели игнорируются (у
  голосов разные фазы).

## UI
Строка Dest та же. Имена: `RESO FENV DLY RVB BIT SRR FINE DUTY SUB NOISE SEMI2 L1 RATE .. L4 RATE
L1 DEPTH .. L4 DEPTH L1 RTRG .. L4 RTRG`; у SYNTH макро-имена свои (SHP1..SENV).

## Проверка
native: каждая новая цель двигает своё значение; L RATE меняет скорость цели; L DEPTH при -1 глушит
цель; RTRG сбрасывает фазу на wrap источника; общая цель игнорирует per-voice RATE / RTRG и принимает
общий; `lfoDestStep` пропускает чужие типу цели; `instrSetType` сбрасывает невалидный dest в PITCH;
кодек — round-trip новых dest. Мануал: таблица целей LFO, абзац про LFO на LFO. Железо: L2 RTRG от
медленного SAW, L1 RATE от второго LFO, DLY / RVB «качаются».
