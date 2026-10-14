# Sound: what next (2026-10-14)

Ideas list after the voice ceiling benches. Not a design yet: each item gets its own design / plan
before work starts.

## The budget (Teensy, see future-teensy.md)

- Per voice: stream ≈1 %, CHIP ≈1.5 %, FM ≈2.3 %, WT SYNTH ≈2.6 %, DRUM ≈2.9 % of the CPU.
- Delay + reverb + comp together ≈2.5 %: master / send effects are nearly free.
- Per-voice drive on 32 voices ≈20 % (16 stream + 16 WT: 50 % without effects, 70 % with).
- Worst case of the firmware's 32 / 16 pool with effects ≈70 %; the guard sheds at 80 %.
- Rule: new per-voice cost multiplies by 32, so put sound features on tracks or the master where
  they can live there; new heavy engines count against kFmVoiceMax (16).

## Workflow

Built on `main` first (the ESP synth: full hardware, screen, buttons), in `lib/core` with native tests,
then merged into `feature/teensy` right after each feature (do not let the branches drift).

- Insert runs on the track's mono signal before pan: one chain for the ESP (mono) and the Teensy
  (stereo + pan).
- New Instrument / TrackCfg / SynthModel fields at the end of the structs; after the merge run
  test_link_msg (the Teensy mirrors SynthModel) and bump kProtocol if the layout changed.
- Benchmark on the ESP before shipping (24 voices / 8 heavy, its own budget); on the Teensy after the
  merge: `link_desk.py bench-stream --heavy --wt --fx` (how much moving drive to the track returned).

## Do

In this order:

1. **Track insert chain** (16 chains instead of 32 voices). Takes over the drive; holds the items
   below. RAM: one block buffer per track (≈8 KB mono at 128 frames) — check on the ESP.
2. **Distortion models** in the insert drive: tanh (today), hard clip, fold, rectify, asymmetric.
   Same cost as today's tanh.
3. **Trance gate** in the insert: a tempo-synced step pattern on the track's volume. Nearly free.
4. **Envelope follower**: one track's level as a modulation source for another track's parameters
   (kick opens the bass filter). Control rate, nearly free. The comp sidechain (`scTrack`) covers
   ducking already; this is for other targets.
5. **Cheap master / send effects** (≈1 % each): chorus / ensemble, phaser, flanger; tape delay
   (wow / flutter, saturation in the feedback) and ping-pong; shimmer in the reverb; master EQ / tilt
   and a limiter.
6. **Braids** (MIT) as a new heavy engine, counted against kFmVoiceMax; models differ in cost, bench
   each.
7. **Resonator / Karplus-Strong** (string, modal, as Rings): a heavy engine next to Braids.
8. **SYNTH: wavefolder, ring mod; new filter types** (ladder, MS-20, comb, formant): per voice, bench
   the cost before shipping.

## Not doing

- Granular: no buffer without PSRAM (CS0), grains from SD-streamed samples jump around the file.
- Vocoder.
- Unison / supersaw and per-voice stereo: multiply the voice cost.
- Already there: 4 LFOs per instrument (Random = S&H, tempo sync, LFO-to-LFO targets), step
  probability, Euclidean rhythms (track fill, FillWhere::Euclid).
