# Microtonal tuning (Scala .scl) — implementation plan

Status: done 2026-10-15 (tasks 1–11; build numbers in the design, "Стоимость на Teensy").

Design: `2026-10-15-scala-tuning-design.md` (accepted 2026-10-15, all recommendations). Project-level tuning
table (12-TET default, bit-identical), linear keyboard mapping anchored at a root that keeps its 12-TET pitch,
applied in `Synth::control()` for melodic INT voices; factory tunings plus user `.scl` from `/presets/TUN`.

Rules: TDD per task (failing test first). No commits (project rule). New struct fields at the end, new enum
values before `Count`. Every task ends green on its own test; the full suite runs in the last task. RAM1 gate
(`2026-10-15-synth-fold-filters.md` task 1: ITCM code + padding <= 256 KB, free for local variables >= 24 KB)
after the synth task. Baseline (2026-10-15, before this feature): RAM1 variables 220992, code 246480,
padding 15664, free 41152.

---

### Task 1: model — `TuningTable` in `SynthModel`

**Files:** `lib/core/src/model.h/.cpp`; Test `test/test_model/test_main.cpp`.

1. Test: a default `Project` has `tuning.count == 0`, `tuning.root == kTuneRootAuto`; `Project::reset()`
   clears a set tuning; `tuningValid()` accepts count 1..64 with a period in (0, 4800] cents and rejects
   count 0 / 65, period 0 / negative / > 4800 c, a degree beyond ±9600 c; `tuningRootKey(t, scaleRoot)` is
   60 + scaleRoot for AUTO, the note otherwise.
2. Implement: `kTuneDegMax = 64`, `kTuneRootAuto = 0xFF`, `kTunePeriodMaxMc`, `kTuneDegAbsMaxMc`,
   `struct TuningTable { char name[17]; uint8_t count, root, pad; int32_t mc[64]; }` appended to
   `SynthModel` after `wavetableCount`; `tuningValid`, `tuningRootKey`, `tuningClear`.
3. → PASS.

### Task 2: `.scl` parser

**Files:** Create `lib/core/src/scala.h/.cpp`; Test `test/test_scala/test_main.cpp`.

1. Tests: comments anywhere, CRLF / LF / CR, empty description, count with leading blanks and a tail, cents
   (`701.955`, `-5.0`, `5.`, `.5`), ratios `3/2`, integer `2`, tails after values, extra lines ignored;
   errors with line numbers: `NoCount`, `BadCount`, `Empty`, `TooMany` (65), `BadValue` (`abc`, `3/0`, `0/2`,
   `-3/2`, `99999999999/1`), `Truncated`, `BadPeriod` (0, negative, > 4800 c); 64 degrees OK; description
   kept (cut to 32).
2. Implement `ParseErr parseScl(const char* text, int len, SclTable& out, int* errLine)`;
   `sclErrName(ParseErr)`; `sclToTuning(const SclTable&, const char* name, TuningTable&)` (keeps root).
3. → PASS.

### Task 3: factory tunings

**Files:** Create `lib/core/src/tuning_factory.h/.cpp`; Test `test/test_scala/test_main.cpp`.

1. Tests: 10 tunings (the design table), names as the design table, every text parses with no error, `tuningValid`; JUST 5-LIMIT
   degree 4 = 386.314 c, 31-EDO step 38.710 c, BOHLEN-PIERCE period 1901.955 c, PYTHAGOREAN 3/2 = 701.955 c;
   `factoryTuning(i, out)` false out of range.
2. Implement the `.scl` texts and `factoryTuningCount / factoryTuningName / factoryTuning`.
3. → PASS.

### Task 4: tuning map (`TuneMap`)

**Files:** Create `lib/core/src/tuning.h/.cpp`; Test `test/test_tuning/test_main.cpp`.

1. Tests: `build` of an invalid / 12-TET table → `on() == false`, `map(x) == x`; 24-EDO at root 60:
   `map(60) == 60`, `map(61) == 60.5`, `map(84) == 72`, `map(36) == 48`; JUST at root 62 (AUTO with scale
   root D) → `map(66) == 62 + 3.86314`; interpolation `map(60.5)` halfway between keys; negative j
   (floorDiv); keys outside 0..127 (−20, 150) follow the period; a 5-degree tuning (SLENDRO).
2. Implement `TuneMap::build(const TuningTable&, uint8_t scaleRoot)` (cold), inline `MT_HOT map(float)`
   (table `off_[128]` + period extrapolation).
3. → PASS.

### Task 5: synth integration

**Files:** `lib/core/src/synth.h/.cpp`, `lib/core/src/synth_voice.h`; Test `test/test_synth/test_main.cpp`.

1. Tests (render with `kBlock` blocks, frequency from zero crossings):
   - 12-TET: a project with tuning count 0 renders bit-identically before and after a JUST table is set and
     cleared again (fine, glide, ARP, VIB), and `Voice::tuned` stays false;
   - 24-EDO: CHIP note 61 sounds at 60.5 semitones; SYNTH / STRING / SAMPLE (root 60) likewise;
   - JUST, AUTO root, scale root C: note 64 at 386.3 c above C4 (± 2 c);
   - fine = cents over the tuning (24-EDO note 61, fine +50 → note 61 in 12-TET);
   - glide 60 → 62 in 24-EDO ends exactly at 61 semitones;
   - DRUM, FM KICK, a KIT lane, a NOTE-sliced SAMPLE: unchanged by a tuning;
   - a table changed in the model between blocks applies to the sounding tuned voice.
2. Implement: `Voice::tuned` (note-on: type eligible and `tune_.on()`); note-on drops fine from `pitch` /
   `lastPitch` for tuned voices; `control()` tuned path `pitch = map(v.pitch + arpOff) + fine + bend + vib`;
   `renderCore` start: `memcmp(&p_.tuning, &tuneSeen_)` / scale root → `tune_.build`.
3. → PASS. Build `pio run -e teensy41`; RAM1 gate.

### Task 6: project file (`TUNE` chunk)

**Files:** `lib/core/src/project_io.h/.cpp`; Test `test/test_project_io/test_main.cpp`.

1. Tests: round trip of a tuning (name, root, degrees); a 12-TET project writes no `TUNE` (file identical to
   one saved with the field zeroed); a file without `TUNE` loads 12-TET; a bad chunk (count 65, short, period
   0) loads 12-TET with `LoadNotes::tuningDropped`.
2. Implement save (only when count > 0) and load.
3. → PASS.

### Task 7: link

**Files:** `lib/core/src/synth_model.h/.cpp`, `lib/core/src/link_msg.h`, `scripts/link_desk.py` if it checks
the protocol; Tests `test/test_synth_model`, `test/test_link_msg`, `test/test_state_mirror`.

1. Tests: `Chunk::Tuning` range covers `tuning` to the struct end, the chunks tile `SynthModel`, send order
   Master, Tracks, Tuning, instruments, Samples, Wavetables; `kProtocol == 8`; editing `tuning` dirties only
   the Tuning chunk; the chunk fits one `StateSet`.
2. Implement.
3. → PASS.

### Task 8: card folder and loading `.scl` (ESP)

**Files:** `src/teensy/fs_server.cpp` (`/presets/TUN` at mount), `src/storage/tuning_store.h/.cpp` (list,
load + parse via `text_store`).

1. No native test (card I/O); the pure part (parse + fill) is task 2's `sclToTuning`.
2. Implement `kTunDir`, `listTunings(names, max)`, `loadTuning(name, TuningTable&, errLine)` (8 KB cap →
   `TooBig`).
3. Build `wt32`, `teensy41`.

### Task 9: PROJ rows

**Files:** `src/ui/proj_screen.h/.cpp`.

1. Rows `Tuning` and `Tuning root` after `Scale`: the list 12-TET, factory, card files (read once per visit at
   the first edit), applied at once, toasts for errors; root `AUTO` / note, greyed in 12-TET.
2. Build `wt32`.

### Task 10: linker, builds, bench

**Files:** `src/teensy/imxrt1062_t41_mt.ld` (`scala.cpp`, `tuning_factory.cpp` cold).

1. `pio test -e native` (all), `pio run -e wt32`, `-e teensy41`, `-e teensy41-bench`; RAM1 gate; record the
   numbers in the design's "Стоимость" section.

### Task 11: docs

`docs/manual_ru.md`, `docs/manual.md` (PROJ rows, a "Tuning" section, card table, limits),
`CHANGELOG_ru.md`, `CHANGELOG.md`, README feature list.
