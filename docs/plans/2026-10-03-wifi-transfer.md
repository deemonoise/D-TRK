# Wi-Fi передача файлов и OTA — план реализации

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** режим Wi-Fi во вкладке FILE: веб-страница для файлов `/midi` и `/projects`, обновление прошивки (веб и ArduinoOTA).

**Architecture:** чистые правила имён/размеров/JSON — в `lib/core` (native-тесты). Сеть (`src/net`) опрашивается из `App::tick` UI-задачи, поэтому картой владеет одна задача. Диалог `WifiDialog` живёт во вкладке FILE, как `ImportDialog`.

**Tech Stack:** Arduino-ESP32 core 3.x (WiFi, ESPmDNS, WebServer, Preferences, Update, ArduinoOTA), LovyanGFX, Unity.

Дизайн: [2026-10-03-wifi-transfer-design.md](2026-10-03-wifi-transfer-design.md). Коммитов нет (только по просьбе).

---

### Task 1: `file_rules` (lib/core) + тесты

**Files:** create `lib/core/src/file_rules.h/.cpp`, `test/test_file_rules/test_main.cpp`.

API (`namespace mt`):
- `enum class WebDir { Invalid, Midi, Projects }`, `parseWebDir(const char*)`, `webDirPath(WebDir)` → `"/midi"`, `"/projects"`.
- `projectBaseValid(name)` — 1..16 символов `[A-Za-z0-9_-]`.
- `webFileAllowed(dir, name)` — печатные ASCII, ≤63, без `/ \ : * ? " < > |`, без ведущей `.`, без `..`; projects: `.mtp`/`.bak` + валидная база; midi: `.mid` (регистр не важен), база 1..31.
- `webMaxBytes(dir, name)` — mtp/bak 256 КБ, mid 512 КБ.
- `webRenameAllowed(dir, from, to)` — оба разрешены, расширение то же (без учёта регистра).
- `isOpenProjectFile(projectName, fileName)` — `name.mtp` без учёта регистра.
- `jsonAppendFile(buf, cap, len, name, size, first)` — `{"name":"..","size":N}` с экранированием `"` и `\`; false при нехватке места.
- `webFirmwareName(name)` — оканчивается на `.bin`.

Тесты: каждая функция, граничные длины, запрещённые символы, переполнение буфера JSON.

Run: `pio test -e native -f test_file_rules` → PASS.

### Task 2: storage

- `validName` → `mt::projectBaseValid`.
- `Result installProject(const char* tmpPath, const char* fileName)`: проверка `readFile` во временный Project; для `.mtp` старый файл в `.bak`; rename с откатом. `.bak` ставится без ротации.

### Task 3: клавиатура, текстовый режим

`open(title, initial, onOk, bool text = false)`; `kMaxText = 63`; раскладка строится в `keys_[]`; в текстовом режиме буквы по умолчанию строчные (Shift — заглавные), нижний ряд CANCEL(4) / страница `#+=`/`ABC`(2) / OK(4); страница символов (`!@#$%^&*()`, `-_=+[]{}\|`, `;:'",.<>/?`, `` ` `` `~` SPACE(6) DEL(2)). Длинный текст — видно хвост.

### Task 4: `src/net/wifi.h/.cpp`

`Creds{ssid[33], pass[64]}`, `loadCreds/saveCreds` (Preferences `"wifi"`), `scan(names, max)` (dedup, по RSSI), `connect(creds)`, `link()` → `Off/Connecting/Up/Failed`, `ip(buf)`, `off()`.

### Task 5: `src/net/web_page.h`

Одна страница (русский, без CDN): секции MIDI и Проекты (drop-зона, прогресс, список, скачать/переименовать/удалить, 409 → «заменить?»), секция Прошивка.

### Task 6: `src/net/web.h/.cpp`

`webBegin(Hooks)`, `webPoll()`, `webEnd()`. Обработчики `/`, `/api/list`, `/api/file`, `/api/upload`, `/api/rename`, `/api/delete`, `/api/update`. Загрузка в `/~upload.tmp`, лимит размера (413), нет места/ошибка записи (507), 409, проверка `.mtp` (422). ArduinoOTA (hostname `tracker`) + mDNS-сервис http. Hooks: лог, busy-сообщение, «тронут файл открытого проекта».

### Task 7: App

`Screen::poll()` (вызов в `tick`), `App::lockTransport(bool)`: Play и тап по транспорту дают toast «WI-FI MODE».

### Task 8: `src/ui/wifi_dialog.h/.cpp`

Состояния: Scan-меню → пароль → Connecting (15 с) → Online / Failed. Лог 7 строк. Кнопки EXIT, NETWORK. Выход: `webEnd`, `wifi::off`, разблокировка транспорта, меню KEEP CURRENT / RELOAD FROM CARD или toast.

### Task 9: FileScreen

Пункт `Wi-Fi...` перед `Retry`, `kActionH = 34`. Вход: dirty → меню Cancel / Save & continue (неактивно у untitled) / Continue without saving. Делегирование ввода/рисования, `poll`, `wantsRedraw`, закрытие в `onLeave`.

### Task 10: `platformio.ini`

`[env:wt32-ota]` `extends = env:wt32`, `upload_protocol = espota`, `upload_port = tracker.local`.

### Task 11: проверка

`pio test -e native` → все PASS; `pio run -e wt32` → SUCCESS.

### Task 12: документация

README: раздел «Wi-Fi» + безопасность + OTA; `docs/manual.html`: подраздел в FILE; память `project-status`.
