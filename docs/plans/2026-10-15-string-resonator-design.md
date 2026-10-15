# STRING instrument type (string / modal resonator) — design

Feature C of the 2026-10-15 sound batch. Item 7 of `2026-10-14-sound-next.md`. Builds on:
- A (`2026-10-15-synth-fold-filters-design.md`): RAM1 headroom, the `VoiceLines` pool (512 int16 per
  voice in RAM2, made for COMB) which holds the string's delay line, bench groups (`BENCH_MAX_GROUP` 4).
- B (`2026-10-15-macro-osc-design.md`): `Voice::heavy` single predicate, the engine / profile pattern
  (`EngineKind`, union member, `voiceStage`, `ProfileRep` stage), the MIT attribution file
  `LICENSES/MIT-eurorack.txt`, the "no brand names" rule.

If A shipped without COMB, the pool is still added (it is A's groundwork task, independent of COMB).

## Goal

A heavy instrument type **STRING**: a physical-model resonator excited by an internal click / noise
burst, in the spirit of a well-known open-source Eurorack resonator (MIT). Two models:

- **STRING** — Karplus-Strong waveguide: delay line, damping low-pass, dispersion all-pass (stiffness),
  pluck position.
- **MODAL** — 8 tuned band-pass modes (bars, plates, bells, marimba).

Parameters: **STRUCT** (structure), **BRIGHT** (brightness), **DAMP** (decay), **POS** (excitation
position), **EXC** (exciter). Amp ADSR, filter, LFOs, insert and sends as every type; counts against the
heavy cap (16).

**Naming rule:** as B — the original module and maker never appear in the UI, manual, type / model /
preset names or the changelog; derived source files carry the copyright and MIT notice (Copyright 2015
Emilie Gillet, `rings/dsp/string.cc`, `resonator.cc`), `LICENSES/MIT-eurorack.txt` the text.

## Port approach

The original resonator is already float (48 kHz, 24-sample blocks), so the maths ports directly; we
re-derive the rate-dependent constants for 44.1 kHz and re-implement on our structures (no vendoring: its
support library, its tables in `.rodata` → DTCM, and its 64-mode / polyphonic design are not what we
need). Taken from it: the partial-frequency stretch from STRUCT (stiffness), the brightness → per-mode Q
loss, the position → mode amplitude law, the string's damping / dispersion filters. Mode count 8 instead
of up to 64; one voice per note (our pool does polyphony).

**Input of another track:** not done. The voices render before the tracks' strips, a track's audio of
the current block does not exist yet when a voice runs (one block of latency at best), and it needs a
routing UI and per-voice input buffers. YAGNI; listed as an open question.

## DSP

Pitch `hz` from the note (glide, LFO PITCH, vibrato as every type). Macros 0..127 → 0..1: d (DAMP),
b (BRIGHT), s (STRUCT), p (POS), e (EXC).

**Exciter** (both models), at note-on:
- `e` = 0: a single impulse; `e` > 0: a noise burst of `0.5 + 20 e²` ms with a fast decay.
- One-pole low-pass on the excitation at `hz x 2^(1 + 6 b)` (brighter strike = brighter tone).
- Level = velocity (as `gain` today); re-trigger (a new note on a sounding voice) adds a new burst into
  the running resonance (no reset unless the voice is new).
- STRING pluck position: feed-forward comb on the excitation `x[n] - x[n - P]`, P = p x 0.5 x period.
  No history buffer: the noise comes from an LFSR; a second LFSR started P samples later regenerates
  `x[n - P]` exactly (two LFSR steps and two one-pole states per sample, only during the burst).

**STRING** (Karplus-Strong):
- Line: the voice's `VoiceLines` line (512 int16, scaled `x 8192`, saturating write, 12 dB headroom).
  D = sr / hz with a fractional read (linear; Thiran all-pass optional if tuning tests fail). Below
  86 Hz the loop runs at half rate (D/2 at 22.05 kHz: write every 2nd sample, output interpolated), below
  43 Hz at quarter rate; lowest 21.5 Hz, lower notes hold.
- In the loop: damping one-pole LP (cutoff from b, relative to hz), dispersion first-order all-pass
  (coefficient from s: 0 = harmonic, 1 = stiff / inharmonic; the delay is shortened by its group delay
  so tuning holds), loop gain from d: `g = 10^(-3 x period / T60)`, `T60 = 0.05 s x 200^d` (0.05..10 s).
- Tuning target: fundamental within ±5 cents from 40 Hz to 2 kHz at s = 0.

**MODAL** (8 modes):
- Mode k frequency: `hz x r_k`, r_1 = 1, r_{k+1} = r_k + stretch, `stretch *= stiffness(s)` (the
  original's law: s = 0 → harmonic series, mid → bar-like, high → bell / plate, partials above 0.45 sr
  dropped).
- Each mode an `Svf` band-pass (TPT, as today's filter) with Q from `T60(d)` and the brightness loss
  (higher modes lose Q faster as b falls); amplitude `|cos(π k p)|`-type law from POS (position nulls).
- Coefficients at control rate (8 `fastTan`) only when hz, s, b, d moved (cached keys as `octKey`).

**Done:** each block the engine tracks the output peak; once the burst is over and the peak stays below
1e-4 (-80 dB) for a control period, `done()` returns true and `renderVoice` kills the envelope (as FM /
DRUM do) — the heavy slot frees even while the note is held.

## Model and files

No new `Instrument` fields:

- `InstrType::String` (= 7, before `Count`). UI order FM, SYNTH, MACRO, STRING, DRUM, SAMPLE, CHIP, KIT.
- `machine` = `StringModel { String, Modal, Count }` (stored in files), `stringModelName`.
- Macro slots: `macro[0]` DAMP (velocity → macro 0 via velMac: harder = longer, as the DECAY macro
  elsewhere), `macro[1]` BRIGHT, `macro[2]` STRUCT, `macro[3]` POS, `macro[4]` EXC. Fx DCY..CON lock
  them, LFO targets Dec..Con move them; `lfoDestName` for STRING: DAMP, BRIGHT, STRUCT, POS, EXC.
- `instrSetType(String)`: macros {80, 64, 0, 30, 10}, machine clamped. `fixInstrument` clamps machine.
- Gated: ADSR applies on top (sustain holds the natural decay, release fades). MONO / glide work (the
  string glides, MODAL retunes per control update).
- Files: INST / FMIN only, no new chunk, no record change; old firmware loads the type as CHIP.
  Presets: `"STRING"`, `"/presets/STRING"`; factory presets (6): `NYLON`, `HARP`, `PIZZ` (STRING);
  `MARIMBA`, `BELL`, `KALIMBA` (MODAL).

## Synth integration

- `EngineKind::String`; `VoiceEngine` union member `StringVoice` (~260 B: 8 modes x (2 states + 3
  coefficients + amp) + string / exciter state). If it exceeds the current largest member, `Voice` grows
  by the difference x 32 (budget <= 4 KB DTCM, checked by the RAM1 gate).
- `Voice::str`; the heavy predicate gains `stringT`; `control()` → `controlString`; `renderVoice` passes
  the voice's line (`voiceLine(&v - voices_)`), and kills the env on `done()`.
- Profile `kProfString` ("string") appended; `ProfileRep::kStages` 23 → 24; **kProtocol 6 → 7**;
  `link_desk.py`. `SynthModel` unchanged.
- The filter of A follows the resonator as for every type (a STRING through FORMANT or COMB is allowed).
  A voice using COMB and STRING at once needs two lines: STRING takes the voice's line; COMB on a STRING
  voice is unavailable (the FILT `Filter` row skips COMB on STRING; the synth treats it as Off). Simpler
  than a second pool.

## UI (ESP, INST)

Pages MAIN, ENV, **RES**, FILT, LFO. RES rows: `Model` (STRING / MODAL), `Damp` (shown as T60: `0.05 s ..
10 s`), `Bright`, `Struct`, `Position` (%), `Excite` (`CLICK` at 0, else burst ms). ADSR / Mode / Glide as
SYNTH (POLY default).

## CPU and RAM (Teensy)

Estimates per voice (heavy overhead ≈0.8 %): STRING ≈1.3 % (+0.1 during the burst), MODAL (8 modes)
≈1.9 %.

- Ship target: STRING <= 2.0 %, MODAL <= 2.4 %; worst case **16 STRING / MODAL (retriggered) + 16 CHIP,
  all fx, <= 64 %**.
- Cut-off: MODAL > 2.4 % → 6 modes, > 2.9 % → 4 modes (a constant, not a file value); STRING > 2.0 % →
  drop the dispersion all-pass (STRUCT then only shortens the decay of high partials); still > 2.9 % →
  not shipped.
- Applied (2026-10-15 bench: STRING ≈2.0 %/voice, MODAL ≈2.6 %): MODAL cut to 6 modes
  (`StringVoice::kModes`), its gain raised 2.2 → 2.42 to keep the level matched to STRING.
- RAM: line from A's pool (no new RAM2); `StringVoice` in the union (see above); no tables (sine for the
  position law via `tableSine`); code ~4 KB ITCM.

## Bench plan (group 4)

Ladders, +4 voices per step to the guard (top 48): `STRING HIT` and `MODAL HIT` (4-note chords per track
re-struck every 1/8 at 120 BPM, DAMP 100 so they ring through), `STRING LOW` (notes 28..40: half / quarter
rate path), `16 MODAL + CHIP`; references `fx only`, `WT SYNTH`, `DRUM`. Profile stage `string`. A
retriggered resonator is the expensive case (burst + full ring); held notes cost less after `done()`.

## Tests (native)

New `test/test_synth_string`:
- STRING tuning: zero-crossing / autocorrelation period at 55, 110, 440, 1760 Hz within ±5 cents (s = 0);
  low notes (30 Hz, half / quarter rate) within ±10 cents.
- T60: DAMP 0.2 / 0.8 → measured -60 dB time within ±25 % of the formula.
- BRIGHT 0 vs 1: HF energy (bins above 4 kHz) lower by >= 10 dB at 0.
- POS 0.5: even harmonics (2, 4) at least 15 dB under odd (1, 3) for STRING; MODAL mode-2 amplitude
  near 0 at p = 0.5.
- MODAL s = 0: partials at 1, 2, 3, 4 x f (±2 %); s = 1: ratio of mode 2 / mode 1 > 2.5 (inharmonic).
- Bounded |y| < 2 and no NaN for every macro at 0 / 0.5 / 1 and 30 Hz .. 4 kHz; repeated re-strikes
  (100 in a row) stay bounded (int16 saturation holds the loop).
- `done()` true within T60 + 0.1 s after one strike; false during the burst.
- Exciter twin-LFSR: `x[n - P]` equals the delayed stream exactly.

test_model (type order, defaults, LFO names), test_synth (heavy cap, done frees the voice while held,
locks / LFO DAMP move the output, profile `kProfString`, STRING + COMB filter treated as Off),
test_project_io / test_preset_io (round trip), test_presets_factory (6 presets), test_link_msg
(`kProtocol == 7`, 24 stages).

## Open questions

1. Models: STRING + MODAL **[default]**; a third (e.g. sympathetic strings) later if wanted.
2. Mode count 8 **[default]**, cut to 6 / 4 by the bench.
3. Input of another track as exciter: no **[default]** (see above).
4. COMB filter unavailable on STRING **[default]**, or a second line pool (16 x 1 KB RAM2).
5. Excite: click / noise burst by one knob **[default]**; a separate "mallet" (soft LP'd impulse) model
   would be a third exciter value range.
