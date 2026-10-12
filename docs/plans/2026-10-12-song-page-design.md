# SONG page (Polyend-style) — design

Replaces the chain-in-BANK "song mode" with a dedicated SONG tab modelled on Polyend Tracker's song screen.

## Data

- Song = up to `kSongMax = 128` slots. Slot = pattern (`uint8_t`) + mute mask (`uint16_t`, bit t = track t muted).
- Removed from `Project`: `chain*` arrays (`chainTr`, `chainRep`, `chainScene`), `scenes[]`, `songMode`.
- Slot storage in `Project`: `uint8_t song[kSongMax]`, `uint16_t songMute[kSongMax]`, `uint8_t songLen`.
- No song-mode flag in the project. SONG/LOOP is a transport state only.

### Old projects (load-time migration)

- `CHN2` item with repeat N becomes N consecutive slots (clipped at 128).
- Scene S1..S8 becomes the slot mask (`scenes[s-1]`; an empty scene counts as "none").
- Scene "none" inherits the previous slot's mask (first slot: 0 = all unmuted). This keeps the old "leave mutes alone" behavior.
- Transpose is dropped. If any item had non-zero transpose, the loader reports it and the UI toasts `SONG: TRANSPOSE DROPPED`.
- Older PROJ-embedded chain: same rules (repeat 1, no scene).
- New chunk `SNG3`: count u8, then per slot pattern u8 + mute u16 LE. Saving writes only `SNG3`. `CHN2` / `SCEN` are read for migration only.

## Playback

- Transport mode: SONG or LOOP.
  - Play on the SONG tab starts the song from the slot under the cursor.
  - Play on any other tab loops the current pattern.
  - While running, changing tabs does not change the mode.
- Status bar shows `S03 P05` in SONG and `P05` in LOOP. Tapping the indicator toggles the mode at the end of the current pass.
  - LOOP→SONG continues from the SONG cursor slot.
  - SONG→LOOP keeps looping the heard pattern.
- The song loops: after the last slot, slot 1 plays again. An empty song makes Play loop the pattern.
- Slot start sets all 16 track mutes from the slot mask.
  - Live Shift+N toggles a mute until the next slot starts. It does not write to the slot.
  - Slot masks are ignored in LOOP.
- Edits during play are picked up at the end of the pass, as now. Inserting or deleting above the heard slot keeps the position.
- The pattern queue (B+N, BANK tile tap) works only in LOOP. In SONG it toasts `SONG MODE`. B+Shift+N (switch immediately) still works.
- GRID shows the heard pattern (already the case).
- Stopped: moving the SONG cursor makes that slot's pattern current.

## SONG tab

Tabs: GRID, TRACK, INST, MIX, BANK, SONG, PROJ, FILE (8 x 60 px).

### Layout

The area is 480x272.

- Header (24 px): `SONG n/128`, then track numbers 1..16 above the columns.
- 10 rows of 24 px, with scrolling. Each row has three parts:
  - Slot number (about 22 px). The heard slot gets `▶`, and its number cell fills with pass progress.
  - Pattern `Pxx` (about 34 px).
  - 16 cells of about 26 px, with a 2 px extra gap after track 8.
- Cell states:
  - Filled: the track plays.
  - Outline: the track is muted in this slot.
  - Dim dot: the track has no steps in this pattern. Mute can still be set on it.
- Cursor: yellow frame on (row, column). Columns are PAT and tracks 1..16.
- Empty song: one hint row `+ ADD Pxx`. A click adds the heard pattern.
- During play the list follows the heard slot. After a manual scroll, following pauses for about 2 s.

### Controls

| Input | Action |
|---|---|
| Turn | Move the row |
| Shift+turn | Move the column |
| Click on PAT / hold A + turn | Change the slot's pattern (P01..P16) |
| Click on a track cell | Toggle that track's mute in the slot |
| Tap a cell | Move the cursor there. On a track cell, also toggle mute |
| Tap PAT of the selected row and drag up/down | Change the pattern |
| Track button N | Toggle mute of track N (visible half) in the selected slot |
| Shift+N | Live mute (as everywhere) |
| Tap A | **Add**: a copy of the selected slot below it; the cursor moves to it |
| Long press / long tap | Row menu: Add, Insert Pxx above, Delete, Mutes ← live, Clear mutes, Clear song (confirm) |
| Shift+B | Undo of song edits (snapshots of the whole song, ~384 B each) |
| Play | Song from the selected slot |

## Other changes

- BANK: tiles 4x4 plus the queue only. Remove the chain list, scenes and the "Song mode" menu item.
- Render WAV: Source `SONG` walks the slots, with no repeats.
- Demos: expand repeats into slots. The demo with transpose +2 gets transposed pattern copies.
- Tests: sequencer (song/loop, masks, edits during play), project_io (CHN2/SCEN → SNG3 migration, SNG3 round-trip), demos, edit ops (add/insert/delete), render length.
- Manual ru and en: new SONG section, BANK without chain, tab order, status-bar toggle, A/B tables.
- Commit only after the hardware check (single commit).

## Out of scope

More than 16 patterns. This is a separate task, using sparse pattern storage.
