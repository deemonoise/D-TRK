# LFO: free-running / bar-locked mode and LFO fx — design

## Goal

1. An LFO can run free (not restarted at note-on), restarted at playback start: with Sync TEMPO
   it is locked to the bar.
2. Step fx control the LFOs: select an LFO, lock depth / rate / wave / dest, reset the phase.

## 1. Trig mode

New LFO page row `Trig`: `NOTE` / `FREE`, per LFO (1..4).

- `NOTE` — current behaviour: phase per voice, reset at note-on (not on legato / glide overlap).
- `FREE` — one phase per track and LFO (`Synth` runtime, not the voice), shared by every voice of
  the track. Note-on does not touch it. `0xFE` (sequencer start, sample-accurate) resets it to 0,
  so `FREE` + `TEMPO` starts on the first beat and counts beats at the BPM (bar-locked).
  Keeps running after Stop (preview notes hear it).
- Random wave in `FREE`: the track's random value, new at every cycle wrap.
- The track phase advances once per control tick (1 ms) for every track, before the voices, so
  several voices of a track don't advance it several times.

## 2. Storage

No file format change. The sync byte carries both flags: bit0 = TEMPO, bit1 = FREE (`lfoSync` of
LFO 1 and `LfoCfg::sync` of LFO 2..4 in memory and in the LFO record). Old files: bit1 = 0, NOTE.
Helpers `lfoTempo(sync)` / `lfoFree(sync)`. `LfoCfg` stays 5 bytes.

## 3. LFO fx

| Fx  | Value | Meaning |
|-----|-------|---------|
| LFO | 1..4  | target LFO of this step's LF* fx (slot order does not matter; none = LFO 1) |
| LFD | -64..63 | depth lock |
| LFS | 0..127 | rate lock (raw: Hz scale, or a division index if the LFO is TEMPO, clamped) |
| LFW | 0..4  | wave lock: SIN TRI SAW SQR RND |
| LFT | 0..8  | dest lock: PIT DEC COL SHP SWP CON VOL CUT DRV (macro targets on FM / DRUM / SYNTH only, as the instrument's) |
| LFR | 0..255 | phase reset to value / 256 |

- INT tracks only (`fxSynthOnly`).
- Locks work like FLT / DCY: on a step with a note they go to its note-ons and last the voice's
  life; on a step without a note they apply to the track's sounding voices. A locked depth != 0
  turns on an LFO whose instrument depth is 0.
- LFR: NOTE LFO — phase of the step's new voices (after note-on reset) or of the sounding voices;
  FREE LFO — the track's phase.
- One LFO per step can be locked (limitation of the selector).
- Grid display, 3 chars: LFO `  1`, LFD `+32`, LFS ` 64`, LFW `SIN`, LFT `CUT`, LFR ` 80` (hex).
- Encoder order: after the sound locks (after CON).

## Testing

Native unit tests (lib/core): codec round trip of the flags, FREE phase not reset by note-on and
reset by 0xFE, shared phase across voices, LFO fx locks / reset applied to voices, fx table and order.
