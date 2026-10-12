# Track speed — design

Per pattern, per track: the track plays its steps faster or slower than the pattern grid. Needed
for the Virus arp patterns (32 steps at 1/32 while drums stay at 1/16), useful on its own.

## Model and file

- `Pattern` += `uint8_t trackSpeed[kTracks]`, values of `enum class TrackSpeed : uint8_t
  { X1, X2, X4, Half, Quarter, Count }` (0 = x1, so zeroed / old patterns are x1).
- Project file: optional chunk `TSPD` (pattern index + kTracks bytes) after its `PATN`, like
  `TLEN`. Written only when a track of the pattern is not x1; files without speeds are byte for
  byte unchanged. Unknown values load as x1. `patternStored()` counts it, `clear()` resets it,
  Copy to… copies the whole `Pattern` already.

## Which track step plays

One shared function in `model.h`, used by the sequencer and GRID. `L` = the track's length
(`trackLen`, else the pattern length), `pos` = the pattern step, `pass` = passes of this pattern
since it started (`Sequencer::loop_`).

- x1: `pos % L` (unchanged, polymeter as now).
- xN (2, 4): N track steps per pattern step: `(pos * N + k) % L`, k = 0..N-1.
- /D (1/2, 1/4): the track plays on pattern steps where `c % D == 0`, c = `pass * length + pos`;
  step `(c / D) % L`. A 1/2 track of 16 steps in a 16-step pattern plays all 16 over two passes.
  c restarts with the pattern (start, pattern change), like `loop_`.

## Sequencer

- The per-track body of `scheduleStep` moves into `playTrackStep(tr, step, t, su, ctx, earliest)`.
  The outer loop, history and rewind stay per pattern step: sub-steps belong to the same step, so
  undo / replanning work as before (`loop_` and `pos_` are already restored by rewind, so c is too).
- xN: sub-step k at `t + k * su / N`, step duration `su / N`. /D: the step at `t` with duration
  `su * D`; on the pattern steps it skips the track is not touched (TIE and ARS go on).
- The track's step duration goes into `ExpandCtx` (`su`, `ticks`) and `pushStepStart` (tps), so
  GAT, RAT, NDG, TIE, ARS, VSL count from the track's own step.
- Swing and groove stay on the pattern grid: they move the pattern step; sub-steps inside it are
  even.
- `skipStep` goes through the sub-steps the same way.
- Risk: x4 tracks push 4x the events per step. A test with all 16 tracks at x4 with RAT checks the
  `EventHeap` capacity (1024).

## UI

- TRACK: row `Speed` after `Pat len`: `1/4, 1/2, x1, x2, x4`; undo like `Pat len`.
- GRID Detail view (one track): the play row and follow use the track's own step (first sub-step
  for xN). Overview keeps the pattern row, as with polymeter now. REC uses the track's step and,
  for xN, the phase inside the sub-step.
- Manual / README (+ru): the Speed row. The Virus section: "the original tempo = track x2".

## Tests (native)

- `test_model`: the index function for every speed, with and without `trackLen`; 1/2 covers the
  whole track in two passes.
- `test_sequencer`: x2 = two notes per step su/2 apart with the gate of half a step; /2 = a note on
  every other step; x1 and x2 tracks stay in sync; rewind / replan with x2; no heap overflow at x4.
- `test_project_io`: `TSPD` round trip; a project without speeds is unchanged.

## Order

Track speed first (core, then UI), then the Virus arp patterns
(`2026-10-10-virus-arp.md`). Both in `feature/dac`, device check, then one commit.
