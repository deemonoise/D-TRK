# D-TRK enclosure

Русская версия: [README_ru.md](README_ru.md)

Parametric model for 3D printing: [case.scad](case.scad) (OpenSCAD 2021.01). Ready-made STLs are in [stl/](stl/).

Overall size ~98.4 × 105.4 × 31 mm. On top: the whole WT32-SC01 Plus module (glass with bezel), with Shift, Play (MX) and the EC11 encoder below it. At the back: the charging Type-C (IP5306), the MIDI OUT mini jack and the power switch.

| File | Qty | How to print |
|---|---|---|
| `stl/case_top.stl` | 1 | face down on the bed, no supports |
| `stl/case_bottom.stl` | 1 | bottom down on the bed, no supports |
| `stl/clamp2.stl` | 1 plate (4 clamps) | flat, pins up |
| `stl/knob.stl` | 1 | top down on the bed, no supports ([knob.scad](knob.scad)) |

**Version with track buttons** (`case8_*`): overall size 98.4 × 164.4 × 31 mm; print `stl/case8_top.stl` and `stl/case8_bottom.stl` instead of `case_*`, the clamps are the same. Under the screen there is a 4 × 4 MX grid at 19 mm pitch:

```
   [     screen      ]
 Shift  Play  ┌─────────┐
              │ ENC ⌀30 │
   A     B    └─────────┘
   1     2     3     4
   5     6     7     8
```

The encoder sits in the centre of a 2 × 2 block with a ⌀30 knob (`knob.stl`): 4.5 mm to the neighbouring 18 mm keycaps. The floor under the buttons has fences for the PCF8575, the PCM5102A DAC and the MAX97220 amplifier. At the back (left to right, seen from behind): power switch, MIDI, PHONES, Type-C (switch and MIDI as in the two-button version); the labels are engraved above the jacks. The 3 mm LEDs go into the standard windows of the MX switches (you need clear keycaps or keycaps with a window). Wiring is in the [README](../README.md#buttons-on-the-pcf8575-required) and [docs/wiring.md](../docs/wiring.md).

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
| PCF8575 | `pcf_w`, `pcf_d` | 32 × 20 |
| PCM5102A | `dac_w`, `dac_d` (centre `dac_x`, `dac_y`) | 18 × 38 (black "PCM5102 audio DAC v2", from a photo; headers along Y, pins up to 51 mm) |
| MAX97220 | `amp_w`, `amp_d` (centre `amp_x`, `amp_y`) | 30 × 23 (from the listing) |

There are ~15 mm above the modules to the bottom of the switches and the encoder: leave out pin headers (or use right-angle ones) and solder wires straight to the pads. The DAC's right-angle headers can stay: no Dupont housings, solder the wires to the pins (they can be trimmed to 3 mm). If a module does not fit the bay, `case.scad` stops with "модуль вне отсека" (module outside the bay).

`part="assembly"` (the default) is an assembly with component mock-ups for checking in OpenSCAD.

The most critical part is the top: print `case_top.stl` first and test-fit the module, then the rest.

## Encoder knob

[knob.scad](knob.scad): ⌀30 × 18 mm, flat top with a 3 mm × 45° chamfer carrying radial notches (36 V-grooves, `chamfer`, `cham_n`), diamond knurling (two opposing helical grooves, pitch ~2.6 mm — 36 of them on ⌀30, 30° to the axis), fits a D-shaft. The underside is recessed for the nut and threaded bushing, so the knob floats 1 mm above the panel (0.5 mm with the encoder button pressed). The shaft goes 12.5 mm into the knob.

Both cases use `knob.stl` (⌀30): in the track-button case the encoder takes a 2 × 2 block of the MX grid.

Measure the encoder:

| What | Variable | Default |
|---|---|---|
| Shaft from the encoder body's seating plane to the tip | `shaft_l` | 20 |
| Shaft ⌀ and size across the flat | `shaft_d`, `shaft_dd` | 6 and 4.5 |
| Flat length | `flat_l` | 10 |
| Bushing: ⌀ and height | `bush_d`, `bush_l` | 7 and 7 |
| Nut: across flats and height, washer | `nut_af`, `nut_h`, `washer_t` | 10, 2, 0.5 |
| Button travel | `push` | 0.5 |

The knob height depends on `shaft_l`. If it is tight on the shaft, increase `shaft_clr` (0.15); if it wobbles, decrease it. `part="view"` shows the knob on the panel with an encoder mock-up.

```
$OS -o stl/knob.stl knob.scad
```

## Hardware and materials

- M3 heat-set inserts (5–6 mm long, outer ⌀ ~4.5) — 8 pcs.
- M3 × 10 DIN 912 screws (socket head cap) — 4 pcs. (bottom part, heads sunk flush).
- M3 × 6 screws — 4 pcs. (clamps).
- ~1 mm foam double-sided tape — under the battery.
- PJ-392 panel-mount mini jack (TRS 3.5 mm, M6 nut).
- MTS-102 toggle switch (M6 nut).
- IP5306 charger/power-bank module with Type-C and USB-A (desolder the USB-A).
- LiPo 103450 battery (~2000 mAh).
- 2 MX switches (12 for the track-button version), EC11 encoder with push button, keycaps.
- Track-button version: PCF8575 module, PCM5102A DAC, MAX97220 amplifier, a second PJ-392 jack (headphones), optionally 8 × 3 mm LEDs and 8 × 330 Ω resistors.

## Power

The IP5306 module charges the LiPo from Type-C by itself and puts out a stable 5 V. Desolder the USB-A socket (otherwise it won't fit height-wise) and solder the output wires to its VBUS and GND pads.

```
LiPo + ── IP5306 B+ (BAT+)
LiPo − ── IP5306 B− (BAT−)
IP5306 VOUT (VBUS USB-A) ── switch ── board 5V
IP5306 GND               ───────────── board GND
```

The switch is on the output, not on the battery: this way charging works even with the device switched off.

IP5306 quirks:
- The output turns on by itself when a load appears (the switch is closed). If it does not, briefly press the module's button or short its pads (KEY) to GND. If there is no button and auto-start is unreliable, move the switch into the battery line, but then charge only with the switch on.
- Below ~50 mA the output shuts off after ~30 s. The board draws ~175 mA, so this is not a problem.
- The output stays on while charging (power comes from Type-C). At the moment charging is connected or disconnected the output may drop briefly and the board will reboot. Connect the charger with the device off, or after saving.
- The charge-level LEDs are on the board inside the case and are not visible from outside.

Do not connect the battery directly to the board's 3.3V or 5V.

The board's own Type-C (flashing) and the microSD slot are accessible only with the lid off. Do not flash with the switch closed: two 5 V sources on the same line.

## Assembly

1. Melt the inserts in with a soldering iron: 4 into the corner posts, 4 into the clamp posts (all in the top part).
2. Insert the module screen-down into the pocket of the top part, until it rests against the lip.
3. Put the clamps' pins into the holes of the module's corner bosses and screw the clamps to the posts with M3 screws.
4. Snap the MX switches into the panel from above. The encoder goes in from below, secured with its nut from above.
5. Fit the jack and the switch into the back-wall holes and tighten their nuts. Turn the switch so the long side of its body (7.9 mm) is vertical.
6. In the bottom part: the IP5306 into its cradle with the Type-C jack towards the back wall, the battery on tape against the ridge under the module.
7. Solder the power (see above); the controls and MIDI OUT per the table in the [root README](../README.md#wiring).
8. Check for ~5 V at the IP5306 output before connecting it to the board, then close the bottom part with 4 M3 × 10 screws.

## License

The enclosure (OpenSCAD sources and STL): © 2026 deemonoise, [CERN-OHL-S v2](LICENSE).
