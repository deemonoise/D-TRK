# Sound fx: drive, reverb, master compressor, ARP modes, velocity — design

Block 4 of the roadmap agreed on 2026-10-06 (after 16 tracks, KIT drum track, song + live). Executed
**after** the SYNTH / wavetable work lands: it touches `synth.cpp`, `synth_voice.h`, `model.h`,
`inst_screen.cpp`, which that agent edits now.

## Goal

Make the INT mix sound finished without external gear: saturation per instrument, a reverb send next
to the delay, a master compressor that can duck to a drum track, a real arpeggiator and velocity that
shapes the sound, not only the level. Old projects and presets sound exactly as before.

## Model

- `LockBit`: `kLockFlt = 5, kLockRes, kLockDly, kLockDrv, kLockRvb, kLocks = 10`; `lockMask` in
  `Voice` and `Synth::TrackRt` becomes `uint16_t` (`static_assert(kLocks <= 16)`).
- `Fx`: `DRV` (drive lock 0..127), `RVB` (reverb send lock 0..127), `ARM` (arp mode, see below)
  appended before `Count`. All three synth-only (`fxSynthOnly`).
- `Instrument`: `drive` 0..127 (0 = bypass), `rsend` 0..127 (reverb send, 0 = none), `velCut`
  -64..63 (velocity → cutoff), `velMac` -64..63 (velocity → DECAY macro, FM / DRUM / SYNTH). All
  default 0.
- `LfoDest::Drive` appended before `Count`; UI name "DRIVE", available on every type.
- `Project`: `rvbSize = 60`, `rvbDamp = 70`, `rvbLevel = 80`; `compAmt = 0` (bypass), `compRel = 50`,
  `scTrack = 0` (off, 1..16 = track), `scDepth = 64`. All 0..127 except `scTrack` 0..kTracks.

## File format

- Instruments: the `FMIN` record (16 bytes, written for every instrument) has bytes 10..15 reserved
  (0): `b[10] = drive`, `b[11] = rsend`, `b[12] = velCut`, `b[13] = velMac` (signed as uint8). Old
  files read 0 = the new defaults; nothing else moves. Presets share the record (`inst_codec`):
  `kPresetVersion` 3 → 4 (newer readers accept older files, older firmware refuses the new ones).
- `AUDI`: 6 → 13 bytes, appended `rvbSize, rvbDamp, rvbLevel, compAmt, compRel, scTrack, scDepth`.
  `readAudi` reads what the chunk holds (6 = old file → defaults for the rest), clamps every value.
  `storage.cpp` snapshot copies the new fields.

## DSP

All at `kSynthRate` = 32 kHz, blocks of 128, control every 32 samples. Signal chain of a voice:
engine → **drive** → filter → (mix, delay send, reverb send, sidechain bus). Block end: delay return,
reverb return, **compressor**, master volume, soft clip.

### Drive (per voice, `synth_drive.h`)

`y = tanh(g x) / tanh(g)`, `g = 1 + drive / 127 * 7` (1..8). A 257-entry table of `tanh` over
[-8, 8] (`kDriveTab[i] = tanh(-8 + i / 16)`), linear interpolation, input clamped to ±8; `1 / tanh(g)`
is computed per control update, not per sample. Drive 0 (after lock and LFO) skips the stage: output
bit-exact to today. Cost ≈ 8 flops / sample / driven voice (~2 % with 16 voices all driven).
Applied in `renderVoice` to `dst` before the filter (the filtered path already renders into `flt[]`;
the unfiltered path renders into a scratch too when driven).

### Reverb (`synth_reverb.h/.cpp`, `mt::Reverb`)

Freeverb-lite, mono. 4 parallel feedback combs, base lengths 1116, 1188, 1277, 1356, actual length
`base * (0.5 + 0.5 * size / 127)` (clamped ≥ 1), feedback `0.70 + 0.28 * size / 127`, one-pole damping
in the loop `d = damp / 127 * 0.4` (`store = out * (1 - d) + store * d`); then 2 series allpasses 556,
441 with g = 0.5. Buffers: **float**, handed in by the caller (`setBuffer(float*, n)`), needed
`1116 + 1188 + 1277 + 1356 + 556 + 441 = 5934` floats = 23.2 KB (PSRAM on the device; no buffer =
silent). Float over int16: no scaling / conversion per tap, same memory traffic class, well under
64 KB. Rendered once per block from the `rsend_` bus: `mix += wet * (rvbLevel / 127 * 0.5)`. Cost per
sample ≈ 4 × 7 + 2 × 5 = 38 flops + 12 buffer accesses ≈ 3–5 % on one core (PSRAM latency dominates;
rings are read sequentially, the 32 KB cache holds them). Delay output does not feed the reverb.
`Synth::reset()` clears the buffer; a size change moves the write index (no glide).

### Compressor (`synth_comp.h`, `mt::Compressor`)

Peak detector on `|key|` with one-pole attack (1 ms) / release (`20 ms × 50^(compRel / 127)` =
20..1000 ms). Key = mix, plus `sc * scDepth / 127 * 4` when `scTrack` is set (`sc_` bus = the summed
voices of that track; the sidechain signal is weighted 4× so a drum track at normal level ducks hard
at depth 127). Threshold `T = -6 dB - 24 dB × compAmt / 127` (linear `exp2(dB / 6.02)`), ratio 4:1,
makeup `+(6 + 24 × compAmt / 127) / 2 / 2 dB` (half the threshold drop, then halved again for
headroom). Gain in dB `= -(envdB - TdB) × 0.75` above threshold, 0 below; computed **once per 8
samples** with `log2f` / `exp2f` and applied per sample (held, no ramp: 8 samples = 0.25 ms, inaudible
at these release times). `compAmt == 0` bypasses the stage bit-exactly. Cost < 1 %.

## Fx

- `DRV` 0..127, default 64: like FLT — on a step with a note the drive of its note-ons, else of the
  track's sounding voices. `RVB` 0..127, default 64: same for the reverb send.
- `ARM` "ARP MODE": `val = mode << 4 | rate`, mode 0 UP, 1 DOWN, 2 UPDOWN, 3 RANDOM, rate 1..8 notes
  per step; default `0x03` (UP, 3 = today's behaviour). Shown `U3`, `D4`, `B2`, `R8`. `fxStep` cycles
  the rate 1..8 then the mode. ARM alone does nothing; it shapes the next `ARP` of the step (same step
  or later steps while the ARP holds: ARM is stored in `TrackRt::arm` and reset with the ARP on a new
  step with a note).
- ARP today (`synth.cpp:516`): three pitches per step, offsets `0, x, y`, each a third of the step.
  With ARM: `n = rate` slots per step, offsets taken from the list `{0, x, y}` (or the chord, below):
  UP cycles forward, DOWN backward, UPDOWN forward then backward without repeating the ends, RANDOM
  picks per slot with the voice RNG. `ARP 00` keeps meaning off.
- **ARP + CHD** on the same step: the sequencer (`step_expand`) emits only the root note-on and a
  synth fx `kSynthArpChord = 0xF1 chord` before it (INT tracks only; MIDI tracks keep playing the full
  chord, they have no ARP). The synth stores the chord's intervals (`chordNotes` from `scale.h`, up to
  4 notes) in `TrackRt::arpNotes[4]` / `arpN` and the ARP cycles those instead of `{0, x, y}`; the
  ARP value is ignored except `00` = off (then the step is expanded as a normal chord).

## Velocity

- `velCut`: cutoff offset in octaves `= velCut / 64 × (vel - 64) / 64 × 6` (vel 64 = no change;
  +63 at vel 127 ≈ +6 octaves, like `fenv` at full depth). Added in `controlFilter` from
  `v.gain × 127` (the note's velocity, VSL excluded: use the velocity stored at note-on, `v.vel`).
- `velMac`: DECAY macro offset `= velMac × (vel - 64) / 64` in macro units (±63), clamped 0..127,
  applied where the macro value is read (`macros()` / the lock fallback) for FM, DRUM, SYNTH voices.
  Both 0 by default, so old projects are unchanged.

## UI

- INST MAIN (9 rows, fills `kListRows`): `… Glide, Send, Rvb send`. FILT page gets `Drive` as its
  first row (pre-filter in the chain; 8 rows). ENV page: `Attack, Decay, Sustain, Release, Vel>Cut,
  Vel>Dec` (`Vel>Dec` grey on CHIP / SAMPLE). LFO Dest gains `DRIVE` for every type.
- PROJ after `Dly level`: `Reverb` (size), `Rvb damp`, `Rvb level`, `Comp` (0 = OFF), `Comp rel`,
  `SC track` (OFF, T1..T16), `SC depth`.
- `fx_info`: `DRV "DRIVE"`, `RVB "REVERB SEND"`, `ARM "ARP MODE"`; `fxFormat(ARM)` = letter + rate.

## CPU

Estimates above: drive ≤ 2 % (all 16 voices driven), reverb 3–5 %, compressor < 1 %. The plan ends
with a bench (`AUDIO_BENCH_FX`) that reads the status-bar CPU figure with and without reverb /
compressor on the 16-voice DRUM bench; the executor writes the measured numbers here.

Measured: **not yet** (2026-10-06, no device in that session). Build `pio run -e wt32-fxbench -t upload`
(drive 100 + reverb send 100 on every bench instrument, reverb level 100, compressor 100 keyed by
track 1), read the status-bar CPU after 10 s, then set PROJ -> Rvb level 0 and Comp OFF and read it
again; fill in: DRUM bench baseline __ %, + drive + reverb __ %, + compressor __ %. Host-side, the
unit tests show drive 0 / Rvb send 0 / Comp OFF are bit-exact bypasses.

## Tests (native)

- Drive: table monotonic, `drive(x, 0) == x` bit-exact, drive 127 saturates (|y| ≤ 1, y(1) ≈ 1).
- Reverb: impulse → non-zero tail that decays below -60 dB within 3 s at size 60; silent without a
  buffer; size 127 tail longer than size 0; level 0 leaves the mix bit-exact.
- Compressor: gain 1 for a -20 dB sine at `compAmt 64`; a 0 dB sine reduced by the expected dB ± 1 dB
  after 50 ms; a loud `scTrack` voice ducks a quiet voice on another track; `compAmt 0` bit-exact.
- ARM: UP / DOWN / UPDOWN orders over one step at rate 4; RANDOM stays within the set; `ARP` without
  `ARM` unchanged (existing `test_synth` ARP tests pass untouched); ARP + CHD cycles the chord
  intervals and the sequencer emits one note-on; MIDI tracks still get the full chord.
- Velocity: vel 127 vs 64 moves the cutoff by `velCut / 64 × 6` octaves (check `flt` cutoff via a
  test hook or the rendered spectrum peak); velMac shifts the DECAY macro.
- Formats: project and preset round trips for every new field; a 6-byte AUDI and an old FMIN record
  give the defaults; out-of-range values clamp; `fxNextCmd` wraps `ARM → None`.
