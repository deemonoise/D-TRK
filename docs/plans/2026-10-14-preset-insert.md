# Preset insert recommendation + KIT migration (plan)

Follows the "Decisions after review" of 2026-10-14-track-insert-design.md (1b, 2b).

## Design

- **Storage.** A preset's recommended insert reuses `Instrument::drive / crushBits / crushRate`
  (FM record bytes 10, 14, 15, already in inst_codec) plus a new `Instrument::driveModel` at the END
  of the struct, stored in the INST record's reserved byte 47 (old files: 0 = TANH; unknown -> TANH).
  No preset version bump: v1..v5 files read byte 47 as 0. `sizeof(Instrument)` stays 380 (the byte
  fills the tail padding), so the storage.cpp static_asserts and snapshot() are unchanged.
- **Projects.** Instruments in a project carry no insert: `clearInsert()` after project load (as
  before, now including driveModel), after a preset load (`takePresetInsert`), on the KIT drums of
  `factoryBuildKit`, and in demos / templates (they set track inserts themselves).
- **Load** (preset browser, factory and user, also every audition): `applyPreset` then
  `takePresetInsert(p, instr, curTrack)` copies drive / bits / rate / model into the current track's
  insert, overwriting it, when that track is INT and plays the instrument; CANCEL restores the track
  insert from a backup taken on open. A factory KIT: `takePresetInsert` on the KIT slot (the KIT
  track takes the kit's own recommendation, 0 for the factory kits).
- **KIT lanes.** A preset loaded into a lane's instrument does not touch the KIT track's insert: the
  current track plays the KIT slot, not the lane instrument, so the recommendation is dropped. Chosen
  because a lane has no insert of its own and one lane must not repaint the whole kit's sound.
- **Save:** `presetInsertForSave(p, instr, curTrack, m)` stores the current track's insert (none when
  the track does not play the instrument), so save -> load round-trips.
- **Factory presets** get back their pre-insert drive / crush values (= HEAD); drive model TANH.
- **Migration 1b:** a project without TINS: every track takes its instrument's own drive / crush; a
  KIT track the KIT's own, not the max over its lanes.

## Steps (TDD)

1. test_project_io: KIT track migration expects the KIT's values -> migrateInsert assigns, no max.
2. test_preset_io: driveModel byte round-trip / clamp; takePresetInsert (playing track, other track,
   MIDI track, -1); presetInsertForSave + save/load/apply round-trip.
3. test_presets_factory: recommended values per preset; factoryBuildKit drums carry none.
4. test_demos / test_templates: built projects' instruments carry none.
5. Implement: model.h (driveModel, clearInsert), inst_codec, preset_io (takePresetInsert,
   presetInsertForSave), project_io, presets_factory, demos, templates; UI preset_browser.
6. manual_ru.md: presets section + the #insert migration note. `pio test -e native`, `pio run -e wt32`.
