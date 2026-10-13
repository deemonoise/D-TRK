# Sample editor without touch — design

Goal: every action of the SMPL page (INST, SAMPLE type) is reachable with the encoder and buttons.
Before, the toolbar (`< > - + CHOP CLR`) and the waveform gestures were touch-only.

## Layout

| Action | Touch (unchanged) | Hardware |
|---|---|---|
| Next / previous marker | `>` / `<` | A tap / Shift + A tap |
| Zoom | `-` / `+` | New list row **Zoom** right under Marker: click + turn (A + turn); centred on the selected marker |
| Scroll | drag on empty space | none separate: the view follows the moved marker and centres on zoom |
| CHOP | `CHOP` | Shift + long press → menu `Chop` |
| New slice | long press on the wave | menu `Add slice`: midway between the selected marker and the next one (none: the sample end); with Shift held on the click — at the next transient right of the selected marker. The new slice gets selected |
| Delete slice | `CLR` | menu `Delete slice` (greyed unless a slice is selected) |
| Delete all slices | long press `CLR` | menu `Clear slices` → confirm |

On the SMPL page Shift + long press opens the menu `SAMPLE`: Chop / Add slice / Delete slice /
Clear slices / Load preset / Save preset. Other INST pages keep the `PRESET` menu.

## Geometry

The list shows 4 rows instead of 2: waveform 148 → 100 px, toolbar 24 px, list 4 × 24 px.

## Notes

- The Zoom row is view state, not instrument data: editing it does not mark the project dirty.
- Hardware actions give toasts where touch was silent (`NO SAMPLE`, `NO ONSET`, `NO ROOM`).
