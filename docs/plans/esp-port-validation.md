# Проверка переноса на ESP32-S3 — 2026-11-08

Первоначальная проверка первой волны описана ниже; актуальные результаты второй волны — в последнем разделе.

Перенос выполнен в рабочем дереве `feature/teensy-port2`, исходная точка `e7efe3b`, источник Teensy `4000b2e`. Коммитов и push не было.

## Перенесённые блоки

1. Сохранения с backup, проверкой временных файлов, восстановлением и копированием ресурсов до публикации проекта.
2. GRID: motion recording, автоматизация, quantize, rotate/duplicate/double, velocity, count-in, метроном и ONE PASS.
3. SONG TRN/BPM и имена паттернов; сохранение основного BPM и правильная длительность офлайн-рендера.
4. Общие браузеры, память папок/курсоров, preview, аккорды навигации и контекст дорожки/инструмента.
5. Исправления компрессора/sidechain, delay/reverb, сглаживания, sample loops и нагрузки затухающих голосов.
6. DICE с шестью ролями, фразами, формами, плотностью, MIDI library, Undo и 557 совместимыми пресетами.

Подробности и ограничения: [дополнение к руководству](esp-port_ru.md).

## Проверено на компьютере

- Полный `pio test -e native`: **2057/2057**, без неуспешных наборов.
- Host-проверки с ASan/UBSan: файловые транзакции (12 сценариев), непропадающая замена проекта/полная очередь/count-in, политика диалога DICE. Дополнительно проверяется подготовка FM-пробы (7 Python-тестов).
- `pio run -e wt32 -e esp32s3-headless`: обе сборки успешны.
- clangd: AST для `sequencer.cpp`, `dice.cpp`, `synth.cpp` без диагностик; проверка инструментальных действий ограничена первыми 20 строками каждого файла.
- `git diff --check`: без ошибок.

В регрессиях отдельно проверены: неудачные rename и восстановление backup; непубликация проекта при ошибке ресурсов; сохранение новых полей; смена проекта во время count-in; старые проекты/пресеты; перенос TRN/BPM при правках SONG; метроном на 32 кГц; отсечённые хвосты голосов и отсутствие старого PCM при повторном использовании голоса; линейная калибровка мастер-тракта. Прежние 64 Virus ARP сохраняют индексы 42–105.

WT32: статическая RAM **97 924 байта**, приложение **2 050 287 / 3 145 728 байт**. Это отчёт линковщика, а не измерение runtime heap/PSRAM и не оценка CPU.

Логи текущей сессии:

- [native](/private/tmp/dtrk-port-native-final.log)
- [сборки](/private/tmp/dtrk-port-builds-verified.log)
- [host](/private/tmp/dtrk-port-host-verified.log)
- [clangd](/private/tmp/dtrk-port-clangd)

## Quality-gates

Установленные Unclebob-инструменты не поддерживают C++; вызовы для ядра вернули `No source files to analyze/mutate`. C++ CRAP/duplication/mutation здесь **не измерены**, а не объявлены пройденными.

Для Python-скрипта подготовки FM-пробы применимые проверки пройдены: crapper — 100% покрытия семи функций, максимальный CRAP 4 при пороге 10; dryer — без дубликатов при пороге 0,8; mutator — 20 killed, 0 survived, 0 uncovered.

Логи: [crapper](/private/tmp/dtrk-fm-crapper.log), [dryer](/private/tmp/dtrk-fm-dryer.log), [mutator](/private/tmp/dtrk-fm-mutator.log).

## Аппаратные замеры и новый FM

Замеры выполнены на ESP32-S3 rev 0.2, 240 МГц, flash 16 МиБ / octal PSRAM 8 МиБ: **45 сценариев baseline, 45 текущего синта и 140 нового FM**. Все прогоны завершились DONE; у нового FM все голоса активны и PCM конечный/ненулевой.

[Подробный отчёт, ограничения и сырые данные](esp32s3-performance-2026-11-08.md).

Перенос нового FM возможен с отдельными CPU-ценами и ограничениями полифонии; прежние восемь тяжёлых голосов не подходят. Особенно дорог CHORD: 2001 мкс на один и 3996 мкс на два голоса только ядром при бюджете 4000 мкс на блок. По памяти ориентировочная прибавка к пулу 24 голосов — 3840 байт internal RAM.

N16R8 не заменяет проверку полной WT32 N16R2 с экраном, quad PSRAM, SD и I2S. Прежние цены движков ESP не перекалиброваны; во второй волне добавлен отдельный резерв для матрицы. FIR-антиалиасинг SAMPLE и новый FM в основной синт не включены. На отладочной ESP оставлен текущий headless-стенд; другие USB-устройства не использовались.

## Вторая волна: sample tools, WT, PERF и MATRIX

Дополнительно перенесены шесть согласованных блоков: extract/normalize/float32 WAV; BPM/onset/128 slices; offline stretch; sample→wavetable/DRAW; PERF без триолей; 8-route MATRIX. Источник матрицы `3e72836`, PERF `7d202b1`. Нового FM по решению пользователя нет.

Финальное ядро: **2190/2190 native-тестов**, включая девять проверок цены матрицы и live-edit перераспределения голосов. Хостовый `unittest discover -s scripts/tests`: **12/12**, включая ASan/UBSan для 16 sample-job сценариев, 7 WT staged-readback/rollback сценариев и 12 прежних storage-транзакций. Дополнительный прогон sample/WT core: 96 ASan/UBSan тестов; matrix extreme RATE подтверждён UBSan float-cast-overflow (RED до исправления), после исправления 10 matrix и 315 synth тестов проходят.

Проверены старые ESP-пресеты, классический FM, v12/E8, SLX1/MODM в разном порядке, неполные/повреждённые чанки, сохранение SONG/BPM/names, отсутствие триолей в PERF и повторное сохранение их старых ID после нормализации. Новые Teensy FM family presets/projects отклоняются вместо неверной интерпретации machine ID.

WT32 `pio run -e wt32` проходит: статическая RAM **97 924 / 327 680 байт**, приложение **2 101 391 / 3 145 728 байт**. Динамические буферы sample/WT/PERF в это число не входят. На момент этих стендовых проверок WT32 ещё не прошивалась; последующая OTA-проверка описана ниже.

Независимое ревью обнаружило RATE overflow при сложении legacy/matrix и обход WT payload verification через cache hit. Оба случая исправлены и проверены регрессионными тестами; финальная проверка дополнительно выявила пропуск резерва матрицы на старых голосах после смены типа инструмента. Учёт source/LFO выровнен с runtime, регрессия проверяет освобождение голосов без смены цены звучащего движка. Также общая проверка выявила и исправила буфер SLX1 reader и маршрутизацию raw touch в DRAW.

clangd: AST ядра sample/WT/codec/matrix/PERF/synth без диагностик. Проверка refactoring actions ограничена первыми 20 строками (matrix — 9); полный token-action check выдаёт внутренние ошибки ExtractFunction для break/continue, поэтому его успешным не считаем. ESP UI проверен компилятором Xtensa, не native clangd.

Unclebob crapper/dryer/mutator повторно вызваны для C++: `No source files to analyze/mutate`. Эти C++ gates **неприменимы и не измерены**. Новый Python в этой волне — только тестовые runner-файлы. Для production `tools/fm_probe/prepare.py` применимые gates повторены: max CRAP 4, функции покрыты на 100%, dryer без дубликатов, mutator 20 killed / 0 survived / 0 uncovered. [CRAP](/private/tmp/dtrk-wave2-python-crapper.log), [dryer](/private/tmp/dtrk-wave2-python-dryer.log), [mutator](/private/tmp/dtrk-wave2-python-mutator.log).

Логи:

- [native](/private/tmp/dtrk-wave2-native-complete.log), [host](/private/tmp/dtrk-wave2-host-complete.log), [WT32](/private/tmp/dtrk-wave2-wt32-complete.log).
- [sample/WT core ASan](/private/tmp/dtrk-sample-core-tests.log), [sample jobs](/private/tmp/dtrk-sample-jobs-final.log), [WT fault tests](/private/tmp/dtrk-wt-source-green.log).
- [UBSan synth](/private/tmp/editor-rate-host/test_synth.log), [clangd synth](/private/tmp/dtrk-wave2-clangd-synth-limited.log).
- [crapper](/private/tmp/dtrk-wave2-crapper.log), [dryer](/private/tmp/dtrk-wave2-dryer.log), [mutator](/private/tmp/dtrk-wave2-mutator.log).

[Вторая волна: стендовый отчёт и ограничения](esp32s3-wave2-performance-2026-11-08.md). Физическое отключение питания, SD и LCD/touch/I2S на устройстве в этом прогоне не проверялись; их проверяем на полной WT32 после прошивки.


## OTA на пользовательскую WT32

Обычный образ `wt32` (`qio_qspi`, 16 MB), с первой кнопкой пользовательского экземпляра на P12/bit10, принят по `http://192.168.3.221/api/update`: HTTP 200 / OK. SHA-256 `f81cfa341fb5544ab7c4fb9f02902eea2f62496e2bf4846aabe371c1b1b6ec3c`. NVS и partition table через этот OTA endpoint не записывались; factory image не использовался. Аппаратная поправка pins.h после сборки возвращена к опубликованной схеме, персональный образ и ELF сохранены в `/private/tmp/dtrk-wt32-ota-2026-11-08/`.

После штатной перезагрузки пользователь повторно открыл FILE → Wi-Fi transfer, устройство доступно на прежнем IP. Проверено: HTTP, запись нового E8/v12 пресета 544 байта, его чтение с точным сравнением байтов и удаление. Принятие E8/v12 подтверждает работу обновлённого preset loader. Временный файл удалён, прежний crashlog не изменился. Это подтверждает Wi-Fi/SD file transfer после OTA, но не заменяет прослушивание и проверки всех новых функций на экране.

[OTA manifest и результаты проверки](bench/wt32-ota-2026-11-08.json).


## Перенос экранов Teensy

[Карта переноса UI и OTA-проверки](teensy-ui-port-2026-11-08.md): 2217 native-тестов, 13 host-проверок (включая 12 real-UI сценариев ASan/UBSan), личная сборка WT32/P12 и принятая OTA. Новые BLE/REC/расширенный export backends не входят в UI-волну.
