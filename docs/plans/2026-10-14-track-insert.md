# Track insert chain Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Per-track mono insert chain (drive -> crush) before the fader and pan; drive and crush
leave the voice. Design: `docs/plans/2026-10-14-track-insert-design.md`.

**Architecture:** Voices add their filtered output into a per-track segment buffer (`trk_`, 32
samples). Per segment each hot track runs `TrackStrip`: drive, crush, fader, peak, sends, pan ramp
into the mix or the sidechain bus. Strip parameters come from TrackCfg / the track instrument plus
the newest voice's locks and LFOs.

**Tech Stack:** C++17, lib/core, Unity native tests (`pio test -e native`), ESP32-S3 build
(`pio run -e wt32`), pool bench (`pio run -e wt32-pool24 -t upload`).

No commits: the user commits after the hardware check.

---

### Task 1: Model fields

**Files:** Modify `lib/core/src/model.h` (TrackCfg end: `drive`, `crushBits`, `crushRate`),
`lib/core/src/model.cpp` if reset needs it. Test: `test/test_model` (defaults 0).

### Task 2: Synth strip (core)

**Files:** `lib/core/src/synth_voice.h` (drop gl/gr/dgl/dgr/tgl/tgr/panSet, Drive, Crush; add
`uint8_t pan, drv, bits, srr`), `lib/core/src/synth.h` (`TrackStrip`, `trk_`, `trkHot_`,
`trkDirty_`, `previewIns_`, `refreshStrip`, `kProfStrip`), `lib/core/src/synth.cpp`.

Steps:
1. Tests first in `test/test_synth`: track drive changes the waveform; drive acts on the sum (two
   notes on one track driven != sum of two driven notes); fader after drive (vol 20 vs 127 with
   drive 127: RMS ratio ~ 20/127, not compressed); DRV lock on a note step / sounding voices sets the
   strip drive; LFO Drive / Bit / Srr / Dly / Rvb / Pan via the strip; KIT lane instrument send
   ignored, KIT send used; preview `0xF7 t` takes track t's drive; peaks and sidechain still per
   track. Adapt the old voice-field tests (`v.drive`, `v.crush`, `v.gl`) to `Synth::strip(t)`.
2. Run: fail (no API).
3. Implement: control() computes the voice bytes (base drive/bits/srr from the track config, sends
   from the track instrument or KIT); amp without trackVol; renderVoice without drive / crush;
   segment loop: voices add into `trk_`, then strips; refresh at boundaries and for dirty tracks.
4. `pio test -e native -f test_synth`: pass; then the whole suite.

### Task 3: Project file TINS + migration

**Files:** `lib/core/src/project_io.cpp`, `lib/core/src/preset_io.cpp` (zero fields on preset load).
Test: `test/test_project_io`, `test/test_preset_io`.

1. Tests: round trip of the three fields; a longer record (recSize 5) reads the known 3; a file
   without TINS migrates from instruments (KIT: max of KIT and lane instruments); instrument
   fields are 0 after load.
2. Implement writer (after TPAN), reader, migration in the CRC branch, zeroing.

### Task 4: Presets and demos

**Files:** `lib/core/src/presets_factory.cpp` (drop drive / crush), `lib/core/src/demos.cpp`
(acid drive, lofi crush on their tracks). Tests: `test/test_presets_factory` (no preset has
drive / crush), `test/test_demos` (the tracks carry them).

### Task 5: UI and audio glue

**Files:** `src/ui/track_screen.{h,cpp}` (Drive, Bit crush, Downsample after Pan),
`src/ui/inst_screen.{h,cpp}` (remove the three rows, shift the tail indexes),
`src/audio/audio.{h,cpp}` (`preview(instr, note, track)` sends `0xF7 track`; pool / FX benches set
track drive), `src/ui/grid_screen.cpp`, `src/ui/inst_screen.cpp` callers.
Build: `pio run -e wt32`, `pio run -e wt32-pool24`.

### Task 6: Docs

`docs/manual_ru.md` (TRACK rows, INST rows gone, DRV / BIT / SRR / LFO targets act on the track) —
the file has the user's uncommitted edits: touch only these sections. `lib/core/src/fx_info.cpp`
descriptions if they mention the instrument.

### Task 7: Bench

Pool bench on the device: main before (instrument drive 100) vs after (track drive 100), step
24 voices / 8 heavy, `/diag/cpuprof.txt`. Record the numbers in the design doc.
