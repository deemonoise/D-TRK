# Bill of materials

Parts for the full build (`case8` enclosure: 12 buttons, Audio Shield, headphone output). The reduced build is described [below](#reduced-build). Wiring: [wiring.md](wiring.md), enclosure: [enclosure/README.md](../enclosure/README.md).

Русская версия: [bom_ru.md](bom_ru.md)

## Electronics

| # | Part | Qty | Notes |
|---|---|---|---|
| 1 | WT32-SC01 Plus (ESP32-S3, 16 MB flash, PSRAM, 3.5″ touch) | 1 | display, touch and microSD are on the board |
| 2 | PCF8575 module (I2C expander) | 1 | required; up to 32 × 20 mm |
| 3 | Teensy 4.1 | 1 | synth board: synthesizer, microSD card, sample bank |
| 4 | Teensy Audio Shield Rev D (SGTL5000) | 1 | sound and the headphone jack; can be added later |
| 5 | INA226 module (battery monitor) | 1 | R100 shunt in a 2512 package; R010 to measure charge current too |
| 6 | IP5306 charger module with Type-C and USB-A | 1 | desolder USB-A; board ~20 × 26 mm |
| 7 | LiPo 103450 battery, ~2000 mAh, with protection board | 1 | |
| 8 | EC11 encoder with push button, D shaft | 1 | |
| 9 | MX switches | 12 | Shift, Play, A, B + 8 track buttons |
| 10 | MX keycaps | 12 | |
| 11 | MTS-102 toggle switch (M6 nut) | 1 | power |
| 12 | PJ-392 panel jack, TRS 3.5 mm (M6 nut) | 2 | MIDI OUT and LINE IN; headphones use the shield's jack |
| 13 | 10 Ω resistor | 1 | MIDI Tip (IO10) |
| 14 | 33 Ω resistor | 1 | MIDI Ring (3V3) |
| 15 | 10 nF capacitor | 2 | optional: encoder A / B to GND |
| 16 | 4.7 kΩ resistor | 2 | only if the PCF8575 / INA226 modules have no SDA / SCL pull-ups |
| 17 | microSD card | 1 | projects and samples; goes into the Teensy slot, FAT32 or exFAT (MBR) |
| 18 | Hook-up wire | — | INA226 IN+ / IN− — same gauge as the battery wire |
| 19 | PSRAM APS6404L-3SQR (8 MB, SOIC-8) | 1 | optional: sample cache and PERF buffer, on the first pad under the Teensy (CS0) |
| 20 | Electret capsule ⌀9.7 mm, 2 leads (generic "9767", Kingstate KECG2742, PUI POM-2244P) | 1 | recording mic; capsule only, no amplifier module; −42…−46 dB |
| 21 | Capacitor 33–100 pF | 1 | across the capsule leads, against Wi-Fi pickup |
| 22 | Shielded single-core wire (thin coax, headset cable) | ~15 cm | capsule → shield MIC |
| 23 | Foam tape 0.5–1 mm, acoustic cloth or thin foam | — | capsule wrap and a disc behind the grille |

## Printed parts

PLA / PETG, 3 perimeters, 20–30 % infill. Printing notes: [enclosure/README.md](../enclosure/README.md).

| File | Qty |
|---|---|
| `enclosure/stl/case8_top.stl` | 1 |
| `enclosure/stl/case8_bottom.stl` | 1 |
| `enclosure/stl/clamp2.stl` | 1 plate (4 clamps) |
| `enclosure/stl/knob.stl` (⌀30, D-shaft) or `knob_spline.stl` (splined shaft) | 1 |

## Hardware and consumables

| Part | Qty |
|---|---|
| M3 heat-set insert (5–6 mm long, ⌀ ~4.5) | 8 |
| M3 × 10 DIN 912 screw | 4 |
| M3 × 6 screw | 4 |
| Foam double-sided tape, ~1 mm | for the battery |

## Reduced build

`case` enclosure (98.4 × 105.4 mm), no track buttons and no headphone output:

- items 1, 2, 5–8, 11, 13, 14, 17, 18 from the electronics table;
- MX switches and keycaps — 2 each (Shift, Play);
- PJ-392 — 1 (MIDI OUT);
- printed parts — `case_top.stl` and `case_bottom.stl` instead of `case8_*`, clamps and knob the same.

The PCF8575 is needed here too: without it the encoder button, Play and Shift do not work.
