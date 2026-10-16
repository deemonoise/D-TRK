# Changelog

Built from the git history. Russian: [CHANGELOG_ru.md](CHANGELOG_ru.md).

## 2026-10-15
- SYNTH: wavefolder and ring mod; filters LADDER, MS20, COMB, FORMANT on every type.
- New instrument type MACRO: 8 oscillator models with Timbre / Color.
- New instrument type STRING: plucked string and modal resonator (models STRING / MODAL; Damp, Bright,
  Struct, Position, Excite); 6 factory presets.
- Track insert chain (drive, crush, trance gate) instead of per-voice drive / crush; old projects migrate.
- Drive models; presets carry a recommended insert.
- Trance gate and envelope follower per track (TRACK -> FOL).
- Master fx: MOD (chorus / ensemble / flanger / phaser), delay modes (digital / tape / ping-pong),
  shimmer reverb, master EQ, PERF MOD throw and tape wobble.
- ESP32-S3 audio path optimised; internal RAM freed (UI in PSRAM, Wi-Fi works again). Main branch only:
  with the Teensy synth board the ESP renders no audio.
- BANK: Length 128 in the pattern menu.
- Teensy synth board: voice pool by cost: each note is priced at note-on, the sounding voices stay
  under ~65 % of the CPU (the oldest fade out first); the CPU guard stays the backstop.
- Microtonal project tuning (PROJ -> SONG: Tuning, Tuning root): 12-TET (default), 10 factory tunings
  (just, Pythagorean, meantone, well temperament, 19/24/31-EDO, slendro, pelog, Bohlen-Pierce) and your own
  Scala `.scl` files from `/presets/TUN`. Applies to melodic INT voices; MIDI out stays 12-TET. The tuning is
  saved in the project; link protocol 8, flash both boards.
- Scenes and crossfader (Octatrack style): 8 scenes per project locking per-track VOL, PAN, insert, sends and
  filter plus 14 master parameters; an A↔B crossfader on MIX (tap / drag, encoder), the SCENE view for editing
  (long tap on the bar), A + turn in PERF. The morph runs on the synth board, smoothed over ~30 ms; a p-lock
  beats the scene. Scenes and the fader position are saved in the project (`SCEN` chunk); link protocol 9,
  flash both boards.
- Shift-register generator per track (TRACK -> GEN): a looping 2–16-bit register with a Flip chance plays
  notes of the project scale live, the pattern stays as it is. PITCH mode (pitch of the step notes) and GATE
  (the rhythm too, Density); Low, Range, Seed. FLP fx sets Flip per step; live Flip from a track button (the
  VOL → PAN → FLIP cycle on an encoder click); Capture gen in the GRID menu writes the loop into the pattern.
  Saved in the project (`TGEN` chunk); ESP firmware only, the link protocol is unchanged.
- Your own wavetables: SAMPLE -> To wavetable… makes a 64 × 256 table from a sample's Start..End region
  (CYCLE — one period per frame with pitch detection, WINDOW — 64 equal windows; Norm FRAME / TABLE, Seam),
  computed on the synth board; DRAW… in the table list — a touch keyframe editor (SIN / TRI / SAW / SQR,
  SMTH, linear blend between keys). The result is `/wavetables/<name>.wav` and a project table; link
  protocol 10 — flash both boards.
- Song / pattern export to WAV with stems (FILE -> Export…, formerly Render WAV): the master and optionally
  dry INT track stems and the MOD / DLY / RVB returns in one render, all files one length; 24-bit (or
  16-bit with TPDF dither); Loops 1–16, Tail AUTO / OFF / 2 / 4 / 8 s / WRAP (seamless loop). Files go to
  `/export/<project>/<P01 | P01x4 | SONG>/` and download from the Wi-Fi page (Export section, resumable).
  Without PSRAM on the synth board, passes of 6 files. The old STEMS mode and `/samples/render/*.wav` are
  gone; link protocol 11 — flash both boards.

## 2026-10-14
- CPU guard no longer steals voices on a single late block.
- Due events are sent before a step is planned (no late notes).
- README: overview only, hardware details in wiring docs.
- Teensy synth board: sample streaming from SD, PSRAM on CS0 only, voice ceiling benches.

## 2026-10-13
- PERF: punch-in effects on the master mix (repeat, roll, reverse, scratch, tape stop, gate, filter,
  crush, downsample, drive, delay / reverb throw, freeze); DJ filter moved into PERF.
- PERF on the SONG tab, Shift+Play starts the song; INA226 found at 0x40-0x4F.

## 2026-10-12
- External PCM5102A DAC and headphone amp; all buttons on PCF8575.
- Enclosure: 4x4 key grid, 30 mm knob, DAC / amp bay, phones jack.
- LFO FREE trig mode and LFO step fx.
- INA226 battery gauge, FILE -> SYSTEM, bill of materials.
- SONG tab (128 slots with mute masks) replaces chain song mode.

## 2026-10-09
- Project master volume (saved) and device Phones level; Shift+click cancels an edit.
- Factory KIT presets.
- FILL: arp generator (10 modes, 42 patterns).
- LFO Retrig ON / OFF, more LFO destinations, LFO-to-LFO.
- Enclosure clamps v2.

## 2026-10-07
- KIT drum track: lanes, choke, GRID lane view, Euclid lanes, MIDI import to lanes.
- SD: steadier bus, recovery after a stuck transfer.
- Sidechain key track bypasses the master compressor.

## 2026-10-06
- Internal synth: CHIP, sampler, FM (8 machines), DRUM (16, 808 / 909), SYNTH (BL oscillators,
  wavetables); filter, LFO, factory presets.
- Sample editor with slices, project samples, send delay, 6 fx slots per step.
- 16 tracks (A / B halves).

## 2026-10-05
- Wi-Fi transfer and OTA, 8 track buttons, internal audio, FM machines.

## 2026-10-01 - 2026-10-02
- MIDI tracker for the WT32-SC01 Plus: model, sequencer, fx, scales, Euclid, project format,
  MIDI import; engine, input, MIDI out, SD, UI.
- README, manual, OpenSCAD enclosure.
