# D-TRK enclosure

Русская версия: [README_ru.md](README_ru.md)

Parametric model for 3D printing: [case.scad](case.scad) (OpenSCAD 2021.01). Ready-made STLs are in [stl/](stl/).

Overall size ~98.4 × 105.4 × 31 mm. On top: the whole WT32-SC01 Plus module (glass with bezel), with Shift, Play (MX) and the EC11 encoder below it. At the back: the charging Type-C (IP5306), the MIDI OUT mini jack and the power switch.

| File | Qty | How to print |
|---|---|---|
| `stl/case_top.stl` | 1 | face down on the bed, no supports |
| `stl/case_bottom.stl` | 1 | bottom down on the bed, no supports |
| `stl/clamp2.stl` | 1 plate (4 clamps) | flat, pins up |
| `stl/knob.stl` or `stl/knob_spline.stl` | 1 | top down on the bed, no supports ([knob.scad](knob.scad)); the first fits a D-shaft, the second a splined one |
| `stl/keycap_test.stl`, then `stl/keycap.stl` | 1; one per MX | top down on the bed, no supports ([keycap.scad](keycap.scad)); find the cross clearance first |

**Version with track buttons** (`case8_*`): overall size 98.4 × 164.4 × 31 mm; print `stl/case8_top.stl` and `stl/case8_bottom.stl` instead of `case_*`, the clamps are the same. Under the screen there is a 4 × 4 MX grid at 19 mm pitch:

```
   [     screen      ]
 Shift  Play  ┌─────────┐
              │ ENC ⌀30 │
   A     B    └─────────┘
   1     2     3     4
   5     6     7     8
```

The encoder sits in the centre of a 2 × 2 block with a ⌀30 knob (`knob.stl`): 4.5 mm to the neighbouring 18 mm keycaps. The floor under the buttons holds the Teensy 4.1 synth board and, next to it, the Audio Shield Rev D (on wires); the PCF8575 fence sits behind the shield. No glue: the boards slide in along grooves — a ledge under each edge, a lip over it. The Teensy goes in from the back, microSD end towards the front wall; lipped grooves only along the front 30 mm (no wires there), the wired part rests on two rails between the pin rows, and a stop on the top half behind the USB keeps it from sliding back. The shield goes in from the back, headphone jack into the front wall, grooves along its full length, with a wedge latch behind the rear edge (it passes with 0.1 mm interference; the lip stops it coming back). The front wall has a slot to the Teensy's card with a finger notch (change the card without opening the case) and the headphone jack labelled PHONES. At the back (left to right, seen from behind): power switch, MIC, MIDI, Type-C; the MIDI and MIC labels are engraved above them. Wiring is in [docs/wiring.md](../docs/wiring.md) (buttons: [PCF8575 and buttons](../docs/wiring.md#pcf8575-and-buttons)).

`case8_*` has two tabs on the bottom half: a tab is a piece of wall rising above the joint; the top goes on from above and the tab enters a cutout in its wall, with grooves on the inner half of the wall at both sides of the cutout taking the tab's ridges. Everything on the tabs is fitted and soldered while the top is off. The front tab (x 60.5…87, up to z = 10.5) carries PHONES and LINE IN at the same height: the hole for the shield's headphone plug and a PJ-392 jack with its M6 nut, ribs down to the floor at both sides of the PJ-392 body. The back tab (x 50.4…79, up to z = 13.5) carries the microphone and the power switch: a grille of 7 ⌀1.2 holes, and inside a ⌀11.2 × 7.5 cup on a pedestal down to the floor for a 9767 capsule (⌀9.7 × 6.7) wrapped in 0.75 mm foam tape (a disc of acoustic cloth between the capsule and the grille); to its right, the switch on its nut. In `case8_*` MIDI is on the top half between Type-C and the microphone (x = 43.5). The labels are on the top half above the cutouts.

PLA/PETG, 3 wall perimeters, 20–30 % infill.

The bottom part is a tray 4.5 mm deep (`tray_h`) with a 3 mm floor: the battery sits 4.5 mm below the joint with the top, with 6 mm clearance to the module. Along the edge runs a 1.2 × 1.5 mm centring lip (`guide_t`, `guide_h`, clearance `guide_clr` = 0.2) that fits inside the top. If the fit is tight, reduce `guide_t` or increase `guide_clr`. The top does not depend on `tray_h`: you can change the tray depth without reprinting the top.

## Measure before printing

All dimensions are variables at the start of `case.scad`. Check with calipers:

| What | Variables | Default |
|---|---|---|
| Module: width, depth, thickness | `mod_w`, `mod_d`, `mod_h` | 92 × 60 × 10.8 |
| Lip over the bezel: overlap and thickness | `lip`, `lip_t` | 1.0 and 1.2 |
| 103450 battery | `bat_w`, `bat_d`, `bat_h` | 50 × 34 × 10 |
| IP5306: board (along the wall × inwards), thickness, Type-C jack | `ip_w`, `ip_d`, `ip_pcb`, `usbc_w`, `usbc_h` | 20 × 26 × 1.2, 9.2 × 3.4 |
| Jack and switch thread | `jack_d`, `sw_d` | 6.3 (M6) |
| Bosses on the module's back: centres from the left/right edge (looking at the screen), from the long edges | `boss_x0`, `boss_x1`, `boss_y` | 10.34, 6.52, 4.3 |
| Bosses: outer ⌀ and hole ⌀ | `boss_d`, `boss_hole` | 6 and 2.5 (not measured) |

`lip` is how far the lip overlaps the module's bezel. Increase it if the bezel is wider and the window shows too much; decrease it if the lip covers the active area of the screen.

The clamps press the module by its 4 corner bosses; the `pin_d` pin (2 mm) goes into the boss hole and keeps the clamp from turning around its screw — `pin_d` must be smaller than `boss_hole`. The 10.34 offset is from the right edge looking at the board, i.e. from the left edge looking at the screen; if the module sits in the case rotated 180°, set `boss_flip = true`. The old bars (v1, 2 pcs. with tape) are `part="clamp"`.

After editing, rebuild the STLs:

```
OS=openscad   # OpenSCAD 2021.01 or newer; give the full path if it is not on PATH
$OS -o stl/case_top.stl    -D 'part="top"'    case.scad
$OS -o stl/case_bottom.stl -D 'part="bottom"' case.scad
$OS -o stl/clamp2.stl      -D 'part="clamp2"' case.scad
```

For the track-button version, add `-D trk=true` and write to `stl/case8_*.stl`. Measure the modules on the floor (width along X × depth along Y, without protruding pins):

| Module | Variables | Default |
|---|---|---|
| PCF8575 | `pcf_w`, `pcf_d` (centre `pcf_x`, `pcf_y`) | 32 × 20 |
| Teensy 4.1 | `teensy_w`, `teensy_d` (centre `teensy_x`; the card's overhang `teensy_sd_out` and height above the board `teensy_sd_z`, the USB overhang `teensy_usb_out`) | 18 × 61; card 2.5 mm past the board, its axis 1 mm above it, USB 0.8 mm past the board (measure) |
| Audio Shield Rev D | `aud_w`, `aud_d` (left edge `aud_x0`); jack axis from the left edge `aud_jack_dx` (top view, jack towards you), above the board `aud_jack_z`, overhang `aud_jack_out` | 36.83 × 35.56 from the PJRC footprint; jack 31.7, 2.5 and 0.5 (measure) |
| Grooves | `rail_gap` (gap above the board), `rail_clr` (gap to the groove wall), `snap_h` (wedge height) | 0.15, 0.15 and 0.25 |
| MIC capsule | `mic_cup_d` (cup bore: capsule ⌀ + 2 × wrap), `mic_cup_h` (depth); centre `mic_x`, axis `mic_z` | 11.2 and 7.5; 57 and 9 |
| LINE IN and tabs | `lin_x` (axis at the PHONES height), `tab_f_top`, `tab_b_top` (top of the front and back tabs; the edges follow the jacks), `tab_clr` (gap in the cutout) | 81; 10.5 and 13.5; 0.2 |

If a tab is tight in the top's cutout, increase `tab_clr`. The boards sit tight in the grooves. If one does not go in, increase `rail_gap` or `rail_clr`; if it rattles or the latch does not hold, decrease `rail_gap` and increase `snap_h`. In the track-button version the IP5306 is on grooves too: it slides in from the front, Type-C into the rear wall, with a latch in front of its front edge; fit it before the battery.

There are ~15 mm above the modules to the bottom of the switches and the encoder: leave out pin headers (or use right-angle ones) and solder wires straight to the pads. There are 3 mm under the Teensy and 2 mm under the shield for solder tails. If a module or a board does not fit the bay, `case.scad` stops with "модуль вне отсека" or "плата вне отсека" (module / board outside the bay).

`part="assembly"` (the default) is an assembly with component mock-ups for checking in OpenSCAD.

The most critical part is the top: print `case_top.stl` first and test-fit the module, then the rest.

## Encoder knob

[knob.scad](knob.scad): ⌀30 × 18 mm, flat top with a 3 mm × 45° chamfer carrying radial notches (36 V-grooves, `chamfer`, `cham_n`), diamond knurling (two opposing helical grooves, pitch ~2.6 mm — 36 of them on ⌀30, 30° to the axis), fits a D-shaft (`knob.stl`) or a splined shaft (`knob_spline.stl`, `shaft="spline"`: 18 teeth along the top 7 mm of the shaft, a round bore below). The underside is recessed for the nut and threaded bushing, so the knob floats 1 mm above the panel (0.5 mm with the encoder button pressed). The shaft goes 12.5 mm into the knob.

Both cases use `knob.stl` (⌀30): in the track-button case the encoder takes a 2 × 2 block of the MX grid.

Measure the encoder:

| What | Variable | Default |
|---|---|---|
| Shaft from the encoder body's seating plane to the tip | `shaft_l` | 20 |
| Shaft ⌀ and size across the flat | `shaft_d`, `shaft_dd` | 6 and 4.5 |
| Flat length | `flat_l` | 10 |
| Splined shaft: teeth, root ⌀, knurl length | `spline_n`, `spline_dr`, `spline_l` | 18, 5.4 and 7 |
| Bushing: ⌀ and height | `bush_d`, `bush_l` | 7 and 7 |
| Nut: across flats and height, washer | `nut_af`, `nut_h`, `washer_t` | 10, 2, 0.5 |
| Button travel | `push` | 0.5 |

The knob height depends on `shaft_l`. If it is tight on the shaft, increase `shaft_clr` (0.15); if it wobbles, decrease it. `part="view"` shows the knob on the panel with an encoder mock-up.

```
$OS -o stl/knob.stl knob.scad
$OS -o stl/knob_spline.stl -D 'shaft="spline"' knob.scad
```

## Keycaps

[keycap.scad](keycap.scad): 1u for an MX stem (Outemu, Cherry, Gateron), flat top — printed top down on the bed with no supports, the stem growing up from the ceiling. 18 × 18 × 7.4 mm, top 12.7 × 12.7, a 0.6 mm chamfer on the top edge (it sits on the bed — against elephant's foot), 1.5 mm walls. Legend: `legend`, engraved 0.6 mm into the top face.

Print the test plate `keycap_test.stl` first, with the same filament and settings: 6 stems with a cross socket, clearance per side 0.05…0.30 mm (the number next to a stem is in hundredths of a mm). Try each on a switch, pick the one that goes on firmly without force and does not wobble, and put it in `cross_clr`. Default 0.10 — fitted for ABS GF on Outemu MX Brown (0.15 already wobbles). The switch cross is nominally 4.0 × 1.17 (`cross_l`, `cross_w`) — Outemu can be slightly thicker, measure it.

ABS GF: hardened nozzle, 0.12–0.16 layers, 3–4 perimeters, minimal cooling, enclosed chamber. No acetone smoothing — the fibres come up to the surface.

```
$OS -o stl/keycap_test.stl -D 'part="test"' keycap.scad
$OS -o stl/keycap.stl keycap.scad
$OS -o stl/keycap_play.stl -D 'legend="PLAY"' keycap.scad
```

## Hardware and materials

Full parts list for both versions: [docs/bom.md](../docs/bom.md).

- M3 heat-set inserts (5–6 mm long, outer ⌀ ~4.5) — 8 pcs.
- M3 × 10 DIN 912 screws (socket head cap) — 4 pcs. (bottom part, heads sunk flush).
- M3 × 6 screws — 4 pcs. (clamps).
- ~1 mm foam double-sided tape — under the battery.
- PJ-392 panel-mount mini jack (TRS 3.5 mm, M6 nut).
- MTS-102 toggle switch (M6 nut).
- IP5306 charger/power-bank module with Type-C and USB-A (desolder the USB-A).
- LiPo 103450 battery (~2000 mAh).
- INA226 module (battery monitor, R100 or R010 shunt).
- 2 MX switches (12 for the track-button version), EC11 encoder with push button, keycaps.
- PCF8575 module — in both versions: without it the encoder button, Play and Shift do not work.
- Track-button version: Teensy Audio Shield Rev D (SGTL5000, headphone jack on the board), a ⌀9.7 mm electret capsule and a PJ-392 jack (LINE IN).

## Power

The IP5306 module charges the LiPo from Type-C by itself and puts out a stable 5 V. Desolder the USB-A socket (otherwise it won't fit height-wise) and solder the output wires to its VBUS and GND pads.

```
LiPo + ── INA226 IN+ ─[shunt]─ INA226 IN− ── IP5306 B+ (BAT+)
LiPo − ───────────────────────────────────── IP5306 B− (BAT−)
IP5306 VOUT (VBUS USB-A) ── switch ── board 5V
IP5306 GND               ───────────── board GND
```

The switch is on the output, not on the battery: this way charging works even with the device switched off.

The INA226 measures the battery voltage and current for the charge indicator; it runs from the board's 3.3 V, its I2C is in parallel with the PCF8575. Details in the [wiring diagram](../docs/wiring.md#ina226-battery-monitor).

IP5306 quirks:
- The output turns on by itself when a load appears (the switch is closed). If it does not, briefly press the module's button or short its pads (KEY) to GND. If there is no button and auto-start is unreliable, move the switch into the battery line, but then charge only with the switch on.
- Below ~50 mA the output shuts off after ~30 s. The board draws ~175 mA, so this is not a problem.
- The output stays on while charging (power comes from Type-C). At the moment charging is connected or disconnected the output may drop briefly and the board will reboot. Connect the charger with the device off, or after saving.
- The charge-level LEDs are on the board inside the case and are not visible from outside.

Do not connect the battery directly to the board's 3.3V or 5V.

The board's own Type-C (flashing) is accessible only with the lid off; its microSD slot is not used (the card is in the Teensy). The Teensy's USB is inside too: flash it once before assembly. Do not flash with the switch closed: two 5 V sources on the same line.

## Assembly

1. Melt the inserts in with a soldering iron: 4 into the corner posts, 4 into the clamp posts (all in the top part).
2. Insert the module screen-down into the pocket of the top part, until it rests against the lip.
3. Put the clamps' pins into the holes of the module's corner bosses and screw the clamps to the posts with M3 screws.
4. Snap the MX switches into the panel from above. The encoder goes in from below, secured with its nut from above.
5. Fit the jack and the switch into the back-wall holes and tighten their nuts (in `case8_*` the switch goes into the back tab of the bottom half). Turn the switch so the long side of its body (7.9 mm) is vertical.
6. In the bottom part: the IP5306 into its cradle with the Type-C jack towards the back wall, the battery on tape against the ridge under the module.
7. Solder the power (see above); the controls and MIDI OUT per [docs/wiring.md](../docs/wiring.md).
8. Check for ~5 V at the IP5306 output before connecting it to the board, then close the bottom part with 4 M3 × 10 screws.

## License

The enclosure (OpenSCAD sources and STL): © 2026 deemonoise, [CERN-OHL-S v2](LICENSE).
