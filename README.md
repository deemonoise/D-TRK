# D-TRK

A 16-track tracker with a built-in synthesizer: the **WT32-SC01 Plus** (ESP32-S3, 480×320 touchscreen) plus a **Teensy 4.1** synth board.
16 patterns, song mode, Fill (every Nth / Euclid / random) for notes, volume and FX, saving to microSD, MIDI file import.
Built-in sound: FM, 808 / 909 drum machines, the SYNTH synthesizer (saw / square / triangle and wavetable), a sampler and chiptune, in stereo at 44.1 kHz through a PCM5102A DAC and a headphone amplifier — each track plays either the internal synth or external MIDI.

User manual: [docs/manual.md](docs/manual.md). Русская версия: [README_ru.md](README_ru.md).
3D-printable enclosure and battery power circuit: [enclosure/](enclosure/README.md).

## Architecture

The device is two boards connected by a UART link:

- **WT32-SC01 Plus (ESP32-S3)** — the tracker: sequencer, UI (display, touch, encoder, buttons), MIDI OUT, Wi-Fi firmware page.
- **Teensy 4.1 — the synth board**: the synthesizer engine (`mt::Synth` from `lib/core` inside a Teensy Audio Library `AudioStream`, 44.1 kHz stereo, 32 voices, up to 16 of them "heavy" — FM / DRUM / SYNTH WT), the only microSD card (SdFat, FAT32 or exFAT), the sample bank in its program flash, the PCM5102A DAC and the MAX97220 headphone amplifier.
- **Link**: ESP `UART_NUM_2` (GPIO TX 13 / RX 14) ↔ Teensy `Serial1` (RX1 0 / TX1 1), 3 Mbaud, COBS frames with CRC16, protocol version 1. Synth events carry a timestamp and are played 4 ms after they are sent; the sound state is mirrored by diffing a `SynthModel` (the base struct of `Project`); files go through a remote `fs::FS` served by the Teensy.

The status bar shows the synth board's state in the CPU field: `NO SYNTH` (no answer: the sequencer and MIDI OUT work, files and internal sound do not), `SYNTH FW` (protocol version mismatch — update the synth board), `SYNC` (sending the sound state), otherwise `CPU NN%` of the synth board. PROJ → SYS: Synth fw (version), Link (counters), Card, CPU profile, Update synth (from `/firmware/teensy.hex` on the card).

## Wiring

The built-in display and touch panel are already routed on the board; the board's own microSD slot is not used (the card is in the Teensy). The encoder, the MIDI output and the link to the Teensy go to the board's expansion connector (Extended IO); the PCF8575 expander with all the buttons goes to the Debug connector. The DAC and the headphone amplifier are on the Teensy. Full soldering diagram (power, MIDI, buttons, link, DAC, headphones): [docs/wiring.md](docs/wiring.md).

| GPIO | What to connect | How |
|---|---|---|
| 10 | MIDI OUT | through a 10 Ω resistor to the Tip of the TRS jack (see diagram below) |
| 11 | Encoder, pin A | the other end — the encoder's common pin to GND |
| 12 | Encoder, pin B | |
| 13 | Link TX | to the Teensy's pin 0 (RX1), see [Synth board](#synth-board-teensy-41) |
| 14 | Link RX | to the Teensy's pin 1 (TX1) |
| 21 | spare | to the Teensy's pin 2 (reserved, unused) |
| 43 / 44 | SDA / SCL of the PCF8575 module: encoder button, **Play**, **Shift**, track buttons | Debug connector (TXD0 / RXD0), **required**, see below |
| 3.3V | MIDI OUT | through a 33 Ω resistor to the Ring of the TRS jack |
| GND | Common | encoder common pin, the other pins of the buttons, Sleeve of the TRS jack, Teensy GND |

Encoder A and B use the internal pull-up to 3.3 V (`INPUT_PULLUP`), so no external resistors are needed. All buttons are on the PCF8575 expander, whose pins have their own weak pull-ups; a button shorts its pin to GND.

### Encoder

A regular mechanical EC11 with a push button (4 pulses per detent). The middle pin of the A–C–B row goes to GND, the outer ones to GPIO 11 and 12. If the value moves the wrong way when turning, swap A and B.

It is a good idea to add 10 nF capacitors from A and B to GND — they reduce contact bounce. The firmware has the PCNT hardware filter and a 5 ms software debounce for the buttons.

### Buttons on the PCF8575 (required)

The encoder button, Play, Shift and 8 MX track buttons with 8 3 mm LEDs in the switch windows are connected via a PCF8575 expander on I2C (port 0; port 1 is used by the touch panel). There are 16 tracks; the track buttons work on the visible half (1–8 or 9–16, see below). If the module does not respond at startup, the firmware still runs, but only the touchscreen and encoder rotation work: the encoder button, Play, Shift and the track buttons do nothing.

| PCF8575 | Connection |
|---|---|
| SDA / SCL | GPIO 43 / 44 (TXD0 / RXD0 of the Debug connector) |
| VCC / GND | 3.3 V / GND (from the same connector) |
| INT | leave unconnected |
| A0–A2 | any: the firmware looks for the module at 0x20–0x27 |
| P00–P07 | track buttons 1–8, other pin to GND |
| P13 | encoder button (the shaft button), other pin to GND |
| P14 | **Play** button, other pin to GND |
| P15 | **Shift** button, other pin to GND |
| P16 / P17 | buttons **A** / **B** (under Shift / Play), other pin to GND; reserved, the firmware does not use them yet |
| P10–P17 | LED 1–8 cathodes, except P13–P17 (taken by the buttons above) |

The pin mapping is set in `src/hw/pins.h` (`kTrackBtnBit`, `kTrackLedBit`, `kEncSwBit`, `kPlayBit`, `kShiftBit`): if a pin is damaged, a button can be moved to the pin of an unfitted LED (number 8–15 = P10–P17), and that LED marked as `0xFF`. Since P13–P17 now carry the encoder button, Play, Shift, A and B, LEDs 4–8 need other free pins or stay unfitted (`0xFF`). On the author's unit P00 and P10 read low, so button 1 lives on P12, no LEDs are fitted, and P11 is free.

Safe boot (hold Shift at power-on) works as before: the firmware reads Shift from the expander before the autoload.

```
+3.3V ──[330 Ω]──|>|── P1x     (LED is lit when P1x = 0)
```

- 330 Ω — for red/yellow/green LEDs (≈ 4 mA). White and blue ones don't have enough headroom at 3.3 V: use 47–68 Ω, and brightness will vary from unit to unit.
- The PCF8575 pulls a pin down (up to 25 mA) but up only with a weak pull-up, so the LEDs are switched only on the cathode, as in the diagram.
- 4.7 kΩ SDA/SCL pull-ups to 3.3 V are usually already on the module — check.
- INT is not needed: the firmware polls the module every 5 ms. The detected address is printed to the USB log (`trackio: PCF8575 at 0x..`).
- If the A0–A2 pads are not bridged anywhere and the module has no resistors to GND, the address floats and may "wander" — in that case bridge the pads to GND.
- GPIO 43/44 are free because the log and flashing go through the native USB (`ARDUINO_USB_CDC_ON_BOOT=1`). At startup the ESP32 bootloader prints to TXD0 (GPIO 43) — this does not bother the module.
- The buttons and LEDs work on the visible half of the tracks — 1–8 or 9–16; the half is chosen by the cursor (move past track 8 and the second half is shown; the GRID header shows `1-8` / `9-16`). Button N selects track N of that half, Shift + N mutes the same track (the toast shows the real number, 1–16), and in GRID edit mode the buttons enter scale degrees. LED N is the same track: lit for the selected one, flashes on notes; activity in the other half is not shown. Details in the [manual](docs/manual.md#trackkeys).

### Synth board (Teensy 4.1)

```
WT32-SC01 Plus (Extended IO)        Teensy 4.1
  GPIO 13 (UART TX) ───────────────── 0  (RX1)
  GPIO 14 (UART RX) ───────────────── 1  (TX1)
  GPIO 21           ───────────────── 2  (spare, unused)
  5V (IP5306 output) ──────────────── VIN
  GND               ───────────────── GND
```

- Both boards are 3.3 V, no level shifting needed; the Teensy's pins are not 5 V tolerant.
- Cut the VUSB–VIN jumper on the back of the Teensy so that the computer's USB and the IP5306 do not power each other.
- The microSD card goes into the Teensy's slot. The DAC is on the Teensy's I2S1, see [DAC](#dac-pcm5102a).

### MIDI OUT (TRS type A, 3.3 V)

```
+3.3V  ──[33 Ω]── Ring   (MIDI DIN pin 4)
GPIO10 ──[10 Ω]── Tip    (MIDI DIN pin 5)
GND    ────────── Sleeve (MIDI DIN pin 2, shield)
```

- TRS **type A** pinout (MMA standard): Tip = pin 5, Ring = pin 4. Type B devices (some Arturia, Novation) need an A→B adapter, or swap Tip and Ring.
- For a 5-pin DIN jack: the same resistors on pin 4 and pin 5, GND on pin 2.
- Non-inverted UART, 31250 baud. Optionally, a 74HC14 buffer (two inverters in series) can be placed between GPIO10 and the 10 Ω resistor.
- There is no MIDI input: the device is always the master and sends clock, Start/Stop/Continue.

### Built-in peripherals (for reference)

| Block | Pins |
|---|---|
| microSD (SPI) | CS 41, MOSI 40, CLK 39, MISO 38 — not used (the card is in the Teensy), reserve |
| FT6336U touch (I2C) | SDA 6, SCL 5, INT 7, RST 4 (shared with LCD) |
| ST7796 display | 8080 bus, 8-bit (internal board routing) |

GPIO 1, 2, 42 go to the built-in RS485 transceiver; only the A/B lines reach the connector. GPIO 35, 36, 37 go to the built-in NS4168 amplifier and the SPK connector; they are not used: the speaker is removed and sound comes from the synth board's DAC (see below). GPIO 43/44 (Debug connector) are used by the PCF8575.

## Sound

Built-in synthesizer: 32 instruments (FM — 8 machines; DRUM — 16 machines in the spirit of the TR-808 / TR-909; SYNTH — 2 oscillators BL saw / square / tri or wavetable, sub, noise, sync; SAMPLE — project samples, played from flash; CHIP — pulse, triangle, saw, noise, metal, 16 wavetables; see below), each with a filter and LFO; it runs on the synth board: 32 voices, 44.1 kHz, stereo, per-track pan (TRACK → Pan, equal power; stored in the project, old projects are centred). A track is switched in TRACK → Out: INT (default in a new project) or MIDI. Sound goes to a PCM5102A DAC on the Teensy's I2S1 and from it to a MAX97220 headphone amplifier; the WT32's NS4168 amplifier and SPK connector are not used. Master volume: MIX tab → MAIN, 0–200 % (above 100 % — up to +6 dB, peaks of loud chords are softly clipped); saved with the project and included in Render WAV. Output level of the device: PROJ → SYS → Phones, 0–100 %, a device setting stored in the board's memory.

### DAC (PCM5102A)

A common module (purple GY-PCM5102, black "PCM5102 audio DAC v2" and similar), on the Teensy.

| DAC module | Connect to | Notes |
|---|---|---|
| VIN | Teensy 3V3 (or 5 V) | the module has its own LDO |
| GND | GND | star point of the audio ground, see below |
| BCK | Teensy 21 (BCLK1) | bit clock |
| LCK (LRCK, WS) | Teensy 20 (LRCLK1) | word clock |
| DIN | Teensy 7 (OUT1A) | data |
| SCK (MCLK) | GND | no MCLK: the DAC's PLL derives the clock from BCK |
| FMT | L (GND) | I2S format |
| FLT | L (GND) | normal-latency filter |
| DEMP | L (GND) | de-emphasis off |
| XSMT | H (3.3 V) | unmuted; at L the output is muted |

On many modules FMT, FLT, DEMP and XSMT are solder jumpers (H / L pads) on the back, and SCK has its own pad or header pin — check the silkscreen of your module.

The synth board sends 44.1 kHz, 16-bit stereo (Teensy Audio Library `AudioOutputI2S`, BCK = 64 fs). The L / R outputs are a line level: 2.1 Vrms at full scale, centred on ground (no DC offset). Unlike the old SPK output, this can go to a mixer, audio interface, powered speakers or a recorder — from the DAC's L / R (before the amplifier) or from the amplifier's headphone output.

### Headphone amplifier (MAX97220)

```
DAC L   ── IN L+        OUT L ── Tip    (left)
DAC GND ── IN L−        OUT R ── Ring   (right)
DAC R   ── IN R+        GND   ── Sleeve (common, GND is fine here)
DAC GND ── IN R−
board 5V (or 3.3V) ── VCC     GND ── GND     SHDN ── VCC
```

- The inputs are differential: each channel's "+" goes to the DAC output, its "−" to the DAC's GND (AGND). Pin names on the module's silkscreen may differ — check them.
- DirectDrive: the outputs are ground-referenced and need no coupling capacitors; the jack's Sleeve goes to GND as usual.
- Power: 5 V from the IP5306 output after the switch (the same line that feeds the boards) or 3.3 V. SHDN, if broken out, goes to VCC (amplifier running).
- The PCM5102A's 2.1 Vrms full scale is more than the amplifier can deliver: if it clips or is too loud, lower Phones (PROJ → SYS) or add a divider / potentiometer between the DAC and the amplifier inputs.
- Ground: keep audio ground wires short and use a star point at the DAC's GND: the DAC, amplifier and headphone jack grounds meet there, and one wire goes from it to the Teensy's GND. This keeps the IP5306 boost converter's whine out of the headphones.
- Before first power-up — Phones (PROJ → SYS) at minimum, headphones off your ears, then bring it up.

The internal sound lags behind MIDI tracks by a few milliseconds: events are played 4 ms after they are sent over the link, plus the audio buffer (constant, no jitter; not compensated yet). The synth board's load is shown by `CPU NN%` in the right corner of the status bar (yellow from 60%, red from 85% or after an audio dropout — an audible click).

### Samples

- Samples belong to the project: each project has its own list (up to **128**), and an instrument (INST → Sample) chooses only from it. On the card, a project is `/projects/NAME.mtp` plus the folder `/projects/NAME/` with its samples (`<sample>.wav`, mono, 16-bit). The folder is written by Save: missing and changed WAVs are written, files of samples removed from the project are deleted. If the folder could not be written — "SAMPLES NOT SAVED", the project stays unsaved (`*`), and the next Save completes it.
- To move a project to another card or tracker, copy the `.mtp` and its folder (take the card out of the Teensy and use a card reader). Load and autoload pull samples that are not in flash from the project folder (with progress); if a file is not found or has changed, the sample is marked `MISSING` (silent), toast "N SAMPLES MISSING".
- The sample bank in the Teensy's program flash (from 0x60200000, ~5.75 MB, ≈ 1 min mono at 44.1 kHz, up to 128 entries) is the cache the samples play from. Identical data in different projects is stored once. When space runs out, import and loading evict other projects' samples on their own (largest first) and compact the flash; otherwise "BANK FULL". The cache does not depend on the card, and a synth board firmware update does not touch it.
- FILE → SAMPLES: the line `FREE n / m KB  CACHE k KB` (CACHE — space taken by other projects' samples), **Import WAV…**, **Compact** (compact the flash), **Clear cache** (remove from flash everything not in the current project), then the project's samples: name, length, size, red `MISSING`. Click a sample — **Rename** (also renames it in instruments; a MISSING sample cannot be renamed) or **Delete** (remove from the project; the next Save deletes the file).
- `/samples` on the card (created automatically) is the import library, subfolders allowed up to 4 levels. Import WAV…: folders first (`name/`, click to enter, `< Up (..)` to go up), then `.wav` files; hidden files (macOS `._*`) are not shown. Play on a file previews it (first 8 s, Play again stops; in this list the Play button does not start the transport). An imported sample goes into the project folder on save.
- WAV: PCM **8 / 16 / 24-bit**, mono or stereo (stereo is mixed down to mono), any sample rate: above 44.1 kHz it is downsampled to 44.1 kHz, lower rates are stored as is. **Float and 32-bit are not supported** ("UNSUPPORTED WAV").
- Import, Rename, Delete, Compact and Clear cache work only with playback stopped (writing to the Teensy's flash pauses the sound).
- Old projects (from when samples were shared): on load the list is built from the instruments, and samples are taken from flash by their old names. Save such a project once and its folder will appear.

### FM

**FM** — 8 machines in the style of the Model:Cycles: KICK, SNARE, METAL, PERC, TONE, CHORD, CLAP, HAT. Instead of operators there are 5 macros: DECAY (length), COLOR (brightness / index), SHAPE (timbre variant; for CHORD — chord type), SWEEP and CONTOUR (pitch or index envelope). Percussion plays one-shot (note length and ADSR don't matter, a new note cuts off the previous one); TONE and CHORD hold the note (attack / sustain / release from the ADSR). TONE can be POLY; the other machines are mono per track.

P-lock: fx `DEC`, `COL`, `SHP`, `SWP`, `CON` (0–127) set a macro for the note on their step; on a step without a note — for the playing note until the next one. They work on FM, DRUM and SYNTH.

### KIT (drum track)

The **KIT** instrument type has 8 lanes: each is either its own mini-sampler (a project sample, Volume, Pitch, Decay) or a reference to any of the 32 instruments. A track with a KIT becomes a drum track: in GRID, instead of notes there are 8 squares per step; in edit mode, a pad of 8 buttons (and track buttons 1–8) toggles the lanes. Each lane is mono with choke; lanes sound together. On a MIDI track, the lanes are sent as their own notes (the lane's Note) — for an external drum machine. Fx `ACC` is a mask of lanes at full volume, the rest at 60 %. Fill (NOTE) writes to the selected lane. More in the [manual](docs/manual.md#drumtrack).

### DRUM (808 / 909)

**DRUM** — 16 "analog" machines: BD8, SD8, TOM8, CP8, RS8, CL8, CB8, HH8, CY8 (808) and BD9, SD9, TOM9, CP9, RS9, HH9, CY9 (909). The same 5 macro slots and `DEC…CON` locks, but each machine names them differently (TONE, SNAPPY, DRIVE, N.DEC, HP…); an empty slot is a grey "-". Every machine has DECAY. All machines are one-shot and mono per track: a new note cuts off the previous one (choke), note-off doesn't matter; Attack, Decay, Sustain, Release, Mode, Glide are greyed out. Note C4 is the machine's base pitch. Closed / open hat — short and long DECAY. The timbres approximate the originals (the 909 hats and cymbals are samples there, synthesis here). FM, DRUM and SYNTH with a WT oscillator together — no more than 16 voices at once (an extra one steals the oldest).

### SYNTH (oscillators and wavetable)

**SYNTH** — an "analog" synthesizer: 2 oscillators, each in **SAW**, **SQR**, **TRI** mode (alias-free, PolyBLEP) or **WT** (a wavetable of 64 frames × 256 points with mipmaps, frame position = SHAPE), plus sub (a square −1 / −2 octaves below osc 1), white noise, hard sync of osc 2 to osc 1, osc 2 semitones (±24) and its own AD envelope on SHAPE (Env>Shp ±). The volume envelope is ADSR; Mode POLY / MONO and Glide work as on CHIP. INST pages: MAIN / ENV / **OSC** / **MOD** / FILT / LFO.

- 5 macros on the same slots as FM / DRUM: **SHP1** (square PW or WT frame of osc 1), **SHP2** (the same for osc 2), **MIX** (osc 1 ↔ osc 2), **DET** (osc 2 detune, ±50 cents), **SENV** (env→SHAPE depth). Locks are fx `DEC`, `COL`, `SHP`, `SWP`, `CON` respectively; LFO dest — SHP1…SENV.
- Wavetables: 8 built-in (`*SAWSQR`, `*PWM`, `*SINSAW`, `*TRISQR`, `*FORMANT`, `*ORGAN`, `*SYNC`, `*BELL`) — built into the synth board's flash bank on first start, work without a card. Your own — **IMPORT…** in the wavetable chooser (tap / click the Table row) from `/wavetables` on the card (with subfolders up to 4 levels): WaveEdit (256-point frames, up to 64) and Serum / Vital (2048-point frames or a `clm` chunk; with more than 64 frames, they are taken evenly). One wavetable takes 96 KB of the bank; up to 32 wavetables per project. A name already in use with different data gets a `-2`, `-3`… suffix.
- Save writes the project's wavetables to `/projects/NAME/wt/<name>.wav`, and Load pulls missing ones into flash from there. If a wavetable is neither in flash nor in the folder, its name in INST is red and the oscillator is silent.
- Import works only with playback stopped ("STOP PLAYBACK FIRST"). 37 factory presets (BASS, LEAD, PAD, KEYS, PLUCK, FX) on the built-in wavetables; your own go in `/presets/SYNTH/`.

### Filter and LFO

All types have these at the end of the INST list:

- **Filter** OFF / LP / BP / HP (SVF, 12 dB/oct), **Cutoff** 20 Hz…14 kHz, **Reso** 0–127, **Flt env** ±6 octaves with its own **Flt attack** / **Flt decay** (HOLD — hold), **Key track** 0–100 % (from C4). The filter comes after the whole voice; OFF is a bypass, so old projects sound as before.
- **LFO** wave / rate / depth / dest — now on all types. Targets PITCH, VOL, CUTOFF — everywhere; DECAY…CONTOUR — only FM, DRUM and SYNTH (on SYNTH — SHP1…SENV).
- Filter p-locks: fx `FLT` (cutoff) and `RES` (resonance), 0–127, on any INT instrument with the filter enabled; on a step without a note — for the track's playing voices until the next note.

### Drive, reverb, compressor

- **Drive** (INST → FILT, first row): tanh overdrive before the filter, lock `DRV`, LFO target `DRIVE`.
- **Reverb**: the instrument's Rvb send (lock `RVB`), size / decay / level in PROJ. Stereo (Freeverb), buffers in the Teensy's RAM.
- **Compressor** on all built-in sound (PROJ → Comp, Comp rel) with **sidechain** from a track (SC track, SC depth): set SC track to the kick and the mix "pumps".
- **ARP**: fx `ARM` sets the order (up, down, up-down, random) and the number of notes per step (1–8); ARP + CHD on the same step arpeggiates the chord.
- **4 LFOs** per instrument (INST → LFO), each in Hz or synced to tempo (1/32 … 8 bars), restarted per note or free-running per track from the playback start (bar-locked with TEMPO); step fx `LFO` / `LFD` / `LFS` / `LFW` / `LFT` / `LFR` select an LFO, lock its depth / rate / wave / dest and restart its phase.
- **Lo-fi**: fx `BIT` (bit depth) and `SRR` (sample rate) per note; **DJ filter** on the master (PROJ → FX).
- Pattern **groove** (PROJ → SONG → Groove: MPC 54–66, SHUFFLE, PUSH, LAID BACK, DRUNK, BOOM BAP, HOUSE) and track **Humanize** (TRACK → NOTE); conditions `CND PRE / NEI`, volume ramp up/down on `RAT` (`4^`, `4v`).
- **Step arp** `ARS` (INT and MIDI): the CHD notes (or ARP 0/x/y, or the note itself) across a range of 1–4 octaves play as separate notes on the track's following empty steps — every step or every N; until the next note, OFF or a pattern change. The fx list in GRID is grouped: notes and arp, timing, randomness, pitch and volume, sound, sample, sends, MIDI.
- **Velocity**: Vel>Cut (filter cutoff) and Vel>Dec (DECAY macro) in INST → ENV.

Everything is off by default: old projects and presets sound as before. Presets with the new fields are version 4; old firmware cannot open them.

### Presets

The **PRESET** button in the INST header (or Shift + long press of the encoder) → **Load** / **Save**.

- **Load:** a list for the instrument type (arrows in the header or Shift + turn — another type). At the root — `[FACTORY]` (145 factory presets: 33 CHIP, 37 FM, 38 DRUM, 37 SYNTH, by category; up to 63 rows are visible in one category or folder), then your own folders and files. The selected preset is applied to the instrument immediately and plays C4. **OK** — keep it, **CANCEL** (or a long press) — revert, **DEL** — delete your own preset.
- **Save:** **SAVE** — name on the keyboard (if it already exists, asks whether to overwrite), **+DIR** — new folder, click a file — overwrite it. The file name also becomes the instrument name.
- On the card: `/presets/CHIP|SAMPLE|FM|DRUM|SYNTH/…/NAME.mti` (204 bytes, format v3; old v1 / v2 are readable), up to 4 folder levels inside a type. A SAMPLE preset applies its sample only if it is in the project; otherwise the sample and Root stay as they were. A SYNTH preset stores wavetable names: if a wavetable is not in the project, it is imported from `/wavetables/<name>.wav` (with playback stopped); otherwise the name is red.

Details (INST tab, preset browser, DRUM machine table, SYNTH and wavetables, fx SLD/VIB/ARP/VSL/OFS/CUT, DEC/COL/SHP/SWP/CON, FLT/RES) are in the [manual](docs/manual.md#sound).

## Song and live performance

- **Song mode** (BANK): each chain entry has a pattern, a transposition of melodic tracks (±24), a number of passes (x1–x16) and a mute scene (S1–S8) that is applied when the entry starts. The chain row reads `P05 +3 x2 S1`; Shift + turn in row edit mode selects the field.
- **Mute scenes**: 8 tiles under the chain — tap recalls, long tap stores the current mutes, Shift + tap clears.
- **Per-track length** (TRACK → Pat len): the track loops its first N steps within the pattern (polymeter); in GRID the steps beyond are grey, and the boundary is marked with a line.
- **Fill**: hold Shift + Play while playing — steps with `CND FIL` play, those with `NFL` are silent; a short press is pause, as before.
- **REC** (GRID menu): while playing, the track buttons write scale degrees into the playing step of the current track (on a drum track — lanes); Shift + N erases.
- **PERF** (GRID menu): an effect while the button is held — 1 RAT 2, 2 RAT 4, 3 filter closed, 4 open, 5 delay, 6 short DECAY, 7 fade, 8 mute.

More in the [manual](docs/manual.md#song).

## Mixer and render

- **MIX** is a separate tab at the bottom, next to TRACK: 8 strips for the half of the tracks under the cursor (volume fader, delay / reverb sends, M / S) and the MAIN strip — overall volume 0–200 % (moved here from PROJ).
- Track volume from any screen: hold its button and turn the encoder (Shift ×10); not in GRID edit mode, REC or PERF.
- **FILE → Render WAV…**: a pattern or the whole song (ALL, solo tracks only, or STEMS — each track to its own file) to `/samples/render/<project>_P01.wav` / `_SONG.wav`, stereo 16-bit 44.1 kHz, rendered by the synth board, with delay / reverb / compressor and a 2 s tail. The file can be taken from the card or imported back as a sample (mixed down to mono).
- **GRID → Resample track / pattern**: a pattern (the current track or all audible ones) into a new project sample `RS1`, `RS2`… — normalized to −1 dBFS, trailing silence trimmed, up to 60 s.
- Render and resampling work only when stopped, roughly in real time; cancel with a long press of the encoder or Play.

More in the [manual](docs/manual.md#render).

## Interface

- **Themes**: PROJ → SYS → Theme, 17 built-in themes (CLASSIC, AMBER, PHOSPHOR, NORD, DRACULA, SOLARIZED, GRUVBOX, MONOKAI, TOKYO, MOCHA, ROSE PINE, GAMEBOY, C64, SYNTHWAVE, OCEAN, CONTRAST, PAPER). A device setting: stored in the board's memory, does not change the project.
- **Pages** in TRACK (MAIN / NOTE / MIDI) and PROJ (SONG / FX / COMP / SYS): tap a page tab, or use the encoder on the page tabs and click (Shift+click — back); the list wraps around.
- **PERF**: the effect of each button is configured in PROJ → PERF (RAT 2/3/4/8, ROLL UP, FILTER LOW/HIGH, DELAY/REVERB MAX, CRUSH, DOWNSAMPLE, DRIVE, SHORT DECAY, FADE, MUTE); pressing shows a toast with its name.
- **Project templates** (FILE → New: EMPTY, 808 SET, 909 SET, FM SET, CHIPTUNE, MIDI 8 and your own from `/templates`, Save as template), **8 demo songs** (FILE → New → Demo songs: trance, chiptune, acid, lo-fi, synthwave, dub techno, IDM, house), **autosave** to `/projects/<name>.auto` (PROJ → SYS → Autosave, FILE → Restore autosave), **safe start** (hold Shift at power-on — no autoload), **crash log** `/projects/crashlog.txt` and the firmware version in PROJ → SYS.
- **MIX**: encoder — MAIN volume, Shift+turn — tracks A (1–8) / B (9–16); track volume — its button + encoder, mute — Shift+button, solo — tap. Each track has a level meter; at the bottom there is a scope with auto-gain and a level meter / CLIP.

## SD card

The card is in the **Teensy's** microSD slot (the WT32's own slot is not used); the tracker reaches it over the link. **FAT32** or **exFAT**, long names. To move files, take the card out of the Teensy and use a card reader. When upgrading from the single-board version, copy the contents of the old card to the new one.

The firmware creates the folders `/projects`, `/midi`, `/samples`, `/wavetables` and `/presets` (with subfolders `FM`, `DRUM`, `SAMPLE`, `CHIP`, `SYNTH`) itself; `/firmware/teensy.hex` is the synth board firmware for Update synth. Put MIDI files for import in `/midi`, WAVs for importing into projects in `/samples`, wavetables for SYNTH in `/wavetables`. A project's samples live in `/projects/NAME/`, its wavetables in `/projects/NAME/wt/` (written by the tracker on save), instrument presets in `/presets/<TYPE>/`.

## Wi-Fi

FILE → **Wi-Fi firmware…**: the tracker joins your home network (network and password are entered on screen and stored in flash) and serves the page `http://d-trk.local/firmware` (or by the IP shown on screen). The page has two sections:

- **Tracker (.bin)** — firmware update of the ESP32-S3 with the file `.pio/build/wt32/firmware.bin`;
- **Synth board (.hex)** — firmware update of the Teensy with the file `.pio/build/teensy41/firmware.hex`: the tracker stores it on the synth card as `/firmware/teensy.hex`, the Teensy checks the image in its staging area (FlasherX) and only then flashes itself and restarts. Without Wi-Fi: copy the file to `/firmware/teensy.hex` on the card and run PROJ → SYS → Update synth.

There is no file manager any more: projects, samples, MIDI files and presets are moved by taking the card out of the Teensy.

While this mode is open, playback is stopped. Exit with EXIT, a long press of the encoder or another tab; Wi-Fi is turned off.

**Security:** the page has no password. While the mode is open, anyone on the same network can flash the tracker and the synth board. Do not enable it on other people's or public networks.

More in the [manual](docs/manual.md#wifi).

## Build and flash

You need [PlatformIO](https://platformio.org/).

```
pio run -e wt32 -t upload          # tracker (ESP32-S3): build and flash over USB
pio run -e wt32-ota -t upload      # tracker over Wi-Fi: FILE → Wi-Fi firmware… open on the tracker
pio run -e teensy41 -t upload      # synth board: first flash over the Teensy's USB (Teensy Loader)
pio run -e teensy41                # synth board .hex for the Wi-Fi page or /firmware/teensy.hex
pio test -e native                 # core tests on the computer (955 tests)
```

- **First flash of the Teensy** goes over its USB with Teensy Loader (before the enclosure is closed); later the synth board is updated from the tracker (Wi-Fi page or PROJ → SYS → Update synth).
- The ESP partition table is custom (`partitions.csv`): two ~8 MB firmware slots (`app0` / `app1`); the old `samples` partition is gone. After switching to it, flash once over USB (`pio run -e wt32 -t upload`): OTA does not change the partition table. Wi-Fi settings (NVS) are kept — `nvs` and `otadata` are at the same addresses.
- Teensy program flash: firmware from 0x60000000, OTA staging area at 0x60100000, sample bank from 0x60200000.

Service environments:

| Env | What for |
|---|---|
| `teensy41-bench` | voice pool bench on the Teensy: results over USB serial and the link log; do not save projects from this build |
| `teensy41-desk` | desk mode: a bare Teensy on a computer, the link over USB serial, sound on USB audio; drive it with `scripts/link_desk.py hello\|notes\|status\|fs\|selftest` |
| `wt32-echo` + `teensy41-echo` | link wiring check: echo test between the boards |

## Structure

| Folder | Contents |
|---|---|
| `lib/core` | hardware-independent code shared by both boards: model, sequencer, fx, scales, Euclid and Fill, file format, MIDI parser, synthesizer (CHIP, SAMPLE, FM, DRUM, SYNTH, filter, delay, reverb, compressor), WAV parser, sample bank, project sample list, presets; link: `link_frame`, `link_msg`, `link_fs`, `state_mirror`, `time_sync`, `bank_ops`, `render_link`, `wave_peaks`, `ihex` |
| `lib/flasherx` | FlasherX (vendored, with its license): writing the Teensy's flash for OTA |
| `src/engine` | sequencer task on core 0 (timer, MIDI output) |
| `src/link` | link client on the ESP: UART task, synth events and state, sample bank commands, synth board firmware update |
| `src/hw` | input (encoder, buttons), MIDI UART, card access, display configuration |
| `src/ui` | UI screens |
| `src/storage` | saving and loading projects and presets; `remote_fs` — the Teensy's card as an `fs::FS` |
| `src/net` | Wi-Fi, firmware web page, OTA |
| `src/teensy` | synth board firmware: link server, audio output, sample bank in flash, card file server, render, preview, firmware update, bench |
| `test` | Unity tests of the core |
| `docs` | manual and development plans |
| `enclosure` | enclosure: OpenSCAD model and STL |

## License

Copyright © 2026 deemonoise.

- **Firmware, tests, scripts and documentation** — [GNU GPL v3](LICENSE) (GPL-3.0-only). You may use, change and share them, sell devices with them included, as long as the source of the firmware you ship (with your changes) is published under the same license.
- **Hardware** — the enclosure ([enclosure/](enclosure/): OpenSCAD sources and STL) and the wiring diagrams ([docs/img](docs/img)) — [CERN-OHL-S v2](enclosure/LICENSE) (CERN-OHL-S-2.0): products made from them must make their modified design sources available under the same license.

The name **D-TRK** and its logo are not covered by these licenses: a modified firmware or a device built from this project may say it is based on D-TRK, but must not be called or sold as D-TRK without permission.

Full texts: [LICENSES/](LICENSES/). Third-party libraries (ESP-IDF, Arduino-ESP32, LovyanGFX, Teensyduino, Teensy Audio Library, SdFat, FlasherX, Unity) keep their own licenses.
