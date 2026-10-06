# Mixer view, offline render (Render WAV, Resample) — design

## Goal

See and mix all tracks on one screen, and bounce the internal synth offline: a pattern or the whole
song to a WAV on the card, or a pattern straight into a project sample (resampling). Block 5 of the
roadmap of 2026-10-06; assumes blocks 1–4: `kTracks = 16`, KIT drum tracks, chain repeats /
transpose, master compressor / reverb / delay inside `Synth::render`.

## MIXER

Block 5 also adds the hold-button-and-turn volume gesture (below). A second view of the TRACK screen (as GRID has Overview / Detail): `TrackScreen::mixer_`.
Toggle: Shift + encoder click on TRACK (GRID toggles its view the same way, `grid_screen.cpp:257`),
Shift + tap on the TRACK tab (`App::onTouch`, tab row: a Shift-tap on the active TRACK tab calls
`track_.toggleMixer()` instead of `setTab`), and a `Mixer` / `Settings` item in a TRACK context menu
(long press). Settled by `mixer_` only; the parameter list is untouched.

Eight strips of the half holding the cursor (`firstTrack() = curTrack / 8 * 8`, exactly GRID's
rule; moving the cursor to the other half switches the strips) and a master strip at the right.
Strip = 52 px (`kStripW`, 8 x 52 = 416, left margin 8), master = 48 px (`kMasterW`) at x = 432,
separated by a 1 px `kDim` line. Top to bottom:

- name (8 chars), colours as GRID: solo = `kCursor`, audible = `kText`, muted = `kDim`; the selected
  strip has a `kCursor` frame;
- fader: `kFaderH` = 120 px tall, 12 px wide, filled from the bottom to `vol / 127`; value printed
  under it (`vol`); on a MIDI track the fader is drawn in `kDim`, the value field says `MIDI`, edits
  are ignored (velocity scaling is out of scope);
- `S nn` / `R nn`: delay send and reverb send (`Instrument::send`, `Instrument::rsend`) of the
  track's instrument, read-only here (edit in INST);
- `M` and `S` buttons (24 x 16), filled when on.

Master strip: label `MAIN`, the same fader for `Project::masterVol` 0..200 % (the device setting:
written to NVS by `App::saveVolumeIdle` as today; above 100 the fill turns `kCursor`), value `nn%`
under it, no sends, no M / S. The strip is "selected" when the cursor is on it: the master is
position 8 of the mixer cursor (`mixSel_` 0..7 = strips, 8 = master; track buttons and `setCurTrack`
land on 0..7). PROJ loses its `Volume` row (`proj_screen.cpp`): the mixer is the only place to set it;
the manual's PROJ / Volume text moves to the MIXER section.

Touch: Tap on the name selects the track (`App::setCurTrack`); Tap on `M` / `S` toggles
`TrackCfg::mute` / `solo`; `Drag` on a fader sets `vol` from the touch y (top = 127, bottom = 0;
`TouchEvent::y` of the current point, not dy, so the fader follows the finger; on the master the
drag sets `masterVol` 0..200). Encoder: turn = vol ±1 of the selected strip (Shift ±10; on the master
±1 / ±10 %), Shift + turn moves the mixer cursor across the 9 positions, Shift + click toggles the
view, click toggles mute (nothing on the master), long press = the context menu. Every write under `engine::lockProject()` + `markDirty()`; `vol` changes go to the
synth the same way the TRACK list does today (check how `kVol` edits are published — mirror it).
`wantsRedraw`: on mute / solo / vol / curTrack change (compare a small signature), not on play
position.

## Hold a track button + turn: track volume anywhere

Holding track button N (of the visible half) and turning the encoder changes that track's `vol`
±1 (Shift ±10) on every screen, with a toast `TRK5 VOL 100`; the turn is consumed (the screen under
it does not see it). The press itself still selects the track (Shift: mute) as before, so the gesture
adds to the existing behaviour. Not active while GRID is in edit mode (buttons enter notes / lanes)
or PERF is on (block 3); on a MIDI track the toast says `TRK5 MIDI` and nothing changes. Needs the
`TrackRelease` input event that block 3 adds (`trackio.cpp`); `App` keeps `heldTrackBtn_` (-1 = none)
and a `heldTurned_` flag so a press-and-release without a turn behaves exactly as today. Writes under
the project lock, published to the synth like the TRACK list's `kVol` edit, `markDirty()`.

## Offline render (core)

`lib/core/src/render.h/.cpp`, `mt::OfflineRender`. Works on the live `Project` (no copy: 460 KB) with
a `Sequencer` of its own (the live one runs on core 0 under the project mutex) and the live `Synth`
object handed in by the caller (see Memory).

```cpp
struct RenderSpec {
  enum class Mode : uint8_t { Pattern, Song } mode = Mode::Pattern;
  uint8_t pattern = 0;          // Mode::Pattern
  uint16_t tracksMask = 0xFFFF; // bit = track sounds (others muted for the render)
  uint32_t tailBlocks = 0;      // blocks after the end (delay / reverb ring-out), 2 s = 500
};
class OfflineRender {
 public:
  OfflineRender(Project& p, Synth& synth, const RenderSpec& spec);  // start() at t = 0
  bool renderBlock(int16_t out[Synth::kBlock]);  // false when done (nothing written)
  uint32_t blocksTotal() const;  // body + tail, known up front (progress)
  uint32_t blocksDone() const;
  int16_t peak() const;          // |max| so far
  uint32_t clips() const;        // samples at +-32767
};
```

- Time: block n starts at `blockT = n * kBlockUs` (4000 µs, 128 samples at 32 kHz); virtual clock, no
  `nowUs()`. The sequencer is driven with `seq.process(blockT + kBlockUs, sink)` once per block before
  the synth renders it — the live engine calls `process` at least `kLagUs` (6 ms) ahead of the block
  it renders, so every event of the block is scheduled by then. The sink drops MIDI (`send` = no-op);
  `synth(t, track, b, len)` calls `synth.event(eventOffset(t, blockT), track, b, len)` — the same
  `eventOffset` as `audio.cpp::drain` (late = 0, future = kept in a small FIFO for the next blocks; the
  FIFO is drained at the start of each block exactly like `drain`). Result: playback timing, minus the
  14 ms output latency.
- Length: analytic. `passUs(p, idx) = len * ticksPerStep(res) * 625000 / bpm`.
  `Mode::Pattern`: body = `passUs(pattern)`. `Mode::Song`: Σ over the chain of
  `passUs(chain[i]) * repeats(i)` (block 3's repeat count; 1 if the field is absent). `bodyBlocks =
  ceil(bodyUs / kBlockUs)`. After `bodyUs` the sink lets only note-offs / `OFF`-type messages through
  (held notes release, the next pass's lookahead note-ons never sound); notes nudged / swung past the
  pass end are cut — the same thing a live pattern switch does.
- Project mode flags: `Mode::Pattern` needs `songMode = false` and the pattern selected,
  `Mode::Song` needs `songMode = true`. The caller sets them on the live project before constructing
  the render and restores them afterwards (playback is stopped, the project is locked, the dirty flag
  is not touched: `OfflineRender::Guard` RAII in `render.h` does save / set / restore).
- `tracksMask`: new `Sequencer::setTrackMask(uint16_t)` (default `0xFFFF`) and a private
  `audible(tr) = p_.trackAudible(tr) && (mask_ >> tr & 1)` replacing the three `p_.trackAudible(tr)`
  calls in `sequencer.cpp` (`:378, :385, :498`). Per instance: the live sequencer is unaffected, no
  project copy, no mute flags written.
- Determinism: `seq.seed(0x5EED)` so two passes of the same render (resample measures first, writes
  second) are identical.
- CND first-pass rules: `start()` resets `loop_`, the pattern plays once as pass 1 — as live.

## Render WAV (FILE)

FILE menu gets `Render WAV...` (after `Import MIDI...`). Dialog (a `ParamList`, like the Euclid
dialog): `Source` = `PATTERN nn` (turn through 1..16, default = the current pattern) / `SONG` (only if
`chainLen > 0`); `Tracks` = `ALL` / `SOLOED` (`SOLOED` only if a solo is on: mask = the solo tracks);
`OK` / `CANCEL`. Needs playback stopped (`STOP FIRST` toast otherwise) and the card mounted.

Output: `/projects/<name>/render.wav` (SONG) or `/projects/<name>/pNN.wav` (NN = pattern 01..16),
mono 16-bit 32 kHz; the folder is the project's sample folder (created if absent, the project need not
be saved). If the file exists: confirm menu `OVERWRITE render.wav?`. Written via `.tmp` + rename like
`exportWav` (`bank.cpp`); the header (`wavHeader`, with the `mtcr` crc) is written last: the file is
opened, `kWavHeaderBytes` zeros written, blocks appended (`sampleCrc` running), then `seek(0)` and the
real header. Progress `RENDER nn%` through `App::showProgress`, cancel by long press (file deleted).
Done: toast `12.3s  PEAK -2.1dB` (peak = `20*log10(peak/32767)`; `CLIP n` appended when clips > 0).

## Resample (GRID)

Context menu items `Resample track` and `Resample pattern` (after `Euclid...`). Playback must be
stopped (`STOP FIRST`). Render = current pattern, `tailBlocks = 0`, `tracksMask` = the current
track, or the pattern's audible tracks (`trackAudible`), `Mode::Pattern`.

Two passes over the same deterministic render:
1. measure: peak, `lastLoud` = index of the last sample with |x| > -60 dBFS (33), frames = max(lastLoud
   + 1 rounded up to a block, 1 block);
2. write: `bankMakeRoom(frames)`, `bank.begin(kImportName, frames, 32000, 60)`, render again, each
   block scaled by `gain = 0.891 * 32767 / peak` (−1 dBFS; silence → gain 1) and written with
   `bank.write`, crc running; then the same rename-to-key / duplicate handling as `importToCache`
   (`bank.cpp:448-470`) — factor that tail into `finishImport(crc, frames)` and call it from both.

Project list: `projSampleSet(p, name, crc, frames)` with `name = nextResampleName(p)` = `RS1`, `RS2`…
(first n whose name is not in the list, case-insensitive). Toast `RS3 2.1s`. Cap:
`kResampleMaxFrames = 60 * 32000` — a pattern longer than 60 s stops there (`SAMPLE CAP` toast,
the sample is still added). The sample goes into the project's folder on the next Save like any
import. The user assigns it in INST → Sample.

## Memory & audio task

- The live `Synth` is reused: `audio::pauseForFlash()` parks the audio task (it already
  `synth->reset()`s and flushes the queues, `audio.cpp:park`), `audio::liveSynth()` (new accessor,
  valid only while parked) is passed to `OfflineRender`; afterwards `synth->reset()` again and
  `resumeAfterFlash()`. The delay line (256 KB PSRAM), the reverb and the compressor state are the
  live ones — reset clears them. `bank.cpp::FlashWork` already pairs pause / resume: expose it as
  `audio::Paused` (RAII) in `audio.h` and use it for both renders and the bank write.
- The render's `Sequencer` (`sizeof` ≈ heap of scheduled events + 32-step history; print it once in
  the boot log) lives in PSRAM, allocated for the render and freed after.
- Rendering runs on the UI core inside the FILE / GRID action, with the project locked
  (`engine::lockProject()`); the UI redraws only the progress. Speed: the synth at 50 % CPU renders
  2x realtime; a 60 s resample ≈ 60 s wall time for two passes — the progress bar covers both
  (`blocksTotal * 2`).
- The sequencer / synth `Project` references are the live project: nothing is copied.

## Errors

- Not stopped: `STOP FIRST`. No card: `NO SD CARD`. Folder / file open fails: `WRITE FAILED`; write
  short (card full): `DISK FULL`; the `.tmp` is removed in both cases.
- Resample: `BANK FULL` (`bankMakeRoom` false), `WRITE FAILED` (bank write), `SAMPLE CAP` (60 s cut).
- `pauseForFlash()` false (audio task did not park in 500 ms): `AUDIO BUSY`, nothing rendered.
- Cancel (long press during progress): file / bank entry removed, toast `CANCELLED`.

## Tests (native)

- `test_render`: pattern of 16 steps, one note at step 4 on track 0, INT CHIP: the first non-zero
  sample lands in the block of `4 * stepUs` (± 1 block), zero before; `tailBlocks = 0` →
  `blocksTotal == ceil(passUs / 4000)`; a note on track 1 with `tracksMask = 1` renders silence;
  `Mode::Song` with chain {0, 1, 0} (lengths 16 / 32, repeats 1 / 2 / 1) → `blocksTotal` = the sum;
  two renders are sample-identical (determinism); `peak()` and `clips()` on a loud block.
- `test_render` helpers: `normalizePeak(buf, n, peak)` → max |x| = 29204 (−1 dBFS), `trimTail` → last
  index above the threshold, never below one block; `nextResampleName` → `RS1`, then `RS2` when `rs1`
  exists.
- `test_wav`: a header written by `wavHeader(frames = 1000)` then 1000 int16 frames parses back with
  `wavParse` (frames, rate, root, crc).
- `test_sequencer`: `setTrackMask(1 << 3)` — only track 3's notes are scheduled; the default mask is
  all ones.
