# Master / send effects — design

Feature 5 of the "sound next" list: cheap stereo colour on the master side. Chorus / ensemble,
flanger, phaser, tape delay, ping-pong, shimmer, master EQ / tilt. Out of scope: granular, vocoder,
unison.

## Why

The per-track insert (feature 1) adds grit, but the mix has no width or movement: the delay
returns the same signal to both sides, the reverb is a plain Freeverb-lite, and the master has no
EQ. Effects that run once per block on a bus cost about the same as one voice, and they make the
biggest difference to how a pattern sounds (Digitone / Model:Cycles / OP-1 territory). Same rule as
feature 1: nothing per voice, nothing per track. Effects run once on a bus or on the master.

## Existing (lib/core/src, src/audio, src/ui)

- `Synth::renderCore` (synth.cpp ~1472): voices -> `trk_` -> `runStrip` (insert, fader, peak,
  mono sends into `send_` / `rsend_`, pan into `mixL_/mixR_` or `scL_/scR_`) -> PERF throws add to
  the sends -> `Delay` (mono line, the same return on L and R) -> freeze duck -> `Reverb` (4 combs +
  2 allpasses, stereo spread on the last allpass) -> `Compressor` (stereo-linked, sidechain) -> key
  track added back -> `PerfBus` -> master gain + softClip.
- Delay (`synth_delay.h`): int16 line in PSRAM, 4 s (`4 * kRate`, 256 KB), `kScale` 8192, one-pole
  LP TONE in the feedback, fb max 0.95, time = `dlyTime` sixteenths. A time change jumps the read
  pointer (click / no glide).
- Reverb (`synth_reverb.h`): 6398 floats (25 KB), internal RAM if 24 KB stay free, else PSRAM;
  moved to PSRAM for Wi-Fi (`moveReverb`). Goes idle when its tails die out. FREEZE flag.
- Compressor (`synth_comp.h`): < 1 % CPU.
- PERF (`synth_perf.h`): stereo int16 ring in PSRAM (4 s on the device, ~670 KB PSRAM free before
  it), tone effects incl. an `Svf` filter and `Drive`.
- Sends are per instrument (`Instrument::send`, `rsend`), the strip takes the track instrument's
  (KIT: the KIT's). Locks `DLY`, `RVB` (`kLockDly`, `kLockRvb`), LFO targets `Dly`, `Rvb`.
  `kLocks` = 13 of 16 bits used.
- Project fields: `dlyTime, dlyFb, dlyTone, dlyLevel, rvbSize, rvbDamp, rvbLevel, compAmt, compRel,
  scTrack, scDepth` in chunk `AUDI` (14 bytes, byte 13 = the old DJ filter, written 64). `readAudi`
  reads what it knows and skips the rest, so older firmware loads a longer AUDI.
- UI: PROJ screen (`proj_screen.cpp/.h`), pages SONG / FX / COMP / PERF, 9 visible rows,
  `Param{name, fmt, edit, disabled}`; FX = 7 rows (Delay, Feedback, Tone, Dly level, Reverb,
  Rvb damp, Rvb level), COMP = 4 rows.
- Rate 32 kHz, block 128 = 4 ms = 960 k cycles at 240 MHz. 1 % CPU = 9.6 k cycles per block =
  75 cycles per sample. Hot code `MT_HOT` (IRAM), tables `MT_DATA`. `gSine` (1024 + 1 floats) and
  `fastTan` (synth_filter) exist.
- Bench: `env:wt32-fxbench` (`-DAUDIO_BENCH_FX`, audio.cpp): DRUM bench with drive + reverb + comp.
  Profiler stages `kProfDelay`, `kProfReverb`, `kProfComp`, ... (PROJ -> SYS -> CPU profile).
- Teensy port mirrors the model: new fields go at the END of `Project` / `TrackCfg`. The DSP
  headers in lib/core are hardware-free, so the Teensy reuses them as they are.

## Approaches

A. **One master insert slot** (stereo, on the whole mix after the returns, before the comp), one
   effect at a time picked from a list. Smallest change, no per-track params. But chorus / flanger
   / phaser on the whole mix smear the kick and bass, and there is no way to keep a track dry.

B. **Modes of what exists, plus one MOD send bus, plus a master EQ** (recommended).
   - Delay gets a mode: DIGITAL (today) / TAPE / PING-PONG.
   - Reverb gets a mode: PLAIN (today) / SHIMMER.
   - A third send bus, MOD, with one selectable effect: CHORUS / ENSEMBLE / FLANGER / PHASER.
     Each track sends to it (pads and leads in, kick and bass dry).
   - Master EQ (low / high shelf; together they make a tilt) after the compressor.
   Every effect runs once per block. Mode 0 / OFF is bit-exact to today.

C. **Mod FX in the per-track insert** (feature 1 chain). Best control, but 16 x the CPU and
   16 x the delay lines in internal RAM (16 x 2 KB). No.

Recommendation: B. It follows the Digitone layout (chorus send + delay + reverb) and the cost does
not grow with tracks or voices.

## Signal flow (B)

```
runStrip:  ... fader -> sends: send_ (DLY), rsend_ (RVB), msend_ (MOD, new) -> pan -> mix
master:    PERF throws -> MOD (msend_ -> stereo return into mix)
                       -> DELAY (mode) -> freeze duck -> REVERB (mode) -> COMP -> + key track
                       -> EQ (new) -> PERF -> master gain, softClip
```

- MOD comes back before the delay and reverb processing, as a return into the mix only. It does
  not feed the delay / reverb sends (that would be a "MOD -> DLY/RVB" option later; open question 6).
- EQ sits after the compressor and the key track (the comp does not react to the EQ boost; the key
  track gets EQ'd too), before PERF (PERF FILTER stays the last colour).

## Per effect

### 1. Master EQ (master insert)

- Two one-pole shelves per side: Low shelf ~120 Hz, High shelf ~5 kHz, each -12..+12 dB.
  `y = x + gL * lp(x) + gH * hp(x)`; one LP state per side, hp = x - lp2 (a second LP at the high
  corner). Low cut / high boost = tilt. Coefficients only on a change (`expf` like the delay TONE).
- Params: `eqLow`, `eqHigh` (0..127, 64 = flat, shown -12.0 .. +12.0 dB).
- RAM: 4 floats of state. CPU: ~12 cycles per sample -> ~0.15 %. Both 64: skipped (bit-exact).
- Header `synth_eq.h` (`class MasterEq { process(l, r, n, low, high); }`).

### 2. Tape delay (delay mode TAPE)

- Same mono line. The read pointer is fractional (linear interpolation of two adjacent int16s),
  delayed by `wow` (sine ~0.6 Hz, up to +-3 ms) + `flutter` (sine ~7 Hz, up to +-0.3 ms), LFO
  phases from `gSine` once per 32 samples, the offset ramped linearly in between.
- Feedback path: TONE LP (existing) + one-pole HP (60 -> 250 Hz with Wear) + soft saturation
  (`x / (1 + |x|)` style, drive 1..3 with Wear) before the line write. Repeats darken, thin and
  compress instead of building up.
- Time changes glide in TAPE (the read offset slews ~200 ms): pitch bend instead of a jump.
- Param: `dlyChar` ("Wear", 0..127): wow / flutter depth, saturation drive, HP corner, all at once.
- RAM: none new (a few floats of state). CPU: +~30 cycles per sample over DIGITAL -> +0.4 %.

### 3. Ping-pong (delay mode PING-PONG)

- The 4 s line split into two 2 s halves, L line and R line. In: `send` -> L line; L output ->
  R line; R output x fb -> L line. First echo left, second right, ... TONE LP on both outputs.
- Width (`dlyChar` in this mode): 0 = both returns centred (a plain delay at 2x density), 127 =
  hard L / R.
- Limit: max time 2 s per side (16/16 at 120 BPM; slower tempos clamp the longest times).
  Alternative: the R tap at 2 x d on one 4 s line, same limit.
- RAM: none new. CPU: +~15 cycles per sample -> +0.2 %.

### 4. MOD bus: chorus / ensemble / flanger / phaser (send bus, one selectable slot)

- `msend_[kBlock]` filled in `runStrip` like `send_` (track's mod send x fader). Stereo return x
  `modLevel`. Idle like the reverb: no input and the line / allpass tails below -100 dB -> skipped.
- Shared line: mono int16, 1024 samples (32 ms), inside `Synth` (internal RAM, 2 KB). Reads are
  fractional (linear interpolation); LFOs per 32 samples, delay offsets ramped inside a segment.
- CHORUS: 2 taps (L, R), base 12 ms, +- depth x 6 ms, LFOs 90 deg apart. ~30 cycles / sample, 0.4 %.
- ENSEMBLE: 3 taps at 120 deg, slow (~0.6 Hz) + fast (~6 Hz) LFO mix (string-machine style), the
  taps panned L / C / R. ~60 cycles / sample, 0.8 %.
- FLANGER: 1 tap per side, 0.3..8 ms, feedback 0..0.9 (Mod fb), R LFO 90 deg off. 0.4 %.
- PHASER: no line; 6 first-order allpasses per side, notch sweep 200 Hz..4 kHz (exp), coefficient
  per segment (`fastTan`), feedback 0..0.85, R LFO 90 deg off. The dry track stays in the mix, so
  the return makes the notches; send 127 = deepest. ~70 cycles / sample, 0.9 %.
- Params (Project): `modType` (OFF, CHORUS, ENSEMBLE, FLANGER, PHASER), `modRate` (0..127 ->
  0.05..8 Hz exp), `modDepth`, `modFb` (FLANGER / PHASER only), `modLevel` (return).
- Per track: `modSend` (0..127). Where it lives: open question 2 (recommended: `TrackCfg`, TINS
  record +1 byte; the KIT shares one send as for the other sends).
- Header `synth_modfx.h` (`class ModFx { setType; process(in, mixL, mixR, n, rate, depth, fb, level); }`).
  Type change: clear the line and the allpass states (no clicks from stale content; a short fade
  is cheaper than it looks: 64 samples).

### 5. Shimmer (reverb mode SHIMMER)

- A +12 pitch shifter in the reverb's feedback: the last block's reverb output (mono, L) goes
  through the shifter, x `rvbShim`, into the reverb input of this block (one block of latency,
  inaudible in a tail). Each pass climbs an octave: the classic shimmer.
- Shifter: int16 line 2048 samples (64 ms), two read taps at speed 2 with a triangular crossfade
  (window ~40 ms), then a one-pole LP (~6 kHz) so the octaves do not turn to hiss. Gain clamped
  (max 0.5 x fb) and soft-clipped: no runaway at size 127.
- Params: `rvbMode` (PLAIN, SHIMMER), `rvbShim` (amount). Interval fixed +12 (open question 4).
- RAM: 4 KB, caller-owned like the reverb buffer: internal if 24 KB stay free, else PSRAM; moved
  with the reverb for Wi-Fi. Or one block: reverb buffer + 4 KB (simpler, one move).
- CPU: ~40 cycles / sample -> 0.5 % on top of the reverb. FREEZE: the shimmer loop stops (input
  muted), the frozen tail holds.
- Idle: the reverb's idle test sees the shimmer input too (stays awake while it climbs).
- Header `synth_shimmer.h` (`class PitchShift { process(in, out, n); }`), hooked in `Synth`.

## p-locks, LFO, PERF

- New lock `MOD` (Fx at the end before `Count`, lock bit `kLockMod` = 13, still <= 16): the mod
  send per step, like DLY / RVB. LFO target `ModSend` (LfoDest at the end before `Count`).
- Master params (modes, rate, depth, Wear, EQ) are project-wide like delay / reverb today: no
  per-step locks. PERF could add `MOD THROW` (all to the MOD bus) and `TAPE WOBBLE` (Wear to max
  while held) later; not in this feature (open question 5).

## Model and file

`Project`, at the end (Teensy mirror):

```
uint8_t dlyMode = 0;    // DlyMode: Digital, Tape, PingPong
uint8_t dlyChar = 40;   // TAPE: wear; PING-PONG: width
uint8_t rvbMode = 0;    // RvbMode: Plain, Shimmer
uint8_t rvbShim = 60;
uint8_t modType = 0;    // ModType: Off, Chorus, Ensemble, Flanger, Phaser
uint8_t modRate = 30, modDepth = 64, modFb = 40, modLevel = 100;
uint8_t eqLow = 64, eqHigh = 64;  // 64 = flat
```

`TrackCfg`, at the end: `uint8_t modSend = 0;`.

- `AUDI` grows from 14 to 25 bytes: bytes 14..24 = the fields above in that order. `readAudi`:
  new `kAudiMfxSize = 25`; shorter files keep the defaults (OFF / flat = today's sound). Older
  firmware reads 14 and skips the rest (already the case).
- `TINS` record grows by one byte (`modSend`) after feature 1's fields (and after a trance gate
  if that lands first: fields append in landing order). Readers take what they know.
- No version bump: both chunks are size-tagged.
- Demos: one demo shows them (e.g. DEMO-HOUSE: chorus on pads, tape delay); factory presets
  unchanged.

## UI

PROJ pages today: SONG, FX, COMP, PERF (9 rows visible). FX would grow to 11 rows. Recommended:
split into **SONG / DLY / RVB / MOD / MSTR / PERF**:

- DLY: Delay (time), Dly mode, Feedback, Tone, Wear / Width (name follows the mode, grey in
  DIGITAL), Dly level.
- RVB: Reverb (size), Rvb mode, Rvb damp, Shimmer (grey in PLAIN), Rvb level.
- MOD: Mod type, Mod rate, Mod depth, Mod fb (grey unless FLANGER / PHASER), Mod level.
- MSTR: Comp, Comp rel, SC track, SC depth, EQ low, EQ high.
- TRACK screen: `Mod send` after the insert rows (INT only). fx_info: `MOD "MOD SEND"`.
- Check that `PageBar` fits 6 names on the 480 px screen (short names: 4 chars).
- Manual (ru / en): new rows, signal-flow figure.

## CPU / RAM budget (ESP32-S3, 240 MHz, float; estimates, to be measured)

| Effect | Where | RAM (new) | CPU / block | Bypass |
|---|---|---|---|---|
| EQ low / high | master insert | 16 B | ~0.15 % | both 64: skipped |
| Tape delay | delay mode | ~32 B state | +0.4 % | DIGITAL: today's loop |
| Ping-pong | delay mode | ~16 B (line split) | +0.2 % | DIGITAL |
| Chorus | MOD bus | 2 KB internal (line) + 512 B bus | 0.4 % | OFF / idle |
| Ensemble | MOD bus | same line | 0.8 % | OFF / idle |
| Flanger | MOD bus | same line | 0.4 % | OFF / idle |
| Phaser | MOD bus | 48 B | 0.9 % | OFF / idle |
| Shimmer | reverb mode | 4 KB (internal or PSRAM) | +0.5 % | PLAIN |

Worst case all on: ~+2.5 % (one MOD type at a time). Internal RAM: +2.6 KB inside `Synth` (+4 KB
shimmer when internal). PSRAM: nothing new (ping-pong reuses the 256 KB line). Measure with
`wt32-fxbench` extended (bench turns on TAPE, SHIMMER, ENSEMBLE / PHASER, EQ) and new profiler
stages `kProfMod`, `kProfEq` (appended before `kProfStages`, names in `profName`).

## Tests (native)

Per DSP header, standalone (no Synth):
- EQ: 64 / 64 bit-exact; +12 dB low shelf: 50 Hz sine +12 +- 1 dB, 8 kHz sine +- 0.5 dB; high
  shelf mirror.
- Tape: Wear 0 output = DIGITAL bit-exact; Wear 127 impulse echo time jitters (spread of the echo
  peaks > 0), echoes stay bounded at fb 127 (saturation), time change glides (no sample jump > x).
- Ping-pong: impulse -> first echo only on L at d, second only on R at 2d (width 127); width 0 ->
  L == R; no line -> silent.
- Mod FX: each type: OFF and send 0 leave the mix bit-exact; impulse produces a stereo return
  (L != R); flanger fb 0.9 bounded; phaser notch moves with the LFO (spectrum check at two phases);
  idle after silence; type change clears.
- Shimmer: impulse spectrum after 1 s has energy at 2x the input sine frequency; bounded at size 127
  / shim 127 for 10 s; PLAIN bit-exact to today; freeze holds.
Integration:
- Synth: mod send via TrackCfg / MOD lock / LFO target reaches `msend_`; KIT one send; defaults
  render bit-exact to a pre-feature reference block.
- project_io: AUDI 25 round trip; 14-byte AUDI -> defaults; longer AUDI skipped; TINS with modSend
  round trip, older record -> 0.

## Order (independent sub-features)

Each step: DSP header + native tests first (no Synth change), then integration + file + UI, then a
device check on `wt32-fxbench`. Commit after the device test (project rule).

1. **Master EQ** — `synth_eq.h` + tests; then Synth hook, AUDI bytes 23..24, MSTR page. Smallest,
   proves the AUDI growth path and the PROJ page split.
2. **Delay modes** — 2a tape (`Delay` gains mode / char, fractional read, glide), 2b ping-pong;
   tests on `Delay` alone; then AUDI bytes, DLY page.
3. **Shimmer** — `synth_shimmer.h` + tests; then the reverb hook, the buffer placement (audio.cpp,
   Wi-Fi move), RVB page.
4. **MOD bus** — 4a `synth_modfx.h` chorus + flanger (shared line), 4b ensemble, 4c phaser, each
   with tests; 4d integration: `msend_`, `TrackCfg::modSend`, TINS byte, Fx MOD, `kLockMod`, LfoDest,
   MOD page, TRACK row, fx_info, manual. Largest: touches the model, the sequencer fx list and the UI.
5. Bench + profiler stages + manual + one demo.

AUDI byte order is fixed up front (all 11 bytes in the order above), so the steps can land in any
order: each one reads / writes its bytes; unwritten ones default.

## Open questions for the user

1. Overall layout:
   a) **Delay / reverb modes + one MOD send bus + master EQ (recommended)**
   b) One master insert slot for everything (no per-track control)
   c) MOD in the per-track insert (16 x CPU / RAM)
2. Where the MOD send lives:
   a) **TrackCfg (TRACK screen, TINS byte), like the insert (recommended)**
   b) Instrument (INST screen, next to Dly / Rvb send; preset carries it; inst_codec change)
3. Ping-pong time limit (2 s per side, clamps 16/16 below 120 BPM):
   a) **Accept the clamp (recommended)**
   b) Allocate a second 256 KB PSRAM line (PSRAM is tight: ~670 KB free before PERF)
4. Shimmer interval:
   a) **Fixed +12 (recommended)**
   b) Selectable +12 / +7 / +24 (one more row)
5. PERF additions (MOD THROW, TAPE WOBBLE):
   a) **Later, separate task (recommended)**
   b) In this feature
6. MOD return feeding delay / reverb:
   a) **No, return into the mix only (recommended)**
   b) Yes, the MOD return also goes to the delay and reverb sends at the track's send levels
7. Master EQ shape:
   a) **Two shelves, EQ low + EQ high (recommended; tilt = opposite settings)**
   b) One Tilt knob
   c) Three bands (low shelf, mid peak, high shelf; ~+0.4 % CPU)
8. PROJ pages:
   a) **SONG / DLY / RVB / MOD / MSTR / PERF (recommended)**
   b) Keep SONG / FX / COMP / PERF, FX scrolls 11 rows, MOD rows on COMP
9. Implementation start:
   a) **EQ first, then delay modes, shimmer, MOD bus (recommended)**
   b) MOD bus first (biggest audible gain, biggest change)

## Decisions (user, 2026-10-14)

All recommended options, except: Q4 b) shimmer interval selectable +12 / +7 / +24; Q5 b) PERF additions
(MOD THROW, TAPE WOBBLE) in this feature; Q9 b) MOD bus first.
