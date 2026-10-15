# Trance gate in the track insert — design

Feature 3 of the "sound next" list: a tempo-synced rhythmic volume gate in each INT track's insert,
after the crush, before the fader. Builds on the track insert (2026-10-14-track-insert-design.md).

## Why

Chopped pads / gated supersaws are a core trance / techno gesture. Today the only ways are an LFO
(Square, TEMPO, Vol) — per voice, not pattern-shaped, its phase integrates Hz and drifts against the
sequencer — or PERF GATE, which chops the whole master mix. A per-track gate on the mono strip costs
one gain ramp per track (no voice scaling) and the gated signal also feeds the sends (gated delay
tails, the classic sound).

## Existing

- Strip (synth.cpp `runStrip`): per hot track and segment (<= 32 samples, events split segments):
  drive -> crush -> sends (pre-pan, x fader) -> meter -> pan into mix / sidechain. The gate slots
  between crush and the sends.
- Step clock: the sequencer sends `0xF6 4 idx` on the first INT track at every 1/16 boundary of the
  *pass* (`pushPerfStep`, idx = pass position / 16th, mod 64, unshifted time = no swing). The synth
  applies it at its exact sample offset; `PerfBus::stepAt` re-anchors the PERF grid at the next
  `process()`. `0xFA` restarts, `0xFC / 0xFB` pause / resume (`paused_`). The synth does not know stop.
- The PERF grid is pass-relative: every pass restarts at 0 (a 16-step pattern never sees grid 16..63),
  and PerfBus advances it at the block end, after the strips have run. Not directly usable for a gate
  that must span > 1 pass (1/8 rate x 16 steps = 2 bars) or be read per segment.
- PERF GATE (synth_perf.cpp): 50 % duty on the grid, 1 ms linear ramp (kGateStep) — the ramp model to
  copy.
- TEMPO LFOs: `lfoSyncHz(div, bpm)`, divisions 1/32 .. 8 bars; Retrig OFF phases restart on 0xFA.
- Swing / groove live only in the sequencer (`stepShift`, per pattern), not in the synth.
- Locks: `LockBit` (kLocks = kFmMacros + 8, <= 16), per voice, the strip reads the newest voice's
  (DRV / BIT / SRR / PAN path). LfoDest appends before Count.
- TINS: `count, recSize, records`; the reader zero-fills and assigns every field it knows.
- UI: TRACK INS page = ParamList rows Drive / Bit crush / Downsample. Name editing already has a
  cursor model (turn = value, Shift+turn = position, tap = position) and screens can take track
  buttons via `trackKey(n, shift)` (GRID, INST, SONG do).

## Approaches

A. **Per-track step gate in the strip (recommended).** TrackCfg holds a 16-bit step mask + rate,
   depth, length, attack, release. Gate position comes from a synth-wide song clock. Presets are a
   flash table of masks the UI copies into the mask (no preset index stored: "USER" = no match).
   Cheap, exact, song-locked, editable.

B. **LFO wave "PATTERN" + Vol target.** New LfoWave reading a mask; reuses LFO UI / locks / LFO->LFO
   mod. But per voice (CPU scales with the pool), per instrument not per track, phase not anchored
   to the sequencer (drift), and a KIT gates per lane. Rejected.

C. **Per-track PERF GATE.** Extend PerfBus with a track mask. Only 50 % duty patterns, master-side
   (after pan, no gated sends), no per-track settings. Rejected.

## Timing / sync

- New synth state `gatePos_` (double, 1/16 steps, wraps at 64 = 4 bars; 64 is a multiple of every
  cycle: 16 steps x 1/32 = 8, x 1/16 = 16, x 1/8 = 32 sixteenths).
- New message `0xF6 5 idx` (song clock): the sequencer sends it with `0xF6 4`, but idx = absolute
  1/16 count since play start (`stepTick_ / (kPpqn/4)` mod 64, only when on a 1/16 boundary), so it
  does not restart per pass. Applied at its sample (segments split at events): `gatePos_ = idx`.
  Between clocks the synth free-runs: per segment `gatePos_ += n / samplesPer16th`
  (`(kPpqn/4) * tickSamples()`, follows bpm changes). Clocks only correct audio-vs-timer drift; a
  triplet pattern (no 1/16 boundaries in a pass) just free-runs.
- `0xFA`: `gatePos_ = 0` (play restarts the gate). Paused (`paused_`): position holds, gate holds its
  gain. Stopped: free-runs from where it was (preview / live notes hear the gate moving) — see Q.
- Pattern switch / song chaining: continuous count, no jump. A pattern of odd length (e.g. 12 steps)
  phases against a 16-step gate — the DAW / trance-gate convention (bar-locked, not pass-locked).
- Rate: 1/32, 1/16, 1/8 (option: 1/16T, 1/8T). Gate step i = floor(gatePos_ / rateSteps) & 15.
  Independent of the track's speed (x2, 1/2 ...) and of the pattern resolution.
- Swing: v1 straight (as PERF GATE). Option: follow the playing pattern's swing via a new
  `0xF6 6 swing` sent at each pass start; odd 1/16 steps of the gate start (swing-50)/50 of a step
  late (groove templates ignored). See Q.
- Preview track: through `0xF7 t` it already takes track t's insert, so the gate too.

## Signal

```
trk_ -> drive -> crush -> GATE -> sends (x fader) -> meter -> pan -> mix
```

- Per hot track with depth > 0, per sample: phase p (in gate steps) = gatePos_seg / rateSteps +
  i * inc; step = int(p) & 15; frac = p - int(p); open = mask bit(step) && frac < len.
  target = open ? 1 : 1 - depth/127. Gain g moves linearly toward target: +a per sample up (attack),
  -r down (release); x[i] *= g.
- Length (duty) 1..100 % of a step; at 100 % adjacent on-steps merge (no dip); < 100 % retriggers.
- Attack / release floor 0.5 ms (16 samples) — no clicks even at 1/32 and 300 BPM (a 1/32 step =
  800 samples at 32 kHz / 150 BPM, 400 at 300 BPM). Defaults: attack 1 ms, release 4 ms.
- Depth 0 = bypass: no multiply, bit-exact to today.
- Strip state: `TrackStrip::Gate { float g; }` + derived increments, refreshed in `refreshStrip`.
  When the strip snaps (panSet false: track was silent) the gate gain snaps to the current target.
- Lock / LFO (option, see Q): fx GTE locks the depth through the voice (`kLockGte`, read by the strip
  from the newest voice like DRV); LfoDest `Gate` (appended before Count) moves the depth.

## Model and file

- TrackCfg, at the end (after crushRate; also the Teensy SynthModel mirror order):
  `uint16_t gateMask = 0x5555` (bit i = step i+1), `uint8_t gateRate = 1` (0 1/32, 1 1/16, 2 1/8),
  `gateDepth = 0` (0..127, 0 = off), `gateLen = 100` (1..100 %), `gateAtk = 2`, `gateRel = 8`
  (0..127 -> 0.5..50 ms, exp). 7 bytes per track, ~112 B in Project (+ padding).
- Presets: `kGatePresets[]` in model.cpp (name + uint16 mask, flash), ~16 patterns
  (1/16 straight, offbeat, "x.xx", 3-3-2, half-time ...). `gatePresetOf(mask)` -> index or USER.
- TINS record grows: drive, bits, rate, [bytes of the parallel distortion design if it lands first],
  gateMask lo, hi, gateRate, gateDepth, gateLen, gateAtk, gateRel. Reader change needed: assign only
  the fields inside `take`, so fields missing from an older record keep TrackCfg defaults (today it
  zero-fills and assigns all — a mask of 0 would be a muted gate when depth is later raised).
- Old files: no gate (depth 0). Demos may set a gate on a pad track.
- Synth RAM: `double gatePos_`, per strip ~16 B (g, inc up/down, cached target) x 17 = ~0.3 KB.

## UI (TRACK -> INS, INT only)

Rows after Downsample (row name "Gate" is taken by the step-length row on NOTE, so "Trance"):

| Row | Value |
| --- | --- |
| Trance | OFF / depth 1..127 (`gateDepth`) |
| Pattern | preset name or USER; turn = next preset (writes the mask) |
| Steps | 16 cells `x.x.x.x. x.x.x.x.` (drawn as boxes, 4-groups) |
| Rate | 1/32 1/16 1/8 |
| Length | 1..100 % |
| Attack / Release | ms |

- Steps editing reuses the name-edit model: click = edit; turn = cursor; tap on a cell = toggle;
  track buttons 1..8 toggle the cells of the cursor's half (1-8 / 9-16) while editing (TrackScreen
  gets `trackKey`, like SONG); A + turn = toggle and step the cursor; Shift + turn = rotate the mask.
  Click / B = done. Cancel = setEditScope on the 2 mask bytes.
- Playhead: the current gate step highlighted while playing (UI computes it from the engine status
  position and the rate; cosmetic, may be off for odd pattern lengths — or read gatePos_ from the
  synth status).
- Rows dim while Trance = OFF. Manual: one section under the insert.

## CPU

Per hot track with depth > 0: ~6 flops per sample (phase add, compare, ramp, multiply) = ~200 ops
per segment; 16 tracks gated ~3.5 k ops/ms — well under 1 % on the S3. Edge search per segment
(at most one edge per 32 samples) could make it branch-free if needed. Silent tracks cost nothing.
No per-voice cost. Profile under kProfStrip.

## Tests (native)

Gate off = bit-exact; mask / rate / depth produce expected gain at known positions (render with fixed
bpm); 0xFA restarts at step 1; 0xF6 5 re-anchors (drift correction, mid-segment offset exact);
continuous across a pass end of a 12-step pattern; pause holds; len 100 % merges adjacent steps, < 100
% dips; ramp never steps more than 1/attack-samples per sample (no clicks); depth lock / LFO (if
chosen) via newest voice; preview through 0xF7 gets the gate; sends carry the gated signal;
TINS round trip; older 3-byte record keeps gate defaults (mask 0x5555, depth 0); longer unknown
records skipped; sequencer emits 0xF6 5 with absolute indices across passes / song chaining.

## Open questions for the user

1. Phase reference:
   a) continuous since PLAY, bar-locked (pattern of odd length phases against it) — **recommended**
   b) restart every pattern pass (like PERF GATE today; 1/8 rate needs >= 32-step passes)
2. Swing / groove:
   a) straight in v1 — **recommended**
   b) follow the pattern's swing (new 0xF6 6 message; groove templates ignored)
   c) follow swing and groove shifts
3. Pattern source:
   a) a 16-step mask per track + preset table that writes into it (USER when edited) — **recommended**
   b) presets only (index, no editing)
   c) mask with a length 1..16 (polymetric gates)
4. Rates:
   a) 1/32, 1/16, 1/8 — **recommended**
   b) plus triplets 1/16T, 1/8T
5. Envelope controls:
   a) Length + Attack + Release (3 rows) — **recommended**
   b) one "Smooth" row (attack = release) + Length
   c) fixed 1 ms ramp, Length only
6. Per-step / modulation control:
   a) none in v1
   b) fx GTE: depth lock per step (newest voice, like DRV) — **recommended**
   c) b) plus LFO target "Gate" (depth)
7. When the transport is stopped:
   a) the gate keeps running at the tempo (audition while stopped) — **recommended**
   b) the gate is bypassed while stopped
8. Steps editor input:
   a) turn = cursor, tap / track buttons toggle, A + turn = toggle and advance — **recommended**
   b) turn = toggle at cursor, Shift + turn = cursor (exactly the name-edit model)

## Decisions (user, 2026-10-14)

All recommended options, except Q7: b) the gate is bypassed while the transport is stopped.
TINS record order: drive, bits, rate, driveModel (feature 2), then the gate fields. readTins assigns only
the fields present in the record (missing ones keep their defaults).
