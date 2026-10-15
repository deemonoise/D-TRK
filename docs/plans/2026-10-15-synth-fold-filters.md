# SYNTH wavefolder, ring mod and new filter types — implementation plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** SYNTH gets Fold and Ring; every type gets the LADDER, MS20, COMB and FORMANT filters; plus the
groundwork B (MACRO) and C (STRING) need.

**Architecture:** see `2026-10-15-synth-fold-filters-design.md`. Fold / ring live in `SynVoice`; the
filters in a new `VoiceFilter` that replaces `Voice::flt` (LP / BP / HP delegate to `Svf`, bit-exact);
COMB uses a per-voice int16 line from a RAM2 pool shared later with STRING.

**Tech stack:** C++17, `lib/core`, Unity native tests (`pio test -e native`), Teensy 4.1 (`teensy41*`
envs), ESP32-S3 UI (`wt32`).

Rules: TDD per task (failing test first). No commits (project rule). New struct fields at the end. Every
task ends green on its own test; the full suite runs in the last tasks. Keep the RAM1 gate (task 1) after
every task that adds hot code.

---

### Task 1: RAM1 headroom (cold synth code to flash)

**Files:** `src/teensy/imxrt1062_t41_mt.ld`; any `lib/core/src/synth*.cpp` function on the per-block
path missing `MT_HOT`.

1. Run `pio run -e teensy41` and note the `teensy_size` lines (RAM1 code / variables / padding / free
   for local variables) in the scratchpad: the baseline.
2. List the functions `Synth::render`/`renderCore` reach per block (render, control, controlFilter,
   controlFm/Drum/Syn, renderVoice, renderChip/Sample/Stream, runStrip, refreshStrip, the fx `process`
   functions, `Env`, `Svf::set`, fx DSP in `synth_*.cpp`). Every one must be `MT_HOT` (or inline in a
   header). Add `MT_HOT` where missing.
3. Add to the `.text.code` list (both `*:file.cpp.o` and `*file.cpp.o` forms, as the existing entries):
   `synth.cpp`, every `synth_*.cpp`, `model.cpp`, `fx_info.cpp`, `render.cpp`, `render_link.cpp`,
   `perf_fx.cpp`, `scale.cpp` if it exists. Their `.fastrun` (MT_HOT) code stays in ITCM.
4. Verify: `pio run -e teensy41` and `pio run -e teensy41-bench-max` build; ITCM shrinks (padding up),
   "free for local variables" >= baseline. Record the new numbers in the scratchpad for task 16.
5. `pio test -e native` (no behaviour change).

Gate used by every later task: ITCM within 8 banks (code + padding <= 256 KB) and free for local
variables >= 24 KB.

### Task 2: shared fold curve (`synth_shape.h`)

**Files:** Create `lib/core/src/synth_shape.h`; Modify `lib/core/src/synth_drive.h/.cpp`; Test
`test/test_synth_drive/test_main.cpp`.

1. Test first: `foldCurve(u)` = 0, 1, 0, −1 at u = 0, 1, 2, 3 and odd; `foldAnti(u)` matches the numeric
   integral of `foldCurve` (trapezoid, 1e-3 step) over [−6, 6] within 1e-3. Run
   `pio test -e native -f test_synth_drive` → FAIL (no header).
2. Move the FOLD f / F / small-step fallback from `ModelDrive` into `synth_shape.h` as `MT_INLINE`
   functions; `ModelDrive` calls them.
3. `pio test -e native -f test_synth_drive` → PASS, including the existing FOLD tests unchanged
   (bit-exact).

### Task 3: fold and ring in `SynVoice`

**Files:** `lib/core/src/synth_syn.h/.cpp`; Test `test/test_synth_syn/test_main.cpp`.

1. Tests first:
   - `SynParams::fold = 0, ring = 0`: render bit-exact to a render with the fields absent (compare with a
     reference buffer from the same code path before your change: store a short FNV hash of 4096 samples
     of today's output for SAW+SQR mix 0.5 at 220 Hz, computed before editing synth_syn.cpp).
   - fold 1.0 on a 220.5 Hz TRI (200-sample period, 4000 samples = 20 periods): harmonic energy over
     the fundamental's (total minus Goertzel fundamental) rises >= 20 dB vs fold 0 (a lone bin such as
     660 Hz does not: gain 8 folds a full-scale tri into a near-clean higher triangle); |out| <= 1.5;
     no NaN for fold 0..1 at 20, 220, 2000, 10000 Hz.
   - fold at 5 kHz osc 1: effectively off (pitch limit): output equals fold 0 within 1e-6.
   - ring 1.0, both oscs TRI at 220 Hz, MIX 0: the 440 Hz bin dominates the 220 Hz bin by >= 20 dB.
   - ring 0.5, MIX 0: osc 2 rendered (Semi2 = +7: the sum frequency 220 + 329.6 = 549.6 Hz rises
     >= 20 dB vs ring 0; a product has no line at osc 2's own 329.6 Hz).
   Run `pio test -e native -f test_synth_syn` → FAIL.
2. Implement: `SynParams` `float fold = 0, ring = 0`; ramps `fold_/ring_` with steps like `mix_`;
   `need2 |= ring > 0`; per chunk: `m = a + (b - a) * mx; a = m + (a * b * kRingGain - m) * rg` before
   sub / noise; then fold with ADAA state `foldX1_, foldF1_` (reset in `trigger()`), gain
   `g = 1 + 7 * fold * pitchLimit(hz[0])`, skipped when fold and its ramp end are 0. Hot code `MT_HOT`.
   Tune `kRingGain`, `kFoldGain` so fold 1.0 and ring 1.0 at 220 Hz are within ±3 dB RMS of the plain mix.
3. `pio test -e native -f test_synth_syn` → PASS.

### Task 4: model fields and LFO targets

**Files:** `lib/core/src/model.h`, `lib/core/src/model.cpp`; Test `test/test_model/test_main.cpp`.

1. Tests first: `lfoDestValid(Fold/Ring, Synth)` true, false for Chip / Fm / Drum / Sample / Kit;
   `lfoDestName` "FOLD", "RING"; `lfoDestStep` from `Semi2` +1 → `Fold`, +1 → `Ring`, +1 → `Rate1` on
   SYNTH, and skips both on FM; `instrSetType(Fm)` turns an LFO on `Fold` into `Pitch`; `Instrument{}`
   has `synFold == 0 && synRing == 0`. Run `pio test -e native -f test_model` → FAIL.
2. Implement: `uint8_t synFold = 0, synRing = 0;` at the end of `Instrument`; `LfoDest::Fold, Ring` before
   `Count`; `kDestOrder` after `Semi2`; names; `lfoDestValid`.
3. `pio test -e native -f test_model` → PASS; `pio test -e native -f test_synth_model` → PASS.

### Task 5: SYNI bytes

**Files:** `lib/core/src/inst_codec.h/.cpp`; Tests `test/test_project_io`, `test/test_preset_io`.

1. Tests first: project and .mti round trip with synFold 77 / synRing 33; a SYNI record with bytes 41..47
   zero loads 0 / 0; value 200 clamps to 127. Run `pio test -e native -f test_project_io` and
   `-f test_preset_io` → FAIL.
2. Implement: `packSyn` b[41] = synFold, b[42] = synRing; `unpackSyn` clamps 0..127. Update the comment
   of `kSynRecSize` (43 bytes of fields).
3. Both tests → PASS.

### Task 6: Synth reads fold / ring

**Files:** `lib/core/src/synth.cpp` (`controlSyn`); Test `test/test_synth/test_main.cpp`.

1. Tests first: a SYNTH note with synFold 0 / synRing 0 renders bit-exact to the checksum of today's
   build (capture it first: add the test with the hash printed, run it on the unchanged code, paste the
   value); synFold 100 changes the output; an LFO with dest FOLD, depth 63 on a fold-0 instrument changes
   it; the follower targeting RING on a SYNTH track changes it. Run `pio test -e native -f test_synth` →
   FAIL.
2. Implement in `controlSyn`: `sp.fold = u7(m.synFold + 64 * la[Fold]) / 127`, `sp.ring` likewise.
3. `pio test -e native -f test_synth` → PASS.

### Task 7: `VoiceFilter` skeleton with LP / BP / HP (bit-exact)

**Files:** Create `lib/core/src/synth_vfilter.h/.cpp`; Test create `test/test_synth_vfilter/test_main.cpp`.

1. Tests first: for LP / BP / HP at several hz / reso, `VoiceFilter` output equals `Svf` output exactly
   (same inputs, `set` each 32 samples, 4096 samples); `ringing()` equals `Svf::ringing()`.
   Run `pio test -e native -f test_synth_vfilter` → FAIL.
2. Implement: `enum class FltKind` mirroring FltMode minus Off; `set(uint8_t mode, float hz, float reso127,
   float q)` (q = today's cached `resoQ` for the SVF modes); `processAdd(in, out, n, line)`; `ringing(line)`;
   `reset()`. Union of the per-mode states; mode change resets.
3. → PASS.

### Task 8: LADDER

**Files:** `synth_vfilter.h/.cpp`; Test `test_synth_vfilter`.

1. Tests first (white noise or swept sine, Goertzel): r 0, fc 1 kHz: power at 4 kHz >= 20 dB under 250 Hz;
   r 1: after a single impulse, output RMS over samples 20000..22050 > 0.01 (self-oscillation);
   no NaN / |y| < 4 for a sweep 20 Hz .. 20 kHz at r 0, 0.5, 1 with full-scale noise; passband gain at
   r 0.8 within 6 dB of r 0. → FAIL.
2. Implement the TPT ladder (zero-delay solve, `fastTanh` on the input sum, compensation).
3. → PASS.

### Task 9: MS20

Same files. Tests first: −12 dB/oct (power at 4 x fc >= 10 dB under passband), bounded |y| < 2 at r 1
with full-scale saw, no NaN over the sweep, resonance peak at fc for r 0.8 (bin at fc > bin at fc / 2).
Implement the Sallen-Key TPT with the saturator in the feedback. → PASS.

### Task 10: `VoiceLines` pool and COMB

**Files:** `lib/core/src/hot.h` (`MT_RAM2`), create `lib/core/src/synth_lines.h/.cpp` (`constexpr int
kLineLen = 512; int16_t* voiceLine(int voice);` backed by `MT_RAM2 int16_t gLines[kVoices][kLineLen]`),
`synth_vfilter.*`; Test `test_synth_vfilter`.

1. Tests first: hz 220, r 0.9 on noise: bins at 220 and 440 Hz exceed the 330 Hz bin by >= 10 dB;
   hz 50 behaves as 86 Hz (peak at 86); r 1 with full-scale noise stays |y| < 4, no NaN; `ringing()`
   true right after input, false 2 s after silence at r 0.9; two filters on two lines do not interact.
   → FAIL.
2. Implement: line written `clamp(y * 8192)`, read with linear interpolation, `fb = 0.97 r`, output scale
   `1 - 0.5 fb`; running peak per block for `ringing()`.
3. → PASS. Build `pio run -e teensy41`: RAM2 static grows by 32 KB; gate of task 1 holds.

### Task 11: FORMANT

Same files. Tests first: position 0 (A): bins at 730 and 1090 Hz each > bin at 1600 Hz by >= 6 dB on
noise; position 4 (U): 300 Hz > 600 Hz bin; position 1.5 (E..I) moves F2 above 1800 Hz; Q from r;
`set` with unchanged inputs does not recompute (counter in a test build, or time-free: same output).
Implement 3 `Svf` band-passes, vowel table, log interpolation with `fastExp2`. → PASS.

### Task 12: wire `VoiceFilter` into Synth

**Files:** `lib/core/src/model.h` (`FltMode` values), `lib/core/src/synth_voice.h` (`Voice::flt`),
`lib/core/src/synth.cpp` (`controlFilter`, `renderVoice`, `sounding`); Test `test_synth`.

1. Tests first: capture before editing a checksum of a project render that uses LP, BP and HP on CHIP,
   FM, SYNTH (4 s); after: identical. Each new mode on a CHIP saw note renders non-silent and the voice is
   freed within 3 s after release; FORMANT ignores key track (same output for keytrack 0 and 127); COMB
   on two simultaneous notes of one track produces distinct pitches. → FAIL for the new modes.
2. Implement: FltMode `Ladder, Ms20, Comb, Formant`; `Voice::flt` type; `controlFilter` passes
   `oct` without key track for Formant; `renderVoice` and the tail path pass `voiceLine(&v - voices_)`.
3. `pio test -e native -f test_synth` → PASS. `pio test -e native -f test_voices` → PASS.

### Task 13: UI

**Files:** `src/ui/inst_screen.h/.cpp`.

1. FILT `Filter` names `OFF LP BP HP LADDER MS20 COMB FORMANT` (array sized by `FltMode::Count`,
   static_assert); `Cutoff` display: vowel name for FORMANT (A, A>E, E, … from `cutoff * 4 / 127`);
   `Key track` dimmed for FORMANT and OFF.
2. SYNTH MOD page rows `Ring`, `Fold` after `Noise` (0 = OFF); update `kSub`-based row constants.
3. Verify: `pio run -e wt32` builds. (No native UI tests.)

### Task 14: link protocol

**Files:** `lib/core/src/link_msg.h` (`kProtocol = 5`), `scripts/link_desk.py` (`PROTOCOL = 5`, the
self-test assert), `test/test_link_msg/test_main.cpp`.

1. Test first: `TEST_ASSERT_EQUAL(5, kProtocol)` → FAIL. 2. Bump. 3. `pio test -e native -f
test_link_msg` → PASS; `~/.platformio/penv/bin/python -I scripts/link_desk.py selftest` prints
`selftest ok`.

### Task 15: bench ladders (group 2) and the refactor

**Files:** `src/teensy/bench_max.cpp`, `platformio.ini` (comment only: how to pass the group).

1. Refactor `Ladder` to `{name, kind, top, instr, hits}`; build the instruments in `begin()` from a
   table; keep today's ladders as group 1 bit-for-bit in behaviour.
2. `BENCH_MAX_GROUP` (default 0 = all): 1 = today's; 2 = `CHIP LADDER`, `CHIP MS20`, `CHIP COMB`,
   `CHIP FORMANT`, `WT FOLD+RING`, `16 WT FX + CHIP LADDER` (+ `fx only`, `CHIP`, `WT SYNTH` as
   references). Group 3 / 4 are filled by plans B / C.
3. Verify: `pio run -e teensy41-bench-max` and
   `PLATFORMIO_BUILD_FLAGS="-DBENCH_MAX_GROUP=2" pio run -e teensy41-bench-max` build; gate of task 1.

### Task 16: full verification, bench on the Teensy, docs

**Files:** `docs/plans/future-teensy.md`, `docs/manual.md`, `docs/manual_ru.md`, `CHANGELOG.md`,
`CHANGELOG_ru.md`; optionally `lib/core/src/presets_factory.cpp` + `test/test_presets_factory`.

1. `pio test -e native` → all PASS. `pio run -e wt32`, `pio run -e teensy41`, `pio run -e teensy41-desk`
   build; record `teensy_size` RAM1 / RAM2.
2. Bench: `PLATFORMIO_BUILD_FLAGS="-DBENCH_MAX_GROUP=2" pio run -e teensy41-bench-max -t upload`, then
   `~/.platformio/penv/bin/python -I scripts/link_desk.py --port /dev/cu.usbmodem105489401 status 300`
   (the summary arrives ~6–8 min after boot; rerun `status` if it has not). Also group 1 once to compare
   with the 2026-10-15 numbers (task 1 must not have slowed anything: CHIP / WT within ±0.1 %/voice).
3. Apply the design's cut-offs if a target is missed (each cut-off is its own small TDD change: the test
   of that filter / fold must stay green), re-run the bench.
4. future-teensy.md: a dated paragraph (2026-10-xx) with per-mode price, the worst case, RAM1 / RAM2.
5. Manual (en and ru, same content): FILT table (8 modes; what Cutoff / Reso mean per mode; FORMANT
   vowels; COMB from 86 Hz; Key track off for FORMANT), SYNTH MOD rows Ring / Fold, LFO Dest list (FOLD,
   RING for SYNTH; range note "±64 steps"). The filter paragraph no longer says "12 dB/oct SVF" for every
   mode.
6. CHANGELOG.md / CHANGELOG_ru.md: one entry "SYNTH: wavefolder and ring mod; filters LADDER, MS20, COMB,
   FORMANT on every type".
7. Optional factory presets (FOLD LD, RING BEL; ACID LADDER on CHIP …) with `test_presets_factory`
   updated (`pio test -e native -f test_presets_factory`).
