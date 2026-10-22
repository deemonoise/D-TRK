# D-TRK

<p align="center">
  <a href="docs/img/tracker.jpg"><img src="docs/img/tracker.jpg" alt="D-TRK" width="320"></a>
</p>

A pocket 16-track tracker with a built-in synthesizer. Built on the **WT32-SC01 Plus** (ESP32-S3, 480×320 touchscreen): an encoder, Play, Shift, A and B buttons and 8 mechanical track buttons. With A / B the tracker works almost without the touchscreen: navigation, editing and page switching are done with the encoder and buttons. Stereo sound through a PCM5102A DAC and a headphone amplifier. MIDI OUT (TRS) sends notes, clock and Start / Stop. The firmware is open (GPL v3), and so is the 3D-printable enclosure (CERN-OHL-S).

User manual: [docs/manual.md](docs/manual.md). Wiring: [docs/wiring.md](docs/wiring.md). Русская версия: [README_ru.md](README_ru.md).

## Features

### Sequencer

- 16 tracks, 16 patterns, 4 to 128 steps, tempo 20–300 BPM, swing.
- Each track has its own length and speed (1/4, 1/2, x1, x2, x4): polymeter, slow pads over fast grooves.
- A step holds a note, volume, chord (36 types × 4 inversions) and 6 fx commands: slide, vibrato, retrigger, arp, trig conditions (probability, fill, "previous played"…), p-locks of sound parameters, pan and LFOs.
- Fill notes, volume or any fx on every Nth step, by Euclid or at random, with value ramps.
- Generators that write straight into notes: **ARP** (106 patterns, including 64 from the Access Virus), **CHORDS** (55 progressions in the project key, 32 rhythms, RANDOM, Mutate, voice leading) and **BASS** (48 bass patterns that follow the chords). Your own patterns are saved with Capture.
- A live shift-register generator per track (**GEN**): a looping 2–16-bit register slowly mutates and plays notes of the project scale (pitch, or the rhythm too), Flip live from a track button and the FLP fx per step, Capture writes the loop into the pattern.
- **Dice** on PROJ: one roll gives a genre tempo and scale, factory sounds on four role tracks (drums, bass, chords, lead) and a pattern from the generators, undoable; **Mutate** on INST nudges an instrument's sound.
- Evolve per track (**EVO**): the pattern drifts live (notes along the scale, drops and ghost notes, retriggers and velocity, shifted steps), pulled back by Home; Capture writes the heard variant into the pattern.
- Grooves (MPC, shuffle, drunk, boom bap, house…) and per-track humanize.
- Scales, undo, selection, copy.
- Microtonal project tuning: 10 factory tunings (just, meantone, 19/24/31-EDO, slendro, pelog, Bohlen-Pierce…) and your own Scala `.scl` files from the card.

### Built-in sound

32 instruments, up to 24 voices, 32 kHz, stereo with per-track pan. Types:

- **FM**: 8 machines in the spirit of the Model:Cycles (kick, snare, metal, perc, tone, chord, clap, hat) with 5 macros.
- **DRUM**: 16 machines in the spirit of the TR-808 and TR-909.
- **SYNTH**: 2 oscillators (alias-free saw / square / tri or wavetable), sub, noise, hard sync. Imports WaveEdit, Serum and Vital tables.
- **SAMPLE**: a sampler with slices, WAV import from the card. The sample editor works without the touchscreen too.
- **CHIP**: pulse, triangle, noise, metal and wavetable for chiptune.
- **KIT**: a drum track of 8 lanes, each with its own sample or any instrument.

Every instrument has an LP / BP / HP filter with an envelope and 4 LFOs (tempo sync, modulate each other, controlled from steps). 145 factory presets.

### Mix and effects

- Mixer: faders, pan, sends, mute / solo, level meters, scope.
- Track insert: drive (TANH, CLIP, FOLD, RECT), bit crush, downsample and a trance gate with a 16-step pattern. Instrument presets carry a recommended insert.
- Envelope follower: one track's level moves a parameter of another (sidechain pumping, ducking, auto-wah).
- Sends: delay (DIGITAL, TAPE, PING-PONG), pseudo-stereo reverb with shimmer, MOD bus (chorus, ensemble, flanger, phaser).
- Master: compressor with sidechain, EQ shelves, PERF punch-in effects (repeat, roll, reverse, scratch, tape stop, gate, LP / HP filter, crush, downsample, drive, delay / reverb / MOD throw, tape wobble, freeze).
- Each track goes to the internal synth or to external MIDI.

### Live performance and song

- **Song**: up to 128 slots, each with a pattern and the mutes of 16 tracks. An A tap repeats a slot, a tap or a track button mutes a track in the slot. Follow actions per slot (next, previous, stay, jump, random, with a chance) from a project seed: every Play a new path, the render repeatable. Play on the SONG tab plays the song, on other tabs it loops the pattern; Shift + Play plays the song from the cursor on any tab; SONG / LOOP switch on the fly, undo for song edits.
- **PERF**: punch-in effects on the whole mix while a button is held (repeat, roll, reverse, scratch, tape stop, gate, filter, crush, downsample, drive, delay / reverb / MOD throw, tape wobble, freeze), the encoder sets the depth, several buttons stack; button assignment is configurable. Works on GRID, SONG and MIX; held effects also show on the whole screen (dim, pixelate, invert…), switchable in FILE → SYSTEM. XY pad (A + B): the touch screen holds two effects, the finger sets their depths.
- **REC**: record notes from the track buttons while playing.
- Shift + Play: the song from the SONG cursor on any tab; held while playing, fill.

### Files and extras

- Projects, samples and presets on microSD, autosave, templates and 8 demo songs.
- MIDI file import.
- Stereo WAV render: a pattern, the whole song or per-track stems. Resample a track into a new sample.
- Wi-Fi: a web page for uploading files and firmware.
- 17 themes.

## Hardware

The base is the WT32-SC01 Plus board (ESP32-S3, 480×320 touchscreen, microSD slot). Soldered to it: an EC11 encoder, a PCF8575 expander with the Play, Shift, A, B and 8 track buttons, a PCM5102A DAC with a MAX97220 headphone amplifier, a TRS MIDI output, LiPo power on an IP5306 and an optional INA226 battery monitor.

- Wiring diagram, pinout and checks before power-up: [docs/wiring.md](docs/wiring.md).
- Parts list: [docs/bom.md](docs/bom.md).
- Enclosure and power circuit: [enclosure/](enclosure/README.md).

## Build and flash

You need [PlatformIO](https://platformio.org/).

```
pio run -e wt32 -t upload     # build and flash over USB
pio run -e wt32-ota -t upload # over Wi-Fi: FILE → Wi-Fi transfer open on the tracker
pio test -e native            # core tests on the computer
```

The partition table is custom (`partitions.csv`): two 3 MB firmware slots and a `samples` partition (~9.9 MB) for the sample flash cache. After switching to it, flash once over USB (`pio run -e wt32 -t upload`): OTA does not change the partition table. Wi-Fi settings (NVS) are kept — `nvs` and `otadata` are at the same addresses.

## Structure

| Folder | Contents |
|---|---|
| `lib/core` | hardware-independent core: model, sequencer, fx, scales, tuning (`.scl` parser, factory tunings, key map), shift-register generator, evolve, song follow actions, Euclid and Fill, file format, MIDI parser, synthesizer (CHIP, SAMPLE, FM, DRUM, filter), WAV parser, sample bank, project sample list, presets |
| `src/engine` | sequencer task on core 0 (timer, MIDI output) |
| `src/audio` | built-in sound: I2S to the external PCM5102A DAC, audio task, sample bank in flash, WAV import |
| `src/hw` | input (encoder, buttons), MIDI UART, SD, display configuration |
| `src/ui` | UI screens |
| `src/storage` | saving and loading projects and presets |
| `src/net` | Wi-Fi, file transfer web page, OTA |
| `test` | Unity tests of the core |
| `docs` | manual, wiring diagram, parts list and development plans |
| `enclosure` | enclosure: OpenSCAD model and STL |

## License

Copyright © 2026 deemonoise.

- **Firmware, tests, scripts and documentation** — [GNU GPL v3](LICENSE) (GPL-3.0-only). You may use, change and share them, sell devices with them included, as long as the source of the firmware you ship (with your changes) is published under the same license.
- **Hardware** — the enclosure ([enclosure/](enclosure/): OpenSCAD sources and STL) and the wiring diagrams ([docs/img](docs/img)) — [CERN-OHL-S v2](enclosure/LICENSE) (CERN-OHL-S-2.0): products made from them must make their modified design sources available under the same license.

The name **D-TRK** and its logo are not covered by these licenses: a modified firmware or a device built from this project may say it is based on D-TRK, but must not be called or sold as D-TRK without permission.

Full texts: [LICENSES/](LICENSES/). Third-party libraries (ESP-IDF, Arduino-ESP32, LovyanGFX, Unity) keep their own licenses.
