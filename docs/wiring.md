
# Tracker wiring diagram

Everything you solder to the WT32-SC01 Plus: battery power, MIDI OUT, the encoder, the PCF8575 expander with the encoder button, Play, Shift and eight track buttons with LEDs, and the synth board — a Teensy 4.1 with a PCM5102A DAC and a MAX97220 headphone amplifier. The display and touch are already routed on the board. The microSD card sits in the Teensy (FAT32 or exFAT); the board's own card slot is not used.

Русская версия: [wiring_ru.md](wiring_ru.md)

## Schematic

```
LiPo 103450 + / − ──── IP5306 B+ / B−          (IP5306 Type-C — charging)
IP5306 VOUT ──[switch]── board 5V              (VOUT/GND — pads of the desoldered USB-A)
IP5306 GND ──────────── board GND              (common ground)

WT32-SC01 Plus, expansion connector (Extended IO)
  IO10 ──[10 Ω]──── MIDI Tip     (DIN 5)
  3V3  ──[33 Ω]──── MIDI Ring    (DIN 4)
  GND  ──────────── MIDI Sleeve  (DIN 2, shield)
  IO11 ──────────── encoder A
  IO12 ──────────── encoder B         encoder C (middle) ── GND
  IO13 ──────────── Teensy 0 (RX1)     link, UART TX
  IO14 ──────────── Teensy 1 (TX1)     link, UART RX
  IO21 ──────────── Teensy 2           spare, not used yet

WT32-SC01 Plus, Debug connector
  IO43 TXD0 ─────── PCF SDA
  IO44 RXD0 ─────── PCF SCL
  3V3 ───────────── PCF VCC, 3.3 V rail for the LED resistors
  GND ───────────── PCF GND

PCF8575 (address 0x20–0x27, found automatically; INT not needed, A0–A2 open)
  P00 … P07 ── track button 1 … 8 ── GND
  P13 ──────── encoder shaft button ── GND
  P14 ──────── Play ── GND
  P15 ──────── Shift ── GND
  P1x ──────── LED cathode;  LED anode ──[R]── 3.3 V

Teensy 4.1 (synth board; VUSB–VIN trace on the back cut)
  VIN ── board 5V                          GND ── board GND
  21 (BCLK1) ── DAC BCK                    3V3 ── DAC VIN, DAC XSMT
  20 (LRCLK1) ── DAC LCK                   microSD slot ── the card (FAT32 or exFAT)
  7 (OUT1A) ── DAC DIN

PCM5102A                                 MAX97220
  VIN ── Teensy 3V3 (or 5 V)               VCC ── board 5V (or 3.3V);  SHDN ── VCC
  GND ── star point ── Teensy GND          GND ── star point
  SCK, FMT, FLT, DEMP ── GND (L)           IN L+ ── DAC L;   IN L− ── DAC GND
  XSMT ── 3.3 V (H)                        IN R+ ── DAC R;   IN R− ── DAC GND
  L / R ── line out, to the amplifier      OUT L / OUT R / GND ── jack Tip / Ring / Sleeve
```

*Every "GND" is a wire to the board's common GND; every "3.3 V" goes to the board's 3V3 pin. Encoder A and B use the board's internal pull-ups, and the PCF8575 pins have their own weak pull-ups, so the buttons and encoder need no external resistors. The PCF8575 is required: without it the encoder button, Play, Shift and the track buttons do not work.*

## Connection table

| From                              | To                                  | Notes                                                       |
|-----------------------------------|-------------------------------------|-------------------------------------------------------------|
| **Power** |                                     |                                                             |
| `LiPo + / −`                      | `IP5306 B+ / B−`                    | 103450 battery with a protection board                      |
| `IP5306 VOUT`                     | `switch → board 5V`                 | VOUT and GND are the pads of the desoldered USB-A port      |
| `IP5306 GND`                      | `board GND`                         |                                                             |
| **MIDI OUT** |                                     |                                                             |
| `board IO10`                      | `10 Ω → Tip`                        | MIDI DIN pin 5                                              |
| `board 3V3`                       | `33 Ω → Ring`                       | MIDI DIN pin 4                                              |
| `board GND`                       | `Sleeve`                            | MIDI DIN pin 2, shield                                      |
| **EC11 encoder** |                                     |                                                             |
| `board IO11 / IO12`               | `encoder A / B`                     | outer pins of the group of three; turns the wrong way — swap them |
| `board GND`                       | `encoder C`                         | middle pin of the group of three                            |
| **Teensy 4.1 synth board** |                                     |                                                             |
| `board 5V`                        | `Teensy VIN`                        | after the switch; cut the VUSB–VIN trace on the Teensy's back first |
| `board GND`                       | `Teensy GND`                        |                                                             |
| `board IO13`                      | `Teensy 0 (RX1)`                    | link: 3 Mbaud UART, both sides 3.3 V                        |
| `board IO14`                      | `Teensy 1 (TX1)`                    | link                                                        |
| `board IO21`                      | `Teensy 2`                          | spare, not used by the firmware yet                         |
| **PCM5102A DAC** |                                     |                                                             |
| `Teensy 21 / 20 / 7`              | `DAC BCK / LCK / DIN`               | BCLK1 / LRCLK1 / OUT1A; LCK is also labelled LRCK or WS     |
| `Teensy 3V3 (or board 5V)`        | `DAC VIN`                           | the module has its own LDO                                  |
| `Teensy GND`                      | `DAC GND`                           | star point of the audio ground                              |
| `GND`                             | `DAC SCK, FMT, FLT, DEMP`           | L: no MCLK (PLL from BCK), I2S, normal latency, de-emphasis off |
| `3.3 V`                           | `DAC XSMT`                          | H: unmuted                                                  |
| **MAX97220 headphone amplifier** |                                     |                                                             |
| `DAC L / R`                       | `amp IN L+ / IN R+`                 | names on the silkscreen may differ                          |
| `DAC GND`                         | `amp IN L− / IN R−`                 | differential inputs: "−" to the DAC's ground                |
| `board 5V (or 3V3)`               | `amp VCC`                           | 5V = IP5306 output after the switch                         |
| `DAC GND`                         | `amp GND`                           | to the star point                                           |
| `amp VCC`                         | `amp SHDN`                          | if broken out: VCC = running                                |
| `amp OUT L / OUT R / GND`         | `jack Tip / Ring / Sleeve`          | DirectDrive: no output capacitors, Sleeve to GND is normal  |
| **PCF8575 and buttons (required)** |                                     |                                                             |
| `board 3V3`                       | `PCF VCC + 3.3 V rail`              | the rail feeds all 8 LED resistors                          |
| `board GND`                       | `PCF GND`                           | do not solder A0, A1, A2: the firmware finds the address 0x20–0x27 itself |
| `Debug TXD0 (IO43) / RXD0 (IO44)` | `PCF SDA / SCL`                     | Debug connector; 3V3 and GND can be taken from it too       |
| `—`                               | `PCF INT`                           | leave unconnected: the module is polled every 5 ms          |
| `PCF P00 … P07`                   | `track button 1 … 8`                | the button's other pin goes to GND                          |
| `PCF P13`                         | `encoder shaft button`              | other pin to GND                                            |
| `PCF P14`                         | `Play button`                       | other pin to GND                                            |
| `PCF P15`                         | `Shift button`                      | other pin to GND                                            |
| `PCF P16 / P17`                   | `button A / B`                      | other pin to GND; reserved, not used by the firmware yet    |
| `PCF P10 … P17`                   | `LED 1 … 8 cathode`                 | anode through its own resistor to 3.3 V; P13–P17 are taken by the buttons, so LEDs 4–8 need other pins (`kTrackLedBit`) |

The pins are set in `src/hw/pins.h`. On the author's unit P00 and P10 read low, so track button 1 is on P12, no LEDs are fitted, and P11 is free.

10 nF capacitors from the encoder's A and B pins to GND are recommended for less bounce. They are not required: the firmware has a filter.

## Power: important

- The switch sits after the IP5306, so the battery charges through the module's Type-C even with the tracker switched off.
- **Flash firmware only with the switch off.** Otherwise two sources meet on the 5V line: the computer's USB and the IP5306.
- Never connect the battery directly to the board's 5V or 3V3 — only through the IP5306.
- The battery, IP5306 and board negatives form the common ground; everything else in the diagram connects to it as well.
- **Cut the VUSB–VIN trace** on the back of the Teensy (between the two pads marked VUSB and VIN). Otherwise the computer's USB on the Teensy and the IP5306 feed each other through the 5V line.

## Synth board (Teensy 4.1)

The Teensy runs the internal synth, the microSD card and the sample bank (in its flash); the tracker talks to it over the link (IO13 / IO14). Its USB stays inside the case, so:

- **Flash it once before assembly:** `pio run -e teensy41 -t upload` (Teensy Loader opens; press the button on the Teensy if asked). With the VUSB–VIN trace already cut, USB carries data only: switch the tracker on so the Teensy runs from the board's 5V.
- Later updates go from the tracker: put `teensy.hex` (`.pio/build/teensy41/firmware.hex`) into `/firmware/` on the card and use PROJ → SYS → Update synth, or upload it on the Wi-Fi page (FILE → Wi-Fi firmware..., section "Synth board"). The Teensy checks the image before replacing its firmware.
- The card goes into the Teensy's slot; the case has an opening for it, so it can be changed without opening the case. FAT32 or exFAT.
- Check the link without the rest of the firmware: `wt32-echo` on the board and `teensy41-echo` on the Teensy; the board's serial monitor shows the byte rate and mismatches (0 expected).

## MIDI jack

- TRS pinout **type A**: Tip = pin 5, Ring = pin 4. For type B devices (some Arturia, Novation) use an A→B adapter or swap the Tip and Ring wires.
- For a 5-pin DIN: the same resistors on pin 5 and pin 4, GND on pin 2.

## DAC and headphones

```
Teensy 21 ─── DAC BCK
Teensy 20 ─── DAC LCK
Teensy 7 ──── DAC DIN

DAC L ──────── amp IN L+          amp OUT L ── Tip    (left)
DAC R ──────── amp IN R+          amp OUT R ── Ring   (right)
DAC GND ─┬──── amp IN L−          amp GND ──── Sleeve
         ├──── amp IN R−
         ├──── amp GND
         └──── Teensy GND         (star point at the DAC's GND)

DAC SCK, FMT, FLT, DEMP ── GND     DAC XSMT ── 3.3 V     DAC VIN ── Teensy 3V3 (or 5 V)
amp VCC, amp SHDN ── board 5V (or 3V3)
```

*The board's NS4168 amplifier and the SPK connector (GPIO 35/36/37) are not used. The signal is stereo: 44.1 kHz, 16-bit samples in 32-bit slots (BCK = 64 fs). The Teensy's MCLK (pin 23) is not connected.*

- **DAC settings.** SCK to GND (no MCLK: the PLL takes the clock from BCK), FMT = L (I2S), FLT = L (normal latency), DEMP = L (de-emphasis off), XSMT = H (unmuted). On the purple GY-PCM5102 modules these are solder pads on the back — check your module's silkscreen.
- **Line out.** The DAC's L / R give 2.1 Vrms at full scale, centred on ground. Unlike the old SPK output, they can go to a mixer, audio interface or powered speakers — from the DAC before the amplifier, or from the amplifier's headphone output.
- **Amplifier inputs** are differential: L+ / R+ to the DAC's L / R, L− / R− to the DAC's GND (AGND). Pin names on the module's silkscreen may differ.
- **DirectDrive outputs** are ground-referenced, without coupling capacitors: the headphone jack's Sleeve goes to GND, and the jack may touch the case.
- **Level.** 2.1 Vrms from the DAC is more than the MAX97220 can deliver: if it clips or is too loud, lower MAIN (MIX tab) or add a divider / potentiometer between the DAC and the amplifier.
- **Ground.** Short audio ground wires and one star point at the DAC's GND: the DAC, amplifier and jack grounds meet there, and a single wire runs to the Teensy's GND. Otherwise the IP5306 boost converter may whine in the headphones.
- Power up the first time with MAIN (MIX tab) at minimum and without wearing the headphones.

## Which track button is which

![Button layout: Shift, Play and the encoder under the screen; below them buttons 1–4, and 5–8 at the front edge. The bottom view is mirrored: button 1 is on the right.](img/wiring-3.svg)

*Button number = track number = pin number + 1 (button 1 is P00 and P10). While soldering, the lid lies face down, so the left-to-right order is reversed: button 1 ends up on the right.*

## LED polarity

![LED: the long leg is the anode, to the resistor and 3.3 V; the short leg on the flat side is the cathode, to pin P1x.](img/wiring-4.svg)

*The LED goes into the window of the MX switch housing, with the legs passing straight through. A reversed LED will not burn out, it just won't light.*

## LED resistors

| LED colour                 | Drop          | Resistor   | Current    |
|----------------------------|---------------|------------|------------|
| red, yellow, orange        | `≈ 2.0 V`     | `330 Ω`    | `≈ 4 mA`   |
| green (yellow-green)       | `≈ 2.1 V`     | `330 Ω`    | `≈ 3.5 mA` |
| white, blue, bright green  | `≈ 2.9–3.1 V` | `47–68 Ω`  | `≈ 3–8 mA` |

With white and blue LEDs at 3.3 V the voltage headroom is only 0.2–0.4 V, and brightness varies noticeably from one LED to the next. Red or yellow with 330 Ω are more reliable. Do not go below 47 Ω: the PCF8575 handles up to 25 mA per pin, but all 8 LEDs together should not draw more than ~80 mA.

## Before powering up

1.  Continuity check: the board's 5V and GND are not shorted; the switch in the "off" position breaks VOUT.
2.  Teensy: the VUSB–VIN trace is cut (no continuity between the pads); VIN goes to the board's 5V, GND to GND; GPIO 13, 14, 21 go to Teensy pins 0, 1, 2 and are not shorted to each other, to GND or to 3.3 V. DAC: XSMT reads 3.3 V; SCK, FMT, FLT, DEMP read GND; BCK / LCK / DIN go to Teensy 21 / 20 / 7. Amplifier: IN L− and IN R− ring to the DAC's GND, SHDN (if broken out) to VCC.
3.  A0, A1, A2 on the module can be left alone. If the module is found only sometimes (the address "floats"), bridge all three pads to GND.
4.  Check whether the module has pull-up resistors on SDA and SCL (usually 4.7–10 kΩ to VCC). If not, add 4.7 kΩ from each line to 3.3 V.
5.  Power the module only from 3.3 V, not 5 V: otherwise 5 V reaches the board's GPIOs.
6.  Check that TXD0 and RXD0 are not shorted to each other or to GND. Do not touch GPIO 1, 2, 42: they are used by the built-in RS485.
7.  After flashing, open the serial monitor: the line `trackio: PCF8575 not found` means the module is not responding — check SDA/SCL and power (without the module the encoder button, Play and Shift do not work either); `trackio: PCF8575 at 0x..` means it was found. The status bar shows `NO SYNTH` while the Teensy does not answer: check its power and the link wires (IO13 → pin 0, IO14 → pin 1).
