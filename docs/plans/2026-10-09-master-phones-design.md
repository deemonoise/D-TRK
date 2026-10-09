# Master vs phones volume — design

## Goal
Split the single output level into two:
- **Master** (`Project::masterVol`, 0..200 %): project data, saved in the .mtp, applied in
  `Synth::render` (after the compressor, before the soft clip), so it reaches Render WAV.
- **Phones** (device setting, 0..100 %): live output only, kept in NVS, edited in PROJ / SYS.

## Master
- No longer loaded from / saved to NVS (`storage::loadVolume`, `App::savedVol_`,
  `App::saveVolumeIdle`, the override in `App::projectReplaced` go away).
- TRACK: turn and MAIN fader edit the project's master and mark the project dirty.

## Phones
- PROJ / SYS row "Phones", 0..100 % step 1, default 100 %.
- NVS key `phones`; not project data (no dirty mark); written once it has stayed put for a second
  and the transport is stopped (same rule the volume had).
- Attenuation only: `g = (v / 100)^2`, applied in `audio.cpp` to the int16 block before
  `i2s_channel_write` (covers the sample preview too). `renderWav` does not see it.

## Migration
Old NVS key `vol` is no longer read. Projects load with their own saved `masterVol`.

## Tests
Core (`lib/core`) unchanged: existing `masterVol` round trip in `test_project_io`. Native tests +
firmware build.
