# Tracker wiring diagram

Everything you solder to the WT32-SC01 Plus: battery power, an INA226 battery monitor, MIDI OUT, the encoder, the PCF8575 expander with the encoder button, Play, Shift, A, B and eight track buttons, and the synth board, a Teensy 4.1. Sound comes from a Teensy Audio Shield Rev D (SGTL5000), soldered to the Teensy on wires. The display and touch are already routed on the board. The microSD card sits in the Teensy (FAT32 or exFAT); the WT32's own card slot is not used.

Русская версия: [wiring_ru.md](wiring_ru.md)

## Architecture

The device is two boards connected by a UART link:

- **WT32-SC01 Plus (ESP32-S3)** — the tracker: sequencer, UI (display, touch, encoder, buttons), MIDI OUT, Wi-Fi firmware page.
- **Teensy 4.1 — the synth board**: the synthesizer engine (`mt::Synth` from `lib/core` inside a Teensy Audio Library `AudioStream`, 44.1 kHz stereo, 32 voices, up to 16 of them "heavy" — FM / DRUM / SYNTH WT), the only microSD card (SdFat, FAT32 or exFAT), the sample store on that card (samples stream from it), the bank in its program flash (wavetables, sample starts), optional 8 MB PSRAM on the CS0 pad (sample cache, PERF buffer; without it the PERF buffer effects are off), the Teensy Audio Shield Rev D (SGTL5000, headphones).
- **Link**: ESP `UART_NUM_2` (GPIO TX 13 / RX 14) ↔ Teensy `Serial1` (RX1 0 / TX1 1), 3 Mbaud, COBS frames with CRC16, a protocol version (`kProtocol` in `lib/core/src/link_msg.h`). Synth events carry a timestamp and are played 4 ms after they are sent; the sound state is mirrored by diffing a `SynthModel` (the base struct of `Project`); files go through a remote `fs::FS` served by the Teensy.

The status bar shows the synth board's state in the CPU field: `NO SYNTH` (no answer: the sequencer and MIDI OUT work, files and internal sound do not), `SYNTH FW` (protocol version mismatch — update the synth board), `SYNC` (sending the sound state), otherwise `CPU NN%` of the synth board. FILE → SYSTEM: Synth fw (version), Update synth (from `/firmware/teensy.hex` on the card), Card, Link (counters), CPU profile.

## Diagram

![Tracker wiring: LiPo through the INA226, IP5306 and the switch to the 5 V bus; WT32-SC01 Plus with MIDI OUT, the encoder and the link to the Teensy 4.1; the PCF8575 with the buttons on the Debug connector; the Audio Shield on wires to the Teensy.](img/wiring-1.svg)

*Solid lines: solder now. Dashed: optional — the IO21 wire (spare) and the PSRAM on the Teensy.*

## Build order

Each step can be checked before the next one, which makes mistakes easy to find.

1.  **Power.** LiPo → INA226 (IN+ / IN−) → IP5306 → switch → board 5V. Until the INA226 is in, wire LiPo + straight to IP5306 B+. Check: the switch turns the board on and the screen lights up.
2.  **Flash the WT32** over USB with the switch off: `pio run -e wt32 -t upload`.
3.  **PCF8575 and buttons** on the Debug connector. Check: the serial monitor prints `trackio: PCF8575 at 0x..` and the buttons respond.
4.  **Encoder** (IO11, IO12, GND) and **MIDI OUT** (IO10, 3V3, GND through the resistors). Check: the encoder changes values, a MIDI device hears notes.
5.  **INA226** in the battery's positive wire, SDA / SCL in parallel with the PCF8575. Check: the status bar shows the charge percentage.
6.  **Teensy 4.1.** Flash it over USB before assembly (see "Synth board"), cut the VUSB–VIN trace, solder VIN, GND and the link (IO13 → 0, IO14 → 1). Insert the card. Check: `NO SYNTH` is gone, FILE shows the card's files, samples import. No sound yet — that is expected.
7.  **PSRAM** (optional): an 8 MB chip on the first pad under the Teensy (CS0, the one PJRC's startup code probes). Without it samples play straight from the card, but the PERF buffer is silent.
8.  **Audio Shield Rev D** — eight wires to the Teensy (see "Audio Shield"). After that you get sound in the headphones.

## Schematic

```
LiPo 103450 + ── INA226 IN+ ─[shunt]─ INA226 IN− ── IP5306 B+   (IP5306 Type-C — charging)
LiPo 103450 − ──────────────────────────────────── IP5306 B−
IP5306 VOUT ──[switch]── 5V bus                 (VOUT/GND — pads of the desoldered USB-A)
IP5306 GND ──────────── GND bus                 (common ground)
5V bus ── WT32 5V, Teensy VIN                   GND bus ── WT32 GND, Teensy GND

WT32-SC01 Plus, expansion connector (Extended IO)
  IO10 ──[10 Ω]──── MIDI Tip     (DIN 5)
  3V3  ──[33 Ω]──── MIDI Ring    (DIN 4)
  GND  ──────────── MIDI Sleeve  (DIN 2, shield)
  IO11 ──────────── encoder A
  IO12 ──────────── encoder B         encoder C (middle) ── GND
  IO13 ──────────── Teensy 0 (RX1)     link, UART TX
  IO14 ──────────── Teensy 1 (TX1)     link, UART RX
  IO21 ──────────── Teensy 2           spare, not used yet (may be left out)

WT32-SC01 Plus, Debug connector
  IO43 TXD0 ─────── PCF SDA
  IO44 RXD0 ─────── PCF SCL
  3V3 ───────────── PCF VCC
  GND ───────────── PCF GND
  (INA226 SDA / SCL / VCC / GND — in parallel with the PCF, on the same wires)

INA226 (address 0x40–0x4F, found automatically; A0, A1 as on the module; ALE not needed)
  IN+ ── LiPo +          IN− ── IP5306 B+
  VBS ── IN+             (if present: battery voltage)
  VCC ── 3.3 V           GND ── GND

PCF8575 (address 0x20–0x27, found automatically; INT not needed, A0–A2 left open)
  P00 … P07 ── track button 1 … 8 ── GND
  P13 ──────── encoder shaft button ── GND
  P14 ──────── Play ── GND
  P15 ──────── Shift ── GND
  P16 ──────── B ── GND
  P17 ──────── A ── GND

Teensy 4.1 (synth board; VUSB–VIN trace on the back cut)
  VIN ── 5V bus                            GND ── GND bus
  0 ── IO13,  1 ── IO14,  2 ── IO21        microSD slot ── card (FAT32 / exFAT)
  underside: PSRAM 8 MB on the 1st pad, CS0 (optional)

Audio Shield Rev D (on wires next to the Teensy, not stacked)
  Teensy 7  (OUT1A)  ── shield 7  (TX, DIN)      Teensy 18 (SDA) ── shield 18
  Teensy 20 (LRCLK1) ── shield 20 (LRCLK)        Teensy 19 (SCL) ── shield 19
  Teensy 21 (BCLK1)  ── shield 21 (BCLK)         Teensy 3V3 ──── shield 3V3
  Teensy 23 (MCLK1)  ── shield 23 (MCLK)         Teensy GND ──── shield GND
  Teensy 8  (IN1)    ── shield 8  (RX, DOUT)   input: recording from MIC / LINE IN
  the shield's microSD slot, the pot pad — not needed
  the shield's headphone jack goes to the front panel
  shield MIC / GND ── 9767 capsule (shielded wire, 33–100 pF at the capsule)
  shield LINE IN L / R / GND ── LINE IN jack (PJ-392, front panel)
```

*Every "GND" is a wire to the common GND, every "3.3 V" goes to the WT32's 3V3 pin (for the shield — to the Teensy's 3V3). Encoder inputs A and B use the board's internal pull-ups, and the PCF8575 pins have their own weak pull-ups, so the buttons and encoder need no external resistors. The PCF8575 is required: without it the encoder button, Play, Shift, A, B and the track buttons do not work.*

## Connection table

| From                              | To                                  | Note                                                        |
|-----------------------------------|-------------------------------------|-------------------------------------------------------------|
| **Power** |                                     |                                                             |
| `LiPo + / −`                      | `IP5306 B+ / B−`                    | 103450 battery with protection board; "+" through the INA226 |
| `IP5306 VOUT`                     | `switch → 5V bus`                   | VOUT and GND are the pads of the module's desoldered USB-A  |
| `IP5306 GND`                      | `GND bus`                           |                                                             |
| `5V bus / GND`                    | `WT32 5V / GND`                     |                                                             |
| **INA226 battery monitor** |                                     |                                                             |
| `LiPo +`                          | `INA IN+`                           | the shunt sits in the battery's positive wire               |
| `INA IN−`                         | `IP5306 B+`                         | current "+" is discharge, "−" is charging                   |
| `INA IN+`                         | `INA VBS`                           | if the pin exists; measures the battery voltage             |
| `WT32 3V3`                        | `INA VCC`                           | 3.3 V only, not 5 V                                         |
| `WT32 GND`                        | `INA GND`                           | common ground, which is also the battery negative           |
| `Debug TXD0 (IO43) / RXD0 (IO44)` | `INA SDA / SCL`                     | in parallel with the PCF8575, the addresses don't overlap   |
| —                                 | `INA A0, A1`                        | leave as on the module: the firmware finds 0x40–0x4F itself |
| `—`                               | `INA ALE`                           | leave unconnected                                           |
| **MIDI OUT** |                                     |                                                             |
| `WT32 IO10`                       | `10 Ω → Tip`                        | MIDI DIN pin 5                                              |
| `WT32 3V3`                        | `33 Ω → Ring`                       | MIDI DIN pin 4                                              |
| `WT32 GND`                        | `Sleeve`                            | MIDI DIN pin 2, shield                                      |
| **EC11 encoder** |                                     |                                                             |
| `WT32 IO11 / IO12`                | `encoder A / B`                     | the outer pins of the three; turns backwards — swap them    |
| `WT32 GND`                        | `encoder C`                         | the middle pin of the three                                 |
| **PCF8575 and buttons (required)** |                                     |                                                             |
| `WT32 3V3`                        | `PCF VCC`                           | 3.3 V only, not 5 V                                         |
| `WT32 GND`                        | `PCF GND`                           | leave A0, A1, A2 open: the firmware finds address 0x20–0x27 |
| `Debug TXD0 (IO43) / RXD0 (IO44)` | `PCF SDA / SCL`                     | Debug connector; 3V3 and GND can come from it too           |
| `—`                               | `PCF INT`                           | leave unconnected: the module is polled every 5 ms          |
| `PCF P00 … P07`                   | `track button 1 … 8`                | the button's other pin goes to GND                          |
| `PCF P13`                         | `encoder shaft button`              | other pin to GND                                            |
| `PCF P14`                         | `Play button`                       | other pin to GND                                            |
| `PCF P15`                         | `Shift button`                      | other pin to GND                                            |
| `PCF P16 / P17`                   | `B / A button`                      | other pin to GND                                            |
| **Teensy 4.1 synth board** |                                     |                                                             |
| `5V bus`                          | `Teensy VIN`                        | after the switch; first cut the VUSB–VIN trace on the back of the Teensy |
| `GND bus`                         | `Teensy GND`                        |                                                             |
| `WT32 IO13`                       | `Teensy 0 (RX1)`                    | link: UART 3 Mbaud, 3.3 V on both sides                     |
| `WT32 IO14`                       | `Teensy 1 (TX1)`                    | link                                                        |
| `WT32 IO21`                       | `Teensy 2`                          | spare, not used by the firmware yet; may be left out        |
| `microSD card`                    | `Teensy slot`                       | FAT32 or exFAT, MBR partitioning (GPT is not read)          |
| `PSRAM 8 MB`                      | `Teensy underside, 1st pad (CS0)`   | optional: sample cache and the PERF buffer                  |
| **Audio Shield Rev D**         |                                     |                                                             |
| `Teensy 7 / 20 / 21 / 23`         | `shield 7 / 20 / 21 / 23`           | I2S: data / LRCLK / BCLK / MCLK; MCLK is required           |
| `Teensy 18 / 19`                  | `shield 18 / 19`                    | I2C control of the SGTL5000 (address 0x0A)                  |
| `Teensy 3V3 / GND`                | `shield 3V3 / GND`                  | from the Teensy's regulator, not from the 5V bus            |
| `Teensy 8`                        | `shield 8`                          | I2S input (codec ADC): recording from MIC / LINE IN         |
| `9767 capsule (+ / −)`            | `shield MIC / GND`                  | shielded wire, shield to GND at the Audio Shield end        |
| `LINE IN tip / ring / sleeve`     | `shield LINE IN L / R / GND`        | PJ-392 jack in the front panel                              |
| `—`                               | `the shield's microSD slot`         | leave unconnected: the shield's card is not used            |

The pins are set in `src/hw/pins.h`.

## Encoder

A regular mechanical EC11 with a push button (4 pulses per detent). The middle pin of the A–C–B row goes to GND, the outer ones to GPIO 11 and 12. If the value moves the wrong way when turning, swap A and B. A and B use the internal pull-ups (`INPUT_PULLUP`), no external resistors needed.

10 nF capacitors from the encoder's A and B pins to GND are recommended for less bounce. They are not required: the firmware has the PCNT hardware filter and a 5 ms software debounce for the buttons.

## PCF8575 and buttons

The encoder button, Play, Shift, A, B and the 8 track buttons are on a PCF8575 expander on I2C (port 0; port 1 is used by the touch panel). Its pins have their own weak pull-ups; a button shorts its pin to GND. If the module does not respond at startup, the firmware still runs, but only the touchscreen and encoder rotation work.

- The pin mapping is set in `src/hw/pins.h` (`kTrackBtnBit`, `kEncSwBit`, `kPlayBit`, `kShiftBit`, `kBtnABit`, `kBtnBBit`): if a pin is damaged, a button can be moved to a free pin (number 0–15 = P00–P07, P10–P17).
- 4.7 kΩ SDA/SCL pull-ups to 3.3 V are usually already on the module — check.
- INT is not needed: the firmware polls the module every 5 ms. The detected address is printed to the USB log (`trackio: PCF8575 at 0x..`).
- If the A0–A2 pads are not bridged anywhere and the module has no resistors to GND, the address floats and may "wander" — in that case bridge the pads to GND.
- GPIO 43/44 are free because the log and flashing go through the native USB (`ARDUINO_USB_CDC_ON_BOOT=1`). At startup the ESP32 bootloader prints to TXD0 (GPIO 43) — this does not bother the module.
- Safe boot (hold Shift at power-on) reads Shift from the expander before the autoload.

## Power: important

- The switch sits after the IP5306, so the battery charges through the module's Type-C even with the tracker switched off.
- **Flash firmware only with the switch off.** Otherwise two sources meet on the 5V line: the computer's USB and the IP5306.
- Never connect the battery directly to the board's 5V or 3V3 — only through the IP5306.
- The battery, IP5306, WT32 and Teensy negatives form the common ground; everything else in the diagram connects to it as well.
- **Cut the VUSB–VIN trace** on the back of the Teensy (between the two pads marked VUSB and VIN). Otherwise the computer's USB on the Teensy and the IP5306 feed each other through the 5V line.

## Synth board (Teensy 4.1)

The Teensy runs the built-in synth, the microSD card and the sample bank; the tracker talks to it over the link (IO13 / IO14). Its USB stays inside the case, so:

- **Flash it once before assembly:** `pio run -e teensy41 -t upload` (Teensy Loader opens; press the button on the Teensy if it asks). If the VUSB–VIN trace is already cut, USB carries data only: switch the tracker on so the Teensy is powered from the 5V bus.
- Later updates go through the tracker: put `teensy.hex` (`.pio/build/teensy41/firmware.hex`) into `/firmware/` on the card and choose FILE → SYSTEM → Update synth, or upload it on the Wi-Fi page (FILE → Wi-Fi firmware..., "Synth board" section). The Teensy checks the image before replacing its firmware.
- The WT32 and Teensy firmware must come from the same commit: the link carries a protocol version, and on a mismatch `SYNTH FW` lights up in place of CPU and the **UPDATE** button offers to reflash the Teensy from the card.
- The card goes into the Teensy's slot; the case has a slot for it, so you can swap it without opening the case. FAT32 or exFAT with MBR partitioning — that is how cards come from the shop; GPT (which macOS Disk Utility may create) is not read.
- **PSRAM** is an APS6404L chip (8 MB) under the Teensy, on the **first** pad (CS0) — the standard PJRC layout; the Teensy startup code finds it and maps it. A chip on the second pad (CS1) alone is not used. The firmware tests it at boot and copies the project's samples into it; it also holds the PERF buffer (6 s). Without PSRAM samples are read from the card on the fly — it works, it just depends more on the card; the buffer-based PERF effects are silent. The Teensy log prints `psram off: ...` when the chip is missing or fails the test.
- **Without the Audio Shield** the synth runs, but there is no sound: the firmware doesn't find the SGTL5000 and sends plain I2S to pins 7 / 20 / 21. Import, the bank, the link and the tests can all be checked anyway.
- To test the link without the rest of the firmware: flash `wt32-echo` to the board and `teensy41-echo` to the Teensy; the board's serial monitor shows the speed and errors (0 expected).
- If the Teensy hangs (for example on a failing card), the watchdog restarts it after 8 s; the tracker picks it up again by itself.

## INA226 battery monitor

The INA226 sits in the battery's positive wire, before the IP5306, and reports over I2C the battery voltage and the signed current through it: "+" is discharge, "−" is charging from Type-C. The switch is after the IP5306, so the shunt is always in the circuit, while the chip itself runs from the board's 3.3 V and is unpowered when the tracker is off. It does not drain the battery.

- **Shunt.** Most modules carry an R100 (0.1 Ω): ±0.8 A range. The tracker draws ~0.3 A from the battery — enough. The IP5306 charges at up to ~2 A: the reading will clip at 0.8 A, but the sign (charging) is still visible. The shunt then dissipates up to ~0.4 W — it should be a 2512 package on the module. To measure the charge current too, replace the shunt with an R010 (0.01 Ω, ±8 A).
- **IN+ and IN− wires** — same gauge as the battery wire: the full current, including charging, flows through them.
- **VBS.** If the module has a VBS (or VBUS) pin, connect it to IN+ so the voltage is measured right at the battery. If not, the module measures at IN− — off by the shunt drop, which doesn't matter for the charge percentage.
- **Address 0x40–0x4F.** Set by A0 and A1; modules ship with them strapped in any state (often 0x40 or 0x44). The firmware finds the INA226 itself by its manufacturer ID; the boot log says `battery: INA226 at 0x44`. The PCF8575 is at 0x20–0x27, no overlap.
- **I2C pull-ups.** The module has its own 10 kΩ on SDA and SCL; together with the PCF8575's pull-ups this is fine.

## MIDI jack

- TRS pinout **type A**: Tip = pin 5, Ring = pin 4. For type B devices (some Arturia, Novation) use an A→B adapter or swap the Tip and Ring wires.
- For a 5-pin DIN: the same resistors on pin 5 and pin 4, GND on pin 2.
- Non-inverted UART, 31250 baud, 3.3 V. Optionally, a 74HC14 buffer (two inverters in series) can be placed between GPIO10 and the 10 Ω resistor.
- There is no MIDI input: the device is always the master and sends clock, Start/Stop/Continue.

## Audio Shield Rev D

The shield sits next to the Teensy on wires (in its own slots in the case), with its headphone jack poking out of the front panel next to the card slot. The firmware picks up the shield and the headphone volume by itself: at boot it looks for the SGTL5000 on I2C (address 0x0A), and FILE → SYSTEM → Phones sets the volume.

```
Teensy 7  ─── shield 7   (I2S data, OUT1A)
Teensy 20 ─── shield 20  (LRCLK)
Teensy 21 ─── shield 21  (BCLK)
Teensy 23 ─── shield 23  (MCLK)
Teensy 18 ─── shield 18  (SDA)
Teensy 19 ─── shield 19  (SCL)
Teensy 3V3 ── shield 3V3
Teensy GND ── shield GND
```

- The shield's pin labels are Teensy pin numbers: solder number to number.
- Keep the wires short (under 10 cm), MCLK and BCLK away from the IO13 / IO14 link. Run a separate ground wire next to the I2S lines.
- Pin 8 (I2S input) carries MIC / LINE IN for recording; solder it with the rest. The firmware reads it in the REC window (INST → SAMPLE → Record…): without the 8 → 8 wire MIC and LINE record silence, RESAMPLE (the tracker's own output) still works. Leave the shield's microSD slot (pins 10–13) and the pot pad unconnected: the card lives only in the Teensy, don't give pins 10–13 to the shield.
- The shield's headphone output uses a virtual ground (HP_VGND): do **not** tie the jack's Sleeve to GND or to the case; the shield's own jack is already wired that way.
- The shield's line out (LINE OUT L / R) can go to a second jack — same signal, not affected by Phones.
- First power-up with Phones and MAIN (MIX tab) at minimum and without the headphones on.

### Microphone and line input

For recording samples. Both inputs go to the SGTL5000 ADC and to the Teensy on pin 8; only one works at a time — the Source row of the REC window selects it (see the manual, "Recording a sample"). The capsule and the jack sit on tabs of the case bottom (MIC on the back between MIDI and the power switch, on one tab with the switch; LINE IN on the front to the right of PHONES, on one tab with it), so they are soldered before the top goes on.

```
9767 capsule  + ─── shield MIC      (inner conductor of a shielded wire)
              − ─── shield GND      (the lead tied to the capsule can; shield to GND at the Audio Shield end only)
              33–100 pF across the leads — right at the capsule
PJ-392 LINE IN  Tip ── shield LINE IN L    Ring ── LINE IN R    Sleeve ── GND
```

- **Capsule** — electret, 2 leads, ⌀9.7 mm (generic "9767", Kingstate KECG2742, PUI POM-2244P). The shield powers it (MIC_BIAS through a resistor on the board), no amplifier module needed. The minus is the lead connected by a trace to the metal can.
- **MIC wire** — shielded (thin coax, a headset cable), about 13 cm to the shield, away from the IP5306 inductor and the button wires. The signal is in millivolts and amplified up to +40 dB — an unshielded wire picks up hum and whine.
- **33–100 pF capacitor** across the capsule leads kills RF from the WT32 Wi-Fi: without it the transistor inside the capsule detects it and the recording buzzes.
- **Fitting** — wrap the capsule in foam tape (0.75 mm per side) and push it into the cup to the stop; put a disc of acoustic cloth or thin foam between the capsule and the grille. The foam decouples the capsule from the case, otherwise button clicks get into the recording.
- **LINE IN** — PJ-392 jack with an M6 nut in the tab; level up to ~1 V (line out, phone). Labels tell LINE IN from PHONES.

## Built-in peripherals (for reference)

| Block | Pins |
|---|---|
| microSD (SPI) | CS 41, MOSI 40, CLK 39, MISO 38 — not used (the card is in the Teensy), reserve |
| FT6336U touch (I2C) | SDA 6, SCL 5, INT 7, RST 4 (shared with LCD) |
| ST7796 display | 8080 bus, 8-bit (internal board routing) |

GPIO 1, 2, 42 go to the built-in RS485 transceiver; only the A/B lines reach the connector. GPIO 35, 36, 37 go to the built-in NS4168 amplifier and the SPK connector; they are not used: the speaker is removed and sound comes from the synth board's Audio Shield; GPIO 13 / 14 / 21 carry the link to the Teensy. GPIO 43/44 (Debug connector) are used by the PCF8575 and the INA226.

## Before powering up

1.  Continuity check: the bus 5V and GND are not shorted; the switch in the "off" position breaks VOUT.
2.  Teensy: the VUSB–VIN trace is cut (the pads don't beep); VIN goes to the 5V bus, GND to GND; IO13, IO14 (and IO21, if soldered) go to Teensy pins 0, 1, 2 and are not shorted to each other, to GND or to 3.3 V.
3.  PCF8575: A0, A1, A2 can be left as they are. If the module is found only sometimes (the address "floats"), bridge all three pads to GND.
4.  Check whether the PCF8575 has SDA and SCL pull-up resistors (usually 4.7–10 kΩ to VCC). If not and there is no INA226 either, add 4.7 kΩ from each line to 3.3 V.
5.  Power the PCF8575 and the INA226 from 3.3 V only, not 5 V: otherwise 5 V reaches the board's GPIOs.
6.  Check that TXD0 and RXD0 are not shorted to each other or to GND. Don't touch GPIO 1, 2, 42: they are used by the built-in RS485.
7.  INA226: IN+ beeps to the battery positive, IN− to IP5306 B+; between IN+ and IN− you read the shunt resistance (a fraction of an ohm), not an open circuit.
8.  Shield (when you have it): the shield's 3V3 goes to the Teensy's 3V3, not to the 5V bus; pins 7, 20, 21, 23, 18, 19 are not shorted to each other or to GND.
9.  After flashing, open the serial monitor: `trackio: PCF8575 not found` means the module doesn't answer — check SDA/SCL and power (without it the encoder button, Play, Shift, A and B don't work either); `trackio: PCF8575 at 0x..` means it was found. The status bar shows `NO SYNTH` while the Teensy doesn't answer: check its power and the link wires (IO13 → pin 0, IO14 → pin 1). `SYNTH FW` means the WT32 and Teensy firmware versions differ.
