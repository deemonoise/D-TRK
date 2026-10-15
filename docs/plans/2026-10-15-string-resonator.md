# STRING instrument type — implementation plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** a heavy instrument type STRING with two models (Karplus-Strong STRING, 8-mode MODAL), an
internal click / noise exciter and the macros DAMP, BRIGHT, STRUCT, POS, EXC.

**Architecture:** see `2026-10-15-string-resonator-design.md`. `StringVoice` (`lib/core/src/
synth_string.*`) in the voice-engine union, the string's delay line from A's `VoiceLines` pool, the
heavy / engine / profile pattern of B. Requires plans A and B done.

**Tech stack:** C++17 `lib/core`, Unity (`pio test -e native`), Teensy 4.1, ESP32-S3 UI.

Rules: TDD per task, no commits, new enum values before `Count`, no brand names user-facing, RAM1 gate
of plan A task 1 after each task with hot code.

Reference source (read only, its own scratchpad directory): `github.com/pichenettes/eurorack`,
`rings/dsp/string.cc`, `resonator.cc`, `rings/dsp/dsp.h`. Derived files carry the MIT header
(Copyright 2015 Emilie Gillet); add `rings` files to the first line of `LICENSES/MIT-eurorack.txt`.

---

### Task 1: type, model enum, helpers

**Files:** `lib/core/src/model.h/.cpp`, `lib/core/src/inst_codec.cpp` (`fixInstrument`),
`lib/core/src/preset_paths.cpp`, `src/ui/inst_screen.cpp` (arrays sized by `InstrType::Count`: name
`"STRING"`, page name `"RES"`); Tests `test/test_model`, `test/test_preset_paths`.

1. Tests first: `InstrType::String`, UI order FM, SYNTH, MACRO, STRING, DRUM, SAMPLE, CHIP, KIT;
   `instrSetType(String)` macros {80, 64, 0, 30, 10}, machine 5 -> 0; `fixInstrument` clamps machine >= 2;
   `lfoDestValid(Dec..Con, String)` true, `Duty|Sub|Fold` false; `lfoDestName` DAMP, BRIGHT, STRUCT, POS,
   EXC; `stringModelName(0) == "STRING"`, `(1) == "MODAL"`; `presetRoot(String) == "/presets/STRING"`.
   → FAIL.
2. Implement. 3. `pio test -e native -f test_model`, `-f test_preset_paths`, `-f test_project_io` → PASS;
   `pio run -e wt32` builds.

### Task 2: exciter

**Files:** create `lib/core/src/synth_string.h/.cpp` (MIT header); Test create
`test/test_synth_string/test_main.cpp`.

1. Tests first: e 0 → exactly one non-zero sample before the LP (impulse), e 1 → burst ~20.5 ms then
   zeros; the LP output's HF (above 4 kHz) is >= 10 dB lower at b 0 than b 1; twin LFSR: the delayed
   stream equals the main stream shifted by P for P in {1, 37, 200}; with position comb the output at
   n equals `x[n] - x[n-P]`. → FAIL.
2. Implement `StringExciter { trigger(level, e, b, hz, P); MT_INLINE float next(); bool active() }`.
3. `pio test -e native -f test_synth_string` → PASS.

### Task 3: STRING (Karplus-Strong)

**Files:** `synth_string.*`, uses `synth_lines.h` (plan A); Test `test_synth_string`.

1. Tests first (helpers: period by autocorrelation peak with parabolic interpolation; T60 from the
   envelope of |y|):
   - tuning at 55, 110, 440, 1760 Hz within ±5 cents at s 0; 30 Hz within ±10 cents (half / quarter rate);
   - T60 at d 0.2 and 0.8 within ±25 % of `0.05 x 200^d`;
   - b 0 vs 1: HF energy lower by >= 10 dB at 0;
   - p 0.5: harmonics 2 and 4 >= 15 dB under 1 and 3;
   - s 1: partial 5 frequency / f > 5.05 (dispersion stretches);
   - bounded |y| < 2, no NaN, all macros 0 / 0.5 / 1, 30 Hz .. 4 kHz; 100 re-strikes in a row bounded.
   → FAIL.
2. Implement: fractional read, damping LP, dispersion all-pass with delay compensation, loop gain,
   saturating int16 write, half / quarter rate below 86 / 43 Hz. `MT_HOT` loops.
3. → PASS.

### Task 4: MODAL (8 modes)

Same files. Tests first: s 0 → partials at 1, 2, 3, 4 x f within ±2 %; s 1 → mode 2 / mode 1 > 2.5;
d controls T60 (±25 %); b 0 → modes 5..8 decay at least 2x faster than at b 1; p 0.5 → mode 2 amplitude
< -30 dB relative to mode 1; modes above 0.45 sr are skipped (no NaN at 4 kHz with s 1); coefficients
are not recomputed when inputs are unchanged (outputs identical to a run that recomputes every update).
Implement: stretch law, 8 `Svf` band-passes, Q from T60 and brightness loss, amplitudes from p. → PASS.

### Task 5: `StringVoice` wrapper and `done()`

Same files. Tests first: `StringVoice::trigger / control(StringParams, n) / render(out, n, amp)` for
both models; model switch takes effect at `trigger()`; `done()` false during the burst, true within
T60 + 0.1 s; a re-trigger while ringing adds energy without a click (max sample-to-sample jump < 0.5).
Implement. → PASS. Static_assert the struct is trivially copyable.

### Task 6: Synth integration, profile, protocol

**Files:** `lib/core/src/synth_voice.h` (`EngineKind::String`, union member, `Voice::str`),
`lib/core/src/synth.h/.cpp` (heavy predicate + `stringT`, `controlString`, render path with
`voiceLine(&v - voices_)`, env kill on `done()`, `kProfString` / "string", `voiceStage`, COMB on STRING
treated as Off in `controlFilter`), `lib/core/src/link_msg.h` (`ProfileRep::kStages = 24`,
`kProtocol = 7`), `src/teensy/link_server.cpp`, `src/audio/audio_link.cpp`, `scripts/link_desk.py`
(`PROTOCOL = 7`); Tests `test/test_synth`, `test/test_link_msg`.

1. Tests first: a STRING note sounds at its pitch; 9 STRING notes on native (cap 8) → one stolen; a held
   note whose resonance died frees its voice (`on == false`) before note-off; fx DCY lock and LFO DAMP
   change the decay; COMB filter on a STRING instrument renders as filter Off; profile counts in
   `kProfString`; `kProtocol == 7`; ProfileRep 24 stages round trip. → FAIL.
2. Implement.
3. `pio test -e native -f test_synth`, `-f test_voices`, `-f test_link_msg` → PASS;
   `~/.platformio/penv/bin/python -I scripts/link_desk.py selftest` → `selftest ok`; `pio run -e teensy41`
   (RAM1 gate: if the union grew, note the DTCM change).

### Task 7: files

Tests (`test_project_io`, `test_preset_io`): a STRING instrument (MODAL, macros 10 / 90 / 64 / 20 / 127)
round trips in a project and a .mti. → PASS (fix the codec if not).

### Task 8: factory presets

**Files:** `lib/core/src/presets_factory.cpp`; Test `test/test_presets_factory`.

Tests first: `factoryCount(String) == 6`, models as in the design (3 STRING, 3 MODAL), names <= 8 chars,
unique. → FAIL. Add NYLON, HARP, PIZZ, MARIMBA, BELL, KALIMBA. → PASS.

### Task 9: UI

**Files:** `src/ui/inst_screen.h/.cpp`.

STRING params: Model, Damp (T60 text), Bright, Struct, Position (%), Excite (`CLICK` / ms); ADSR / Mode /
Glide as SYNTH; one type page `RES`. FILT `Filter` row skips COMB when the type is STRING (step over it in
both directions). Verify `pio run -e wt32`.

### Task 10: bench group 4

**Files:** `src/teensy/bench_max.cpp`.

Ladders under `BENCH_MAX_GROUP == 4`: `STRING HIT`, `MODAL HIT` (4-note chords per track, re-struck every
1/8, DAMP 100), `STRING LOW` (notes 28..40), `16 MODAL + CHIP`; references `fx only`, `WT SYNTH`, `DRUM`.
Needs a "hits on chords" play mode in the ladder table (plan A's refactor has `hits` for one note per
track; extend to chords). Verify `PLATFORMIO_BUILD_FLAGS="-DBENCH_MAX_GROUP=4" pio run -e
teensy41-bench-max`.

### Task 11: full verification, bench on the Teensy, docs

**Files:** `docs/plans/future-teensy.md`, `docs/manual.md`, `docs/manual_ru.md`, `CHANGELOG.md`,
`CHANGELOG_ru.md`.

1. `pio test -e native` all PASS; `pio run -e wt32`, `-e teensy41`, `-e teensy41-desk`; note RAM1 / RAM2.
2. Bench: `PLATFORMIO_BUILD_FLAGS="-DBENCH_MAX_GROUP=4" pio run -e teensy41-bench-max -t upload`, then
   `~/.platformio/penv/bin/python -I scripts/link_desk.py --port /dev/cu.usbmodem105489401 status 300`.
3. Apply the cut-offs (MODAL modes 8 → 6 → 4; STRING without dispersion), re-run until
   `16 MODAL + CHIP` <= 64 %.
4. future-teensy.md: dated paragraph (STRING / MODAL price, low-note path, worst case, RAM).
5. Manual (en / ru, same content): `### STRING` after MACRO (models, the five macros, exciter, natural
   decay vs ADSR, lowest note 21.5 Hz, COMB not available on STRING, heavy voice), LFO Dest names for
   STRING, fx DEC..CON on STRING, Presets folder. No original product names.
6. CHANGELOG.md / CHANGELOG_ru.md: "New instrument type STRING: plucked string and modal resonator".
