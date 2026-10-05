# UI tweaks: wrap-around, encoder selection, Transpose dialog, FX repeat

## 1. Wrap-around navigation
Selection in lists wraps (values still clamp):
- `Menu::move` — past the last item to the first, skipping disabled items.
- `ParamList` row selection (TRACK, INST, PROJ, Euclid, Import).
- `FileScreen` file list (`listSel_`) and sample list (`sampleMove`).
Name-edit text cursor stays clamped. GRID / BANK / keyboard / Wi-Fi / track & instrument switch already wrap.

## 2. Encoder selection (GRID)
- Step menu gets "Select": anchor = current cell.
- While a selection is on, every cursor move (turn, Shift+turn over tracks/fields) moves the selection end.
  Shift+tap uses the same state.
- Click while a selection is on drops it (no edit mode).
- Long press: selection menu — Copy sel, Paste, Clear sel, Note OFF sel, Transpose..., Undo, Clear selection.
- Copy drops the selection (move the cursor, Paste at the cursor). Clear / Note OFF / Transpose keep it.

## 3. Transpose dialog
`TransposeDialog` modelled on `EuclidDialog`: header CANCEL / OK, rows Amount, Mode, OK, Cancel.
- Scale: Amount ±2 octaves in degrees (`2 * scaleDegrees`), Shift = one octave of degrees.
- Chromatic: ±24 semitones, Shift = 12.
- Mode switch clamps Amount. OK applies to the selection, else the whole track, one undo step.
  Cancel / long press close without changes. Amount and Mode kept in RAM.
- Transpose +1 / -1 / +12 / -12 menu items removed.
- core: `scaleDegrees(ScaleType)` (notes per octave) with a unit test.

## 4. FX repeat
`lastFx_[track][slot]` (cmd + val) updated on every FX / VAL edit with cmd != None.
On an empty slot the first FX or VAL turn writes the remembered FX with its value; nothing remembered = old behaviour.

## Verification
`pio test -e native`, `pio run`. Manual updated.
