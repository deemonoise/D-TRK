# Scenes and crossfader (SCENE / morph A↔B) — implementation plan

Status: done 2026-10-15 (tasks 1–10; build numbers in the design, "Стоимость на Teensy"). Native: 1741 tests.

Design: `2026-10-15-scenes-morph-design.md` (accepted 2026-10-15, all recommendations; see "Решения"). 8 project
scenes of parameter locks (12 per INT track, 14 master); a crossfader 0..127 morphs scene A into scene B on the
synth board, smoothed, p-locks above the morph, LFO / follower on top. Only the two ends travel to the Teensy
(`SynthModel::sceneLive[2]`, chunk `Scenes`), the fader position is a byte of the Master chunk.

Rules: TDD per task (failing test first). No commits (project rule). New struct fields at the end, new enum
values before `Count`. Every task ends green on its own test; the full suite runs in the last task. RAM1 gate
(`2026-10-15-synth-fold-filters.md` task 1: ITCM code + padding <= 256 KB, free for local variables >= 24 KB)
after the synth task. Baseline (2026-10-15, after Scala, before this feature): `teensy41` RAM1 variables
222816, code 247056, padding 15088, free 39328.

---

### Task 1: model and scene core

**Files:** `lib/core/src/model.h/.cpp`; Create `lib/core/src/scene.h/.cpp`; Test `test/test_scene/test_main.cpp`.

1. Tests: `sizeof(Scene) == 240`; a default `Project` has empty scenes, `sceneA 0`, `sceneB 1`, `xfade 0`,
   empty `sceneLive`; `sceneMix`: neither set → base bit-exact for every xfade; only A → A at 0, base at 127;
   midpoint rounding (`0 → 127` at 64 = 64); discrete DTY / DLY time switches exactly at 64; mask bits >= 12
   (track) / >= 14 (master) ignored; `sceneTrkBase` / `sceneMstBase` read the right fields (KIT track: the
   KIT's sends); `sceneTrkEff` / `sceneMstEff` with `sceneLive`; `syncSceneLive` copies `scenes[A/B]`;
   `sceneSet` / `sceneUnset` / `sceneCaptureMix` (VOL + PAN of INT tracks only) / `sceneClear`;
   `sceneClampTrk` / `sceneClampMst` (DTY >= Count → 0, DLY time 1..16, MAIN <= 200); names and ranges.
2. Implement: `kScenes`, `ScnTrk`, `ScnMst`, `struct Scene` in `model.h`; `SynthModel::xfade` after `shimInt`,
   `Scene sceneLive[2]` after `tuning`; `Project::scenes[kScenes]`, `sceneA`, `sceneB`; `reset()`.
   `scene.h`: inline `sceneMix(base, setA, a, setB, b, xf, discrete)`, the rest in `scene.cpp`.
3. → PASS.

### Task 2: synth morph

**Files:** `lib/core/src/synth.h/.cpp`; Test `test/test_synth/test_main.cpp`.

1. Tests: empty scenes render bit-identically to a project with the fields untouched at xfade 0 and 127
   (regression); scene B VOL 0 at xfade 127 silences the track, at 0 the base plays; PAN morph moves L/R;
   DRV morph changes the output; CUT morph changes the output, a FLT p-lock wins over it; DLY morph per track:
   two tracks on one instrument, only the scene track feeds the delay; LFO Pan adds to the morphed pan;
   MAIN morph scales the master; smoothing: a jump 0 → 127 reaches the target in ~30 ms (10..14 blocks at
   128) and the gain moves monotonically; the first block snaps (a synth created at xfade 127 plays B at once).
2. Implement: `xf_` (snap on the first block / `reset`), advanced per control period in `renderCore`; inline
   `morphT(track, p, base)` / `morphM(p, base)` in `synth.h`; reads in `trackVol`, `trackPan`, `control` (sends,
   insert, gate depth, MOD send), `controlFilter` (cutoff / reso), `delaySamples` and the master stages.
3. → PASS. Build `pio run -e teensy41`; RAM1 gate.

### Task 3: project file (`SCEN` chunk)

**Files:** `lib/core/src/project_io.h/.cpp`; Test `test/test_project_io/test_main.cpp`.

1. Tests: round trip (masks, values, A / B, xfade); a project with empty scenes, A 0, B 1, xfade 0 writes no
   `SCEN`; a file without `SCEN` loads empty scenes, A 0, B 1, xfade 0; a record with more parameters (extra
   skipped) and fewer (missing unset); mask bits above the known params dropped; values clamped; `sceneLive`
   synced after load.
2. Implement save / load.
3. → PASS.

### Task 4: link

**Files:** `lib/core/src/synth_model.h/.cpp`, `lib/core/src/link_msg.h`, `scripts/link_desk.py` if it checks the
protocol; Tests `test/test_synth_model`, `test/test_link_msg`, `test/test_state_mirror`.

1. Tests: `Chunk::Scenes` covers `sceneLive` to the struct end, the chunks tile `SynthModel`, send order
   Master, Tracks, Tuning, Scenes, instruments, Samples, Wavetables; `kProtocol == 9`; editing `sceneLive` dirties
   only Scenes, `xfade` only Master; the chunk fits one `StateSet`.
2. Implement.
3. → PASS.

### Task 5: ESP wiring

**Files:** `src/ui/app.cpp` (project replaced → `syncSceneLive`), every scene edit through `scene.h` helpers.

1. Pure parts are covered by task 1 (`syncSceneLive` after every operation). Build `wt32`.

### Task 6: MIX crossfader strip

**Files:** `src/ui/track_screen.h/.cpp`.

1. A 16 px bar between the M / S buttons and the scope: `A3 ━━●━━ B5`; tap / drag sets the position, a tap
   selects it (the encoder and A + turn move it, Shift x8); a tap on `A3` / `B5` selects that end (turn: scene
   1..8); B / tap again / leaving the tab drop it. Effective VOL / PAN tick over the base, the strip name
   yellow when a scene holds a parameter of the track. Build `wt32`.

### Task 7: SCENE view

**Files:** `src/ui/track_screen.h/.cpp`, `src/ui/app.cpp` (held track button + turn, track button = scene).

1. Long tap on the bar: the SCENE view (design "Решения"); strips show one parameter of the edited scene,
   `---` when unset; drag / held button + turn set it from the base; Shift + tap on the fader unsets; buttons
   `CAP` / `CLR` / `CPY`; the fader parks on the edited end while the view is open. Build `wt32`.

### Task 8: PERF

**Files:** `src/ui/perf_pad.h/.cpp`, `src/ui/grid_screen.cpp`, `src/ui/song_screen.cpp`.

1. `PerfPad::xfadeTurn(delta, shift)`: PERF on and nothing held → the fader, toast `XFADE 64 A3>B5`; the badge
   shows `X64` when the fader is off 0. Build `wt32`.

### Task 9: linker, builds, bench

**Files:** `src/teensy/imxrt1062_t41_mt.ld` (`scene.cpp` cold).

1. `pio test -e native` (all), `pio run -e wt32`, `-e teensy41`, `-e teensy41-bench`; RAM1 gate; record the
   numbers in the design.

### Task 10: docs

`docs/manual_ru.md`, `docs/manual.md` (MIX crossfader, SCENE view, PERF), `CHANGELOG_ru.md`, `CHANGELOG.md`,
README feature lists.
