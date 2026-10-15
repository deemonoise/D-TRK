# SYNTH wavefolder, ring mod and new filter types — design

Feature A of the 2026-10-15 sound batch (A: this, B: MACRO `2026-10-15-macro-osc-design.md`, C: STRING
`2026-10-15-string-resonator-design.md`). Item 8 of `2026-10-14-sound-next.md`, done first because it
lays the groundwork B and C build on (RAM1 headroom, the voice line pool, the bench ladder refactor).

Branch: `feature/teensy` only. The synth renders on the Teensy (44.1 kHz, 32 voices / 16 heavy); the ESP
on this branch renders no audio. `main`'s ESP synth (24 / 8, 240 MHz) has no budget for these and does
not get them (see open questions).

## Goal

- SYNTH: a **wavefolder** (`Fold`) and a **ring modulator** (`Ring`) on the oscillators.
- Four new filter types next to LP / BP / HP: **LADDER** (4-pole 24 dB LP, Moog style), **MS20**
  (2-pole Sallen-Key LP with a saturating feedback loop), **COMB** (tuned feedback comb), **FORMANT**
  (three band-passes morphing through the vowels A E I O U).

Everything runs per voice, so its cost multiplies by 32 (rule in sound-next). Each part has a price
target and a cut-off threshold (CPU section).

## Existing (what this builds on)

- `SynVoice` (synth_syn.h/.cpp): two oscillators (BL saw / square / tri via `BlOsc`, or wavetable via
  `WtOsc`), `mix` crossfade ramped over the control period, optional hard sync, sub, noise; output
  `a[i] * amp * kSynGain`. An oscillator out of the mix for the whole block is skipped.
- `SynMacro`: SYNTH uses all five macro slots (SHP1, SHP2, MIX, DET, SENV). Fx DCY..CON lock them.
- Lock bits are full: `kLocks == 16`, `lockMask` is a `uint16_t` (model.h). No new per-step lock is
  possible without widening `lockMask` (not done here, see YAGNI).
- `ModelDrive` (synth_drive.h) has a triangle fold with closed-form antiderivative (first-order ADAA),
  per-track; its curve maths is what the SYNTH fold reuses.
- Filter: `Instrument::fltMode` (`FltMode { Off, Lp, Bp, Hp, Count }`), cutoff, reso, fenv, fAtk, fDec,
  keytrack, velCut — on **every** type. `Synth::controlFilter` sums cutoff + LFO + env + key track +
  velocity into octaves above 20 Hz (`v.octKey` / `v.octHz` cache) and calls `Svf::set` at control rate;
  `renderVoice` renders the engine into `flt[]` then `v.flt.processAdd`. `Voice::flt` is an `Svf`
  (Simper TPT). A finished note stays allocated while `flt.ringing()`.
- The FILT page rows come from `InstScreen::initTail` and are shared by every type.
- `unpackFlt` maps a mode >= `FltMode::Count` to Off; `unpackSyn` reads bytes 0..40 of the 48-byte SYNI
  record (41..47 reserved, written 0).
- Teensy RAM1 (2026-10-15 build, `teensy41`): ITCM 251.0 KB code in 8 banks of 32 KB (**~5 KB to the next
  bank**), DTCM 36.5 KB data + 182.8 KB bss of the remaining 256 KB (~37 KB for the stack). Read-only
  data (`.rodata`) is copied to DTCM (custom `imxrt1062_t41_mt.ld`). RAM2 (OCRAM): 150 KB static, ~90 KB
  heap free at run time (future-teensy.md). New hot code that crosses the ITCM bank boundary takes 32 KB
  from DTCM: the stack would be left with ~5 KB. **This is the main risk of all three features.**

## Decisions

### 1. Wavefolder (SYNTH)

- Position: `osc mix (with ring) -> fold -> + sub + noise -> amp`. Sub and noise stay clean (the sub is
  the bass anchor, folded noise is just louder noise).
- Curve: the triangle fold of the track insert (period 4, `f(0)=0, f(1)=1, f(2)=0, f(3)=-1`) with its
  first-order ADAA. Gain `g = 1 + fold * 7 / 127` (1..8, up to ~4 folds), make-up `kFoldGain`. The fold
  maths (`f`, `F`, the fallback for tiny steps) moves from `synth_drive.*` into a shared header
  `synth_shape.h` (`foldCurve`, `foldAnti`); `ModelDrive` calls it and stays bit-exact
  (test_synth_drive).
- Anti-aliasing at 44.1 kHz: ADAA plus a pitch limit as in the original macro oscillator's fold: above
  osc 1 at ~1 kHz the effective fold amount falls linearly to 0 at ~5 kHz (control rate, one multiply).
- `fold == 0` (and LFO / follower sum 0): the fold is skipped, output bit-exact to today.
- State: 2 floats per voice in `SynVoice` (previous input, previous `F`); reset on `trigger()`.

### 2. Ring modulator (SYNTH)

- `ring` 0..127 crossfades the oscillator mix towards `osc1 * osc2 * kRingGain`:
  `m = mix(a, b, MIX); out = m + (a * b * kRingGain - m) * ring / 127`. Ramped over the control period
  like `mix`. Osc 2's Semi2 / Detune set the ratio: bells, metal, AM growl.
- `ring > 0` forces both oscillators on (`need1 && need2`).
- `ring == 0`: bit-exact to today.

### 3. Filter types (every instrument type)

`FltMode { Off, Lp, Bp, Hp, Ladder, Ms20, Comb, Formant, Count }` (stored in files: new values before
Count only). UI names `OFF LP BP HP LADDER MS20 COMB FORMANT`.

**Why every type and not SYNTH only:** the filter is already generic (one `controlFilter`, one FILT page
for all types, one Voice member); restricting it would need per-type mode lists in the UI and a clamp in
the codec for no saving — an unused mode costs nothing (a switch at control rate). The risk (32 light CHIP
/ SAMPLE voices all on FORMANT) is bounded by the CPU guard (`Synth::setLoad` sheds voices past 80 %) and
by the price targets below; the bench measures exactly that case. New filters do **not** make a voice
heavy (a filter costs ~0.1–0.4 % per voice; heavy engines cost 2–3 %).

Common mapping: `hz` = today's cutoff path (cutoff / lock + LFO + env + key track + velocity, clamped
20 Hz .. 0.45 sr); `r` = reso (lock + LFO) / 127 in 0..1. Each type maps `r` itself (not through
`resoQ`). A mode change mid-note resets the filter state (as switching the filter on mid-note does).

| Mode | Structure | `r` maps to | Notes |
|---|---|---|---|
| LADDER | 4 TPT one-poles, zero-delay feedback solved linearly (Zavalishin, "The Art of VA Filter Design", ch. 5), one saturator `vfSat` on `x - k * y4` | `k = 4.2 r` (linear threshold k = 4: self-oscillates from r ~ 0.95) | passband loss compensation `x * (1 + 0.5 k)`; LP 24 dB/oct |
| MS20 | Korg-35 Sallen-Key 2-pole LP (TPT: LP1 -> LP2, the output fed back with gain K through a one-pole HP into LP2's input, zero-delay solve; linear `1 / (s^2 + (2 - K) s + 1)`, unity at DC), saturator `vfSat` on LP2's input (signal + feedback, so r = 0 is slightly colored at full scale) | `K = 2.1 r` (threshold K = 2: self-oscillates from r ~ 0.95) | screams at high reso, stays bounded by the saturator (|y| < 1.6 at full scale); LP 12 dB/oct |
| COMB | feedback comb `y = x + fb * s[n - D]`, `s = 2 vfSat(y / 2)` (saturated before the line write), D = sr / hz, linear-interpolated read (slight HF damping), D ramped over the block on a change (no zipper) | `fb = 0.97 r` | the cutoff is the comb's pitch (key track 100 % = tuned to the note); out `s / (1 + fb)`: unity at r 0, a tuned saw 0.8 peaks <= ~1.0 at any r (the raw peak gain is 1 / (1 - fb), 33x at r 1) |
| FORMANT | 3 parallel `Svf` band-passes F1, F2, F3 (amplitudes 1, 0.5, 0.25) | `Q = 2 + 18 r` | the cutoff (without key track) is the vowel position, see below |

- **COMB line**: per voice, 512 int16 samples (1 KB) in RAM2: `VoiceLines` pool, `kVoices x 512`
  (32 KB on the Teensy, `DMAMEM`; static array on native). D <= 511 -> lowest pitch 86 Hz (F2); a lower
  cutoff holds D at 511. Holds `vfSat(y / 2) * 32766`: |y| <= 1 + 0.97 x 2 < 3 keeps the write off
  vfSat's clamp for full-scale input, so the loop cannot blow up and the line never hard-clips; the
  line is cleared on reset (RAM2 is not zeroed), before the next read. Index = voice index (`&v - voices_`), no allocation. **STRING
  (feature C) reuses this pool** for its string delay line.
- **FORMANT vowels** (male formants, Hz): A 730 / 1090 / 2440, E 530 / 1840 / 2480, I 270 / 2290 / 3010,
  O 570 / 840 / 2410, U 300 / 870 / 2240. Position `p = clamp(oct / 9.45, 0, 1) * 4` over A..U, formant
  frequencies interpolated in log (exp2 of the lerp of log2s, `fastExp2`). Key track does not apply
  (the row is dimmed); env / LFO / velocity on the cutoff sweep the vowel ("wah-yah"). `set()` only when
  `p` or `Q` changed (three `fastTan` per change).
- **Saturator** `vfSat`: x clamped to +-1.5, `x - (4 / 27) x^3` (tanh-like: slope 1 at 0, +-1 with zero
  slope at the clamp), 5 ops; `fastTanh` (~20 ops) would double the LADDER loop.
- **LADDER / MS20 tail fade**: Synth applies the amp envelope before the filter, so a self-oscillating
  filter would keep a released voice forever. A LADDER / MS20 tail (`in == nullptr`) fades out
  (-60 dB in 50 ms, per sample) at once when over the threshold (k > 4, K > 2), and after a 2 s cap
  otherwise (slow decays just under it); `ringing()` is false once the fade is under -60 dB. Input
  restores full level. LP / BP / HP do not fade (bit-exact).
- **Ringing tails**: `VoiceFilter::ringing()` per mode (LADDER / MS20 states, COMB line energy via a
  running peak of the last control period, FORMANT three `Svf::ringing()`), so a tail is freed as today.

### 4. Architecture

- New `synth_shape.h`: fold curve + antiderivative (shared by `ModelDrive`, `SynVoice`, and B's FOLD).
- `SynParams` gains `fold` (0..1) and `ring` (0..1); `SynVoice` ramps them like `shape` / `mix`.
- New `synth_vfilter.h/.cpp`: `VoiceFilter` = mode + union { `Svf`; `Ladder`; `Ms20`; `Comb` (state:
  write index, delay ramp; the interpolated read damps the highs slightly); `Formant` (3 x `Svf`) }. API mirrors `Svf`: `set(mode, hz, r)`,
  `processAdd(in, out, n, int16_t* line)`, `ringing(line)`, `reset()`. LP / BP / HP delegate to `Svf`
  with today's `resoQ(reso)` — **bit-exact** for existing projects. Hot paths `MT_HOT`, one loop per mode
  (no per-sample switch), state in locals over the block (as `Svf::processAdd`).
- `Voice::flt` becomes `VoiceFilter`; `Voice::fltQ` / `fltRes` caches stay for LP / BP / HP.
  `controlFilter` computes the octave sum as today, keeps the key-track term separate for FORMANT, and
  calls `v.flt.set`. `renderVoice` passes `lineOf(v)`.
- `hot.h`: `MT_RAM2` (Teensy `DMAMEM`, empty elsewhere) for the line pool.

### 5. Model and files

- `Instrument`, at the end (after `driveModel`): `uint8_t synFold = 0; uint8_t synRing = 0;` (0..127).
- SYNI record: byte 41 = synFold, 42 = synRing (were reserved 0: old files and presets load "off").
  `kSynRecSize` stays 48, `kPresetVersion` stays 5 (record sizes unchanged).
- FLTR byte 0 takes the new FltMode values. Old firmware reading a new file: unknown mode -> Off (the
  sound changes, the file loads).
- `LfoDest`: `Fold`, `Ring` appended before `Count` (stored in files). `kDestOrder`: after `Semi2`. Names
  `FOLD`, `RING`. `lfoDestValid`: SYNTH only. Full depth = +-64 steps. The envelope follower gets them
  too (folDestStep walks lfoDestStep). Old firmware: dest >= Count -> PITCH (unpackLfo).
- No new fx and no lock bit (the mask is full); fold / ring are LFO / follower targets only.
- KIT, presets, demos: nothing to migrate. Factory presets: two new SYNTH presets showing fold and ring,
  three showing LADDER / MS20 / FORMANT on existing types (optional, last plan task).

### 6. Link mirror

`Instrument` grows by 2 bytes -> `sizeof(SynthModel)` changes (Hello `modelSize` already refuses a
mismatched pair). Bump `kProtocol` 4 -> 5 (rule of sound-next), `scripts/link_desk.py` `PROTOCOL = 5`
and its self-test assert, `test_link_msg`. `test_synth_model` (chunk tiling) must stay green.

### 7. UI (ESP, INST screen)

- FILT page, `Filter` row: 8 values. `Cutoff` shows Hz for every mode but FORMANT, where it shows the
  vowel (`A`, `A>E`, `E`, …) — the position the knob sets, not the modulated one. `Reso` label stays.
  `Key track` dimmed for FORMANT. Value width: `FORMANT` is 7 chars, as wide as `14.0 kHz`.
- SYNTH MOD page (`kPgType2`): new rows `Ring` (after Noise) and `Fold` (after Ring): `0..127`, 0 shown
  `OFF`. Rows: Sub, Sub oct, Noise, Ring, Fold, Env>Shp, Env atk, Env dec.
- LFO Dest list: `FOLD`, `RING` on SYNTH. TRACK FOL Dest likewise.

## CPU and RAM (Teensy, 44.1 kHz)

1 % of the CPU = ~136 cycles per sample. Today: CHIP + LP ≈1.1 %/voice, WT SYNTH ≈2.4 %, worst case of
the 32 / 16 pool (16 WT + 16 CHIP, all fx) ≈64 %, guard at 80 %.

| Part | Estimate (cycles / sample) | % per voice (added) | Ship target | Cut-off |
|---|---|---|---|---|
| Fold (ADAA, on) | ~30 | +0.2 | <= +0.3 | > +0.5: naive fold (no ADAA), pitch limit kept |
| Ring (on) | ~4 (+ osc 2 if it was skipped: up to +0.6) | +0.05 | <= +0.1 | — |
| LADDER over LP | ~25 | +0.2 | <= +0.3 | > +0.5: linear ladder + one output `fastTanh` |
| MS20 over LP | ~15 | +0.12 | <= +0.3 | > +0.5: saturator per control segment peak, not per sample |
| COMB over LP | ~10 | +0.1 | <= +0.3 | — |
| FORMANT over LP | ~25 + control | +0.2 | <= +0.4 | > +0.6: two formants (F1, F2) |

Whole-feature gate: the bench's worst case, **16 WT SYNTH (fold 100, ring 64, LADDER) + 16 CHIP (LADDER),
all fx**, must stay **<= 75 %** average (5 % below the guard). Over it: apply the cut-offs above in the
order fold, ladder, formant, then re-run.

**Result (bench 2026-10-15, see future-teensy.md):** first run fold + ring +1.26, LADDER +0.70, COMB +0.55.
Shipped: the fold cut-off (naive fold, no ADAA; pitch limit and fade-in kept), branch-free LADDER / MS20 /
COMB (same sound). After: fold +0.39, ring +0.09, LADDER +0.35, COMB +0.34, MS20 +0.23, FORMANT +0.40.
The linear-ladder cut-off was not taken (diverges at k = 4.2 without the in-loop saturator). The gate is
missed: the full worst case is ≈84 % (28 voices clean at 78 %); the guard sheds 2–3 voices there.

RAM: `VoiceFilter` union grows `Voice` by ~100 B (3 `Svf` for FORMANT) -> ~3.2 KB DTCM; SynVoice fold
state 8 B / voice; line pool 32 KB RAM2. Code: ~4 KB ITCM (hot loops). Both RAM1 costs are why plan task 1
moves cold code to flash first.

## Groundwork for B and C (built here)

1. **RAM1 headroom** (plan task 1): add the cold `lib/core` synth files to the flash list of
   `imxrt1062_t41_mt.ld` (`synth.cpp`, `synth_*.cpp`, `model.cpp`, `fx_info.cpp`, `scale.cpp`,
   `render*.cpp`, …: their `MT_HOT` functions stay in ITCM via `.fastrun`), after checking that every
   function on the per-block path is `MT_HOT`. Gate for every later task: `pio run -e teensy41` reports
   ITCM in at most 8 banks and "free for local variables" >= 24 KB.
2. **`VoiceLines`** pool (COMB, then STRING's delay line).
3. **bench_max refactor**: a ladder names its instrument slot and its play mode (held chord / hits); a
   build flag `BENCH_MAX_GROUP` (0 all, 1 today's, 2 filters + fold, 3 MACRO, 4 STRING) runs one group.
4. `synth_shape.h` (B's FOLD model reuses the fold curve).

## Bench plan (bench_max.cpp, group 2)

New ladders, each +4 voices per step to the 80 % guard (top 64 for light, 48 for heavy in this env):
`CHIP LADDER`, `CHIP MS20`, `CHIP COMB`, `CHIP FORMANT` (CHIP saw, reso 100, fenv 40, held chords), `WT
FOLD+RING` (the WT SYNTH of today's ladder with fold 100, ring 64), and the gate case `16 WT FX + CHIP LADDER`.
Per-voice price = (avg at the clean step − fx-only base) / voices, compared with today's `CHIP` and `WT
SYNTH` ladders run in the same build (group 1). The profile's `ctlFlt` and `chip` / `synth` stages show
where it goes. Results into future-teensy.md.

## Tests (native, Unity)

- test_synth_drive: `ModelDrive` FOLD bit-exact after the extraction; `synth_shape.h` F vs numeric
  integral of f.
- test_synth_syn: fold 0 / ring 0 bit-exact; fold raises the 3rd / 5th harmonic of a 220 Hz tri; output
  bounded, no NaN over fold 0..127 and 20 Hz .. 10 kHz; ring 127 with osc 2 = osc 1 (both TRI) ≈ product
  (2nd harmonic dominant); ring forces osc 2 on with MIX 0.
- test_synth_filter (or new test_synth_vfilter): LADDER −24 dB/oct (power at 4 x fc >= 20 dB under
  passband at r 0), self-oscillation at r 1 sustains after an impulse, stable over a 20 Hz..20 kHz sweep
  at r 0..1, no NaN; MS20 −12 dB/oct, bounded at r 1 with full-scale input; COMB peaks at f and 2f (one
  Goertzel bin each over a valley at 1.5 f), D clamp at 86 Hz, bounded at r 1; FORMANT: at p 0 power
  near 730 / 1090 Hz above 1600 Hz, at p 4 near 300 / 870 Hz; `ringing()` false after silence settles;
  LP / BP / HP through `VoiceFilter` bit-exact to `Svf`.
- test_model: `LfoDest::Fold / Ring` valid for SYNTH only, names, order, instrSetType moves them to PITCH
  for other types.
- test_project_io / test_preset_io: SYNI bytes 41 / 42 round trip; a file with them 0 loads off; FLTR
  new modes round trip; mode 200 loads Off.
- test_synth: a project's LP / BP / HP render bit-exact to a checksum captured before the change; each
  new mode renders sound and frees its voice after the tail; LFO FOLD moves the output; COMB on two
  voices uses separate lines.
- test_link_msg: `kProtocol == 5`. test_synth_model: tiling still exact.

## Open questions (defaults taken by the plan; change before starting if needed)

1. Branch: Teensy only, `main`'s ESP synth does not get A / B / C **[default]**; or port to `main` with a
   compile-time switch.
2. Filters on every type **[default]**, or SYNTH only.
3. FORMANT and key track: ignored / dimmed **[default]**, or key track shifts the formants ("size").
4. COMB lowest pitch 86 Hz with 1 KB / voice **[default]**; 43 Hz costs 64 KB of RAM2 (2 KB / voice).
5. No per-step lock for fold / ring (lock mask full) **[default]**; widening `lockMask` to 32 bits is a
   separate change (Voice, TrackRt, fx, files).
