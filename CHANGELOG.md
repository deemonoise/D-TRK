# Changelog

Built from the git history. Russian: [CHANGELOG_ru.md](CHANGELOG_ru.md).

## 2026-10-28
- **DICE phrases** ([manual](docs/manual.md#dice)): a Length row (AUTO / 16 / 32 / 64 / 128; AUTO = 64 for
  melodic styles, 32 for groove ones), kept on the device; all four patterns share it. Parts are built as
  four-bar phrases A A′ A B: chords with led inversions, added notes by style and a push in the last bar; bass
  walks to the next root; arp turns in bar 4; the lead answers in B and ends on the tonic or fifth, and plays
  in MAIN too (simpler, quieter); drums vary per bar with a mini-fill at the phrase end. Variety 0 keeps the
  variations but fixed.

## 2026-10-27
- **DICE by genre** ([manual](docs/manual.md#dice)): the DICE button / a long press on PROJ opens a window
  instead of the menu: Genre (HOUSE, TECHNO, TRANCE, DNB, BREAKS, HIPHOP, IDM, AMBIENT, SYNTHWAVE), Style
  (36 styles, each a recipe of BPM, scales, groove, kits, presets, progressions, bass / arp / lead parts),
  Variety 0..100 (0 = canon), Scope ALL / TEMPO / SOUNDS / PATTERNS, Song ON / OFF; ROLL (the window stays,
  ROLL again = a new take), UNDO, CLOSE. Settings kept on the device. Six roles on tracks 1–6 (DRUMS, BASS,
  CHORDS, LEAD, ARP, FX); a role the style lacks leaves its track empty and muted. Four patterns from the
  edited one: MAIN, FILL, BREAK, DROP (`OVERWRITE Pxx-Pyy?` before notes are replaced); Song ON makes the
  song MAIN ×2, FILL, BREAK, DROP ×2, FILL. One undo step for all of it. File format and link protocol
  unchanged. Sound: the DRUMS kit is built per style lane by lane (FM drums first in most styles, 808 / 909
  where the style is built on them) with levels (hats 7–8 dB under the kick) and macro offsets; role volume
  and pan per style; melodic presets softened (filter, resonance, env, drive / crush capped, longer pad
  envelopes, sends by role) and soft presets weighted up; drum velocities by beat position with ghost notes,
  Variety adds velocity spread and humanize.
- **More factory presets** ([list](docs/manual.md#presets)): 582 in all, SYNTH 123, FM 141, DRUM 149, KIT 49,
  CHIP 65, STRING 25, LOFI 30 (was 196). New ones are tuned for the DICE styles: deep / acid / psy / reese /
  808 basses, trance / psy / outrun leads, dub chords, drones and pads, plucks, risers and impacts; new DRUM
  categories HOUSE, TECHNO, TRANCE, DNB, BREAKS, HIPHOP, IDM, AMBIENT, SYNTHWAVE; FM drums, perc and an FX
  category; CHIP ARP, STRING PAD, LOFI TEXTURE; a factory KIT per style. Harsh ones sit at lower volume.
  DICE uses them: every style has its own sound list per role and more drums per kit lane, weighted with the
  most typical first, so rolls vary more. Factory KIT lane levels now reach the drum's volume.
- **Renders unified** ([Resample](docs/manual.md#resample), Export, [Save one-shot](docs/manual.md#oneshot), KIT
  bake): one client driver and one readiness check. A playing transport is stopped (toast `STOPPED`, no more
  `STOP FIRST`); a REC take refuses with `REC BUSY` (the synth board too, for every render); the card space is
  checked. One set of toasts: `BUSY`, `NO SYNTH`, `CARD FULL`, `REC BUSY`, `SILENT`, `SAMPLE LIST FULL` /
  `BANK FULL`; done: `RS3 2.1s, 4 CLIPS` (one-shot: `<name> 0.8s`, `, NO COPY` when the library copy failed;
  Export: `, N CLIPS` instead of `CLIP`). One-shot progress shows the time rendered (`SHOT 0.4s`), no jump at the end.
- **Resample is a loop now:** exactly the pattern (Resample track: the track's own length, Pat len at its
  Speed), the delay / reverb tail wrapped into the start (two passes, the first dropped, as Export WRAP), no
  normalizing, no trimming; silence — `SILENT`, nothing added. 60 s cap as before.
- REC SAMPLE: a full sample list is refused before the take (`SAMPLE LIST FULL`), not after it.
- Link protocol unchanged; **Teensy flash needed** (the bank render honours the skip and the cut, REC busy
  check; the unused file target removed).
- **Browsers unified** ([manual](docs/manual.md#browsers)): FILE Load / Import MIDI / Import WAV, the preset
  browser and the wavetable picker (with IMPORT…) are one list component (`ui::FileList`, rules in `file_rules`).
  Row 0 is `< Back` / `< Up (..)` everywhere (presets too, instead of `..`); the encoder wraps (presets used to
  stop at the ends), <kbd>Shift</kbd>+turn pages; <kbd>B</kbd> / a long press goes up a folder with the cursor on
  the folder left and closes at the top (Load used to close at once; preset Load: cancel at the top only). Tap:
  folders and Back open at once, a file's first tap selects (WAV / WT import also play it, presets audition),
  a tap on the selected one takes it. Play previews in WT import too. Long names are cut with `~`; header: title
  (`LOAD`, `IMPORT WAV`, `SAVE CHIP`…) + path tail + SD status; an empty folder shows `EMPTY FOLDER` in the list
  (a toast only at the top). Each browser reopens in its last folder on its last row (presets per instrument
  type, Load / Save apart); a folder gone — the top. 4 folder levels everywhere (MIDI too); names with `?` are
  never listed. File format and link protocol unchanged, no Teensy flash.
- **SONG: slot TRN and BPM.** A second SONG view (<kbd>Shift</kbd>+click outside an edit, or menu →
  **View**): `01  P03  +5  128 … FOL`. **TRN** −24…+24 semitones: while the slot plays, the tracks' notes and
  the scale root move (CHD, NRN, GEN, MUT in the new key, MIDI tracks too), a note past 0–127 does not sound;
  KIT is never transposed, and a track can opt out with TRACK → MAIN → **Song trn** OFF. **BPM** 20–300 or
  `---` (the tempo stays): the live tempo from the slot's start; Stop and LOOP bring back the project BPM, which
  the song never changes (PROJ shows and saves it; the status bar shows the live tempo, delay / LFO TEMPO /
  PERF follow it). Edited like FOL (<kbd>Shift</kbd>+turn: an octave / ×10), undo, Add / insert / delete move
  the fields; view 1 marks `Pxx` with a dot, the header shows `T+5 B128` while playing. A song Export follows
  the slot tempos. File: a new SSLT chunk (older firmware skips it); link protocol unchanged, no Teensy flash.
- Manual, limits: 32 voices, up to 16 heavy, and the load is capped by the voice cost budget (on overload the
  oldest voices fade out); the SYNTH, STRING, LOFI and built-in sound sections say the same.
- Status bar BPM edit: while a slot's BPM holds, it is live only (until the next slot with its own BPM, LOOP or
  Stop; the project BPM stays, PROJ edits it); otherwise it edits the project BPM as before. The <kbd>Shift</kbd>+click
  cancel brings back the tempo from before the edit, and only after a turn.
- GRID: GAT value with <kbd>Shift</kbd> moves ±10 % of the shown value (to the nearest GAT value in the turn's direction) instead of ×10 raw units; the piano roll's A + turn steps the same way.
- GRID **piano roll**: a third view (<kbd>Shift</kbd>+click cycles Overview → Detail → Roll, or menu → **Piano
  roll**) shows the current track as a horizontal roll — keys on the left (root accented, out-of-scale ones greyed:
  white keys darker, black keys lighter), each octave's root row a tinted stripe across the grid, 16 semitones × 16 steps, paged after the cursor; a crosshair on the cursor's row and column. Notes are bars
  as long as their gate (GAT, else Def gate), cut by the next note, as bright as their velocity; CHD notes as
  outlines. A places the row's note (a note step keeps fx and velocity, an empty one plays Def gate), A + turn
  moves the GAT value as the grid's GAT edit does (±1, <kbd>Shift</kbd> ±10 % of the shown value; 1..200; Def gate removes GAT; toast `GATE n`) (`NO FX SLOT` when all slots are taken), hold A + B clears the step; one undo step per A press. KIT
  tracks show their 8 lanes, A toggles the lane. File format and link protocol unchanged.
- GRID: clearing a step is **hold A + B** in every view (Overview, Detail, Roll, KIT tracks), replacing
  Shift+A (Shift+A now does nothing in GRID). With a selection it clears the selection. One undo step; after
  A + turn the press and the clear are one step (B while holding A used to cancel the edit). With PERF on,
  A then B without a turn still toggles the XY pad.
- Undo is only <kbd>Shift</kbd>+<kbd>B</kbd> (and the menu's **Undo**): in GRID (all views, the roll too)
  <kbd>Shift</kbd>+encoder long press and <kbd>Shift</kbd>+long tap no longer undo and do nothing.
- **REC -> SD** from MIX: the **REC** button in the MAIN column (or an encoder long press on MIX) records
  straight to `/samples/rec/RECn.wav` on the synth card, no import, up to 4 GB or the card's space. Records on
  in the background on any tab (status bar `● REC 03:41`, tap = back to it); stop with STOP / B / long press.
  Toast `REC12.WAV 3:41 SAVED`. REC settings (Source, gains, Thresh, Mon) are now remembered after power-off.
  Link protocol 18: flash both boards.
- REC window (SAMPLE and REC -> SD): a big touch **START / STOP** button below the rows replaces the REC row
  — a silent tap for mic takes (red outline START / ARM, ARMED blinks, filled STOP with the time, grey
  SAVING). An encoder long press in the window also starts / arms / stops; B before a take closes it.
  RESAMPLE with the transport stopped: START waits for Play (`ARMED: PLAY` blinks), and Play starts the
  take just before the transport, so the first note is in it; a tap while waiting = back to READY.
- REC window Cancel row: during a take, an arm, a start or a wait for Play it drops the take (DROP THE
  TAKE / CANCEL ARM, the file deleted) or ends the wait (CANCEL WAIT) and the window stays open in READY;
  in READY it reads CLOSE and closes the window.
- REC window: STOP (the button, B, a long press, or a take that ends itself) saves with its toast and leaves
  the window open in READY for the next take (SAMPLE still binds the take to the instrument). B in READY and
  leaving INST close it.
- REC window: an A tap (pressed and released with no turn) starts / arms / stops like the big button; A's
  usual meaning (marker step, Shift+A SCENE view) is off while the window is open.
- FILE → SAMPLES → Import WAV… and FILE → Import MIDI…: B in a subfolder goes up one folder (the cursor on
  the folder you left); at the top it closes the list as before.
- **MUT**: EVO and VAR are one mutator per track — the TRACK page **MUT** (tabs MAIN INS FOL NOTE GEN MUT
  MIDI) replaces EVO and VAR. The **Mut** row picks OFF / EVO / CA1D / CA2D (KIT tracks only) / MARKOV; only
  the chosen mode's rows are shown, the header shows its state. The modes no longer stack. Old projects with
  both EVO and VAR on open as VAR; CA2D on a melodic track opens as CA1D (same rule). TEVO marks EVO settings kept outside MUT EVO with mask
  bit 0x80 (older firmware ignores the bit).
- GRID menu: one **Capture live** replaces Capture gen / evo / var: writes what the track plays (MUT, then
  GEN) into the pattern (undo), then GEN turns OFF and MUT goes on from a fresh state — no jump in the sound. Undo brings back the pattern
  and GEN.

## 2026-10-26
- DRUM: a machine is picked by group and model — the **Machine** row (KICK, SNARE, TOM, CLAP, RIM, HAT,
  CYMBL, PERC) and **Model** (808, 909, RES, SYN, MODAL, MULTI, RING, CLAVE, BELL). Machine numbers in
  files are unchanged. The BDF machine is gone (FM KICK covers it): an old file opens it as BDA; the BDF and
  BDF ZAP presets are removed, the ANALOG kit has BDS DIRT instead of BDF ZAP.
- DRUM sound: every 808 / 909 machine is calibrated against recordings of the real ones (pitch, sweeps,
  decays, partials, noise bands, clap timing). Default hits peak at about −1 dBFS (kicks ~+7 dB louder);
  the output soft clip is always on, DRIVE is the gain into it. Renamed: SD9 TONE → TUNE, TOM9 NOISE →
  TUNE, CY9 HP → TUNE (as the 909's Tune). HH8 DECAY goes from closed (τ 5–40 ms over 0–51) to open
  (160–660 ms over 76–127); CY9 RIDE/CR blends ride to crash. RS9 was wrong (1700 / 3400 Hz), now 220 + 460
  Hz. New defaults for TOM8/9, CP8/9, RS8/9, CL8, CB8, HH8, HH9, CY9: on most percussion 64 is the original
  sound. RES / MODAL / SYN / MULTI / RING (BDA…HHR) are louder too, with the soft clip; HHR no longer loses
  its partials at high notes; CPX claps decay 0.05–0.3 s, SPREAD also sets the tail's attack (2–100 ms).
  The file format is unchanged, but projects and presets with DRUM sound different.
- CHIP: C64 SID and AY-3-8910 / YM2149 sounds. New waves **SID TS, SID PS, SID PT, SIDPTS** (SID combined
  waves, quieter than PULSE; SID PS gets louder with DUTY), **SID NZ** (SID noise), **AY SAW, AY TRI** (the AY
  envelope as an oscillator), **AY NZ** (AY noise), **AY TN** (tone AND noise); Duty and PWM are active on
  the ones with a pulse in them (SID PS / PT / PTS, AY TN). New CHIP row **Step** OFF / ON: the volume envelope moves in the 16 AY
  steps (1.5 dB apart). New filter **SID** for every INT type: a 6581-style 12 dB/oct low-pass, 200 Hz …
  12 kHz, slightly dirty, never self-oscillates. Factory presets C64 LEAD, ZX ARP, AY TN, C64 BASS, ZX BUZZ,
  SID NZ, AY HAT. The SID / AY models are our own approximations, no emulator code or data. Link protocol
  16: flash the WT32 and the Teensy together.
- Save one-shot also copies the render to the synth board's card as `/samples/oneshots/<folder>/<name>.wav`
  (taken name → `<name>_1`, `_2`, …), so other projects can import it; the project sample is added as
  before. Folders by instrument: DRUM `<model>/<type>` (`909/BD`, `808/SD`, `MODAL/RS`, `CLAVE/PERC`…), FM
  `FM/<machine>`, SYNTH `SYNTH/<osc 1>`, CHIP `CHIP/<wave>` (`CHIP/SID_PS`), STRING `STRING/STRING` /
  `STRING/MODAL`, LOFI `LOFI/BYTE` / `LOFI/APC`, SAMPLE `SAMPLE`. Toast `SAVED, NO COPY` when the copy fails;
  the project is now marked modified after Save one-shot.
- KIT: **PRESET** opens **Load** / **Save one-shot…**. On a KIT Save one-shot bakes every INST lane (not
  SAMPLE, not a lane pointing at a KIT or a SAMPLE instrument, not a lane sharing its note with an earlier
  lane): the kit is rendered dry (no insert) and not normalized (the kit keeps its balance) at the lane's
  note into a new sample `<lane instrument>_N`, copied to that instrument's folder, and the lane switches to
  SAMPLE with it, Pitch 0, Volume 127 (as loud as the instrument), Decay full (Note kept). Rows Hold, Vel; header `ONE-SHOT KIT <name>`, progress `LANE i/n`; silent
  lanes stay, CANCEL keeps the lanes already baked. Toasts `NO INSTR LANES`, `SAVED n LANES` (`, NO COPY`),
  `SILENT`. Link protocol 17: flash the WT32 and the Teensy together.

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
