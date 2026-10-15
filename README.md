# D-TRK

A pocket 16-track tracker with a built-in synthesizer. Built on the **WT32-SC01 Plus** (ESP32-S3, 480×320 touchscreen) plus a **Teensy 4.1** synth board: an encoder, Play, Shift, A and B buttons and 8 mechanical track buttons. With A / B the tracker works almost without the touchscreen: navigation, editing and page switching are done with the encoder and buttons. Stereo sound at 44.1 kHz through a Teensy Audio Shield (SGTL5000) to headphones. MIDI OUT (TRS) sends notes, clock and Start / Stop. The firmware is open (GPL v3), and so is the 3D-printable enclosure (CERN-OHL-S).

User manual: [docs/manual.md](docs/manual.md). Wiring: [docs/wiring.md](docs/wiring.md). Русская версия: [README_ru.md](README_ru.md).

## Features

### Sequencer

- 16 tracks, 16 patterns, 4 to 128 steps, tempo 20–300 BPM, swing.
- Each track has its own length and speed (1/4, 1/2, x1, x2, x4): polymeter, slow pads over fast grooves.
- A step holds a note, volume, chord (36 types × 4 inversions) and 6 fx commands: slide, vibrato, retrigger, arp, trig conditions (probability, fill, "previous played"…), p-locks of sound parameters, pan and LFOs.
- Fill notes, volume or any fx on every Nth step, by Euclid or at random, with value ramps.
- Generators that write straight into notes: **ARP** (106 patterns, including 64 from the Access Virus), **CHORDS** (55 progressions in the project key, 32 rhythms, RANDOM, Mutate, voice leading) and **BASS** (48 bass patterns that follow the chords). Your own patterns are saved with Capture.
- Grooves (MPC, shuffle, drunk, boom bap, house…) and per-track humanize.
- Scales, undo, selection, copy.

### Built-in sound

32 instruments, 32 voices (up to 16 of them FM / DRUM / SYNTH WT), 44.1 kHz, stereo with per-track pan. Types:

- **FM**: 8 machines in the spirit of the Model:Cycles (kick, snare, metal, perc, tone, chord, clap, hat) with 5 macros.
- **DRUM**: 16 machines in the spirit of the TR-808 and TR-909.
- **SYNTH**: 2 oscillators (alias-free saw / square / tri or wavetable), sub, noise, hard sync. Imports WaveEdit, Serum and Vital tables.
- **SAMPLE**: a sampler with slices, WAV import from the card. The sample editor works without the touchscreen too.
- **CHIP**: pulse, triangle, noise, metal and wavetable for chiptune.
- **KIT**: a drum track of 8 lanes, each with its own sample or any instrument.

Every instrument has an LP / BP / HP filter with an envelope, drive and 4 LFOs (tempo sync, modulate each other, controlled from steps). 145 factory presets.

### Mix and effects

- Mixer: faders, pan, sends, mute / solo, level meters, scope.
- Delay and pseudo-stereo reverb on sends, master compressor with sidechain, PERF punch-in effects on the master (repeat, roll, reverse, scratch, tape stop, gate, LP / HP filter, crush, downsample, drive, delay / reverb throw, freeze).
- Per-note lo-fi: bit depth and sample rate.
- Each track goes to the internal synth or to external MIDI.

### Live performance and song

- **Song** (SONG tab, Polyend-style): up to 128 slots, each with a pattern and the mutes of 16 tracks. An A tap repeats a slot, a tap or a track button mutes a track in the slot. Play on the SONG tab plays the song, on other tabs it loops the pattern; Shift + Play plays the song from the cursor on any tab; SONG / LOOP switch on the fly, undo for song edits.
- **PERF**: punch-in effects on the whole mix while a button is held (repeat, roll, reverse, scratch, tape stop, gate, filter, crush, downsample, drive, delay / reverb throw, freeze), the encoder sets the depth, several buttons stack; button assignment is configurable.
- **REC**: record notes from the track buttons while playing.
- Shift + Play: the song from the SONG cursor on any tab; held while playing, fill.

### Files and extras

- Projects, samples and presets on microSD, autosave, templates and 8 demo songs.
- MIDI file import.
- Stereo WAV render: a pattern, the whole song or per-track stems. Resample a track into a new sample.
- Wi-Fi: a web page for firmware updates (the tracker and the synth board).
- 17 themes.

## Hardware

Two boards. The tracker is a WT32-SC01 Plus (ESP32-S3, 480×320 touchscreen): soldered to it are an EC11 encoder, a PCF8575 expander with the Play, Shift, A, B and 8 track buttons, a TRS MIDI output, LiPo power on an IP5306 and an optional INA226 battery monitor. The synth board is a Teensy 4.1 on a 3 Mbaud UART link: the synthesizer, the only microSD card, the sample bank, optional 8 MB PSRAM and a Teensy Audio Shield Rev D (SGTL5000) for the headphones.

- Architecture, link, wiring diagram, pinout and checks before power-up: [docs/wiring.md](docs/wiring.md).
- Parts list: [docs/bom.md](docs/bom.md).
- Enclosure and power circuit: [enclosure/](enclosure/README.md).

## Build and flash

You need [PlatformIO](https://platformio.org/).

```
pio run -e wt32 -t upload          # tracker (ESP32-S3): build and flash over USB
pio run -e wt32-ota -t upload      # tracker over Wi-Fi: FILE → Wi-Fi firmware… open on the tracker
pio run -e teensy41 -t upload      # synth board: first flash over the Teensy's USB (Teensy Loader)
pio run -e teensy41                # synth board .hex for the Wi-Fi page or /firmware/teensy.hex
pio test -e native                 # core tests on the computer
```

- **First flash of the Teensy** goes over its USB with Teensy Loader (before the enclosure is closed); later the synth board is updated from the tracker (Wi-Fi page or FILE → SYSTEM → Update synth).
- The ESP partition table is custom (`partitions.csv`): two ~8 MB firmware slots (`app0` / `app1`); the old `samples` partition is gone. After switching to it, flash once over USB (`pio run -e wt32 -t upload`): OTA does not change the partition table. Wi-Fi settings (NVS) are kept — `nvs` and `otadata` are at the same addresses.
- Teensy program flash: firmware from 0x60000000, OTA staging area at 0x60100000, bank (wavetables, sample starts) from 0x60200000.

Service environments:

| Env | What for |
|---|---|
| `teensy41-bench` | voice pool bench on the Teensy: results over USB serial and the link log; do not save projects from this build |
| `teensy41-desk` | desk mode: a bare Teensy on a computer, the link over USB serial, sound on USB audio; drive it with `scripts/link_desk.py hello\|notes\|status\|fs\|selftest` |
| `wt32-echo` + `teensy41-echo` | link wiring check: echo test between the boards |

## Structure

| Folder | Contents |
|---|---|
| `lib/core` | hardware-independent code shared by both boards: model, sequencer, fx, scales, Euclid and Fill, file format, MIDI parser, synthesizer (CHIP, SAMPLE, FM, DRUM, SYNTH, filter, delay, reverb, compressor), WAV parser, sample bank, sample store and streaming (`stream_pool`, `stream_loader`, `stream_sched`, `sample_store`, `sample_heads`, `psram_cache`), project sample list, presets; link: `link_frame`, `link_msg`, `link_fs`, `state_mirror`, `time_sync`, `bank_ops`, `render_link`, `wave_peaks`, `ihex` |
| `lib/flasherx` | FlasherX (vendored, with its license): writing the Teensy's flash for OTA |
| `src/engine` | sequencer task on core 0 (timer, MIDI output) |
| `src/link` | link client on the ESP: UART task, synth events and state, sample bank commands, synth board firmware update |
| `src/hw` | input (encoder, buttons), MIDI UART, card access, display configuration |
| `src/ui` | UI screens |
| `src/storage` | saving and loading projects and presets; `remote_fs` — the Teensy's card as an `fs::FS` |
| `src/net` | Wi-Fi, firmware web page, OTA |
| `src/teensy` | synth board firmware: link server, audio output, sample store and streaming from the card, PSRAM, bank in flash, card file server, render, preview, firmware update, bench |
| `test` | Unity tests of the core |
| `docs` | manual, wiring diagram, parts list and development plans |
| `enclosure` | enclosure: OpenSCAD model and STL |

## License

Copyright © 2026 deemonoise.

- **Firmware, tests, scripts and documentation** — [GNU GPL v3](LICENSE) (GPL-3.0-only). You may use, change and share them, sell devices with them included, as long as the source of the firmware you ship (with your changes) is published under the same license.
- **Hardware** — the enclosure ([enclosure/](enclosure/): OpenSCAD sources and STL) and the wiring diagrams ([docs/img](docs/img)) — [CERN-OHL-S v2](enclosure/LICENSE) (CERN-OHL-S-2.0): products made from them must make their modified design sources available under the same license.

The name **D-TRK** and its logo are not covered by these licenses: a modified firmware or a device built from this project may say it is based on D-TRK, but must not be called or sold as D-TRK without permission.

Full texts: [LICENSES/](LICENSES/). Third-party libraries (ESP-IDF, Arduino-ESP32, LovyanGFX, Teensyduino, Teensy Audio Library, SdFat, FlasherX, Unity) keep their own licenses.
