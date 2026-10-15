# Voice pool by cost (2026-10-15)

## Problem

The pool counts voices: up to `kVoices` (32 on the Teensy) and up to `kFmVoiceMax` (16) heavy ones.
Voices differ in price by 3× and more (CHIP ≈1.2 % of the CPU, WT SYNTH with fold + ring + LADDER
≈3.2 %), so the worst case of the full pool (≈84 % after plan A) is left to the CPU guard
(`Synth::setLoad`): it reacts after the load is already over 80 % and fades a voice.

## Decision

Each voice gets a **cost** at note-on, in hundredths of a percent of the CPU on the Teensy. The pool
keeps the sum of the costs of live voices (on, not stolen) under a **budget**. A new note that does not
fit steals by the existing rules: the oldest voice, releasing first, fades out (`kStealMs`, `stolen`),
as many as needed.

- The heavy limit (`kFmVoiceMax`) and the CPU guard stay as they are; the guard is the backstop.
- The cost is fixed at note-on: LFOs that move fold / filter later do not change it.
- Budget: default "unlimited" (native, ESP: unchanged behaviour); the Teensy firmware sets
  `kVoiceBudgetTeensy` ≈ 7000 (70 % for voices + ≈5 % fx ≈ 75 %, under the guard's 80 %).
- Tests set the budget with a setter.

## Cost table

One header (`voice_cost.h`), constants from the Teensy benches of 2026-10-14/15, a pure function
`voiceCost(const Instrument&, kit lane / sample flags)`:

- Base by engine: SAMPLE (stream) ≈100, CHIP 119, FM ≈184, DRUM by bench, WT SYNTH 232, MACRO by model
  (SAW+ 148 … MORPH 233), STRING added by plan C.
- SYNTH add-ons, only when on: filter LADDER +35, MS20 +23, COMB +34, FORMANT +40; fold +39; ring +9.
- Exact numbers are taken from future-teensy.md / the bench logs while implementing.

## allocVoice

New arguments `cost`, `budget`. While scanning it also sums `used` (live voices). The voice the note
reuses (mono legato, poly-per-track steal, the picked victim) gives its cost back. If
`used - reused + cost > budget`, steal the oldest live voices (releasing first, never the picked one)
with `env.fade(kStealMs)` / `stolen = true` until it fits or nothing is left; the note always plays.
`Voice::cost` stores the cost for the sum.

## Tests

- Unlimited budget: allocation identical to today (existing tests unchanged).
- Expensive notes past the budget steal the oldest, releasing first; cheap notes under the budget steal
  nothing.
- Mono legato and the per-track poly steal do not double-count.
- `voiceCost`: add-ons only when on; MACRO by model.
- Bench on the Teensy (bench-max group with the plan A worst case): ≤75 % without the guard shedding.
