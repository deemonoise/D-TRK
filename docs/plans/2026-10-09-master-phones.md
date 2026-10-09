# Master vs Phones Volume Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Master volume becomes project data (saved, rendered); a separate device "Phones" level
attenuates only the live output.

**Architecture:** Drop the NVS override of `Project::masterVol`; TRACK master edits mark the
project dirty. New `audio::setPhones(0..100)` scales the int16 block in `audio.cpp` right before
the I2S write (after the scope, so the scope still shows the mix). App keeps the phones level as a
device setting next to theme / autosave (NVS key `phones`), PROJ / SYS row "Phones".

**Tech Stack:** ESP32-S3 Arduino / PlatformIO, C++17.

Design: `docs/plans/2026-10-09-master-phones-design.md`. No commits (user rule).

---

### Task 1: Live output phones gain (audio)

**Files:** `src/audio/audio.h`, `src/audio/audio.cpp`

- `audio.h`: `void setPhones(uint8_t pct);  // live output level 0..100 %, (pct/100)^2; not in Render WAV`
- `audio.cpp`: `std::atomic<int32_t> phonesQ15{32768};` `setPhones` stores `pct*pct*32768/10000`.
  In `run()`: `lr[2i] = lr[2i+1] = (mono[i] * g) >> 15` with `g` read once per block.

### Task 2: Device setting in App

**Files:** `src/ui/app.h`, `src/ui/app.cpp`, `src/storage/settings.h`, `src/storage/settings.cpp`

- Remove `loadVolume/saveVolume`, `savedVol_`, `pendingVol_`, `volChangedAt_`, `saveVolumeIdle`,
  the override in `begin` and `projectReplaced`.
- Add `phones()` / `setPhones(int)` (clamp 0..100, `audio::setPhones`), `phones_`, `savedPhones_`;
  load `storage::loadSetting("phones", 100)` (>100 = 100); save in the existing device-settings
  idle writer with theme / autosave.

### Task 3: TRACK master marks dirty

**Files:** `src/ui/track_screen.cpp:300-307`, `src/ui/track_screen.h:74`

- `setMasterVol`: `app_.markDirty()` instead of `app_.invalidate()`; comment: project data.

### Task 4: PROJ / SYS "Phones" row

**Files:** `src/ui/proj_screen.h:22-27`, `src/ui/proj_screen.cpp`

- New `kPhones` first on SYS (before `kPreview`); `kPageFirst` SYS = `kPhones`.
- Row "Phones": `"%d%%"`, edit `app_.setPhones(app_.phones() + d)`.
- onEdit dirty rule: rows from `kPhones`..`kPreview` handled: Phones is not project data
  (`r != kPhones && r < kTheme`).

### Task 5: Verify

- `pio test -e native` (all pass), `pio run` (firmware builds).
- grep: no `loadVolume`, `saveVolume`, `savedVol_` left.
