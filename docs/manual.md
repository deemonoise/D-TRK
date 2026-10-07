
# D-TRK — user manual

A tracker with a built-in synthesizer on the WT32-SC01 Plus: 16 tracks, a bank of 16 patterns, a song chain, Fill with Euclid, MIDI file import. Each track plays either the built-in synthesizer (FM, 808 / 909 drum machines, a wavetable synth, a sampler, chiptune) through the board's speaker, or external MIDI. Controls: a touchscreen, an encoder and two buttons, Play and Shift.

Русская версия: [manual_ru.md](manual_ru.md)

1. [Quick start](#start)
2. [Controls](#hw)
3. [Screen and status bar](#screen)
4. [GRID: pattern editor](#grid)
5. [Effects (fx)](#fx)
6. [Fill and Euclid](#euclid)
7. [TRACK: track settings](#track)
8. [INST: instruments](#inst)
9. [PROJ: project and pattern](#proj)
10. [BANK: pattern bank](#bank)
11. [Song mode](#song)
12. [FILE: saving and loading](#file)
13. [MIDI import](#import)
14. [MIDI output](#midi)
15. [Built-in sound](#sound)
16. [Limits](#limits)

<a id="start"></a>

## Quick start

1.  Insert a microSD card formatted **FAT32** with an **MBR** partition scheme and power on the device. Without a card, a demo beat loads on tracks 1–3.
2.  Press <kbd>Play</kbd>. The sequencer sends MIDI Start and plays from the first step. Press again to stop.
3.  In the **GRID** tab, tap a cell, then tap it again to enter edit mode. Press keys on the mini keyboard at the bottom or turn the encoder.
4.  In the **FILE** tab, choose **Save As…**, enter a name and press OK. The project loads automatically at the next power-on.

<a id="hw"></a>

## Controls

| Action | What it does |
|--------|--------------|
| <kbd>Play</kbd> | Start from the first step / stop. When paused, also restarts from the beginning. |
| <kbd>Shift</kbd>+<kbd>Play</kbd> | Pause and resume: stopped → start, playing → pause, paused → resume from the same spot. During playback, pause triggers on releasing Play if it was held for less than 0.5 s. |
| <kbd>Shift</kbd>+<kbd>Play</kbd>, hold | During playback: **fill** while Play is held (steps with [CND FIL](#cnd) play, steps with NFL don't). Releasing after 0.5 s only ends the fill, without pausing. |
| <kbd>Shift</kbd> | A modifier; does nothing on its own. While held, a yellow `SHIFT` replaces the FILE tab. |
| Encoder turn | Moves the cursor or selects a row. In edit mode, changes the value. With Shift, a coarse step (usually ×10). |
| Encoder click | Enter or leave edit mode, choose a menu item. |
| Encoder long press | 0.5 s: context menu or close a dialog. |
| Tap | Select. Tapping the selected item again enters edit mode. |
| Long tap | 0.5 s: context menu. |
| Drag | Scroll. In edit mode, changes the value: up increases, down decreases. |

Play works on any screen, even over an open menu.

<a id="trackkeys"></a>

### Track buttons

In GRID the buttons also record notes live (REC) and hold effects (PERF) — see [REC and PERF](#live).

An optional block of 8 buttons, each with an LED. There are 16 tracks, so the buttons work on the **visible half** — 1–8 or 9–16, the same one [Overview](#grid) shows. The half is not a mode: the cursor selects it. Move past track 8 (<kbd>Shift</kbd>+turn, tapping a track name in Detail, a track button) and the buttons and LEDs switch to 9–16; move back and they return to 1–8. The GRID header shows which half is active: `1-8` or `9-16` on the left above the step numbers.

| Action | What it does |
|--------|--------------|
| <kbd>N</kbd> | Select track N of the visible half (in 9–16, button 1 is track 9): the GRID cursor moves to it, TRACK shows it. |
| <kbd>Shift</kbd>+<kbd>N</kbd> | Mute / unmute the same track (N of the visible half), on any screen. The toast `TRACK n MUTE` / `TRACK n ON` shows the real number, 1–16. |
| Hold <kbd>N</kbd> + turn | Volume of track N (Volume, INT) on any screen, ×10 with <kbd>Shift</kbd>; toast `TRK5 VOL 100`. The turn does not reach the screen meanwhile. The press on its own works as usual (selects the track). Not active in GRID edit mode, REC or PERF — there the buttons play. On a MIDI track: `TRK5 MIDI`, nothing changes. |
| <kbd>N</kbd> in GRID edit mode | Write the project scale degree into the step under the cursor and move the cursor one step down. On an INT track the note sounds immediately if Preview is on in PROJ. Buttons 1–7 are degrees I–VII, 8 is I an octave up. In scales with fewer than 7 notes (pentatonics, blues) the extra buttons continue the scale into the next octave; Chromatic enters major-scale degrees. The octave follows the note under the cursor, or on an empty step, the track's last entered note. |
| <kbd>N</kbd> in the sample editor (INST, SMPL page) | Play a slice to the end: 1 — the selected slice (if no slice is selected, the first one), 2 — the one after it, and so on. Works with any Slices setting (OFF, NOTE, FX), at the original pitch. Toast "SLICE n"; no such slice — "NO SLICE n"; no slices at all — "NO SLICES". |
| <kbd>Shift</kbd>+<kbd>N</kbd> in GRID edit mode | The same degree an octave up. |
| <kbd>N</kbd> in GRID edit mode on a [drum track](#drumtrack) | Toggle lane N in the step under the cursor. The cursor doesn't move; no Shift needed. |

The LEDs show the same half as the buttons: LED N is track N of half 1–8 or 9–16. A track flashes on each of its notes; a muted track stays dark. The selected track is lit steadily and goes dark on its notes. Activity of tracks in the other half is not shown. Degree entry and slices in the sample editor don't depend on the half — there the button number is what counts. OFF and clearing a step work as before, through the menu.

### Wiring

| GPIO | Function |
|------|----------|
| `10` | MIDI TX (UART, 31250 baud) |
| `11 / 12` | Encoder A / B (EC11) |
| `13` | Encoder button |
| `14` | Play |
| `21` | Shift |
| `43 / 44` | Track buttons: PCF8575 SDA / SCL, Debug connector (TXD0 / RXD0; optional, schematic in the README) |
| `41 / 40 / 39 / 38` | microSD: CS / MOSI / CLK / MISO (built-in slot) |

The buttons short the pin to GND; the internal pull-up is enabled. MIDI OUT is TRS type A, 3.3 V:

- +3.3 V → 33 Ω → Ring (DIN pin 4)
- GPIO10 → 10 Ω → Tip (DIN pin 5)
- GND → Sleeve (DIN pin 2)

A 74HC14 buffer (two inverters in series) is optional. There is no MIDI input; the device is always the clock master.

<a id="screen"></a>

## Screen and status bar

    S03 P05*  >P07  120 BPM  5/16  PLAY  L3  CPU  42%
    tab work area
    GRID  TRACK  MIX  BANK  INST  PROJ  FILE

| Element | Meaning |
|---------|---------|
| `P05` | The pattern currently playing. In song mode — `S03 P05`: third chain entry, pattern 5. `S--` — the chain hasn't started yet. |
| `*` | There are unsaved changes. |
| `>P07` | Queued pattern: switches at the end of the pass. Not shown in song mode. |
| `120 BPM` | Tempo. A tap enters tempo editing (red): encoder ±1, ±10 with Shift, range 20–300. Leave with a click, a tap on the work area or a tab change. |
| `5/16` | Step / pattern length. |
| `PLAY` | State: PLAY, PAUSE, STOP. A tap works like the Play button; with Shift, like Shift+Play. |
| `L3` | Pattern pass counter. The CND condition uses it. While fill is held — `FILL`. |
| `CPU 42%` | Built-in synth load: the average time to compute an audio block over the last 0.5 s, as a % of the block duration (4 ms). Gray below 60%; yellow at 60–84% or if any block in those 0.5 s took longer than 4 ms (the DMA queue headroom covers that); red from 85% or for 2 s after an audio dropout (DMA ran empty — an audible click). While no audio is being computed, the last value is shown. |

Tabs switch only by tapping the bottom bar. Yellow messages (toasts) cover the right part of the status bar for 1.5 seconds.

**Menus** open in the center of the screen and show up to 12 items; long lists scroll by dragging. Encoder: turn to select (wraps around: after the last item comes the first), click to execute, long press to close. All parameter and file lists wrap around the same way. A tap outside the menu also closes it. Gray items are currently unavailable.

<a id="grid"></a>

## GRID: pattern editor

GRID always edits the pattern that is playing. When the pattern changes (queue, chain, loading), the cursor moves to step 1, edit mode turns off and the selection is cleared.

### Two views

- **Overview** (default): 8 columns — one half of the 16 tracks, the one the cursor is in: 1–8 or 9–16. Which half is on screen is shown on the left above the step numbers: `1-8` or `9-16`. The half is not a separate mode: when the cursor moves from track 8 to track 9 (<kbd>Shift</kbd>+turn, tapping a track name in Detail, a [track button](#trackkeys)), the second half is shown; move back and the first returns. The [track buttons and LEDs](#trackkeys) follow the same half. A cell shows the note, a velocity bar and a yellow dot if the step has fx. The dot is gray if all of the step's fx are synth fx on a MIDI track (they have no effect there). Every fourth step is highlighted, and the playback row is marked.
- **Detail**: one (the current) track across the full width, 14 fields: NOTE, VEL and six pairs FX1…FX6 with their values. The track number (`T3`) is above the step numbers, the name above NOTE/VEL. <kbd>Shift</kbd>+turn past the last field moves to the next track; the track buttons switch it too. Tapping the name row mutes (with <kbd>Shift</kbd>, solos) that track. Synth fx on a MIDI track are shown in gray.

Track name color: yellow — solo, gray — not audible, white — normal.

### Encoder

| Input | Outside edit mode | In edit mode |
|-------|-------------------|--------------|
| Turn | Step up/down (wraps around) | Change the field value |
| <kbd>Shift</kbd>+turn | Overview: another track. Detail: another field | Coarse step |
| Click | Enter edit mode (toast with the field name) | Leave edit mode |
| <kbd>Shift</kbd>+click | Toggle Overview / Detail | |
| Long press | Context menu | |
| <kbd>Shift</kbd>+long press | Undo | |

### What a turn changes in edit mode

- **NOTE.** On an empty step, the track's last entered note is placed (C-4 at first). After that the note moves by degrees of the project scale, by octaves with Shift. The encoder cannot clear a step or set OFF; use the menu for that. On an INT track every entered note (encoder, mini keyboard, track buttons) sounds immediately with the track's instrument if Preview is on in PROJ.
- **VEL.** 0–127, ×10 with Shift. A value of 0 is shown as `...` and means "the track's default velocity".
- **FX1…FX6.** Cycles through the commands by group, related ones next to each other: `...`; notes and arp CHD, STR, ARP, ARM, ARS; time RAT, NDG, GAT, TIE, OFF, CUT; randomness PRB, CND, VRN, NRN; pitch and volume SLD, VIB, PBN, VSL, ACC; sound FLT, RES, DRV, DEC, COL, SHP, SWP, CON; sample OFS, SLC; sends DLY, RVB; MIDI only CHN, CCA, CCB, PGM (turning back from an empty slot goes straight to PGM). The order is the same as in the [table](#fx). Choosing a command sets its default value and shows its full name in the status line (for example, VIBRATO). On an empty slot, the first turn places the last fx written to this slot on this track, with the same value — handy for repeating an fx step after step; further turns cycle through the commands as usual.
- **FX VAL.** ±1, ×10 with Shift. For CND the step is the same with Shift. On an empty slot the first turn likewise inserts the track's last fx; if there was none yet, the value doesn't change.

One edit session (from entering to leaving edit mode) is one undo step.

### Mini keyboard

Appears at the bottom of the screen when editing the NOTE field. Twelve keys — the octave of the current note. A yellow bar marks scale notes, a red frame the current note. Tapping a key writes the note — any note; the scale doesn't restrict it here. <kbd>Shift</kbd>+encoder turn changes the octave.

<a id="drumtrack"></a>

### Drum track

A track whose TRACK → Instr is set to a [KIT](#kit) instrument is a drum track: a step holds not a note but a set of 8 lanes (kick, snare, hats…). The step stores a lane mask and one velocity for all of the step's lanes.

- **Overview:** the cell shows 8 small squares; a filled one means the lane sounds. There is no velocity bar; the fx dot is in the top corner of the cell.
- **Detail:** the NOTE field shows the same squares, the VEL field the step velocity (`...` — the track's Def vel).
- **Editing NOTE:** a turn moves the red frame across lanes, <kbd>Shift</kbd>+turn toggles the lane under the frame. Instead of the mini keyboard, the bottom shows a **pad of 8 buttons** with the lanes' sample (or instrument) names; a lit one means the lane is in the step; a tap toggles the lane. [Track buttons](#trackkeys) 1–8 do the same in edit mode. A lane sounds as soon as it's turned on (Preview in PROJ).
- **Editing VEL:** 1–127, ×10 with Shift.
- A step with all lanes turned off stays (silent, fx still work) — remove it with Clear step. OFF is set as usual through the menu and releases all lanes.
- A step's fx apply to all its lanes: RAT repeats each one, NDG shifts them, PRB and CND decide for the whole step, DEC / FLT / DLY and other locks apply per voice. CHD, NRN, STR, TIE, ARP, SLD, VIB and SLC do nothing on a drum track. The **ACC** fx is for drum tracks only.
- **Transpose** skips drum tracks (toast "DRUM TRACK" if the selection contains only them).
- Copy / Paste between drum and regular tracks copies the bytes as is: a lane mask becomes a velocity and vice versa. Likewise, switching a track's instrument between KIT and non-KIT reinterprets the steps already written.
- On a MIDI track each lane goes out as its own note (the lane's Note) on the track's channel — this way a KIT drives an external drum machine (GM map: 36 — kick, 38 — snare, 42 — closed hat…).

### Touch

| Gesture | Action |
|---------|--------|
| Tap a cell | Moves the cursor there. Tapping the cell under the cursor toggles edit mode. Clears the selection. |
| <kbd>Shift</kbd>+tap | Selection: the first tap sets a corner, subsequent taps move the opposite corner. |
| Long tap | Moves the cursor there and opens the context menu. |
| <kbd>Shift</kbd>+long tap | Undo. |
| Tap a track name | Mute. With Shift — solo. |
| Drag | Scrolls rows. Disables follow until the next start. |

**Follow**: during playback, if the cursor hasn't been touched for 2 seconds, the screen scrolls to the playing step by itself. Toggle it in the context menu.

### Selecting with the encoder

Menu → **Select** sets a selection corner at the cell under the cursor. While a selection exists, any cursor movement (turn — by steps, <kbd>Shift</kbd>+turn — by tracks or fields) moves the opposite corner. A long press opens the selection menu, a click clears the selection. After **Copy sel** the selection is cleared: move the cursor away and choose **Paste**. Clear, Note OFF and Transpose keep the selection.

### Context menu

| Without selection | With selection |
|-------------------|----------------|
| Copy step · Paste · Clear step · Note OFF · Select · Copy track · Clear track · Transpose track… · **Fill…** · **Resample track** · **Resample pattern** · **Rec: ON/OFF** · **Perf: ON/OFF** · Detail view / Overview · Follow: ON/OFF · Undo | Copy sel · Paste · Clear sel · Note OFF sel · Transpose… · **Fill…** · Undo · Clear selection |

- **Transpose…** opens a dialog: **Amount** and **Mode** (SCALE — scale degrees, CHROMATIC — semitones), with OK / CANCEL buttons. The range is two octaves up and down (in SCALE that is 2 × the number of scale notes; ±14 for major). <kbd>Shift</kbd>+turn changes Amount by an octave. Long press — Cancel. Affects only steps with a note; without a selection, the whole track; one undo step. It is a shift, not a state: Amount starts at 0 every time the dialog opens; to restore the notes, apply the opposite shift (+12, then −12). Mode is remembered until power-off.
- Paste inserts at the cursor position or at the top-left corner of the selection. It doesn't go past track 16 or the pattern length; the selection is also clipped at track 16.
- The clipboard survives loading another project — so you can move parts between projects.
- If the pattern changed while the menu was open, the action is not performed (toast "PATTERN CHANGED").

<a id="live"></a>

### REC and PERF: track buttons live

Two modes from the GRID context menu. They exclude each other and edit mode: turning one on turns the others off. The header above the step numbers shows `REC` (red) or `PERF` (yellow). They work during playback; when stopped, the buttons behave as usual.

- **REC** — records from the track buttons into the current track. Button N writes scale degree N (in the octave of the track's last note) into the playing step; a press in the second half of a step lands in the next one. <kbd>Shift</kbd>+N erases that step's note. On a drum track button N sets lane N, <kbd>Shift</kbd>+N removes it. Each pattern pass is one undo step. Choose the track to record into by tapping its name (in REC this is not mute) or with <kbd>Shift</kbd>+turn. The recorded note sounds immediately if Preview is on.
- **PERF** — an effect on the current track while the button is held; the press shows its name (`PERF FILTER LOW`). Which effect is on which button is set in [PROJ → PERF](#proj) and stored in the project. Release — the effect is removed. Pressing another button replaces the effect. <kbd>Shift</kbd>+N still mutes.

| Effect | What it does | Output |
|--------|--------------|--------|
| RAT 2 / RAT 3 / RAT 4 / RAT 8 | retrigger (by default RAT 2 is button 1, RAT 4 is button 2) | both |
| ROLL UP | RAT 8 with rising velocity — a "build-up" | both |
| FILTER LOW / FILTER HIGH | FLT 30 / FLT 120 (buttons 3, 4) | INT |
| DELAY MAX / REVERB MAX | DLY 127 (button 5) / RVB 127 | INT |
| CRUSH / DOWNSAMPLE / DRIVE | BIT 100 (button 6) / SRR 90 / DRV 100 | INT |
| SHORT DECAY | DEC 20 — short DECAY macro | INT |
| FADE | fade VSL −16; on MIDI, half velocity (button 7) | both |
| MUTE | track mute (button 8) | both |
| --- | the button does nothing | |

The effect is added to steps as they play: the step's own fx stay, and the same command in the step is replaced. Steps already scheduled ahead (up to an eighth note) play as they were. Stop removes all effects; leaving GRID removes the held one.

<a id="resample"></a>

### Resample

**Resample track** and **Resample pattern** in the GRID menu record the playing pattern with the built-in sound into a new project sample: only the current track, or all audible tracks (respecting mute / solo). The sample is named `RS1`, `RS2`… (the first free name), normalized to −1 dBFS, trailing silence is trimmed, and there is no delay / reverb tail past the end of the pattern. You can then select it in INST (SAMPLE or a KIT lane) and save it with the project.

- Only when stopped (`STOP FIRST`). The pattern is rendered twice (measuring, then recording), each pass roughly in real time — progress `RENDER nn%`; a long encoder press or Play cancels (`CANCELLED`).
- Nothing longer than 60 s is recorded: `SAMPLE CAP`, the sample is still added. No room in flash — `BANK FULL`; the same if the list already has 128 samples.
- The result: `RS3 2.1s`. The render matches playback (without the 14 ms output latency); notes pushed past the end of the pattern by NDG / swing are cut off.

### Undo

8 steps. Each step is a snapshot of the whole pattern (with 16 tracks the snapshot doubled in size, so the depth is 8 rather than 32). It undoes edits in GRID, menu operations, Fill, and also Copy-to, Clear and Length in BANK. TRACK and PROJ parameters, tempo, mute/solo and chain edits are not undone. There is no redo. Load, New and import clear the history.

### Step

- Note: `C-4` = 60, sharps are `C#4`. Octave −1 is shown as `m`.
- `---` is an empty step, `OFF` an explicit note release (ends TIE and the track's sounding note; on an INT track it releases all its voices, including samples).
- Velocity 1–127 or `...` (the track default). Six fx slots; an empty one is `...`.

<a id="fx"></a>

## Effects (fx)

Each step has six slots. If the same command is in several slots, the first one wins. The "Output" column: MIDI — MIDI tracks only, INT — INT tracks only (built-in sound), both — everywhere.

| Code | Value | Output | What it does |
|------|-------|--------|--------------|
| **Notes and arpeggio** | | | |
| `CHD` | tri 7th su2 su4 6th ad9 pwr oct | both | Chord built from the note: up to 4 notes; notes above 127 are dropped. |
| `STR` | 1–50 | both | Strum: the k-th chord note is delayed by k×STR% of a step. |
| `ARP` | 00–FF (xy) | INT | Arpeggio: note, +x, +y semitones in a loop, three switches per step (ARM changes order and speed) — a fast "chiptune" arp inside the sounding note. With CHD in the same step it arpeggiates the chord notes — see below. For an arp on sequencer steps, use ARS. |
| `ARM` | U1…R8 | INT | ARP mode: the letter is the order (U up, D down from the top, B up-down without repeating the end notes, R random), the digit is notes per step (1–8). A turn changes the digit first, then the letter. The default U3 = plain ARP. Does nothing without ARP; holds together with ARP until the next step with a note. |
| `ARS` | U1…R84 | both | Step arp: each note is a separate hit on the sequencer grid (see [below](#ars)). The letter is the order, as in ARM; the first digit is how many steps until the next note (1 — every step, 2 — every other step …, up to 8); the second digit, if present, is the range in octaves (2–4; without it, 1 octave): `U12` — up, every step, 2 octaves. A turn changes the steps first, then the octaves, then the letter. |
| **Time and length** | | | |
| `RAT` | 2–8, 2^…8^, 2v…8v | both | Retrigger: N equal hits within a step. Gate is a fraction of the sub-step, 95% max. After 8 the turn goes to `2^…8^` — hit velocity rises up to the step velocity, then `2v…8v` — falls from it. |
| `NDG` | −50…+50 | both | Shifts the whole step by ±% of the step length. Negative shift works. |
| `GAT` | 1–800% | both | Note length in % of a step. Up to 100% in 1% steps, then 107, 114 … 800%. |
| `TIE` | — | both | The note is not released and holds through empty steps (see below). |
| `OFF` | 0–96 | both | Note off N ticks after the start of the step (NDG included). This step's notes are released no later than that (RAT hits after that moment don't sound, TIE is cancelled; on a step with a note, no earlier than 1 ms), and a hanging TIE on the track is released. On an INT track all the track's voices are released, including samples and DRUM — with the normal release (CUT — without release). On an empty step `OFF 0` = OFF in the note column. |
| `CUT` | 1–96 | INT | Cut the note after N ticks, without release. A tick is 1/96 of a quarter note: a 1/16 step = 24 ticks. |
| **Randomness and conditions** | | | |
| `PRB` | 0–100 | both | Probability that the step triggers, %. |
| `CND` | FST, A:B, FIL, NFL, PRE, !PR, NEI, !NE | both | Condition based on passes and on other conditions (see below). |
| `VRN` | 0–64 | both | Random velocity spread ±N. |
| `NRN` | 1–7 | both | Random note shift by ±N scale degrees. |
| **Pitch and volume** | | | |
| `SLD` | 1–255 | INT | Glide, time ≈ value × 4 ms (4 ms … 1.02 s). |
| `VIB` | 00–FF (xy) | INT | Vibrato: x — rate (x × 0.5 Hz), y — depth (up to ±2 semitones at F). |
| `PBN` | −64…+63 | both | Pitch bend: 8192 + v×128. Doesn't reset by itself; return to 0 with a separate step. On INT — shifts the pitch of the track's voices by up to ±2 semitones. |
| `VSL` | −64…+63 | INT | Smooth volume change over one step: ±64 spans the full range. |
| `ACC` | 00–FF | both | [Drum tracks](#drumtrack) only: a mask of lanes at full step velocity (bit 0 — lane 1); the step's other lanes sound at 60%. On other tracks it is shown in gray and has no effect. |
| **Sound (instrument locks)** | | | |
| `FLT` | 0–127 | INT | [Filter](#filter) cutoff for the step's note, any instrument type. |
| `RES` | 0–127 | INT | Filter reso for the step's note, any instrument type. |
| `DRV` | 0–127 | INT | Drive (overdrive) for the step's note, any instrument type; on a step without a note, for the sounding note. |
| `BIT` | 0–127 | INT | Bitcrusher for the step's note: bit depth from 16 bits (0 — off) down to 2 bits (127). After Drive, before the filter. The constant value is INST → FILT → Bit crush; the step lock replaces it. |
| `SRR` | 0–127 | INT | Sample-rate reduction for the step's note: 0 — off, 127 — each sample is held for ≈ 64 samples (a rough "digital" sound). Together with BIT — classic lo-fi. The constant value is INST → FILT → Downsample. |
| `DEC` | 0–127 | INT | FM, DRUM, SYNTH: the DECAY macro for the step's note (for DRUM — the machine slot with the same number, for SYNTH — SHP1). |
| `COL` | 0–127 | INT | FM, DRUM, SYNTH: the COLOR macro for the step's note (for DRUM — the machine slot with the same number, for SYNTH — SHP2). |
| `SHP` | 0–127 | INT | FM, DRUM, SYNTH: the SHAPE macro for the step's note (for DRUM — the machine slot with the same number, for SYNTH — MIX). |
| `SWP` | 0–127 | INT | FM, DRUM, SYNTH: the SWEEP macro for the step's note (for DRUM — the machine slot with the same number, for SYNTH — DET). |
| `CON` | 0–127 | INT | FM, DRUM, SYNTH: the CONTOUR macro for the step's note (for DRUM — the machine slot with the same number, for SYNTH — SENV). |
| **Sample** | | | |
| `OFS` | 0–255 | INT | Starts the sample at value/256 of the Start–End region. SAMPLE only. |
| `SLC` | 0–31 | INT | Sample slice for the step's note: 0 — the first slice, 31 — the 32nd. SAMPLE with Slices = FX only (see [slices](#slices)). |
| **Sends** | | | |
| `DLY` | 0–127 | INT | Send to [delay](#delay) for the step's note (replaces the instrument's Dly send), any instrument type. |
| `RVB` | 0–127 | INT | Send to [reverb](#reverb) for the step's note (replaces the instrument's Rvb send). |
| **MIDI only** | | | |
| `CHN` | 1–16 | MIDI | MIDI channel for this step: notes, CC, PB, PGM. |
| `CCA` | 0–127 | MIDI | CC with the track's "CC A" number (default 74). |
| `CCB` | 0–127 | MIDI | CC with the track's "CC B" number (default 71). |
| `PGM` | 0–127 | both | Program Change. On INT — the track's instrument: 0 = INS1 … 31 = INS32, above 31 — INS32. Lasts until playback ends; not written to TRACK → Instr. |

<a id="ars"></a>

### Step arp (ARS)

ARP plays the arpeggio notes inside a single sounding note, fast and without a new attack. ARS is an arp on the sequencer grid: each arpeggio note sounds as a separate step note with its own attack, and it works on both INT and MIDI.

- **Which notes.** With CHD in the step — the chord notes; otherwise with ARP (not 00) — note, +x, +y; otherwise — the note itself. A range of 2–4 octaves repeats these notes one, two, three octaves up (C E G → C E G C' E' G' …); a single note without CHD or ARP always spans at least 2 octaves. Notes above 127 are taken an octave lower.
- **How it runs.** The step with ARS plays the first note (U — the lowest, D — the highest across the whole octave range; B goes up and back through the whole range). The track's following steps without a note play the next notes: every step with digit 1, every second step with 2, and so on. You don't need to fill the empty steps — the arp runs by itself.
- **When it stops.** On the next step with a note (a new arp or just a note), on OFF (in the note column or fx OFF), on a pattern change, on stop. Repeating the same pattern doesn't interrupt the arp.
- **Note length** — the GAT of the ARS step (or the track's Gate), relative to the interval between arp notes; a note is always released before the next one. Velocity — as on the ARS step.
- **Fx on arp steps.** The fx of an empty step that an arp note falls on apply to that note (for example, FLT or DLY). ARP, ARM, TIE and STR have no effect in a step with ARS; ARS has no effect on drum tracks.

Example: note C-4, CHD tri, ARS U1, empty steps below and OFF on step 9 — eight sixteenths C E G C E G C E.

<a id="synthfx"></a>

### Synth fx on INT tracks

**ARP + CHD.** If a step has both ARP (not 00) and CHD, an INT track plays a single note and ARP cycles through the chord notes (in the project scale) instead of 0, x, y — in ARM's order and speed. On a MIDI track the chord sounds in full, as before. ARP 00 with CHD is a plain chord.

SLD, VIB, ARP, VSL, OFS, CUT, DEC, COL, SHP, SWP, CON, FLT, RES, SLC, DLY, DRV, RVB, ARM only take effect on tracks with Out = INT. On MIDI tracks they send nothing and are shown in gray. VIB and ARP values are hexadecimal: `47` = x 4, y 7.

- **Step with a note.** The fx apply to that step's note. A new step with a note resets VIB and ARP: to keep the vibrato or arpeggio going, repeat the fx.
- **Step without a note** (empty or OFF). VSL, CUT and ARP act immediately on the track's sounding voices; VIB changes the parameters of the sounding vibrato. SLD glides the sounding voice to the track's last played note. VIB and ARP hold until the next step with a note.
- **SLD on a step with a note:** if the track's voice is still sounding, it glides to the new note without a new attack (legato); otherwise the new note starts from the previous note's pitch.
- **ARP:** each of the three values lasts a third of a step. `00` turns it off.
- **VSL** is counted from the note's velocity; a full rise or fall takes exactly one step (taking the pattern resolution into account).
- **CUT:** tick length is taken from the tempo (tick = 1/96 of a quarter note) and doesn't depend on the resolution. If CUT is shorter than the gate, the note is cut earlier.
- **OFS** works only on SAMPLE and only at the start of a note; with Reverse it counts from the end. If the note plays a slice, OFS counts within the slice.
- **SLC** works only on SAMPLE with Slices = FX and only at the start of a note, like OFS: the first note-on of the step plays the chosen slice at the note's pitch (with a chord or RAT, the other notes play the whole region). Without SLC, or if the number is not less than the number of slices, the whole Start–End region plays. On a step without a note SLC does nothing.
- **DEC, COL, SHP, SWP, CON** (macro p-locks) work only on [FM](#fm), [DRUM](#drum) and [SYNTH](#synth); they are ignored on CHIP and SAMPLE. On a step with a note, the value replaces the instrument's macro for that note; on a step without a note, for the track's sounding note until the next note. The next step doesn't inherit the lock. For TONE and CHORD the envelope decay is fixed at the start of the note: DEC on a step without a note (and LFO on DECAY) doesn't change the length of a note already sounding.
- **FLT, RES** (filter p-locks) work on all types, but only when Filter is not OFF. The value replaces the instrument's Cutoff or Reso: on a step with a note — for that note; on a step without a note — for the track's sounding voices until the next note. The filter envelope, Key track and LFO CUTOFF are added to the lock value.
- **DLY** (delay send p-lock) works the same way: on a step with a note — the send of that step's notes; on a step without a note — the send of the track's sounding voices until the next note. Handy for echo "throws" on individual hits: instrument Dly send at 0, `DLY 127` on the steps you want.

Other fx on INT: RAT, GAT, PRB, TIE, NDG, CHD, STR, CND, VRN, NRN work as on MIDI; PBN and PGM — see the table; CHN, CCA, CCB are ignored.

### CHD chords

tri = 1-3-5, 7th = 1-3-5-7, su2 = 1-2-5, su4 = 1-4-5, 6th = 1-3-5-6, ad9 = 1-3-5-9 — degrees of the project scale. pwr = note + fifth (7 semitones), oct = note + octave. With the chromatic scale, the chord is built as in major from that note.

<a id="cnd"></a>

### CND condition

- `FST` — only on the first pass after start or a pattern change.
- `A:B` — on the A-th pass out of every B. For example, `2:4` — on the 2nd, 6th, 10th… B ranges from 2 to 8.
- `PRE` / `!PR` — the step plays if the last condition on this track (CND A:B / FST / FIL / NFL or PRB) was / was not met. `NEI` / `!NE` — the same for the track to the left (on track 1 NEI never triggers). PRE / NEI themselves don't count as the "last condition": handy for building chains of variations — a step with `1:2`, and a few steps later `PRE` plays on exactly the same passes.
- `FIL` — only while fill is held (<kbd>Shift</kbd>+<kbd>Play</kbd> during playback), `NFL` — only while it isn't. They come after 8:8. Steps already scheduled ahead (up to an eighth note) are decided by the state at scheduling time.
- Passes are counted by the `L` counter in the status bar. It resets on start and on a pattern change.

### TIE

- The next note of the same pitch on the same channel continues the sound without a new attack (legato). Its end is set by its gate or by another TIE.
- A different note starts, and the old one is released 1 ms later (overlap).
- TIE is released by: OFF, mute, a failed CND/PRB on a step with a note, a pattern change.
- TIE doesn't work on chords. With RAT only the last hit is tied.

### Step processing order

CND first, then PRB. If any condition fails, the whole step is silent, including CC/PB/PGM. Then the channel (CHN), then controllers (at the start of the step, NDG included), then notes. Controllers are sent on steps without a note and on OFF too. A muted track sends nothing.

<a id="euclid"></a>

## Fill and Euclid

Quick track filling, like Fill on the Polyend Tracker: notes, velocity or any FX slot on every N-th step, in a Euclidean rhythm or at random. Open it in GRID: long tap on a cell (or long encoder press) → **Fill…**. With a selection, the selection is filled (all its tracks); without one, the track under the cursor for its length (a track's own length is respected). **What** to fill is taken from the column under the cursor: NOTE, VEL or an FX slot (in Detail view); you can change it in the dialog.

| Row | Values | Meaning |
|-----|--------|---------|
| Steps | EVERY N, EUCLID, RANDOM % | Which steps to write to. Steps are counted from the start of the selection. |
| Every / Offset | 1…length / 0…N−1 | EVERY N: steps Offset, Offset+N, Offset+2N… |
| Hits / Length / Rotation | 0…Length / 1…length / ±(Length−1) | EUCLID: hits, rhythm length (repeats), rotation. |
| Density | 0–100 % | RANDOM %: share of steps. |
| Fill | NOTE, VELOCITY, FX 1–6 | Which field to write. |
| Command | any FX command, `...` | For FX: the slot command. `...` clears the slot on these steps. |
| Value | CONST, RAMP, RANDOM | One value, a smooth From→To ramp across the hits, or random within From…To. |
| Value / From, To | note, 1–127 or FX value | For notes, RAMP and RANDOM move through scale notes between From and To (From above To — downward). |
| Mode | OVERWRITE, EMPTY ONLY, NOTES ONLY | OVERWRITE writes to all chosen steps; EMPTY ONLY — only where the field is empty (no note / track velocity / free slot); NOTES ONLY — only to steps with a note. |
| Seed / Reseed | — | Variant for RANDOM. Reseed (or <kbd>Shift</kbd>+click) — a new random one. |

- Steps Fill didn't hit stay as they were. To rewrite a track completely, Clear track first.
- VELOCITY is written only to steps with a note: velocity without a note doesn't sound. Accents are done like this: Fill VELOCITY, EVERY 4, Value 127.
- On a [drum track](#drumtrack) NOTE sets a lane (the **Lane** row, 1–8; by default the lane under the cursor), other lanes are left alone; new steps get the track velocity. VELOCITY writes the step velocity.
- Every change is written to the track immediately and sounds. **OK** keeps the result (one undo step). **CANCEL**, a long press or leaving GRID restore everything as it was.
- Settings are remembered until power-off; they are not saved to a file.

The list works the same as in PROJ: turn — row, click — edit, turn in edit mode — value. A tap selects a row, a second tap edits, dragging in edit mode changes the value.

<a id="track"></a>

## TRACK: track settings

The track is the one under the cursor in GRID. To switch: tap the arrows in the header or <kbd>Shift</kbd>+turn outside edit mode. The mixer for all tracks is the neighboring [MIX](#mixer) tab.

Parameters are on three pages: **MAIN**, **NOTE**, **MIDI**. To pick a page, tap its tab below the header; with the encoder, a turn cycles through the rows and the page tabs (above the first row, framed); a click on the page tabs goes to the next page, <kbd>Shift</kbd>+click to the previous one (the cursor stays on the page tabs).

| Parameter | Values | Default |
|-----------|--------|---------|
| **MAIN** | | |
| Name | up to 8 characters: A–Z 0–9 - \_ space | TRK1…TRK16 |
| Out | MIDI / INT | INT |
| Instr | 1–16 (instrument number and name), INT only | 1 |
| Volume | 0–127, INT only | 100 |
| Mute / Solo | ON / OFF | OFF |
| **NOTE** | | |
| Def vel | 1–127 | 100 |
| Def gate | 1–800% | 50% |
| Pat len | OFF or 1…length−1 (in the playing pattern) | OFF |
| Humanize | OFF, 1–100: each note of the track is randomly shifted in time (up to ±10% of a step at 100) and in velocity (up to ±20) — a "live" feel | OFF |
| **MIDI** | | |
| Channel | 1–16 | track number |
| CC A / CC B | CC number 0–127 | 74 / 71 |
| Program | --- or 0–127 | --- (don't send) |

- **Name:** click to edit. A turn changes the character; <kbd>Shift</kbd>+turn or tapping a character moves the cursor.
- **Program** is sent immediately when changed and at every start. The value is "raw": 0 = the first patch.
- **Out:** MIDI — the track plays to MIDI OUT; INT — to the built-in synthesizer (see [Built-in sound](#sound)). You can switch during playback: sounding notes are released, and the following steps go to the new output.
- **Instr / Volume** work only on INT; on MIDI they are gray. On INT, Channel, CC A, CC B and Program are gray. Def vel and Def gate apply to both outputs.
- **Solo:** if solo is on for at least one track, only the soloed tracks play. Mute takes priority over solo.

<a id="mixer"></a>

### MIX

A separate tab at the bottom of the screen, next to TRACK: tap **MIX**. Eight strips for the half of the tracks where the cursor is (1–8 or 9–16, as in Overview GRID), with the **MAIN** strip on the right.

- **Strip:** name (colored as in GRID: yellow means solo, gray means not audible), the track's Volume fader, a level meter next to it (−48…0 dB post-fader, at MAIN 100%; yellow above −6 dB, a red mark for one second means a full-scale peak), the value, its instrument's sends to delay (`S`) and reverb (`R`) — display only, edited in INST — and the **M** (mute) and **S** (solo) buttons. On a MIDI track the fader is empty and `MIDI` is shown instead of the value.
- **MAIN** — the overall volume of the built-in sound, 0–200%, default 40% (it used to live in PROJ). Above 100% you get up to +6 dB, and loud peaks are soft-clipped (the fill turns yellow). This is a device setting: it is stored in the board's memory one second after a change, and loading a project does not change it.
- **Encoder:** turn — **MAIN** volume; <kbd>Shift</kbd>+turn — half **A** (tracks 1–8) / **B** (9–16), shown as a letter on the MAIN strip. Hold a track button and turn — that track's volume (with <kbd>Shift</kbd>, step ×10); <kbd>Shift</kbd>+track button — mute. Solo is only by tapping S. An encoder click does nothing on MIX; there is no strip selection.
- **Touch:** tap or drag on a fader — volume set by finger position (in 8 px steps; for precise values use the track button and encoder); tap M / S.
- **Scope** across the full width at the bottom: the last ≈ 14 ms of the speaker output, ~20 frames per second. Auto-gain (up to ×32, value at top left) stretches a quiet signal to the full height; the real level is the meter on the right (yellow near the top); `CLIP` means a full-scale sample occurred (shown for one second).
- Track volume without going to MIX: hold its button and turn the encoder (see [track buttons](#trackkeys)).

<a id="inst"></a>

## INST: instruments

32 instruments per project, INS1–INS32 (projects saved before 32 open with INS17–INS32 at their defaults). The instrument for a track is chosen in TRACK → Instr (with Out = INT) or with fx PGM. When you enter the tab, the current track's instrument opens.

Parameters are spread over 5 pages (6 for SYNTH). Below the header are the page tabs: **MAIN**, **ENV**, the type page (**OSC** for CHIP, **SMPL** for SAMPLE, **FM**, **DRUM**; SYNTH has two — **OSC** and **MOD**), **FILT**, **LFO**. The current page is highlighted.

| Input | Action |
|---|---|
| Tap the page tabs | Open the page (cursor on the tabs). |
| Turn | Cycles through the page's rows and the tab strip above the first row (framed when selected). |
| Click / <kbd>Shift</kbd>+click on the tabs | Next / previous page (wraps: after LFO comes MAIN); the cursor stays on the tabs. |
| Tap the arrows in the header | Previous / next instrument (wraps). |
| <kbd>Shift</kbd>+turn outside edit mode | Also changes the instrument. The open page stays the same. |
| **PREVIEW** button / encoder long press | Audition the instrument: note C-4, 0.3 s. Works during playback too. On the SMPL page with Slices = NOTE and a slice selected, that slice plays. |
| **PRESET** button / <kbd>Shift</kbd>+encoder long press | Load / Save menu — [presets](#presets). |
| Turn, click, tap | As in PROJ: row, edit, value. |

The page is remembered when you switch instruments and when you change Type: for example, from FILT of one instrument you land on FILT of the next.

| Page | Rows |
|---|---|
| MAIN | Name, Type, Volume, Transpose, Fine, Mode, Glide, Dly send, Rvb send |
| ENV | Attack, Decay, Sustain, Release, Vel\>Cut, Vel\>Dec and an ADSR graph on the right |
| OSC / SMPL / FM / DRUM | Type parameters: [CHIP](#chip) — Wave, Duty, PWM rate, PWM depth; [SAMPLE](#sample) — sample editor; [FM](#fm) and [DRUM](#drum) — Machine and 5 macros; [SYNTH](#synth) — oscillators and tables |
| MOD (SYNTH only) | Sub, Sub oct, Noise, Env\>Shp, Env atk, Env dec |
| FILT | Drive, Bit crush, Downsample, Filter, Cutoff, Reso, Flt env, Flt attack, Flt decay, Key track — [drive, lo-fi and filter](#filter) |
| LFO | LFO (1–4), Wave, Sync, Rate, Depth, Dest |

### MAIN and ENV

| Parameter | Values | Default |
|---|---|---|
| Name | up to 8 characters, edited like a track name; empty becomes INSn | INS1…INS32 |
| Type | FM / SYNTH / DRUM / SAMPLE / CHIP / KIT | FM |
| Volume | 0–127 | 100 |
| Transpose | −24…+24 semitones | 0 |
| Fine | −50…+50 cents | 0 |
| Mode | POLY (up to 4 voices per track) / MONO (one voice, legato) | POLY |
| Glide | OFF, 4–1020 ms; MONO only | OFF |
| Dly send | 0–127, all types | 0 |
| Rvb send | 0–127, all types (for KIT — for the whole kit) | 0 |
| Vel\>Cut (ENV) | −64…+63; gray without a filter | 0 |
| Vel\>Dec (ENV) | −64…+63; FM, DRUM, SYNTH | 0 |
| Attack / Decay / Release (ENV) | 0 ms … 10 s (exponential scale, 128 values) | 0 / 18 ms / 9 ms |
| Sustain (ENV) | 0–100% | 79% |

**Dly send** — how much of the instrument's sound goes to the shared [delay](#delay) (the dry sound does not get quieter). For individual steps, use fx [DLY](#synthfx).

**Rvb send** — send to the shared [reverb](#reverb); for steps, use fx RVB.

**Velocity.** Vel\>Cut shifts the filter cutoff by note velocity: at +63 a note with velocity 127 is opened ≈ 6 octaves higher, and one with velocity 1 the same amount lower; velocity 64 changes nothing. Vel\>Dec shifts the DECAY macro (for SYNTH, the first macro, SHP1) the same way by ±63 steps. 0 means off.

**Glide** — portamento between overlapping notes in MONO (legato). For individual steps there is fx [SLD](#synthfx).

**The ADSR graph** on the ENV page follows the values: the widths of the attack, decay and release segments are logarithmic in time, the height of the plateau is Sustain. If the type has no envelope (FM drums, DRUM), the graph is gray, like the rows.

<a id="chip"></a>

### CHIP (OSC page)

| Parameter | Values | Meaning |
|---|---|---|
| Wave | PULSE, TRI, SAW, NOISE, METAL, WT1–WT16 | Waveform. NOISE is noise (higher note — brighter, lower — darker), METAL is "metallic" noise with a tone at the note's pitch, WT1–WT16 are wavetables built into the firmware. |
| Duty | 1–99% | PULSE duty cycle. |
| PWM rate | 0–127 | Speed of the duty cycle sweep (0 — off). |
| PWM depth | 0–49% | Sweep depth. |

Duty, PWM rate and PWM depth are active only for PULSE; for other waves they are gray.

<a id="sample"></a>

### SAMPLE (SMPL page)

**A sample plays to the end.** On pattern tracks SAMPLE ignores note off: GAT and TIE are not needed, the sample (or slice) plays to the end of its region, and with a loop — forever. The envelope works (Sustain 0 silences it after Decay). The sound is stopped by: OFF in the note column, fx `OFF` and `CUT`, stop, and **choke** — a new sample note on this track silences its previous samples within 3 ms. Notes of the same step (chord CHD, STR) do not choke each other; a repeat of the same note (RAT) does. In MONO the next note always restarts the sample from the beginning; legato (continuing from the same position) is only possible with SLD; Glide does not work for samples. Auditioning (Preview, the editor) still cuts off on release.

The SMPL page is the sample editor: a large waveform with markers at the top, a button strip below it, and below that a list of rows (2 rows visible, the list scrolls).

| Parameter | Values | Meaning |
|---|---|---|
| Marker | `S`, `E`, `L` or `#n` and position in % | The selected marker (see below). Click to edit: turning moves the marker. |
| Sample | --- or a project sample | Steps through the [project samples](#samples) (FILE → SAMPLES); samples from other projects are not visible. Selecting a sample sets its Root and Loop and erases the slices. A red name means the sample is unavailable (MISSING or removed from the project): the voice is silent, and after importing it again under the same name it plays again. |
| Root | C-m…G-9 | The note at which the sample plays at its original pitch. With Slices = NOTE, this note plays slice 1. |
| Start / End | 0–100%, step 0.1% | The played region of the sample. Start cannot exceed End. |
| Loop | OFF, FWD, PING | No loop, forward loop, ping-pong. With a loop the sample plays until it is stopped (see below). |
| Loop start | 0–100% | Loop start as a fraction of the Start–End region (loop end = End). Gray with Loop OFF. |
| Reverse | ON / OFF | Play backwards; the loop is mirrored. |
| Slices | OFF, NOTE, FX | How [slices](#slices) are played: OFF — not played (the whole region, as before), NOTE — by notes from Root, FX — with the `SLC` command. |
| Chop | EQUAL, TRANS | Auto-slicing method for the CHOP button: equal parts or by transients (attacks). |
| Chop N / Sens | 2–32 / 0–100% | With EQUAL the row is called Chop N — the number of parts (default 8). With TRANS it is Sens, the sensitivity: 0% — strong attacks only, 100% — all detected ones (default 50%). |

**Waveform:** the Start–End region is light, everything outside it is gray. Markers: green Start, red End, yellow Loop start (only if Loop is not OFF), cyan slices numbered 1, 2, 3… (labels that don't fit are skipped). The selected marker is thicker. In the top right corner is the zoom: `x1`, `x2`, `x4` … and `1:1` at maximum (one sample per pixel). Without a sample — `NO SAMPLE`; if the sample is unavailable — a red `MISSING`.

| Button | Action |
|---|---|
| **\<** / **\>** | Select the previous / next marker by position on the waveform (Start, End, Loop and slices mixed, in the order they sit). |
| **−** / **+** | Halve / double the zoom. Centered on the selected marker. |
| **CHOP** | Re-slice the Start–End region according to the Chop and Chop N / Sens rows; old slices are erased. EQUAL — Chop N equal parts. TRANS — the first slice at Start, the rest on attacks, up to the 31 strongest. After chopping, slice 1 is selected. |
| **CLR** | Tap — delete the selected slice (selection moves to the previous one). Long press — delete all slices. |

| Input on the waveform | Action |
|---|---|
| Tap | Select the nearest marker (within ~16 px). |
| Horizontal drag of a marker | Move it. With <kbd>Shift</kbd> the marker snaps to the nearest transient. |
| Drag on empty space | At zoom above x1 — scroll the waveform. |
| Long tap | New slice at that point (with <kbd>Shift</kbd> — at the nearest transient). Not added if there are already 32 slices or the point is taken. |
| Marker row in edit mode: turn | Move the selected marker by 1 pixel at the current zoom (at 1:1 — by one sample); the waveform scrolls to follow the marker. |
| Marker row in edit mode: <kbd>Shift</kbd>+turn | Jump to the next / previous transient. |

- The Marker row is the first on the SMPL page, Chop N / Sens the last: turning wraps around through the tabs; for other pages click on the tabs.
- A slice can only move between its neighboring slices; it cannot overtake them. Slices are stored as a fraction of the whole sample and do not depend on Start / End.
- Transients (attacks) are detected automatically from rising loudness, no closer than 40 ms to each other. They are used both for <kbd>Shift</kbd> snapping and for Chop TRANS.
- Changing the sample (Sample row) erases the slices.

<a id="slices"></a>

### Slices: playback

Up to 32 slices per instrument. Slice n plays from its marker to the next slice; the last one plays to End (if End is to its left — to the end of the sample). While there are no slices, NOTE and FX sound like OFF.

- **NOTE:** the Root note plays slice 1, Root+1 plays slice 2 and so on (slice number = note − Root + 1). Notes below Root and above the last slice are silent. The slice plays at its original pitch (Transpose and Fine work) — handy for laying a break out across the keys.
- **FX:** notes play the whole Start–End region at normal pitch, and fx [SLC](#fx) on a step picks the slice for that step's note: `SLC 0` — slice 1, `SLC 31` — slice 32. The slice plays at the note's pitch (like a regular sample from Root). Without SLC, or with a number not less than the slice count — the whole region.
- You can audition slices with the [track buttons](#trackkeys) right in the editor: 1 — the selected slice, 2 — the next one, etc.
- A slice plays once: Loop and Loop start are ignored. Reverse works within the slice (from its end to its start), and so does OFS.

<a id="fm"></a>

### FM

**FM** — 8 machines in the style of Model:Cycles: KICK, SNARE, METAL, PERC, TONE, CHORD, CLAP, HAT. Instead of operators there are 5 macros: DECAY (length), COLOR (brightness / index), SHAPE (timbre variant; for CHORD — the chord type), SWEEP and CONTOUR (pitch or index envelope). Drums play one-shot (note length and ADSR don't matter, a new note cuts off the previous one); TONE and CHORD hold the note (attack / sustain / release from the ADSR). TONE can be POLY; the other machines are mono per track. The LFO is on the shared [FILT and LFO](#filter) pages; for FM it can also drive the macros.

P-lock: fx `DEC`, `COL`, `SHP`, `SWP`, `CON` (0–127) set a macro for the note on their step; on a step without a note — for the sounding note until the next one (see [synth fx](#synthfx)).

| Parameter | Values | Meaning |
|---|---|---|
| Machine | KICK, SNARE, METAL, PERC, TONE, CHORD, CLAP, HAT | The machine. Changing the machine sets its default macro values. |
| DECAY | 5 ms … 4 s | Length: for drums — the sound's decay; for TONE and CHORD — the decay to Sustain (replaces Decay). |
| COLOR / SHAPE / SWEEP / CONTOUR | 0–127 | Meaning depends on the machine, see the table below. |

| Machine | COLOR | SHAPE | SWEEP | CONTOUR |
|---|---|---|---|---|
| KICK | index: from sine to punch and click | "body" (modulator ratio 0.5–2); the upper half adds feedback | pitch drop, up to 48 semitones | drop time 5–200 ms |
| SNARE | tone / noise balance | noise filter: lower half — BP 0.8–4 kHz, upper half — HP 2–8 kHz | tone pitch drop, up to 12 semitones | noise length relative to the tone |
| METAL | index | frequency set: bell → cowbell → gong → cymbal | slight pitch drop, up to 5 semitones | modulator fades faster than the carriers |
| PERC | index: tom → wood, rim, click | modulator ratio 1–7, including inharmonic ones | pitch drop, up to 24 semitones | drop time 5–200 ms |
| TONE | brightness (constant part of the index) | 5 zones: sine, "saw", "square", stack, bell | decaying part of the index (pluck) | its time 20 ms … 2 s |
| CHORD | from sine to saw (feedback) | chord type, 12 zones | decaying brightness boost | its time 20 ms … 2 s |
| CLAP | noise filter frequency (BP 0.6–3 kHz, higher with the note) | number of claps 2–5 and spacing 5–15 ms | filter resonance | tail level relative to the claps |
| HAT | HP cutoff 3–12 kHz | metal / noise balance | metal frequency spread | metal and noise length relative to DECAY |

Base pitch at C-4: KICK 55 Hz, SNARE 180 Hz, PERC 200 Hz, METAL 400 Hz, HAT 3.5 kHz; TONE and CHORD follow the note pitch. Closed / open hat — short and long DECAY (for example, a `DEC` lock).

- **Drums** (KICK, SNARE, METAL, PERC, CLAP, HAT): Attack, Sustain, Release are gray; length is set by DECAY. Always mono per track: a new note restarts the voice without a click (choke) — open and closed hats on the same track cut each other off.
- **TONE and CHORD:** Attack, Sustain, Release come from the shared parameters; Decay is always gray for FM — DECAY replaces it. Mode and Glide work only for TONE; CHORD is mono, the whole chord in one voice.
- **The note** sets the machine's pitch relative to C-4; Transpose and Fine work as for CHIP. PREVIEW plays note C-4.
- The LFO phase resets on every new note except legato: TONE in MONO and CHORD (always mono, overlapping notes play legato).

<a id="drum"></a>

### DRUM (808 / 909)

**DRUM** — 16 "analog" machines in the spirit of the TR-808 and TR-909, with their own synthesis (tones with a pitch envelope, a click, "metal" from 6 squares, filtered noise, drive). The same 5 macro slots as FM and the same `DEC`, `COL`, `SHP`, `SWP`, `CON` locks, but each machine has its own slot names (table below). A slot that does nothing is shown as a gray "-". All values are 0–127; DECAY is a number too — its range differs between machines.

- All machines are one-shot and mono per track: a new note cuts off the previous one without a click (choke); note length and note-off don't matter. Attack, Decay, Sustain, Release, Mode and Glide are gray.
- **Note** C-4 plays the machine's base pitch, other notes are relative to it (tones and metal; clap noise does not depend on the note). Transpose and Fine work as for CHIP.
- Closed / open hat — short and long DECAY (for example, a `DEC` lock); on the same track they cut each other off.
- Changing the machine sets its default macro values. When switching Type to FM or DRUM, the machine and macros are reset as well: the slots mean different things in each.
- FM, DRUM and SYNTH with a wavetable (at least one oscillator in WT mode) together play no more than 8 voices: a ninth steals the oldest of them (releasing ones first); CHIP, SAMPLE and SYNTH without WT are not affected.
- The timbres are approximations. On the 909 the hats and cymbals are samples in the original; here they are synthesized: a different inharmonic frequency set and more noise.

| Machine | Pitch at C-4 | DECAY | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| BD8 | 55 Hz | decay, 5 ms … 4 s | TONE: click | DRIVE | SWEEP: pitch drop up to 12 st | TIME: drop time 5–100 ms |
| SD8 | 180 + 330 Hz, noise HP 1.8 kHz | tone decay | TONE: low / high tone balance | SNAPPY: noise level | SWEEP: up to 7 st | N.DEC: noise length 0.3–2 × DECAY |
| TOM8 | 110 Hz | tone decay | NOISE: noise amount | DRIVE | SWEEP: up to 7 st | TIME: 10–200 ms |
| CP8 | noise, BP | tail | FREQ: 600–2500 Hz | SPREAD: 3 claps, spacing 6–14 ms | Q: 1–6 | TAIL: tail level |
| RS8 | 500 + 1700 Hz | 10–120 ms | TONE: tone balance | NOISE: noise click | SWEEP: up to 5 st | \- |
| CL8 | 2.5 kHz | 10–120 ms | TONE: ±1 octave | DRIVE | SWEEP: up to 3 st | \- |
| CB8 | 540 + 800 Hz (squares) | decay | FREQ: BP 1.5–4 kHz | BAL: 540 ↔ 800 Hz | ACCENT: peak at the start of the hit | \- |
| HH8 | 808 metal (205–800 Hz) | 20 ms … 2 s | HP: 4–12 kHz | NOISE: metal ↔ noise | SPREAD: frequency spread ×0.8–1.25 | N.DEC: noise length 0.3–1.5 × DECAY |
| CY8 | 808 metal × 0.7 | 0.3–4 s | HP: 2–8 kHz | NOISE | SPREAD | N.DEC |
| BD9 | 50 Hz | decay, 5 ms … 4 s | ATTACK: beater click | DRIVE | SWEEP: up to 24 st | TIME: 10–80 ms |
| SD9 | 190 + 345 Hz, noise HP 1 kHz | tone decay | TONE | SNAPPY | SWEEP: up to 7 st | N.DEC |
| TOM9 | 120 Hz | tone decay | NOISE | DRIVE | SWEEP: up to 12 st | TIME: 10–200 ms |
| CP9 | noise, BP | tail | FREQ: 800–3000 Hz | SPREAD: 4 claps | Q | TAIL |
| RS9 | 1700 + 3400 Hz | 10–80 ms | TONE | NOISE | SWEEP: up to 5 st | \- |
| HH9 | 909 metal | 20 ms … 2 s | HP: 6–14 kHz | NOISE | SPREAD | N.DEC |
| CY9 | 909 metal × 0.6 | 0.4–4 s | HP: 3–10 kHz | RIDE/CR: ride (lower metal) → crash (brighter noise) | SPREAD | N.DEC |

Columns 2–5 are the COLOR, SHAPE, SWEEP, CONTOUR slots (locks `COL`, `SHP`, `SWP`, `CON`); the first slot is always DECAY (`DEC`). "st" means semitones. In the LFO dest list for DRUM the slots use the generic names DECAY…CONTOUR.

<a id="synth"></a>

### SYNTH: oscillators and wavetables

**SYNTH** — an "analog" synthesizer: 2 oscillators, a sub-oscillator, noise, hard sync and a dedicated envelope on SHAPE, followed by the shared ADSR, [filter and LFO](#filter). Each oscillator's mode: **SAW**, **SQR**, **TRI** — classic alias-free waveforms (PolyBLEP), or **WT** — a wavetable (64 frames of 256 points, the frame position is SHAPE). The amplitude envelope is Attack, Decay, Sustain, Release from the ENV page; Mode (POLY / MONO) and Glide work as for CHIP. A SYNTH with a wavetable (at least one oscillator in WT) shares the limit of 8 "heavy" voices with FM and DRUM; a SYNTH using only SAW / SQR / TRI is light and has the whole pool of 16 available.

SYNTH has 6 pages: **MAIN**, **ENV**, **OSC**, **MOD**, **FILT**, **LFO**. When Type is changed to another type, the MOD page becomes the type page.

| Parameter | Values | Default | Meaning |
|---|---|---|---|
| **OSC page** | | | |
| Osc1 / Osc2 | SAW, SQR, TRI, WT | SAW | Oscillator mode. When switching to WT without a table, the first built-in one, `*SAWSQR`, is set. |
| Table1 / Table2 | table name or "-" | — | The oscillator's table in WT mode (in other modes the row is gray). Click or tap — [table selection](#wtpick). A red name means the table is neither in flash nor in the project folder: the oscillator is silent. |
| Shape1 / Shape2 | 0–127 | 0 | Macro **SHP1** / **SHP2**. For SQR — pulse width 50 → 95%; for WT — table frame 0 → 63 (smooth crossfade between frames). For SAW and TRI the row is gray. |
| Semi2 | −24…+24 | 0 | Osc 2 offset in semitones. |
| Detune | −50…+50 cents | 0 | Macro **DET** (0–127, center 64 = 0): osc 2 detune. |
| Sync | OFF / ON | OFF | Hard sync: osc 2's phase is reset on every period of osc 1. Pitch is set by osc 1, timbre by osc 2's Semi2 and Detune (the classic sync lead). For SAW and SQR the discontinuity is smoothed; for WT there is no correction (slight aliasing possible); osc 2 in TRI mode does not accept sync. |
| Mix | 0–100% | 0% | Macro **MIX**: 0% — osc 1 only, 100% — osc 2 only. |
| **MOD page** | | | |
| Sub | 0–127 | 0 | Sub-oscillator level: a square below osc 1. 0 — off (Sub oct is gray). |
| Sub oct | −1 / −2 | −1 | How many octaves the sub is below osc 1. |
| Noise | 0–127 | 0 | White noise level. |
| Env\>Shp | −64…+63 | 0 | Macro **SENV**: envelope depth on SHAPE of both oscillators (± the full range at its peak). 0 — no envelope (Env atk and Env dec are gray). |
| Env atk | 0 ms … 10 s | 0 ms | SHAPE envelope rise. |
| Env dec | HOLD, 1 ms … 10 s | 18 ms | SHAPE envelope decay. HOLD — stays at the peak. |

On the right of the OSC page is the waveform of the oscillator under the cursor (OSC1 or OSC2): a pulse with the current width, triangle, saw, or the table frame at the current SHAPE. The SHAPE envelope and oscillator phases restart on every new note except legato (MONO with overlap) — like the filter envelope.

**Macros and p-locks.** SYNTH has 5 macros in the same slots as FM and DRUM, so the fx locks [DEC…CON](#synthfx) and the LFO destinations work. In the FX column the names are generic; in INST and in LFO dest they have their own names:

| fx | SYNTH macro | LFO dest | What it changes |
|---|---|---|---|
| `DEC` | SHP1 | SHP1 | pulse width (SQR) or frame (WT) of osc 1 |
| `COL` | SHP2 | SHP2 | the same for osc 2 |
| `SHP` | MIX | MIX | osc 1 ↔ osc 2 balance |
| `SWP` | DET | DET | osc 2 detune, ±50 cents (64 = 0) |
| `CON` | SENV | SENV | SHAPE envelope depth (64 = 0) |

A lock on a step with a note applies to that note; on a step without a note — to the track's sounding note until the next note. An LFO on SHP1 / SHP2 gives PWM or wavetable "scanning".

<a id="wavetables"></a>

### Wavetables

A table is 64 frames of 256 points (one period per frame). In flash it is stored with mipmaps: 8 levels with a decreasing number of harmonics (128, 64, 32…); the level is chosen by note pitch, so high notes play without aliasing. One table in flash takes 96 KB, in the same partition as samples (shared cache and eviction, see [SAMPLES](#samples)).

**Built-in tables** (names with an asterisk) are always available, without a card and without importing. They are generated in flash on the firmware's first start (≈ 770 KB, a few seconds) and are never evicted:

| Name | Frame 0 → frame 63 |
|---|---|
| `*SAWSQR` | saw → square (even harmonics fade out) |
| `*PWM` | pulse 50% → 5% |
| `*SINSAW` | sine → saw (harmonics come in one by one) |
| `*TRISQR` | triangle → square |
| `*FORMANT` | fundamental + a formant sweeping across harmonics 2–32 |
| `*ORGAN` | organ "drawbars" (harmonics 1, 2, 3, 4, 6, 8) with rotating levels |
| `*SYNC` | hard-sync saw, ratio 1 → 8 |
| `*BELL` | FM 1:3, index 0 → 4 (bell) |

<a id="wtpick"></a>

**Table selection.** A click or tap on the Table1 / Table2 row opens a list over the whole tab area: built-in tables, then project tables, and the last row is **IMPORT…**. On the right is frame 0 of the table under the cursor.

| Input | Action |
|---|---|
| Turn, tap a table | The table is set on the oscillator immediately and plays C-4 (audition; a new table sounds from the next note). |
| Click, **OK** button | Keep the selection and close. |
| **CANCEL** button, long press | Restore the previous table. |
| **IMPORT…** | The `/wavetables` list: folders first (up to 4 levels), then `.wav` files; the `..` row goes up (from the root — back to the table list). Click or tap a file — import with progress, toast "IMPORTED name", the table is set on the oscillator. Buttons **OPEN** and **BACK**. |

- **Library on the card:** `/wavetables` (created automatically), subfolders up to 4 levels are allowed. Files are copied with a card reader or over [Wi-Fi](#wifi). Importing does not copy the file: the table goes into the project folder when you save.
- **Formats:** WAV PCM 8 / 16 / 24 bit, mono or stereo (mixed down to mono); the sample rate in the header doesn't matter. The frame layout is detected by length:
  - **WaveEdit** and similar — frames of 256 points, up to 64 frames (up to 16,384 samples);
  - **Serum / Vital** — frames of 2048 points (or the size from the `clm` chunk), up to 256 frames; each frame is reduced to 256 points.

  More than 64 frames — 64 are taken evenly; fewer — the missing frames are smoothly interpolated. DC offset is removed from each frame, and the table is peak-normalized. If the length fits no layout — "UNSUPPORTED WAV"; silence — "NOT A WAV FILE".
- **Name** of the table — the file name without extension (up to 16 characters, A–Z a–z 0–9 \_ -). If the project already has a table with this name and different data, the new one gets a suffix `-2`, `-3`…; the same data under the same name — the existing one is used. Up to 32 tables per project ("TABLE LIST FULL").
- **Project:** Save writes the tables selected in instruments to `/projects/name/wt/<table>.wav` (mono, 16 bit, 64 × 256); tables not used by any instrument are removed from the project. Load pulls missing tables from this folder into flash; if the file is missing or holds different data, the table is absent (name red, oscillator silent) and counts toward "N SAMPLES MISSING".
- **A SYNTH preset** stores table names. If a table is neither built-in nor in the project, loading the preset imports it from `/wavetables/<name>.wav` (only from the root and only with playback stopped); otherwise the name stays red.

> Importing a table writes to flash and works only with playback stopped ("STOP PLAYBACK FIRST"). Selecting an already imported or built-in table works during playback too.

<a id="filter"></a>

### Filter and LFO

The FILT and LFO pages exist for all types. Voice chain: sound → **Drive** → **Bit crush** → **Downsample** → filter. The filter is a 12 dB/oct SVF after the whole voice (for FM and DRUM — after their own sound).

| Parameter | Values | Default | Meaning |
|---|---|---|---|
| Drive | OFF, 1–127 | OFF | Saturation (tanh) before the filter: the higher it is, the denser the sound and the louder the quiet parts. Lock — fx `DRV`, LFO destination — DRIVE. |
| Bit crush | OFF, 1–127 | OFF | Bit depth reduction: from 16 bits (1) to 2 bits (127); the number in brackets is how many bits remain. Step lock — fx `BIT`. |
| Downsample | OFF, 1–127 | OFF | Sample rate reduction: each sample is held for up to ~64 samples at 127, the sound becomes "digital" and ringing. Step lock — fx `SRR`. |
| Filter | OFF, LP, BP, HP | OFF | Mode. OFF — bypass: the sound is as without a filter, the other filter rows are gray. |
| Cutoff | 20 Hz … 14 kHz (128 values, exponential) | 14 kHz | Cutoff frequency. Lock — fx `FLT`. |
| Reso | 0–127 | 0 | Resonance (Q 0.5–20). High resonance is loud — lower the Volume. Lock — fx `RES`. |
| Flt env | −64…+63 | 0 | Filter envelope depth: ±6 octaves at its peak. 0 — no envelope (attack and decay are gray). |
| Flt attack | 0 ms … 10 s | 0 ms | Envelope rise. |
| Flt decay | HOLD, 1 ms … 10 s | 18 ms | Envelope decay (exponential). HOLD — the envelope stays at the peak. |
| Key track | 0–100% | 0% | Cutoff follows the note from C-4: 100% — one octave per octave. |
| LFO | 1–4 | 1 | Which of the instrument's four LFOs the rows below edit; the number in brackets is how many are on. All four run at once; their effects on the same destination add up (VOL — multiplies). |
| Wave | SINE, TRI, SAW, SQR, RND | SINE | LFO shape; RND — a random value each period. |
| Sync | FREE / TEMPO | FREE | TEMPO — the rate is set as a fraction of a bar and follows the project BPM. |
| Rate | 0.05–30 Hz or 1/32 … 8 BARS | ≈ 1.3 Hz | Rate: in FREE — an exponential Hz scale; in TEMPO — 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2, 1, 2, 4, 8 bars per period. |
| Depth | −64…+63 | 0 | Depth; 0 — LFO off (then wave, sync, rate and dest are gray). Full depth: PITCH ±12 semitones, VOL ±100%, CUTOFF ±64 Cutoff steps (≈ ±4.8 octaves), macro ±64. |
| Dest | PITCH, DECAY, COLOR, SHAPE, SWEEP, CONTOUR, VOL, CUTOFF, DRIVE | PITCH | Destination. DECAY…CONTOUR (macros) — only for FM, DRUM and SYNTH (for SYNTH they are called SHP1, SHP2, MIX, DET, SENV); for CHIP and SAMPLE they are skipped. CUTOFF acts when the filter is on. DRIVE — ±64 Drive steps at full depth. |

- The filter envelope and LFO phase restart on every new note except legato (MONO with overlap, FM CHORD): there the envelope keeps running, like on a 303.
- Cutoff = Cutoff (or FLT lock) + envelope + Key track + LFO CUTOFF, limited to 20 Hz … 14 kHz.
- Old projects load with Filter OFF and sound as before.

<a id="kit"></a>

### KIT (drum kit)

**KIT** — a set of 8 lanes for a [drum track](#drumtrack). It makes no sound on its own: each lane is either its own mini-sampler on a project sample or a reference to one of the 16 instruments. There are two pages: **MAIN** (Name, Type, Dly send — the delay send for the whole kit) and **LANES** — a scrolling list of lanes.

| Row | Values | Meaning |
|---|---|---|
| L1…L8 Src | SAMPLE / INST | Lane source. Rows for the other mode are hidden. |
| Sample | --- or a project sample | SAMPLE: what the lane plays. Red — the sample is not in flash (the lane is silent). |
| Volume | 0–127 | SAMPLE: lane volume. |
| Pitch | −24…+24 | SAMPLE: offset in semitones from the sample's original pitch. |
| Decay | FULL, 1 ms … 10 s | SAMPLE: FULL — the sample plays to the end, otherwise it fades out over this time. |
| Instr | INS1…INS32 | INST: the lane's instrument (with its own parameters, macros, filter). A KIT inside a KIT is silent and shown in red. Selecting a SAMPLE instrument sets the lane's Note to its Root. |
| Note | 0–127 | The lane's note: sent to MIDI on a MIDI track, and used by the lane to play its INST instrument. Lanes in a kit have different notes — turning skips taken ones. Default C-4…G-4. |

- Each lane is mono: a repeated hit cuts off its own previous one (choke); different lanes sound at the same time — up to 8 voices per track.
- PREVIEW plays a lane with note C-4 (the first one by default).
- KIT has no presets ("NO KIT PRESETS"); there is no KIT type in the preset browser.
- Renaming a sample in FILE → SAMPLES also updates the lanes; Delete leaves the name in the lane (the lane is silent, like MISSING) and asks for confirmation if the sample is used in a lane.

<a id="presets"></a>

### Presets

The **PRESET** button in the INST header (or <kbd>Shift</kbd>+long press of the encoder) opens a **Load** / **Save** menu, then a preset browser that fills the whole tab. A preset is the entire instrument: type, parameters, filter, LFO, machine and macros, slices and their modes, SYNTH oscillators and wavetable names, and the name.

**Browser header:** the type (with arrows in Load), the path inside the type folder, and three buttons. At the root of a type there is a `[FACTORY]` row (Load only), then your own folders (`name/`), then files; inside a folder the first row is `..` (up one level). `[FACTORY]` holds the factory presets built into the firmware, sorted into categories; they cannot be changed or deleted.

| Input | Load | Save |
|---|---|---|
| Turn | Row; the preset under the cursor is applied to the instrument at once and plays C-4 (audition, marked `>`). | Row. |
| <kbd>Shift</kbd>+turn, tap on the arrows | Another type (FM, SYNTH, DRUM, SAMPLE, CHIP). Defaults to the instrument's type. | — |
| Tap on a preset | Audition (same as turning). | Write over it (asks Overwrite). |
| Click on a preset | Take it and close. | Write over it (asks Overwrite). |
| Click / tap on a folder | Enter; `..` goes up. | |
| Buttons | **OK** keeps the selected preset, **DEL** deletes your own preset under the cursor (with confirmation), **CANCEL** restores the instrument as it was. | **SAVE**: name on the keyboard (defaults to the instrument name; if the file exists, asks Overwrite), **+DIR**: new folder inside the current one, **CANCEL**: close. |
| Long press of the encoder | Same as CANCEL. | Close. |

- **Load** changes the instrument immediately; CANCEL brings the previous one back. Auditioning works during playback too: tracks using this instrument play with the new sound.
- **Save** always writes to the folder of the instrument's type; `[FACTORY]` is not shown. File name: up to 16 characters, A–Z 0–9 \_ -; the first 8 characters also become the instrument name.
- A **SAMPLE preset** stores the sample name. If that sample is in the project, it is applied together with the preset's Root and slices; otherwise the instrument's sample, Root and slices stay as they were. The Slices, Chop and Chop N / Sens modes are always taken from the preset. There are no factory SAMPLE presets.
- A **SYNTH preset** stores the oscillator wavetable names: built-in tables are always available, a project table is matched by name, and a missing one is imported from `/wavetables` (see [wavetables](#wavetables)). Factory SYNTH presets use only built-in tables.
- **Format:** presets are saved in v3 format, including slices and SYNTH parameters; older presets (v1, v2) load as before.
- **Folders:** up to 4 levels inside a type folder (deeper gives "TOO DEEP"). The last opened folder of each type is remembered until power-off.
- Without a card, Load shows only `[FACTORY]` and Save does not open ("NO SD CARD").

| Type | Category | Factory presets |
|---|---|---|
| CHIP | LEAD | SQ LEAD, ARP PLK |
| CHIP | BASS | TRI BASS, ACID |
| CHIP | PAD | PWM PAD, WT PAD |
| CHIP | KEYS | WT BELL |
| CHIP | PERC | NOIS HH, NOIS SN, METALBL |
| FM | DRUMS | KICK, SNARE, CLAP, HAT C, HAT O, WOODBLK |
| FM | KEYS | BELL, E.PIANO, CHORD M7 |
| FM | BASS | FM BASS |
| DRUM | 808 | BD808, BD808 L, SD808, CH808, OH808, CP808, CB808 |
| DRUM | 909 | BD909, SD909, CH909, OH909, CP909 |
| SYNTH | BASS | BASS, ACID, SUBBASS |
| SYNTH | LEAD | LEAD, SYNCLD |
| SYNTH | PAD | PAD, PWMSTR, WTSWEEP |
| SYNTH | KEYS | PLUCK, BELL |

<a id="proj"></a>

## PROJ: project and pattern

Five pages: **SONG** (tempo, scale, pattern, groove), **FX** (delay, reverb, DJ filter), **COMP** (compressor, sidechain), **PERF** (button effects), **SYS** (Preview, theme, autosave, version). Switch pages as in TRACK and FILE: tap a page tab, or put the encoder on the page tabs and click (<kbd>Shift</kbd>+click goes back).

| Parameter | Values | Scope |
|---|---|---|
| **SONG** | | |
| BPM | 20–300 | project |
| Scale root | C…B | project |
| Scale | 13 scales | project |
| Length | 4–128 | playing pattern |
| Resolution | 1/4, 1/8, 1/16, 1/32, 1/8T, 1/16T | playing pattern |
| Swing | 50–75% | playing pattern (grayed out when a Groove is selected) |
| Groove | OFF, MPC 54/58/62/66, SHUFFLE, PUSH, LAID BACK, DRUNK, BOOM BAP, HOUSE | playing pattern: a template of timing offsets and accents for every 16 steps, used instead of swing. MPC: sixteenth-note swing with accents; SHUFFLE: triplet feel; PUSH: off-beats slightly early; LAID BACK: slightly late; DRUNK: uneven (the same on every pass); BOOM BAP: hip-hop; HOUSE: accent on the off-beats. OFF: Swing applies. |
| **FX** | | |
| Delay | 1/16–16/16, default 3/16 | project: [delay](#delay) time in sixteenths, follows the tempo |
| Feedback | 0–127, default 50 | project: how much of the echo is fed back for repeats; 0 is a single echo, 127 a long tail (it always decays) |
| Tone | 0–127, default 90 | project: echo brightness (low-pass 300 Hz … 16 kHz, 127 is unfiltered); each repeat is darker than the previous one |
| Dly level | 0–127, default 100 | project: echo volume; 0 turns the delay off (the rows above are grayed out) |
| Reverb | 0–127, default 60 | project: [reverb](#reverb) size, i.e. tail length |
| Rvb damp | 0–127, default 70 | project: high-frequency damping in the tail (higher is darker) |
| Rvb level | 0–127, default 80 | project: reverb volume; 0 turns it off |
| DJ filter | LP 64 … OFF … HP 63 | project: a filter on all internal sound, before the compressor. Turning left closes a low-pass (20 kHz → 100 Hz), turning right opens a high-pass (20 Hz → 8 kHz), with a little resonance. Handy for live tweaking. |
| **COMP** | | |
| Comp | OFF, 1–127, default OFF | project: [compressor](#comp) on all internal sound; higher means a lower threshold (−6 … −30 dB) |
| Comp rel | 20 ms … 1 s, default ≈ 90 ms | project: compressor release |
| SC track | OFF, T1–T16, default OFF | project: sidechain key track: its sound ducks the whole mix |
| SC depth | 0–127, default 64 | project: sidechain amount |
| **PERF** | | |
| Button 1 … Button 8 | [PERF](#live) effects or --- | project: what track button N holds in PERF mode |
| **SYS** | | |
| Preview | ON / OFF, default ON | project: entering a note in GRID on an INT track sounds it immediately |
| Theme | 17 themes, default CLASSIC | device: interface colors (see below) |
| Autosave | OFF, 1, 2, 5, 10 min, default 5 | device: [autosave](#file) interval |
| Firmware | build commit | display only: firmware version ("+" means built with local changes) |
| Last reset | POWER ON, SOFTWARE, PANIC, WATCHDOG, BROWNOUT… | display only: reason for the last reboot (crashes are logged to crashlog.txt) |
| Audio RAM | INTERNAL / SYNTH IN PSRAM / SEQ IN PSRAM, RVB INT / PSRAM, free K | display only: the synth and sequencer should be in internal memory; in PSRAM, audio costs noticeably more CPU. The reverb buffer goes into internal memory if 24 KB remain free after it; while Wi-Fi is on it moves to PSRAM |
| CPU profile | CLICK TO START / RUNNING | diagnostics: click to start, click again and the audio time broken down by part (queue, events, voice management, voices by type, delay, reverb, master) is appended to /projects/cpuprof.txt |

<a id="themes"></a>

**Themes.** They change as soon as you turn the encoder. The choice is a device setting, like MAIN volume: it is stored in the board's memory after a second (when the transport is stopped); the project and file are not changed. Themes: CLASSIC (the original), AMBER and PHOSPHOR (monochrome terminals), NORD, DRACULA, SOLARIZED, GRUVBOX, MONOKAI, TOKYO, MOCHA, ROSE PINE (inspired by well-known editor palettes), GAMEBOY, C64 (retro), SYNTHWAVE, OCEAN, CONTRAST (maximum contrast) and PAPER (light).

<a id="reverb"></a>

**Reverb** is a shared mono reverb (4 comb filters and 2 allpasses, like Freeverb). Instruments send to it with the Rvb send parameter, steps with the RVB fx. Delay echoes do not go into the reverb. Changing the size on the fly jumps.

<a id="comp"></a>

The **compressor** sits on the sum of the internal sound after the delay and reverb, before the master volume: 4:1, 1 ms attack; Comp sets the threshold and the makeup gain (half of the threshold reduction). **Sidechain:** SC track = the kick track, Comp 60–100, SC depth 100+, and the whole mix ducks on every hit ("pumping"); Comp rel sets how fast it comes back. The key is that track's internal sound (INT); the track itself plays as usual. Comp OFF: the compressor leaves the sound untouched.

<a id="delay"></a>

**Delay** is a single shared mono delay for the internal sound (it does not affect the MIDI output). Instruments send sound to it with the Dly send parameter, individual steps with the DLY fx. The time is synced to BPM: 1/16 at 120 BPM = 125 ms. The line is sized for 4 s, i.e. 16/16 down to 60 BPM; at slower tempos the time is capped at 4 s. Changing the time or tempo on the fly jumps (a click on the tail is possible). On stop, the echo rings out.

**Scales:** Chromatic, Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Harmonic minor, Melodic minor, Pentatonic major, Pentatonic minor, Blues. The default is C Chromatic, i.e. no restriction.

**How the scale works.** Notes are not filtered; the scale defines the scale degrees. These move by scale degree: note entry with the encoder, Transpose in SCALE mode, NRN, CHD and Fill (RAMP and RANDOM for notes). An out-of-scale note is first snapped to the nearest lower scale note on the first move. Octave shifts and the mini keyboard ignore the scale. Changing the scale does not rewrite notes already entered.

**Swing** delays every second step: 75% means by half a step.

<a id="bank"></a>

## BANK: pattern bank

A 4×4 grid of tiles: patterns P01–P16. Each tile shows the number, length, resolution and 16 small squares in two rows (tracks 1–8 on top, 9–16 below): a filled square means the track has data.

- The playing pattern is filled with a yellow border. A queued one has a blinking double border. A copy source has a red border.

| Input | Action |
|---|---|
| Tap / click | **Queue**: during playback switches at the end of the pass, when stopped switches at once. |
| <kbd>Shift</kbd>+tap / click | **Immediately**: switches from the next step, keeping the position. |
| Long tap / press | Pattern menu. |
| Turn | Select a tile. |

**Pattern menu:**

- **Copy to…**, then tap the target tile. Everything is copied: steps, length, resolution, swing. Tapping the same tile cancels.
- **Clear**, with confirmation. Also resets length (16), resolution (1/16), swing (50).
- **Length 16 / 32 / 64**: quick length change.
- **Song mode**: turns on song mode.

Any pattern change clears TIE. The pass counter is reset.

<a id="song"></a>

## Song mode

A chain of up to 64 entries. An entry is a pattern, a transposition, a number of passes and a mute scene. To turn it on: tile menu → **Song mode**. In song mode the BANK tab shows the chain list instead of the tiles.

- Header: `SONG n/64`, the **+ ADD** button (append the playing pattern at the end) and **SONG OFF**.
- Row: `>` next to the playing entry, the number and four fields, `P05 +3 x2 S1`: pattern, transposition, passes, scene; default values (`+0 x1 S-`) are gray. On the right, the pattern's length and resolution.
- Turn to select a row. Click to edit the row: turning or dragging changes the field (red), <kbd>Shift</kbd>+turn selects the field. Tapping a field of the selected row edits that field directly. On an empty chain, a click adds the playing pattern.
- **Transposition** −24…+24 semitones: notes on melodic tracks sound shifted while the entry plays (clamped to 0 and 127). Drum tracks, OFF and the pattern's steps themselves are not changed.
- **Passes** x1…x16: how many times to play the pattern before the next entry. Repeats sound like a loop: TIE and the CND counter carry on.
- **Scene** S1…S8: when the entry starts, track mutes are set from the scene (an empty scene changes nothing). `S-`: leave as is.
- **Scenes**: 8 tiles `S1…S8` below the list. Tap to recall (mute all tracks as in the scene), long tap to store the current mutes, <kbd>Shift</kbd>+tap to clear. A filled tile is highlighted; one matching the current mutes has a yellow border. Scenes are stored in the project.
- Long press opens the row menu: **Insert Pxx before**, **Delete**, **Duplicate**, **Append Pxx**, **Song mode off**. Pxx is the playing pattern.

### How the song plays

- Playback starts from the first entry. At the end of a pass, the next entry follows; after the last one, the first one again.
- Turning the mode on during playback: the chain starts at the end of the current pass. Turning it off: the current pattern keeps looping.
- Chain edits during playback are picked up at the end of the pass. Inserting or deleting a row above the playing one does not disturb the position.
- Two identical entries in a row sound as a repeat: TIE and the CND counter are kept.
- Editing the passes during playback is picked up if the decision to move on has not been made yet. Deleting the playing entry: the next one starts at the end of the pass.
- An empty chain plays the current pattern, as without song mode.
- Pattern queueing does not work in song mode; the tiles are unavailable.
- When stopped, GRID shows and edits the first entry of the chain, and the status bar shows `S01`.

<a id="file"></a>

## FILE: saving and loading

> The card must be **FAT32** with an **MBR** partition scheme. exFAT (common on cards larger than 32 GB) and GPT are not readable; the screen will show "NO SD CARD". On a Mac: `diskutil eraseDisk FAT32 MIDI MBRFormat /dev/diskN`. This command erases the whole card.

| Item | What it does |
|---|---|
| Save | Save under the current name. Without a name it works like Save As. Works during playback. The project's sample folder is written along with the project (see [below](#samples)). |
| Save As… | On-screen keyboard, then OK. If a file with that name already exists, asks Overwrite. The sample folder is written in full under the new name. |
| Load… | Project list. If there are unsaved changes, asks for confirmation. Stops playback. Samples and wavetables missing from flash are pulled in from the project folder. |
| New | Template menu: **EMPTY** (blank), built-in **808 SET**, **909 SET**, **FM SET** (a drum KIT on track 1 and melodic instruments on the following ones), **CHIPTUNE**, **MIDI 8** (8 MIDI tracks on channels 1–8), then your own templates (`> NAME`), **Demo songs…** and **Save as template…**, which saves the current project without notes (instruments, tracks, settings) to `/templates`. Asks about unsaved changes after you choose. |
| Import MIDI… | Import a `.mid` from the `/midi` folder. |
| Render WAV… | Record the internal sound to a WAV on the card; see [below](#render). |
| Wi-Fi transfer… | Files to and from the card over Wi-Fi, firmware update. See [below](#wifi). |
| Retry / Restore autosave | Without a card, Retry: reconnect the card. With a card, if this project has an autosave, **Restore autosave**: load it (the project stays unsaved; Save makes it permanent). |

**Demo songs** (FILE → New → Demo songs…) — nine finished projects built into the firmware, no samples needed; Play runs the song (SONG mode). Each one shows off part of the device:

| Demo | Tempo, key | What to look at |
|---|---|---|
| DEMO-TRANCE | 138, A minor | supersaw hook, pads and bass pumped by the sidechain (key: the KICK track), ARS arpeggio on CHD chords, a breakdown with FLT locks opening the lead and a riser held by TIE, snare rolls with RAT ramps |
| DEMO-DNB | 174, D minor | two-step 909 breaks with ghost snares, a reese bass with a tempo-synced filter LFO plus a sub, both held by TIE, liquid FM e-piano with delay, 7th-chord pads |
| DEMO-CHIPTUNE | 150, C major | CHIP only: chords from the ARP fx, an octave-bouncing triangle bass, a noise kit, VIB on long notes, a bridge, and the last chorus a tone up (chain transpose) |
| DEMO-ACID | 128, A minor | a 303-style line with SLD slides, accents and a FLT lock on every step, the filter opening section by section; a 12-step polymeter section (track length), hats on probability (PRB), RES and DLY locks in the break |
| DEMO-LOFI | 84, C major | swing, humanize, FM e-piano 7th chords through bit crush and SRR, vinyl crackle from PRB + NRN noise hits, a low-passed master (DJ filter) |
| DEMO-SYNTHWAVE | 108, E minor | driving 16th bass pumped by the sidechain, a gated-reverb clap, a sync-pluck ARS arpeggio, a wavetable lead with VIB, tom fills |
| DEMO-DUBTECHNO | 120, C minor | one FM CHORD (minor 9) stab through a long dark delay and a big reverb, its filter swept by a 4-bar synced LFO and FLT locks, DLY / RVB throws, a drone held by TIE |
| DEMO-IDM | 110, D dorian | every part on its own track length (7, 5, 13, 9, 11, 6 steps against 16): the parts drift; CND 1:2 / 1:3, PRE and NEI conditions, PRB and NRN random notes, a 5/16 delay |
| DEMO-HOUSE | 124, F minor | swung 909 with humanize, an offbeat FM bass, 7th-chord stabs, congas on probability, a choir breakdown with a clap roll; PERF buttons set for live play (filter down / up, delay and reverb throws, rolls, short decay, mute) |

A demo opens as a new project named after it: Save writes `DEMO-….mtp` to the card, and from there it is an ordinary project to take apart and change.

<a id="render"></a>

**Render WAV…** is an offline render of the internal sound (INT tracks, with delay, reverb and compressor) to WAV: mono, 16-bit, 32 kHz. Rows: **Source**: `PATTERN 01…16` (defaults to the playing one) or `SONG` (the whole chain with repeats; available if the chain has rows); **Tracks**: `ALL`, `SOLOED` (only soloed tracks; available if solo is on) or `STEMS`, where each audible INT track with notes goes to its own file `_P01_T03.wav` (stems for mixing; each stem gets its own shared delay / reverb / compressor, and sidechain from other tracks does not apply). **RENDER** writes to `/samples/render/<project>_P01.wav` or `_SONG.wav`, with a 2 s tail; if the file exists, it asks **Overwrite**. The folder is visible on the [Wi-Fi](#wifi) page (download) and in FILE → SAMPLES → Import (bring it back into the project as a sample). MIDI tracks are not included in the render.

- Only when stopped (`STOP FIRST`). Progress shows `RENDER nn%`, roughly in real time; a long press of the encoder or Play cancels (`CANCELLED`, no file is created).
- Done: `12.3s PEAK -2.1dB`; `CLIP` means there was clipping (lower MAIN or the volumes). Errors: `DISK FULL`, `WRITE FAILED`, `AUDIO BUSY` (audio did not stop within 0.5 s; try again).
- The render reproduces playback, just without the 14 ms output latency; PRB / NRN / VRN are random but identical from render to render. Notes pushed past the end of the pattern (NDG, swing) are cut off.

Below the header are the **PROJECTS** and **SAMPLES** page tabs, as in TRACK and PROJ: tap a tab, or put the encoder on them (frame) and click. PROJECTS holds the items above, SAMPLES the [project samples](#samples).

- **Project name:** up to 16 characters, A–Z a–z 0–9 \_ -. Case is preserved, but names differing only in case are the same file.
- **Keyboard:** digits, A–Z, - \_, DEL, CANCEL, OK. <kbd>Shift</kbd>+key gives a lowercase letter. Encoder: turn selects a key, click presses it, long press cancels.
- **Reliability:** saving writes a temporary file, verifies it, and renames the previous version to `.bak`. If the file is corrupted, Load offers **Load backup**. A `.bak` has no sample folder of its own: it takes samples from the same project folder, so a sample removed from the project after that version may turn up MISSING if it has already been evicted from flash.
- **Autosave:** unsaved changes are written every N minutes (PROJ → SYS → Autosave: OFF, 1, 2, 5, 10 min, default 5) to `/projects/name.auto` (the project file only), while the transport is stopped and nothing has been pressed for 3 s (toast "AUTOSAVE…"). Save deletes it. After a crash or power-off: FILE → Restore autosave.
- **Safe boot:** hold <kbd>Shift</kbd> at power-on and the project is not loaded automatically (toast "SAFE BOOT"). Use it for a project that crashes the tracker on load.
- **Crash log:** if the tracker rebooted because of a crash, the watchdog or a brownout, the reason, firmware version and (if available) a backtrace are written to `/projects/crashlog.txt`; it is visible and downloadable on the [Wi-Fi](#wifi) page. The firmware version and the reason for the last reboot are in PROJ → SYS.
- **Autoload:** at power-on, the last saved or loaded project is loaded. If it is corrupted, the `.bak` is used (toast "LOADED BACKUP"). If that fails too, the demo loads with the toast "AUTOLOAD: …". New disables autoload until the next save.
- Saved: all patterns, track settings (including mute/solo, Program, Out, Instr, Volume), 32 instruments, Preview, delay settings, tempo, scale, the chain and song mode. The project also stores the list of its samples and wavetables, while the WAVs themselves sit next to it in the project folder (see [SAMPLES](#samples) and [wavetables](#wavetables)). Old projects open with MIDI on all tracks. Not saved: undo, the clipboard and Fill parameters.
- **Compatibility:** the file now holds 16 tracks per pattern. Projects from older versions (8 tracks) open as usual, with tracks 9–16 empty. A file written by this firmware cannot be opened by older firmware: it will report a corrupted file (not a "newer version"). To go back to older firmware, keep a copy of the `.mtp` / `.bak` saved by it.

| Path | Contents |
|---|---|
| `/projects/name.mtp` | project |
| `/projects/name.bak` | previous version |
| `/projects/name/*.wav` | project samples (written by Save) |
| `/projects/name/wt/*.wav` | project [wavetables](#wavetables) (written by Save) |
| `/projects/legacy.idx` | internal: samples carried over from old projects |
| `/projects/name.auto` | autosave |
| `/projects/crashlog.txt` | log of reboots after crashes |
| `/templates/*.mtp` | your own project templates (FILE → New) |
| `/last.txt` | name of the project to autoload |
| `/midi/*.mid` | files for import (subfolders allowed) |
| `/samples/*.wav` | WAV library for importing into projects (subfolders allowed) |
| `/wavetables/*.wav` | wavetable library for SYNTH (subfolders allowed) |
| `/presets/TYPE/…/name.mti` | [instrument presets](#presets): folders CHIP, SAMPLE, FM, DRUM, SYNTH, each with up to 4 levels of your own folders |

The `/projects`, `/midi`, `/samples`, `/wavetables` and `/presets` folders (with the type folders) are created automatically.

<a id="samples"></a>

### SAMPLES: project samples

Samples belong to the project: each project has its own list (up to 128), and instruments can only choose from it. A project on the card is self-contained: the `.mtp` plus a folder with its samples:

| Where | What |
|---|---|
| `/projects/name.mtp` | the project and its sample list (name, length, checksum) |
| `/projects/name/<sample>.wav` | project samples: mono, 16-bit, same sample rate as in flash (up to 32 kHz). Written by Save: missing and changed files are written, files of samples removed from the project are deleted. |
| `/samples/` | library: samples are imported into the project from here. Import does not copy the file; the sample gets into the project folder when you save. |
| flash, **samples** partition | cache (~9.9 MB, ≈ 2.5 min of mono at 32 kHz, up to 128 entries): samples play from here. Independent of the card and not erased by firmware updates. Identical data in different projects is stored once. |

Screen rows:

- At the top: `FREE n / m KB CACHE k KB` and a bar showing used space. FREE is free flash space, CACHE is how much the samples of other projects take up (the cache). If there is not enough room for an import or load, the cache is evicted automatically, largest entries first, never touching the current project's samples; if it still does not fit, "BANK FULL".
- **Import WAV…**: a list of `.wav` files from `/samples`. Subfolders first (`name/`, select to enter), then files; in a subfolder the first row **\< Up (..)** goes up one level, and the path is shown in the header. Up to 4 levels of nesting; hidden files (macOS `._*`) are not shown. Selecting a file opens the keyboard with the sample name (defaults to the file name without extension, up to 16 characters A–Z a–z 0–9 \_ -). If the project already has that name, asks **Overwrite**. Import progress is shown in percent. When the list is full, "SAMPLE LIST FULL". **Play** on a file auditions it without importing: the first 8 seconds at the sample's volume with Vol at full (Master applies); Play again or moving the cursor stops it. Works during playback too; in this list the Play button does not start the transport (the button in the header does).
- **Compact**: defragment flash by moving samples to the start so the free space becomes one block. Usually not needed: import and load compact on their own when gaps get in the way.
- **Clear cache**: remove everything from flash that is not part of the current project (toast "CLEARED n"). Other projects' samples stay in their folders on the card and are pulled back in when those projects are loaded.
- Project sample rows: name, duration, size. A red `MISSING` label means the data is not in flash and could not be taken from the folder: the voice is silent. Click or tap opens the **Rename** / **Delete** menu.
  - **Rename** changes the name in the project and in the instruments using it; a name already in use gives "NAME TAKEN". A MISSING sample cannot be renamed ("SAMPLE MISSING"): its file is looked up by name.
  - **Delete** removes the sample from the project; if it is selected in an instrument or a KIT lane, it asks again (`USED BY INSn. DELETE?`). The data stays in flash as cache; the next Save deletes the file in the folder.

Import, Rename and Delete change the project (a `*` appears); save it so the changes reach the `.mtp` and the folder.

**Loading.** Load and autoload at power-on check every sample of the project: if it is in flash, it is ready; if not, it is imported from `/projects/name/` with a progress bar (file name, bytes). If the file is missing, corrupted or contains different data (the WAV was changed on a computer), the sample stays in the list as MISSING, with the toast "N SAMPLES MISSING" at the end. To bring it back, put the correct file in the folder and load the project again, or import a WAV under the same name.

**Saving.** If the folder could not be written (card full, write error), toast "SAMPLES NOT SAVED". The `.mtp` file is saved anyway, but the project stays marked `*`: the next Save completes the folder. A MISSING sample leaves its file untouched on save; Save As to a new name copies such files from the old folder.

**Moving a project** to another card or another tracker: copy `name.mtp` and the `name/` folder to `/projects` (on a computer or via [Wi-Fi](#wifi)). On load, the samples are pulled into flash automatically.

**Old projects** (before this version, when samples were shared by all projects): on load, the sample list is built from the instruments and taken from flash by the old names. Save such a project once and its folder appears; from then on it moves like any other. If an old sample is no longer in flash, it is MISSING; import it again.

| WAV | Support |
|---|---|
| Format | PCM 8, 16, 24-bit. Float and 32-bit are not supported ("UNSUPPORTED WAV"). |
| Channels | mono or stereo (stereo is mixed down to mono) |
| Sample rate | any; above 32 kHz it is downsampled to 32 kHz, below that it is stored as is |
| Size | as much as fits in flash; via Wi-Fi, files up to 4 MB |

> Import, Rename, Delete, Compact and Clear cache work only with playback stopped ("STOP PLAYBACK FIRST"): writing to flash stops the audio. Load and New stop playback themselves. If the partition is not found ("NO SAMPLE BANK"), the firmware was flashed with an old partition table; flash it over USB once (see README).

<a id="wifi"></a>

### Wi-Fi: files and firmware without opening the case

FILE → **Wi-Fi transfer…** connects the tracker to your home network and opens a web page. The tracker has no access point of its own: the computer or phone must be on the same network.

At the top of the page are tabs: **Projects**, **Samples**, **MIDI**, **Wavetables**, **Presets**, **Firmware**; one section is shown at a time. The file list scrolls in its own pane, with a name filter and the file count above it. The selected tab stays in the address (`#samples`), so you can bookmark it.

1.  If there are unsaved changes, a menu appears: **Cancel**, **Save & continue** (only for a project with a name), **Continue w/o saving**. The reason: after a firmware update the tracker reboots.
2.  Playback stops, and <kbd>Play</kbd> does not work in this mode (toast "WI-FI MODE").
3.  The first time: a list of networks, then the password on the on-screen keyboard. **\#+=** switches to the symbols page with space, **ABC** goes back. Letters are lowercase; <kbd>Shift</kbd> gives uppercase. The network and password are stored in flash after a successful connection.
4.  The screen shows the address `http://d-trk.local` and the IP (if `.local` does not open, e.g. on Android, use the IP).
5.  **EXIT** (or a long press of the encoder, or switching to another tab) turns Wi-Fi off. **NETWORK** chooses another network. **RETRY** retries the connection after an error.

The page has five sections: **MIDI** (`/midi`), **Projects** (`/projects`, with the projects' sample and wavetable folders), **Samples** (`/samples`, the import library), **Wavetables** (`/wavetables`) and **Presets** (`/presets`). Drag files with the mouse into the drop zone or pick them with the button; the list lets you download (click the name), rename and delete. If a file already exists, the page asks whether to replace it. A log of recent actions is shown on the tracker's screen.

- **MIDI:** `.mid` only, up to 512 KB, name up to 59 characters, Latin characters only (Cyrillic is not displayed on screen).
- **Samples:** `.wav` only, up to 4 MB, name up to 59 characters, Latin characters only. The file goes into the library on the card; FILE → SAMPLES → Import WAV… on the tracker adds it to the project.
- **Wavetables:** `.wav` only (formats as for [import](#wavetables)), up to ~3 MB (the longest Serum table: 256 frames × 2048, stereo 24-bit), name up to 59 characters, Latin characters only. Folders work as for samples, up to 4 levels. A table gets into the project via INST → SYNTH → Table → IMPORT… on the tracker.
- **Sample folders:** the path is shown above the list (click a part to go there); the **..** row goes up. **New folder** creates a folder inside the current one, and files are uploaded there. Folder names: up to 32 characters, Latin letters, digits, space, `. _ -`, not starting with a dot or a space; no deeper than 4 levels. Only an empty folder can be deleted. Wavetable folders work the same way. MIDI has no folders.
- **Presets:** `.mti` only, up to 1 KB, name up to 16 characters (A–Z 0–9 \_ -). The section root holds the type folders CHIP, SAMPLE, FM, DRUM, SYNTH: they cannot be deleted, no new folders can be created next to them, and files cannot be uploaded to the root. Inside a type folder you can have your own folders, as for samples, up to 4 levels. An uploaded preset is checked (CRC, version) and must go into the folder of its own type: a DRUM preset is rejected in `/presets/FM` ("it belongs in /presets/DRUM").
- **Projects:** `.mtp` and `.bak`, names following the project rules. An uploaded `.mtp` is checked (CRC, version); a corrupted one is rejected. When replacing, the old file becomes the `.bak`, as when saving.
- **Project folders:** in the Projects section, each project has a **name/** row marked "samples". Click to enter: inside are the project's `.wav` files (up to 10 MB, name up to 16 characters), which you can download, upload, rename and delete. If the folder does not exist on the card yet, the row is still there and opens an empty folder; the first uploaded WAV creates it. There is no New folder button here: folders appear and disappear together with projects. Inside a project folder there is a **wt/** row ("wavetables"): the project's [wavetables](#wavetables) (`.wav`, name up to 16 characters), with the same actions; the `wt` folder is created by the first uploaded file and is deleted together with the project.
- **Moving a project over Wi-Fi:** download `name.mtp` and the files in its folder from one tracker; on the other, upload `name.mtp`, open the **name/** row ("samples") and upload the WAVs there (several at once is fine), and the wavetables into its **wt/**. Samples and wavetables get into flash when the project is loaded on the tracker.
- **Renaming and deleting a project:** renaming the `.mtp` moves its folder too (if there is no folder with the new name yet; renaming a lone `.bak` moves it when there is no `.mtp`). Deleting a project deletes the folder once neither the `.mtp` nor the `.bak` remains; the confirmation warns about this. If the folder was not moved or deleted, the page says so in its response. The exception is the project open on the tracker: renaming or deleting its file leaves the folder in place. You can change the files inside its folder; the next Save rewrites them from flash.
- **Open project:** if its file was replaced through the page, the tracker asks on exit: **Keep current** or **Reload from card**. If the file was deleted or renamed, toast "PROJECT FILE REMOVED"; the project stays in memory and can be saved.

**Over-the-air firmware.** The Firmware section of the page: the file `.pio/build/wt32/firmware.bin`. The tracker's screen shows "FIRMWARE n KB", then reboots after verifying the image. From PlatformIO, while the mode is open: `pio run -e wt32-ota -t upload`. If the upload is interrupted or the image is corrupted, the old firmware stays.


> The page has no password: while Wi-Fi mode is open, anyone on the same network can read, change and delete files and flash the tracker. Do not use this mode on other people's or public networks.

<a id="import"></a>

## MIDI import

Put the file in `/midi` and choose **FILE → Import MIDI…**. SMF format 0 and 1 are supported, up to 512 KB and 16,384 notes. A source is a "file track + channel" pair; there can be up to 32 of them.

Subfolders: the list shows folders first (`name/`), then files. Click a folder to enter, **\< Up** goes up one level, and at the root of `/midi` it is **\< Back**. The header shows the current path; the last opened folder is remembered until power-off. Files .mid and .midi; folder and file names up to 95 characters, Latin characters only (long ones are truncated on screen).

### Mapping screen

- For each source with notes: a row `name chN → T1…T16 / skip` with the note count and range, with **transpose** ±24 semitones below it. By default the first 16 sources go to T1–T16.
- Drum tracks are marked with an asterisk (`T5*`). A source note (after transpose) goes to the lane with the same Note; notes without a lane are dropped, several notes on one step are combined into a mask, and the step volume is taken from the first note. GAT is not written to a drum track.

| Parameter | Values | Meaning |
|---|---|---|
| Offset bars | 0–999 | Skip bars (4/4) from the start of the file. |
| Quantize | 6 resolutions | Grid and pattern resolution, default 1/16. |
| Pattern length | 4–128 | Pattern length. The file is cut into chunks of this length. |
| First pattern | P01–P16 | Pattern to start writing at. |
| Mono | Highest, Lowest, First | Which note to keep if several land on one track step. |
| Source channel | OFF / ON | ON writes a CHN fx with the channel from the file. |
| Use tempo | ON / OFF | Take the file's first tempo. "keep" means the file has no tempo. |
| Microtiming | OFF / ON | ON keeps off-grid timing using NDG. |

- **IMPORT** shows which patterns will be overwritten (`OVERWRITE P01-P04?`) and, after confirmation, stops playback and writes.
- Only the target tracks in the written patterns are cleared. Other tracks and patterns are untouched.
- Note length is written with GAT if it differs from the track's Def gate by more than 10 percentage points. Notes longer than 800% of a step are shortened. Empty bars at the end of the file also become patterns.
- Dropped notes (beyond P16, extra under the Mono rule, outside 0–127 after transposition) are listed in the summary message.
- After import, undo is cleared and the project is marked unsaved.

<a id="midi"></a>

## MIDI output

- Channel: the track's channel or the CHN fx.
- **Clock** (24 PPQN) is sent only during playback.
- **Play (start):** Program Change for tracks with a Program set, then Start.
- **Play (stop):** Note Off for all sounding notes, then Stop. Position goes to step 1.
- **Shift+Play (pause):** Note Off and Stop. **Resume:** Song Position Pointer (position within the current pattern) and Continue.
- Retriggering a note that is already sounding first sends its Note Off.

<a id="sound"></a>

## Internal sound

The built-in synthesizer: FM machines ([FM](#fm)), oscillators and wavetables ([SYNTH](#synth)), 808 / 909 machines ([DRUM](#drum)), a sampler (SAMPLE) and chiptune (CHIP), all with a [filter and LFO](#filter); 16 voices shared by all tracks, 32 kHz, mono. A track plays through it if TRACK → Out is set to **INT**; the instrument is set with TRACK → Instr or the PGM fx.

> **The SPK output is bridged (BTL): neither of the two pins is ground.** Connect a 4–8 Ω speaker directly to the two SPK pins. Headphones only through an isolated jack (not touching the case or GND) and 100–220 Ω series resistors, starting at low volume. Never connect the SPK pins to the board's GND, to ground, or to the inputs of other devices (mixer, audio interface, amplifier): that shorts the amplifier output.

3.5 mm headphone jack (optional, mono to both ears):

- SPK+ → 150 Ω → Tip (left)
- SPK+ → 150 Ω → Ring (right)
- SPK− → Sleeve (common; **not GND**, the jack is isolated)

Resistors of 100–220 Ω, one per ear. Only headphones go into this jack, not a cable to a mixer or speakers. The speaker keeps playing as well; to disconnect it, use a switch in its wire. More details in the README, "Sound" section.

- **Volume:** voice × instrument Volume × track Volume × MAIN on the [MIX](#mixer) tab (default 40%), with soft limiting.
- **Voices:** a shared pool of 24. POLY: up to 4 voices per track (an extra note takes the track's oldest voice); MONO: one voice with legato. FM: drums and CHORD are always mono on a track, and Mode applies only to TONE; DRUM is always mono. FM, DRUM and SYNTH with a WT oscillator together: no more than 8 voices; SYNTH using only SAW / SQR / TRI does not count toward this limit. A ninth heavy voice fades out the oldest heavy one in 4 ms (no click); filter tails do not count toward the limit. If the pool is full, a voice is stolen from another track.
- **CPU guard:** when the audio render nears its time budget (above 80 % on average, or one block over 100 %), the oldest voice fades out in 4 ms (releasing voices first) and the pool shrinks to the voices left; once the load drops below 65 % it grows back by one voice every 0.1 s. A heavy project thins out instead of crackling or restarting the device.
- **Latency:** the internal sound lags the MIDI tracks by about 14 ms, constantly, without jitter. MIDI tracks are not delayed.
- **Sequencer:** RAT, GAT, PRB, TIE, NDG, CHD, STR, CND, VRN, NRN work as on MIDI. Added: the [synth fx](#synthfx) SLD, VIB, ARP, VSL, OFS, CUT, slice selection SLC, locks of the FM, DRUM and SYNTH macros DEC, COL, SHP, SWP, CON, and filter locks FLT, RES. Track button LEDs also flash on INT notes. Mute, solo, stop and pause silence the sound the same way as MIDI notes.
- **Start:** Program Change is not sent to INT tracks; the instrument from PGM (back to Instr from TRACK), PBN and synth fx are reset.
- **Load:** `CPU NN%` in the right corner of the [status bar](#screen).
- Sample import, renaming and deletion, wavetable import, Compact and Clear cache are available only when stopped: there is no sound while writing to flash.

<a id="limits"></a>

## Limits

| What | How many |
|---|---|
| Tracks / patterns / steps | 16 / 16 / 4–128 |
| Song chain | 64 entries |
| Undo | 8 steps (pattern snapshots) |
| BPM / Swing | 20–300 / 50–75% |
| fx per step | 2 |
| Files in a list | 128 |
| Project / track / instrument name | 16 / 8 / 8 characters |
| MIDI import | 512 KB, 16,384 notes, 32 sources |
| Instruments / voices | 32 / 24 (POLY: up to 4 per track; fewer under the CPU guard) |
| Samples | 128 per project, name up to 16 characters; flash cache ~9.9 MB (≈ 2.5 min at 32 kHz), 128 entries |
| Wavetables | 32 per project, name up to 16 characters; 64 frames × 256 points, 96 KB of flash per table (in the cache shared with samples); 8 built-in; import file up to 256 frames × 2048, via Wi-Fi up to ~3 MB |
| Presets | `.mti` file 204 bytes (v2: 156, v1: 84; via Wi-Fi up to 1 KB), name up to 16 characters, up to 4 levels of folders in a type folder, up to 64 rows in one folder; 145 factory |
| WAV via Wi-Fi | 4 MB to `/samples`, 10 MB to a project folder (name up to 16 characters) |
| Long press | 0.5 s |

**Default project:** 120 BPM, C Chromatic, tracks TRK1–TRK16 on channels 1–16, volume 100, gate 50%, CC A 74, CC B 71, 16-step 1/16 patterns with no swing. All tracks are Out INT, track N uses instrument N, volume 100; instruments INS1–INS32 are FM TONE; master volume 40%, Preview ON.
