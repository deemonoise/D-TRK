# Track insert chain — design

Feature 1 of the "sound next" list: an insert chain on each of the 16 INT tracks. The drive and the
bit / sample-rate crush move from the voice into it.

## Why

Teensy benches: anything computed per voice scales with the pool (drive on 32 voices ~ 20 % CPU),
anything per track or on the master is nearly free. New sound features go on the track or the master.

## Signal flow

Before (per voice): engine -> drive -> crush -> filter -> pan gains -> mix, sends and peak per voice.

After:

```
voice:  engine -> filter                          -> trk_[track] (mono sum)
track:  trk_ -> insert (drive -> crush) -> fader -> peak -> sends (mono) -> pan -> mix / sidechain
```

- `trk_[kSynthTracks][kControl]`: one control segment (32 samples) per track, 17 x 32 floats = 2.2 KB
  inside Synth (internal RAM). The chain runs per segment, in the existing segment loop.
- A track runs its strip in a segment only if one of its voices is allocated (`trkHot_` mask).
- Fixed chain order: drive -> crush. Later features append (trance gate after crush).
- Fader (TrackCfg::vol) moves after the insert: the track volume no longer changes how hard the
  drive is hit. It is folded into the pan gains and the send gains (ramped with the pan). Voice amp
  no longer contains it.
- The chain is mono and before the pan, so it maps onto the Teensy mixer (pan per track) unchanged.

## Track parameters (per control update)

`TrackStrip` per runtime track (17): pan ramp (gl, gr, steps, targets, panSet), send, rsend,
Drive, Crush.

- Base values: drive / bits / srr from TrackCfg (new fields), pan from TrackCfg::pan, sends from the
  track's instrument (`trackInstr`; on a KIT track the KIT itself: the whole kit shares one send).
- Modulation comes from the newest sounding voice of the track: its locks (DRV, BIT, SRR, PAN, DLY,
  RVB) and its LFO targets (Drive, Bit, Srr, Pan, Dly, Rvb). The voice computes these per control
  update as before (small byte fields), the strip reads the newest one. The lock mechanism (a step
  without a note locks the sounding voices) is unchanged.
- Refresh: at every control boundary after the voices' updates (ramp over kControl), and for tracks
  touched by an event mid-segment (ramp over the samples left to the boundary, as voices did).
- Pan snaps (no ramp) when a fresh note starts on a track with no other gated voice; then the crush
  state resets too. Otherwise pan / fader changes ramp.
- Preview track (INST / GRID preview): uses the insert settings of a pattern track named by a new
  message `0xF7 t` (t >= kTracks: no insert). Pan centre, fader kPreviewVol as before.
- Sidechain key (scTrack) and the MIX meters work per strip now.

## Model and files

- TrackCfg gains, at the end: `drive`, `crushBits`, `crushRate` (0..127, 0 = off).
- Instrument::drive / crushBits / crushRate stay in the struct (file layout, inst_codec byte 10..)
  but are not used by the synth or shown in the UI.
- New project chunk `TINS`: `count, recSize, count x recSize` — fields drive, bits, rate. Later
  features grow the record; readers take the fields they know, skip the rest.
- Migration on load: a file without TINS gets, per track, the instrument values of its instrument
  (a KIT: max over the KIT and its lane instruments). After every project load the instrument
  fields are zeroed. Instrument presets loaded from files zero them too.
- Factory presets drop their drive / crush; the demos set them on their tracks instead.

## UI

- TRACK: Drive, Bit crush, Downsample after Pan (INT only).
- INST: Drive, Bit crush, Downsample rows removed from the FILT page.
- Fx DRV / BIT / SRR and LFO targets keep their names; the manual says they act on the track.

## Sound changes (accepted)

- Drive / crush after the filter, on the sum: chords intermodulate, the filter no longer tames the
  distortion.
- One send / pan for the whole track (newest voice): no per-note autopan, a KIT shares one send.
- Multitrack output is no longer bit-exact to before (float summation order).

## CPU

Per voice: the stereo pan add, two sends, the peak scan, drive and crush are gone; a mono add into
the track buffer remains. Per track and segment: the strip. Measured with the pool bench (24 voices,
8 heavy, drive 100): before = drive on every instrument, after = drive on every track.

Measured 2026-10-14 on the ESP32-S3 (wt32-pool24, us per 4000 us block, avg; the CPU guard cuts heavy
steps to ~6 sounding voices in both builds):

| step               | before        | after         |
|--------------------|---------------|---------------|
| 0 heavy + 16 CHIP  | 2188 (54 %)   | 1747 (43 %)   |
| 8 heavy + 16       | 2329 (58 %)   | 2349 (58 %)   |
| 10 heavy + 14      | 2293 (57 %)   | 2231 (55 %)   |
| 12 heavy + 12      | 2224 (55 %)   | 2262 (56 %)   |

Stages at 8 heavy (us/block): fm 917 -> 766, drum 666 -> 535, mix 1031 -> 27, strip 0 -> 1322; the
four together 2614 -> 2650. At 16 CHIP: chip 1266 -> 657 (79 -> 41 us per voice), strip 211.
Result: heavy load unchanged within noise, light polyphony ~ -11 % CPU. The strip (drive on all 16
tracks) is the new big stage, a candidate for optimisation.

## Tests (native)

Strip drive / crush on the track sum; fader after the drive; pan ramps and snaps per track; locks
and LFO from the newest voice; KIT one send; sidechain and meters per track; preview insert message;
TINS round trip, unknown longer records, migration from instruments (KIT max), instrument fields
zeroed; factory presets without drive.

## Decisions after review (user, 2026-10-14)

- Migration of a KIT track (file without TINS): take the KIT instrument's own drive / crush only, not
  the max over its lanes (1b).
- Instrument presets carry a recommended insert (drive, bits, rate, drive model). Loading a preset onto
  a track copies it into the track insert, overwriting it (2b). Factory presets get their old drive /
  crush back as that recommendation.
- Implemented in 2026-10-14-preset-insert.md (KIT lane presets leave the KIT track's insert alone).
