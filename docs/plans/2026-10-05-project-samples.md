# Project Samples Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.
> По правилу пользователя шаги Commit пропускаются (коммит — только по просьбе).

**Goal:** Сэмплы принадлежат проекту: список в `.mtp`, WAV в `/projects/NAME/`, флеш-банк — кэш по CRC.

**Architecture:** Записи банка именуются hex CRC32 данных (`%08x`). `Project` хранит список
`{name, crc, frames}`; синт резолвит имя → crc → запись банка. Save синхронизирует папку проекта
(экспорт из флеша), load — подтягивает недостающее из папки с вытеснением чужих записей. Чистая
логика — в `lib/core` (native-тесты), железо — `src/audio/bank.cpp`, `src/storage/storage.cpp`.

**Tech Stack:** C++17, PlatformIO (`native` для тестов, `wt32` для прошивки), Unity.

Дизайн: `docs/plans/2026-10-05-project-samples-design.md`.

Команды:
- тесты: `pio test -e native` (один: `pio test -e native -f test_sample_set`)
- сборка: `pio run -e wt32`

---

### Task 1: Модель — список сэмплов в Project

**Files:**
- Modify: `lib/core/src/model.h` (константы, `ProjSample`, поля `Project`)
- Modify: `lib/core/src/model.cpp` (`Project::reset`)
- Modify: `src/storage/storage.cpp:43-60` (static_assert размера, `snapshot()`)
- Test: `test/test_model/test_main.cpp`

**Step 1: тест** — после `reset()` список пуст:

```cpp
void test_reset_clears_samples() {
  static Project p;
  p.sampleCount = 3;
  strcpy(p.samples[0].name, "kick");
  p.reset();
  TEST_ASSERT_EQUAL(0, p.sampleCount);
  TEST_ASSERT_EQUAL_STRING("", p.samples[0].name);
}
```
Зарегистрировать в `main()` (`RUN_TEST`). Run: `pio test -e native -f test_model` → FAIL (нет поля).

**Step 2: реализация.** В `model.h` после `kSampleNameMax`:

```cpp
constexpr int kProjSamples = 128;  // = kBankEntries

// A sample of the project: name seen by instruments, data identified by crc32 of the int16 data.
struct ProjSample {
  char name[kSampleNameMax + 1] = {0};
  uint32_t crc = 0;
  uint32_t frames = 0;
};
```
В `Project` после `preview`:

```cpp
  ProjSample samples[kProjSamples];
  uint8_t sampleCount = 0;  // names unique ignoring case
```
В `Project::reset()` (`model.cpp`): `sampleCount = 0; for (ProjSample& s : samples) s = ProjSample{};`.

В `storage.cpp`: обновить `static_assert(sizeof(mt::Project) == ...)` на новое значение (взять из
ошибки компиляции `pio run -e wt32`), в `snapshot()` добавить
`memcpy(out.samples, live.samples, sizeof(out.samples)); out.sampleCount = live.sampleCount;`.

**Step 3:** `pio test -e native -f test_model` → PASS.

---

### Task 2: Chunk `SMPL` в `.mtp`

**Files:**
- Modify: `lib/core/src/project_io.cpp`
- Test: `test/test_project_io/test_main.cpp`

Формат: `count u8`, затем `count` записей по `kSmplSize = 24`: `name[16] crc u32 frames u32`.
Пишется всегда (в том числе count = 0). При чтении: пустое имя и дубль имени (без учёта
регистра) — запись пропускается; записи сверх `kProjSamples` — пропускаются (`readRecords`).

**Step 1: тесты**

```cpp
void test_samples_roundtrip() {
  static Project a, b;
  a.reset();
  a.sampleCount = 2;
  strcpy(a.samples[0].name, "kick");
  a.samples[0].crc = 0xDEADBEEF;
  a.samples[0].frames = 1234;
  strcpy(a.samples[1].name, "snare_longname16");  // 16 chars
  a.samples[1].crc = 1;
  a.samples[1].frames = 7;
  VecSink s;
  TEST_ASSERT_TRUE(saveProject(a, s));
  VecSource src(s.v);
  TEST_ASSERT_EQUAL(LoadErr::Ok, loadProject(src, b));
  TEST_ASSERT_EQUAL(2, b.sampleCount);
  TEST_ASSERT_EQUAL_STRING("kick", b.samples[0].name);
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, b.samples[0].crc);
  TEST_ASSERT_EQUAL(1234, b.samples[0].frames);
  TEST_ASSERT_EQUAL_STRING("snare_longname16", b.samples[1].name);
}

void test_samples_dup_and_empty_skipped() {
  static Project a, b;
  a.reset();
  a.sampleCount = 3;
  strcpy(a.samples[0].name, "kick");
  strcpy(a.samples[1].name, "KICK");
  // samples[2] has an empty name
  VecSink s;
  saveProject(a, s);
  VecSource src(s.v);
  TEST_ASSERT_EQUAL(LoadErr::Ok, loadProject(src, b));
  TEST_ASSERT_EQUAL(1, b.sampleCount);
}
```
Файл без `SMPL` (уже есть тесты со старыми файлами — проверить, что `sampleCount == 0`).
Использовать существующие в тесте `VecSink`/`VecSource` (имена сверить с файлом).

**Step 2:** Run → FAIL.

**Step 3: реализация** в `project_io.cpp`:

```cpp
constexpr size_t kSmplSize = 24;  // name[16] u32 crc u32 frames
```
Сохранение — после `AUDI`:

```cpp
  count = p.sampleCount > kProjSamples ? kProjSamples : p.sampleCount;
  if (!o.chunk("SMPL", 1 + count * kSmplSize) || !o.write(&count, 1)) return false;
  for (int i = 0; i < count; ++i) {
    uint8_t b[kSmplSize] = {0};
    memcpy(b, p.samples[i].name, kSampleNameMax);
    wr32(b + 16, p.samples[i].crc);
    wr32(b + 20, p.samples[i].frames);
    if (!o.write(b, sizeof(b))) return false;
  }
```
(`count` объявлен как `uint8_t` выше — `kProjSamples` = 128 влезает.)

Чтение:

```cpp
LoadErr readSmpl(CrcSource& in, uint32_t size, Project& p) {
  return readRecords(in, size, kSmplSize, kProjSamples, [&](int, const uint8_t* b) {
    char nm[kSampleNameMax + 1];
    memcpy(nm, b, kSampleNameMax);
    nm[kSampleNameMax] = 0;
    if (!nm[0] || projSampleFind(p, nm) >= 0) return;
    ProjSample& s = p.samples[p.sampleCount++];
    memcpy(s.name, nm, sizeof(nm));
    s.crc = rd32(b + 16);
    s.frames = rd32(b + 20);
  });
}
```
`projSampleFind` — из Task 3 (сделать Task 3 Step 3 раньше или временно локальный поиск через
`strcasecmp`). Диспетчер: `else if (memcmp(ch, "SMPL", 4) == 0) e = readSmpl(in, size, out);`.
Добавить `static_assert(kSmplSize <= kInstSize, "readRecords buffer");`.

**Step 4:** `pio test -e native -f test_project_io` → PASS.

---

### Task 3: Ядро списка — `sample_set`

**Files:**
- Create: `lib/core/src/sample_set.h`, `lib/core/src/sample_set.cpp`
- Test: `test/test_sample_set/test_main.cpp` (новый; `RamFlash` скопировать из `test_sample_bank`)

API (`sample_set.h`):

```cpp
#pragma once
#include <stdint.h>
#include "model.h"
#include "sample_bank.h"

namespace mt {

// Bank entry name for sample data: crc32 as 8 lower-case hex digits.
void sampleKey(uint32_t crc, char out[kSampleNameMax + 1]);
bool isSampleKey(const char* name);  // exactly 8 hex digits
// crc32 of frames int16 samples, little-endian bytes (as stored in flash).
uint32_t sampleCrc(const int16_t* d, uint32_t frames, uint32_t prev = 0);

int projSampleFind(const Project& p, const char* name);  // ignoring case, -1 if absent
// Adds or, for an existing name, replaces crc / frames. -1: list full or bad name.
int projSampleSet(Project& p, const char* name, uint32_t crc, uint32_t frames);
// Removes entry i; instruments keep the name (they become "missing").
void projSampleRemove(Project& p, int i);
// New name for entry i; instruments that used the old name follow. False: bad or taken name.
bool projSampleRename(Project& p, int i, const char* name);

// Bank index of project sample i (key and frames match), -1 if not cached.
int projSampleBank(const Project& p, const SampleBank& b, int i);
// True if bank entry e is used by p.
bool bankEntryUsed(const Project& p, const BankEntry& e);
// Removes bank entries not used by p, largest first, until a sample of frames fits
// (SampleBank::fits). Compacts if that is not enough. False: still no room.
bool bankMakeRoom(SampleBank& b, const Project& p, uint32_t frames);
// Removes every bank entry not used by p. Count removed.
int bankClearUnused(SampleBank& b, const Project& p);

}  // namespace mt
```

`SampleBank` получает публичный `bool fits(uint32_t frames) const;`
(`uint32_t at; return n_ < kBankEntries && firstFit(kBankHeader, span(frames), at);` — сверить
сигнатуру `firstFit` и начало области данных в `sample_bank.cpp`).

**Step 1: тесты** (по одному — `RUN_TEST` в `main`):

```cpp
void test_key() {
  char k[17];
  sampleKey(0x00ABCDEF, k);
  TEST_ASSERT_EQUAL_STRING("00abcdef", k);
  TEST_ASSERT_TRUE(isSampleKey(k));
  TEST_ASSERT_FALSE(isSampleKey("kick"));
  TEST_ASSERT_FALSE(isSampleKey("00abcdeg"));
  TEST_ASSERT_FALSE(isSampleKey("00abcdef0"));
}

void test_crc_chunks_equal_whole() {
  std::vector<int16_t> v = ramp(1000, 3);
  const uint32_t whole = sampleCrc(v.data(), 1000);
  const uint32_t part = sampleCrc(v.data() + 400, 600, sampleCrc(v.data(), 400));
  TEST_ASSERT_EQUAL_HEX32(whole, part);
}

void test_set_find_replace() {
  static Project p;
  p.reset();
  TEST_ASSERT_EQUAL(0, projSampleSet(p, "kick", 1, 10));
  TEST_ASSERT_EQUAL(1, projSampleSet(p, "snare", 2, 20));
  TEST_ASSERT_EQUAL(0, projSampleSet(p, "KICK", 3, 30));  // replace, keeps name
  TEST_ASSERT_EQUAL(2, p.sampleCount);
  TEST_ASSERT_EQUAL_STRING("kick", p.samples[0].name);
  TEST_ASSERT_EQUAL(3, p.samples[0].crc);
  TEST_ASSERT_EQUAL(1, projSampleFind(p, "Snare"));
  TEST_ASSERT_EQUAL(-1, projSampleSet(p, "", 1, 1));
}

void test_rename_follows_instruments() {
  static Project p;
  p.reset();
  projSampleSet(p, "kick", 1, 10);
  projSampleSet(p, "snare", 2, 20);
  strcpy(p.instruments[3].sample, "KICK");
  TEST_ASSERT_FALSE(projSampleRename(p, 0, "snare"));  // taken
  TEST_ASSERT_TRUE(projSampleRename(p, 0, "bd"));
  TEST_ASSERT_EQUAL_STRING("bd", p.samples[0].name);
  TEST_ASSERT_EQUAL_STRING("bd", p.instruments[3].sample);
}

void test_remove_shifts() {
  static Project p;
  p.reset();
  projSampleSet(p, "a", 1, 1);
  projSampleSet(p, "b", 2, 2);
  projSampleSet(p, "c", 3, 3);
  projSampleRemove(p, 1);
  TEST_ASSERT_EQUAL(2, p.sampleCount);
  TEST_ASSERT_EQUAL_STRING("c", p.samples[1].name);
  TEST_ASSERT_EQUAL_STRING("", p.samples[2].name);
}

// Adds data to the bank under its key; returns crc.
static uint32_t addKeyed(SampleBank& b, uint32_t frames, int seed) {
  std::vector<int16_t> v = ramp(frames, seed);
  const uint32_t crc = sampleCrc(v.data(), frames);
  char k[17];
  sampleKey(crc, k);
  TEST_ASSERT_TRUE(b.begin(k, frames, 32000, 60));
  TEST_ASSERT_TRUE(b.write(v.data(), frames));
  TEST_ASSERT_TRUE(b.commit());
  return crc;
}

void test_bank_lookup() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  static Project p;
  p.reset();
  const uint32_t c = addKeyed(b, 5000, 1);
  projSampleSet(p, "kick", c, 5000);
  projSampleSet(p, "gone", 0x1234, 10);
  TEST_ASSERT_EQUAL(b.find([&] { static char k[17]; sampleKey(c, k); return k; }()), projSampleBank(p, b, 0));
  TEST_ASSERT_EQUAL(-1, projSampleBank(p, b, 1));
  p.samples[0].frames = 4999;  // frames must match too
  TEST_ASSERT_EQUAL(-1, projSampleBank(p, b, 0));
}

void test_make_room_evicts_unused_largest_first() {
  SampleBank b(*flash);  // 1 MB flash: ~1 MB data area
  TEST_ASSERT_TRUE(b.mount());
  static Project p;
  p.reset();
  const uint32_t used = addKeyed(b, 100000, 1);      // ~200 KB, in project
  addKeyed(b, 150000, 2);                             // ~300 KB, unused
  const uint32_t small = addKeyed(b, 50000, 3);       // ~100 KB, unused
  projSampleSet(p, "keep", used, 100000);
  // Needs ~350 KB: free is ~400 KB minus the header... pick a size that only fits after one eviction.
  const uint32_t need = (b.freeBytes() + 300 * 1024) / 2;
  TEST_ASSERT_FALSE(b.fits(need));
  TEST_ASSERT_TRUE(bankMakeRoom(b, p, need));
  TEST_ASSERT_TRUE(b.fits(need));
  TEST_ASSERT_EQUAL(0, projSampleBank(p, b, 0) >= 0 ? 0 : -1);  // used one survives
  char k[17];
  sampleKey(small, k);
  TEST_ASSERT_TRUE(b.find(k) >= 0);  // the smaller unused one was not needed
}

void test_make_room_fails_when_all_used() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  static Project p;
  p.reset();
  const uint32_t c = addKeyed(b, 400000, 1);
  projSampleSet(p, "big", c, 400000);
  TEST_ASSERT_FALSE(bankMakeRoom(b, p, 400000));
  TEST_ASSERT_TRUE(projSampleBank(p, b, 0) >= 0);
}

void test_clear_unused() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  static Project p;
  p.reset();
  const uint32_t c = addKeyed(b, 1000, 1);
  addKeyed(b, 1000, 2);
  addKeyed(b, 1000, 3);
  projSampleSet(p, "x", c, 1000);
  TEST_ASSERT_EQUAL(2, bankClearUnused(b, p));
  TEST_ASSERT_EQUAL(1, b.count());
}
```
Числа в `test_make_room_*` подогнать по `capacity()` RAM-флеша при реализации — смысл теста:
неиспользуемая большая запись удаляется, используемая и ненужная маленькая остаются.

**Step 2:** `pio test -e native -f test_sample_set` → FAIL (нет файлов).

**Step 3: реализация** `sample_set.cpp`:

```cpp
#include "sample_set.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "project_io.h"

namespace mt {

void sampleKey(uint32_t crc, char out[kSampleNameMax + 1]) { snprintf(out, kSampleNameMax + 1, "%08x", crc); }

bool isSampleKey(const char* n) {
  for (int i = 0; i < 8; ++i)
    if (!((n[i] >= '0' && n[i] <= '9') || (n[i] >= 'a' && n[i] <= 'f'))) return false;
  return n[8] == 0;
}

uint32_t sampleCrc(const int16_t* d, uint32_t frames, uint32_t prev) {
  // Flash and both targets are little-endian: the bytes of d are the stored bytes.
  return crc32(d, frames * 2, prev);
}

int projSampleFind(const Project& p, const char* name) {
  for (int i = 0; i < p.sampleCount; ++i)
    if (strcasecmp(p.samples[i].name, name) == 0) return i;
  return -1;
}

int projSampleSet(Project& p, const char* name, uint32_t crc, uint32_t frames) {
  if (!name[0] || strlen(name) > kSampleNameMax) return -1;
  int i = projSampleFind(p, name);
  if (i < 0) {
    if (p.sampleCount >= kProjSamples) return -1;
    i = p.sampleCount++;
    strcpy(p.samples[i].name, name);
  }
  p.samples[i].crc = crc;
  p.samples[i].frames = frames;
  return i;
}

void projSampleRemove(Project& p, int i) {
  if (i < 0 || i >= p.sampleCount) return;
  for (int k = i; k + 1 < p.sampleCount; ++k) p.samples[k] = p.samples[k + 1];
  p.samples[--p.sampleCount] = ProjSample{};
}

bool projSampleRename(Project& p, int i, const char* name) {
  if (i < 0 || i >= p.sampleCount || !name[0] || strlen(name) > kSampleNameMax) return false;
  const int other = projSampleFind(p, name);
  if (other >= 0 && other != i) return false;
  for (Instrument& in : p.instruments)
    if (strcasecmp(in.sample, p.samples[i].name) == 0) strcpy(in.sample, name);
  strcpy(p.samples[i].name, name);
  return true;
}

int projSampleBank(const Project& p, const SampleBank& b, int i) {
  if (i < 0 || i >= p.sampleCount) return -1;
  char k[kSampleNameMax + 1];
  sampleKey(p.samples[i].crc, k);
  const int j = b.find(k);
  return j >= 0 && b.entry(j)->frames == p.samples[i].frames ? j : -1;
}

bool bankEntryUsed(const Project& p, const BankEntry& e) {
  for (int i = 0; i < p.sampleCount; ++i) {
    char k[kSampleNameMax + 1];
    sampleKey(p.samples[i].crc, k);
    if (strcmp(k, e.name) == 0 && e.frames == p.samples[i].frames) return true;
  }
  return false;
}

bool bankMakeRoom(SampleBank& b, const Project& p, uint32_t frames) {
  while (!b.fits(frames)) {
    int best = -1;
    for (int i = 0; i < b.count(); ++i)
      if (!bankEntryUsed(p, *b.entry(i)) && (best < 0 || b.entry(i)->frames > b.entry(best)->frames)) best = i;
    if (best < 0) break;
    if (!b.remove(best)) return false;
  }
  if (b.fits(frames)) return true;
  // Enough in total but fragmented: close the holes.
  return b.freeBytes() >= frames * 2 && b.compact() && b.fits(frames);
}

int bankClearUnused(SampleBank& b, const Project& p) {
  int n = 0;
  for (int i = b.count() - 1; i >= 0; --i)
    if (!bankEntryUsed(p, *b.entry(i)) && b.remove(i)) ++n;
  return n;
}

}  // namespace mt
```
Примечание: `bankMakeRoom` удаляет до тех пор, пока не влезет, но не «если всё равно не влезет»
— если после удаления всех неиспользуемых не хватает и компактирование не помогло, удалённые
записи уже потеряны (это только кэш — допустимо).

**Step 4:** `pio test -e native -f test_sample_set` и `-f test_sample_bank` → PASS.

---

### Task 4: Миграция старых проектов (ядро)

**Files:**
- Modify: `lib/core/src/sample_set.h/.cpp`
- Test: `test/test_sample_set/test_main.cpp`

API:

```cpp
// Legacy sample (old bank name) -> data identity, kept in /projects/legacy.idx on the device.
struct LegacySample {
  char name[kSampleNameMax + 1];
  uint32_t crc, frames;
};
struct LegacyIndex {
  virtual bool find(const char* name, LegacySample& out) = 0;
  virtual bool add(const LegacySample& s) = 0;
};
// For a project without a sample list (old file): every distinct sample name of its instruments is
// looked up as an old bank entry (renamed to its key, recorded in idx) or in idx; found ones are added
// to p.samples. Names not found are left to show as missing. Returns how many were not found.
int migrateSamples(Project& p, SampleBank& b, LegacyIndex& idx);
```
CRC старой записи считается по `b.data(i)` (`mapped()`), без маппинга — через чтение кусками
нельзя (у `SampleBank` нет `read`) → добавить в `SampleBank`
`bool readData(int i, uint32_t frame, int16_t* d, uint32_t n) const` через `f_.read`.
Использовать `readData` всегда (на устройстве mmap тоже есть, но единый путь проще тестировать).

**Step 1: тесты** — `MemIndex : LegacyIndex` на `std::vector`:

```cpp
void test_migrate_renames_and_records() {
  SampleBank b(*flash);
  TEST_ASSERT_TRUE(b.mount());
  std::vector<int16_t> v = ramp(3000, 5);
  TEST_ASSERT_TRUE(b.begin("Kick", 3000, 32000, 60));
  b.write(v.data(), 3000);
  b.commit();
  static Project p;
  p.reset();
  strcpy(p.instruments[0].sample, "kick");
  strcpy(p.instruments[1].sample, "KICK");  // same sample twice
  strcpy(p.instruments[2].sample, "lost");
  MemIndex idx;
  TEST_ASSERT_EQUAL(1, migrateSamples(p, b, idx));
  TEST_ASSERT_EQUAL(1, p.sampleCount);
  TEST_ASSERT_EQUAL_STRING("kick", p.samples[0].name);
  TEST_ASSERT_EQUAL_HEX32(sampleCrc(v.data(), 3000), p.samples[0].crc);
  TEST_ASSERT_EQUAL(-1, b.find("Kick"));
  TEST_ASSERT_TRUE(projSampleBank(p, b, 0) >= 0);
  TEST_ASSERT_EQUAL(1, (int)idx.v.size());
}

void test_migrate_second_project_uses_index() {
  // as above, then a second project referencing "kick": found through idx, bank unchanged
}

void test_migrate_same_data_twice_dedups() {
  // two old entries "a" and "b" with equal data: after migration one bank entry, both in idx
}
```
**Step 2:** FAIL. **Step 3: реализация:**

```cpp
int migrateSamples(Project& p, SampleBank& b, LegacyIndex& idx) {
  int missing = 0;
  for (const Instrument& in : p.instruments) {
    if (!in.sample[0] || projSampleFind(p, in.sample) >= 0) continue;
    LegacySample s{};
    const int i = isSampleKey(in.sample) ? -1 : b.find(in.sample);
    if (i >= 0) {
      const BankEntry e = *b.entry(i);
      uint32_t crc = 0;
      int16_t buf[256];
      for (uint32_t f = 0; f < e.frames; f += 256) {
        const uint32_t n = e.frames - f < 256 ? e.frames - f : 256;
        if (!b.readData(i, f, buf, n)) { crc = 0; break; }
        crc = sampleCrc(buf, n, crc);
      }
      char k[kSampleNameMax + 1];
      sampleKey(crc, k);
      const int dup = b.find(k);
      if (dup >= 0 && b.entry(dup)->frames == e.frames) b.remove(i);  // same data already cached
      else if (!b.rename(i, k)) { ++missing; continue; }
      strcpy(s.name, e.name);
      s.crc = crc;
      s.frames = e.frames;
      idx.add(s);
    } else if (!idx.find(in.sample, s)) {
      ++missing;
      continue;
    }
    projSampleSet(p, in.sample, s.crc, s.frames);
  }
  return missing;
}
```
**Step 4:** PASS.

---

### Task 5: WAV — chunk `mtcr` и запись заголовка

**Files:**
- Modify: `lib/core/src/wav.h`, `lib/core/src/wav.cpp`
- Test: `test/test_wav/test_main.cpp`

`WavInfo` + `bool hasCrc = false; uint32_t crc = 0;`. Парсер: `mtcr` (size ≥ 8: `crc u32 frames u32`)
→ `hasCrc = true` если `frames` совпадает с итоговым `frames()` (проверять после разбора: сохранить
`crcFrames` и сравнить в конце; если не совпало — `hasCrc = false`).

Писатель (порядок: fmt, smpl, mtcr, data — до data, чтобы парсер видел их без пропуска данных):

```cpp
constexpr uint32_t kWavHeaderBytes = 12 + 24 + 44 + 16 + 8;  // RIFF, fmt, smpl, mtcr, data header
// Header of a mono 16-bit WAV with root note and data crc; data (frames * 2 bytes) follows.
void wavHeader(uint8_t out[kWavHeaderBytes], uint32_t frames, uint32_t rate, uint8_t root, uint32_t crc);
```
smpl: 36 байт нулей, `MIDIUnityNote` (смещение 12 в payload) = root.

**Step 1: тест** — `wavHeader` + данные → `wavParse`: channels 1, bits 16, rate, frames, root,
`hasCrc`, crc; и что данные начинаются с `kWavHeaderBytes`. Отдельно: `mtcr` с неверными frames →
`hasCrc == false`. **Step 2:** FAIL. **Step 3:** реализация. **Step 4:** `pio test -e native -f test_wav` → PASS.

---

### Task 6: Правила Wi-Fi для папок проектов

**Files:**
- Modify: `lib/core/src/file_rules.h/.cpp`
- Test: `test/test_file_rules/test_main.cpp`

Изменения:
- `webSubValid(Projects, sub)`: `""` или один сегмент с `projectBaseValid(sub)`.
- новый `bool webFileAllowedIn(WebDir d, const char* sub, const char* name)`: для `Projects` с
  непустым `sub` — только `<base>.wav`, где base проходит `projectBaseValid` (имя сэмпла); иначе
  `webFileAllowed(d, name)`.
- `webMkdirAllowed(Projects, "", name)` — false (папка создаётся загрузкой).
- `bool projectFolderOf(const char* file, char out[17])`: для `<base>.mtp`/`.bak` даёт base.

**Step 1: тесты:**

```cpp
TEST_ASSERT_TRUE(webSubValid(WebDir::Projects, "song1"));
TEST_ASSERT_FALSE(webSubValid(WebDir::Projects, "song1/x"));
TEST_ASSERT_FALSE(webSubValid(WebDir::Projects, "bad name"));
TEST_ASSERT_TRUE(webFileAllowedIn(WebDir::Projects, "song1", "kick.wav"));
TEST_ASSERT_TRUE(webFileAllowedIn(WebDir::Projects, "song1", "KICK.WAV"));
TEST_ASSERT_FALSE(webFileAllowedIn(WebDir::Projects, "song1", "song1.mtp"));
TEST_ASSERT_FALSE(webFileAllowedIn(WebDir::Projects, "", "kick.wav"));
TEST_ASSERT_TRUE(webFileAllowedIn(WebDir::Samples, "drums", "Kick 01.wav"));
```
Шаги 2–4 как обычно (`-f test_file_rules`).

---

### Task 7: Банк на устройстве — резолв через проект, импорт в кэш, экспорт

**Files:**
- Modify: `src/audio/bank.h`, `src/audio/bank.cpp`, `src/audio/audio.cpp` (передать project)

Изменения:
1. `BankSource::find(name)`: `project_` (указатель из `audio::begin`) →
   `i = mt::projSampleFind(*project_, name)` → `j = mt::projSampleBank(*project_, theBank, i)` →
   `theBank.data(j)`. Добавить `void bankSetProject(const mt::Project*)`, звать из `audio::begin`.
2. Новый импорт вместо `importWav`:

```cpp
// Imports a WAV into the bank cache under its data key, making room by evicting entries the project
// does not use. If knownCrc is set and that key is cached, the file is not read at all.
// out: crc, frames (bank), root.
struct ImportOut { uint32_t crc, frames; uint8_t root; };
BankResult importToCache(const char* path, const mt::Project& p, ImportOut& out, const uint32_t* knownCrc,
                         BankProgressFn cb = nullptr, void* ctx = nullptr);
```
   Порядок: `openWav` → если `w.hasCrc` и ключ `w.crc` в банке с теми же frames → `out` из записи,
   `Ok` (быстрый путь; так же при `knownCrc`). Иначе: `FlashWork`, `bankMakeRoom(theBank, p, frames)`
   (`Full` при неудаче), `begin("~import", ...)`, `convertData` через sink, который пишет в банк и
   копит `sampleCrc`; `commit`; ключ уже есть → `remove(~import)`, иначе `rename(~import, key)`.
   Удалить `~import`, если остался от сбоя (перед `begin`). `Exists`/`replace` убрать.
3. `BankResult exportWav(int bankIndex, const char* path)`: `wavHeader` + `theBank.data(i)` кусками
   по 2 КБ через буфер в internal RAM (mmap-данные читать можно напрямую — запись идёт на SD).
   Писать в `path + ".tmp"`, затем rename.
4. `BankResult clearCache(const mt::Project& p)` → `bankClearUnused` под `FlashWork`.
5. `removeSample` удалить (из UI больше не зовётся); `compactBank` оставить.
6. Legacy-индекс: класс `SdLegacyIndex : mt::LegacyIndex` — текстовый `/projects/legacy.idx`,
   строки `name crc_hex frames`; `find` — линейное чтение, `add` — дописывание.
   `BankResult migrateProject(mt::Project& p, int* missing)` под `FlashWork`.

Проверка: `pio run -e wt32` собирается (UI ещё зовёт старые функции — править в Task 9; до тех пор
временно оставить старые сигнатуры или делать Tasks 7–9 подряд перед сборкой).

---

### Task 8: Storage — синхронизация папки при save, подтягивание при load

**Files:**
- Modify: `src/storage/storage.h`, `src/storage/storage.cpp`, `src/main.cpp`, `src/ui/file_screen.cpp`
  (вызовы save/load), `src/ui/wifi_dialog.cpp:63`

API:

```cpp
// Progress of sample sync: current file, bytes.
using SyncProgress = void (*)(const char* file, uint32_t done, uint32_t total, void* ctx);
// After save: writes /projects/<name>/<sample>.wav for each listed sample whose file is absent or has
// another crc (mtcr), removes other .wav files there. Missing samples (not cached) keep their files.
Result syncFolder(const mt::Project& live, SyncProgress cb, void* ctx);
// After load / autoload (engine stopped): old file without a list -> migrate; then every listed
// sample not cached is imported from the folder. *missing = samples left without data.
Result pullSamples(mt::Project& live, int* missing, SyncProgress cb, void* ctx);
```
- `save()` в конце (после `writeLast`) вызывает `syncFolder`; ошибка папки не отменяет сохранённый
  `.mtp`, а возвращается как `WriteFail` с отдельным тостом `SAMPLES NOT SAVED`.
  `save` получает параметры `cb, ctx` (по умолчанию nullptr).
- Проверка CRC существующего файла: только `wavParse` заголовка (`mtcr`); без `mtcr` — файл
  перезаписывается (дешевле, чем считать CRC с SD).
- `load()`: после замены `live` — `pullSamples`. `autoload()` (до `audio::begin`) не тянет:
  `main.cpp` после `app.begin` вызывает `storage::pullSamples(*project, &missing, ...)` с прогрессом
  через `app.showBusy`, и тост `N SAMPLES MISSING`.
- Миграция: если `live.sampleCount == 0` и у какого-то инструмента есть `sample` →
  `audio::migrateProject`.
- `wifi_dialog.cpp:63` — после `load` тоже `pullSamples` (уже внутри `load`).
- `installProject`/web: см. Task 10.

Проверка: `pio run -e wt32`.

---

### Task 9: UI — список сэмплов проекта и выбор в INST

**Files:**
- Modify: `src/ui/file_screen.cpp/.h`, `src/ui/inst_screen.cpp`

FILE → SAMPLES:
- строки: `Import WAV...`, `Compact`, `Clear cache`, затем `live.samples` (имя, длина в секундах по
  `frames / rate` из записи банка, `MISSING` при `projSampleBank < 0`).
- инфо-строка: `FREE X / Y KB  CACHE Z KB` (Z — сумма `span` неиспользуемых записей).
- меню сэмпла: Cancel / Rename / Delete. Rename — клавиатура → `projSampleRename`; Delete —
  `projSampleRemove`. Обе — только при остановленном движке (`playbackBusy()`), `app_.markDirty()`.
- импорт: имя → если есть в проекте — меню Overwrite → `importToCache` → `projSampleSet` →
  `markDirty`, корень/loop инструмента не трогаем.
- `Clear cache` → `audio::clearCache(project)`, тост `CLEARED N`.
- `usedBy()` оставить для подтверждения Delete («USED BY INST N»).

INST → Sample: перебор `live.samples` (индекс `projSampleFind`, `-1` = `---`); root/loop брать из
записи банка `projSampleBank`, если есть. `bankIndex()` → `projSampleBank(p, bank, projSampleFind(...))`.
Волна (`updateWave`) — через тот же индекс.

Проверка: `pio run -e wt32`.

---

### Task 10: Wi-Fi — папки проектов

**Files:**
- Modify: `src/net/web.cpp`, `src/net/web_page.h`

- Листинг `projects` с пустым `sub`: кроме файлов — папки, для которых `webSubValid(Projects, name)`.
- Всюду `webFileAllowed(d, name)` → `webFileAllowedIn(d, sub, name)`.
- Upload в `projects/<name>/`: папка создаётся, если нет (уже есть логика для sub — проверить).
- Rename `.mtp` → переименовать и папку `/projects/<old>` → `/projects/<new>`, если она есть и новой нет.
- Delete `.mtp`/`.bak`: если после удаления нет ни `.mtp`, ни `.bak` этого base — удалить файлы папки и
  саму папку.
- Открытый проект (`isOpenProjectFile`) — папку не трогать (как и сейчас с `.mtp`).
- Страница: папки в разделе Projects открываются как в Samples (сверить, что JS не завязан на Samples).

Проверка: `pio run -e wt32`.

---

### Task 11: Документация

**Files:** `docs/manual.html`, `README.md`

Раздел про сэмплы: проект владеет сэмплами, папка `/projects/NAME/`, перенос (`.mtp` + папка),
кэш во флеше, `Clear cache`, миграция старых проектов (сохранить один раз), MISSING.

---

### Task 12: Финальная проверка

1. `pio test -e native` — всё PASS.
2. `pio run -e wt32` — сборка без ошибок и новых предупреждений.
3. Ручная проверка на железе (для пользователя): импорт в проект A → save → new → импорт в B → load A
   (сэмплы A, без B) → скопировать папку A на другую карту / удалить кэш (`Clear cache` в B) → load A
   подтягивает с прогрессом; старый проект грузится и после save получает папку.
