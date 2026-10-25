
# D-TRK — user manual

A tracker with a built-in synthesizer on two boards: the WT32-SC01 Plus (ESP32-S3: sequencer, screen, controls, MIDI OUT, Wi-Fi firmware updates) and a Teensy 4.1 synth board (the built-in sound, the microSD card and the sample bank). 16 tracks, a bank of 16 patterns, a song of up to 128 slots with follow actions, Fill with Euclid and arpeggio, chord and bass generators, MIDI file import. Each track plays either the built-in synthesizer (FM, analog-style drum machines, a wavetable synth, a macro oscillator, a sampler, chiptune, bytebeat and Atari Punk Console) through the synth board's stereo audio output (the Audio Shield), or external MIDI. Controls: a touchscreen, an encoder, the Play and Shift buttons, the chord buttons [A and B](#abkeys) and 8 [track buttons](#trackkeys).

Русская версия: [manual_ru.md](manual_ru.md)

1. [Quick start](#start)
2. [Controls](#hw)
3. [Screen and status bar](#screen)
4. [GRID: pattern editor](#grid)
5. [Effects (fx)](#fx)
6. [Fill, Euclid, arp, chords and bass](#euclid)
7. [TRACK: track settings](#track)
8. [INST: instruments](#inst)
9. [PROJ: project and pattern](#proj)
10. [BANK: pattern bank](#bank)
11. [SONG: the song](#song)
12. [FILE: saving and loading](#file)
13. [MIDI import](#import)
14. [MIDI output](#midi)
15. [Built-in sound](#sound)
16. [Limits](#limits)

<a id="start"></a>

## Quick start

1.  Insert a microSD card formatted **FAT32** or **exFAT** into the synth board (Teensy 4.1) and power on the device. Without a card, a demo beat loads on tracks 1–3.
2.  Press <kbd>Play</kbd>. The sequencer sends MIDI Start and plays from the first step. Press again to stop.
3.  In the **GRID** tab, tap a cell, then tap it again to enter edit mode. Press keys on the mini keyboard at the bottom or turn the encoder.
4.  In the **FILE** tab, choose **Save As…**, enter a name and press OK. The project loads automatically at the next power-on.

<a id="hw"></a>

## Controls

| Action | What it does |
|--------|--------------|
| <kbd>Play</kbd> | Start from the first step / stop. Stopped on the SONG tab with a non-empty song, plays the song from the slot under the SONG cursor; otherwise loops the current pattern (LOOP), see [SONG](#song). |
| <kbd>Shift</kbd>+<kbd>Play</kbd> | **Song from the cursor**, on any tab: stopped → the song starts from the slot under the SONG cursor; playing → a short press (Play released within 0.5 s) starts the song from that slot again. Empty song: toast `SONG EMPTY`, nothing happens. There is no pause. |
| <kbd>Shift</kbd>+<kbd>Play</kbd>, hold | During playback: **fill** while Play is held (steps with [CND FIL](#cnd) play, steps with NFL don't). Releasing after 0.5 s only ends the fill, the song does not restart. |
| <kbd>Shift</kbd> | A modifier; does nothing on its own. While held, a yellow `SHIFT` replaces the FILE tab. |
| Encoder turn | Moves the cursor or selects a row. In edit mode, changes the value. With Shift, a coarse step (usually ×10). |
| Encoder click | Enter or leave edit mode, choose a menu item. With a track button held — switch that button's turn between volume and pan (see [Track buttons](#trackkeys)). |
| Hold <kbd>A</kbd> + turn | Edit the value under the cursor without a click; releasing A commits it. See [Buttons A and B](#abkeys). |
| Encoder long press | 0.5 s: context menu or close a dialog. |
| Tap | Select. Tapping the selected item again enters edit mode. |
| Long tap | 0.5 s: context menu. |
| Drag | Scroll. In edit mode, changes the value: up increases, down decreases. |

Play works on any screen, even over an open menu. Tabs, pages, back and undo are also on the [A and B buttons](#abkeys).

<a id="trackkeys"></a>

### Track buttons

In GRID the buttons also record notes live (REC), and in GRID and SONG they hold effects (PERF) — see [REC and PERF](#live).

A block of 8 buttons. There are 16 tracks, so the buttons work on the **visible half** — 1–8 or 9–16, the same one [Overview](#grid) shows. The half is not a mode: the cursor selects it. Move past track 8 (<kbd>Shift</kbd>+turn, tapping a track name in Detail, a track button) and the buttons switch to 9–16; move back and they return to 1–8. The GRID header shows which half is active: `1-8` or `9-16` on the left above the step numbers.

| Action | What it does |
|--------|--------------|
| <kbd>N</kbd> | Select track N of the visible half (in 9–16, button 1 is track 9): the GRID cursor moves to it, TRACK shows it. |
| <kbd>Shift</kbd>+<kbd>N</kbd> | Mute / unmute the same track (N of the visible half), on any screen. The toast `TRACK n MUTE` / `TRACK n ON` shows the real number, 1–16. |
| <kbd>N</kbd> on the [SONG](#song) tab | Mute / unmute track N in the selected song slot. The half is the one holding the current track, as in GRID and TRACK (not the SONG cursor column). With [PERF](#live) on and the transport playing, N holds its effect instead; <kbd>Shift</kbd>+N still mutes. |
| Hold <kbd>N</kbd> + turn | Volume of track N (Volume, INT) on any screen, ×10 with <kbd>Shift</kbd>; toast `TRK5 VOL 100`. The turn does not reach the screen meanwhile. The press on its own works as usual (selects the track). Not active in GRID edit mode, REC or PERF (GRID or SONG) — there the buttons play. On a MIDI track: `TRK5 MIDI`, nothing changes. |
| Hold <kbd>N</kbd> + encoder click | Switch what the held-button turn changes: volume → pan of track N (INT); on a track with a [generator](#gen), volume → pan → the generator's **Flip**; toast `TRK5 PAN L12` / `TRK5 VOL 100` / `TRK5 FLIP 30%`. Turning then changes the chosen value (×10 with <kbd>Shift</kbd>). On a MIDI track with a generator the turn changes Flip right away. Releasing the button returns to volume: every new hold starts on volume. While a track button is held, the click always goes to this toggle (in GRID outside edit mode it no longer turns edit mode on); only where the buttons are busy — GRID edit mode, REC, PERF and the sample editor in INST — the click keeps its usual meaning. |
| <kbd>N</kbd> in GRID edit mode | Write the project scale degree into the step under the cursor and move the cursor one step down. On an INT track the note sounds immediately if Preview is on (FILE → SYSTEM). Buttons 1–7 are degrees I–VII, 8 is I an octave up. In scales with fewer than 7 notes (pentatonics, blues) the extra buttons continue the scale into the next octave; Chromatic enters major-scale degrees. The octave follows the note under the cursor, or on an empty step, the track's last entered note. |
| <kbd>N</kbd> in the sample editor (INST, SMPL page) | Play a slice to the end: 1 — the selected slice (if no slice is selected, the first one), 2 — the one after it, and so on. Works with any Slices setting (OFF, NOTE, FX), at the original pitch. Toast "SLICE n"; no such slice — "NO SLICE n"; no slices at all — "NO SLICES". |
| <kbd>Shift</kbd>+<kbd>N</kbd> in GRID edit mode | The same degree an octave up. |
| <kbd>N</kbd> in GRID edit mode on a [drum track](#drumtrack) | Toggle lane N in the step under the cursor. The cursor doesn't move; no Shift needed. |

Degree entry and slices in the sample editor don't depend on the half — there the button number is what counts. OFF and clearing a step work as before, through the menu.

<a id="abkeys"></a>

### Buttons A and B

Two buttons: B under Shift, A under Play. Both are chord buttons: hold one and turn the encoder or press another button. A short press on its own (a tap) counts on release and only if nothing else happened while the button was held. If both are held, B wins.

| Action | What it does |
|--------|--------------|
| Hold <kbd>A</kbd> + turn | Edit the value under the cursor: enters edit mode if it is off and changes the value at once. Releasing A commits the value and leaves edit mode (if A entered it). Works on parameter lists, the GRID cell, name rows, SONG (the slot's pattern), sample markers, MIX (the [scene crossfader](#scenes), or the selected pan). With [PERF](#live) on and no effect held, on GRID and SONG (except in the FOL column) it moves the crossfader too. |
| Hold <kbd>A</kbd> + encoder click in MIX, SCENE view | Unset the master parameter's lock in the edited scene (see [SCENE view](#scenes)). Elsewhere the click works as usual. |
| Hold <kbd>A</kbd> + <kbd>Shift</kbd> + turn | Whatever <kbd>Shift</kbd>+turn does in edit mode: ×10, a note by octaves, a sample marker to the nearest onset, the cursor position on a name row. |
| Hold <kbd>A</kbd>, press <kbd>B</kbd> | Cancel the edit: the value made with A is restored. |
| <kbd>A</kbd> tap in GRID | Empty step: put the track's last note (on a [drum track](#drumtrack), toggle the lane under the cursor). Step with a note: hear it. |
| <kbd>Shift</kbd>+<kbd>A</kbd> in GRID | Clear the step under the cursor. |
| <kbd>Shift</kbd>+<kbd>A</kbd> in MIX | Open / close the [SCENE view](#scenes). |
| <kbd>A</kbd> tap / <kbd>Shift</kbd>+<kbd>A</kbd> in the sample editor (INST, SMPL page) | Next / previous marker, like **\>** / **\<** — see [SAMPLE](#sample). |
| Hold <kbd>Shift</kbd> + <kbd>A</kbd> + <kbd>N</kbd> | Solo / unsolo track N of the visible half; toast `TRACK n SOLO` / `TRACK n ON`. |
| Hold <kbd>A</kbd> + <kbd>N</kbd> in MIX | Scene N on the crossfader end A (toast `A = SCENE 3`), also in the SCENE view and with the XY pad open; see [scenes](#scenes). Elsewhere it does nothing (the press does not reach the screen). |
| Hold <kbd>B</kbd> + turn | Switch tabs in a ring: GRID → TRACK → INST → MIX → BANK → SONG → PROJ → FILE → GRID. In MIX in the SCENE view: the master parameter MAIN … EQHI instead (the tab stays). |
| Hold <kbd>B</kbd> + <kbd>Shift</kbd> + turn | Previous / next page in TRACK, INST and PROJ, section in FILE (wraps). The cursor stays on the page tabs if it was there, otherwise it goes to the first row. |
| <kbd>A</kbd>+<kbd>B</kbd> (press both, release) on GRID, SONG, MIX | With [PERF](#live) on: XY pad on / off (see [XY pad](#xypad)). Nothing happens if a turn or a track button came in between. |
| <kbd>B</kbd> tap | With the XY pad open: close it. Back: close a menu, dialog, picker, the on-screen keyboard or the file list; cancel copy mode in BANK; leave edit mode keeping the value; leave tempo editing. In the wavetable picker it steps back one level. In GRID, one thing per tap, in this order: close Fill / Transpose → clear the selection → edit mode off → REC off → PERF off. In SONG: pattern edit off → PERF off. In MIX: leave the SCENE view (in CPY mode, cancel the copy first) → drop the pan or crossfader selection → PERF on / off. |
| <kbd>Shift</kbd>+<kbd>B</kbd> | [Undo](#undo); on the SONG tab, undo of song edits. |
| Hold <kbd>B</kbd> + <kbd>N</kbd> in MIX | Scene N on the crossfader end B (toast `B = SCENE 3`), as A + N. Elsewhere it does nothing. |
| Hold <kbd>B</kbd> + <kbd>Shift</kbd> + <kbd>N</kbd> | Queue pattern N of the current bank of 8 (patterns 1–8 or 9–16, the bank holding the pattern being edited). While playing it switches at the end of the pattern (toast `PATTERN 05 NEXT`); when stopped, at once. While the song plays (SONG mode) a queue is ignored (toast `SONG MODE`). |

The encoder click works as before; A is just a faster way to edit one value: hold, turn, release.

### Wiring

The pinout, the soldering diagram and the checks before power-up are in [wiring.md](wiring.md); the link to the synth board is there too. All buttons sit on one I2C expander: without it the encoder button, Play, Shift, A, B and the track buttons do not work. There is no MIDI input: the device is always the clock master.

The display board's built-in microSD slot and its NS4168 amplifier are not used: the card and the Audio Shield with the headphone jack are on the synth board (see [Built-in sound](#sound)).

<a id="screen"></a>

## Screen and status bar

    S03 P05*  >P07  120 BPM  5/16  PLAY  L3  [=] 85%  CPU  42%
    tab work area
    GRID  TRACK  INST  MIX  BANK  SONG  PROJ  FILE

| Element | Meaning |
|---------|---------|
| `P05` | The pattern currently playing (LOOP mode). In SONG mode — `S03 P05`: third song slot, pattern 5; `S--` — the song has not been heard yet. During play, tapping this part of the status bar toggles SONG / LOOP at the end of the current pass — see [SONG](#song). |
| `*` | There are unsaved changes. |
| `>P07` | Queued pattern: switches at the end of the pass. Not shown in SONG mode. |
| `120 BPM` | Tempo. A tap enters tempo editing (red): encoder ±1, ±10 with Shift, range 20–300. Leave with a click, a tap on the work area or a tab change. |
| `5/16` | Step / pattern length. |
| `PLAY` | State: PLAY, STOP. A tap works like the Play button; <kbd>Shift</kbd>+tap, like <kbd>Shift</kbd>+<kbd>Play</kbd> (the song from the SONG cursor). |
| `L3` | Pattern pass counter. The CND condition uses it. Up to `L9999`, beyond that `L10K+`. While fill is held — `FILL`. |
| `[=] 85%` | Battery charge from the [INA226](wiring.md#ina226-battery-monitor): the icon fills by level, a lightning bolt while charging. Green while charging, yellow at 20% and below, red at 10% and below, otherwise dim. Estimated from the voltage corrected for the load current and smoothed (~30 s); while discharging it only goes down (in the first minute after power-on it may move both ways). While charging it reads high and shows 100% before charging ends. Not shown without the sensor. At 10% (not while charging) a single toast `BATTERY LOW - SAVE`; it repeats only after a recharge. |
| `CPU 42%` | Synth board load: the average time to compute an audio block over the last 0.5 s, as a % of the block duration (128 samples, 2.9 ms). Gray below 60%; yellow at 60–84% or if any block in those 0.5 s took longer than a block; red from 85% or for 2 s after an audio dropout (an audible click). While no audio is being computed, the last value is shown. |
| `NO SYNTH` | Red, in place of CPU: the synth board does not answer. INT tracks are silent and the card is unavailable; MIDI OUT still plays. |
| `SYNTH FW` | Red, in place of CPU: the synth board's firmware protocol does not match the tracker's. A full-screen notice blocks the UI; a click or a tap on **UPDATE** opens a menu: **Update synth…** (flash from `/firmware/teensy.hex` on the synth card, as FILE → SYSTEM → Update synth) or **Wi-Fi firmware…** (see [below](#wifi)). |
| `SD!` | Red, in place of `CPU` for 2 s (`SD! 42%`): a sample voice ran out of data (the synth board's card fell behind) and faded out for a moment. Playback goes on. Often: a slow card, many long samples pitched up. See [Samples and SD cards](#streaming). |
| `SYNC` | Yellow, in place of CPU: the sound state (instruments, mixer, samples) is being sent to the synth board. Play waits for it for a moment. |

Tabs switch by tapping the bottom bar or with <kbd>B</kbd>+turn (see [Buttons A and B](#abkeys)). Yellow messages (toasts) cover the right part of the status bar (battery and CPU) for 1.5 seconds.

**Menus** open in the center of the screen and show up to 12 items; long lists scroll by dragging. Encoder: turn to select (wraps around: after the last item comes the first), click to execute, long press to close. All parameter and file lists wrap around the same way. A tap outside the menu also closes it. Gray items are currently unavailable.

<a id="grid"></a>

## GRID: pattern editor

GRID always edits the pattern that is playing. When the pattern changes (queue, song, loading), the cursor moves to step 1, edit mode turns off and the selection is cleared.

### Two views

- **Overview** (default): 8 columns — one half of the 16 tracks, the one the cursor is in: 1–8 or 9–16. Which half is on screen is shown on the left above the step numbers: `1-8` or `9-16`. The half is not a separate mode: when the cursor moves from track 8 to track 9 (<kbd>Shift</kbd>+turn, tapping a track name in Detail, a [track button](#trackkeys)), the second half is shown; move back and the first returns. The [track buttons](#trackkeys) follow the same half. A cell shows the note, a velocity bar and a yellow dot if the step has fx. The dot is gray if all of the step's fx are synth fx on a MIDI track (they have no effect there). Every fourth step is highlighted, and the playback row is marked.
- **Detail**: one (the current) track across the full width, 14 fields: NOTE, VEL and six pairs FX1…FX6 with their values. The track number (`T3`) is above the step numbers, the name above NOTE/VEL. <kbd>Shift</kbd>+turn past the last field moves to the next track; the track buttons switch it too. Tapping the name row mutes (with <kbd>Shift</kbd>, solos) that track. Synth fx on a MIDI track are shown in gray. The playback row and follow show the track's own step (Pat len, [Speed](#track)): a x2 / x4 track moves 2 / 4 rows per pattern step. Overview shows the pattern step.

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
| Hold <kbd>A</kbd> + turn | Enter edit mode and change the value (the mini keyboard / lane pad stays hidden); releasing A leaves edit mode | Change the value; releasing A keeps edit mode on |

### What a turn changes in edit mode

- **NOTE.** On an empty step, the track's last entered note is placed (C-4 at first). After that the note moves by degrees of the project scale, by octaves with Shift. The encoder cannot clear a step or set OFF; use the menu for that. On an INT track every entered note (encoder, mini keyboard, track buttons) sounds immediately with the track's instrument if Preview is on (FILE → SYSTEM).
- **VEL.** 0–127, ×10 with Shift. A value of 0 is shown as `...` and means "the track's default velocity".
- **FX1…FX6.** Cycles through the commands by group, related ones next to each other: `...`; notes and arp CHD, STR, ARP, ARM, ARS; time RAT, NDG, GAT, TIE, OFF, CUT; randomness PRB, CND, VRN, NRN, FLP; pitch and volume SLD, VIB, PBN, VSL, ACC; sound FLT, RES, DRV, DTY, BIT, SRR, GTE, DEC, COL, SHP, SWP, CON; LFO LFO, LFD, LFS, LFW, LFT, LFR; sample OFS, SLC; sends and pan DLY, RVB, MOD, PAN; MIDI only CHN, CCA, CCB, PGM (turning back from an empty slot goes straight to PGM). The order is the same as in the [table](#fx). Choosing a command sets its default value and shows its full name in the status line (for example, VIBRATO). On an empty slot, the first turn places the last fx written to this slot on this track, with the same value — handy for repeating an fx step after step; further turns cycle through the commands as usual.
- **FX VAL.** ±1, ×10 with Shift. For CND the step is the same with Shift. On an empty slot the first turn likewise inserts the track's last fx; if there was none yet, the value doesn't change.

One edit session (from entering to leaving edit mode) is one undo step.

### Mini keyboard

Appears at the bottom of the screen when editing the NOTE field. Twelve keys — the octave of the current note. A yellow bar marks scale notes, a red frame the current note. Tapping a key writes the note — any note; the scale doesn't restrict it here. <kbd>Shift</kbd>+encoder turn changes the octave.

<a id="drumtrack"></a>

### Drum track

A track whose TRACK → Instr is set to a [KIT](#kit) instrument is a drum track: a step holds not a note but a set of 8 lanes (kick, snare, hats…). The step stores a lane mask and one velocity for all of the step's lanes.

- **Overview:** the cell shows 8 small squares; a filled one means the lane sounds. There is no velocity bar; the fx dot is in the top corner of the cell.
- **Detail:** the NOTE field shows the same squares, the VEL field the step velocity (`...` — the track's Def vel).
- **Editing NOTE:** a turn moves the red frame across lanes, <kbd>Shift</kbd>+turn toggles the lane under the frame. Instead of the mini keyboard, the bottom shows a **pad of 8 buttons** with the lanes' sample (or instrument) names; a lit one means the lane is in the step; a tap toggles the lane. [Track buttons](#trackkeys) 1–8 do the same in edit mode. A lane sounds as soon as it's turned on (Preview in FILE → SYSTEM).
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
| Copy step · Paste · Clear step · Note OFF · Select · Copy track · Clear track · Transpose track… · **Fill…** · **Capture gen** (on a track with a generator) · **Capture evo** (on a track with Evolve on) · **Capture var** (on a track with VAR on, instead of Capture evo) · **Resample track** · **Resample pattern** · **Rec: ON/OFF** · **Perf: ON/OFF** · Detail view / Overview · Follow: ON/OFF · Undo | Copy sel · Paste · Clear sel · Note OFF sel · Transpose… · **Fill…** · Undo · Clear selection |

- **Transpose…** opens a dialog: **Amount** and **Mode** (SCALE — scale degrees, CHROMATIC — semitones), with OK / CANCEL buttons. The range is two octaves up and down (in SCALE that is 2 × the number of scale notes; ±14 for major). <kbd>Shift</kbd>+turn changes Amount by an octave. Long press — Cancel. Affects only steps with a note; without a selection, the whole track; one undo step. It is a shift, not a state: Amount starts at 0 every time the dialog opens; to restore the notes, apply the opposite shift (+12, then −12). Mode is remembered until power-off.
- **Capture gen** (a track with a [generator](#gen), not a drum track) writes the generator's loop into the pattern: one pass of the track length. PITCH replaces the notes of steps with a note; GATE puts a note on the hit steps and removes it from the others; OFF and step fx stay, Flip is not applied (the loop as it is). While playing, the loop is taken from where it is now; when stopped, from the start (from the seed). Then Gen is turned off so the pattern sounds as written; toast `GEN CAPTURED`, one undo step. While [Evolve](#evolve) has changes on the track the heard notes differ from the loop: it refuses with `EVO: CAPTURE EVO FIRST` (Capture evo first, or wait for a stop); likewise with a [VAR](#var) generation other than 0 — `VAR: CAPTURE VAR FIRST`. In BYTE PITCH / BYTE GATE, Capture gen writes the formula's notes the same way as PITCH / GATE.
- **Capture evo** (a track with [Evolve](#evolve) on) writes the track's heard drift into the edited pattern, then the drift starts over; see [Capture evo](#captureevo).
- **Capture var** (a track with [VAR](#var) on) writes what the track plays now (VAR, and EVO on top of it) into the edited pattern; see [Capture var](#capturevar).
- Paste inserts at the cursor position or at the top-left corner of the selection. It doesn't go past track 16 or the pattern length; the selection is also clipped at track 16.
- The clipboard survives loading another project — so you can move parts between projects.
- If the pattern changed while the menu was open, the action is not performed (toast "PATTERN CHANGED").

<a id="live"></a>

### REC and PERF: track buttons live

Two modes from the GRID context menu. They exclude each other and edit mode: turning one on turns the others off. The header above the step numbers shows `REC` (red) or `PERF` (yellow). They work during playback; when stopped, the buttons behave as usual.

PERF also works on the [SONG](#song) tab: **Perf: ON/OFF** is in the SONG row menu too, and it is one shared mode — turned on in either menu it is on in both (turning it on in SONG leaves pattern edit; GRID turns REC off when you come back). In SONG with PERF on, button N holds its effect while playing, the encoder turns a held effect's depth instead of moving the cursor, and <kbd>Shift</kbd>+N mutes the track live. The SONG header shows the same `PERF …` badge in place of `n/128`.

- **REC** — records from the track buttons into the current track. Button N writes scale degree N (in the octave of the track's last note) into the playing step; a press in the second half of a step lands in the next one. On a track with its own Speed this is the track's own step that is sounding. <kbd>Shift</kbd>+N erases that step's note. On a drum track button N sets lane N, <kbd>Shift</kbd>+N removes it. Each pattern pass is one undo step. Choose the track to record into by tapping its name (in REC this is not mute) or with <kbd>Shift</kbd>+turn. The recorded note sounds immediately if Preview is on.
- **PERF** (GRID and SONG) — punch-in effects on the whole internal mix while a button is held. Which effect is on which button, and the Value it starts at, is set in [PROJ → PERF](#proj) and stored in the project: button 1 can hold REPEAT 1/8 and button 2 REPEAT 1/32. The press starts the effect at the button's Value and shows it (`PERF REPEAT 1/8`); turning the encoder while the button is held changes the depth (<kbd>Shift</kbd> ×10 on the wide ranges: FILTER, DRIVE, DELAY THROW, REVERB THROW, FREEZE). The turn is not remembered: every press starts at the button's Value (PROJ → PERF). Release — the effect is gone. <kbd>Shift</kbd>+N still mutes. While no button is held, <kbd>A</kbd> + turn moves the [scene crossfader](#scenes) (<kbd>Shift</kbd> ×8).

| Effect | What it does | Encoder | Default |
|--------|--------------|---------|---------|
| REPEAT | loops the mix from the last grid step boundary | 1/2, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 — applies at the next loop | 1/16 |
| ROLL | a repeat from 1/8, the loop halves every N loops down to 1/64 | N = 1…4 | 2 |
| REVERSE | plays the buffer backwards from the press point, looping over the window | 1/4, 1/2, 1, 2 bars | 1 bar |
| SCRATCH | the read head swings (sine) around the press point | period 1/4, 1/8, 1/16, 1/32 | 1/8 |
| TAPE STOP | the speed falls to 0, then silence; release returns the live mix | 1/16, 1/8, 1/4, 1/2, 1 bar | 1/4 bar |
| GATE | 50 % gate in phase with the grid | 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 | 1/16 |
| FILTER | at OFF nothing happens; counter-clockwise closes a low-pass (20 kHz → 100 Hz), clockwise opens a high-pass (20 Hz → 8 kHz), with a little resonance | LP 64 … OFF … HP 63 | OFF |
| CRUSH | bit depth | 2…12 bits | 6 |
| DOWNSAMPLE | sample and hold | 16 kHz, 8 kHz, 4 kHz, 2 kHz, 1 kHz, 500 Hz | 8 kHz |
| DRIVE | saturation, level compensated | 0…100 | 60 |
| DELAY THROW | the whole mix into the [delay](#delay) send while held, the tail rings out | send 0…127 | 100 |
| REVERB THROW | the same into the [reverb](#reverb) | send 0…127 | 100 |
| FREEZE | the reverb frozen (feedback 1, input closed), the tail hangs; the encoder sets the live mix level under the frozen tail (DRY 0…100) | 0…100 | 50 |
| MOD THROW | the whole mix into the [MOD bus](#modbus) send while held, the tail rings out; with Mod type = OFF it throws into CHORUS | send 0…127 | 100 |
| TAPE WOBBLE | while held, the delay becomes TAPE and Wear rises to the encoder value (never below the project's): the echo drifts and "stretches"; does nothing in PING-PONG | Wear 0…127 | 127 |
| --- | the button does nothing | | |

The header shows the held effects in press order by short name: `PERF RPT FLT CRS` (RPT ROL REV SCR TPS GAT FLT CRS DWN DRV DLY RVB FRZ MOD WOB). Several buttons held together stack. The buffer effects (REPEAT, ROLL, REVERSE, SCRATCH, TAPE STOP) share one read head, so only the last pressed of them plays; release it and the previous still-held one returns. Everything else stacks freely. The encoder turns the last pressed held effect. Stop, PERF off or leaving the tab (GRID or SONG) releases everything. PERF touches INT sound only — MIDI tracks are not affected — and only live: Export and Resample do not include it. The buffer effects play from a 6 s buffer in the synth board's PSRAM; without PSRAM (or after it failed a check) there is none: toast `PERF: NO MEMORY FOR REPEAT`, and they are silent.

<a id="xypad"></a>

**XY pad.** With PERF on, <kbd>A</kbd>+<kbd>B</kbd> on GRID, SONG or MIX turns the work area into a pad (the status bar and the tabs stay). A finger on the pad holds two effects — the X and Y axes from [PROJ → PERF](#proj) (new project: X FILTER, Y CRUSH); the position sets the depth: left / bottom is the start of the range, right / top its end. List-valued effects (REPEAT 1/2…1/32, GATE, CRUSH, DOWNSAMPLE and so on) split the axis into equal zones, drawn as lines. FILTER: LP on the left / bottom, OFF in the middle band, HP on the right / top. Lift the finger — the effects end. Works only while playing (stopped: `XY: PRESS PLAY`; a stop during a touch releases the effects). The PERF buttons and the encoder work as usual: an axis effect a button already holds is not restarted by the finger, the finger drives its depth, and it lives while the button or the finger holds it. An axis set to `---` does nothing; Y on the same effect as X, or a buffer effect (REPEAT, ROLL, REVERSE, SCRATCH, TAPE STOP) on both axes, does nothing (the row is dim): they share one read head. Both axes doing nothing — toast `XY: NO EFFECTS (PROJ > PERF)`. The finger's trail is drawn over the visuals (on FREEZE the frame stands still, the trail too). Leave: <kbd>A</kbd>+<kbd>B</kbd>, a <kbd>B</kbd> tap, another tab or PERF off. While the pad is open the encoder click and long press and <kbd>A</kbd> do nothing — the screen under the pad does not change; <kbd>A</kbd> + turn, while no button is held, moves the [scene crossfader](#scenes) (<kbd>Shift</kbd> ×8). If the encoder was turned while <kbd>A</kbd> was held, <kbd>A</kbd>+<kbd>B</kbd> does not toggle the pad.

<a id="resample"></a>

### Resample

**Resample track** and **Resample pattern** in the GRID menu record the playing pattern with the built-in sound into a new project sample: only the current track, or all audible tracks (respecting mute / solo). The sample is named `RS1`, `RS2`… (the first free name), normalized to −1 dBFS, trailing silence is trimmed, and there is no delay / reverb tail past the end of the pattern. The sample is mono (the bank is mono): left and right are mixed, (L + R) / 2. You can then select it in INST (SAMPLE or a KIT lane) and save it with the project.

- Only when stopped (`STOP FIRST`). The pattern is rendered twice (measuring, then recording), each pass roughly in real time — progress `RENDER nn%`; a long encoder press or Play cancels (`CANCELLED`).
- Nothing longer than 60 s is recorded: `SAMPLE CAP`, the sample is still added. No room in the synth board's flash — `BANK FULL`; the same if the list already has 128 samples.
- The result: `RS3 2.1s`. The render matches playback (without the output latency); notes pushed past the end of the pattern by NDG / swing are cut off.

<a id="undo"></a>

### Undo

<kbd>Shift</kbd>+long press in GRID or <kbd>Shift</kbd>+<kbd>B</kbd> on any screen. 8 steps. Each step is a snapshot of the whole pattern (with 16 tracks the snapshot doubled in size, so the depth is 8 rather than 32). It undoes edits in GRID, menu operations, Fill, and also Copy-to, Clear and Length in BANK. TRACK and PROJ parameters, tempo and mute/solo are not undone. Song edits have their own history on the [SONG](#song) tab (also <kbd>Shift</kbd>+<kbd>B</kbd>, 8 steps). There is no redo. Load, New and import clear the history.

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
| `CHD` | 36 types × 4 inversions: `tri`…`a11`, `MAJ`…`QRT`, inversion digit `MA72` | both | Chord built from the note: up to 4 notes; notes above 127 are dropped. Lowercase types follow the project scale, UPPERCASE ones are fixed intervals — see [CHD chords](#chd). |
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
| `FLP` | 0–100 | both | Track generator ([GEN](#gen)): the register shift on this step inverts the bit with this chance instead of the track's Flip. `FLP 0` pins this note of the loop, `FLP 100` is a sure flip. Does nothing in PITCH on a step without a note and with Gen OFF (grey). |
| **Pitch and volume** | | | |
| `SLD` | 1–255 | INT | Glide, time ≈ value × 4 ms (4 ms … 1.02 s). |
| `VIB` | 00–FF (xy) | INT | Vibrato: x — rate (x × 0.5 Hz), y — depth (up to ±2 semitones at F). |
| `PBN` | −64…+63 | both | Pitch bend: 8192 + v×128. Doesn't reset by itself; return to 0 with a separate step. On INT — shifts the pitch of the track's voices by up to ±2 semitones. |
| `VSL` | −64…+63 | INT | Smooth volume change over one step: ±64 spans the full range. |
| `ACC` | 00–FF | both | [Drum tracks](#drumtrack) only: a mask of lanes at full step velocity (bit 0 — lane 1); the step's other lanes sound at 60%. On other tracks it is shown in gray and has no effect. |
| **Sound (instrument locks)** | | | |
| `FLT` | 0–127 | INT | [Filter](#filter) cutoff for the step's note, any instrument type. |
| `RES` | 0–127 | INT | Filter reso for the step's note, any instrument type. |
| `DRV` | 0–127 | INT | Drive (overdrive) of the [track insert](#insert) for the step's note; on a step without a note, for the sounding note. Replaces TRACK → INS → Drive. Acts on the whole track (see [below](#trackfx)). |
| `DTY` | TNH, CLP, FLD, RCT, ASY | INT | Drive type of the [track insert](#insert) for the step's note; on a step without a note, for the sounding note. Replaces TRACK → INS → Drive type; does nothing while Drive = 0. Acts on the whole track (see [below](#trackfx)). |
| `BIT` | 0–127 | INT | Track insert bitcrusher for the step's note: bit depth from 16 bits (0 — off) down to 2 bits (127). After Drive. The constant value is TRACK → INS → Bit crush; the step lock replaces it. |
| `SRR` | 0–127 | INT | Track insert sample-rate reduction for the step's note: 0 — off, 127 — each sample is held for ≈ 64 samples (a rough "digital" sound). Together with BIT — classic lo-fi. The constant value is TRACK → INS → Downsample. |
| `GTE` | OFF, 1–127 | INT | Depth of the track's [trance gate](#trancegate) for the step's note; on a step without a note, for the sounding note. Replaces TRACK → INS → Trance: OFF turns the gate off, 127 — closed steps go silent. The pattern, Rate and the other settings belong to the track. Acts on the whole track (see [below](#trackfx)). |
| `LFO` | 1–4 | INT | Which LFO of the instrument this step's `LFD`…`LFR` act on; without it — LFO 1. Slot order doesn't matter. |
| `LFD` | −64…+63 | INT | Depth lock of the selected LFO (turns the LFO on even if the instrument's Depth is 0). |
| `LFS` | 0–127 | INT | Rate lock of the selected LFO: in FREE — the Hz scale, in TEMPO — the division index (above 11 = 8 BARS); not applied to a Retrig OFF LFO (its phase is the instrument's). |
| `LFW` | SIN, TRI, SAW, SQR, RND, BYT | INT | Wave lock of the selected LFO. |
| `LFT` | PIT, DEC, COL, SHP, SWP, CON, VOL, CUT, DRV, RES, FEN, DLY, RVB, PAN, BIT, SRR, FIN, DUT, SUB, NOI, SM2, FLD, RNG, RA1–RA4, DP1–DP4, RG1–RG4, MOD | INT | Dest lock of the selected LFO, same targets as Dest (RA / DP / RG — L1–L4 RATE / DEPTH / RTRG); a target the type lacks does nothing. |
| `LFR` | 00–FF | INT | Restarts the selected LFO's phase at value / 256 (`00` — start of the period, `80` — middle): Retrig OFF — the instrument's shared phase, Retrig ON — the step's notes or the sounding notes. |
| `DEC` | 0–127 | INT | FM, DRUM, SYNTH, STRING, LOFI: the DECAY macro for the step's note (for DRUM and LOFI — the machine slot with the same number, for SYNTH — SHP1, for STRING — DAMP). |
| `COL` | 0–127 | INT | FM, DRUM, SYNTH, STRING, LOFI: the COLOR macro for the step's note (for DRUM and LOFI — the machine slot with the same number, for SYNTH — SHP2, for STRING — BRIGHT). |
| `SHP` | 0–127 | INT | FM, DRUM, SYNTH, STRING, LOFI: the SHAPE macro for the step's note (for DRUM and LOFI — the machine slot with the same number, for SYNTH — MIX, for STRING — STRUCT). |
| `SWP` | 0–127 | INT | FM, DRUM, SYNTH, STRING, LOFI: the SWEEP macro for the step's note (for DRUM and LOFI — the machine slot with the same number, for SYNTH — DET, for STRING — POS). |
| `CON` | 0–127 | INT | FM, DRUM, SYNTH, STRING, LOFI: the CONTOUR macro for the step's note (for DRUM and LOFI — the machine slot with the same number, for SYNTH — SENV, for STRING — EXC). |
| **Sample** | | | |
| `OFS` | 0–255 | INT | Starts the sample at value/256 of the Start–End region. SAMPLE only. |
| `SLC` | 0–31 | INT | Sample slice for the step's note: 0 — the first slice, 31 — the 32nd. SAMPLE with Slices = FX only (see [slices](#slices)). |
| **Sends** | | | |
| `DLY` | 0–127 | INT | Track send to [delay](#delay) for the step's note (replaces the instrument's Dly send), any instrument type. One send per track (see [below](#trackfx)). |
| `RVB` | 0–127 | INT | Track send to [reverb](#reverb) for the step's note (replaces the instrument's Rvb send). |
| `MOD` | 0–127 | INT | Track send to the [MOD bus](#modbus) for the step's note (replaces the track's Mod send). |
| `PAN` | L64…C…R63 | INT | Pan lock (replaces the track's [Pan](#track)): for the step's notes, on a step without a note — for the track's sounding notes (moves smoothly, no click). One pan per track. The LFO PAN target moves around it. |
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

SLD, VIB, ARP, VSL, OFS, CUT, DEC, COL, SHP, SWP, CON, FLT, RES, SLC, DLY, DRV, DTY, BIT, SRR, GTE, RVB, MOD, PAN, ARM only take effect on tracks with Out = INT. On MIDI tracks they send nothing and are shown in gray. VIB and ARP values are hexadecimal: `47` = x 4, y 7.

- **Step with a note.** The fx apply to that step's note. A new step with a note resets VIB and ARP: to keep the vibrato or arpeggio going, repeat the fx.
- **Step without a note** (empty or OFF). VSL, CUT and ARP act immediately on the track's sounding voices; VIB changes the parameters of the sounding vibrato. SLD glides the sounding voice to the track's last played note. VIB and ARP hold until the next step with a note.
- **SLD on a step with a note:** if the track's voice is still sounding, it glides to the new note without a new attack (legato); otherwise the new note starts from the previous note's pitch.
- **ARP:** each of the three values lasts a third of a step. `00` turns it off.
- **VSL** is counted from the note's velocity; a full rise or fall takes exactly one step (taking the pattern resolution into account).
- **CUT:** tick length is taken from the tempo (tick = 1/96 of a quarter note) and doesn't depend on the resolution. If CUT is shorter than the gate, the note is cut earlier.
- **OFS** works only on SAMPLE and only at the start of a note; with Reverse it counts from the end. If the note plays a slice, OFS counts within the slice.
- **SLC** works only on SAMPLE with Slices = FX and only at the start of a note, like OFS: the first note-on of the step plays the chosen slice at the note's pitch (with a chord or RAT, the other notes play the whole region). Without SLC, or if the number is not less than the number of slices, the whole Start–End region plays. On a step without a note SLC does nothing.
- **DEC, COL, SHP, SWP, CON** (macro p-locks) work only on [FM](#fm), [DRUM](#drum), [SYNTH](#synth) and [STRING](#string); they are ignored on CHIP and SAMPLE. On a step with a note, the value replaces the instrument's macro for that note; on a step without a note, for the track's sounding note until the next note. The next step doesn't inherit the lock. For TONE and CHORD the envelope decay is fixed at the start of the note: DEC on a step without a note (and LFO on DECAY) doesn't change the length of a note already sounding.
- **FLT, RES** (filter p-locks) work on all types, but only when Filter is not OFF. The value replaces the instrument's Cutoff or Reso: on a step with a note — for that note; on a step without a note — for the track's sounding voices until the next note. The filter envelope, Key track and LFO CUTOFF are added to the lock value.
- **LFD, LFS, LFW, LFT** (LFO p-locks) work like FLT: on a step with a note — for that note; on a step without a note — for the track's sounding voices until the next note. One LFO per step can be locked — the one picked by `LFO`.
- **DLY** (delay send p-lock) works the same way: on a step with a note — the send from that step; on a step without a note — the send of the track's sounding notes until the next note. Handy for echo "throws" on individual hits: instrument Dly send at 0, `DLY 127` on the steps you want.
- <a id="trackfx"></a>**DRV, DTY, BIT, SRR, GTE, DLY, RVB, MOD, PAN** act not on a single note but on the whole track: a track has one [insert](#insert), one set of sends and one pan. The value comes from the newest sounding note of the track — its lock and LFO; a new note with a different lock also changes the sound of the notes already sounding. That is why there is no per-note autopan (chord notes sit at one point), and a drum track with a KIT sends to the delay and reverb by the KIT's own sends — the sends of the lane instruments are ignored.

Other fx on INT: RAT, GAT, PRB, TIE, NDG, CHD, STR, CND, VRN, NRN, FLP work as on MIDI; PBN and PGM — see the table; CHN, CCA, CCB are ignored.

<a id="chd"></a>

### CHD chords

The step's note is the chord's root; CHD adds up to 3 notes to it (4 in total, notes above 127 are dropped). A CHD value is a chord type plus an inversion. The cell shows 4 characters: the type's 3 and the inversion digit — `MA7 ` (root position), `MA71`, `tri2`. A turn goes through all 36 types in root position, then all of them with inversion 1, 2 and 3.

- **Inversion** k moves the k lowest notes of the chord up an octave: C E G with inversion 1 is E G C', with inversion 2 G C' E'. The step's note stays the root, it just is no longer the bottom note. A note that would go above 127 or land on a note already in the chord is dropped.
- **lowercase types are diatonic:** degrees of the project scale counted from the note, so in C major `tri` on C is C E G and on D is D F A. With the chromatic scale they are built as in major from the note. Exceptions: `pwr` and `oct` are always note + 7 and note + 12 semitones.
- **UPPERCASE types are fixed:** semitones from the note, the scale is ignored. `MAJ` on D in C major is D F# A.

Diatonic (scale degrees, 1 = the note):

| Type | Degrees | | Type | Degrees |
|------|---------|---|------|---------|
| `tri` | 1 3 5 | | `ad9` | 1 3 5 9 |
| `7th` | 1 3 5 7 | | `pwr` | note + 7 semitones |
| `su2` | 1 2 5 | | `oct` | note + 12 semitones |
| `su4` | 1 4 5 | | `9th` | 1 3 7 9 |
| `6th` | 1 3 5 6 | | `7s4` | 1 4 5 7 |
| | | | `a11` | 1 3 5 11 |

Fixed (semitones from the note):

| Type | Semitones | Chord | | Type | Semitones | Chord |
|------|-----------|-------|---|------|-----------|-------|
| `MAJ` | 0 4 7 | major | | `MI6` | 0 3 7 9 | minor 6th |
| `MIN` | 0 3 7 | minor | | `MA9` | 0 4 11 14 | major 9th (no 5th) |
| `DIM` | 0 3 6 | diminished | | `MI9` | 0 3 10 14 | minor 9th (no 5th) |
| `AUG` | 0 4 8 | augmented | | `DO9` | 0 4 10 14 | dominant 9th (no 5th) |
| `SU2` | 0 2 7 | sus2 | | `7S4` | 0 5 7 10 | 7sus4 |
| `SU4` | 0 5 7 | sus4 | | `AD9` | 0 4 7 14 | add9 |
| `MA7` | 0 4 7 11 | major 7th | | `MD9` | 0 3 7 14 | minor add9 |
| `MI7` | 0 3 7 10 | minor 7th | | `D7#` | 0 4 10 15 | 7♯9 |
| `DOM` | 0 4 7 10 | dominant 7th | | `D7b` | 0 4 10 13 | 7♭9 |
| `HDM` | 0 3 6 10 | half-diminished (m7♭5) | | `D13` | 0 4 10 21 | 13th |
| `DM7` | 0 3 6 9 | diminished 7th | | `M11` | 0 3 10 17 | minor 11th |
| `MMA` | 0 3 7 11 | minor-major 7th | | `QRT` | 0 5 10 15 | quartal (fourths) |
| `MA6` | 0 4 7 9 | major 6th | | | | |

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

## Fill, Euclid, arp, chords and bass

Quick track filling: notes, velocity or any FX slot on every N-th step, in a Euclidean rhythm or at random. Open it in GRID: long tap on a cell (or long encoder press) → **Fill…**. With a selection, the selection is filled (all its tracks); without one, the track under the cursor for its length (a track's own length is respected). **What** to fill is taken from the column under the cursor: NOTE, VEL or an FX slot (in Detail view); you can change it in the dialog. The first row, **Type**, switches the dialog between filling (`FILL`), the [arpeggio generator](#arp) (`ARP`), the [chord generator](#chords) (`CHORDS`) and the [bass generator](#bass) (`BASS`).

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

<a id="arp"></a>

### Arp (Type: ARP)

The first row of the Fill dialog is **Type**: `FILL` (filling, see above) or `ARP`, an arpeggio generator in the spirit of the Access Virus. It does not play live: it writes the arpeggio as notes and fx onto one track (**Dest**). A rhythm pattern (factory or your own) runs over the "held" notes: the chord from the Root and Chord rows, or the notes of the selection. The step range is the same as Fill's: the selection or, without one, the track under the cursor for its length. Preview, **OK** (one undo step, toast `ARP`) and **CANCEL** work as in Fill. The header shows where it writes and where it reads: `ARP T4 1-16`, with Source SELECTION `ARP T4 1-16 < T1-3`.

| Row | Values | Default | Meaning |
|-----|--------|---------|---------|
| Type | FILL, ARP, CHORDS, BASS | FILL | Filling, arp, [chords](#chords) or [bass](#bass). Each has its own set of rows and its own settings. |
| Source | CHORD, SELECTION | CHORD | Where the notes come from: the chord from Root + Chord, or the notes of the selected tracks (see below). |
| Root | any note | the track's last entered note | CHORD: the chord's bottom note. The default is taken on the first open. CHORD only. |
| Chord | any [CHD](#chd) type and inversion | tri | CHORD: the chord, as in [CHD](#chd): lowercase types on the project's scale degrees, UPPERCASE ones fixed. CHORD only. |
| Dest | T1–T16 and the track name | the first track of the selection | The track written. It may lie inside or outside the selection. A drum track turns the row red and nothing is written. |
| Mode | UP, DOWN, UP/DN, DN/UP, PLAYED, RANDOM, CONVERGE, DIVERGE, PEDAL, CHORD | UP | Note order, see below. |
| Octaves | 1–4 | 1 | How many octaves: the notes repeat an octave up, one octave after another (above 127 dropped). |
| Pattern | factory, then your own | 16THS | The rhythm pattern. Your own (from `/presets/ARP`) follow the factory ones, alphabetically. |
| Rate | ×1–×4 | ×1 | Track steps per arp step: ×2 runs the arp in eighths on a 1/16 track. The steps in between are left empty. |
| Rotate | 0…pattern length − 1 | 0 | The pattern step to start from. |
| Gate | 5–100 % | 50 % | Length of a normal note. Short (`s`) is half, long (`l`) is 100 %. All × Rate. For patterns with exact gates (VIRUS) 50 % = as written; other values scale them. |
| Swing | 0–100 % | 0 % | NDG = Swing / 2 on the odd arp steps (100 % shifts them by half a step). |
| Vel Lo | 1–127 | 60 | Velocity of ghost notes (`o`). |
| Vel Hi | 1–127 | 120 | Velocity of accents (`X`). A normal note gets the middle between Lo and Hi. VIRUS patterns: exact velocity, Vel Hi is the level of 128 %. |
| Slide | SLD value | 16 | SLD time on slide notes (`~`). |
| Roll | 0–100 % | 0 % | Share of notes with a random RAT 2–4 (slide notes are left alone). |
| Ghost PRB | OFF, 0–99 % | OFF | PRB on ghost notes: they don't play every pass. OFF (100 %) writes no PRB. |
| Mutate | 0–100 % | 0 % | Pattern variations: hits dropped and added, accents and pitches changed. Depends on Seed. A redrawn accent drops the step's exact velocity. |
| Seed / Reseed | — | — | Variant of the random parts (RANDOM, Roll, Mutate); the rows show only when one of them is on. Reseed (or <kbd>Shift</kbd>+click) picks a new random one. |
| Capture | SAVE DEST AS PATTERN | — | Save Dest's rhythm as your own pattern, see below. |

**Modes.** The notes are sorted bottom to top (except PLAYED); Octaves adds copies of them in the octaves above.

| Mode | Order |
|------|-------|
| UP | Bottom to top. |
| DOWN | Top to bottom. |
| UP/DN | Up, then down; the end notes are not repeated. |
| DN/UP | Down, then up; the end notes are not repeated. |
| PLAYED | As the notes were taken: a CHORD chord from its bottom note; SELECTION track by track from the top, each with its step note, then the notes of its CHD. |
| RANDOM | A random note on every hit (from Seed). |
| CONVERGE | From the edges to the middle: bottom, top, second from the bottom, second from the top… |
| DIVERGE | From the middle to the edges (CONVERGE backwards). |
| PEDAL | The bottom note every other hit, the others in turn: 1-2-1-3-1-4… |
| CHORD | The whole chord on every hit: the bottom note + CHD (the Chord row; with SELECTION, tri). |

The note cycle runs on continuously: the pattern repeats over the range, and the note cycle restarts neither on a pattern repeat nor on a chord change.

**SELECTION.** The notes of all selected tracks together with their CHD form the held chord. A step with notes on at least one of the tracks replaces the whole chord, which then holds until the next such step. An OFF with no notes on that step releases the chord: the arp is silent until new notes. The source is read from step 1 of the pattern, not from the start of the range: a chord set before the selected steps already sounds on the first of them. The source is taken as it was when the dialog opened, so Dest may lie inside the selection too: the arp is written over the chord it is built from. Drum tracks in the selection are not a source.

**What is written to Dest.** On every step of the range the note and velocity are replaced, and the arp's own fx GAT, TIE, SLD, NDG, RAT, PRB, CHD, ARS and ARP are cleared; other locks (FLT, DLY…) stay. Then on every hit:

- **Note and velocity:** Vel Lo / the middle / Vel Hi by accent. A step with an exact velocity (see the pattern format): Vel Hi × vel % / 128, 1–127.
- **Length:** always GAT. A normal note is Gate %, a short one half of it, a long one 100 %; all × Rate. A step with an exact gate: gate % × Gate / 50 × Rate (Short / Long ignored).
- **Tie (`-`):** TIE on the previous note and OFF on the first rest after it. A tie that runs to the end of the range becomes a long GAT to the range's end instead of TIE (up to 800 %); otherwise the note would hang past the range.
- **Slide (`~`):** SLD (the Slide row) on the note, and the previous note's GAT is stretched into the next one so the voice glides legato. After a rest SLD is written too: the synth takes the note from the last pitch. A slide on the first note of the range is not written.
- **Swing:** NDG on the odd arp steps; a step's own nudge (× Rate) is added to it, clamped to ±50. **Ghost PRB:** PRB on ghost notes. **Roll:** RAT 2–4. **CHORD mode:** CHD.
- No free fx slot: that fx is skipped.

**Pattern format.** A pattern is a line of tokens separated by spaces (or commas), one token per step, up to 32 steps (the rest are ignored). The first character is the step type, followed without a space by note modifiers in any order:

| Token | Meaning |
|-------|---------|
| `x` | note |
| `X` | accented note (Vel Hi) |
| `o` | ghost note (Vel Lo, Ghost PRB) |
| `.` | rest |
| `-` | tie: the previous note holds |
| `s` | after a note: short (half of Gate) |
| `l` | after a note: long (100 %) |
| `~` | after a note: slide into it from the previous one |
| `r` | after a note: repeat the previous note (the cycle does not move) |
| `p` | after a note: the lowest held note, a "pedal" (the cycle does not move) |
| `^` / `v` | after a note: an octave up / down |

After the modifiers a note may carry exact values as fields `:vel:gate[:nudge]`:

| Field | Values | Meaning |
|-------|--------|---------|
| `vel` | 0–200 % | Velocity = Vel Hi × vel % / 128 (1–127); Vel Lo is not used for such a step. |
| `gate` | 0–800 % of a step | GAT, scaled by Gate / 50 (Gate 50 % = as written) and × Rate; Short / Long are ignored. |
| `nudge` | −50…50 % of a step | NDG × Rate, plus Swing / 2 on the odd arp steps, clamped to ±50. |

An empty field or 0 means none (the usual rule applies). Example: `X:128:150 x:61:33:-17`: an accent at full Vel Hi, 1.5 steps long; a quiet short note pulled 17 % early.

Example: `X x~ x . X x~ X x`: 8 steps, an accent, a slide note, a note, a rest, and again. Unknown tokens are skipped. Your own `.arp` files may use the fields too.

**Capture** saves Dest's rhythm as your own pattern: a name from the keyboard (up to 16 characters: A–Z, 0–9, `-`, `_`), file `/presets/ARP/NAME.arp`. It takes Dest's range as it was before the dialog opened (your own line, not the preview), at most 32 steps. A note becomes `x` / `X` / `o` (velocity split into three levels within the range's spread; with a spread under 6 or the track velocity, a normal note), GAT under 35 % `s`, from 90 % `l`, SLD `~`, empty steps after a TIE note `-`, everything else `.`; the same pitch as the previous note becomes `r`, the range's lowest note `p`. The first note is always a plain one (NEXT); octaves are not captured. GAT is captured as written, so capture a line written at Rate ×1: the arp's GAT at Rate ×2–×4 is multiplied by Rate and captures as long notes. After saving, the pattern is selected in Pattern right away (toast `SAVED`). No notes in the range: `NO NOTES`; no valid characters in the name: `BAD NAME`; the write failed (no card etc.): a toast with the error. `.arp` is plain text in the format above, so it can be edited on a computer (card reader). A broken file shows an error toast when selected and plays the first factory pattern. The Pattern list holds up to 32 of your own patterns.

**Factory patterns** (106), by style:

| Style | Patterns |
|-------|----------|
| BASIC | 16THS, 8THS, TRIPLET, DOTTED, QUARTERS, OFFBEAT, ACCENT 4 |
| TR (trance) | TR GATE, TR OFFBT, TR ROLL, TR PEDAL, TR UPLIFT, TR 332, TR CHUG |
| PSY (psytrance) | PSY GALOP, PSY TRIPL |
| TE (techno) | TE STAB, TE HYPNO3, TE MINIMAL, TE RUMBLE, TE ROLLER |
| HO (house) | HO OFFBT, HO ORGAN, HO PIANO, HO SHUFFLE |
| DNB | DNB ROLL, DNB STAB, DNB AMEN, DNB REESE, DNB 2STEP |
| ACID | ACID 1, ACID 2, ACID 3 |
| EL (electro) | EL FUNK, EL ROBOT |
| BR (breaks) | BR BREAK |
| SW (synthwave) | SW 8THS, SW DRIVE |
| DUB | DUB STAB, DUB ECHO |
| CHIP | CHIP OCT, CHIP RUN |
| VIRUS | VIRUS 01 … VIRUS 64 |

TRIPLET, PSY TRIPL and TE HYPNO3 are 12 steps long (over 16 steps they make a polyrhythm), the rest 16.

**VIRUS** — the 64 arpeggiator patterns of the Access Virus B / Indigo (OS 4.90), with exact velocity, gate and nudge per step. Each is 32 steps (one Virus cell = 2 steps), so at Rate ×1 on a 1/16 track it plays like the Virus at Arp Clock 1/8. For the original 1/16 tempo set the Dest track's Speed to x2 ([TRACK → Speed](#track)). The triplet and 1/64 patterns (35, 38–42, 51, 56, 64) are placed with NDG; Swing adds to it (clamped to ±50). Events that fell into the same step were merged (the louder one kept), so VIRUS 35, 37 and 40 are noticeably thinner than the originals. VIRUS sounds best at Rate ×1 (use track Speed for tempo): at Rate ×2 and up nudges over ±25 % hit the NDG limit of ±50 and long gates can reach the GAT maximum of 800 %.

- Dest is a [drum track](#drumtrack): nothing is written, the Dest row is red.
- Slides (SLD) sound only on INT tracks; on a MIDI track SLD is written but does nothing (grey).
- Like Fill's, the arp settings are remembered until power-off; your own patterns live on the card.

<a id="chords"></a>

### Chords (Type: CHORDS)

`CHORDS` writes a chord progression onto one track (**Dest**): on every hit of a rhythm, the chord's root as the step note and its type with inversion as [CHD](#chd). The roots come from the project scale and its root (PROJ → SONG), so one progression follows the key of any project: `1 5 6 4` in A minor is Am Em F Dm (degrees of the scale), `1 b7 b6 5` is a fixed "Roman" line in any scale. The range, Preview, **OK** (one undo step, toast `CHORDS`) and **CANCEL** work as in ARP; the header reads `CHORDS T4 1-16`. A drum Dest turns the row red and nothing is written.

| Row | Values | Default | Meaning |
|-----|--------|---------|---------|
| Type | FILL, ARP, CHORDS, BASS | FILL | See above. |
| Progression | factory, your own, RANDOM | POP 1564 | The progression. Your own (from `/presets/PRG`) follow the factory ones, alphabetically; the last entry, RANDOM, is generated, see below. |
| Chords | 2–8 | 4 | RANDOM: how many chords. RANDOM only. |
| Octave | C2–C5 | C4 | Octave of the roots: every root is placed within C…B of it (CLOSE may take one an octave lower). |
| Chord Type | TRIAD, 7TH, 9TH, SUS | TRIAD | The chord for tokens without a quality: `tri`, `7th`, `9th`, `su4` — diatonic, so major or minor as the scale has it. |
| Voicing | ROOT, CLOSE | CLOSE | ROOT: every chord in root position. CLOSE: inversions chosen for smooth voice leading, see below. |
| Length | AUTO, 1–64 | AUTO | Steps per unit of chord length. AUTO spreads the progression evenly over the range: range / sum of the lengths, at least 1. |
| Dest | T1–T16 and the track name | the first track of the selection | The track written; reset to that track every time the dialog opens. |
| Rhythm | factory | HOLD | How each chord is played, in ARP pattern tokens. It restarts on every chord. |
| Gate | 5–100 % | 50 % | Length of a normal hit, as in ARP. |
| Vel Lo / Vel Hi | 1–127 | 60 / 110 | Velocity of ghost / accented hits; a normal one gets the middle. |
| Mutate | 0–100 % | 0 % | Chords replaced by relatives and rhythm variations, see below. Depends on Seed. |
| Seed / Reseed | — | — | Variant of RANDOM and Mutate; shown only when one of them is on. Reseed (or <kbd>Shift</kbd>+click) picks a new one. |
| Capture | SAVE DEST AS PROG | — | Save Dest's chords as your own progression, see below. |

**Progression format.** One token per chord, separated by spaces: `[b|#|n]degree[quality][*N]`.

| Part | Meaning |
|------|---------|
| `1`…`7` | Degree of the project scale from its root. With the chromatic scale — major degrees. Scales shorter than 7 notes wrap around. |
| `b` / `#` / `n` before the degree | A Roman numeral: the degree of the major scale from the scale root, a semitone down / up / unchanged — the same in any scale. `b7` is always 10 semitones above the root, `b6` 8, `b3` 3; `n6` in minor is the major sixth. |
| quality | A [CHD](#chd) type name, case-sensitive: `MI7`, `DOM`, `MA7`, `tri`, `9th`… Without one: the Chord Type row's chord; a Roman numeral without one: `MAJ`. |
| `*N` | Length in units of Length (default 1, up to 128). |
| `@steps` | As the first token: lengths count steps, the Length row is ignored (Capture writes files like this). |

Examples: `1 5 6 4` · `2MI7 5DOM 1MA7*2` · `1 b7 b6 5` · `@steps 1MI7*8 b6MA7*8`. Up to 16 chords; unknown tokens are skipped. The chords are placed one after another from the start of the range; the progression does not loop: the last chord holds to the end of the range, chords that don't fit are dropped. With AUTO the leftover steps of the division go to the last chord.

**Voicing CLOSE.** The first chord is in root position. Every next one takes, among its inversions at its root or an octave lower (not more than 7 semitones below the Octave's C), the one whose notes move least from the previous chord's notes (the sum of the distances, note by note from the bottom); ties keep the root's octave and the lower inversion. The result is written as the CHD inversion digit; when the octave-lower variant wins, the step note itself is an octave lower.

**What is written to Dest.** The note, velocity and the generator fx (GAT, TIE, SLD, NDG, RAT, PRB, CHD, ARS, ARP) of the range are replaced, as in ARP; other locks stay. Then, with the rhythm restarting on every chord:

- **Hit** (`x` `X` `o`): the root with CHD, velocity Vel Lo / middle / Vel Hi (`:vel` — exact, as in ARP), GAT from Gate (`s` half, `l` 100 %, `:gate` exact). `^` / `v` move the whole chord an octave up / down; other pitch modifiers (`r`, `p`) and slides (`~`) are ignored, nudge too.
- **Tie** (`-`): TIE on the previous hit of the same chord and OFF on the first rest after it; a `-` at the start of a chord acts as a rest. A tie running to the end of the range becomes a long GAT (up to 800 %). A new chord's hit simply replaces a running tie.
- No free fx slot: the fx is skipped (CHD goes first).

**RANDOM.** Starts on I and moves by harmonic function: tonic → mostly subdominant or dominant, subdominant → mostly dominant, dominant → mostly tonic; never the same degree twice in a row. Major-like scales (Major, Lydian, Mixolydian, PentMaj, Chromatic) use I ii iii IV V vi, the others i III iv v VI VII; the last chord is V or IV in major (v, iv or VII in minor), so the loop leads back to I. The chords are diatonic (the Chord Type row). The same Seed gives the same progression.

**Mutate.** Each chord, with the Mutate chance, is replaced by its relative (major: I↔vi, ii↔IV, iii↔V; minor: i↔III, iv↔VI, v↔VII) and, if the token has no quality, gets a random Chord Type (triad, 7th, 9th or sus). Roman tokens (`b` `#` `n`) are left as they are. The rhythm mutates as in ARP (hits dropped and added, accents redrawn), without pitch changes. Works with factory, your own and RANDOM progressions.

**Capture** saves Dest's chords as your own progression `/presets/PRG/NAME.prg`: the keyboard asks `PROG NAME:`; name rules, toasts (`SAVED`, `NO NOTES`, `BAD NAME`, `LIST FULL`) and the list of up to 32 files as in [ARP Capture](#arp). After saving, the progression is selected in Progression. It takes Dest's range as it was before the dialog opened. A new chord starts at every step whose note or CHD differs from the current chord; steps without a note continue it. A root in the scale becomes a degree with its CHD type name, a root outside the scale a Roman numeral (`n`, else `b`, else `#`) with its type; a note without CHD becomes `MAJ` or `MIN` by the scale's third (`MAJ` outside the scale). Lengths are written in steps (`@steps`), up to 16 chords; inversions are not saved — Voicing chooses them. `.prg` is plain text in the format above.

**Factory progressions** (55):

| Name | Progression | | Name | Progression |
|------|-------------|---|------|-------------|
| POP 1564 | `1 5 6 4` | | DEEP 21 | `2MI7 1MA7` |
| POP 6415 | `6 4 1 5` | | HOUSE 4 | `1MI7 b6MA7 b7DOM 1MI7` |
| POP 1645 | `1 6 4 5` | | HOUSE 2 | `1MI9 b7MA7 b6MA7 b7MA7` |
| POP 1465 | `1 4 6 5` | | LIQUID | `1MI9 b6MA7 b3MA7 b7DOM` |
| POP 4156 | `4 1 5 6` | | TRNC EPIC | `1 b6 b7 b3` |
| POP 1364 | `1 3 6 4` | | TRNC 6471 | `6 4 b7 1` |
| OLDIES | `1 6 2 5` | | TRNC 1671 | `1 b6 b7 1` |
| AXIS | `1 5 6 4 1 5 4 4` | | SW 1671 | `1MIN b6 b7 1MIN` |
| CANON | `1 5 6 3 4 1 4 5` | | SW 6455 | `6 4 5 5` |
| ROYAL RD | `4MA7 5DOM 3MI7 6MIN` | | DUB 17 | `1 b7` |
| CITY POP | `4MA7 3DOM 6MI7 5MI7 1DOM` | | DUB 1b6 | `1MI7 b6MA7` |
| JAZZ 251 | `2MI7 5DOM 1MA7*2` | | FUNK 14 | `1DO9 4DO9` |
| JAZZ 1625 | `1MA7 6MI7 2MI7 5DOM` | | BLUES 12 | `1DOM*4 4DOM*2 1DOM*2 5DOM 4DOM 1DOM 5DOM` |
| JAZZ 3625 | `3MI7 6MI7 2MI7 5DOM` | | BLUES QC | `1DOM 4DOM 1DOM*2 4DOM*2 1DOM*2 5DOM 4DOM 1DOM 5DOM` |
| JAZZ TURN | `1MA7 6DOM 2MI7 5DOM` | | MIN BLUES | `1MI7*4 4MI7*2 1MI7*2 b6DOM 5DOM 1MI7*2` |
| JAZZ BACK | `4MA7 b7DOM 1MA7*2` | | ROCK 1741 | `1 b7 4 1` |
| MIN 251 | `2HDM 5D7b 1MI7*2` | | ROCK 1454 | `1 4 5 4` |
| NEO SOUL | `4MA7 3MI7 2MI7 1MA7` | | MIXO 1574 | `1MAJ 5MAJ b7MAJ 4MAJ` |
| SOUL 25 | `2MI9 5DO9` | | DORIAN 14 | `1MIN 4MAJ` |
| LOFI | `4MA7 3MI7 6MI7 1MA7` | | DORIAN 12 | `1MI7 2MI7` |
| GOSPEL | `1 4 1 5 6 4 1 5` | | PHRYG 12 | `1MIN b2MAJ` |
| GOSPEL 2 | `1MAJ 4MAJ #4DIM 5MAJ` | | LYDIAN 12 | `1MAJ 2MAJ` |
| MIN 1636 | `1 b6 b3 b7` | | CINEMA | `1MAJ b6MAJ b3MAJ 4MAJ` |
| MIN 1767 | `1 b7 b6 b7` | | HERO | `1MAJ 3MAJ 4MAJ 4MIN` |
| ANDALUS | `1MIN b7 b6 5MAJ` | | AMBIENT | `1ad9 4ad9 6ad9 5ad9` |
| MIN 1451 | `1 4 5 1` | | SUS DRIFT | `1SU2 4SU2 5SU4 1SU2` |
| DEEP 2 | `1MI7 4MI7` | | QUARTAL | `1QRT 2QRT 4QRT 5QRT` |
| DEEP 1MI9 | `1MI9 b7MA7` | | | |

The MIN, DEEP, TRNC and SW progressions are meant for a minor project scale, the POP / JAZZ ones for major; with fixed qualities (`MI7`, `DOM`, `MIN`…) a progression sounds the same in any scale.

**Rhythms** (16 steps each): HOLD (one hit held for the whole chord), HALF, QUARTER, 8THS, STAB, OFFBEAT, CHARLSTN, HOUSE PNO, DUB SKNK, REGGAE, TRNC GATE, SYNCOPA, PUSH, FUNK, POP 8, BALLAD, UPBEAT16, GALLOP, REV GALLOP, THREE, DOUBLE, ANTICIP, ROLL UP, SWELL, LONG SHRT, SHRT LONG, LATE HOLD, STUTTER, PNO COMP, GHOST 3, CHOP 3, TRESILLO. The rhythm counts from the start of each chord and repeats every 16 steps; a chord shorter than 16 steps plays only its beginning. A rhythm of a single hit followed only by ties (HOLD) is not repeated: it holds for the whole chord, however long.

- The settings are remembered until power-off, like Fill's; your own progressions live on the card.

<a id="bass"></a>

### Bass (Type: BASS)

`BASS` writes a bass line in harmony with chords onto Dest. The pattern is an ARP rhythm in which a note is not a pitch but a role against the chord of its step: root, third, fifth, seventh, a lead-in to the next chord. The chords come from a fixed root, from the selected tracks or from the CHORDS progression. The range, Preview, **OK** (one undo step, toast `BASS`) and **CANCEL** work as in ARP; the header reads `BASS T4 1-16`, with Source SELECTION `BASS T4 1-16 < T1-3`.

| Row | Values | Default | Meaning |
|-----|--------|---------|---------|
| Type | FILL, ARP, CHORDS, BASS | FILL | See above. |
| Source | ROOT, SELECTION, PROGRESSION | ROOT | Where the chords come from, see below. |
| Root | any note | the track's last entered note, in octave 2 | ROOT: the tonic; only its pitch class counts, the octave comes from Octave. Taken on the first open. ROOT only. |
| Octave | C1–C3 | C2 | Octave of the bass: every chord root is placed within C…B of it. |
| Dest | T1–T16 and the track name | the first track of the selection | The track written; reset to that track every time the dialog opens. |
| Pattern | factory, then your own | HOUSE OFF | The bass pattern. Your own (from `/presets/BAS`) follow the factory ones, alphabetically. |
| Rate | ×1–×4 | ×1 | Track steps per pattern step, as in ARP. |
| Rotate | 0…pattern length − 1 | 0 | The pattern step to start from. |
| Gate | 5–100 % | 50 % | As in ARP. |
| Swing | 0–100 % | 0 % | As in ARP. |
| Vel Lo / Vel Hi | 1–127 | 70 / 120 | As in ARP. |
| Slide | SLD value | 16 | SLD time on slide notes (`~`). |
| Mutate | 0–100 % | 0 % | Pattern variations as in ARP; a redrawn pitch becomes the root, fifth, third or the root an octave up. |
| Seed / Reseed | — | — | Variant of Mutate; shown only when it is on. With Source PROGRESSION the rows edit the CHORDS seed instead (the variant of its RANDOM progression / Mutate) and show when one of those is on. |
| Capture | SAVE DEST AS PATTERN | — | Save Dest's line as your own bass pattern, see below. |

**Sources.**

| Source | The chord of a step |
|--------|---------------------|
| ROOT | One chord for the whole range: the scale's triad (`tri`) on Root. |
| SELECTION | As in [ARP SELECTION](#arp): the lowest note of the selected tracks with its CHD (without one — `tri`) is the chord until the next step with notes; an OFF with no notes is silence. Read from step 1 of the pattern as it was when the dialog opened, so Dest may lie inside the selection. Drum tracks are not a source. |
| PROGRESSION | The [CHORDS](#chords) progression with its current settings (Progression, Chords, Octave, Chord Type, Voicing, Length, Mutate, Seed), laid over the same range. Set it up on Type CHORDS, write the chords to one track, then switch to BASS and write the bass to another. |

The chord's root is moved into the Octave; the bass always uses the chord in root position (CHD inversions are ignored).

**Pattern format.** The ARP tokens (`x X o . - s l ~` and `:vel:gate:nudge`, see the [ARP format](#arp)); after a note, instead of the ARP pitch modifiers, a role:

| Token | Note |
|-------|------|
| `1` | the chord's root (also with no role) |
| `3` `5` `7` | the 2nd, 3rd, 4th note of the chord — the third, fifth, seventh of `tri`, `MI7`, `DOM`…; a chord without that note: the 3rd / 5th / 7th scale degree above the root |
| `a` | approach: a semitone below the root of the next chord |
| `w` | walk: one scale step from the previous note toward the next chord's root (never past it; already there — the root) |
| `r` | the previous note |
| `^` / `v` | an octave up / down; combines with a role: `5^`, `av` |

The next chord: with PROGRESSION the next one of the progression, with SELECTION the next chord change (searched past the range, up to the end of the pattern); with ROOT, or no change ahead, the range's first chord. `r` and `w` start from the previous note without its `^` / `v`. If a token has several roles, the last one wins. Example: `x . x5 . x^ . x7 . x . x3 . x5 . xa .`.

**What is written to Dest** — as in ARP: note, velocity, GAT, TIE / OFF, SLD with the previous note stretched, NDG for Swing and nudge; no RAT, PRB or CHD. Same cleared fx.

**Capture** saves Dest's line (up to 32 steps) as your own pattern `/presets/BAS/NAME.bas` (the keyboard asks `BASS NAME:`; toasts as in [ARP Capture](#arp)); the rhythm is captured as in ARP. A note's role is found against the chord of its step from the current Source (SELECTION without a selection or PROGRESSION without chords falls back to ROOT): root / 3 / 5 / 7 within an octave up or down (with `^` / `v`), else `a` if it is a semitone below the next root, else `r` if it repeats the previous note (± an octave), else the nearest of root, third and fifth. `w` is never captured. `.bas` is plain text; the Pattern list holds up to 32 of your own.

**Factory patterns** (48):

| Name | Pattern | | Name | Pattern |
|------|---------|---|------|---------|
| HOUSE OFF | `. . x . . . x . . . x . . . x .` | | DEEP HOUSE | `x . . x . . x . . . x . . x5 . .` |
| DISCO OCT | `x . x^ . x . x^ . x . x^ . x . x^ .` | | GARAGE | `. . x . . x . x . . x . . x . x` |
| TECHNO RL | `. x x x . x x x . x x x . x x x` | | MINIMAL | `x . . . . . x . . . . . x . . .` |
| TECHNO 16 | `X x x x X x x x X x x x X x x x` | | ROLLING | `x x^ x x x x^ x x x x^ x x x x^ x x` |
| ACID 1 | `X x~ x5 . X x~ X3 x x . X~ x . x5 X~ x` | | GALLOP | `x . x x x . x x x . x x x . x x` |
| ACID 2 | `x^ x X~ x5 . x x^~ x X . x~ x7 x X x^ .` | | PSY TRIP | `. x x . x x . x x . x x . x x .` |
| ACID 3 | `Xr x~ x . X~ xv x . Xr x x~ x . X x~ x5` | | EBM | `X x x5 x X x x5 x X x x5 x X x x5 x` |
| DNB | `Xl - . . . . x . . . Xl - . . x .` | | ITALO | `x . x^ x x . x^ x x . x^ x x . x^ x` |
| REESE | `xl - - - - - - - x5l - - - - - - -` | | NU DISCO | `x . x^ . . x . x^ x . x^ . . x x^ .` |
| DUB | `Xl - - . . . x5 . Xl - - . . . x .` | | ACID 4 | `X~ x x^ x~ . x X5 x~ x x^ . X~ xv x x~ x5` |
| REGGAETN | `X . . x . . x . X . . x . . x .` | | BREAKBEAT | `X . . x . . . x . x . . . . x .` |
| SYNTHW 8 | `x . x . x . x . x . x . x . x xa` | | SYNCOPA | `x . x . . x . x . . x . x . . x` |
| ELECTRO | `X . . x . . X . . x . x X . x .` | | SUB HOLD | `xl - - - - - - - - - - - - - - -` |
| FUNK | `X . x^ . . x . x X . . x^ . x7 x .` | | OCT HOLD | `xl - - - x^l - - - xl - - - x^l - - -` |
| WALKING | `x . . . x3 . . . x5 . . . xa . . .` | | DUBSTEP | `Xl - - - - - . . . . x . xv . . .` |
| ROCK 8 | `x x x x x x x x x x x x x x x x` | | TRAP 808 | `Xl - - - - - x . . . Xl - - x . .` |
| TRNC OFF | `. . X . . . X . . . X . . . X .` | | ONE DROP | `Xl - . . . . x5 x . . . . x . . .` |
| PSY | `. X x x . X x x . X x x . X x x` | | MOTOWN | `x . . x x5 . x . x . . x x7 . xa .` |
| UKG | `X . . x . . . x . . X . . . x .` | | SLAP | `X . x^ x . x X . . x^ . X x . x^ .` |
| BOOGIE | `x . x3 . x5 . x7 . x^ . x7 . x5 . x3 .` | | WALK STEP | `x . . . xw . . . xw . . . xa . . .` |
| OCT JUMP | `x x^ x x^ x x^ x x^ x x^ x x^ x x^ x x^` | | ARPEGGIO | `x x3 x5 x7 x^ x7 x5 x3 x x3 x5 x7 x^ x7 x5 x3` |
| ROOT 5TH | `x . . . x5 . . . x . . . x5 . . .` | | COUNTRY | `x . . . x5v . . . x . . . x5v . . .` |
| APPROACH | `xl - - - - - - - - - - - - - xa .` | | BOSSA | `x . . x5 x5 . . x x . . x5 x5 . . x` |
| TECHNO OFF | `. . x . . x . . . . x . . x . .` | | TUMBAO | `. . . x . . x . . . . x5 . . x .` |

- Dest is a [drum track](#drumtrack): nothing is written, the Dest row is red. Slides sound only on INT tracks.
- The settings are remembered until power-off; your own patterns live on the card.

<a id="track"></a>

## TRACK: track settings

The track is the one under the cursor in GRID. To switch: tap the arrows in the header or <kbd>Shift</kbd>+turn outside edit mode. The mixer for all tracks is the [MIX](#mixer) tab.

Parameters are on eight pages: **MAIN**, **INS**, **FOL**, **NOTE**, **GEN**, **EVO**, **VAR**, **MIDI**. To pick a page, tap its tab below the header; with the encoder, a turn cycles through the rows and the page tabs (above the first row, framed); there, <kbd>A</kbd> held + turn goes to the next / previous page (the cursor stays on the page tabs); a click does nothing. From anywhere on the screen: <kbd>B</kbd>+<kbd>Shift</kbd>+turn.

| Parameter | Values | Default |
|-----------|--------|---------|
| **MAIN** | | |
| Name | up to 8 characters: A–Z 0–9 - \_ space | TRK1…TRK16 |
| Out | MIDI / INT | INT |
| Instr | 1–16 (instrument number and name), INT only | 1 |
| Volume | 0–127, INT only | 100 |
| Pan | L64…C…R63, INT only | C |
| Mute / Solo | ON / OFF | OFF |
| **INS** | | |
| Drive | OFF, 1–127, INT only | OFF |
| Drive type | TANH, CLIP, FOLD, RECT, ASYM, INT only; gray while Drive = OFF | TANH |
| Bit crush | OFF, 1–127 (the number in brackets is how many bits remain), INT only | OFF |
| Downsample | OFF, 1–127, INT only | OFF |
| Trance | OFF, 1–127: [trance gate](#trancegate) depth, INT only | OFF |
| Pattern | a ready-made gate pattern (8THS, OFFBEAT, 16THS, QUARTERS, OFF 4THS, X.XX, XX.X, 3-3-2, 3-3-2 FILL, HALF BAR, PAIRS, SKIP, SYNCOPE, STUTTER, BUILD, GALLOP) or USER | 8THS |
| Steps | 16 cells: a filled cell is an open step | x.x.x.x.x.x.x.x. |
| Rate | 1/32, 1/16, 1/8 — gate step length | 1/16 |
| Length | 1–100% of the gate step during which it is open | 100% |
| Attack / Release | 0.5–50 ms: volume rise / fall at the step edges | 1.0 / 4.0 ms |
| Mod send | 0–127: the track's send to the [MOD bus](#modbus), INT only; gray while PROJ → MOD → Mod type = OFF | 0 |
| **FOL** | | |
| Source | OFF, 1–16 (track number and name, may be this track): whose level the [follower](#follower) listens to, INT only | OFF |
| Target | a target as for the LFO of the track's instrument (CUTOFF, VOL, PAN, DRIVE, …, L1–L4 RATE / DEPTH; no RTRG) | CUTOFF |
| Amount | −64…+63: + opens / raises, − lowers | +0 |
| Attack / Release | 0 ms – 10 s: how fast the envelope follows a louder and a quieter source | 0 ms / 160 ms |
| **NOTE** | | |
| Def vel | 1–127 | 100 |
| Def gate | 1–800% | 50% |
| Pat len | OFF or 1…length−1 (in the playing pattern) | OFF |
| Speed | 1/4, 1/2, x1, x2, x4 (in the playing pattern) | x1 |
| Humanize | OFF, 1–100: each note of the track is randomly shifted in time (up to ±10% of a step at 100) and in velocity (up to ±20) — a "live" feel | OFF |
| **GEN** | see [GEN: shift-register generator](#gen) | |
| **EVO** | see [EVO: evolve](#evolve) | |
| **VAR** | see [VAR: cellular automaton and Markov chain](#var) | |
| **MIDI** | | |
| Channel | 1–16 | track number |
| CC A / CC B | CC number 0–127 | 74 / 71 |
| Program | --- or 0–127 | --- (don't send) |

- **Name:** click to edit (or hold <kbd>A</kbd> and turn). A turn changes the character; <kbd>Shift</kbd>+turn or tapping a character moves the cursor.
- **Pan:** where the track sits between the left and right channels; `C` is the centre, `L64` hard left, `R63` hard right. The instruments are mono, the track places them in the stereo field. Not sent to MIDI (no CC10). The step fx [PAN](#fx) locks it, the LFO target PAN moves it (autopan). <kbd>Shift</kbd>+turn — ×10, as on the other rows.
- <a id="insert"></a>**INS** — the track insert (INT only): a mono chain on the sum of all the track's notes, after the voice filters. The chain: notes (sound → filter) → **Drive** → **Bit crush** → **Downsample** → [**Trance gate**](#trancegate) → Volume → delay / reverb sends → Pan → mix (or the sidechain bus). **Drive** — overdrive: the higher it is, the denser the sound and the louder the quiet parts; Volume comes after it and does not change the amount of drive. **Drive type** — the drive curve: **TANH** — soft warm saturation; **CLIP** — hard clipping, a sharp "digital" edge; **FOLD** — wavefolder: the signal above the threshold folds back (several times at high Drive), a bright "west coast" timbre; **RECT** — rectifier: an octave up, fuzz, narrow dips remain at high Drive; **ASYM** — asymmetric tanh, even harmonics, a "tube" tint. The types are roughly level-matched. CLIP, FOLD and RECT are anti-aliased (ADAA); RECT and ASYM have the DC offset removed. Drive = OFF turns the drive off with any type. **Bit crush** — bit depth from 16 bits (1) down to 2 bits (127). **Downsample** — each sample is held for up to ~64 samples at 127, the sound becomes "digital" and ringing. The drive works on the sum, so chord notes intermodulate. Locks — fx `DRV`, `DTY`, `BIT`, `SRR`; instrument LFO targets — DRIVE, BIT, SRR (the drive type has no LFO target) (see [fx](#trackfx)). Auditioning an instrument (INST, preset browser, GRID) goes through the current track's insert if that track plays this instrument. An instrument preset stores a **recommended insert** (Drive, Drive type, Bit crush, Downsample) and applies it to the current track on load — see [presets](#presets).
- <a id="trancegate"></a>**Trance gate** — the last link of the insert: after Downsample, before Volume, so the delay and reverb receive the already "chopped" sound (chopped echo tails). The gate rhythmically closes the track following the 16-step **Steps** pattern: an open step sounds, a closed one is attenuated by **Trance** (127 — silence, 64 — half as loud). The steps follow the song clock from PLAY, each **Rate** long (1/32, 1/16, 1/8 — at 1/8 the pattern lasts two bars), independent of the pattern's resolution and Speed and of swing (always straight). The clock is continuous: changing the pattern or the song slot does not reset it, and a pattern of odd length (for example, 12 steps) "drifts" against the gate, as in a DAW. **Length** — how much of the step the gate stays open: at 100% neighbouring open steps merge, below that each step is staccato. **Attack / Release** — smoothness of the edges (at least 0.5 ms, no clicks). **Pattern** cycles through ready-made patterns and writes them into Steps; after you edit Steps the row shows USER. **Steps:** click — edit; turn — cursor (a bar under the cell), <kbd>Shift</kbd>+turn — rotate the whole pattern, hold <kbd>A</kbd> and turn — toggle the step under the cursor and move to the next one, tap a cell — toggle it, track buttons 1–8 — toggle step 1–8 or 9–16 (the half the cursor is in); click or <kbd>B</kbd> — done, <kbd>Shift</kbd>+click — cancel (the pattern as it was). During playback the current gate step is highlighted. The gate works only during playback (and pause — then it freezes in place); when stopped it is off and notes are heard in full. Trance = OFF — the gate is off, the other rows are gray. Depth lock — fx [GTE](#fx) (for example, `GTE OFF` in a breakdown opens up a pad); the gate has no LFO target. Auditioning through the track insert (INST, GRID) passes through the gate only while playback is running.
- <a id="follower"></a>**FOL — envelope follower:** the volume of one track moves a parameter of this one. The follower is set up on the track *being* modulated: **Source** — the source track, **Target** — what to move, **Amount** — by how much (in the same units as the LFO depth: ±64 is the target's full range; it adds to the LFO and locks). The source level is taken after its insert (Drive … Trance gate) but before its Volume, in decibels: from −48 dB (0) to 0 dB (full), so a quiet hat and a kick alike give the full swing. The envelope is smoothed by **Attack** (0 — instant) and **Release** (≈ 63% of the way in the given time); latency is 1 ms. The page header shows a bar with the current envelope. Examples: a bass with **Source** = KICK, **Target** CUTOFF, **Amount** −40, Release ~150 ms — the bass "hides" under the kick through the filter; the same with **Target** VOL — sidechain "pumping" of a single track (the volume changes smoothly, the delay / reverb sends duck with it); a pad with **Source** = HATS, CUTOFF +30 — the hats open the pad; a track following itself (Source = this track), CUTOFF +40, Attack 0, Release ~100 ms — auto-wah; VOL on itself — no feedback (the level is taken before Volume). A source with Volume 0 still works — a "ghost" kick. Targets DRIVE, BIT, SRR, PAN, DLY, RVB act on the whole track, the rest on each note (on a KIT — on the lanes that have that target). Targets the instrument lacks do nothing. The difference from the compressor sidechain (PROJ): that one ducks the whole mix and only in volume; the follower is one track, any parameter, and every track has its own (16 of them). A MIDI source track or Source = OFF — the follower is off; auditioning (INST, GRID) is not modulated. The follower has no step locks.
- **Program** is sent immediately when changed and at every start. The value is "raw": 0 = the first patch.
- **Out:** MIDI — the track plays to MIDI OUT; INT — to the built-in synthesizer (see [Built-in sound](#sound)). You can switch during playback: sounding notes are released, and the following steps go to the new output.
- **Instr / Volume / Pan** and the **INS** and **FOL** pages work only on INT; on MIDI they are gray. On INT, Channel, CC A, CC B and Program are gray. Def vel and Def gate apply to both outputs.
- **Solo:** if solo is on for at least one track, only the soloed tracks play. Mute takes priority over solo.
- **Pat len** and **Speed** are stored per pattern for each track: saved in the project, copied with the pattern, undone with Undo.
- **Speed:** x2 / x4 — the track plays 2 / 4 of its own steps per pattern step (at 1/16 resolution x2 = 1/32); its length stays the same, it just loops faster. 1/2 / 1/4 — one track step per 2 / 4 pattern steps; the track carries on across the end of the pattern pass (a 16-step track at 1/2 in a 16-step pattern plays all its steps over two passes) and restarts from step 1 when a pattern starts or changes. Works together with Pat len (polymeter). GAT, RAT, NDG, TIE and ARS count in the track's own step. Swing and groove move the pattern step; the sub-steps of a fast track are spread evenly inside the (swung) pattern step.

> Speed limits: very dense combinations (many x4 tracks with RAT) can drop notes (the event queue holds 1024). A 1/2 track in a pattern of even length never swings (it always lands on even pattern steps). Conditions (CND) count pattern passes, not track loops. On INT tracks the step length is capped at ~127 ticks, so slow tracks at a coarse resolution have a limited step length and long NDG. Muting a slow track ends its notes at its next own step. Changing the pattern length during playback shifts the phase of slow tracks.

<a id="gen"></a>
### GEN: shift-register generator

A live melody generator on a track: a looping shift register 2–16 bits long. On every clock the register shifts, the bit that falls out comes back in at the start, and with probability **Flip** it is inverted on the way. Flip `LOCK` (0) locks the loop: it repeats every Length clocks; `x2 INV` (100) locks it too, but twice as long (the second half is the first one inverted); in between the loop slowly mutates (fastest around 50%). The register value picks a note of the project scale (PROJ → Scale) in a range.

The **BYTE PITCH** and **BYTE GATE** modes read a bytebeat formula instead of the register — one of the 45 [LOFI](#lofi) formulas: each generator clock listens to its own stretch of the formula, which gives a scale degree and a hit loudness (see [below](#genbyte)).

The generator **plays live and leaves the pattern alone**, unlike Fill, which writes notes once. The track stays an ordinary one: MIDI or INT, its own instrument, insert, mute / solo, every step fx works. To write the loop into the pattern, use **Capture gen** in the GRID menu.

| Parameter | Values | Default |
|-----------|--------|---------|
| Gen | OFF, PITCH, GATE, BYTE PITCH, BYTE GATE (on a drum track `DRUM`: the generator does not act) | OFF |
| Length | 2–16 bits, PITCH / GATE only | 8 |
| Flip | LOCK (0), 1–99%, x2 INV (100), PITCH / GATE only | 10% |
| Low | the lowest note of the range (raised to the nearest scale note) | C-3 |
| Range | 1–4 OCT | 2 OCT |
| Density | 0–100%, GATE and BYTE GATE only | 50% |
| Seed | 0–65535; click — a new random seed; PITCH / GATE only | track number |
| Register | the register bits as cells (the newest bit on the left), display only; PITCH / GATE only | — |
| Formula | a bytebeat formula, the same 45 as in [LOFI](#lofi) (the turn wraps around); BYTE only | 42MELODY |
| A / B / C | 0–127: the formula's `a`, `b`, `c`; grey when the formula does not read it; BYTE only | 42 / 10 / 0 |
| Zoom | 0–12: the stretch of the formula per clock — 2^Zoom values of `t`; BYTE only | 10 |

- **PITCH:** steps **with a note** get the generator's note instead of their pitch; velocity, gate and every fx of the step stay. CHD builds the chord on the new note, NRN scatters it, ARS arpeggiates it, TIE joins equal neighbouring notes. The register shifts on every step with a note, so Length is the number of notes in the loop: 4 notes in the pattern with Length 8 give an 8-note melody over two passes. Editing the pattern's rhythm shifts the melody's phase.
- **GATE:** the generator sets the rhythm too. The register shifts on every step of the track (Speed counts); a step plays the generator's note when the register gives a hit (Density: higher — more hits, the old ones stay; 0% — silence, 100% — every step). The pattern's notes are ignored except **OFF**, which ends notes as usual. The step's fx act on the hit: CND / PRB decide whether it sounds, FLT / GAT / CHD are its locks. The velocity is the step's (`...` — Def vel).
- <a id="genbyte"></a>**BYTE PITCH / BYTE GATE:** the clock runs as in PITCH (steps with a note) / GATE (every track step), and everything above about fx, OFF and velocity applies the same way. Clock number `n` reads a stretch of 2^Zoom values of `t` starting at `n × 2^Zoom`; at Zoom 10 a step is 1024 values, exactly what LOFI with Sync runs per 1/16 step, so the rhythmic formulas (35–44) line up with the steps. 8 samples are taken from the stretch: their hash picks the scale degree in the Low / Range range, and their loudness (the spread around their mean) decides the hit in BYTE GATE: a step hits when it is loud enough for Density — as Density grows, the loudest steps come in first; 100% — every step. Formula silence (including a flat 128) is the quietest step. A smaller Zoom — short stretches, the melody travels through the formula faster; a larger one — each step covers more of the formula. Turning Formula loads its default A, B, C, as in LOFI. Length, Flip, Seed and Register are unused in BYTE (grey): the melody comes from the formula, A / B / C and Zoom only, and repeats from every Play from stop.
- A step failing CND / PRB, a muted track and steps skipped after a stall shift the register too: after unmute the loop is in the same phase.
- The register resets only on **Play from stop** (and <kbd>Shift</kbd>+<kbd>Play</kbd>). Pattern changes, song slots, pause — the loop goes on. The generator acts in every pattern.
- The header shows on the right the note the register gives now (in GATE also a hit dot). The cells follow the heard step; a flip of the new bit blinks yellow.
- Live edits (every row, Flip from a track button, a new seed) apply from the next unheard step.
- **Live Flip:** hold a track button and click the encoder — the turn switches volume → pan → Flip (see [Track buttons](#trackkeys)); `FLP` on a step is a per-step "lock" (see [fx](#fx)).
- In GRID a track with a generator has a yellow `G` by its name; in Detail its note column is dim: the generator sets the pitch.
- **Render and Resample** sound like "Play from stop": the register from the start, flips from the seed, so two renders give the same bytes. To keep the live loop, use Capture gen.
- The generator is saved with the project (TGEN chunk); its rows have no undo (like Volume). Older files open with the generators OFF; older firmware skips TGEN and reads FLP as an empty slot. The TGEN record grew (Formula, A, B, C, Zoom): older files lack these fields and get the defaults.

<a id="evolve"></a>
### EVO: evolve

The track's pattern slowly drifts by itself while it plays: notes move along the scale, hits drop out and quiet ghost notes appear, retriggers and velocity change, steps swap or the track turns. Like GEN, evolve **plays live and leaves the pattern alone**: GRID shows the notes as written. To keep a variant you like, use **Capture evo** in the GRID menu.

| Parameter | Values | Default |
|-----------|--------|---------|
| Evolve | OFF, 1–100%: how strong each change step is | OFF |
| Every | 1–16 PASS: a change step every that many pattern passes | 4 PASS |
| Home | 0–100%: the chance that each change steps back towards the pattern at every change step | 25% |
| Note | ON / OFF: notes move ±1–2 scale degrees, now and then an octave | ON |
| Rhythm | ON / OFF: hits drop out; an empty step next to a note plays that note quietly (ghost) | ON |
| Rtrg+Vol | ON / OFF: retriggers x2 / x3 (RAT) and velocity ±8…24 | ON |
| Shift | ON / OFF: the track turns by a step, or two neighbouring steps swap | OFF |

- **What a pass is:** one pass of the pattern (or one song slot). The count goes on across pattern changes and song slots: evolve runs over the song. The first pass after Play always sounds as written; with Every 1 the second one already changes. All tracks change at the same pass end.
- **What changes:** the drift builds up from change to change (each one starts from the previous). Notes stay in the project scale (PROJ → Scale; Chromatic moves by semitones). A ghost note is the neighbour's note at 40% of Def vel, without its fx. A drop keeps the step's fx. A retrigger is added as `RAT` in a free fx slot unless the step already has one. Velocity is clamped to 1–127.
- **Home** pulls back: each change moves one unit back (a degree, 8 of velocity, a drop / ghost / retrigger / swap removed, the turn one step back) with this chance. Evolve OFF with Home on lets the track return to the pattern smoothly; Home 0 keeps the drift as it is.
- **Polymeter and Speed:** the changes are per step of the track's own length (Pat len): a short track plays the same variant on each of its loops within a pass. A slow track (1/2, 1/4) may change in the middle of its own loop; x2 / x4 tracks use one variant for the whole pass.
- **Track types:** a drum track — no Note (its steps hold velocity); Rhythm drops the whole step, a ghost copies the neighbour's lanes quietly. A [GEN](#gen) track — no Note (the generator sets the pitch), evolve acts before the generator; on GATE also no Rhythm (the generator makes the rhythm). The grey rows show this. MIDI tracks drift like INT ones.
- **Step fx:** CHD builds the chord on the moved root; a ghost plays a single note. A moved note after TIE starts the note again (TIE only goes on at the same pitch); a dropped step under a TIE lets the note ring on. ARS arpeggiates from the moved root; a ghost note stops a running arp like a real note. CND, PRB, Humanize and groove apply after evolve.
- **Reset and randomness:** Stop (and <kbd>Shift</kbd>+<kbd>Play</kbd>) clears the drift; pause keeps it. The changes come from the project **Seed** (PROJ → SONG), mixed with a random number at every Play like the [follow actions](#follow): each start drifts its own way. **Export and Resample** use the Seed alone: the same file every time.
- The header shows `MUT` (change steps since Play), `PASS` (passes counted towards the next one) and `CHG` (changed steps of the track's length) of the heard drift. Live edits apply from the next unheard step; turning a kind off silences its changes at once (they come back when it is turned on).
- <a id="captureevo"></a>**Capture evo** (GRID menu, on a track with Evolve on) writes what the track plays now into the edited pattern, all steps of its length (one undo step, toast `EVO CAPTURED`). Then the track's drift starts over from the written pattern. Only while the edited pattern is the one playing (else toast `EVO: OTHER PATTERN`); with no drift, `EVO: NO CHANGES`.
- Evolve is saved with the project (TEVO chunk); its rows have no undo (like Volume). Older files open with evolve OFF; older firmware skips TEVO.

<a id="var"></a>
### VAR: cellular automaton and Markov chain

The track plays variations of its pattern: every few passes a new **generation** comes in — of a cellular automaton grown from the pattern's notes, or of a Markov chain built from the transitions between its own notes. Like GEN and EVO, VAR **plays live and leaves the pattern alone**: GRID shows the notes as written. To keep a variant you like, use **Capture var** in the GRID menu.

| Parameter | Values | Default |
|-----------|--------|---------|
| Var | OFF, CA1D, CA2D, MARKOV (CA2D on a melodic track — `CA2D (AS 1D)`) | OFF |
| Every | 1–16 PASS: a new generation every that many pattern passes | 2 PASS |
| Rule | CA1D: Wolfram rule 0–255; CA2D: LIFE, HIGHLIFE, SEEDS, DAYNIGHT, MAZE; CA only | 90 / LIFE |
| Reset | NEVER, 1–64 GEN: after that many generations, back to generation 0; CA only | 16 GEN |
| Noise | 0–100%: the chance that one random cell flips in a generation; CA only | 0% |
| Amount | 0–100%: the chance that a step takes the chain's note instead of the pattern's; MARKOV only | 50% |
| Order | 1–2: how many previous notes the chain remembers; MARKOV only | 1 |
| Rhythm | ON / OFF: rests are links of the chain too, the rhythm changes as well; MARKOV only | OFF |

Rows the mode does not use are grey (with Var OFF, all but Var). The CA1D rule number and the CA2D rule are kept apart: switching modes does not lose them.

- **Cells (CA1D, CA2D):** each step of the track (within its length, Pat len) is a cell, alive or dead. **Generation 0** is the pattern itself: steps with a note are alive. On a drum track the cells are 8 rows, one per KIT lane: the lanes a step plays are alive.
- **CA1D** — a one-dimensional automaton on a Wolfram rule: a cell of the next generation depends on itself and its two neighbouring steps, wrapping round the end of the track. For example, 90 — a Sierpinski pattern, 30 — chaos, 110 — complex moving structures; many rules die out or freeze quickly. On a drum track each lane follows the rule on its own.
- **CA2D** — a Life-like automaton on a field of 8 lanes × the track's steps, wrapped into a torus (the last lane borders the first, the end of the track the start); a cell has 8 neighbours. **LIFE** — the classic Game of Life (B3/S23); **HIGHLIFE** — close to it, with self-replicating shapes (B36/S23); **SEEDS** — every live cell dies at once, births burst out (B2/S); **DAYNIGHT** — live and dead behave alike (B3678/S34678); **MAZE** — grows into mazes (B3/S12345). Drum tracks only: a melodic track has one row, so CA2D plays as CA1D (the Var row shows `CA2D (AS 1D)`, Rule is the CA1D rule number).
- **Back to generation 0:** after **Reset** generations (NEVER — never; Reset 1 goes back right after the first generation, so the track sounds as written), when every cell has died and when a generation did not change (stasis). **Noise** flips one random cell per generation with its chance — a frozen field comes back to life.
- **How the cells sound:** a live cell with a note — the step as written; a live empty one — a copy of the nearest step with a note to its left (wrapping), whole, with its velocity and fx; on a track with no notes at all — the project scale's root in the C-4 octave at the default velocity. A dead cell is silent: the note is dropped, the step's fx stay. On a drum track a step plays the lanes whose cells are alive.
- **MARKOV:** each generation builds a transition table from the track's pattern: which notes (up to 16 distinct ones; on a drum track, lane sets) follow which, wrapping round the end of the track. **Order** 1 — the next note depends on one previous note, 2 — on two (closer to the original). Then a new sequence of the track's length is built: it starts on the pattern's first note, and each next step takes a note from the chain with chance **Amount** (the more often a transition occurs in the pattern, the likelier it is), otherwise the pattern's. Amount 0 — the pattern as is. A note from the chain plays with the velocity and fx of the first pattern step holding that note. **Rhythm** OFF — only the pitch of the steps with a note changes (on a drum track, their lanes), the rhythm stays as written; ON — rests are links of the chain too, so notes can move onto empty steps and back. Each MARKOV generation is built afresh from the pattern; Reset and Noise do not apply. If a note has no transitions in the pattern, the step plays the pattern.
- **Passes and generations:** the first pass after Play always sounds as written; with Every 2 the first generation sounds from the third pass. Generations go on across song slots with the same pattern. Another pattern (queued, or the next song slot) — generation 0 of the new pattern, the pass count from zero. Stop (and <kbd>Shift</kbd>+<kbd>Play</kbd>) resets VAR; pause keeps it.
- **Order:** pattern → VAR → [EVO](#evolve) → [GEN](#gen). Evolve drifts what VAR plays (both change the rhythm and stack); the generator acts last (PITCH replaces the pitch of the notes VAR played). CND, PRB, Humanize and groove apply after all of them. MIDI tracks work like INT ones.
- **Randomness:** as for EVO — from the project **Seed** (PROJ → SONG), mixed with a random number at every Play: each start goes its own way. **Export and Resample** use the Seed alone: the same file every time.
- The header shows `GEN` (generations since generation 0; for MARKOV, since Play) and `PASS` (passes counted towards the next generation); on the right, a mini grid of the heard state over the track's length: CA live cells (on a drum track 8 rows, lane 1 on top), MARKOV the steps that differ from the pattern. Live edits apply from the next unheard step.
- <a id="capturevar"></a>**Capture var** (GRID menu, on a track with VAR on; it replaces Capture evo) writes what the track plays now — VAR, and the EVO drift on top of it — into the edited pattern, all steps of its length (one undo step, toast `VAR CAPTURED`). Then VAR starts from generation 0 of the written pattern (and the EVO drift starts over). Only while the edited pattern is the one playing (else toast `VAR: OTHER PATTERN`); at generation 0 with no drift, `VAR: NO CHANGES`. Capture gen on a track whose VAR is not at generation 0 refuses with `VAR: CAPTURE VAR FIRST`.
- **Dice** (PROJ) → Pattern turns VAR off on the rewritten tracks.
- VAR is saved with the project (TVAR chunk); its rows have no undo (like Volume). Older files open with Var OFF; older firmware skips TVAR.

<a id="mixer"></a>

### MIX

A separate tab at the bottom of the screen, between INST and BANK: tap **MIX**. Eight strips for the half of the tracks where the cursor is (1–8 or 9–16, as in Overview GRID), with the **MAIN** strip on the right.

- **Strip:** name (colored as in GRID: yellow means solo, gray means not audible), the track's Volume fader, a level meter next to it (−48…0 dB post-fader, at MAIN 100%; yellow above −6 dB, a red mark for one second means a full-scale peak), the value, its instrument's sends to delay (`S`) and reverb (`R`) — display only, edited in INST — and the **M** (mute) and **S** (solo) buttons. Under the value, on INT tracks, the **pan** row: `C` / `L12` / `R40` and a bar with a tick at the position. On a MIDI track the fader is empty, `MIDI` is shown instead of the value, and there is no pan row.
- **MAIN** — the overall volume of the built-in sound, 0–200%, default 100%. Above 100% you get up to +6 dB, and loud peaks are soft-clipped (the fill turns yellow). It is project data: saved with the project, applied before the soft clip and included in Export. The headphone / speaker level of the device is a separate setting: FILE → SYSTEM → Phones.
- **Encoder:** turn — **MAIN** volume (or the selected pan, see below); <kbd>Shift</kbd>+turn — half **A** (tracks 1–8) / **B** (9–16), shown as a letter on the MAIN strip. Hold a track button and turn — that track's volume (with <kbd>Shift</kbd>, step ×10; a click while holding switches the turn to pan); <kbd>Shift</kbd>+track button — mute. Solo is only by tapping S. An encoder click does nothing on MIX.
- **Pan:** tap a strip's pan row — that track is selected (the row is highlighted), and the encoder turn (and <kbd>A</kbd> + turn, ×10 with <kbd>Shift</kbd>) changes its pan instead of MAIN. With no pan selected, <kbd>A</kbd> + turn on MIX moves the [scene crossfader](#scenes) (<kbd>Shift</kbd> ×8), with or without PERF. Tapping the same row again, tapping another strip's pan row (selects that one), a <kbd>B</kbd> tap, <kbd>Shift</kbd>+turn (half switch) or leaving the tab drops the selection: the encoder turns MAIN again.
- **Touch:** tap or drag on a fader — volume set by finger position (in 8 px steps; for precise values use the track button and encoder); tap M / S.
- **PERF:** the **PERF** button under the MAIN strip (yellow — on) or <kbd>B</kbd> (when neither a pan nor the crossfader is selected) turns [PERF](#live) on / off — the same mode as in GRID and SONG. While playing, a track button holds its effect, the encoder changes the held effect's depth instead of MAIN, <kbd>Shift</kbd>+button still mutes; holding a button does not turn the volume in PERF.
- **Scope** across the full width at the bottom: the last ≈ 14 ms of the audio output (left and right summed), ~20 frames per second. Auto-gain (up to ×32, value at top left) stretches a quiet signal to the full height; the real level is the meter on the right (yellow near the top); `CLIP` means a full-scale sample occurred (shown for one second).
- **Scene crossfader** — the bar between the M / S buttons and the scope, under the eight strips (it stays out of the MAIN column); the SCENE view: <kbd>Shift</kbd>+<kbd>A</kbd>; see [Scenes and crossfader](#scenes).
- Track volume and pan without going to MIX: hold its button and turn the encoder; a click while holding switches to pan (see [track buttons](#trackkeys)).

<a id="scenes"></a>

### Scenes and crossfader

Like the Octatrack crossfader: a **scene** is a set of parameter locks for the mix and the sound; two scenes sit at the fader ends **A** and **B**, and the fader morphs the sound from one into the other live. A project has 8 scenes; a scene holds only the parameters set in it.

- **Track parameters** (INT only): VOL, PAN, DRV, DTY (drive model), BIT, SRR, GTE (trance gate depth), DLY and RVB (the track instrument's sends), MOD (the MOD bus send), CUT and RES (the track instrument's filter). The scene holds DLY / RVB / CUT / RES **per track**: two tracks on one instrument morph independently.
- **Master parameters:** MAIN, DTIM (delay time, 1–16 sixteenths), DFB, DTON, DCHR, DLVL (delay), RSIZ, RDMP, RLVL (reverb), MRAT, MDEP, MLVL (MOD bus), EQLO, EQHI.
- **Rule:** a parameter not set in a scene takes the project's value at that end. Continuous values move linearly, DTY and DTIM switch at the middle (64). When neither A nor B holds a parameter, exactly the project's value plays: a project without scenes sounds as before.
- **Priority:** a step p-lock beats the scene (FLT, RES, DLY, RVB, DRV, BIT, SRR, PAN, DTY, GTE, MOD), LFO and follower add on top of the morph. PERF sits after the master; the fader does not touch it.
- The fader is smoothed on the synth board: full travel in ~30 ms, no steps from encoder clicks.

**Crossfader bar on MIX** — under the M / S buttons, above the scope, under the eight strips (the MAIN column stays right of the separator): `A1 ━━━●━━━ B2`.

- Tap or drag on the bar — fader position 0–127; a tap also selects it (the knob is highlighted): the encoder turn and <kbd>A</kbd> + turn move the fader, <kbd>Shift</kbd> ×8.
- Tap `A1` / `B2` to select that end: the turn steps the scene 1–8 (toast `A = SCENE 3`).
- Hold <kbd>A</kbd> / <kbd>B</kbd> + track button N: scene N straight onto end A / B (also in the SCENE view and with the XY pad open; PERF buttons are not triggered).
- The selection is dropped like the pan selection: a second tap, <kbd>B</kbd>, leaving the tab.
- When scene A or B holds a track's VOL / PAN, a yellow tick on the fader and the pan bar shows what sounds now; the strip name is yellow when a scene holds any of the track's parameters. The fader knob is yellow when at least one end is not empty.

**SCENE view** — scene editing. <kbd>Shift</kbd>+<kbd>A</kbd> on MIX opens and closes it; **END**, a <kbd>B</kbd> tap (in CPY mode the first tap cancels the copy) or leaving the tab closes it too. The eight strips show **one parameter** of the edited scene for the eight tracks, the master strip one master parameter.

- The crossfader and the scope make room for a panel of big buttons in two rows. Top: `< SCN 3 >` (tap the left / right half: previous / next scene), `< VOL >` (parameter back / forward), `=A` / `=B` — the edited scene sits at that end, `X64` — the fader position (these two only show). Bottom: **CAP** (VOL + PAN of every INT track from the project's current values into the scene), **CLR** (clear the scene), **CPY** (then track button N: copy into scene N; tap again to cancel), **END**.
- An unset parameter is an empty frame and `---`; under the value, dim `b100` is the project's value. Dragging on a fader sets the value; <kbd>Shift</kbd> + tap on a fader unsets the lock. Tap a strip name to move the cursor to that track, tap the master name for the next master parameter.

| Action (SCENE view) | What it does |
|----------|------------|
| Encoder turn | Nothing (a stray turn does not edit the scene). |
| <kbd>Shift</kbd> + turn | Track parameter VOL … RES. |
| Hold track button N + turn | Track N's value (an unset one starts from the project's value), <kbd>Shift</kbd> ×8. |
| Hold track button N + encoder click | Unset track N's lock. |
| Track button N tap (on release, no turn) | Edit scene N; after CPY: copy into scene N. |
| <kbd>Shift</kbd>+N | Mute, as everywhere. |
| Hold <kbd>A</kbd> / <kbd>B</kbd> + N | Scene N on end A / B (the edited scene stays). |
| Hold <kbd>B</kbd> + turn | Master parameter MAIN … EQHI. |
| Hold <kbd>A</kbd> + turn | The master parameter's value, <kbd>Shift</kbd> ×8. |
| Hold <kbd>A</kbd> + encoder click | Unset the master parameter's lock. |
| Long encoder press | Menu: Capture mix / Clear scene / Copy scene (same as CAP / CLR / CPY). |

Outside the SCENE view nothing changes: a held track button + click cycles VOL / PAN / FLIP, <kbd>B</kbd> + turn switches tabs.

- While the view is open, the fader parks on the edited scene's end (when it is A or B), so you hear what you edit; leaving restores the previous position.

**PERF:** while PERF is on and no effect is held, <kbd>A</kbd> + turn moves the crossfader (<kbd>Shift</kbd> ×8), toast `XFADE 64 A1>B2`. On MIX A + turn always moves the fader, PERF or not, while no pan is selected; on SONG, except in the FOL column; with the [XY pad](#xypad) open too. The header shows `X64` next to `PERF` when the fader is off A.

Scenes, the A / B ends and the fader position are saved with the project (`SCEN` chunk); the project opens as you left it. Export and Resample play at the current fader position. MIDI tracks, mute / solo, effect modes and engine parameters are not part of scenes.

<a id="inst"></a>

## INST: instruments

32 instruments per project, INS1–INS32 (projects saved before 32 open with INS17–INS32 at their defaults). The instrument for a track is chosen in TRACK → Instr (with Out = INT) or with fx PGM. When you enter the tab, the current track's instrument opens.

Parameters are spread over 5 pages (6 for SYNTH). Below the header are the page tabs: **MAIN**, **ENV**, the type page (**OSC** for CHIP, **SMPL** for SAMPLE, **FM**, **DRUM**, **RES** for STRING, **LOFI**; SYNTH has two — **OSC** and **MOD**), **FILT**, **LFO**. The current page is highlighted.

| Input | Action |
|---|---|
| Tap the page tabs | Open the page (cursor on the tabs). |
| Turn | Cycles through the page's rows and the tab strip above the first row (framed when selected). |
| <kbd>A</kbd> + turn on the tabs | Next / previous page (wraps: after LFO comes MAIN); the cursor stays on the tabs. A click on the tabs does nothing. |
| <kbd>B</kbd>+<kbd>Shift</kbd>+turn | The same from anywhere on the page (not while a browser is open). |
| Tap the arrows in the header | Previous / next instrument (wraps). |
| <kbd>Shift</kbd>+turn outside edit mode | Also changes the instrument. The open page stays the same. |
| **PREVIEW** button / encoder long press | Audition the instrument: note C-4, 0.3 s. Works during playback too. On the SMPL page with Slices = NOTE and a slice selected, that slice plays. |
| **PRESET** button / <kbd>Shift</kbd>+encoder long press | Load / Save / Save one-shot / Mutate menu — [presets](#presets), [Save one-shot](#oneshot). On the SMPL page — the SAMPLE menu with the slice commands and the same Load / Save ([SAMPLE](#sample)). |
| Turn, click, tap | As in PROJ: row, edit, value. |

The page is remembered when you switch instruments and when you change Type: for example, from FILT of one instrument you land on FILT of the next.

| Page | Rows |
|---|---|
| MAIN | Name, Type, Volume, Transpose, Fine, Mode, Glide, Dly send, Rvb send |
| ENV | Attack, Decay, Sustain, Release, Vel\>Cut, Vel\>Dec (for STRING — Vel\>Dmp) and an ADSR graph on the right |
| OSC / SMPL / FM / DRUM / RES / LOFI | Type parameters: [CHIP](#chip) — Wave, Duty, PWM rate, PWM depth; [SAMPLE](#sample) — sample editor; [FM](#fm) and [DRUM](#drum) — Machine and 5 macros; [SYNTH](#synth) — oscillators and tables; [STRING](#string) — Model, Damp, Bright, Struct, Position, Excite; [LOFI](#lofi) — Machine, Formula, Sync and 5 macros |
| MOD (SYNTH only) | Sub, Sub oct, Noise, Ring, Fold, Env\>Shp, Env atk, Env dec |
| FILT | Filter, Cutoff, Reso, Flt env, Flt attack, Flt decay, Key track — [filter](#filter) |
| LFO | LFO (1–4), Wave, Sync, Retrig, Rate, Depth, Dest |

### MAIN and ENV

| Parameter | Values | Default |
|---|---|---|
| Name | up to 8 characters, edited like a track name; empty becomes INSn | INS1…INS32 |
| Type | FM / SYNTH / STRING / DRUM / SAMPLE / CHIP / LOFI / KIT (an instrument of the removed MACRO type in an old project loads as CHIP) | FM |
| Volume | 0–127 | 100 |
| Transpose | −24…+24 semitones | 0 |
| Fine | −50…+50 cents | 0 |
| Mode | POLY (up to 4 voices per track) / MONO (one voice, legato) | POLY |
| Glide | OFF, 4–1020 ms; MONO only | OFF |
| Dly send | 0–127, all types | 0 |
| Rvb send | 0–127, all types (for KIT — for the whole kit) | 0 |
| Vel\>Cut (ENV) | −64…+63; gray without a filter | 0 |
| Vel\>Dec (ENV) | −64…+63; FM, DRUM, SYNTH, STRING (Vel\>Dmp), LOFI | 0 |
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

The SMPL page is the sample editor: a large waveform with markers at the top, a button strip below it, and below that a list of rows (4 rows visible, the list scrolls).

| Parameter | Values | Meaning |
|---|---|---|
| Marker | `S`, `E`, `L` or `#n` and position in % | The selected marker (see below). Click to edit: turning moves the marker. |
| Zoom | x1, x2, x4 … 1:1 | Waveform zoom, the same as **−** / **+**. Click + turn (or hold <kbd>A</kbd> + turn) changes it, centered on the selected marker. A view setting: not saved with the instrument. |
| Sample | --- or a project sample | Steps through the [project samples](#samples) (FILE → SAMPLES); samples from other projects are not visible. Selecting a sample sets its Root and Loop and erases the slices. A red name means the sample is unavailable (MISSING or removed from the project): the voice is silent, and after importing it again under the same name it plays again. |
| Root | C-m…G-9 | The note at which the sample plays at its original pitch. With Slices = NOTE, this note plays slice 1. |
| Start / End | 0–100%, step 0.1% | The played region of the sample. Start cannot exceed End. |
| Loop | OFF, FWD, PING | No loop, forward loop, ping-pong. With a loop the sample plays until it is stopped (see below). |
| Loop start | 0–100% | Loop start as a fraction of the Start–End region (loop end = End). Gray with Loop OFF. |
| Reverse | ON / OFF | Play backwards; the loop is mirrored. |
| Slices | OFF, NOTE, FX | How [slices](#slices) are played: OFF — not played (the whole region), NOTE — by notes from Root, FX — with the `SLC` command. |
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

**Without touch.** Every button and gesture has a hardware equivalent:

| Input | Action |
|---|---|
| <kbd>A</kbd> tap / <kbd>Shift</kbd>+<kbd>A</kbd> tap | Next / previous marker, like **\>** / **\<**. |
| Zoom row | Zoom, like **−** / **+**. |
| <kbd>Shift</kbd>+encoder long press or the **PRESET** button | The **SAMPLE** menu (see below). |

There is no separate scrolling: the view follows the marker being moved (Marker row) and centers on the selected marker when the zoom changes.

The **SAMPLE** menu (on the SMPL page instead of the PRESET menu; on other INST pages it stays PRESET with Load / Save):

| Item | Action |
|---|---|
| Record… | Opens the [REC window](#rec): record from the mic, the line input or the tracker output into this instrument. |
| Save one-shot… | Renders a note of this instrument into a new project sample — [Save one-shot](#oneshot). |
| Chop | Same as the **CHOP** button. |
| Add slice | New slice midway between the selected marker and the next one (if there is none — the end of the sample). With <kbd>Shift</kbd> held on the click — at the next transient to the right of the selected marker. The new slice becomes selected. Toasts: `NO SAMPLE` — no sample, `NO ONSET` — no transient to the right, `NO ROOM` — 32 slices already or no space. |
| Delete slice | Same as a **CLR** tap. Gray unless a slice is selected. |
| Clear slices | Same as a long **CLR** press, with a confirmation. |
| To wavetable… | The sample's Start..End region → a new project wavetable, see [Your own tables](#wtmake). Gray without a sample or when MISSING. |
| Load preset / Save preset | As in the PRESET menu — [presets](#presets). |
| Mutate 15% / 30% / 60%, Undo mutate | As in the PRESET menu — [Mutate](#presets). |

- The Marker row is the first on the SMPL page, Chop N / Sens the last: turning wraps around through the tabs; for other pages hold <kbd>A</kbd> and turn on the tabs.
- A slice can only move between its neighboring slices; it cannot overtake them. Slices are stored as a fraction of the whole sample and do not depend on Start / End.
- Transients (attacks) are detected automatically from rising loudness, no closer than 40 ms to each other. They are used both for <kbd>Shift</kbd> snapping and for Chop TRANS.
- Changing the sample (Sample row) erases the slices.

<a id="rec"></a>

#### Recording a sample (REC window)

SAMPLE instrument → SMPL page → the **SAMPLE** menu → **Record…** opens the REC window over the editor. While it is open the input is set up and the meter is live; the sequencer, PERF and the knobs keep working, so you can record over a playing track.

| Row | Values | Meaning |
|---|---|---|
| Source | MIC, LINE, RESAMPLE | MIC — the Audio Shield microphone, LINE — its line input, RESAMPLE — the tracker's own output (INT tracks, with PERF and knobs, as you hear it). Without the shield only RESAMPLE: `RESAMPLE (NO SHIELD)`. |
| Gain | MIC 0..+63 dB (default +32), LINE 0..15 (default 5), RESAMPLE — | Input gain. Can be changed during the take. |
| Thresh | OFF, −60..−6 dB (step 2) | OFF (default) — recording starts at once. A level — REC arms the take (`ARMED` blinks) and it starts when the input crosses the threshold; the 20 ms before it are kept, so the attack is not cut. |
| Mon | OFF / ON | Hear the input mixed over the track (a few ms of delay). OFF by default. With MIC and loud headphones or a speaker MON ON can howl (feedback) — keep the level low. Can be changed during the take. |
| REC | ARM / START NOW → STOP | Starts the take; during it the row becomes **STOP** (save the take). |
| Cancel | | Drops the take: nothing in the project, the file is deleted. Without a take — just closes the window. |

Below the rows: the meter (peak hold 1 s, red at −1 dB and above), the clip counter, the state (READY, ARMED, REC, SAVING) and the time against the **5:00** limit. At 5 minutes the take stops by itself and is saved.

- **STOP** is also: encoder long press, <kbd>B</kbd>, or leaving the INST tab. During a take only Gain and Mon change; Source and Thresh are locked.
- **Save:** `SAVING` with progress, then a toast like `REC1 12.4s, 3 CLIPS`. The take is written to the card as `/samples/rec/RECn.wav` (44.1 kHz 16-bit; stereo for LINE and RESAMPLE, mono for MIC) and imported into the project as sample **RECn** in mono, (L+R)/2 without normalization; the instrument gets it in its Sample row (slices cleared) and the editor shows it. The number is the next free one.
- Source, Gain, Thresh and Mon are kept until reboot.
- Both boards must run firmware with link protocol 14 (flash the WT32 and the Teensy together).

| Toast | Meaning |
|---|---|
| `NO SD` | No card in the synth board — the take does not start. |
| `BUSY` | The card is busy with a bank task (sync, import, export) — try again later. |
| `NO PSRAM` | The synth board has no working PSRAM for the recording buffer. |
| `CARD TOO SLOW` | The card could not keep up: the take stops, what was recorded is saved. |
| `TOO SHORT` | A take under 50 ms: nothing is saved. |
| `SAMPLE LIST FULL` | The project has no free sample slot: the file stays in `/samples/rec/` and can be imported later. |
| `REC KEPT ON CARD` | The project was replaced during the take: the board finishes the file in `/samples/rec/`, the project does not get it. The same happens if the display board reboots: after 2 s the synth board closes the file. |

<a id="oneshot"></a>

#### Save one-shot (render a note into a sample)

**Save one-shot…** in the instrument's **PRESET** menu (on a SAMPLE instrument — in the **SAMPLE** menu of the SMPL page, after Record…; KIT has no such item) renders one note of the instrument into a new project sample — for example, to put a DRUM or SYNTH sound into a [KIT](#kit) lane. The window header is `ONE-SHOT <name>`, with `+FX` when the sound goes through the track insert.

| Row | Values | Meaning |
|---|---|---|
| Note | any | The note to render; defaults to the preview note (C-4; on the SMPL page — the editor's preview note). |
| Hold | 0.05…5 s, step 0.05 (<kbd>Shift</kbd> — 0.5) | How long the key is held before the note-off. Default 0.5 s; kept between opens. |
| Vel | 1…127 | Note velocity. Default 127. |
| Name | | Sample name on the keyboard. Default `<NAME>_N` from the instrument name with the first free N; only A–Z a–z 0–9 \_ - (spaces and other characters become `_`). |
| SAVE / CANCEL | | Render and save / close (also an encoder long press). |

- If the transport is playing, SAVE stops it first (toast `STOPPED`): the render resets the synth.
- The note goes through the current track's [insert](#insert) if that track plays this instrument (`+FX`), otherwise dry. No delay / reverb / MOD sends and no master.
- The render runs until silence (below −60 dB for ≈ 50 ms after the note-off) or **10 s** at most; then silence is trimmed and the sample is normalized to −0.3 dBFS.
- Done: toast `SAVED <name>`; the sample is in the project's sample list right away and can be picked in a KIT lane or a SAMPLE instrument. A name that is already taken asks **Overwrite**.
- Both boards must run firmware with link protocol 14.

| Toast | Meaning |
|---|---|
| `NO SD` | No card. |
| `BUSY` | The card or the synth is busy (sync, import, export, recording) — try again later. |
| `SYNTH DOWN` | No link to the synth board. |
| `SAMPLE LIST FULL` | The project has no free sample slot. |
| `SILENT` | The render came out silent (nothing saved). |

<a id="slices"></a>

### Slices: playback

Up to 32 slices per instrument. Slice n plays from its marker to the next slice; the last one plays to End (if End is to its left — to the end of the sample). While there are no slices, NOTE and FX sound like OFF.

- **NOTE:** the Root note plays slice 1, Root+1 plays slice 2 and so on (slice number = note − Root + 1). Notes below Root and above the last slice are silent. The slice plays at its original pitch (Transpose and Fine work) — handy for laying a break out across the keys.
- **FX:** notes play the whole Start–End region at normal pitch, and fx [SLC](#fx) on a step picks the slice for that step's note: `SLC 0` — slice 1, `SLC 31` — slice 32. The slice plays at the note's pitch (like a regular sample from Root). Without SLC, or with a number not less than the slice count — the whole region.
- You can audition slices with the [track buttons](#trackkeys) right in the editor: 1 — the selected slice, 2 — the next one, etc.
- A slice plays once: Loop and Loop start are ignored. Reverse works within the slice (from its end to its start), and so does OFS.

<a id="fm"></a>

### FM

**FM** — 8 machines: KICK, SNARE, METAL, PERC, TONE, CHORD, CLAP, HAT. Instead of operators there are 5 macros: DECAY (length), COLOR (brightness / index), SHAPE (timbre variant; for CHORD — the chord type), SWEEP and CONTOUR (pitch or index envelope). Drums play one-shot (note length and ADSR don't matter, a new note cuts off the previous one); TONE and CHORD hold the note (attack / sustain / release from the ADSR). TONE can be POLY; the other machines are mono per track. The LFO is on the shared [FILT and LFO](#filter) pages; for FM it can also drive the macros.

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
- The LFO phase (with Retrig ON) resets on every new note except legato: TONE in MONO and CHORD (always mono, overlapping notes play legato).

<a id="drum"></a>

### DRUM

**DRUM** — 24 machines: 16 "analog" machines in two sets (names ending in 8 and in 9), with their own synthesis (tones with a pitch envelope, a click, "metal" from 6 squares, filtered noise, drive), and 8 more after CY9 — BDA, SDA, BDS, SDS, CPX, RSX, BDF, HHR (resonant and synthetic kicks and snares, a clap, a rim shot, an FM kick, a ring-modulated hat; [second table](#drum2)). The same 5 macro slots as FM and the same `DEC`, `COL`, `SHP`, `SWP`, `CON` locks, but each machine has its own slot names (table below). A slot that does nothing is shown as a gray "-". All values are 0–127; DECAY is a number too — its range differs between machines.

- All machines are one-shot and mono per track: a new note cuts off the previous one without a click (choke); note length and note-off don't matter. Attack, Decay, Sustain, Release, Mode and Glide are gray.
- **Note** C-4 plays the machine's base pitch, other notes are relative to it (tones and metal; clap noise does not depend on the note). Transpose and Fine work as for CHIP.
- Closed / open hat — short and long DECAY (for example, a `DEC` lock); on the same track they cut each other off.
- Changing the machine sets its default macro values. When switching Type to FM or DRUM, the machine and macros are reset as well: the slots mean different things in each.
- FM, DRUM, STRING, SYNTH with a wavetable (at least one oscillator in WT mode) and LOFI BYTE together play no more than 16 voices: a seventeenth steals the oldest of them (releasing ones first); CHIP, SAMPLE, SYNTH without WT and LOFI APC are not affected.
- Hats and cymbals are fully synthesized: an inharmonic set of squares plus noise.

| Machine | Pitch at C-4 | DECAY | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| BD8 | 55 Hz | decay, 5 ms … 4 s | TONE: click | DRIVE | SWEEP: pitch drop up to 12 st | TIME: drop time 5–100 ms |
| SD8 | 180 + 330 Hz, noise HP 1.8 kHz | tone decay | TONE: low / high tone balance | SNAPPY: noise level | SWEEP: up to 7 st | N.DEC: noise length 0.3–2 × DECAY |
| TOM8 | 110 Hz | tone decay | NOISE: noise amount | DRIVE | SWEEP: up to 7 st | TIME: 10–200 ms |
| CP8 | noise, BP | tail | FREQ: 600–2500 Hz | SPREAD: 3 claps, spacing 6–14 ms | Q: 1–6 | TAIL: tail level |
| RS8 | 500 + 1700 Hz | 10–120 ms | TONE: tone balance | NOISE: noise click | SWEEP: up to 5 st | \- |
| CL8 | 2.5 kHz | 10–120 ms | TONE: ±1 octave | DRIVE | SWEEP: up to 3 st | \- |
| CB8 | 540 + 800 Hz (squares) | decay | FREQ: BP 1.5–4 kHz | BAL: 540 ↔ 800 Hz | ACCENT: peak at the start of the hit | \- |
| HH8 | metal, set 8 (205–800 Hz) | 20 ms … 2 s | HP: 4–12 kHz | NOISE: metal ↔ noise | SPREAD: frequency spread ×0.8–1.25 | N.DEC: noise length 0.3–1.5 × DECAY |
| CY8 | metal, set 8 × 0.7 | 0.3–4 s | HP: 2–8 kHz | NOISE | SPREAD | N.DEC |
| BD9 | 50 Hz | decay, 5 ms … 4 s | ATTACK: beater click | DRIVE | SWEEP: up to 24 st | TIME: 10–80 ms |
| SD9 | 190 + 345 Hz, noise HP 1 kHz | tone decay | TONE | SNAPPY | SWEEP: up to 7 st | N.DEC |
| TOM9 | 120 Hz | tone decay | NOISE | DRIVE | SWEEP: up to 12 st | TIME: 10–200 ms |
| CP9 | noise, BP | tail | FREQ: 800–3000 Hz | SPREAD: 4 claps | Q | TAIL |
| RS9 | 1700 + 3400 Hz | 10–80 ms | TONE | NOISE | SWEEP: up to 5 st | \- |
| HH9 | metal, set 9 | 20 ms … 2 s | HP: 6–14 kHz | NOISE | SPREAD | N.DEC |
| CY9 | metal, set 9 × 0.6 | 0.4–4 s | HP: 3–10 kHz | RIDE/CR: ride (lower metal) → crash (brighter noise) | SPREAD | N.DEC |

Columns 2–5 are the COLOR, SHAPE, SWEEP, CONTOUR slots (locks `COL`, `SHP`, `SWP`, `CON`); the first slot is always DECAY (`DEC`). "st" means semitones. In the LFO dest list for DRUM the slots use the generic names DECAY…CONTOUR.

<a id="drum2"></a>

The other 8 machines. They follow the same rules (one-shot, choke, note relative to C-4, velocity as accent), but cost more in the [voice pool](#sound) (an estimate until measured on the synth board).

| Machine | Sound | Pitch at C-4 | DECAY | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|---|
| BDA | kick: a short pulse rings a resonant filter, with an attack click and self-FM | 55 Hz | ring length | TONE: brightness | ATTACK: click | PUNCH: self-FM | \- |
| SDA | snare: a pulse rings 5 tuned modes, plus a band of noise | 200 Hz | length | TONE: body ↔ upper modes | SNAPPY: noise level and length | \- | \- |
| BDS | synthetic 909-style kick: a distorted sine with an FM sweep, a saturating VCA, a click and noise | 50 Hz | body length | TONE: brightness and click | DIRT: distortion | SWEEP: FM sweep depth | TIME: sweep time |
| SDS | synthetic snare: two distorted sines (f and 1.47 f) with a short FM, filtered noise | 180 Hz | length | TONE: noise band 0.5–2× | SNAPPY: drum ↔ noise | FM: pitch blip | \- |
| CPX | clap: noise through two band-pass filters, a burst of claps then a tail | noise, BP | tail | FREQ: band 350–4000 Hz | TONE: filter Q | SPREAD: clap spacing | PATTERN: 16 patterns of clap timing and levels |
| RSX | rim shot: a pulse rings 3 modes, hard-clipped, plus noise | 470 Hz | length | TONE: high-pass and body | NOISE | \- | \- |
| BDF | 2-operator FM kick: carrier 20–100 Hz with a pitch sweep, the modulator with feedback | 45 Hz | length | SWEEP: pitch drop up to +1 kHz | TIME: sweep and index decay | INDEX: FM index | RATIO: modulator ratio (64 steps) |
| HHR | ring-modulated hat: 3 pairs of square × saw oscillators, band-pass + high-pass, sample-and-hold noise | metal (200–10500 Hz) | 20 ms … ≈ 2 s | TONE: filter cutoff | NOISE: metal ↔ noise | \- | \- |

Factory presets: two per machine — BDA, BDA BOOM, SDA, SDA SNAP, BDS, BDS DIRT, SDS, SDS FM, CPX, CPX WIDE, RSX, RSX NOIS, HHR C, HHR O (category ANALOG), BDF, BDF ZAP (category FM); and the kit **ANALOG** (KIT, category DRUM) built from them.

<a id="synth"></a>

### SYNTH: oscillators and wavetables

**SYNTH** — an "analog" synthesizer: 2 oscillators, a sub-oscillator, noise, hard sync, a ring modulator, a wavefolder and a dedicated envelope on SHAPE, followed by the shared ADSR, [filter and LFO](#filter). Each oscillator's mode: **SAW**, **SQR**, **TRI** — classic alias-free waveforms (PolyBLEP), or **WT** — a wavetable (64 frames of 256 points, the frame position is SHAPE). The amplitude envelope is Attack, Decay, Sustain, Release from the ENV page; Mode (POLY / MONO) and Glide work as for CHIP. A SYNTH with a wavetable (at least one oscillator in WT) shares the limit of 16 "heavy" voices with FM, DRUM and STRING; a SYNTH using only SAW / SQR / TRI is light and has the whole pool of 32 available.

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
| Ring  | OFF, 1–127 | OFF | Ring modulator: crossfades the oscillator mix towards the product osc 1 × osc 2 (127 — the product only): bell-like, metallic tones. Turns both oscillators on even at Mix 0%. LFO dest RING. |
| Fold  | OFF, 1–127 | OFF | Wavefolder after Mix and Ring (Sub and Noise stay clean): up to ~4 folds, adds bright harmonics. From ≈1 kHz on osc 1 it weakens and is gone by ≈5 kHz, so high notes don't alias. LFO dest FOLD. |
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

<a id="string"></a>

### STRING

**STRING** — a physical-model resonator: a string or a set of tuned modes, struck by a built-in exciter (a click or a short noise burst) at each note. There are 2 **models**: **STRING** — a plucked string (a delay line with damping and stiffness), **MODAL** — 6 tuned resonant modes (bars, bells, plates). Then come the shared ADSR, [filter and LFO](#filter); Mode (POLY / MONO) and Glide work as for SYNTH. STRING is a heavy voice: together with FM, DRUM and SYNTH with a wavetable it shares the limit of 16 heavy voices, and on the synth board it is among the most expensive types (see [voice pool by cost](#sound)).

STRING has 5 pages: **MAIN**, **ENV**, **RES**, **FILT**, **LFO**.

| Parameter | Values | Default | Meaning |
|---|---|---|---|
| Model | STRING, MODAL | STRING | The model. A change applies from the next note. |
| Damp | 0.05 s … 10 s | 1.4 s | Macro **DAMP**: decay time of the resonance (to −60 dB), exponential over 0–127. |
| Bright | 0–127 | 64 | Macro **BRIGHT**: brightness of the strike and of the ring; lower values dampen the high partials faster. |
| Struct | 0–127 | 0 | Macro **STRUCT**: 0 — harmonic partials; higher — stiffer, inharmonic (STRING: a stiff string; MODAL: bar → bell / plate). |
| Position | 0–100% | 24% | Macro **POS**: where the string or bar is struck; 50% — the middle, where the even partials vanish. |
| Excite | CLICK, 0.5 … 20.5 ms | 0.6 ms | Macro **EXC**: CLICK (0) — a single impulse; higher — a noise burst of that length (a softer, breathier strike). |

The resonance decays by itself: with Sustain 127 the length of a note is set by Damp, and the ADSR shapes it on top (Release fades a held ring sooner). Once the ring has died away (−80 dB) the voice is freed even if the note is still held. A new note on a voice that still sounds strikes the running resonance again instead of restarting it. The lowest playable note is ≈ 21.5 Hz; lower notes hold that pitch.

**Macros and p-locks.** STRING uses all five macro slots of FM, DRUM and SYNTH, so the fx locks `DEC` … `CON` and the LFO destinations work:

| fx | STRING macro | LFO dest | What it changes |
|---|---|---|---|
| `DEC` | DAMP | DAMP | decay time |
| `COL` | BRIGHT | BRIGHT | brightness |
| `SHP` | STRUCT | STRUCT | stiffness / inharmonicity |
| `SWP` | POS | POS | strike position |
| `CON` | EXC | EXC | exciter: click or burst length |

Vel\>Dmp on the ENV page moves DAMP by note velocity (harder notes ring longer), like Vel\>Dec on the other types. The **COMB** filter is not available on STRING (the resonator uses the voice's delay line): the FILT page skips it, and switching Type to STRING turns COMB into OFF.

<a id="wavetables"></a>

### Wavetables

A table is 64 frames of 256 points (one period per frame). In the synth board's flash it is stored with mipmaps: 8 levels with a decreasing number of harmonics (128, 64, 32…); the level is chosen by note pitch, so high notes play without aliasing. One table in flash takes 96 KB, in the same bank as samples (shared cache and eviction, see [SAMPLES](#samples)).

**Built-in tables** (names with an asterisk) are always available, without a card and without importing. They are generated in the synth board's flash on its firmware's first start (≈ 770 KB, a few seconds) and are never evicted:

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

**Table selection.** A click or tap on the Table1 / Table2 row opens a list over the whole tab area: built-in tables, then project tables, **DRAW…**, and the last row is **IMPORT…**. On the right is frame 0 of the table under the cursor.

| Input | Action |
|---|---|
| Turn, tap a table | The table is set on the oscillator immediately and plays C-4 (audition; a new table sounds from the next note). |
| Click, **OK** button | Keep the selection and close. |
| **CANCEL** button, long press | Restore the previous table. |
| **DRAW…** | Drawing editor: a new table from keyframes, see [Your own tables](#wtmake). |
| **IMPORT…** | The `/wavetables` list: folders first (up to 4 levels), then `.wav` files; the `..` row goes up (from the root — back to the table list). Click or tap a file — import with progress, toast "IMPORTED name", the table is set on the oscillator. Buttons **OPEN** and **BACK**. |

- **Library on the card:** `/wavetables` (created automatically), subfolders up to 4 levels are allowed. Files are copied on a computer: take the card out of the synth board. Importing does not copy the file: the table goes into the project folder when you save.
- **Formats:** WAV PCM 8 / 16 / 24 bit, mono or stereo (mixed down to mono); the sample rate in the header doesn't matter. The frame layout is detected by length:
  - frames of 256 points, up to 64 frames (up to 16,384 samples);
  - frames of 2048 points (or the size from the `clm` chunk), up to 256 frames; each frame is reduced to 256 points.

  More than 64 frames — 64 are taken evenly; fewer — the missing frames are smoothly interpolated. DC offset is removed from each frame, and the table is peak-normalized. If the length fits no layout — "UNSUPPORTED WAV"; silence — "NOT A WAV FILE".
- **Name** of the table — the file name without extension (up to 16 characters, A–Z a–z 0–9 \_ -). If the project already has a table with this name and different data, the new one gets a suffix `-2`, `-3`…; the same data under the same name — the existing one is used. Up to 32 tables per project ("TABLE LIST FULL").
- **Project:** Save writes the tables selected in instruments to `/projects/name/wt/<table>.wav` (mono, 16 bit, 64 × 256); tables not used by any instrument are removed from the project. Load pulls missing tables from this folder into flash; if the file is missing or holds different data, the table is absent (name red, oscillator silent) and counts toward "N SAMPLES MISSING".
- **A SYNTH preset** stores table names. If a table is neither built-in nor in the project, loading the preset imports it from `/wavetables/<name>.wav` (only from the root and only with playback stopped); otherwise the name stays red.

> Importing a table writes to flash and works only with playback stopped ("STOP PLAYBACK FIRST"). Selecting an already imported or built-in table works during playback too.

<a id="wtmake"></a>

#### Your own tables: from a sample and by drawing

Both ways give an ordinary project table (like an imported one): the file `/wavetables/<name>.wav` in the
library root (64 × 256, 16 bit, with a `clm` chunk that other synths read too), then the import into flash
and an entry in the project's table list. The file is in the root so that a SYNTH preset using the table
finds it in another project. The name comes from the keyboard (up to 16 characters A–Z a–z 0–9 \_ -); if a
file with that name exists — the **Overwrite** menu. Works only with playback stopped ("STOP PLAYBACK
FIRST"); the project needs room (up to 32 tables, "TABLE LIST FULL"). An unused table leaves the project on
Save, like an imported one; the file in `/wavetables` stays (delete it on a computer).

**From a sample: SAMPLE → To wavetable…** (SMPL page). The source is the **Start..End** region (Reverse,
the loop and slices don't matter), at least 2048 samples (≈ 46 ms; shorter — "REGION TOO SHORT"). The synth
board computes it, reading the sample from its card, with progress "WT NN%".

| Row | Values | Meaning |
|---|---|---|
| Mode | **CYCLE** / WINDOW | CYCLE: the pitch (40 Hz … 2 kHz) is found at 64 points of the region, and exactly one period from a rising zero is taken at each — for pitched sounds (voice, string, synth). WINDOW: the region is split into 64 equal windows, each squeezed / stretched into a frame — for noise, percussion, anything without pitch. |
| Norm | **FRAME** / TABLE | FRAME evens out the frames' loudness (a decaying sound doesn't fade out by frame 63; gain up to ×16, frames below −60 dBFS repeat a neighbour). TABLE — only the overall peak, the dynamics stay. |
| Seam | **ON** / OFF | Smooths the joint of a frame's end and start (no click every period). |
| Name | keyboard | The sample's name by default. |

Buttons **MAKE** and **CANCEL** (or a long press). Toast "WT name 58/64" — in CYCLE, how many frames found
a pitch (the rest took their neighbours' period). Fewer than 8 of 64 — "NO PITCH (TRY WINDOW)". The table
lands in the project's list; set it on an oscillator with the usual table selection.

**By drawing: DRAW…** in the table list. The editor starts with two keyframes — frames 0 and 63 of the
table on the oscillator (built-ins too); the result is always a new table. At the top the canvas (256
points of one period; thin — the neighbouring keys), below it the frame ruler 0..63 with key marks (up to
16) and the shown frame's pointer, buttons at the bottom. Frames between keys are a linear blend (keys in
opposite phase get a quieter middle — visible on an in-between frame).

| Input | Action |
|---|---|
| Stroke on the canvas | Draws (a line between points). On an in-between frame a key is first made from what is shown. |
| Tap on the ruler | On a mark — select the key; elsewhere — show that (in-between) frame. |
| Drag a mark | Move the key between its neighbours (keys 0 and 63 don't move). |
| **SIN TRI SAW SQR** | Replace the shown frame's shape. |
| **SMTH** | Smooth (one press — one pass). |
| **KEY** / **DEL** | New key at the shown frame / delete the key (0 and 63 can't be deleted). |
| **OK** | Name keyboard → write and import → the table is set on the oscillator, the list closes. |
| **CANCEL**, long press | Leave without a table (with a confirmation if keys were changed). |

Without touch: <kbd>Shift</kbd>+turn — focus ruler / canvas / buttons. Ruler: turn — key, <kbd>A</kbd>+turn
— move the key, click — to the canvas. Canvas: turn — cursor, click — value edit on / off (a turn changes
the value with an 8-point brush), <kbd>A</kbd>+turn — the value directly. Buttons: turn — select, click —
press.

There is no sound in the editor (the synth plays tables only from flash): listen after OK. Keys are not
saved: editing again starts from frames 0 and 63 of the finished table. Drawing works during playback too,
OK after a stop (the editor stays open).

<a id="lofi"></a>

### LOFI: ByteBeat and Atari Punk Console

**LOFI** — dirty 8-bit sound from two machines: **BYTE** (bytebeat — a melody from a one-line formula) and **APC** (Atari Punk Console — an oscillator triggering a one-shot). The type page is **LOFI**: the **Machine**, **Formula** and **Sync** (BYTE only) rows and 5 macros. The volume envelope is Attack, Decay, Sustain, Release from the ENV page; Mode (POLY / MONO), Glide, [filter and LFO](#filter) work as for CHIP. Both machines always end in a DC blocker: the offset is removed and a note starts without a click.

- **BYTE** — the counter `t` runs at a speed set by the note: 8000 values per second at C-4, like the classic formulas, so on C-4 the tunes play at their "native" tempo; an octave up is twice as fast and an octave higher. A new note starts the formula at `t = 0` (MONO legato keeps `t` running, only the speed changes). The formula is evaluated only when `t` changes; above 44,100 values per second (roughly above F#-6) intermediate values are skipped — the dirty aliasing is part of the sound.
- **Sync** (BYTE only, gray on APC) — OFF / ON. ON: `t` runs from the project tempo, 1024 values per 1/16 step, so rhythmic formulas (`t>>10` is the step, `t>>14` the bar of 16) lock to the sequencer grid; the note only picks the octave from C-4 (an octave up doubles the speed), semitones are ignored. LOOP then shows the loop length in steps (OFF, 1 … 1024 st).
- **APC** — an oscillator at the note frequency; each of its edges starts a pulse of length WIDTH. While the pulse runs, new edges are skipped: WIDTH shorter than the period plays the note, longer gives subharmonics (octaves and fifths down, "broken" tones).
- **Voices:** BYTE counts as a heavy voice — together with FM, DRUM, STRING and SYNTH with WT no more than 16; APC is light and gets the whole pool of 32, like CHIP.
- Changing Machine sets the machine's default macros (BYTE: A, B, C — the defaults of the current formula, TONE 0, LOOP 0; APC: WIDTH 20, TONE 100, DRIFT 0, SMOOTH 0). Turning Formula loads the new formula's defaults into A, B, C (TONE, LOOP and Sync stay); loading a project or preset keeps the stored values.

| Slot (fx) | BYTE | APC |
|---|---|---|
| 1 (`DEC`) | **A** — the formula's `a` parameter, 0–127 | **WIDTH** — pulse length, 0.1 to 8 periods (exponential); longer than the period skips edges, subharmonics |
| 2 (`COL`) | **B** — the `b` parameter | **TONE** — output lowpass; 127 is open |
| 3 (`SHP`) | **C** — the `c` parameter | **DRIFT** — slow random pitch wander, up to ±50 cents (a new target every 0.1 s, glided) |
| 4 (`SWP`) | **TONE** — 0: raw steps; up: linear interpolation between values and a one-pole lowpass | **SMOOTH** — 0: raw aliasing pulse; up: smoothed edges (PolyBLEP) |
| 5 (`CON`) | **LOOP** — loops `t`: 0 is off, 1–127 wrap `t` over 2^10 … 2^20 values (at C-4 from 0.13 s to ~2 min); with Sync on — 1, 2, 4 … 1024 steps | — (gray) |

The macros work as on DRUM: the step locks `DEC`, `COL`, `SHP`, `SWP`, `CON`, the LFO targets DECAY…CONTOUR; Vel\>Dec shifts the first slot (A / WIDTH). Every formula reads at least one of `a`, `b`, `c` (in the classics they replace the original constants; the defaults give the original sound); a macro whose variable the current formula does not read is gray.

**Formula** — the built-in BYTE formula (turning cycles through them); gray on APC. A formula is computed in 32-bit integer arithmetic and the result is taken to 8 bits in one of two ways — a property of the formula:

- **unsigned** (most classic formulas): the low byte 0…255, the middle 128 is silence. A value of 0 is the bottom of the wave, which is why the DC blocker after the formula is always on.
- **signed** (marked below): the byte as a signed number −128…127, 0 is silence. Handy for symmetric waves.

| # | Formula | Mode | Character |
|---|---|---|---|
| 0 | 42MELODY | unsigned | "42 melody": a looping square-wave tune, starts with a ~0.25 s rest at C-4 |
| 1 | VIZNUT1 | unsigned | a rhythmic tune with a "breathing" mask |
| 2 | TEJEEZ1 | unsigned | a buzzing pattern that drops an octave every ~8 s (at C-4) |
| 3 | VISY | unsigned | a bass melody with arpeggios |
| 4 | TEJEEZ2 | unsigned | sparse bleeps |
| 5 | SIERPNSK | unsigned | Sierpinski harmony `t&t>>8`: quiet fractal tones |
| 6 | VIZNUT2 | unsigned | two voices, melody and bass |
| 7 | SKURK | unsigned | buzzing saws, a dense rhythmic drone |
| 8 | LOSTSPC | unsigned | "Lost in Space": a spacey melody |
| 9 | BLUEBRY | unsigned | chaotic, noisy |
| 10 | VIZNUT3 | unsigned | a drone with a rhythmic crunch |
| 11 | HARISM | unsigned | glitch, noise bursts |
| 12 | STEPHTH | unsigned | dense polyrhythm |
| 13 | BEAR | unsigned | harsh buzzing noise |
| 14 | RROLA | unsigned | a melodic sequence |
| 15 | MIIRO | unsigned | two voices, melody over bass |
| 16 | NIBBLES | unsigned | a melody from a note table packed into a constant |
| 17 | CROWD | unsigned | a noisy "crowd" texture |
| 18 | SGNTRI | signed | a triangle jumping by octaves |
| 19 | SGNARP | signed | a saw arpeggio |
| 20 | SGNDUET | signed | a 1-bit duet |
| 21 | MELODY A | unsigned | 42MELODY with A as the mask (A = 42 is the original) |
| 22 | SIERP AB | unsigned | Sierpinski: A shifts the rhythm, B the pitch |
| 23 | SQUARES | unsigned | two squares: A and B are the pitches, C the rhythm |
| 24 | BELLS | unsigned | fractal bells: A, B, C bend the masks |
| 25 | SGNPULSE | signed | a pulse: A is the pitch, B the level |
| 26 | TEJEEZ3 | unsigned | tejeez: a shifting arpeggio drone |
| 27 | BITGATE | unsigned | two patterns switched by a bit of `t` |
| 28 | RED | unsigned | red-: a rough rhythmic tune |
| 29 | SHIFTMEL | unsigned | a melody with jumping shifts |
| 30 | SAWFLIP | unsigned | saw up / saw down alternating |
| 31 | MASK129 | unsigned | a thin 2-bit pattern |
| 32 | MODMIX | unsigned | modulo layers, noisy chords |
| 33 | MODSEQ | unsigned | modulo sequence, glitchy |
| 34 | ODDSHIFT | unsigned | a dense buzzing texture |
| 35 | KICK | unsigned | **rhythm:** kick; A / C — pattern of the first / second half bar, B — pitch (lower is higher) |
| 36 | HATS | unsigned | **rhythm:** noise hats; A — accent pattern, B — hits per step |
| 37 | BACKBEAT | unsigned | **rhythm:** kick on step 0, noise snare on step 4 of A = 7 (A = 15: of 16); B — kick pitch, C — snare length |
| 38 | ARP | unsigned | **rhythm:** major arpeggio; A — steps per note (8: one, 9: two), B — gate, C — octaves down |
| 39 | ARPMIN | unsigned | **rhythm:** minor arpeggio over 8 steps; A — speed, B — octaves down |
| 40 | BASSLN | unsigned | **rhythm:** square bass over 8 steps; A — speed, B — pattern (rests), C — pitch |
| 41 | OCTBASS | unsigned | **rhythm:** octave-jumping 16th bass; A / C — root of the even / odd bars, B — octaves down |
| 42 | CHORD | unsigned | **rhythm:** chord stabs on a 16-step pattern; A, B, C — the three notes (8 10 12: a major triad) |
| 43 | MELSEQ | unsigned | **rhythm:** 16-step melody; A — octaves down, B — gate |
| 44 | PWMARP | unsigned | **rhythm:** pulse arpeggio; A — steps per note, B — pulse width, C — width sweep speed |

Formulas 0–17 and 26–34 are bytebeat classics from 2011 (authors credited in the source), 18–25 and 35–44 are D-TRK's own; 35–44 are rhythmic (the rhythm runs on `t>>10…14`) and are meant for Sync. The formula number is stored in projects and presets; new formulas are only appended. Entering your own formula comes later. The same formulas play in the track generator ([GEN BYTE PITCH / BYTE GATE](#genbyte)) and in an LFO with Wave BYTE ([LFO BYTE](#filter)).

**Factory presets** (`[FACTORY]` → LOFI): BYTE — 42 MELDY, VIZNUT, SIERPNSK, LOSTSPC, NIBBLES, MASK 27, SQ DUET, FRACBELL, LOOP ARP, BYTEBASS, SYNCBEAT, SYNCHATS, SYNCARP, SYNCBASS (the SYNC ones with Sync on); APC — APC, APC SUB, APC DRFT, APC SOFT. Your own LOFI presets go to `/presets/LOFI`.

<a id="filter"></a>

### Filter and LFO

The FILT and LFO pages exist for all types. The filter comes after the whole voice (for FM and DRUM — after their own sound); its mode sets the slope and what Cutoff and Reso do ([modes](#filtmodes)). Drive, Bit crush and Downsample are not here but in the [track insert](#insert) (TRACK → INS), after the filter.

| Parameter | Values | Default | Meaning |
|---|---|---|---|
| Filter | OFF, LP, BP, HP, LADDER, MS20, COMB, FORMANT | OFF | Mode, see [the table below](#filtmodes). OFF — bypass: the sound is as without a filter, the other filter rows are gray. COMB is not available on [STRING](#string). |
| Cutoff | 20 Hz … 14 kHz (128 values, exponential); FORMANT: A, A\>E, E … U | 14 kHz | Cutoff frequency; COMB — the comb's pitch, FORMANT — the vowel. Lock — fx `FLT`. |
| Reso | 0–127 | 0 | Resonance (LP / BP / HP: Q 0.5–20; other modes — the table below). High resonance is loud — lower the Volume. Lock — fx `RES`. |
| Flt env | −64…+63 | 0 | Filter envelope depth: ±6 octaves at its peak. 0 — no envelope (attack and decay are gray). |
| Flt attack | 0 ms … 10 s | 0 ms | Envelope rise. |
| Flt decay | HOLD, 1 ms … 10 s | 18 ms | Envelope decay (exponential). HOLD — the envelope stays at the peak. |
| Key track | 0–100% | 0% | Cutoff follows the note from C-4: 100% — one octave per octave (COMB at 100% is tuned to the note). Gray for FORMANT: no effect. |
| LFO | 1–4 | 1 | Which of the instrument's four LFOs the rows below edit; the number in brackets is how many are on. All four run at once; their effects on the same destination add up (VOL — multiplies). |
| Wave | SINE, TRI, SAW, SQR, RND, BYTE | SINE | LFO shape; RND — a random value each period; BYTE — a bytebeat formula (the Formula and A rows, see below). |
| Sync | FREE / TEMPO | FREE | TEMPO — the rate is set as a fraction of a bar and follows the project BPM. |
| Retrig | ON / OFF | ON | ON — the LFO phase restarts on every new note (except legato). OFF — one phase per instrument, shared by all its voices and all tracks playing it: a note lands in the running phase. With FREE the LFO runs continuously; with TEMPO it restarts when playback starts, so a 1 BAR LFO always begins at the start of the bar and every run sounds the same. RND with OFF gives one shared value per period. |
| Rate | 0.05–30 Hz or 1/32 … 8 BARS | ≈ 1.3 Hz | Rate: in FREE — an exponential Hz scale; in TEMPO — 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2, 1, 2, 4, 8 bars per period. |
| Depth | −64…+63 | 0 | Depth; 0 — LFO off (then wave, sync, retrig, rate and dest are gray). Full depth: PITCH ±12 semitones, VOL ±100%, CUTOFF ±64 Cutoff steps (≈ ±4.8 octaves), macro ±64; RESO, FENV, DLY, RVB, BIT, SRR, SUB, NOISE, FOLD, RING ±64 steps; FINE ±1 semitone; DUTY ±49%; SEMI2 ±24 semitones; L1–L4 RATE ×/÷16; L1–L4 DEPTH 0…×2. |
| Dest | PITCH, DECAY, COLOR, SHAPE, SWEEP, CONTOUR, VOL, CUTOFF, DRIVE, RESO, FENV, DLY, RVB, PAN, BIT, SRR, FINE, DUTY, SUB, NOISE, SEMI2, FOLD, RING, L1–L4 RATE, L1–L4 DEPTH, L1–L4 RTRG, MOD | PITCH | Destination. DECAY…CONTOUR (macros) — only for FM, DRUM, SYNTH and LOFI (for SYNTH they are called SHP1, SHP2, MIX, DET, SENV, for LOFI by the machine: A, B, C, TONE, LOOP / WIDTH, TONE, DRIFT, SMOOTH); STRING has all five, called DAMP, BRIGHT, STRUCT, POS, EXC; DUTY — only for CHIP; SUB, NOISE, SEMI2 (oscillator 2), FOLD and RING — only for SYNTH. Other types skip targets they lack; changing Type turns a missing target into PITCH. CUTOFF acts when the filter is on. DRIVE — ±64 Drive steps at full depth. RESO, FENV, DLY, RVB, BIT, SRR, SUB, NOISE, FOLD, RING shift Reso, Flt env, Dly send, Rvb send, Bit crush, Downsample (track insert), Sub, Noise, Fold, Ring; DRIVE, BIT, SRR, DLY, RVB, PAN act on the whole track by the newest note (see [fx](#trackfx)); FINE — fine pitch; DUTY — the PULSE Duty; PAN — the track's pan (see below); MOD — the track's Mod send (send to the [MOD bus](#modbus), on the whole track); L1–L4 — another LFO of the instrument (see below). |
| Formula | a bytebeat formula, the same 45 as in [LOFI](#lofi) (the turn wraps around); Wave BYTE only | 42MELODY | The LFO BYTE shape. Each of the four LFOs has its own; turning it loads the formula's default A. |
| A | 0–127; Wave BYTE only, grey when the formula does not read `a` | 42 | The formula's `a`. B and C are the formula's defaults. |

- The filter envelope (and the LFO phase with Retrig ON) restart on every new note except legato (MONO with overlap, FM CHORD): there the envelope keeps running, as on a classic acid bass.
- **PAN** (INT tracks): shifts the track's pan by up to ±64 around the track's Pan (or the step's PAN lock) at full depth, limited to hard left / hard right. There is one pan per track and it comes from the LFO of the newest sounding note, so chord notes do not spread across the field; Retrig ON restarts the phase with each new note.
- LFOs can be controlled from steps: fx `LFO`, `LFD`, `LFS`, `LFW`, `LFT`, `LFR` (see the [fx table](#fx)).
- Cutoff = Cutoff (or FLT lock) + envelope + Key track + LFO CUTOFF, limited to 20 Hz … 14 kHz (FORMANT — without Key track).

<a id="filtmodes"></a>**Filter modes.**

| Mode | What it is | Cutoff | Reso |
|---|---|---|---|
| LP, BP, HP | 12 dB/oct SVF: low-pass, band-pass, high-pass | cutoff frequency | Q 0.5–20 |
| LADDER | 24 dB/oct low-pass, a four-pole ladder: deep, round, saturates when pushed | cutoff frequency | resonance; from ≈121 the filter self-oscillates (a sine at the cutoff) |
| MS20 | 12 dB/oct low-pass in the Korg-35 style with saturation in the feedback: aggressive, "screams" at high Reso | cutoff frequency | resonance; self-oscillates from ≈121 |
| COMB | tuned comb: a metallic tone at the cutoff and its harmonics | the comb's pitch, from 86 Hz (lower values stay at 86 Hz); Key track 100% tunes it to the note | feedback: 0 — no comb, higher — a longer, more pitched ring |
| FORMANT | three band-passes that "say" a vowel | the vowel A → E → I → O → U across the range (screen: A, A\>E, E … U); Flt env, Vel\>Cut and LFO CUTOFF sweep it ("wah-yah"); Key track has no effect | vowel sharpness |

Self-oscillation (LADDER, MS20) sounds without any input: after the amp envelope ends it fades out in ≈50 ms, and any LADDER / MS20 tail lasts 2 s at most.

**LFO BYTE.** Wave BYTE plays a bytebeat formula instead of a wave: one LFO period is `t` from 0 to 65535, so Rate (Hz, or a bar fraction in TEMPO) sets how fast the formula runs through the period, and Retrig ON starts it at `t = 0` on every note. The formula's output (unsigned or signed, as in the [LOFI](#lofi) table) is mapped to −1…+1 and moves in steps, unsmoothed: rhythmic and melodic formulas give jagged modulation — stepped cutoff, glitchy pan, broken pitch. Depth, destination and locks (`LFW BYT`) work as for the other shapes. Formula and A are saved with the project (LFOB chunk) and in presets; older files open with formula 0.

**LFO to LFO.** An LFO can modulate another LFO of the same instrument. **L1–L4 RATE** speeds up / slows down the target: its rate × 2^(±4), ≈ ×/÷16 at full depth, in FREE and TEMPO. **L1–L4 DEPTH** scales the target's depth: × (1 + value), limited to 0…2 — −64 silences the target, +63 nearly doubles it. **L1–L4 RTRG** restarts the target at phase 0 on each cycle of the source (RND gets a new value); only depth ≠ 0 matters, its amount doesn't. An LFO can't pick its own RTRG. Modulation reaches the target with a 1 ms lag, so any routing works, loops included. A Retrig OFF target (one phase per instrument) takes RATE and RTRG only from Retrig OFF LFOs — a per-voice LFO has no single phase to give; DEPTH works from any LFO. While playback is stopped, a TEMPO + Retrig OFF target holds and ignores restarts.

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
- KIT has factory presets only: **PRESET** opens Load straight away, `[FACTORY]` → KIT. A factory kit fills the lanes and writes its drums (factory DRUM / FM / CHIP presets) into **INS25…INS32**, or INS17…INS24 when the kit itself is in INS25…INS32; lane k plays note C-4 + k. Whatever was in those slots is overwritten on OK (CANCEL brings them back). Your own KIT presets cannot be saved yet.
- Renaming a sample in FILE → SAMPLES also updates the lanes; Delete leaves the name in the lane (the lane is silent, like MISSING) and asks for confirmation if the sample is used in a lane.

<a id="presets"></a>

### Presets

The **PRESET** button in the INST header (or <kbd>Shift</kbd>+long press of the encoder) opens a **Load** / **Save** menu (on a KIT — Load right away), then a preset browser that fills the whole tab. The same menu has **Save one-shot…** — a note of the instrument rendered into a sample, see [Save one-shot](#oneshot). A preset is the entire instrument: type, parameters, filter, LFO, machine and macros, slices and their modes, SYNTH oscillators and wavetable names, and the name.

**Mutate.** The PRESET menu (and the SAMPLE menu of the SMPL page) also has **Mutate 15% / 30% / 60%**: the instrument's sound moves at random by up to that share of half of each parameter's range — the filter (when it is on: Cutoff, Reso, Env, its attack and decay), the envelope (CHIP, SAMPLE, SYNTH, LOFI, FM TONE / CHORD), the macros (FM, DRUM, SYNTH, LOFI), SYNTH Sub, Noise and Env atk / dec, CHIP pulse width and PWM. Name, type, machine, LOFI formula, volume, tuning, wave, sample, LFOs and sends stay. C-4 plays right after. **Undo mutate** puts the instrument back as it was before the last Mutate (one step; greyed out on another instrument and once the instrument was edited since — a knob, a preset load, a type or slice change; cleared by Load and New). Not on a KIT.

**Recommended insert.** A preset also stores the track insert it was designed with: Drive, Drive type, Bit crush, Downsample. Load (a factory or your own preset) **overwrites** the current track's [insert](#insert) with them if that track plays this instrument (already while auditioning; CANCEL restores the insert too). A preset without a recommendation (Drive and lo-fi off) turns the track insert off. Save writes the current track's insert into the preset (if that track plays this instrument; otherwise — without an insert), so the preset loads with the same sound. Factory presets with drive and lo-fi (GRIT BAS, KICK PNC, BD808 D, BD909 H, BD DIST, SD NOISE, HH HARSH, the LOFI drums, REESE, FM GROWL, CHIPPLK, ROBOT and others) recommend their former Drive / Bit crush / Downsample. A factory KIT applies the kit's own insert to its track (off on the factory kits), and the drums in its lanes carry no insert. A preset loaded into the instrument of a KIT lane does **not change** the KIT track's insert (a lane has no insert of its own; the preset's recommendation is dropped). Templates and demo songs set the track inserts themselves.

**Browser header:** the path and three buttons (Save: the type, then the path inside the type folder). Load always opens at the top: a `[FACTORY]` row, then a folder per type (FM, SYNTH, STRING, DRUM, SAMPLE, CHIP, LOFI) with your own folders (`name/`) and files; inside a folder the first row is `..` (up one level). `[FACTORY]` holds the factory presets built into the firmware, by type (including KIT) and category; they cannot be changed or deleted. A preset of another type changes the instrument's type. Save opens in the folder of the instrument's type.

| Input | Load | Save |
|---|---|---|
| Turn | Row; the preset under the cursor is applied to the instrument at once and plays C-4 (audition, marked `>`). | Row. |
| Tap on a preset | Audition (same as turning). | Write over it (asks Overwrite). |
| Click on a preset | Take it and close. | Write over it (asks Overwrite). |
| Click / tap on a folder | Enter; `..` goes up. | |
| Buttons | **OK** keeps the selected preset, **DEL** deletes your own preset under the cursor (with confirmation), **CANCEL** restores the instrument as it was. | **SAVE**: name on the keyboard (defaults to the instrument name; if the file exists, asks Overwrite), **+DIR**: new folder inside the current one, **CANCEL**: close. |
| Long press of the encoder | Same as CANCEL. | Close. |

- **Load** changes the instrument immediately; CANCEL brings the previous one back. Auditioning works during playback too: tracks using this instrument play with the new sound.
- **Save** always writes to the folder of the instrument's type; `[FACTORY]` is not shown. File name: up to 16 characters, A–Z 0–9 \_ -; the first 8 characters also become the instrument name.
- A **SAMPLE preset** stores the sample name. If that sample is in the project, it is applied together with the preset's Root and slices; otherwise the instrument's sample, Root and slices stay as they were. The Slices, Chop and Chop N / Sens modes are always taken from the preset. There are no factory SAMPLE presets.
- A **SYNTH preset** stores the oscillator wavetable names: built-in tables are always available, a project table is matched by name, and a missing one is imported from `/wavetables` (see [wavetables](#wavetables)). Factory SYNTH presets use only built-in tables.
- **Format:** presets are saved in v7 format, including slices, SYNTH parameters, LFOs (with the LFO BYTE formulas) and the LOFI formula; older files load (a LOFI formula and LFO BYTE formulas missing from them are 0).
- **Folders:** up to 4 levels inside a type folder (deeper gives "TOO DEEP").
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
| DRUM | ANALOG | BDA, BDA BOOM, SDA, SDA SNAP, BDS, BDS DIRT, SDS, SDS FM, CPX, CPX WIDE, RSX, RSX NOIS, HHR C, HHR O |
| DRUM | FM | BDF, BDF ZAP |
| SYNTH | BASS | BASS, ACID, SUBBASS |
| SYNTH | LEAD | LEAD, SYNCLD |
| SYNTH | PAD | PAD, PWMSTR, WTSWEEP |
| SYNTH | KEYS | PLUCK, BELL |
| STRING | PLUCK | NYLON, HARP, PIZZ |
| STRING | KEYS | MARIMBA, BELL, KALIMBA |
| LOFI | BYTE | 42 MELDY, VIZNUT, SIERPNSK, LOSTSPC, NIBBLES, MASK 27, SQ DUET, FRACBELL, LOOP ARP, BYTEBASS, SYNCBEAT, SYNCHATS, SYNCARP, SYNCBASS |
| LOFI | APC | APC, APC SUB, APC DRFT, APC SOFT |
| KIT | DRUM | 808 KIT, 808 DEEP, 909 KIT, 909 HARD, LOFI KIT, HARD KIT, ANALOG |
| KIT | FM | FM KIT, FM PUNCH, FM PERC |
| KIT | CHIP | CHIP KIT |
| KIT | MIX | HYBRID, DUB KIT |

<a id="proj"></a>

## PROJ: project and pattern

Six pages: **SONG** (tempo, scale, tuning, seed, pattern, groove), **DLY** ([delay](#delay)), **RVB** ([reverb](#reverb) and shimmer), **MOD** ([MOD bus](#modbus)), **MSTR** (compressor, sidechain, master EQ), **PERF** (button effects and their start values). Device settings (Phones, Preview, theme, autosave, versions, synth board, card, diagnostics) are in [FILE → SYSTEM](#system). Switch pages as in TRACK and FILE: tap a page tab, or put the encoder on the page tabs and hold <kbd>A</kbd> + turn, or <kbd>B</kbd>+<kbd>Shift</kbd>+turn.

| Parameter | Values | Scope |
|---|---|---|
| **SONG** | | |
| BPM | 20–300 | project |
| Scale root | C…B | project |
| Scale | 13 scales | project |
| Tuning | 12-TET, 10 factory, your own `.scl` files from the card; default 12-TET | project: the built-in sound's [tuning](#tuning). The encoder scrolls the list, a pick applies at once |
| Tuning root | AUTO, C-m…G-9; default AUTO | project: the key that keeps its 12-TET pitch; AUTO is C-4 + Scale root. Grey in 12-TET |
| Seed | 1–9999, default 1 | project: seed of the song's [follow actions](#follow) (and [evolve](#evolve), [VAR](#var)); live play mixes it with a random number at every start, Export uses it as is. Takes effect at the next Play |
| Length | 4–128 | playing pattern |
| Resolution | 1/4, 1/8, 1/16, 1/32, 1/8T, 1/16T | playing pattern |
| Swing | 50–75% | playing pattern (grayed out when a Groove is selected) |
| Groove | OFF, MPC 54/58/62/66, SHUFFLE, PUSH, LAID BACK, DRUNK, BOOM BAP, HOUSE | playing pattern: a template of timing offsets and accents for every 16 steps, used instead of swing. MPC: sixteenth-note swing with accents; SHUFFLE: triplet feel; PUSH: off-beats slightly early; LAID BACK: slightly late; DRUNK: uneven (the same on every pass); BOOM BAP: hip-hop; HOUSE: accent on the off-beats. OFF: Swing applies. |
| **DLY** | | |
| Delay | 1/16–16/16, default 3/16 | project: [delay](#delay) time in sixteenths, follows the tempo |
| Dly mode | DIGITAL, TAPE, PING-PONG, default DIGITAL | project: the [delay](#delay) character: DIGITAL — clean echo; TAPE — tape (pitch drift, saturation, thinner repeats, time changes "stretch"); PING-PONG — echoes alternate left and right |
| Feedback | 0–127, default 50 | project: how much of the echo is fed back for repeats; 0 is a single echo, 127 a long tail (it always decays) |
| Tone | 0–127, default 90 | project: echo brightness (low-pass 300 Hz … 16 kHz, 127 is unfiltered); each repeat is darker than the previous one |
| Wear / Width | 0–127, default 40 | project: in TAPE — Wear, tape wear (wow and flutter, saturation, loss of highs); 0 — tape without drift; in PING-PONG — Width, the echo spread; gray in DIGITAL |
| Dly level | 0–127, default 100 | project: echo volume; 0 turns the delay off (the rows above are grayed out) |
| **RVB** | | |
| Reverb | 0–127, default 60 | project: [reverb](#reverb) size, i.e. tail length |
| Rvb mode | PLAIN, SHIMMER, default PLAIN | project: SHIMMER — the reverb tail is pitch-shifted up and fed back into the reverb: each repeat is higher, a "ringing" tail |
| Rvb damp | 0–127, default 70 | project: high-frequency damping in the tail (higher is darker) |
| Shimmer | 0–127, default 60 | project: how much of the shifted tail returns into the reverb; 0 — same as PLAIN. Gray while Rvb mode = PLAIN |
| Shim int | +12, +7, +24, default +12 | project: shimmer interval in semitones: octave, fifth, two octaves |
| Rvb level | 0–127, default 80 | project: reverb volume; 0 = off, the tail is cleared |
| **MOD** | | |
| Mod type | OFF, CHORUS, ENSEMBLE, FLANGER, PHASER, default OFF | project: the [MOD bus](#modbus) effect; OFF — the bus is off (the tracks' Mod send is gray) |
| Mod rate | 0.05–8 Hz, default ≈ 0.17 Hz | project: effect LFO speed |
| Mod depth | 0–127, default 64 | project: modulation depth |
| Mod fb | 0–127, default 40 | project: feedback, FLANGER and PHASER only (a sharper "whoosh") |
| Mod level | 0–127, default 100 | project: bus return volume; 0 — the bus is silent |
| **MSTR** | | |
| Comp | OFF, 1–127, default OFF | project: [compressor](#comp) on all internal sound; higher means a lower threshold (−6 … −30 dB) |
| Comp rel | 20 ms … 1 s, default ≈ 90 ms | project: compressor release |
| SC track | OFF, T1–T16, default OFF | project: sidechain key track: its sound ducks the whole mix |
| SC depth | 0–127, default 64 | project: sidechain amount |
| EQ low | −12…+12 dB, default 0 dB | project: [master EQ](#eq), 120 Hz shelf |
| EQ high | −12…+12 dB, default 0 dB | project: master EQ, 5 kHz shelf |
| **PERF** | | |
| Button 1 … Button 8 | one of the 15 [PERF](#live) effects or --- | project: what track button N holds in PERF mode; picking an effect sets its Value to the effect's default. New project: 1 REPEAT, 2 ROLL, 3 FILTER, 4 GATE, 5 DELAY THROW, 6 REVERB THROW, 7 CRUSH, 8 TAPE STOP. Old maps are converted: RAT → REPEAT, ROLL UP → ROLL, FILTER LOW / HIGH → FILTER, DELAY MAX / REVERB MAX → DELAY / REVERB THROW, CRUSH / SRR / DRIVE → CRUSH / DOWNSAMPLE / DRIVE, FADE / MUTE / SHORT DECAY → --- |
| &nbsp;&nbsp;Value | the effect's encoder range (see [PERF](#live)), default the effect's default; --- without an effect | project: under each Button row, the depth a press of that button starts at (`1/8`, `LP 20`, `6 BIT`); <kbd>Shift</kbd> ×10 |
| XY pad X · XY pad Y | one of the 15 PERF effects or --- | project: the effect on the [XY pad](#xypad)'s horizontal / vertical axis. New project: FILTER / CRUSH. Y on the same effect as X, or a buffer effect on both, is dim and does nothing |

<a id="reverb"></a>

**Reverb** is a shared reverb (4 comb filters and 2 allpasses, like Freeverb): mono send, pseudo-stereo return (the right channel has its own last allpass, so the tail is wider than the dry sound). Instruments send to it with the Rvb send parameter (before pan), steps with the RVB fx. Delay echoes do not go into the reverb. Changing the size on the fly jumps.

**Shimmer** (Rvb mode = SHIMMER): the reverb tail goes through a pitch shifter (Shim int: +12, +7 or +24 semitones, highs cut around 6 kHz) and is fed back into the reverb input together with the sends — each next reflection sounds higher, the tail "rises" and rings. Shimmer sets how much of the shifted sound returns; the larger the Reverb (size), the smaller the loop gain, so the tail always decays. Shimmer 0 sounds exactly like PLAIN. On FREEZE (PERF) the shift turns off — the frozen tail does not grow.

<a id="comp"></a>

The **compressor** sits on the sum of the internal sound after the MOD bus, delay and reverb, before the [master EQ](#eq) and the master volume: stereo-linked (one gain on both channels), 4:1, 1 ms attack; Comp sets the threshold and the makeup gain (half of the threshold reduction). **Sidechain:** SC track = the kick track, Comp 60–100, SC depth 100+, and the whole mix ducks on every hit ("pumping"); Comp rel sets how fast it comes back. The key is that track's internal sound (INT); the track itself plays as usual. Comp OFF: the compressor leaves the sound untouched. To pump a single track or move a filter with the kick, use the [follower](#follower) (TRACK → FOL).

<a id="delay"></a>

**Delay** is a single shared mono delay for the internal sound (it does not affect the MIDI output): the send is taken before pan, the echoes return to the centre (except PING-PONG, see the modes below). Instruments send sound to it with the Dly send parameter, individual steps with the DLY fx. The time is synced to BPM: 1/16 at 120 BPM = 125 ms. The line is sized for 2 s, i.e. 16/16 down to 120 BPM; at slower tempos the time is capped at 2 s (1 s in PING-PONG: the line is split into two halves). Changing the time or tempo on the fly jumps in DIGITAL (a click on the tail is possible). On stop, the echo rings out.

**Delay modes** (Dly mode): **DIGITAL** — the clean delay. **TAPE** — tape: the time is read smoothly (a change of time or tempo does not jump but "drags" the pitch), Wear adds pitch drift (wow and flutter), saturation and a low and high cut in the repeats — each repeat dirtier and thinner; Wear 0 — clean tape without drift. **PING-PONG** — echoes alternate left and right (mono send), Width sets the spread (0 — centre). Switching into or out of PING-PONG mutes the echo for about a quarter of a second (the line is cleared); TAPE ↔ DIGITAL switches without a gap.

<a id="modbus"></a>

The **MOD bus** is the third shared send effect for the internal sound, for modulation effects: CHORUS (one line, gentle thickening), ENSEMBLE (three lines with different phases — a wide "string" chorus), FLANGER (a short delay with feedback, Mod fb), PHASER (allpass stages, Mod fb — resonance). The send belongs to the track: TRACK → INS → **Mod send** (each INT track has its own, after its insert, before pan); steps use the MOD fx, the instrument LFO the MOD target, and the follower can drive it too. Mono in, stereo return, added to the mix before the delay and reverb (the MOD return does not go into them). Mod type OFF, Mod level 0 or all sends at 0 — the sound is unchanged. Changing the type on the fly does a short fade out and in, no click.

<a id="eq"></a>

**Master EQ** (PROJ → MSTR): two shelves on all internal sound — low at 120 Hz and high at 5 kHz, ±12 dB each; opposite values give a "tilt" (darker / brighter). It sits after the compressor (the compressor does not react to the boost) and before the PERF effects. 0 dB / 0 dB — the EQ is off.

**Scales:** Chromatic, Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Harmonic minor, Melodic minor, Pentatonic major, Pentatonic minor, Blues. The default is C Chromatic, i.e. no restriction.

**How the scale works.** Notes are not filtered; the scale defines the scale degrees. These move by scale degree: note entry with the encoder, Transpose in SCALE mode, NRN, CHD and Fill (RAMP and RANDOM for notes). An out-of-scale note is first snapped to the nearest lower scale note on the first move. Octave shifts and the mini keyboard ignore the scale. Changing the scale does not rewrite notes already entered.

**Swing** delays every second step: 75% means by half a step.

<a id="tuning"></a>

### Tuning

The built-in sound can play in tunings other than 12-tone equal temperament. The tuning belongs to the project (PROJ → SONG → Tuning): **12-TET** (default, the sound exactly as without a tuning), a factory tuning or your own file in the Scala `.scl` format from the card. MIDI out is always 12-TET.

**Factory tunings:** JUST 5-LIMIT (5-limit just intonation), PYTHAGOREAN (wolf fifth G#–Eb), MEANTONE 1/4 (quarter-comma meantone), WERCKMEISTER3 (a well temperament, every key playable), 19-EDO, 24-EDO, 31-EDO (equal divisions), SLENDRO (5 degrees) and PELOG (7 degrees), gamelan approximations, BOHLEN-PIERCE (13 degrees, the period is a twelfth, 3/1). The Tuning list: 12-TET, the factory tunings, then the files in `/presets/TUN` alphabetically.

**How keys map.** The root (Tuning root) is the key that sounds as in 12-TET; AUTO is C-4 plus Scale root, so just intonation plays in the project's key. Each next key is the next degree; after the last degree the tuning repeats, shifted by the period. In 12-degree tunings (JUST, PYTHAGOREAN, MEANTONE, WERCKMEISTER) the keys stay the same and the intervals change. In tunings with another degree count an "octave" of the keyboard spans N keys: in 24-EDO C-4 → C#4 is a quarter tone, and the octave above the root is 24 keys up. Note names in GRID, the track buttons, the mini keyboard and Scale stay 12-tone: the scale picks keys, not tuning pitches.

**Tuned:** CHIP (noise waves included), SAMPLE pitched by note, FM machines TONE and CHORD, SYNTH, STRING. **Not tuned:** DRUM, FM drum machines (KICK…HAT), [KIT](#kit) lanes, SAMPLE with note slices, MIDI out.

**Pitch modifiers.** Transpose, glide, SLD and ARP offsets move in keys (a glide ends exactly on a tuning degree). Fine, pitch bend, VIB and LFO PITCH / FINE are added on top of the tuning in plain cents and semitones. Changing the tuning while playing applies from the next note; switching from one non-12-TET tuning to another is heard at once.

**Your own `.scl` files.** Copy a file onto the card into `/presets/TUN` (the folder is created automatically). The name is up to 16 characters `A–Z a–z 0–9 _ -` plus `.scl`; files with dots or spaces in the name are not listed, rename them. The file is Scala text: lines starting with `!` are comments, the first line is a description, the second the degree count, then one line per degree: cents (with a dot: `701.955`) or a ratio (`3/2`, `2`); the last degree is the period. The list is read on entering the PROJ screen. A bad file gives a toast with the line number (`SCL L5: BAD PITCH`, `TOO MANY NOTES (MAX 64)`, `TOO FEW NOTES`, `BAD PERIOD`, `FILE TOO BIG`) and the tuning stays as it was.

The project stores the name and the table itself: it plays in tune without the file on the card (the Tuning row then shows the name from the project). Editing a `.scl` on a computer does not change projects; pick the tuning again. A broken tuning in a project file falls back to 12-TET (toast `TUNING RESET`). The tuning is not part of instrument presets; a new project is 12-TET. WAV export plays in tune, MIDI export does not. Older firmware opens a tuned project in 12-TET.

### Dice

**DICE** in the PROJ header (or a long press of the encoder on PROJ) rolls a random starting point for the project:

| Item | What it does |
|---|---|
| Roll all | Tempo + scale, sounds and the pattern in one go. |
| Tempo + scale | A genre (HOUSE, TECHNO, TRANCE, DNB, HIPHOP, SYNTHPOP, DOWNTEMPO): BPM from its range, one of its scales, a random root. |
| Sounds | Factory presets on tracks 1–4: T1 **DRUMS** a factory KIT (its drums go to INS25–32, or INS17–24 when the kit itself is there), T2 **BASS**, T3 **CHORDS** (a PAD / KEYS preset), T4 **LEAD** (LEAD / PLUCK). The kit mostly suits the genre (TECHNO / HOUSE / TRANCE a 909, HIPHOP the 808 DEEP or LOFI, DOWNTEMPO DUB / LOFI…; about 70 % of rolls, the rest any kit); without Tempo + scale the genre is the one whose BPM range holds the project tempo. The tracks become INT and take the preset's recommended insert. An instrument another track plays (or a lane of its KIT uses) is never overwritten: the role moves to a free slot, or is skipped when none is left; DRUMS is skipped when another track plays the kit or one of the INS slots its drums would go to. |
| Pattern | Tracks 1–4 of the pattern you hear, rewritten from the generators: Euclid hits on the KIT lanes (kick, snare, hats, clap), a random chord progression ([CHORDS](#chords)), a bass against it ([BASS](#bass)), an arp over the chords ([ARP](#arp)). The pattern length stays; GEN and [VAR](#var) of these tracks are turned off and their [Evolve](#evolve) changes are cleared. Notes stay in the project scale. Drums are written only when track 1 plays a KIT. |
| Sounds + pattern | Both. |
| Undo dice | Puts back what the last roll changed (one step). Edits elsewhere made since stay; edits to the rolled parts are lost. Load and New clear it. |

The toast names the result (`DICE HOUSE 122 A Dorian`). Every roll is different (hardware random). Tracks 5–16, the other patterns, the song, the tuning and the project seed are never touched; mutes and solos stay as they are (neither a roll nor its undo changes them). The roll is computed aside and swapped in at once, so playback does not stutter. Shift+B (pattern undo) does not undo a roll — use **Undo dice**.

<a id="bank"></a>

## BANK: pattern bank

A 4×4 grid of tiles: patterns P01–P16. Each tile shows the number, length, resolution and 16 small squares in two rows (tracks 1–8 on top, 9–16 below): a filled square means the track has data.

- The playing pattern is filled with a yellow border. A queued one has a blinking double border. A copy source has a red border.

| Input | Action |
|---|---|
| Tap / click | **Queue**: during playback switches at the end of the pass, when stopped switches at once. While the song plays: toast `SONG MODE`. |
| <kbd>Shift</kbd>+tap / click | **Immediately**: switches from the next step, keeping the position. Also works while the song plays. |
| Long tap / press | Pattern menu. |
| Turn | Select a tile. |

**Pattern menu:**

- **Copy to…**, then tap the target tile. Everything is copied: steps, length, resolution, swing. Tapping the same tile cancels.
- **Clear**, with confirmation. Also resets length (16), resolution (1/16), swing (50).
- **Length 16 / 32 / 64 / 128**: quick length change.

Any pattern change clears TIE. The pass counter is reset.

<a id="song"></a>

## SONG: the song

A song has up to 128 slots. A slot is a pattern (P01–P16) and the mutes of the 16 tracks. Repeats are equal slots in a row.

- Header: `n/128` and the track numbers 1–16 above the columns. With [PERF](#live) on, the yellow `PERF` badge with the held effects replaces `n/128` (and covers the track numbers it reaches). With the cursor on FOL the header shows the slot's follow instead of the track numbers: `FOLLOW JUMP >07 75%`.
- Row: slot number, `Pxx`, then 16 cells:
  - filled: the track plays;
  - outline: the track is muted in this slot;
  - dim dot: the track has no steps in this pattern (mute can still be set);
  - dim outline: no data and muted.
- Then the **follow** icon (FOL column): dim dot — NEXT, `<` PREV, `=` STAY, `>` JUMP, `?` RANDOM; dim icon — chance below 100 %. See [Follow actions](#follow).
- The heard slot has a highlighted row and a progress bar under its number.
- The cursor is a yellow frame on a cell (columns: PAT, tracks 1–16, FOL); red in pattern edit and follow edit.
- 10 rows are visible, the list scrolls. During play it follows the heard slot; after you turn, tap or scroll, following pauses for 2 s.
- An empty song shows one row `+ ADD Pxx` (the heard pattern). Click or tap adds it.

| Input | Action |
|---|---|
| Turn | Row (in pattern edit: change the pattern). While a [PERF](#live) effect is held: its depth. |
| <kbd>Shift</kbd>+turn | Column. |
| Click on PAT | Pattern edit on / off; <kbd>Shift</kbd>+click in pattern edit cancels it (the old pattern returns). |
| Click on a track cell | Mute / unmute that track in this slot. |
| Click on FOL | [Follow](#follow) edit. |
| Hold <kbd>A</kbd> + turn | Change the slot's pattern (on FOL: its follow action); <kbd>A</kbd>+<kbd>B</kbd> cancels. With [PERF](#live) on and no effect held: the [scene crossfader](#scenes) (<kbd>Shift</kbd> ×8), except in the FOL column. |
| Tap on a cell | Move the cursor there; on a track cell, also toggle mute; on FOL, follow edit. |
| Vertical drag starting on PAT of the selected row | Change the pattern. |
| Other drag | Scroll. |
| [Track button](#trackkeys) <kbd>N</kbd> | Mute / unmute track N in the selected slot. The half 1–8 / 9–16 is the one holding the current track (as in GRID and TRACK), not the cursor column. With [PERF](#live) on and the transport playing: hold its PERF effect (release ends it). |
| <kbd>Shift</kbd>+<kbd>N</kbd> | Live mute, as on every tab (with PERF on too); lasts until the next slot starts. |
| <kbd>A</kbd> tap | **Add**: a copy of the selected slot right below it, and the cursor moves to it (toast `SONG FULL` at 128). |
| Long press / long tap | Row menu. |
| <kbd>B</kbd> tap | Leave pattern / follow edit; otherwise PERF off. |
| <kbd>Shift</kbd>+<kbd>B</kbd> | Undo song edits (8 steps). |

<a id="follow"></a>

### Follow actions

After every pass of a slot the song goes on as its action says: **NEXT** — the next slot (after the last, slot 1 again); **PREV** — the previous one (from slot 1, the last); **STAY** — this slot once more, then it decides again; **JUMP** — slot `>NN`; **RANDOM** — any other slot. **Chance** 0–100 %: if it fails, NEXT. New slots are NEXT, so a song without follow actions plays in order as before.

- **Edit:** put the cursor on FOL and click (or tap): the edited field in the header turns red. Turn — the value (chance in steps of 5 %), <kbd>Shift</kbd>+turn — the next field (action → target for JUMP → chance; NEXT has no chance), click or <kbd>B</kbd> — done, <kbd>Shift</kbd>+click — cancel (toast `CANCEL`). Hold <kbd>A</kbd> + turn on FOL changes the action at once (release keeps it, <kbd>A</kbd>+<kbd>B</kbd> cancels). One edit is one undo step (<kbd>Shift</kbd>+<kbd>B</kbd>). Row menu **Follow: NEXT** resets the slot.
- **Add** (<kbd>A</kbd> tap) copies the follow too. Inserting or deleting slots keeps JUMP pointing at the same slot; a JUMP to a deleted slot goes to the slot in its place (the new last one when the last was deleted).
- **Randomness:** decided from the project **Seed** (PROJ → SONG) mixed with a hardware random number at every Play / <kbd>Shift</kbd>+<kbd>Play</kbd> — every start is a new path. Pause keeps it. Export uses the Seed alone — the same file every time.

**Row menu:**

- **Add**: same as an <kbd>A</kbd> tap.
- **Insert Pxx above**: insert the heard pattern above, with no mutes.
- **Delete**.
- **Mutes ← live**: store the current track mutes in the slot.
- **Clear mutes**.
- **Follow: NEXT**: reset the slot's [follow action](#follow).
- **Clear song**, with confirmation.
- **Perf: ON/OFF**: [PERF](#live), shared with the GRID menu.

### How the song plays

- **Play on the SONG tab** plays the song from the selected slot. **Play on any other tab** loops the current pattern (LOOP). With an empty song, Play loops the pattern.
- **<kbd>Shift</kbd>+<kbd>Play</kbd> on any tab** plays the song from the slot under the SONG cursor; while playing, a short <kbd>Shift</kbd>+<kbd>Play</kbd> starts the song from that slot again (held longer, it is [fill](#cnd)). <kbd>Shift</kbd>+tap on `PLAY` in the status bar does the same. Empty song: toast `SONG EMPTY`.
- While the transport runs, switching tabs does not change the mode.
- The status bar shows `S03 P05` in SONG (`S--` until the song is heard) and `P05` in LOOP. During play, tapping this part of the status bar toggles SONG / LOOP at the end of the current pass (stopped, the tap does nothing: Play picks the mode by tab), with toast `SONG` / `LOOP` (`SONG EMPTY` if the song has no slots). LOOP→SONG continues from the slot under the SONG cursor; SONG→LOOP keeps looping the heard pattern.
- After the last slot (NEXT), slot 1 plays again. Other [follow actions](#follow) pick the next slot their way.
- Edits of a follow action during play apply from the end of the current pass. LOOP→SONG enters the cursor slot without a follow decision.
- **Mutes:** when a slot starts, all 16 track mutes are set from it. <kbd>Shift</kbd>+<kbd>N</kbd> during play changes a mute only until the next slot; the slot itself is not changed. In LOOP the slot mutes are not applied. When the song stops or switches to LOOP, the track mutes return to what they were before the song started; a saved project keeps those mutes too.
- STAY writes the slot's mutes again at every pass: a live <kbd>Shift</kbd>+<kbd>N</kbd> lasts until the next pass.
- Two equal adjacent slots play as a repeat: TIE and the CND counter continue (STAY too). Repeats are made by duplicating slots (<kbd>A</kbd> tap).
- Edits during play are picked up at the end of the pass. Inserting or deleting above the heard slot does not shift the position. Deleting the heard slot: the next one starts at the end of the pass.
- The pattern queue (BANK tile tap, <kbd>B</kbd>+<kbd>Shift</kbd>+<kbd>N</kbd>) works only in LOOP; while the song plays it toasts `SONG MODE`. <kbd>Shift</kbd>+tap on a BANK tile (switch at once) still works.
- When stopped, moving the SONG cursor makes that slot's pattern current, and GRID shows it.

<a id="file"></a>

## FILE: saving and loading

> The card sits in the synth board (Teensy 4.1) and must be **FAT32** or **exFAT**. If the synth board does not see it, the screen shows "NO SD CARD"; if the synth board itself does not answer (`NO SYNTH`), the card is unavailable too. To copy files to and from the card (projects, samples, MIDI, wavetables, presets), take it out of the synth board and use a computer; moving from the old tracker is just copying the old card's contents.

| Item | What it does |
|---|---|
| Save | Save under the current name. Without a name it works like Save As. Works during playback. The project's sample folder is written along with the project (see [below](#samples)). |
| Save As… | On-screen keyboard, then OK. If a file with that name already exists, asks Overwrite. The sample folder is written in full under the new name. |
| Load… | Project list. If there are unsaved changes, asks for confirmation. Stops playback. Samples and wavetables missing from the synth board's flash are pulled in from the project folder. |
| New | Template menu: **EMPTY** (blank), built-in **808 SET**, **909 SET**, **FM SET** (a drum KIT on track 1 and melodic instruments on the following ones), **CHIPTUNE**, **MIDI 8** (8 MIDI tracks on channels 1–8), then your own templates (`> NAME`), **Demo songs…** and **Save as template…**, which saves the current project without notes (instruments, tracks, settings) to `/templates`. Asks about unsaved changes after you choose. |
| Import MIDI… | Import a `.mid` from the `/midi` folder. |
| Export… | Record the internal sound to WAV on the card: the master and track stems; see [below](#render). |
| Wi-Fi firmware… | Firmware update of the tracker and the synth board over Wi-Fi. See [below](#wifi). |
| Retry / Restore autosave | Without a card, Retry: reconnect the card. With a card, if this project has an autosave, **Restore autosave**: load it (the project stays unsaved; Save makes it permanent). |

**Demo songs** (FILE → New → Demo songs…) — eight finished projects built into the firmware, no samples needed; Play on the SONG tab plays the song. Each one shows off part of the device:

| Demo | Tempo, key | What to look at |
|---|---|---|
| DEMO-TRANCE | 138, A minor | supersaw hook, pads chopped by the trance gate (GTE OFF lifts it in the breakdown), pads and bass pumped by the sidechain (key: the KICK track), ARS arpeggio on CHD chords, a breakdown with FLT locks opening the lead and a riser held by TIE, snare rolls with RAT ramps |
| DEMO-CHIPTUNE | 150, C major | CHIP only: chords from the ARP fx, an octave-bouncing triangle bass, a noise kit, VIB on long notes, a bridge, and the last chorus a tone up (transposed copies in patterns P06 and P07) |
| DEMO-ACID | 128, A minor | an acid line with SLD slides, accents and a FLT lock on every step, the filter opening section by section; a 12-step polymeter section (track length), hats on probability (PRB), RES and DLY locks in the break |
| DEMO-LOFI | 84, C major | swing, humanize, FM e-piano 7th chords through bit crush and SRR, vinyl crackle from PRB + NRN noise hits, dark instruments with low cutoffs |
| DEMO-SYNTHWAVE | 108, E minor | driving 16th bass pumped by the sidechain, a gated-reverb clap, a sync-pluck ARS arpeggio, a wavetable lead with VIB, tom fills |
| DEMO-DUBTECHNO | 120, C minor | one FM CHORD (minor 9) stab through a long dark delay and a big reverb, its filter swept by a 4-bar synced LFO and FLT locks, DLY / RVB throws, a drone held by TIE |
| DEMO-IDM | 110, D dorian | every part on its own track length (7, 5, 13, 9, 11, 6 steps against 16): the parts drift; CND 1:2 / 1:3, PRE and NEI conditions, PRB and NRN random notes, a 5/16 delay |
| DEMO-HOUSE | 124, F minor | swung drum machine with humanize, an offbeat FM bass, 7th-chord stabs, congas on probability, a choir breakdown with a clap roll; the delay in TAPE mode (the stab echoes drift), a slow ENSEMBLE on the MOD bus widens the choir and stabs; PERF buttons set for live play (FILTER, REPEAT, ROLL, DELAY THROW, REVERB THROW, MOD THROW, TAPE STOP, FREEZE) |

A demo opens as a new project named after it: Save writes `DEMO-….mtp` to the card, and from there it is an ordinary project to take apart and change.

<a id="render"></a>

**Export…** is an offline render of the internal sound (INT tracks with all effects) to WAV: the master and, optionally, track stems and effect returns, all files the same length; the synth board renders it and writes it to its card. Format: stereo, 44.1 kHz, 24-bit (or 16-bit with dither). The header shows the folder and an estimate: `~63MB x20  4:12  2 PASSES  1 MIDI SKIPPED` (size of one file × number of files, length, passes, skipped MIDI tracks). Rows:

| Row | Values |
| --- | ------ |
| **Source** | `PATTERN 01…16` (the playing one by default) or `SONG` (the song along its [follow actions](#follow) from slot 1 — project Seed: the same render every time; present when the song has slots) |
| **Loops** | 1, 2, 4, 8, 16 — how many times the pattern plays (PATTERN only) |
| **Max slots** | SONG only (grey otherwise): up to this many slot passes, 1–512, default 128 (a pass of STAY counts); the render ends earlier when the song leaves its last slot (NEXT) |
| **Tail** | `AUTO` (default): the render runs past the end until the master stays below −60 dBFS for 1 s (30 s at most), and the files are cut 0.1 s after the last loud sound; `OFF` — exactly the pattern / song length; `2 s`, `4 s`, `8 s` — exactly that much tail; `WRAP` (PATTERN only) — a seamless loop for samples: the last pass's tail already sounds at the start of the file, length exactly Loops × pattern |
| **Tracks** | `ALL` or `SOLOED` (only soloed tracks; present when a solo is on) — what plays in the master |
| **Stems** | `OFF` — the master only; `TRACKS` — plus every audible INT track with notes (for SONG: in the slots on the path) in its own file; `TRACKS+FX` — plus the MOD, DLY, RVB bus returns (when the bus is on and its level > 0) |
| **Format** | `24 BIT` (default) or `16 BIT` (TPDF dither, the same from export to export) |
| **EXPORT** / **Cancel** | run / close |

Files go to `/export/<project>/<src>/`, where `<src>` is `P01`, `P01x4` (a pattern with Loops > 1) or `SONG`: `<project>_<src>_MASTER.wav`, `<project>_<src>_03_BASS.wav` (track number and name; characters other than A–Z 0–9 _ - become `_`, without a name `<project>_<src>_03.wav`), `<project>_<src>_FX_MOD.wav`, `_FX_DLY.wav`, `_FX_RVB.wav`. Take them on a computer (pull the card out of the synth board) or from the [Wi-Fi page](#wifi).

- **Stems are dry:** the track's voices → insert (drive, crush, gate) → volume → pan, without sends (their sound is in the FX files), without the master compressor, EQ and soft clip; MAIN volume applies. With the compressor and EQ off, the stems plus FX sum to the master. Sidechain ducking is not heard in the stems.
- Only when stopped (`STOP FIRST`). Progress shows `EXPORT nn%` (with several passes `EXPORT 1/4`), roughly in real time; a long encoder press or Play cancels (`CANCELLED`, the files of this export are removed).
- **Passes.** With PSRAM on the synth board all files are written in one render (up to 20). Without PSRAM, 6 files per pass: the render repeats until all are written (same result, just longer).
- If the folder exists it asks `OVERWRITE P01?`; after a successful export the other `.wav` files in it (tracks no longer in the set) are removed.
- Done: `4:12 PEAK -1.3dB 17 FILES`; `CLIP` means there was clipping (lower MAIN or the volumes; stems have no soft clip and can clip with MAIN > 100%). Errors: `DISK FULL` (the estimate does not fit the card), `WRITE FAILED`, `AUDIO BUSY` (the sound did not stop within 0.5 s — try again), `NO INT TRACK WITH NOTES` (stems asked, but no such tracks). MIDI tracks are not in the export.
- The render reproduces playback, just without the output latency; PRB / NRN / VRN are random but identical from render to render; the [generator](#gen) sounds as on Play from stop, [evolve](#evolve) and [VAR](#var) follow the project Seed, the same from render to render. Notes pushed past the end of the pattern (NDG, swing) are cut off.

Below the header are the **PROJECTS**, **SAMPLES** and **SYSTEM** page tabs, as in TRACK and PROJ: tap a tab, or put the encoder on them (frame) and hold <kbd>A</kbd> + turn, or <kbd>B</kbd>+<kbd>Shift</kbd>+turn. PROJECTS holds the items above, SAMPLES the [project samples](#samples), SYSTEM the [device settings](#system).

- **Project name:** up to 16 characters, A–Z a–z 0–9 \_ -. Case is preserved, but names differing only in case are the same file.
- **Keyboard:** digits, A–Z, - \_, DEL, CANCEL, OK. <kbd>Shift</kbd>+key gives a lowercase letter. Encoder: turn selects a key, click presses it, long press cancels.
- **Reliability:** saving writes a temporary file, verifies it, and renames the previous version to `.bak`. If the file is corrupted, Load offers **Load backup**. A `.bak` has no sample folder of its own: it takes samples from the same project folder, so a sample removed from the project after that version may turn up MISSING if it has already been evicted from the synth board's flash.
- **Autosave:** unsaved changes are written every N minutes (FILE → SYSTEM → Autosave: OFF, 1, 2, 5, 10 min, default 5) to `/projects/name.auto` (the project file only), while the transport is stopped and nothing has been pressed for 3 s (toast "AUTOSAVE…"). Save deletes it. After a crash or power-off: FILE → Restore autosave.
- **Safe boot:** hold <kbd>Shift</kbd> at power-on and the project is not loaded automatically (toast "SAFE BOOT"). Use it for a project that crashes the tracker on load.
- **Crash log:** if the tracker rebooted because of a crash, the watchdog or a brownout, the reason, firmware version and (if available) a backtrace are written to `/diag/crashlog.txt` on the card in the synth board (as soon as the card is available after the reboot); read it on a computer. The firmware version and the reason for the last reboot are in FILE → SYSTEM.
- **Autoload:** at power-on, the last saved or loaded project is loaded. If it is corrupted, the `.bak` is used (toast "LOADED BACKUP"). If that fails too, the demo loads with the toast "AUTOLOAD: …". New disables autoload until the next save.
- Saved: all patterns, track settings (including mute/solo, Program, Out, Instr, Volume, Pan), 32 instruments, delay settings, tempo, scale and the song (slots with their mutes). The project also stores the list of its samples and wavetables, while the WAVs themselves sit next to it in the project folder (see [SAMPLES](#samples) and [wavetables](#wavetables)). Not saved: undo, the clipboard, Fill parameters and the [SYSTEM](#system) settings (Preview included).

| Path | Contents |
|---|---|
| `/projects/name.mtp` | project |
| `/projects/name.bak` | previous version |
| `/projects/name/*.wav` | project samples (written by Save) |
| `/projects/name/wt/*.wav` | project [wavetables](#wavetables) (written by Save) |
| `/projects/name.auto` | autosave |
| `/diag/crashlog.txt`, `/diag/cpuprof.txt` | log of reboots after crashes, CPU profile results |
| `/export/project/src/*.wav` | [Export](#render) results: master and stems |
| `/firmware/teensy.hex` | synth board firmware for Update synth (put there by the [Wi-Fi page](#wifi) or a computer) |
| `/templates/*.mtp` | your own project templates (FILE → New) |
| `/last.txt` | name of the project to autoload |
| `/midi/*.mid` | files for import (subfolders allowed) |
| `/samples/*.wav` | WAV library for importing into projects (subfolders allowed) |
| `/wavetables/*.wav` | wavetable library for SYNTH (subfolders allowed) |
| `/presets/TYPE/…/name.mti` | [instrument presets](#presets): folders CHIP, SAMPLE, FM, DRUM, SYNTH, STRING, LOFI, each with up to 4 levels of your own folders |
| `/presets/ARP/name.arp` | your own [arp](#arp) patterns (Capture), text |
| `/presets/PRG/name.prg` | your own [chord progressions](#chords) (Capture), text |
| `/presets/BAS/name.bas` | your own [bass patterns](#bass) (Capture), text |
| `/presets/TUN/name.scl` | your own [tunings](#tuning) (Scala), text; copied from a computer, the tracker only reads them |

The `/projects`, `/midi`, `/samples`, `/wavetables` and `/presets` folders (with the type folders) are created automatically.

<a id="samples"></a>

### SAMPLES: project samples

Samples belong to the project: each project has its own list (up to 128), and instruments can only choose from it. A project on the card is self-contained: the `.mtp` plus a folder with its samples:

| Where | What |
|---|---|
| `/projects/name.mtp` | the project and its sample list (name, length, checksum) |
| `/projects/name/<sample>.wav` | project samples: mono, 16-bit, same sample rate as in `/mt-store/` (up to 44.1 kHz). Written by Save: missing and changed files are written, files of samples removed from the project are deleted. |
| `/samples/` | library: samples are imported into the project from here. Import does not copy the file; the sample gets into the project folder when you save. |
| `/mt-store/` on the synth board's card | where samples play from: one file per sample (named by its checksum), shared by all projects, so identical data in different projects is stored once. Import writes here, Load fills it from the project folder, Save copies from it to the folder. Do not edit by hand; Clear cache removes files no project in use needs. |
| synth board's flash | wavetables and the first 100 ms of every sample (the start, the loop start, 25 ms at each slice), so a note starts at once while the rest loads from the card. Not erased by synth board updates (Update synth, Wi-Fi page). |

Screen rows:

- At the top: `FREE n / m KB CACHE k KB` and a bar showing used space. It is about the synth board's flash, which now holds wavetables and sample starts: FREE is its free space, CACHE is how much other projects' entries take up. If there is not enough room in flash for an import or load, the cache is evicted automatically, largest entries first, never touching the current project's samples; if it still does not fit, "BANK FULL".
- **Import WAV…**: a list of `.wav` files from `/samples`. Subfolders first (`name/`, select to enter), then files; in a subfolder the first row **\< Up (..)** goes up one level, and the path is shown in the header. Up to 4 levels of nesting; hidden files (macOS `._*`) are not shown. Selecting a file opens the keyboard with the sample name (defaults to the file name without extension, up to 16 characters A–Z a–z 0–9 \_ -). If the project already has that name, asks **Overwrite**. Import progress is shown in percent. When the list is full, "SAMPLE LIST FULL". **Play** on a file auditions it without importing: the first 8 seconds at the sample's volume with Vol at full (Master applies); Play again or moving the cursor stops it. Works during playback too; in this list the Play button does not start the transport (the button in the header does).
- **Compact**: defragment flash by moving samples to the start so the free space becomes one block. Usually not needed: import and load compact on their own when gaps get in the way.
- **Clear cache**: remove everything from flash and from `/mt-store/` that is not part of the current project (toast "CLEARED n"). Other projects' samples stay in their folders on the card and are pulled back in when those projects are loaded.
- Project sample rows: name, duration, size. A red `MISSING` label means the data is not in `/mt-store/` and could not be taken from the folder: the voice is silent. Click or tap opens the **Rename** / **Delete** menu.
  - **Rename** changes the name in the project and in the instruments using it; a name already in use gives "NAME TAKEN". A MISSING sample cannot be renamed ("SAMPLE MISSING"): its file is looked up by name.
  - **Delete** removes the sample from the project; if it is selected in an instrument or a KIT lane, it asks again (`USED BY INSn. DELETE?`). The data stays in `/mt-store/` and flash as cache until Clear cache; the next Save deletes the file in the folder.

Import, Rename and Delete change the project (a `*` appears); save it so the changes reach the `.mtp` and the folder.

**Loading.** Load and autoload at power-on check every sample of the project: if it is in `/mt-store/`, it is ready; if not, it is copied there from `/projects/name/` with a progress bar (file name, bytes). If the file is missing, corrupted or contains different data (the WAV was changed on a computer), the sample stays in the list as MISSING, with the toast "N SAMPLES MISSING" at the end. To bring it back, put the correct file in the folder and load the project again, or import a WAV under the same name.

**Saving.** If the folder could not be written (card full, write error), toast "SAMPLES NOT SAVED". The `.mtp` file is saved anyway, but the project stays marked `*`: the next Save completes the folder. A MISSING sample leaves its file untouched on save; Save As to a new name copies such files from the old folder.

**Moving a project** to another card or another tracker: copy `name.mtp` and the `name/` folder to `/projects` on a computer (take the card out of the synth board). On load, the samples are copied into `/mt-store/` automatically.

| WAV | Support |
|---|---|
| Format | PCM 8, 16, 24-bit, float 32-bit. Other formats are not supported ("UNSUPPORTED WAV"). |
| Channels | mono or stereo (stereo is mixed down to mono) |
| Sample rate | any; above 44.1 kHz it is downsampled to 44.1 kHz, below that it is stored as is |
| Size | up to the free space on the synth board's card |

> Import, Rename, Delete, Compact and Clear cache work only with playback stopped ("STOP PLAYBACK FIRST"): they write to the synth board's flash (sample starts, wavetables), which stops the audio. Load and New stop playback themselves. If the synth board does not answer, the page shows "SYNTH NOT ANSWERING"; "NO SAMPLE BANK" means the bank in the synth board's flash is not available.

<a id="streaming"></a>

### Samples and SD cards

Samples play from the synth board's card as they go (streaming), so their length is limited only by the card. The first 100 ms of the start and the loop start, and 25 ms at every slice, are kept in the synth board's flash: a note starts at once, and the card has that long to deliver the rest. The sequencer also tells the synth board one step ahead where the next sample note will start (OFS included), so even a start in the middle of a long sample is ready in time.

- **Up to 16 sample voices stream at once.** A 17th takes the place of the oldest one (it fades out in 4 ms, as when voices run out); meanwhile the new note plays from its start in flash.
- **PSRAM** (an 8 MB chip on the first pad under the Teensy, CS0; optional): after a project loads, its samples are copied there in the background, shortest first, until it is full (about 7 MB; the rest holds the PERF buffer). A copied sample plays from memory and does not count towards the 16. The synth board tests the chip at power-on and checks it every 100 ms; if it fails, samples go on from the card until the next restart (FILE → SYSTEM → Samples: `PSRAM FAILED`) and the PERF buffer effects fall silent.
- **Underrun:** if the card falls behind, that voice fades out for a moment and comes back when the data arrives; playback does not stop. The status bar shows `SD!`, FILE → SYSTEM → Samples counts them.
- **Cards:** use a fast, known-good card. Known good: Kingston Canvas Select Plus. Measured with a good card: 16 voices from the card for 10 minutes at the original pitch — no underruns, the card stayed at least 25 ms ahead, synth board CPU 16 %; with PSRAM the margin is 30–37 ms; an octave up (16 voices) it drops to about 6 ms. Cheap cards (SmartBuy) can underrun with many long samples pitched up. FAT32, one partition (MBR).
- **Taking the card out** while playing: samples keep playing as far as their starts in flash go, then fade; nothing hangs. Put the card back and they come back.
- Old projects whose samples were in the synth board's flash are moved to the card (`/mt-store/`) on the first start after the update; then flash keeps only the starts.

**Known issues:**

- While a large file is written to the card (import, save, Wi-Fi upload, render), reading stalls for 150–430 ms: long samples playing at that moment can underrun (`SD!`).

<a id="system"></a>

### SYSTEM: device settings

Settings of the device itself and diagnostics, the same for every project. The settings are stored in the board's memory, not in the project file; none of these rows marks the project as changed (`*`).

| Parameter | Values | Scope |
|---|---|---|
| Phones | 0–100%, default 100% | device: live output level (headphones / speaker), attenuation only; not in Export, the project's master is MIX → MAIN. Stored in the board's memory one second after a change |
| Preview | ON / OFF, default ON | device: entering a note in GRID on an INT track sounds it immediately. Shared by all projects and stored in the board's memory like Phones; not saved with the project |
| Theme | 17 themes, default CLASSIC | device: interface colors (see below) |
| Autosave | OFF, 1, 2, 5, 10 min, default 5 | device: [autosave](#file) interval |
| Firmware | build commit | display only: firmware version ("+" means built with local changes) |
| Synth fw | version and protocol (`Pn`), NO SYNTH | display only: the synth board's firmware; "!" after it means its protocol does not match the tracker's (the status bar shows `SYNTH FW`); NO SYNTH — the board does not answer |
| Update synth | CLICK / NO SYNTH | click, confirm: the synth board is flashed from `/firmware/teensy.hex` on its card (progress `SYNTH FW`) and reboots; the new version is shown when it answers again. The file gets there from the [Wi-Fi page](#wifi) or a computer |
| Card | free / total GB, NO CARD, NO SYNTH | display only: the microSD card in the synth board |
| Samples | SD, SD+PSRAM n%, UNDERRUNS n, NO SYNTH | display only: where samples play from (the synth board's card; with PSRAM, `SD+PSRAM n%` while samples are being copied there and `SD+PSRAM` once done; `SD, PSRAM FAILED` — the chip failed a check, SD only until the synth board restarts) and how many times a sample voice ran out of data since it came up (see [Samples and SD cards](#streaming)) |
| Link | LOST n LATE n CRC n | display only: link counters since power-on: events lost, events that came too late to play on time, damaged frames |
| Last reset | POWER ON, SOFTWARE, PANIC, WATCHDOG, BROWNOUT… | display only: reason for the last reboot of the tracker (crashes are logged to crashlog.txt on the card in the synth board) |
| Audio RAM | SEQ INTERNAL / PSRAM, free K | display only: where the sequencer lives (it should be in internal memory; in PSRAM it costs more) and the tracker's free internal RAM |
| Battery | `3.92V 120mA 85%`, `4.05V CHG 820mA 64%`, NOT FOUND, --- | display only: battery voltage, current and charge from the [INA226](wiring.md#ina226-battery-monitor); CHG — charging; NOT FOUND — no sensor (the boot log says `battery: INA226 at 0x44` (address 0x40–0x4F) / `battery: INA226 not found`); --- — no reading yet. With the stock 0.1 Ω shunt the current tops out at about 820 mA, so while charging it shows ~820 mA |
| CPU profile | CLICK TO START / RUNNING | diagnostics: click to start, click again and the synth board's audio time broken down by stage (queue, events, voice management, voices by type, delay, reverb, PERF, master) is appended to /diag/cpuprof.txt on the card |

<a id="themes"></a>

**Themes.** They change as soon as you turn the encoder. The choice is a device setting, like Phones: it is stored in the board's memory after a second (when the transport is stopped); the project and file are not changed. Themes: CLASSIC (the original), AMBER and PHOSPHOR (monochrome terminals), NORD, DRACULA, SOLARIZED, GRUVBOX, MONOKAI, TOKYO, MOCHA, ROSE PINE (inspired by well-known editor palettes), GAMEBOY, C64 (retro), SYNTHWAVE, OCEAN, CONTRAST (maximum contrast) and PAPER (light).

<a id="wifi"></a>

### Wi-Fi: firmware without opening the case

FILE → **Wi-Fi firmware…** connects the tracker to your home network and opens a web page for firmware updates and [export](#render) downloads. The tracker has no access point of its own: the computer or phone must be on the same network. There is no file manager on the page: projects, samples, MIDI files, wavetables and presets are moved by taking the microSD card out of the synth board and using a computer.

1.  If there are unsaved changes, a menu appears: **Cancel**, **Save & continue** (only for a project with a name), **Continue w/o saving**. The reason: after a firmware update the tracker reboots.
2.  Playback stops, and <kbd>Play</kbd> does not work in this mode (toast "WI-FI MODE").
3.  The first time: a list of networks, then the password on the on-screen keyboard. **\#+=** switches to the symbols page with space, **ABC** goes back. Letters are lowercase; <kbd>Shift</kbd> gives uppercase. The network and password are stored in flash after a successful connection.
4.  The screen shows the address `http://d-trk.local` and the IP (if `.local` does not open, e.g. on Android, use the IP). The page is also at `/firmware`.
5.  **EXIT** (or a long press of the encoder, or switching to another tab) turns Wi-Fi off. **NETWORK** chooses another network. **RETRY** retries the connection after an error.

The page has three sections; in the first two, drag the file with the mouse into the drop zone or pick it with a click:

- **Tracker (.bin):** the file `.pio/build/wt32/firmware.bin`. The tracker's screen shows "FIRMWARE n KB", then reboots after verifying the image. From PlatformIO, while the mode is open: `pio run -e wt32-ota -t upload`. If the upload is interrupted or the image is corrupted, the old firmware stays.
- **Synth board (.hex):** the file `.pio/build/teensy41/firmware.hex`. It is saved on the synth board's card as `/firmware/teensy.hex` ("SYNTH FW n KB"), then the synth board checks it, flashes it ("SYNTH FW n%") and reboots; the page shows the new version when the board answers again. The same file can later be flashed again with FILE → SYSTEM → **Update synth**.
- **Export (.wav):** the files of `/export` on the synth board's card by folder (`/export/<project>/<src>/`) with sizes; **Refresh** reads them again. Click a name to download the file. Speed is about 200 KB/s (the link between the boards limits it): a 4-minute song master at 24-bit (63 MB) takes about 5 min, a full set of stems takes hours; for a stem set, pulling the card is easier. While a download runs the tracker is busy with it (`DL 12/63 MB` on the screen); a cut download resumes where it stopped (the browser sends `Range`).

**Synth board, first flash.** The very first firmware goes into the Teensy over its own USB with Teensy Loader, before assembly. After that it is updated from the tracker: FILE → SYSTEM → Update synth (from `/firmware/teensy.hex` on its card) or the Wi-Fi page.

> The page has no password: while Wi-Fi mode is open, anyone on the same network can flash the tracker and the synth board. Do not use this mode on other people's or public networks.

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
- **Shift+Play while playing (song restart):** Note Off and Stop, then Start, as stop + start.
- Retriggering a note that is already sounding first sends its Note Off.

<a id="sound"></a>

## Internal sound

The built-in synthesizer: FM machines ([FM](#fm)), oscillators and wavetables ([SYNTH](#synth)), a string / modal resonator ([STRING](#string)), analog-style drum machines ([DRUM](#drum)), a sampler (SAMPLE), chiptune (CHIP) and bytebeat / Atari Punk Console ([LOFI](#lofi)), all with a [filter and LFO](#filter); 32 voices shared by all tracks, 44.1 kHz, mono instruments with per-track pan, stereo output. It runs on the synth board (Teensy 4.1), which the tracker drives over a UART link (3 Mbaud). A track plays through it if TRACK → Out is set to **INT**; the instrument is set with TRACK → Instr or the PGM fx.

Sound goes from the synth board to a Teensy Audio Shield Rev D (SGTL5000) and from its jack to the headphones; the display board's NS4168 amplifier and SPK connector are not used. The signal is stereo.

- Headphone volume is FILE → SYSTEM → Phones (the SGTL5000 codec's volume); the project master is MIX → MAIN.
- No shield yet (not soldered): the synth runs but there is no sound — the firmware doesn't find the SGTL5000 and outputs plain I2S on pins 7 / 20 / 21.
- The shield's headphone output uses a virtual ground: don't tie the jack's Sleeve to GND or the case. The shield's line out (LINE OUT) can go to a mixer or audio interface.
- Start at low volume.

Full wiring: [wiring.md](wiring.md#audio-shield-rev-d).

- **Volume:** voice × instrument Volume × track Volume × MAIN on the [MIX](#mixer) tab (default 100%), with soft limiting. Track Volume comes after its [insert](#insert) (Drive, Bit crush, Downsample).
- **Pan:** the track's mono sum is placed by the track's Pan (or the step's PAN lock) plus LFO PAN, constant power normalised to the centre: at `C` both channels get the voice at full level, hard left is +3 dB on the left and silence on the right. The delay, reverb and MOD bus sends are taken before pan (mono); the delay returns to the centre (PING-PONG — alternating sides), the reverb in pseudo-stereo, the MOD bus in stereo. The compressor is stereo-linked; the PERF effects, MAIN and the soft clip work on both channels.
- **Voices:** a shared pool of 32. POLY: up to 4 voices per track (an extra note takes the track's oldest voice); MONO: one voice with legato. FM: drums and CHORD are always mono on a track, and Mode applies only to TONE; DRUM is always mono. FM, DRUM, STRING, SYNTH with a WT oscillator and LOFI BYTE together: no more than 16 voices; SYNTH using only SAW / SQR / TRI and LOFI APC do not count toward this limit. A seventeenth heavy voice fades out the oldest heavy one in 4 ms (no click); filter tails do not count toward the limit. If the pool is full, a voice is stolen from another track. Sample voices stream from the card, up to 16 at once (samples copied to PSRAM do not count), see [Samples and SD cards](#streaming).
- **CPU guard:** when the audio render nears its time budget (above 80 % on average, or one block over 100 %), the oldest voice fades out in 4 ms (releasing voices first) and the pool shrinks to the voices left; once the load drops below 65 % it grows back by one voice every 0.1 s. A heavy project thins out instead of crackling or restarting the device.
- **Voice pool by cost (Teensy synth board):** every note is priced at note-on by its engine and settings (CHIP and samples are cheapest; wavetable SYNTH with fold, ring and LADDER, DRUM (the BDA…HHR machines about the same as the others, HHR and BDS a little more) and STRING cost two to three times more). The summed price of the sounding voices stays under about 65 % of the CPU (the effects and track strips take the rest, about 73 % in all): a note that does not fit fades out the oldest voices in 4 ms (releasing ones first), as many as needed, and always plays. A dense project thins out before the load gets near the CPU guard, which stays as the backstop. The price is fixed when the note starts: an LFO moving fold or cutoff later does not change it. The ESP32-S3 has no such budget.
- **Latency:** events reach the synth board and play about 4 ms after the sequencer step (a fixed delay that absorbs the link), plus the audio output buffer: the internal sound lags the MIDI tracks by about 10 ms, constantly, without jitter. MIDI tracks are not delayed.
- **No synth board:** if it does not answer, the status bar shows `NO SYNTH`: INT tracks are silent, the card is unavailable, MIDI tracks keep playing. After the board comes back, the tracker sends it the sound state (`SYNC`).
- **Sequencer:** RAT, GAT, PRB, TIE, NDG, CHD, STR, CND, VRN, NRN, FLP work as on MIDI. Added: the [synth fx](#synthfx) SLD, VIB, ARP, VSL, OFS, CUT, slice selection SLC, locks of the FM, DRUM, SYNTH, STRING and LOFI macros DEC, COL, SHP, SWP, CON, and filter locks FLT, RES. Mute, solo and stop silence the sound the same way as MIDI notes.
- **Start:** Program Change is not sent to INT tracks; the instrument from PGM (back to Instr from TRACK), PBN and synth fx are reset.
- **Load:** `CPU NN%` in the right corner of the [status bar](#screen).
- Sample import, renaming and deletion, wavetable import, Compact and Clear cache are available only when stopped: there is no sound while the synth board writes to its flash.

<a id="limits"></a>

## Limits

| What | How many |
|---|---|
| Tracks / patterns / steps | 16 / 16 / 4–128 |
| Song | 128 slots |
| Undo | 8 steps (pattern snapshots) |
| BPM / Swing | 20–300 / 50–75% |
| fx per step | 2 |
| Files in a list | 128 |
| Project / track / instrument name | 16 / 8 / 8 characters |
| MIDI import | 512 KB, 16,384 notes, 32 sources |
| Instruments / voices | 32 / 32, of them up to 16 heavy (FM, DRUM, STRING, wavetable SYNTH, LOFI BYTE) (POLY: up to 4 per track; fewer under the CPU guard) |
| Sample voices streaming at once | 16 (samples in PSRAM do not count) |
| Samples | 128 per project, name up to 16 characters |
| Sample length | up to the card's free space (mono, 16-bit, up to 44.1 kHz after import) |
| One-shot render | up to 10 s; hold 0.05–5 s |
| Wavetables | 32 per project, name up to 16 characters; 64 frames × 256 points, 96 KB of flash per table (in the cache shared with samples); 8 built-in; import file up to 256 frames × 2048 |
| Presets | `.mti` file 236 bytes, name up to 16 characters, up to 4 levels of folders in a type folder, up to 64 rows in one folder; 185 factory |
| Arp patterns | up to 32 steps; 106 factory, up to 32 of your own listed, name up to 16 characters |
| Chord progressions | up to 16 chords; 55 factory + RANDOM, 32 rhythms, up to 32 of your own listed |
| Bass patterns | up to 32 steps; 48 factory, up to 32 of your own listed |
| Tuning | up to 64 degrees, period up to 4800 cents; `.scl` file up to 8 KB; up to 64 of your own listed; 10 factory |
| Scenes | 8 per project; 12 track parameters, 14 master; fader 0–127, ~30 ms travel |
| Shift-register generator | 1 per track (not on drum tracks); 2–16 bits, 1–4 octaves; Capture writes one pass of the track length |
| Evolve | 1 per track, up to 128 steps of the track's length; 4 versions kept for live edits: an edit while more than 3 change steps are planned but not heard yet (only a very short, fast pattern with Every 1) starts that track's drift over |
| CHD | 36 types × 4 inversions |
| Long press | 0.5 s |

**Default project:** 120 BPM, C Chromatic, tracks TRK1–TRK16 on channels 1–16, volume 100, gate 50%, CC A 74, CC B 71, 16-step 1/16 patterns with no swing. All tracks are Out INT, track N uses instrument N, volume 100; instruments INS1–INS32 are FM TONE; master volume 100%.
