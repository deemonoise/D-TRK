# Song structure and live performance — design

## Goal

Block 3 of the roadmap (after 16 tracks and the drum track). Two halves:

- **Song (A):** chain items carry transpose, repeat count and a mute scene; a track may have its own
  length inside a pattern (polymeter).
- **Live (B):** fill (conditional steps while a button is held), mute scenes, live recording from the
  track buttons, punch-in effects held on the track buttons.

Assumes block 1 (`kTracks = 16`, `App::trackKey(n)` maps button n onto the visible half) and block 2
(`Project::trackIsDrum(t)`; drum tracks are never transposed).

## Model

- `Project::chainTr[kChainMax]` int8 −24..24 (default 0), `chainRep[kChainMax]` uint8 1..16 (default 1),
  `chainScene[kChainMax]` uint8 0 = none, 1..8 (default 0). Parallel to `chain[]`; insert / delete rows
  move all four arrays together (`edit_ops` helper `chainInsert` / `chainDelete`).
- `Project::scenes[8]` uint16: bit t = track t muted; `kSceneEmpty = 0xFFFF` (all 16 muted is not a
  useful scene, so it marks "empty").
- `Pattern::trackLen[kTracks]` uint8: 0 = the pattern length, else 1..`length`. `sizeof(Pattern)` grows by
  16 (storage.cpp static_assert). Copied with the pattern (undo, Copy-to); `Clipboard` and Copy track
  ignore it. `Pattern::clear()` zeroes it; `isEmpty()` ignores it.
- CND gains two values: `kCndFill = 0x01` (FIL: only while fill is held) and `kCndNoFill = 0x02` (NFL:
  only while it is not). Both have a low nibble < 2, which no A:B value has (b = 2..8), and 0 stays FST.
- `ExpandCtx::fill` bool.
- Transient perf fx (not saved): `enum class PerfFx : uint8_t { None, Rat2, Rat4, FltLow, FltHigh,
  DlyMax, DecShort, Fade, Mute }` = buttons 1..8.

## File format

- `CHN2`: count, then per item `pattern, transpose (int8), repeat, scene` (4 bytes). Written instead of
  relying on the chain bytes inside `PROJ` (those are still written for old firmware; the loader applies
  `CHN2` after `PROJ` when present, else tr 0 / rep 1 / scene 0). Values clamped on load.
- `TLEN`: pattern index + 16 bytes, one chunk per stored pattern, written after its `PATN`; values
  clamped to 0..length. Old files: all 0.
- `SCNS`: 8 × uint16 LE. Old files: all `kSceneEmpty`.

## Sequencer

- Steps are expanded from a **working copy** of the step: `Step s = pat.steps[tr][idx]` then
  `adjustStep(s, tr)` adds the chain transpose to the note of a melodic track (clamped 0..127, `kNoteOff`
  / empty untouched) and writes the perf fx of the track into a slot (the first `None` slot, else slot
  `kFxSlots - 1`). `expandStep` is unchanged apart from the fill flag.
- Polymeter: `idx = pat.trackLen[tr] ? pos_ % pat.trackLen[tr] : pos_`. The pass counter (`loop_`, CND,
  the status bar) keeps following the pattern length.
- Repeat: `rep_` counts passes of the current item. `endOfPass()` in song mode: `if (++rep_ <
  chainRep[songPos_])` stay (like a repeated entry: ties and CND continue); else `rep_ = 0`, advance.
  `Sched` stores `rep` / `prevRep` so `rewind()` restores it. `chainEdited()` clamps `rep_`.
- Scene: when an item starts (`endOfPass` picks `next`, and `start()` at chain[0]) and `chainScene` is
  set, `applyScene()` writes `tracks[t].mute` from `scenes[scene - 1]` (skipped for an empty scene). Muted
  tracks get `pushOff` at that step so nothing hangs.
- Fill: `setFill(bool)`; `ExpandCtx::fill = fill_`. Steps already scheduled are not re-decided (≤ lookahead,
  ~1 step late at most).
- Perf: `perfOn(track, fx)` / `perfOff(track)` set `perf_[track]`. `Mute` makes the track inaudible in
  `scheduleStep` (and `pushOff` once); the others map to fx: Rat2 → `RAT 2`, Rat4 → `RAT 4`, FltLow →
  `FLT 30`, FltHigh → `FLT 120`, DlyMax → `DLY 127`, DecShort → `DEC 20`, Fade → `VSL -16`. On MIDI tracks
  only Rat2 / Rat4 / Fade (velocity) / Mute act; the synth-only fx are skipped (`fxSynthOnly`).
- Record position: `uint8_t phase256(uint64_t now) const` = position inside the heard step; `Status`
  carries it. `recordStepFor(pos, phase, len)` (pure, `lib/core/src/record.h`) = `phase < 128 ? pos :
  (pos + 1) % len`.

## UI

**BANK (chain).** Row text `P05 +3 x2 S1` (fields omitted when default). Encoder: click toggles row
edit as now; in row edit, **Shift + turn** moves between the fields pattern / transpose / repeat /
scene (the active field is drawn in `kCursor`), turn edits the field. Touch: tap the field. Row menu
unchanged. Under the chain list a row of 8 scene tiles `S1..S8` (filled = stored): tap = recall, long
press = store the current mutes, Shift + tap = clear. Recall / store go under the project lock and mark
dirty.

**GRID.** Context menu gains `Rec: ON/OFF` and `Perf: ON/OFF` (mutually exclusive with each other and
with edit mode: turning one on turns the others off). The title bar shows `REC` (red) or `PERF`
(yellow). Overview cells past a track's `trackLen` are drawn in `kDim` with a 1-px line at the boundary;
Detail draws the same line.

**TRACK.** New param `Pat len` (OFF / 1..length) editing `trackLen[curTrack]` of the edit pattern,
under the lock, with an undo snapshot. BANK `Length` clamps every `trackLen` to the new length.

**Status bar.** `FILL` while fill is held (replaces the `L` counter field while held).

## Controls

- **Fill:** Shift + Play is taken (pause / resume, `app.cpp:74`), so fill = **hold Shift + Play while
  playing**: fill starts on the press; on release, a hold shorter than `kLongPressMs` is a normal
  Shift + Play (pause), a longer one just ends the fill. `input` gains `PlayRelease`.
- **Track buttons** gain `TrackRelease` events (`trackio` already debounces a state mask; emit on 1→0).
- **REC armed and playing:** button N (no Shift) writes degree N (edit-mode octave logic) into the
  current track at `recordStepFor(...)`; Shift + N clears that step's note. One undo snapshot per pass
  (taken when `loopCount()` changes). Track selection while armed: tap the track name.
- **PERF on and playing:** `TrackPress` N → `perfOn(curTrack, N)`, `TrackRelease` N → `perfOff`. Shift +
  N keeps muting.

## Tests (native)

- `test_project_io`: `CHN2` / `TLEN` / `SCNS` round trips; old file → tr 0, rep 1, scene 0, trackLen 0,
  scenes empty; garbage clamped.
- `test_sequencer`: transpose applies to a melodic MIDI and INT track and not to a drum track; an item
  with rep 3 plays 3 passes before the next; `rewind` across the advance restores `rep_`; polymeter: a
  track with trackLen 3 in a 16-step pattern plays its step 0 at positions 0, 3, 6…; a scene item sets
  the mutes; fill on/off with FIL / NFL steps; perf Rat2 doubles the NoteOns, Mute silences, perfOff
  restores; `phase256` at the step start = 0, mid-step = 128.
- `test_expand`: `cndPasses` for FIL / NFL / FST / A:B; `fx_info` CND order and names.
- `test_edit`: `chainInsert` / `chainDelete` move all four arrays.
- `test_record`: `recordStepFor` rounding and wrap.
