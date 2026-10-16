# Shift-register generator (GEN / FLP) — implementation plan

Status: done (2026-10-15).

Design: `2026-10-15-turing-track-design.md` (accepted 2026-10-15, all recommendations; see "Решения"). A live
melody generator per track: a looping shift register of 2..16 bits whose dropped bit comes back, inverted with
probability Flip; its value picks a note of the project scale in a range (PITCH) or also the rhythm (GATE). The
sequencer substitutes the step's note live; the pattern is untouched. A track setting in `Project::gen[]`
(not `TrackCfg`): no synth mirror, no `kProtocol` change, no Teensy reflash. UI names: GEN, FLP.

Rules: TDD per task (failing test first). No commits (project rule). New struct fields at the end, new enum
values before `Count`. Every task ends green on its own test; the full suite runs in the last task. The Teensy
does not link the sequencer's callers (render runs on the ESP), but `sequencer.cpp` is listed cold in the
linker script; `shift_gen.cpp` is listed cold too. RAM1 gate as before (ITCM code + padding <= 256 KB).

---

### Task 1: register core

**Files:** `lib/core/src/model.h` (`GenMode`, `GenCfg`, `Project::gen`, `reset()`), `lib/core/src/model.cpp`;
Create `lib/core/src/shift_gen.h/.cpp`; Test `test/test_shift_gen/test_main.cpp`.

1. Tests: defaults (OFF, len 8, flip 10, low 48, oct 2, density 50, seed = track + 1); `genReset` never gives
   reg 0 and depends on seed and track; Flip 0 — period exactly Length for 2..16; Flip 100 — period 2·Length,
   the second half the inverse of the first; same seed → same sequence, other seed → other; flip share ≈ Flip
   (±3 % over 10 000 shifts); `genNote` always in the scale and in `[low', low' + 12·oct]`, all-zero window →
   low', all-ones → top; chromatic; range clipped below 128; `genGate` monotonic in Density (0 none, 100 all);
   shrinking Length masks the high bits; `holdNext` (VOL → PAN → FLIP cycle).
2. Implement: `GenState {reg, n, flipped}`, `genHash`, `genReset`, `genShift`, `genNote`, `genGate`,
   `genClamp`, `HoldMode` / `holdFirst` / `holdNext`.
3. → PASS.

### Task 2: FLP fx

**Files:** `lib/core/src/model.h` (`Fx::FLP` before `Count`), `lib/core/src/fx_info.cpp`; Test
`test/test_fx_info/test_main.cpp`.

1. Tests: name `FLP`, long name, range 0..100, default 50, in `kOrder` right after NRN, not synth-only, not
   drum-only, format `"%3u"`.
2. Implement. `step_expand` ignores it (default branch: not synth-only → not sent).
3. → PASS.

### Task 3: sequencer

**Files:** `lib/core/src/sequencer.h/.cpp`; Test `test/test_gen_seq/test_main.cpp`.

1. Tests (MIDI tracks unless said): PITCH changes only the pitch of note steps (velocity, gate kept), steps
   without a note don't shift; a CHD step builds the chord on the new note; GATE plays empty steps on the
   register's gates and drops note steps without a gate, OFF still ends notes, PRB 0 silences a hit; a muted
   track and `skipTrackStep` shift too (phase after unmute); x2 speed shifts twice per pattern step; an FLP 100
   lock flips; a pattern change does not reset, `start()` does; a `genEdited` replan mid-play gives the same
   events as without it; drum tracks untouched; OFF untouched; `heardGen` follows the heard step; INT tracks get
   the same notes on the synth.
2. Implement: `gen_[kTracks]`, `Sched::gens`, reset in `start()`, restore in `rewind()`; `genStep()` at the top of
   `playTrackStep` / `skipTrackStep`; public `genEdited`, `genReseed(track)`, `genState(track)`, `heardGen(track)`,
   `genFromStep(track)`.
3. → PASS.

### Task 4: capture

**Files:** `lib/core/src/edit_ops.h/.cpp`; Test `test/test_edit/test_main.cpp`.

1. Tests: `captureGen` PITCH replaces only note steps' notes; GATE gives gated steps a note, others lose it, OFF
   kept, fx kept; `fromStep` ≠ 0 wraps; a track length shorter than the pattern; Flip ignored (the loop as it is);
   the state argument is not changed.
2. Implement.
3. → PASS.

### Task 5: project file (`TGEN`)

**Files:** `lib/core/src/project_io.cpp`; Test `test/test_project_io/test_main.cpp`.

1. Tests: round trip; a file without `TGEN` loads all OFF with default seeds; a shorter record (missing fields at
   defaults) and a longer one (extra skipped); values clamped; FLP survives in PATN.
2. Implement: written always, after `TFOL`.
3. → PASS.

### Task 6: render determinism

**Files:** Test `test/test_render/test_main.cpp`.

1. Test: a project with a PITCH generator at Flip 50 renders twice bit-identically, and differently from the
   same project with Gen OFF.
2. → PASS (no code expected).

### Task 7: engine and track button

**Files:** `src/engine/engine.h/.cpp` (`Cmd::GenEdit`, `Cmd::GenReseed`, `engine::genState`), `src/ui/app.h/.cpp`
(`holdMode_` replaces `holdPan_`, FLIP toast and nudge).

1. Pure cycle covered by task 1. Build `wt32`.

### Task 8: TRACK → GEN page

**Files:** `src/ui/track_screen.h/.cpp`.

1. Page `GEN` between NOTE and MIDI: Gen, Length, Flip, Low, Range, Density, Seed (click = Reseed), Register
   (cells, the current note, a gate dot; a flipped bit blinks yellow ~100 ms). Grey on a drum track. Every edit
   posts `GenEdit`. Build `wt32`.

### Task 9: GRID

**Files:** `src/ui/grid_screen.h/.cpp`.

1. Menu item `Capture gen` (Gen ≠ OFF, not a drum track): undo, `captureGen`, Gen OFF, `ReleaseTies`, toast
   `GEN CAPTURED`. A `G` mark on generator tracks' names (Overview, Detail), dim note column in Detail. Build
   `wt32`.

### Task 10: linker, builds

**Files:** `src/teensy/imxrt1062_t41_mt.ld` (`shift_gen.cpp` cold).

1. `pio test -e native` (all), `pio run -e wt32`, `-e teensy41`, `-e teensy41-bench`; RAM numbers in the design.

### Task 11: docs

`docs/manual_ru.md`, `docs/manual.md` (GEN page, FLP in the fx tables, Capture gen in the GRID menu, the
track-button cycle, Limits), `CHANGELOG_ru.md`, `CHANGELOG.md`, README feature lists.
