# Track drive models — design

Feature 2 of the "sound next" list: the track insert's drive (feature 1, `TrackStrip::drive`) gets a
choice of curves: TANH (today's), CLIP, FOLD, RECT, ASYM. Per track, lockable per step.

## Why

One saturation curve covers "warm"; hard clip (digital edge), wavefolding (West Coast timbre), full-wave
rectify (octave-up fuzz) and asymmetric (even harmonics, tube-ish) are the usual groovebox palette. The
insert runs per track, so a new curve costs per track, not per voice (see the track insert design).

## Existing

- `Drive` (synth_drive.h): `y = tanh(g x) / tanh(g)`, `g = 1 + drive x 7/127` (1..8), drive 0 = bypass,
  bit-exact. 257-point tanh table over [-8, 8], linear interpolation. Stateless.
- `TrackStrip` runs `drive -> crush -> sends -> fader x pan` per control segment (32 samples) on hot
  tracks only (`trkHot_`). The strip takes drive / bits / srr from the newest voice of the track
  (`Voice::drive / bits / srr`, set in `control()` from lock or `TrackCfg`, plus LFO).
- `TrackCfg` ends with `drive, crushBits, crushRate` (sizeof 25, static_assert in storage.cpp).
- TINS chunk: `count, recSize = 3, records`; readers take the fields they know.
- Fx: no free code. Values are stored in files: a new command goes before `Count` (after `PAN`).
  Values are shown 3 characters wide (LFW: SIN, TRI, ...).
- Lock bits: `kLockPan = 12`, `kLocks = 13` of 16 (one free slot is enough).
- Sample rate 32 kHz: Nyquist 16 kHz, harmonics of hard curves alias early.
- No `SynthModel` struct in this repo; the Teensy port mirrors `model.h` types, so the only mirrored
  change is one byte at the end of `TrackCfg`.

## Approaches

**A. Naive memoryless curves.** `f(g x)` per sample, no anti-aliasing. Cheapest (~table lookup each).
CLIP / FOLD / RECT alias loudly at 32 kHz on bright input (saw, hats): inharmonic grit that tracks
nothing. TANH is smooth enough that it already works this way.

**B. First-order ADAA (antiderivative anti-aliasing) on the hard curves, naive on the smooth ones.**
`y[n] = (F(u[n]) - F(u[n-1])) / (u[n] - u[n-1])`, `F` = closed-form antiderivative of `f`; when
`|u[n] - u[n-1]| < 1e-4`, `y = f((u[n] + u[n-1]) / 2)`. Kills most of the aliasing of kinks (~-20..30 dB
on high partials) for one divide and a few multiplies; per-track state 2 floats. Adds a half-sample
delay and a gentle top-octave roll-off (accepted: insert, mono, before the pan). TANH and ASYM stay
naive (smooth, aliasing low), so TANH remains bit-exact to feature 1.

**C. 2x oversampling** (halfband polyphase up / down around the curve). ~4-5x the curve cost per hot
track (two curve evals + ~12 MACs per output sample), still aliases folds above 2x. Too expensive for
16 tracks on the ESP32-S3 for what it buys over B.

**Recommendation: B.** Closed-form `F` exists for CLIP, FOLD, RECT; cost stays per track and bounded.

## Signal / math per model

Common: `u = g x`, `g = 1 + drive x 7/127` (as today), drive 0 = bypass for every model (bit-exact).
Output target: a full-scale input stays about full scale; per-model make-up gain `k` (constant, tuned
by test so a 220 Hz sine at drive 64 lands within +-3 dB RMS of TANH).

| Model | f(u) | F(u) (ADAA) | Notes |
|---|---|---|---|
| TANH (0) | `tanh(u) / tanh(g)` (table) | — naive | today's code path, unchanged |
| CLIP (1) | `clamp(u, -1, 1)` | `|u| <= 1: u²/2`, else `|u| - 1/2` | odd harmonics, hard edge |
| FOLD (2) | triangle fold, period 4: `w = (u - 1) mod 4`, `f = |w - 2| - 1` | `w <= 2: w - w²/2`, else `w²/2 - 3w + 4` (period 4, zero mean) | `mod` via int cast, no floorf; g 1..8 = up to ~4 folds |
| RECT (3) | `min(|u|, 1)` (full wave of the clipped signal) | `|u| <= 1: u|u|/2`, else `sign(u)(|u| - 1/2)` | octave up; drive adds the clip; needs DC block |
| ASYM (4) | `(tanh(u + b) - tanh(b)) / (tanh(g + b) - tanh(b))`, `b = 0.5` | — naive | even harmonics; same tanh table, `tanh(b)` and the norm per control update; needs DC block |

- DC blocker (RECT, ASYM only): `y = x - x1 + R y1`, `R = 1 - 2π 10 / 32000 ~ 0.998`; per track,
  reset with the crush on a strip snap. Off for the other models (no state, no cost).
- Parameter change (control update, model or g changed): ADAA keeps the previous *input* `x1`;
  `F1 = F(g x1)` is recomputed once, so a drive move does not produce a step in the difference.
  Model change mid-note: a click like any drive lock, accepted.
- Chain order unchanged: drive (model) -> crush -> fader -> sends -> pan.

## Model and file

- `TrackCfg` gains, at the end: `uint8_t driveModel = 0` (`DriveModel { Tanh, Clip, Fold, Rect, Asym,
  Count }` in model.h). sizeof 25 -> 26: storage.cpp static_asserts (TrackCfg, Project) and
  `snapshot()` updated. Teensy mirror: same byte at the end.
- `Fx::DTY` ("DRIVE TYPE", 0..Count-1, default 0) appended before `Count`; kOrder after DRV;
  `fxSynthOnly`; value names TNH, CLP, FLD, RCT, ASY.
- `kLockDty` after `kLockPan` (kLocks 13 -> 14); `Voice::dmodel` byte (`control()`: lock or
  `TrackCfg::driveModel`); `refreshStrip`: `s.drive.set(v.drive, v.dmodel)`. Per voice: one byte and one
  select per control update, no audio-rate work.
- LFO: no new target (discrete value; see open questions).
- TINS: `kTinsRec` 3 -> 4, byte 3 = driveModel. Records of 3 bytes load model 0 (TANH: old files sound
  the same). Values >= Count clamp to 0 (unknown future model -> TANH, not "the last one").
- Instruments, presets, inst_codec: untouched (the drive is not an instrument field any more).
- Demos: optionally one track switched to FOLD / CLIP to show it off.

## UI

- TRACK, INS page: new row `Drive type` right after `Drive` (rows: Drive, Drive type, Bit crush,
  Downsample). Values TANH / CLIP / FOLD / RECT / ASYM, encoder steps, clamps at the ends. Shown
  regardless of drive (drive 0 still bypasses: the row value shows dimmed or as is — see questions).
- Step fx editor: DTY after DRV in the command list.
- manual_ru.md: the models, DTY, "drive 0 = off for every type".

## CPU

Estimates on the S3 (240 MHz), per hot track with drive on, per sample:
TANH ~15 cycles (today), ASYM ~18 (+DC block 3), CLIP / RECT ~35-40 (ADAA, the single-precision
divide is the bulk), FOLD ~45 (+mod). Worst case 16 tracks on FOLD: 16 x 32000 x 45 ~ 23 Mcycles/s,
~10 % of a core; typical 2-4 driven tracks: 1-3 %. Models other than TANH cost nothing when unused.
Verify with the pool bench (24 voices, 8 heavy) + all 16 tracks FOLD at drive 100, before / after.
If the divide turns out costly: `1/d` via the existing reciprocal trick or skip ADAA when drive < ~16.

## Tests (native, Unity)

test_synth_drive:
- drive 0 bypass bit-exact for every model.
- TANH output bit-exact to the feature-1 `Drive` on a fixed buffer.
- Each model: output bounded (|y| <= ~1.05 after make-up), no NaN / Inf for x in [-4, 4], silence in ->
  silence out (RECT / ASYM after the DC blocker settle).
- ADAA: `F` matches the numeric integral of `f` (sampled check); constant input -> `f(u)` (fallback path);
  aliasing: 5 kHz sine at drive 100, CLIP with ADAA has less energy at a known alias bin (DFT of one bin)
  than naive by >= 10 dB.
- FOLD: f(0)=0, f(1)=1, f(2)=0, f(3)=-1; odd symmetry.
- RECT: 220 Hz sine -> dominant 440 Hz (one-bin DFT), DC < 1e-3 after settle.
- ASYM: second harmonic present (> -40 dB), DC < 1e-3 after settle.
- Loudness: each model within +-3 dB RMS of TANH at drive 64 (220 Hz sine).
- Parameter jump (drive 20 -> 100 at a control boundary): no sample jump > threshold.

test_synth: TrackCfg::driveModel reaches the strip; DTY locks a step, then reverts; preview track uses
the named track's model; strip snap resets the DC blocker / ADAA state.

test_project_io: TINS round trip with the model; old 3-byte records -> model 0; unknown model value ->
0; longer records (5 bytes) skip the tail.

test_fx_info: DTY range, names, order, synth-only.

## Open questions for the user

1. Anti-aliasing: (a) ADAA on CLIP / FOLD / RECT, TANH / ASYM naive **[recommended]**; (b) none
   anywhere (cheapest, harsh aliasing at 32 kHz); (c) 2x oversampling (cleanest, ~4x cost).
2. Set of models: (a) TANH, CLIP, FOLD, RECT, ASYM **[recommended]**; (b) drop RECT; (c) add more
   later (e.g. sine fold, bitwise "XOR") — reserved by the Count clamp.
3. FOLD shape: (a) triangle fold (exact ADAA, brighter) **[recommended]**; (b) sine fold `sin(π/2 u)`
   (smoother, softer, needs a sine table and a cos antiderivative table).
4. RECT: (a) full-wave of the clipped signal (octave up) **[recommended]**; (b) half-wave (keeps the
   fundamental, milder).
5. ASYM bias: (a) fixed `b = 0.5` **[recommended]**; (b) a second "Bias" knob on the INS page (new TINS
   byte, more control, one more row).
6. Per-step lock: (a) new fx DTY + lock bit **[recommended]**; (b) no fx, the model is a track setting
   only.
7. LFO target for the model: (a) none (discrete, gimmicky) **[recommended]**; (b) new LfoDest `Dtype`
   stepping through models.
8. Row name / place: (a) "Drive type" right after Drive **[recommended]**; (b) "Model"; (c) before
   Drive.
9. Fx code name: (a) DTY **[recommended]**; (b) DMO; (c) DST.
10. Demos: (a) switch one demo track to FOLD or CLIP to showcase **[recommended]**; (b) leave demos
    untouched (all TANH, bit-exact to now).

## Decisions (user, 2026-10-14)

All recommended options. TINS record order: drive, bits, rate, driveModel. readTins assigns only the
fields present in the record.

## Bench (ESP32-S3, wt32-pool24, drive 100 on all 16 tracks, 2026-10-14)

Avg us per 4000 us block (feature 1 / TANH / CLIP / FOLD):
0 heavy + 16 CHIP 1747 / 1730 / 1888 / 2030; 8 heavy 2349 / 2333 / 2457 / 2499;
10 heavy 2231 / 2290 / 2413 / 2531; 12 heavy 2262 / 2323 / 2365 / 2598.
Strip stage, heavy steps: TANH ~1280, CLIP ~1505, FOLD ~1660-1716 (feature 1: 1322);
16 CHIP step: TANH 185, CLIP 343, FOLD 474 (feature 1: 211). TANH no regression; FOLD on all 16
tracks is the worst case, ~+400 us.
