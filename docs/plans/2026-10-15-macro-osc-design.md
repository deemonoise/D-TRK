# MACRO instrument type (macro oscillator) — design

Feature B of the 2026-10-15 sound batch. Item 6 of `2026-10-14-sound-next.md`. Builds on A
(`2026-10-15-synth-fold-filters-design.md`): the RAM1 headroom (cold code in flash), `synth_shape.h`
(fold curve), the bench ladder refactor with `BENCH_MAX_GROUP`, the new filter types (MACRO plays
through the same per-voice filter as every type). C (STRING) builds on the heavy-engine groundwork added
here.

## Goal

A new heavy instrument type **MACRO**: one oscillator with a choice of **models** and two continuous
controls, **TIMBRE** and **COLOR**, in the manner of a well-known open-source Eurorack macro oscillator
(MIT). Amp ADSR, filter, LFOs, track insert and sends as every other type. Counts against the heavy cap
(`kFmVoiceMax`, 16 on the Teensy).

**Naming rule:** the original module and its maker are never named in the UI, the manual, type or model
names, preset names or the changelog. Only source file headers carry the copyright and the MIT notice, and
`LICENSES/` the license text.

## Port approach: rewrite in float, do not vendor

The original code is int16 fixed point at 96 kHz in 24-sample blocks, uses its own phase-increment and
waveshaper look-up tables generated for 96 kHz, and depends on a support library (fixed-point helpers,
random, DSP utils). Its resource file holds tens of KB of tables; on the Teensy `.rodata` is copied into
DTCM, which has ~37 KB left (design A). Vendoring would cost RAM1 we do not have, pull in a second DSP
style and still need retuning for 44.1 kHz.

So: **re-implement the chosen models in float at 44.1 kHz**, following the original algorithms and
parameter mappings, on our primitives: `BlOsc` (PolyBLEP saw / square / tri, synth_syn.h), `tableSine`
(synth_osc.h, the FM / DRUM sine table: no new table), `fastTanh` / `fastExp2` (fastmath.h), the fold
curve of `synth_shape.h`, `Svf`. Small data tables that are ported verbatim (vowel formant data, 9 x 6 +
8 x 6 bytes) carry the attribution. Files derived from the original get its header:

```
// Derived from the macro oscillator in github.com/pichenettes/eurorack, MIT License.
// Copyright 2012 Emilie Gillet. <full MIT permission notice>
```

and `LICENSES/MIT-eurorack.txt` holds the full text (shared with C, which derives from the same
repository). README's license list gets one line ("DSP derived from MIT-licensed Eurorack firmware,
see LICENSES/").

## Models

Checked against the original source (`macro_oscillator.cc`, `digital_oscillator.cc`). Eight models, all
cheap enough at 44.1 kHz float; several of the original's digital models already run at half rate (48
kHz effective), so 44.1 kHz does not lose anything they had.

| # | UI name | Original | TIMBRE (t, 0..1) | COLOR (c, 0..1) | Our building blocks |
|---|---|---|---|---|---|
| 0 | SAW+ | CSAW | width of the notch at the start of the ramp | notch depth / DC shift | 1 `BlOsc` saw + a BL pulse edge pair |
| 1 | MORPH | MORPH | tri -> saw -> square -> pulse width (3 zones, crossfades) | one-pole LP relative to pitch + overdrive amount (less above ~830 Hz) | 2 `BlOsc`, 1-pole, `fastTanh` |
| 2 | SAW/SQR | SAW_SQUARE | shape of both (saw notch, square PW) | saw -> square balance (square x 0.58) | 2 `BlOsc` |
| 3 | FOLD | SINE_TRIANGLE | fold amount (pitch-limited as the original) | sine fold -> triangle fold balance | `tableSine` + fold curve |
| 4 | VOSIM | VOSIM | formant 1 frequency | formant 2 frequency | main phase, 2 sines reset per period, bell window (`tableSine`) |
| 5 | VOWEL | VOWEL | vowel (9 phonemes, neighbours blended) | formant shift x0.78 .. x2.78 | 3 formant sines reset per period, decaying amplitude, consonant noise burst on trigger, soft clip |
| 6 | FM | FM | modulation index | ratio, quantised to musical ratios | 2 `tableSine` |
| 7 | FB FM | FEEDBACK_FM | index (attenuated at high pitch x ratio) | ratio | 2 `tableSine` + previous-sample feedback |

Not taken, and why:
- **PLUCK** (PLUCKED): three round-robin Karplus-Strong voices with 1 KB int16 lines each *inside one
  voice* — it is a polyphonic engine in a voice slot; STRING (feature C) covers plucked sounds properly.
- BOWED / BLOWN / FLUTED / STRUCK *, VOWEL_FOF (5 SVF), HARMONICS (14 partials), WAVETABLES / WAVE_MAP
  (the original's 129-wave table: RAM), TRIPLE / SWARM / CLOUD / PARTICLE (several oscillators or grains
  per voice: cost), drums (DRUM covers them), noises: YAGNI or over budget.

Level: each model is scaled so a 220 Hz note at t = c = 0.5 is within ±3 dB RMS of a SYNTH saw at the same
settings (one constant per model, set by test).

Smoothing: t and c ramp per sample over the control period (32 samples), as SYNTH's shape / mix; a model
change applies at the next note (latched at note-on like FM / DRUM machines).

## Parameters and model fields

No new `Instrument` fields:

- `type = InstrType::Macro` (new enum value, appended before `Count`, = 6).
- `machine` = `MacroModel { SawPlus, Morph, SawSqr, Fold, Vosim, Vowel, Fm, FbFm, Count }` (model.h;
  stored in files: new models before Count only; `kMachineMax` 16 still holds). `macroModelName()`.
- Macro slots (fx DCY / COL / SHP lock them, LFO targets Dec / Col / Shp drive them):
  - `macro[0]` TIMBRE (velocity -> macro 0 via `velMac` works as on the other types),
  - `macro[1]` COLOR,
  - `macro[2]` ENV>TMB: bipolar amount (64 = none) of an AD envelope on TIMBRE, times `synEAtk` /
    `synEDec` — SYNTH's env->SHAPE fields reused with the same meaning (saved in SYNI for every
    instrument already).
  - `macro[3]`, `macro[4]`: unused (LFO targets SWEEP / CONTOUR invalid for MACRO).
- `instrSetType(Macro)`: macros {64, 64, 64, 0, 0}, machine clamped (>= Count -> 0). `fixInstrument`:
  machine < `MacroModel::Count`. `lfoDestValid`: Dec / Col / Shp only for MACRO; `lfoDestName`: TIMBRE,
  COLOR, ENV>TMB.
- Gated like SYNTH: the instrument's ADSR, MONO / POLY, glide.

## Files and presets

- Projects: INST byte 8 = 6; machine and macros in FMIN, env times in SYNI — all existing chunks, no new
  chunk, no record size change. Old firmware reading the file: `unpackInst` maps the unknown type to
  CHIP (loads, sounds different).
- Presets (.mti): same records; `kPresetVersion` unchanged. `preset_paths`: `"MACRO"`,
  `"/presets/MACRO"`. Factory presets: one per model (8), categories LEAD / BASS / PAD / VOX / KEYS,
  generic names (e.g. `NOTCHSAW`, `MORPHBAS`, `FOLDLEAD`, `VOSIMPAD`, `TALKER`, `FM BELL`, `FB GRIT`,
  `SQRSAW`).
- KIT lanes may use a MACRO instrument (heavy as any other).

## Synth integration (shared groundwork for C)

- `EngineKind::Macro`; `VoiceEngine` union gets `MacroVoice` (trivially copyable, static_assert as the
  others). Size target <= the current largest member (DRUM), so `Voice` does not grow.
- **Heavy flag:** `Voice::heavy` set at note-on = `fm || drumT || synWtT || macroT` (C adds `stringT`);
  `heavyLoad()` reads it instead of listing types; the note-on's `heavy` local and the macro-lock rule
  (`lockMask` keeps macro bits for heavy or SYNTH) use the same predicate. One place to extend in C.
- `Voice::mac` flag (C adds `str`); `control()` dispatches `controlMacro` (macros + LFO + velocity via
  `macros()`, env->TIMBRE like `controlSyn`'s SENV, hz from `cachedHz`), `renderVoice` treats it like
  the other engines (`tmp` buffer, `Env`).
- **Profile:** `ProfStage` gains `kProfMacro` appended before `kProfStages` (name `"macro"`);
  `voiceStage()` maps it. `link::ProfileRep::kStages` 22 -> 23 (static_asserts in link_server.cpp /
  audio_link.cpp follow).
- **kProtocol** 5 -> 6 (ProfileRep layout changed; `SynthModel` does not change in B).
  `scripts/link_desk.py` `PROTOCOL = 6` and its self-test.

## UI (ESP, INST)

- Type list: `MACRO` (UI order FM, SYNTH, MACRO, STRING*, DRUM, SAMPLE, CHIP, KIT; `kTypeOrder` gets MACRO
  after SYNTH). Every array sized by `InstrType::Count` in `inst_screen.cpp` grows (names, page names).
- Pages: MAIN, ENV, **OSC**, FILT, LFO (one type page, no MOD page). OSC rows: `Model` (SAW+, MORPH,
  SAW/SQR, FOLD, VOSIM, VOWEL, FM, FB FM), `Timbre`, `Color`, `Env>Tmb` (±, 0 = off), `Env atk`,
  `Env dec` (dimmed when Env>Tmb is 0). ADSR / Mode / Glide rows as SYNTH.
- Preset browser: MACRO folder appears through `preset_paths` / `factoryCount`.

## CPU and RAM (Teensy)

Per voice, estimates (1 % ≈ 136 cycles / sample; overhead of a heavy voice — control, env, `tmp` copy,
LP filter — ≈0.8 %): SAW+ ≈1.2 %, SAW/SQR ≈1.4 %, MORPH ≈1.6 %, FOLD ≈1.4 %, VOSIM ≈1.3 %, VOWEL ≈1.7 %,
FM ≈1.3 %, FB FM ≈1.4 %. All under WT SYNTH (2.4 %).

- Ship target: every model <= 2.4 % (WT SYNTH). Worst case **16 MACRO (worst model) + 16 CHIP, all fx,
  <= 64 %** (the pool's worst case today).
- Cut-off: a model > 2.4 % is simplified (MORPH: drop the overdrive; VOWEL: 2 formants); > 2.9 % (DRUM) it
  is not shipped (removed from the enum before release; the slot stays free for later — enum values are
  file values, so this is decided before the first release, not after).
- RAM: no tables (sine shared), vowel data ~100 B in DTCM `.rodata`; `MacroVoice` ~120 B inside the
  existing union; code ~6 KB ITCM (hot loops) — the RAM1 gate of A must hold.

Whether MACRO should count as heavy at ~1.3 % (CHIP is 1.1 %) is an open question; the default follows
the request (heavy).

## Bench plan (group 3)

Ladders (held 4-note chords per track, +4 per step to the guard, top 48 in the bench env): `MACRO SAW+`,
`MACRO MORPH`, `MACRO SAW/SQR`, `MACRO FOLD`, `MACRO VOSIM`, `MACRO VOWEL`, `MACRO FM`, `MACRO FB FM` (t 0.7,
c 0.6, LP filter on, sends as the other ladders), then `16 MACRO <worst> + CHIP`. References in the same
run: `fx only`, `WT SYNTH`. Profile stage `macro` shows the engine share. ~8 min per run.

## Tests (native)

- New `test/test_synth_macro`: per model — fundamental at the note (Goertzel at f beats f / 2 and 1.5 f),
  bounded |y| <= 1.5 and no NaN over t, c in {0, 0.5, 1} and 30 Hz .. 8 kHz; t and c each change the
  spectrum (centroid moves); level within ±3 dB of the SYNTH saw at 220 Hz; SAW+ / SAW/SQR / MORPH
  alias below a naive saw (bin at a known alias frequency, as test_synth_syn does); FOLD pitch limit;
  VOSIM formant peaks follow t and c; VOWEL t = 0 vs 1 moves F1 / F2; FM index 0 = pure sine (2nd
  harmonic < -60 dB); FB FM bounded at full feedback; trigger() resets phases (two renders equal).
- test_model: type order, instrSetType(Macro) defaults, fixInstrument clamp, LFO dest validity / names.
- test_synth: MACRO notes count as heavy (native cap 8: the 9th steals), freed after release; macro locks
  and LFO TIMBRE move the output; profile counts land in `kProfMacro`.
- test_project_io / test_preset_io: MACRO instrument round trip (type, machine, macros, env times).
- test_presets_factory: 8 MACRO presets build, names <= 8 chars, unique.
- test_preset_paths: MACRO folder. test_link_msg: `kProtocol == 6`, ProfileRep 23 stages round trip.

## Open questions

1. Model set: the eight above **[default]**; swap one for PLUCK (needs a small polyphonic KS per voice) or
   HARMONICS (additive, ~+1 %).
2. MACRO heavy **[default, as asked]**, or light if the bench shows every model <= CHIP + 0.3 %.
3. ENV>TMB using SYNTH's env times **[default]**, or a dedicated COLOR envelope too (one more macro slot).
4. Type name `MACRO` **[default]** (alternatives: `MORPH`, `VOX`).
