# LFO free-running mode and LFO fx Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** LFO Trig mode NOTE / FREE (FREE: per-track phase, reset at playback start, bar-locked with
TEMPO) and step fx LFO / LFD / LFS / LFW / LFT / LFR. Design: `2026-10-07-lfo-free-fx-design.md`.

**Architecture:** The LFO sync byte carries TEMPO (bit0) and FREE (bit1), no format change. Synth keeps
a phase per track and LFO (`TrackRt`), advanced once per control tick and reset by 0xFE; FREE LFOs of
a voice read it. LFO fx: TrackRt holds the selected LFO and per-LFO locks (like `lockMask` / `lock`),
copied to voices at note-on or applied to the sounding voices; the voice's effective LfoCfg = the
instrument's + its locks. The sequencer emits LFO (selector) before the step's other fx.

**Tech Stack:** C++17, PlatformIO, Unity tests (`pio test -e native -f <suite>`).

---

### Task 1: sync flags in the model and codec
- `model.h`: `kLfoTempo = 1`, `kLfoFree = 2`, `lfoTempo(s)`, `lfoFree(s)`; comments.
- `inst_codec.cpp`: pack / unpack `& 3`, rate clamp on `lfoTempo`.
- `synth.cpp` `lfo()`: `lfoTempo(c.sync)`.
- Test (`test_preset_io`): FREE flag survives the LFO record round trip, rate not clamped for FREE alone.

### Task 2: FREE phase in the synth
- `TrackRt`: `float lfoPhase[kLfos]`, `float lfoRnd[kLfos]`; `startTrack` resets them (phase 0, new rnd).
- `render`: at each control tick, before the voices, `advanceTrackLfos()`: per track, cfg of the
  newest sounding voice (with its locks) or of the track's instrument; FREE LFOs advance.
- `lfo()`: FREE reads the track's phase / rnd, no advance.
- Tests (`test_synth`): FREE phase not reset by note-on, shared by two voices, reset by 0xFE.

### Task 3: Fx enum, info, order, format
- `Fx`: `LFO, LFD, LFS, LFW, LFT, LFR` before `Count`; `fxSynthOnly`; `kInfo`, `kOrder` (after CON),
  `fxFormat` (LFD signed, LFW / LFT names, LFR hex).
- Tests (`test_fx_info`): names, formats, synth-only.

### Task 4: LFO fx in the synth
- `TrackRt`: `lfoSel`, `lfoLockMask` (bit = lfo * 4 + param), `lfoLock[kLfos]` (LfoCfg), `lfoRst`
  mask, `lfoRstPh[kLfos]`; reset at kSynthStep / startTrack.
- `Voice`: `lfoLockMask`, `lfoLock[kLfos]`.
- `fx()`: LFO sets the selector; LFD / LFS / LFW / LFT lock (now: sounding voices; else the step);
  LFR sets the track phase, and the voices' (now) or the step's note-ons'.
- `noteOn`: copy the step's LFO locks; apply LFR after resetLfos.
- `control()`: `lfoCfgOf(v, m, i)` = lfoAt + locks.
- Tests: depth lock turns an LFO on, rate lock, LFO 2 selected, LFR sets the phase, empty step hits
  sounding voices.

### Task 5: sequencer emits LFO first
- `step_expand.cpp`: the LFO slot goes out before the other control fx.
- Test (`test_expand`): LFO in the last slot comes out before LFD.

### Task 6: UI Trig row
- `inst_screen.h`: `kLfoTrig` after `kLfoSync`, `kTailRows` 17.
- `inst_screen.cpp`: Sync row keeps bit1; Trig row NOTE / FREE; Rate uses `lfoTempo`.
- Build `pio run -e wt32`.

### Task 7: full test run and build
- `pio test -e native`, `pio run -e wt32`.
