# Envelope follower — plan

Design: `2026-10-14-envelope-follower-design.md` (all recommended options). TDD per task. No commits.

Adapted to the current code (insert strip with drive model and trance gate, TINS 11 B, TrackCfg 34 B):

1. **Model**: TrackCfg (end) `folSrc = 0` (0 off, 1..kTracks), `folDest = Cutoff`, `int8_t folAmt = 0`,
   `folAtk = 0`, `folRel = 70`; `folDestStep(dest, d, type)` = lfoDestStep without L1..L4 RTRG
   (an edge, not a level); `folLevel(pk)` (dB -48..0 of pk x 0.25 -> 0..1); `folCoef(v)` (one-pole per
   1 ms segment, time constant envTimeMs, 0 = instant). storage.cpp static_asserts. Tests: test_model.
2. **Synth**: detector in `runStrip` after the gate (post-insert, pre-fader), max into `srcPk_` for
   tracks in the sources mask; at each control boundary, before `control()`: levels of the sources,
   env per destination track (atk / rel), `folLa_[t]` (la index, -1 none) / `folMod_[t]` (amt/64 x env).
   `control()`: one branch per voice — RATE / DEPTH targets go into the LFO mods, the rest `la += mod`;
   Vol target is the strip's: `refreshStrip` fader x clamp(1 + mod, 0, 2) (ramped, sends too).
   `followerLevel(t)` for the UI. Preview track never. Tests: test_synth.
3. **File**: chunk `TFOL` (count, recSize 5, src, dest, amt, atk, rel), written after TINS; reader takes
   the known prefix, skips the rest; src > kTracks -> off, dest >= Count kept as Cutoff. Missing = off.
   Tests: test_project_io.
4. **UI**: TRACK page `FOL` after INS (INT only): Source (OFF / n name), Target, Amount, Attack, Release;
   live bar of `audio::followerLevel(t)`.
5. **Manual**: FOL rows + section with examples; difference to the PROJ sidechain.
6. Verify: `pio test -e native`, `pio run -e wt32`, `pio run -e wt32-pool24`.
