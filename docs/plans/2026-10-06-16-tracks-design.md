# 16 tracks — design

## Goal

Double the track count from 8 to 16: one track per MIDI channel and per instrument, room for drums
without starving the melodic parts. Everything else (patterns, chain, fx, undo) keeps working; old
project files load as tracks 1–8 with tracks 9–16 at their defaults.

This is block 1 of the roadmap agreed on 2026-10-06 (blocks: 16 tracks; drum track / KIT; song + live;
sound; mixer + render). The drum track (block 2) builds on it.

## Model

- `kTracks = 16`. `Project::reset()`: track i gets channel i and instrument i (`model.cpp:51`) — the 16
  tracks, 16 MIDI channels and 16 instruments map one to one.
- `Pattern::steps[kTracks][kMaxSteps]`: 16 x 128 x 14 bytes = 28 KB, the project ≈ 460 KB (PSRAM,
  `main.cpp:36`).
- `Undo::kDepth` 32 → 8: snapshots are whole patterns, 8 x 28 KB ≈ 230 KB. Measure free PSRAM after the
  SYNTH / wavetable work lands (its mip tables may live there) before implementing; if short, drop to 4.

## File format

- `TRKS`, `TOUT` already carry a count byte: an 8-track file fills tracks 1–8, 9–16 keep the defaults
  (`readRecords` loops to `count`). Written files carry 16.
- `PATN`: the header stays 4 bytes (pattern, length, resolution, swing). Today the step size is derived
  from the body size assuming `kTracks` tracks (`project_io.cpp:241`); with 16 tracks an 8-track body no
  longer matches. The reader tries, in order, (tracks, step bytes) = (16, 14), (8, 14), (8, 6): the
  body sizes differ for any length (8 x 14 = 16 x 7 and 7 is not a step size), so the match is unique.
  Tracks beyond the file's count stay cleared. Writer: 16 tracks, 14-byte steps, as now.
- Files written by this firmware do not load on older firmware (PATN size mismatch → BadValue), same as
  earlier format bumps.
- MIDI import maps up to 16 MIDI tracks / channels (was 8); `import_dialog` lists 16 target tracks.

## Sequencer and synth

- All per-track loops and arrays are sized by `kTracks`. `kSynthTracks = kTracks + 1` (preview track
  = 16). Lookahead and tick cost double on core 0; it was far from the limit.
- Voices stay 16 (FM + DRUM ≤ 8), oldest-voice stealing as now. After measuring `CPU %` with 16 INT
  tracks decide separately whether to go to 24.

## GRID

- Overview shows 8 track columns (`kColW` 56 px). New state `half_` (0 = tracks 1–8, 1 = 9–16).
  The cursor moving to a track in the other half (encoder with Shift, track button, track name tap,
  `App` track change) switches `half_` and redraws. No other way to switch halves.
- Column headers show `T9…T16`; the status bar track field is two digits (`T12`).
- Detail view: one track, unchanged.
- Selection and Paste: the "not past track 8" clamps become 16 (`edit_ops`). A selection may span the
  halves; only the visible columns are drawn, the model is the full 16 columns.
- Track name tap: mute, Shift — solo, for the visible half.

## Track buttons

- Button N acts on track `half_ * 8 + N`: select (cursor), Shift + N mute, note entry in edit mode as
  now. The LED mask is built from the visible half (selected / mute). Switching the half (by scrolling)
  refreshes the LEDs. `trackio` stays an 8-button driver; the mapping lives in `App`.

## Other screens

- TRACK, INST (instrument assignment), BANK (Copy-to / Clear / Length act on whole patterns), Euclid and
  Transpose dialogs: track number is `kTracks`-bounded and printed with two digits where needed.

## Tests (native)

- `test_model`: `kTracks == 16`, reset defaults (channel i, instrument i for all 16).
- `test_project_io`: an 8-track file (both 6- and 14-byte steps) loads into tracks 1–8 with 9–16 at
  defaults; a 16-track round trip is bit-exact; a PATN body of a wrong size is `BadValue`.
- `test_midi_import` (or the existing import test): 12 channels land on 12 tracks.
- `test_expand` / `edit_ops`: paste reaching track 16, clamped at 16; selection spanning tracks 6–11.
- `test_sequencer`: notes on track 15 are scheduled; `test_synth`: preview track index 16.
- Euclid on track 15.
