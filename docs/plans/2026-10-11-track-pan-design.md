# Track pan (stereo output) — design

## Goal
Stereo output with mono instruments: every INT track gets a pan position. The step fx `PAN` locks
it per step, the instrument LFO target `PAN` moves it (autopan). Hardware is already stereo
(PCM5102A L/R → MAX97220 → jack Tip/Ring); today both channels carry the same mono mix.

## Data
- `TrackCfg::pan`: uint8 0..127, 64 = centre. Shown `L64 … C … R63` (value − 64). INT tracks only,
  like `vol`; MIDI tracks ignore it (no CC10).
- Project file: new chunk `TPAN` = count + one byte per track. Missing chunk → 64. TOUT keeps its
  fixed 3-byte records (old readers would break if they grew); old firmware skips TPAN.
- Lock bit `kLockPan` (after `kLockSrr`; 13 of 16 bits).
- `Fx::PAN` appended before `Fx::Count`: 0..127 (display L/C/R), a lock like RVB / DRV: this step's
  note-ons, or the track's sounding voices on a step without a note. INT only (`fxSynthOnly`). In
  the encoder order next to DLY / RVB.
- `LfoDest::Pan` appended before `LfoDest::Count` (file format). Offset ±64 at full depth, summed
  with the base and clamped to 0..127. A display-order table puts it after RVB in the Dest list
  (`lfoDestStep` walks the display order; names stay indexed by the enum).
- Voice pan = clamp((PAN lock ? lock : track.pan) + 64 · LFO(PAN)), computed at control rate.

## Engine (`Synth::render`)
- Voice gains `gl, gr` from a 129-point table, normalised to the centre: `gl = gr = 1` at 64,
  hard left `gl = √2, gr = 0` (constant power ×√2). Old projects (all centre) stay bit-exact in
  the dry path. Gains ramp linearly across each control segment towards the new target (no zipper
  on autopan); the first segment of a note starts at the target.
- Every voice renders into `tmp`, then `L += x·gl`, `R += x·gr` (the direct `mix_ + pos` path goes).
- Sends stay mono and pre-pan: `send_ += x·send`, `rsend_ += x·rsend`. Track peaks (MIX meters)
  from the mono voice signal.
- Buffers: `mix_` and `sc_` become L/R pairs; `send_`, `rsend_` stay mono (+1 KB internal RAM).
- Delay: mono line, the return goes to both channels.
- Reverb, pseudo-stereo: L = combs → ap1 → ap2 (today's path), R = the same combs → ap1 → ap2'
  (length 441 + 23 = 464, the Freeverb spread). `kBufLen` grows by 464 floats (~1.8 KB). Mono
  input gives L ≠ R. Idle detection covers both outputs.
- Compressor: stereo-linked, the detector on max(|L|, |R|) plus the key, one gain on both.
- Sidechain: the key track goes into `sc_` L/R and around the compressor, as today.
- DJ filter: two state sets. Master gain and softClip on both channels.
- Output: new `Synth::renderStereo(int16_t* lr)` writes interleaved stereo `lr[2 * kBlock]`.
  `Synth::render(int16_t* out)` stays as the mono downmix `(L + R) >> 1` of the same block (both
  run the same core): at centre pan it is bit-exact to today's output, so the ~240 test call
  sites keep working; resample uses it too.

## Firmware
- `audio.cpp`: `lr[2i] = L·g·2 + 1`, `lr[2i+1] = R·g·2 + 1`. Scope = (L + R) / 2. One-shot preview
  mixes into both channels.
- Offline: `OfflineRender::renderBlockStereo` next to the mono `renderBlock`. Render WAV writes a
  stereo 16-bit WAV (`wavHeader` gets a channel count). Resample into the bank keeps the mono
  `renderBlock` (the bank is mono).

## UI
- TRACK: row `Pan` after `Volume`, inactive on MIDI tracks.
- MIX: under each strip's fader a thin pan bar (tick from the centre, label `C` / `L12` / `R40`);
  none on MIDI tracks. Tap selects that track: the bar is highlighted and the encoder turns its pan
  (Shift ×10). Tap again, tap another bar, B tap or leaving the tab drops the selection; the
  encoder turns MAIN again.
- Track button held + encoder click: toggles VOL ↔ PAN for the held-button turn; toast
  `TRK3 PAN L12` / `TRK3 VOL 100`. Back to VOL when the button is released. Any tab, same rules as
  the held-button volume (`holdVolume`).

## CPU / RAM
Estimate +2–3 % CPU (pan MACs, second allpass, stereo master stages). Internal RAM +~3 KB.

## Tests (native)
- `test_synth`: centre → L == R == the old mono render; hard left → R == 0; PAN lock and LFO PAN
  move the balance, clamped; the gain ramp has no step at control boundaries.
- `test_synth_reverb`: mono input → L ≠ R; level 0 leaves the mix untouched; idle.
- `test_synth_comp`: stereo-linked, equal gain on both channels.
- `test_project_io`: TPAN round trip; no chunk → 64.
- `test_fx_info`: PAN in the order, L/C/R display, synth-only.
- `test_model`: `lfoDestName` / `lfoDestStep` with PAN and the display order.
- `test_render`: stereo blocks; resample downmix.

## Docs
manual en/ru (TRACK, MIX, track buttons, fx PAN, LFO Dest), wiring en/ru (drop "The signal is mono").

## Out of scope
CC10 on MIDI tracks, kit lane pan, ping-pong delay, true stereo reverb, track LFOs.
