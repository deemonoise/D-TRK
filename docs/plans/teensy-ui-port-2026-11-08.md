# Перенос интерфейса Teensy на WT32, 2026-11-08

Источник: свежий `git fetch` подтвердил `origin/feature/teensy` = `3e72836b1a2d03bd8a32d48d84d43fcb4545220f`. Для сравнения создан отдельный read-only checkout `/private/tmp/dtrk-ui-teensy-reference`; текущие две волны ESP сохранены в `/private/tmp/dtrk-ui-wave-baseline`. Изменено 42 исходных файла относительно этой точки, включая персональную карту кнопки в сборке.

Перенесён интерфейс доступных функций ESP. Новые бэкенды BLE MIDI, записи внешнего аудио/REC→SD, расширенного экспорта stems/24-bit и Teensy UART/synth-update в эту волну не включены; неработающих кнопок для них нет. Сохранены прежние восемь FM-машин, KIT8 и 32 кГц. Пользователь разрешил сразу прошить проверенную сборку по OTA.

| Область | Итог |
|---|---|
| FILE | PROJECTS / SAMPLES / SYSTEM; Wi-Fi transfer в SYSTEM; Export вместо Render WAV. Diagnostics — отдельный вложенный список, B возвращает в SYSTEM с прежним курсором. |
| SYSTEM | Память/минимум internal RAM, карта и sample cache показаны локальными ESP API. Wi-Fi остаётся полноценной передачей файлов и прошивки, с прежними RAM guards и reload-handling. |
| BANK | Имена паттернов на плитках и Rename через клавиатуру с Undo; отмена и нехватка памяти сохраняют исходное имя. |
| PROJ | MAIN и PAT разделены; порядок MAIN / PAT / DELAY / REVERB / MOD / MASTER / PERF, подсказки неактивных параметров. |
| INST | Вложенные SAMPLE Render / Slices / Convert / Presets / Mutate; сила мутации, Apply и Undo; подписи M01–M08 и пояснения выключенных маршрутов. |
| SYNTH | MODEL C4: OSC1/OSC2/MIX и MOD OUT, HOLD/LIVE; не чаще 10 Гц, окно 16 мс при 32 кГц. Используется существующий SynVoice, без захвата аудиопотока и новых DSP-полей. |
| WT / presets | DRAW: Edit current / New wave / New morph. Список освобождается перед документом и возвращается при отмене. PRESETS / AUDITION, APPLY / DELETE. Локальные jobs и staged/CRC save сохранены. |
| GRID | Повторный тап GRID переключает Overview / Detail / Roll / Automation. Signed M01–M08 корректно показаны и редактируются по графику; dim-подписи пустых KIT8 lanes и контекстные Value/Offset/Start/Variation. |
| SONG | MUTES / TRN BPM в заголовке и переключение тапом; номер паттерна/слота и count в меню, вложенное подтверждение Clear, PERF badge. |
| Диалоги | Import / Bounce / Export и операции GRID используют постоянные нижние кнопки вместо action-строк в прокручиваемом списке. |
| Общая навигация | Дочерние меню возвращаются к родителю; raw touch остаётся у документа/XY, получившего Down. Смена вкладки/проекта/открытие меню не передаёт хвост жеста другому экрану. Play в DRAW сохраняется за документом и при открытом меню. |

TRACK/MIX/SCENE, FILL, DICE, XY, темы, клавиатура и основные браузеры уже соответствовали доступному ESP-поведению. Сохранены полезные ESP Pattern ×2, Undo dice, восьмисекундный preview, безопасный жизненный цикл preview PCM и остановка транспорта перед офлайн-операциями.

## Проверки

- Полный native: **2217/2217** — `/private/tmp/dtrk-ui-native-final.log`.
- Host: **13/13 unittest** — `/private/tmp/dtrk-ui-host-final.log`, включая **12 сценариев настоящего FILE/BANK/dialog/Menu/Keyboard/ParamList UI** под ASan/UBSan. Подменены только App/hardware/storage boundaries; проверяются input/touch, rendered labels, cancel, Undo, возврат из Diagnostics и вызов callback после закрытия.
- Raw gesture capture: 4 RED → 4 GREEN. MODEL preview: 11 focused ASan/UBSan/float-cast-overflow случаев, затем полный native. PROJ pages, sliceNote, signed automation и KIT timing покрыты отдельными тестами.
- Ревью выявило регрессию KIT re-hit: перенос upstream timing не позволял перезаписать NDG существующей lane. Исправлено: только добавление новой lane сохраняет первый timing, повторная запись той же lane обновляет его как раньше. Два RED regression → 5/5 GREEN.
- Независимое ревью navigation, memory/pointer lifetime и MODEL C4 не оставило подтверждённых P1/P2.
- Персональная WT32/N16R2 `qio_qspi` сборка прошла. Статическая RAM **97 924 / 327 680**, приложение **2 114 759 / 3 145 728 байт**. App и новый frame/model находятся в PSRAM; MODEL render использует 512 Б scratch плюс вложенные вызовы/локальные параметры, не 512 Б всего stack. Таблицы WT не копируются и не удерживаются между кадрами.
- clangd: новые pure/UI helpers и preview/slices — AST без ошибок. Внутренние refactoring tweaks ExtractFunction/DefineOutline отключены, они не являются parser diagnostics.
- Unclebob C++ не поддерживает: CRAP/duplication/mutation для него **не измерены**. Для production Python `tools/fm_probe/prepare.py` gates повторены: функции 100%, max CRAP 4, dryer без дубликатов, mutator 20 killed / 0 survived / 0 uncovered. Логи `/private/tmp/dtrk-ui-python-{coverage,crapper,dryer,mutator}.log`.

Сборка пользователя учитывает первую кнопку P12/bit10; исходный pins.h после сборки возвращён к опубликованной схеме. Образ/ELF/точечный UI diff и source SHA-256 сохранены в `/private/tmp/dtrk-wt32-ui-ota-2026-11-08/`.

Физические LCD/touch, конкуренция UI/аудио и все ручные сценарии на N16R2 не эмулировались. После OTA проверяется возврат Wi-Fi и передача файлов; остальные экраны пользователь проверяет на устройстве.


## OTA

Персональный образ 2 191 776 байт принят `192.168.3.221` через `/api/update`: HTTP 200 / OK. SHA-256 `90c368f5a3d1a2d37d48974bd2d0c7956a42c53b0b26290ceb2754ea87d9f719`. Factory image и erase flash не использовались. После перезагрузки Wi-Fi штатно выключен; его повторное открытие находится в FILE → SYSTEM → Wi-Fi transfer. Контроль runtime ожидает этого действия пользователя.

[OTA/source manifest](bench/wt32-ui-ota-2026-11-08.json).
