
# Tracker wiring diagram

Everything you solder to the WT32-SC01 Plus: battery power, MIDI OUT, the encoder, Play and Shift, eight track buttons with LEDs, and a headphone jack. The display, touch and microSD are already routed on the board.

Русская версия: [wiring_ru.md](wiring_ru.md)

## Schematic

![Overview: LiPo through IP5306 and the power switch feeds the board 5 V; GPIO10 through 10 Ω to the MIDI jack Tip, 3.3 V through 33 Ω to Ring; encoder on GPIO 11, 12, 13; Play on 14, Shift on 21; PCF8575 on GPIO 43 and 44 of the Debug connector, with eight buttons and eight LEDs.](img/wiring-1.svg)

- red — power (5 V from the IP5306, 3.3 V from the board)
- black (white in dark mode) — GND
- blue — signal to a GPIO
- green — button
- orange — LED cathode

*Every ground symbol is a wire to the board's common GND; every "3.3 V" flag goes to the board's 3V3 pin. All board inputs use internal pull-ups, so the buttons and encoder need no external resistors. The PCF8575 block with the track buttons is optional: the firmware works without it.*

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
| **EC11 encoder and buttons** |                                     |                                                             |
| `board IO11 / IO12`               | `encoder A / B`                     | outer pins of the group of three; turns the wrong way — swap them |
| `board GND`                       | `encoder C`                         | middle pin of the group of three                            |
| `board IO13`                      | `shaft button`                      | the shaft button's other pin goes to GND                    |
| `board IO14`                      | `Play button`                       | other pin to GND                                            |
| `board IO21`                      | `Shift button`                      | other pin to GND                                            |
| **Headphones (optional)** |                                     |                                                             |
| `SPK+ connector`                  | `150 Ω → Tip`                       | left ear                                                    |
| `SPK+ connector`                  | `150 Ω → Ring`                      | right ear, its own resistor                                 |
| `SPK− connector`                  | `Sleeve`                            | **not GND**: the jack is isolated                           |
| `SPK+ / SPK− connector`           | `speaker + (through a switch) / −`  | 4–8 Ω speaker, switch optional                              |
| **Track buttons (optional)** |                                     |                                                             |
| `board 3V3`                       | `PCF VCC + 3.3 V rail`              | the rail feeds all 8 LED resistors                          |
| `board GND`                       | `PCF GND`                           | do not solder A0, A1, A2: the firmware finds the address 0x20–0x27 itself |
| `Debug TXD0 (IO43) / RXD0 (IO44)` | `PCF SDA / SCL`                     | Debug connector; 3V3 and GND can be taken from it too       |
| `—`                               | `PCF INT`                           | leave unconnected: the module is polled every 5 ms          |
| `PCF P00 … P07`                   | `button 1 … 8`                      | the button's other pin goes to GND                          |
| `PCF P10 … P17`                   | `LED 1 … 8 cathode`                 | anode through its own resistor to 3.3 V                     |

10 nF capacitors from the encoder's A and B pins to GND are recommended for less bounce. They are not required: the firmware has a filter.

## Power: important

- The switch sits after the IP5306, so the battery charges through the module's Type-C even with the tracker switched off.
- **Flash firmware only with the switch off.** Otherwise two sources meet on the 5V line: the computer's USB and the IP5306.
- Never connect the battery directly to the board's 5V or 3V3 — only through the IP5306.
- The battery, IP5306 and board negatives form the common ground; everything else in the diagram connects to it as well.

## MIDI jack

- TRS pinout **type A**: Tip = pin 5, Ring = pin 4. For type B devices (some Arturia, Novation) use an A→B adapter or swap the Tip and Ring wires.
- For a 5-pin DIN: the same resistors on pin 5 and pin 4, GND on pin 2.

## Headphones

![Headphones: SPK+ through two 150 Ω resistors to Tip and Ring of the 3.5 mm jack, SPK− to Sleeve. The speaker is connected in parallel, its plus through a switch. Sleeve is not connected to GND.](img/wiring-2.svg)

- purple — SPK+
- teal — SPK− (not ground)

*The built-in NS4168 amplifier is brought out to the board's SPK connector, so no soldering to GPIOs is needed. The signal is mono, the same in both ears. The switch in the speaker wire is optional: without it the speaker and headphones play together.*

**The SPK output is bridged: neither of its two pins is ground.** The jack's Sleeve goes to SPK−, never to the board's GND. The jack must not touch GND, the MIDI jack body or USB. Plug only headphones into this jack: a cable to a mixer, audio interface or powered speakers would tie SPK− to their ground and short the amplifier output.

- Resistors 100–220 Ω, 0.25 W, one each on Tip and Ring. Below 100 Ω it is loud with high current through the headphones; above that it gets quieter. 150 Ω is the middle ground.
- Mount the jack in a plastic enclosure or a plastic wall: the printed case works.
- The jack's built-in switching contact is no good for cutting the speaker: on ordinary jacks it is tied to Tip, and Tip goes through a resistor. You need a separate switch.
- Power up the first time with PROJ → Volume at minimum and without wearing the headphones.

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
2.  If the headphone jack is fitted: Sleeve has no continuity with the board's GND; Tip and Ring read through 150 Ω to SPK+.
3.  A0, A1, A2 on the module can be left alone. If the module is found only sometimes (the address "floats"), bridge all three pads to GND.
4.  Check whether the module has pull-up resistors on SDA and SCL (usually 4.7–10 kΩ to VCC). If not, add 4.7 kΩ from each line to 3.3 V.
5.  Power the module only from 3.3 V, not 5 V: otherwise 5 V reaches the board's GPIOs.
6.  Check that TXD0 and RXD0 are not shorted to each other or to GND. Do not touch GPIO 1, 2, 42: they are used by the built-in RS485.
7.  After flashing, open the serial monitor: the line `trackio: PCF8575 not found` means the module is not responding — check SDA/SCL and power; `trackio: PCF8575 at 0x..` means it was found.
