# Virus arp patterns in FILL — design

Source: `docs/AudioXL Arpeggiator.amxd` (Max for Live device). Its embedded v8 script holds 64 arp
patterns captured from Access Virus B/Indigo OS 4.90 (`PATTERNS`, at the end of the script). Only the
patterns are taken; Mode / Octaves / Gate / Swing / Rate already exist in `ArpSpec`.

## Source format

Each pattern: cycle `l = 1536` ticks at 192 PPQ = 16 cells of 96 ticks (one cell = one Virus arp
step), events `[onset, gate ticks, velocity %]`. Velocity is continuous (10..129 %, 100 = normal,
~128 = accent), gate continuous. 55 of 64 patterns sit exactly on a 48-tick grid (32 steps); 9
(35, 37–42, 51, 56, 63, 64) have triplet / 1/64 events off it.

## Data

- `tools/virus_arp.py` reads the `.amxd`, extracts the last `PATTERNS = [...]` from the embedded
  script and writes `lib/core/src/arp_virus.inc` (64 `{"VIRUS NN", "..."}` entries). No `.amxd`
  parsing at runtime.
- Grid: one track step = 48 ticks (half a Virus cell), 32 steps per pattern. At Rate 1 with 1/16
  track steps this plays like the Virus at Arp Clock 1/8 (the pattern spans 2 bars). Faster is not
  possible: the Virus' half-cell events need their own step.
- An off-grid event goes to the nearest step, the remainder becomes a nudge in % of a step (±50).
  Two events in one step: the louder one stays.
- Gate: in % of a step (`gate / 48 * 100`). Notes longer than a step are a large GAT (up to 800 %),
  no TIE. Empty steps are `.`.

## Token format (backward compatible)

- `x:<vel%>:<gate%>[:<nudge>]`, e.g. `X:128:150`, `x:61:33:-17`. The fields follow the modifiers.
  Without fields a token behaves as before; the 42 existing factory patterns and user files are
  unchanged.
- `ArpStep` gains `uint8_t velPct` (0 = none), `uint16_t gatePct` (0 = none), `int8_t nudge`.
  `parseArpPattern` / `formatArpPattern` read and write them.

## applyArp

- Velocity: with `velPct`, `velHi * velPct / 128` clamped to 1..127 (Vel Hi is the maximum, like the
  device's Fixed Vel x accent; Vel Lo does not affect these steps). Otherwise as now.
- Gate: with `gatePct`, `gatePct * (Gate / 50) * rate` (Gate 50 % = the Virus original, as the
  device's `gateFactor`). Short / Long do not apply to such steps.
- Nudge: `NDG = nudge + swing / 2` (swing on odd arp steps as now), clamped to ±50.
- Mutate: when a mutation changes a step's accent, its exact velocity is dropped. Ghost PRB applies
  to steps with `velPct < 75`.

## UI / FILL

- The 64 patterns go at the end of `kFactory` (after CHIP RUN), named `VIRUS 01`..`VIRUS 64`. User
  pattern indices shift; `ArpSpec` is RAM only, nothing persisted breaks. Update the style list
  comment in `arp_gen.h`. Capture unchanged.
- Docs: README / manual (+ru): the Virus patterns and "Gate 50 = original".

## Tests (`test/test_arp`)

- Parse / format round trip of the new token; old tokens unchanged.
- `applyArp`: velPct / gatePct / nudge write vel / GAT / NDG; swing + nudge clamp.
- All 64 VIRUS patterns parse with `len == 32`; spot checks: VIRUS 01 (16 notes on every other
  step), a triplet pattern (NDG != 0).
- `pio test -e native`, then flash and check on the device; commit after the device check.
