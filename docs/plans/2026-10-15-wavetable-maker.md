# Wavetable maker (from a sample / drawing) — implementation plan

Status: done 2026-10-15 (tasks 1–9; build numbers in the design, "Сборка"). Native: 1804 tests.

Design: `2026-10-15-wavetable-maker-design.md` (accepted 2026-10-15, all recommendations; see "Решения").
Two ways to a new project wavetable, both ending in a 64 x 256 WAV in `/wavetables/<name>.wav` on the synth
board's card and the existing `ui::wtImportFile` (BankImport kind 1, `projWtSet`): (a) SAMPLE → To wavetable…
renders the Start..End region on the Teensy (`Msg::WtRender`, CYCLE / WINDOW); (b) the table picker's DRAW…
editor builds keyframes on the ESP and writes the WAV through the link file system.

Rules: TDD per task (failing test first). No commits (project rule). New enum values at the end (protocol
append-only). All maker code is non-hot: on the Teensy it goes to flash (`imxrt1062_t41_mt.ld`, like
`scala` / `scene` / `shift_gen`), its buffers come from `malloc` (RAM2) for the job only. RAM1 gate
(`2026-10-15-synth-fold-filters.md` task 1: ITCM code + padding <= 256 KB, free for locals >= 24 KB) after the
Teensy task.

---

### Task 1: resample, shapes, keyframes (`wt_make`)

**Files:** Create `lib/core/src/wt_make.h/.cpp`; Test `test/test_wt_make/test_main.cpp`.

1. Tests: `wtResample` — constant → constant; a 256-point sine at len 256 → identity; a 1024-point sine
   period → 256 points within 4 LSB; a sine above the target Nyquist averages out (~0); Catmull-Rom on
   len < 256 hits the samples at integer positions. `wtShape` SIN / TRI / SAW / SQR (peak 1, period 256);
   `wtSmooth` keeps a constant, `[1 2 1]/4` circular; `wtFromKeys` — a key frame equals its key, the
   midpoint is the mean, two keys fill all 64, unsorted keys sorted, keys 0 / 63 required.
2. Implement.
3. → PASS.

### Task 2: pitch (YIN-lite)

**Files:** `wt_make.h/.cpp`; Test `test_wt_make`.

1. Tests: `wtYin` on sine / saw / square at 55, 110, 440, 1760 Hz, 44.1 and 22.05 kHz — period within 0.1
   sample; white noise → no pitch; `wtCumNorm` d′(0) = 1.
2. Implement `wtDiff` (lag range, for chunking), `wtCumNorm`, `wtPickPeriod`, `wtYin`.
3. → PASS.

### Task 3: `WtMaker` (CYCLE, WINDOW, seam, norm)

**Files:** `wt_make.h/.cpp`; Test `test_wt_make`.

1. Tests (through `wtMakeArray`, the maker over an array): region < 2048 → `TooShort`; CYCLE of a 220 Hz sine
   starts at a rising zero (|x0| < 1 % peak, x1 > x0), correlation with an ideal sine > 0.999, 64 / 64
   pitched; glissando 220 → 440 Hz gives decreasing periods (frame 63 has ~2 cycles worth of points per 256
   … checked through `pitched` and zero-crossing count); noise in CYCLE → `NoPitch`; seam ON for a decaying
   sine (CYCLE) and noise (WINDOW): |f[255] + (f[255] − f[254]) − f[0]| < 2 % peak; DC +8000 → `wtImport`
   table mean ~0; Norm FRAME on an exponentially decaying sine → frame 0 / 63 RMS within 1 dB; silence at
   the end copies the neighbour; Norm TABLE keeps the decay; reads in pieces of <= 1000 give the same result
   as whole reads; peak 30000.
2. Implement the chunked state machine: `begin`, `nextRead`, `feed`, `step`, `done`, `error`, `result`,
   `pitched`, `progress`.
3. → PASS.

### Task 4: WAV out

**Files:** `wt_make.h/.cpp`; Test `test_wt_make`, `test/test_bank_ops/test_main.cpp`.

1. Tests: `wtWavWrite` → `wavParse` (mono 16 bit, 16384 frames, clm 256, mtcr = `sampleCrc`) → `wtDetect`
   256 x 64 → `wtImport` Ok; bank import of the written WAV gives the same key as `wtImport` of the array.
2. Implement (`wavHeader` + `clm ` chunk before `data`, RIFF size patched).
3. → PASS.

### Task 5: link `WtRender`

**Files:** `lib/core/src/link_msg.h/.cpp`, `scripts/link_desk.py`; Test `test/test_link_msg/test_main.cpp`.

1. Tests: `WtRenderReq` / `WtRenderRep` round trip, a path of 63 chars, bad mode rejected, `kProtocol` 10,
   `Msg::WtRender` after `Debug`.
2. Implement; `kProtocol` 10, `PROTOCOL = 10`.
3. → PASS.

### Task 6: Teensy `Task::WtRender`

**Files:** `src/teensy/sample_tools.cpp`, `src/teensy/link_server.cpp` (dispatch if needed),
`src/teensy/imxrt1062_t41_mt.ld`.

1. In memory (PSRAM / legacy) or from the store file: the maker fed from `bank::sampleAccess` data or
   `StoreReader::frames`; `step()` portions in the background work; Progress (frames done / 64); result
   written to `<path>.tmp` then renamed; path must be under `/wavetables/`; reply cached for a retry.
2. `wt_make.o` text and rodata to flash in the linker script.
3. Build `teensy41`, `teensy41-bench`; RAM1 gate.

### Task 7: ESP client and SAMPLE → To wavetable…

**Files:** `src/link/bank_client.h/.cpp`; Create `src/ui/wt_make_dialog.h/.cpp`; `src/ui/inst_screen.h/.cpp`.

1. `audio::wtRender(index, from, to, params, path, pitched&, err&, cb, ctx)`.
2. Dialog (Mode / Norm / Seam / Name, MAKE / CANCEL) in the style of `TransposeDialog`; checks (stopped,
   card, region, list not full), Overwrite? when the file exists, progress "WT n/64", `wtImportFile`, toast.
3. Build `wt32`.

### Task 8: DRAW… editor

**Files:** Create `src/ui/wt_draw.h/.cpp`; `src/ui/wt_picker.h/.cpp`.

1. Picker row DRAW… before IMPORT…; editor with canvas, key ruler, SIN TRI SAW SQR SMTH KEY DEL OK CANCEL;
   touch drawing with lines between events; encoder control as in the design; OK → name keyboard →
   `wtFromKeys` → `wtWavWrite` through `hw::sdFs()` (`.tmp` → rename) → `wtImportFile` → table set.
2. Build `wt32`.

### Task 9: full check and docs

1. `pio test -e native`; builds `wt32`, `teensy41`, `teensy41-bench`; RAM1 numbers into the design.
2. Manuals (ru / en), changelogs, README feature lists; this plan marked done.
