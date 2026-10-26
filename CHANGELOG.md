# Changelog

Built from the git history. Russian: [CHANGELOG_ru.md](CHANGELOG_ru.md).

## 2026-10-26
- DRUM: a machine is picked by group and model — the **Machine** row (KICK, SNARE, TOM, CLAP, RIM, HAT,
  CYMBL, PERC) and **Model** (808, 909, RES, SYN, MODAL, MULTI, RING, CLAVE, BELL). Machine numbers in
  files are unchanged. The BDF machine is gone (FM KICK covers it): an old file opens it as BDA; the BDF and
  BDF ZAP presets are removed, the ANALOG kit has BDS DIRT instead of BDF ZAP.

## 2026-10-23
- Variations per track (TRACK → VAR, a new page after EVO): every Every passes a new generation replaces
  what the track plays, the pattern stays. **CA1D** — a Wolfram rule (0–255) over the track's steps,
  generation 0 = the pattern's notes; **CA2D** — Life-like rules LIFE, HIGHLIFE, SEEDS, DAYNIGHT, MAZE on a
  torus of KIT lanes × steps (a melodic track plays it as 1D); **MARKOV** — a transition chain from the
  pattern (Order 1–2, Amount, Rhythm). CA: Reset after N generations (NEVER), back to the pattern on death /
  stasis, Noise. An alive empty step plays a copy of the nearest note to its left, a dead one is silent.
  Another pattern restarts at generation 0. Order pattern → VAR → EVO → GEN. Header: generation, pass count
  and a mini grid of the cells. **Capture var** in the GRID menu writes the heard steps (with EVO's drift,
  undo). From the project Seed: Render repeatable, every live Play different. Dice turns VAR off on the
  rolled tracks. Saved in the project (`TVAR` chunk).
- GEN modes **BYTE PITCH** / **BYTE GATE**: one of the 45 LOFI bytebeat formulas, read over each step's
  window (Zoom: 2^Zoom values of `t` per step, 10 = as LOFI Sync), picks the scale degree; BYTE GATE plays
  the loudest steps first as Density grows. Rows Formula, A, B, C, Zoom (turning Formula loads its
  defaults); Length, Flip, Seed and Register are unused. The `TGEN` record grew.
- LFO wave **BYTE**: a bytebeat formula over one LFO period (`t` 0–65535), stepped output; rows Formula and
  A per LFO (B, C the formula's defaults), `LFW BYT` lock. Saved in the project (`LFOB` chunk); presets v7.
  Older projects and presets load.

## 2026-10-22
- New instrument type **LOFI** (INST, the LOFI page) with two machines. **BYTE** — bytebeat: 45 built-in
  formulas (2011 classics, our own and rhythmic ones), the counter `t` at 8 kHz on C-4 and by note pitch,
  unsigned / signed modes; macros A, B, C (in every formula; turning Formula loads its defaults, a macro the
  formula does not read is gray), TONE (steps → interpolation and lowpass), LOOP (loops `t`). **Sync**: `t`
  from the project tempo (1024 per 1/16 step, the note sets only the octave), LOOP then in steps (1–1024).
  **APC** — Atari Punk Console: an oscillator triggers a pulse of length WIDTH, a long pulse gives
  subharmonics; macros WIDTH, TONE, DRIFT, SMOOTH. ADSR, filter, LFOs, macro locks and Mutate as on the other
  types; BYTE is a heavy voice (within the limit of 8), APC a light one. 18 factory presets (BYTE, APC, SYNC*), your
  own in `/presets/LOFI`; presets v6, the formula is stored in the project (`LOFI` chunk).
- Scenes and crossfader (Octatrack style): 8 scenes per project locking per-track VOL, PAN, insert, sends and
  filter plus 14 master parameters; an A↔B crossfader on MIX (tap / drag, encoder), the SCENE view for editing
  (Shift + A on MIX; big touch buttons, a held track button + turn / click sets / unsets its lock), A + turn in
  PERF (GRID, SONG, MIX, XY pad). The morph is smoothed over ~30 ms; a p-lock beats the scene. Scenes and the
  fader position are saved in the project (`SCEN` chunk).
- A / B + track button remapped: solo is now Shift + A + N, the pattern queue B + Shift + N (everywhere);
  plain A + N / B + N put scene N on crossfader end A / B on MIX and do nothing elsewhere. The old
  B + Shift + N "switch at once" is gone (Shift + tap on a BANK tile still does it).

## 2026-10-21
- Song follow actions (SONG, FOL column): after each pass a slot goes NEXT, PREV, STAY, JUMP to a slot or
  RANDOM, with a chance 0–100 %. Decided from the project Seed (PROJ -> SONG): live play takes a new path at
  every start, Export the same one every time (Max slots limits loops). Old songs play as before (all
  NEXT). Saved in the project (`SFOL` chunk).
- Dice (PROJ header DICE, or a long encoder press on PROJ): one roll gives the project a genre tempo and
  scale, factory sounds on tracks 1–4 (DRUMS kit matching the genre, BASS, CHORDS, LEAD) and their part of
  the heard pattern from the Euclid / CHORDS / BASS / ARP generators; Undo dice takes it back in one step.
  Mutate 15 / 30 / 60 % in the INST PRESET menu nudges filter, envelope and macros at random, with Undo mutate.
- PERF XY pad (A + B on GRID / SONG / MIX with PERF on): the work area becomes a touch pad holding two
  PERF effects, the finger's position sets their depths (list values in zones, FILTER LP / OFF / HP); the
  finger's trail is drawn over the visuals. Axes in PROJ -> PERF (XY pad X / Y, default FILTER / CRUSH),
  saved in the project (`PXY ` chunk). B tap, another tab or PERF off closes it.
- Evolve per track (TRACK -> EVO): the pattern drifts live every 1–16 passes (Evolve 1–100 %), the pattern
  itself stays as it is. Notes move along the scale, hits drop out and ghost notes appear, retriggers and
  velocity change, steps turn or swap (Note / Rhythm / Rtrg+Vol / Shift); Home pulls the drift back. From
  the project Seed like the follow actions: each Play drifts its own way, Export the same every time.
  Capture evo in the GRID menu writes the heard variant into the pattern. Saved in the project (`TEVO` chunk).

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
- Song / pattern export to WAV with stems (FILE -> Export…, formerly Export): the master and optionally
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
