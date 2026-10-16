# Song / pattern export to WAV with stems — implementation plan

Status: done (2026-10-15).

Design: `2026-10-15-song-export-design.md` (accepted 2026-10-15, all recommendations; see "Решения"). Render WAV
becomes Export…: one offline render on the synth board writes the master and, optionally, dry track stems and
the MOD / DLY / RVB returns (taps in `Synth::renderCore`), 24-bit (16-bit option, TPDF dither), into
`/export/<project>/<SONG|P01|P01x4>/`. Loops 1..16, tail AUTO / OFF / 2 / 4 / 8 s / WRAP. With PSRAM one pass
(the PERF ring as write buffers), without it passes of up to 6 files. Files download from the Wi-Fi page.

Rules: TDD per task (failing test first). No commits (project rule). New struct fields at the end, new enum
values at the end. The rendered master must stay bit-identical with and without taps. Every task ends green
on its own test; the full suite and all builds run in the last task. RAM1 gate
(`2026-10-15-synth-fold-filters.md` task 1: ITCM code + padding <= 256 KB, free for local variables >= 24 KB).
Export code stays out of ITCM (`src/teensy/imxrt1062_t41_mt.ld`); only the `taps_` checks sit in the hot path.
Baseline (2026-10-15, after the wavetable maker): `teensy41` RAM1 variables 222848, code 248528, padding 13616,
free 39296.

---

### Task 1: synth taps

**Files:** `lib/core/src/synth.h/.cpp`; Test `test/test_render/test_main.cpp`.

1. Tests: a project with delay, reverb, MOD bus, compressor + sidechain and EQ renders the same hash with no
   taps, every tap, some taps, only tracks, only each return (`test_taps_keep_master_identical`); with comp 0
   and EQ flat `softClip((sum of taps) x gain) x 32767` equals `masterL/R()` within 0.05 and every return
   carries signal (`test_taps_sum_to_master`); a track tap holds only its track, returns stay 0 with no sends
   (`test_taps_split_tracks_and_returns`).
2. Implement: `struct RenderTaps {mask, l[19], r[19], gain}`, `Synth::setTaps`, `masterL/R()`; in
   `renderCore` `tapsClear()` after the bus clears, `tapTrack` after the strip's `panAdd`, `tapReturn(k,
   before/after)` around `modfx_` / `delay_` / `reverb_` (difference of the mix: FMA-safe), `taps_->gain`.
   The helpers are not `MT_HOT`.
3. → PASS. Build `pio run -e teensy41`; RAM1 gate.

### Task 2: render length — loops, exact frames, tail modes

**Files:** `lib/core/src/render.h/.cpp`; Create `lib/core/src/export.h/.cpp`; Test `test/test_export/test_main.cpp`.

1. Tests: `RenderSpec::loops` 4 → `blocksTotal = ceil(4 x pass) + tail`, notes of passes 2..4 sound, nothing
   after; `usToFrames`; `exportRenderSpec`: OFF tail 0, 2 s = ceil(88200 / 128) + 1 blocks, AUTO 30 s cap,
   WRAP loops + 1 and one block; `exportBodyFrames` / `exportSkipFrames`; `exportCutFrames` for OFF, 2 s,
   AUTO (quiet → max(body, loud + 35 blocks), never past the rendered blocks), WRAP (loops x pass).
2. Implement: `RenderSpec::loops` (OfflineSequence body and end tick x loops); `ExportSpec`, `ExportTail`,
   `ExportStems`, the length helpers.
3. → PASS.

### Task 3: outputs, names, passes

**Files:** `lib/core/src/export.h/.cpp`; Test `test/test_export/test_main.cpp`.

1. Tests: `exportStemTracks` (INT, audible, with notes in the used patterns; SONG slots); `exportOutputs`:
   MASTER first, tracks, then MOD (type not OFF and level > 0) / DLY / RVB (level > 0) with TRACKS+FX;
   `exportDir` `/export/DEMO/P01`, `/export/DEMO/P03x4`, `/export/DEMO/SONG`; names with the sanitised
   track name, without a name `_03`; paths < 128; `exportPasses(20, 6) == 4`; `exportMidiTracks` counts
   MIDI tracks with notes; `exportBytes`.
2. Implement.
3. → PASS.

### Task 4: WAV header and PCM

**Files:** `lib/core/src/wav.h/.cpp`, `lib/core/src/export.h/.cpp`; Test `test/test_export/test_main.cpp`.

1. Tests: `wavHeaderPcm` 24 / 16 bit stereo parses back (`wavParse`: channels, bits, rate, frames, no
   mtcr); `ExportPcm::put` 24 bit: v x 256 rounded, clamps at +-8388607 counting clips, peak in int16 units;
   16 bit: TPDF dither within +-1 LSB of the rounded value, deterministic per seed, clamps and counts clips.
2. Implement.
3. → PASS.

### Task 5: ExportWriter

**Files:** `lib/core/src/export.h/.cpp`; Test `test/test_export/test_main.cpp` (a POSIX `ExportIo`).

1. Tests: 3 outputs (MASTER, a track, DLY) of a rendered project through the writer: files appear only after
   `end` (written as `.tmp`), all cut to the same frames, parse as 24-bit stereo; the MASTER file's samples
   equal the synth's float master x 256; the writer flushes at most once per file per `flush()` and only
   above the threshold; `skipFrames` drops the first frames; `abort` removes the `.tmp` files; a write
   failure reports false and `abort` leaves nothing; two passes (outputs split 2 + 1) give byte-identical
   files to one pass.
2. Implement: `ExportIo` (open / write / seal / close / remove / rename), `ExportWriter` (begin, add, block,
   flush, end, abort, peak, clips, taps()).
3. → PASS.

### Task 6: link

**Files:** `lib/core/src/link_msg.h/.cpp`, `scripts/link_desk.py`; Test `test/test_link_msg/test_main.cpp`.

1. Tests: `kProtocol == 11`; `RenderStartReq` round trip with `kExport`, `bits`, `outs`, `skipFrames`,
   `estFrames` (target > kExport refused); `RenderOut` round trip (name cut to 63, tap > 19 refused);
   `RenderRep` with `quiet`, `outMax`, `outN` and per-output peaks / clips (outN > 20 refused) fits a frame;
   `RenderEndReq::frames`.
2. Implement; `Msg::RenderOut` appended; `link_desk.py` `PROTOCOL = 11` and the message list.
3. → PASS.

### Task 7: synth board (`render_server`, audio_out)

**Files:** `src/teensy/render_server.cpp`, `src/teensy/audio_out.cpp`, `src/teensy/imxrt1062_t41_mt.ld`.

1. Target `kExport`: buffers from the PERF ring (PSRAM on: up to 20 x 48 KB, PERF detached) or RAM2 malloc
   (up to 6 x 10 KB, fewer if malloc fails); tap buffers malloc; `outMax` in the reply; `RenderOut` opens
   `<dir>/<name>.tmp` (preAllocate); `RenderBlocks` → writer, one flush per frame, `quiet` in the reply,
   PSRAM failure → WriteFail; `RenderEnd` cut / headers / renames, peaks and clips; abort removes `.tmp`.
   SdFat `ExportIo` in `render_server.cpp`.
2. `audio_out.cpp`: no `setLoad` while parked.
3. `export.cpp` (+ `wav.cpp`) in the flash list of the linker script.
4. Build `teensy41`, `teensy41-bench`; RAM1 gate.

### Task 8: ESP export run

**Files:** `src/storage/render_io.h/.cpp`.

1. `storage::exportSet(p, ExportSpec, ExportStats&, cb, ctx)`: outputs, space check (`remoteSpace`), passes
   (first `RenderStart` asks for all outputs, `outMax` splits the rest), `RenderOut`s, blocks with the AUTO
   stop, `RenderEnd` with the cut, progress over all passes, cancel / error removes the files of earlier
   passes, stale `.wav` of the folder removed after success. `renderWav` / the stem path removed.
2. Build `wt32`.

### Task 9: UI (Export… dialog)

**Files:** `src/ui/render_dialog.h/.cpp`, `src/ui/proj_screen.cpp` (the menu item), `src/ui/app.*` if needed.

1. Rows Source, Loops (PATTERN only), Tail, Tracks (ALL / SOLOED), Stems (OFF / TRACKS / TRACKS+FX), Format
   (24 / 16 BIT), EXPORT, Cancel; header with the folder; an estimate line (size x files, length, passes,
   MIDI tracks skipped); `OVERWRITE <src>?`; result toast `4:12 PEAK -1.3dB 17 FILES` (+ `CLIP`).
2. Build `wt32`.

### Task 10: Wi-Fi download

**Files:** `lib/core/src/file_rules.h/.cpp`, `src/net/web.cpp`, `src/net/web_page.h`; Test
`test/test_file_rules/test_main.cpp`.

1. Tests: `webExportPath` accepts `/export/A/P01/A_P01_MASTER.wav`, refuses `..`, `/samples/x.wav`,
   `/export`, `//export/x`, empty; `parseRangeStart`.
2. Implement `/api/export/list`, `/api/export/file` (Range `bytes=N-`), the page's Export section.
3. Build `wt32`.

### Task 11: docs, full run

1. `docs/manual_ru.md` / `docs/manual.md` (Render WAV → Export, paths, Wi-Fi), `CHANGELOG*`, README lists.
2. Full `pio test -e native`; builds `wt32`, `teensy41`, `teensy41-bench`; RAM1 numbers in the design.
3. Mark this plan done.
