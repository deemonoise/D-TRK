# Delay (send effect) — design

## Goal

One tempo-synced mono delay for INT tracks. Instruments send to it, the `DLY` fx locks the send per step.

## Model

- `Project`: `dlyTime` 1..16 sixteenths (default 3), `dlyFb` 0..127 (default 50, gain capped at 0.95),
  `dlyTone` 0..127 (default 90, one-pole LP inside the feedback loop), `dlyLevel` 0..127 (default 100,
  return level). Saved in the project; older files load the defaults.
- `Instrument::send` 0..127, default 0: older projects and presets sound unchanged. Saved in the
  instrument chunk and in presets (`inst_codec`).
- `Fx::DLY` appended before `Count`. New lock bit `kLockDly` (kLocks = 8, fits `uint8_t lockMask`).
  Like FLT / RES: on a step with a note it sets the send of that step's note-ons, on a step without a
  note the send of the track's sounding voices.

## DSP

- `mt::Delay` (`lib/core/src/synth_delay.h`): ring buffer of int16 handed in by the caller (firmware:
  4 s = 128000 samples (256 KB) in PSRAM: 16/16 down to 60 BPM; tests: plain array). No buffer = silent.
- `Synth::render`: each voice renders into a scratch segment; it is added to `mix_` and, with send > 0,
  `send * v` to `send_`. End of block: `mix_ += delay.process(send_)`, then masterVol and soft clip.
- Delay length = `dlyTime * 24 ticks * tickSamples()`, recomputed per block, clamped to the buffer.
  A time / tempo change jumps the read point (no glide).
- `Synth::reset()` clears the buffer; on stop the echoes ring out.

## UI

- PROJ: `Delay` (n/16), `Feedback`, `Tone`, `Dly Level` after Volume.
- INST: `Send` next to Volume, every type.
- `fx_info`: DLY name / help, value 0..127, INT only.

## Tests (native)

Impulse returns after exactly N samples; feedback decays, fb 0 = one echo; send 0 leaves the mix
bit-exact; the DLY lock hits only its step's note-ons; project / preset round trip, old files get the
defaults.
