# Envelope follower — design

Feature 4 of the "sound next" list: the level of one INT track modulates a parameter of another
(kick ducks the bass cutoff / volume, hats open the pad cutoff, a track wahs itself).

Builds on feature 1 (track insert, `TrackStrip`, `trk_[track][kControl]`, see
2026-10-14-track-insert-design.md).

## Why

- The only level-driven modulation today is the master compressor's sidechain (`scTrack`): it ducks
  the whole mix, one key, volume only. Pumping one track, or level -> filter, is not possible.
- After feature 1 every track already has its mono sum per 32-sample segment in internal RAM:
  measuring it is nearly free. The modulation must cost ~zero per voice (Teensy rule: per voice
  scales with the pool, per track is free).

## Existing

- `Synth::renderCore`: per segment (kControl = 32 samples = 1 ms at 32 kHz): at a boundary
  `advanceInstLfos`, `control(v, kControl)` for every sounding voice, then `refreshStrip` for every
  track from its newest voice; events split segments (strips of touched tracks refresh with a ramp
  to the boundary); then voices render into `trk_`, `runStrip` per hot track (insert -> sends ->
  peak if `meters_` -> pan into mix / sc bus).
- `control()`: 4 LFOs per instrument summed per target into `la[LfoDest]` (-1..1 x depth / 64,
  applied as `64 * la` parameter units; `Vol` multiplies `lfoVol`). Per-voice targets: Pitch, Fine,
  macros Dec..Con, Cutoff, Reso, Fenv, Duty, Sub, Noise, Semi2, LFO Rate/Depth/Rtrg. Strip targets
  (Drive, Bit, Srr, Pan, Dly, Rvb) are written into voice fields; the strip reads the newest voice.
- Filter cutoff / reso are per voice (`controlFilter`), so a cutoff target must go through
  `control()`, not the strip.
- Sidechain: `Project::scTrack` / `scDepth`; the key strip goes into `scL_/scR_`, the compressor
  (`synth_comp.h`) adds `max|sc| x scDepth/127 x 4` to its detector. Master only, PROJ screen.
- No mod matrix. Locks (`kLock*`) and LFO fx locks are per note.
- Meters: `trackPeak_` is scanned in `runStrip` only while MIX is shown.

## Approaches

**A. One follower per track, configured on the destination track (recommended).**
TrackCfg gets source track, target (an `LfoDest`), amount, attack, release. "This track listens to
the kick." 16 followers max, any source can feed several tracks, a track may follow itself.
Targets reuse the LFO list (`lfoDestStep` / `lfoDestName` for the track's instrument type), so
every LFO target works, per-voice and strip ones alike, with no new plumbing.

**B. N global slots (e.g. 4) in Project** (src track, dst track, target, amt, atk, rel). More
freedom (two targets on one track) but a new screen, and `control()` loops the slots per voice.
Fewer followers in total.

**C. A FOLLOW LFO wave** (instrument LFO, source in the rate field). Reuses the LFO UI and LFO->LFO,
but instruments are not tracks (one instrument on two tracks, presets carrying a track number), no
room for attack / release. Rejected.

Recommendation: A. Record format leaves room for a second follower per track later.

## Signal / timing

```
segment k:  voices -> trk_[t] -> insert -> [detector: peak |x| of source tracks] -> fader -> ...
boundary:   fol_[t].env <- one-pole(level of src over segment k, atk / rel)   (per dest track t)
            control(v): la[dest] += amt/64 * env        (voices of track t)
            refreshStrip(t): Vol target -> fader x (1 + amt/64 * env), clamped 0..2
```

- Detector: post-insert, pre-fader, pre-pan peak of the source's `trk_` per segment (sub-segments
  between events max-accumulated into `srcPk_[t]`). Computed for the tracks in a `srcMask` (sources
  of any follower), merged with the meter scan (one abs/max loop serves both). Pre-fader: a key track
  at fader 0 still drives (ghost kick). A muted track gets no notes (sequencer): no level.
- Level -> 0..1: dB scale, `clamp((20 log10(pk x 0.25) + 48) / 48, 0, 1)` (0.25 = master gain at
  100 %, as the meters): hats and kick both give useful values. One `log2f` per source per ms.
- Envelope per follower (dest side, so two tracks following the kick may have own times): attack
  coefficient for a rise, release for a fall, per segment, `envTimeMs` scale (atk 0 = instant,
  defaults atk 0, rel 70 ~ 150 ms).
- Timing: env updated at the boundary from the last segment, before `control()` -> one segment
  (1 ms) latency, independent of track order (no source-first scheduling needed). Mid-segment
  note-ons (`control(v, 0)`) read the current env: a new note starts at the follower's value.
- Amount bipolar -64..63: + opens / raises, - ducks. Same units as an LFO at that depth (64 = full
  range of the target), summed with the LFOs and locks, then clamped as today.
- Vol target: applied on the strip (fader factor), not on voice amp — the strip gains already ramp
  over the segment, so ducking does not zipper, and it scales the sends too (post-fader sends).
  Self-follow with Vol does not feed back (detector is pre-fader). Self-follow with Drive / Bit
  does (post-insert detector): allowed, bounded by clamps.
- Strip targets (Drive, Pan, Dly, Rvb, Bit, Srr): via the voice fields as LFOs do, no change.
- KIT tracks: per-lane voices; macro targets act on lanes whose type has them (as LFOs).
- Preview track: no follower (neither source nor destination).

## Model & file

- `TrackCfg`, at the end: `folSrc` (0 = off, 1..kTracks), `folDest` (LfoDest, default Cutoff),
  `folAmt` (int8 -64..63, default 0), `folAtk` (default 0), `folRel` (default 70).
- Synth runtime: `float folEnv_[kSynthTracks]`, `float srcPk_[kSynthTracks]`; getter
  `followerLevel(t)` for the UI bar (lock-free read, like peaks).
- New chunk `TFOL`: `count, recSize, count x recSize`, record = src, dest, amt, atk, rel. Readers
  take what they know, skip the rest (same rules as TINS); a second follower per track would
  append fields. Not merged into TINS: TINS grows with insert features (trance gate) in parallel.
- Files without TFOL: followers off. Invalid dest for the instrument type: kept, ignored by the
  synth (as LFO targets); src out of range or a MIDI track: off.

## UI

- TRACK: new page `FOL` after `INS` (INT only, greyed on MIDI): Source (OFF, 1..16 with track
  name), Target (LFO target list of the track's instrument, `lfoDestStep`), Amount (signed),
  Attack, Release. Header shows a small live bar of the follower envelope.
- Manual: FOL section; note the difference to the PROJ compressor sidechain (master duck vs
  per-track follower).

## CPU

- Per source track per segment: 32 abs/max (shared with the meters), one log2f, nothing else.
- Per follower per segment: one one-pole update.
- Per voice per control update: one branch (`fol_[track] on`) + one add into `la`. Voices of tracks
  without a follower: one predictable branch.
- Per strip: one multiply (Vol target).
- Expected < 0.1 % on ESP32-S3 with 16 followers; check with the pool bench + `kProfStrip`.

## Tests (native)

- Silent source: env 0, target unchanged (bit-exact to follower off).
- Kick source -> bass Cutoff -64: cutoff (voice `octKey`) drops on the kick, recovers with release.
- Vol target ducks the strip gains (ramped, no step inside a segment), sends ducked too.
- Attack / release times (env reaches ~63 % in the set time, segment resolution).
- One segment latency, same result whatever the source / dest track order.
- Ghost key: source fader 0 still drives; self-follow Vol has no feedback.
- Strip target (Drive, Pan) via the newest voice; KIT lanes; preview track ignored.
- TFOL round trip, longer unknown records skipped, missing chunk = off, bad src = off.

## Open questions for the user

1. How many followers?
   a) one per track, set on the destination track (recommended)
   b) two per track
   c) 4 global slots on a PROJ / MIX page
2. Level detector curve?
   a) dB (-48..0 dBFS -> 0..1): quiet hats still modulate fully (recommended)
   b) linear peak
   c) linear plus a Sens / gain parameter (6th field)
3. Where is the source measured?
   a) after the insert, before the fader: fader 0 key still works (recommended)
   b) after the fader: what you hear in the mix
4. Vol target?
   a) on the track strip, ramped, ducks sends too (recommended)
   b) on the voice amp like the LFO Vol (stepped per 1 ms, sends not ducked)
5. Step locks for the follower?
   a) not now (recommended)
   b) new fx FOL locking the amount per step
6. UI place?
   a) TRACK page FOL after INS (recommended)
   b) a separate MOD screen
7. May a track follow itself (auto-wah / self-duck)?
   a) yes (recommended)
   b) no, source list skips the own track

## Decisions (user, 2026-10-14)

All recommended options.
