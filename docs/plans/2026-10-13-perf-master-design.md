# PERF on the master — design

## Goal
PERF buttons become punch-in effects on the whole internal mix (PO-33 / EP-133 style) instead of
fx locks on the current track. The DJ filter moves from PROJ → FX into PERF. While a button is
held, the encoder sets that effect's depth / wet. Several held buttons work together.

## Decisions
- Everything on the master; the per-track PERF effects (RAT, ROLL UP, SHORT DECAY, FADE, MUTE,
  per-track CRUSH / DRIVE locks) go. Per-track work stays with step fx.
- Depth is not remembered: every press starts at the effect's default (FILTER: OFF). The encoder
  changes it only while held.
- Several held: the encoder turns the **last pressed** held effect.
- PERF works as now: GRID → `Perf: ON`, while playing. Shift+N still mutes.
- Master audio effects do not touch MIDI tracks (manual note).
- PERF is live only: Render WAV and Resample ignore it.

## Effects
Grid step length from the tempo (swing ignored). Entering / leaving an effect and loop seams
crossfade ~2 ms. Turning shows a toast with the value (`REPEAT 1/16`, `FILTER HP 20`); Shift ×10
where the range is wide.

| Effect | On press | Encoder | Default |
|---|---|---|---|
| REPEAT | loops the buffer from the last step boundary (already played: instant) | 1/2, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32; applies at the next loop | 1/16 |
| ROLL | repeat from 1/8, length halves every N loops down to 1/64 | N 1..4 | 2 |
| REVERSE | head runs backwards from the press point, loops over the window | 1/4, 1/2, 1, 2 bars | 1 bar |
| SCRATCH | head swings (sine) around the press point | period 1/4 .. 1/32 | 1/8 |
| TAPE STOP | speed falls to 0, then silence; release → live | 1/16 .. 1 bar | 1/4 bar |
| GATE | 50 % gate in phase with the grid | 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 | 1/16 |
| FILTER | nothing; clockwise HP, counter-clockwise LP (old DJ filter curves) | −64..+63 | OFF |
| CRUSH | bit depth | 2..12 bits | 6 |
| DOWNSAMPLE | sample and hold | 16 kHz .. 500 Hz | 8 kHz |
| DRIVE | saturation, level compensated | 0..100 | 60 |
| DELAY THROW | whole mix into the delay send while held, tail stays | send 0..127 | 100 |
| REVERB THROW | the same into the reverb | send 0..127 | 100 |
| FREEZE | reverb frozen (feedback 1, input closed), tail hangs | dry/wet: how much the live mix ducks | 50/50 |

Stacking: buffer effects (REPEAT, ROLL, REVERSE, SCRATCH, TAPE STOP) share one read head, so one
plays at a time — the last pressed; on its release the previous still-held one returns. All others
stack freely with everything.

## Engine
- `lib/core/src/synth_perf.h/.cpp`: `PerfBus` — up to 8 held slots {fx, depth} in press order,
  the ring buffer, read head state, gate phase, filter (the `Svf` pair of the old DJ filter),
  crush / downsample / drive state.
- Chain position: where `djFilter()` was (after the compressor and the key track return, before
  MAIN / softClip): buffer write → buffer effect → GATE → FILTER → CRUSH → DOWNSAMPLE → DRIVE.
- THROWs: while held, `mix × send` is added to `send_` / `rsend_` of the next block; the mix is
  taken before delay / reverb (no feedback loop).
- FREEZE: reverb flag — feedback 1, input muted; the encoder ducks the dry mix.
- Grid: the step length in samples from the tempo, phase from the `kSynthStep` messages.
- Buffer: PSRAM, stereo int16, 6 s (~768 KB) = 2 bars down to 80 BPM, else 4 s / 2 s; longer
  windows are clipped. Taken only while PERF is on in GRID (the screen sprite, imports and renders
  need that PSRAM; at boot it starved the sprite: black screen), handed to the audio task zeroed.
  On the device ~670 KB are free, so it is 4 s.
- Off (nothing held): the master is untouched, output bit-exact to today with DJ filter 0.
- CPU estimate +1–2 %.

## Engine commands / sequencer
- `Cmd::PerfOn(fx)`, `Cmd::PerfOff(fx)`, `Cmd::PerfDepth(delta)` (last pressed),
  `Cmd::PerfAllOff` (PERF off, stop, leaving GRID).
- Sequencer: `perfOn/perfOff/perf_`, `perfSlot`, `perfMuted_` and its undo field go.

## Data / files
- New `PerfFx` enum: None, Repeat, Roll, Reverse, Scratch, TapeStop, Gate, Filter, Crush,
  Downsample, Drive, DelayThrow, ReverbThrow, Freeze.
- `perfMap[8]` stays. Loading old maps: RAT 2/3/4/8 → REPEAT, ROLL UP → ROLL, FILTER LOW / HIGH →
  FILTER, DELAY MAX → DELAY THROW, REVERB MAX → REVERB THROW, CRUSH / SRR / DRIVE → CRUSH /
  DOWNSAMPLE / DRIVE, FADE / MUTE / SHORT DECAY → `---`. Needs a version marker for the map.
- `Project::djFilter` goes. AUDI byte 13 is ignored on read and written as 64 (neutral) so older
  firmware reads no filter.

## UI
- PROJ → PERF: Button 1..8 over the 13 effects or `---`. New project: 1 REPEAT, 2 ROLL, 3 FILTER,
  4 GATE, 5 DELAY THROW, 6 REVERB THROW, 7 CRUSH, 8 TAPE STOP.
- PROJ → FX: the DJ filter row goes.
- GRID header in PERF: short names of held effects after `PERF` (`PERF RPT FLT CRS`).
- Demos: DEMO-HOUSE maps its buttons to the new effects; DEMO-LOFI replaces its master LP with
  lower instrument cutoffs.
- Manual (en / ru): PERF section rewritten, DJ filter row removed.

## Tests (native)
- Nothing held: output bit-exact to the current master with DJ filter 0.
- REPEAT replays the segment exactly; no jump above a threshold at the loop seam.
- FILTER 0 is a passthrough; the sign picks HP / LP.
- Stacking: two buffer effects → the last plays, its release brings back the previous;
  FILTER + CRUSH + GATE together.
- Releasing everything returns the live signal.
- Old perfMap mapping; AUDI byte 13 ignored.
- Each effect's default and encoder limits.
