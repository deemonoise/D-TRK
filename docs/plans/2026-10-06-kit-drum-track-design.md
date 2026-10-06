# KIT drum track — design

## Goal

A drum track: one track, eight lanes (kick, snare, hats…), an x-o-x grid instead of notes. Variant A of
the roadmap (block 2, after 16 tracks): a new instrument type **KIT** holds eight lanes; each lane is its
own mini sampler (a project sample, volume, pitch, decay) or, optionally, points at one of the 16
instruments. A track whose instrument is a KIT stores lane masks in its steps with no change to the
`Step` layout, so copy / paste / clear / selection / undo work unchanged. On MIDI tracks the lanes go
out as fixed notes (external drum machines); on INT tracks each lane plays mono with choke.

## Model

- `InstrType::Kit` appended before `Count`; `kTypeOrder` (UI order) gets Kit last.
- `constexpr int kKitLanes = 8;`

```cpp
// A KIT lane. instr == kNoInstr: a mini sampler on the project sample `sample` (empty / missing name =
// silent) played at its original pitch (root = note) + pitch semitones, vol, decay (0 = whole sample,
// else envTimeMs(decay) then silence). instr < kInstruments: the lane plays that instrument at `note`.
struct KitLane {
  char sample[kSampleNameMax + 1] = {0};
  uint8_t instr = kNoInstr;  // 0xFF
  uint8_t vol = 100;         // 0..127
  int8_t pitch = 0;          // -24..24 semitones
  uint8_t decay = 0;         // envTimeMs, 0 = play to the end
  uint8_t note = 60;         // MIDI note of the lane: unique in the kit, sent on MIDI tracks
};
```

  `constexpr uint8_t kNoInstr = 0xFF;` moves to `model.h` (it is `Synth::kNoInstr` today; keep the alias).
  `Instrument::kit[kKitLanes]` (8 x 22 = 176 bytes; `Instrument` grows from ~150 to ~330 bytes, x16 ≈ 5
  KB in the project — fine). `kitSetDefaults(m)`: every lane `KitLane()` with `note = 60 + k`. A KIT does
  not sound by itself: its Vol / ADSR / filter / LFO are unused; its `send` (and the reverb send once
  block 4 adds one) is the whole kit's delay send.
- `Project::trackIsDrum(t)`: `instruments[tracks[t].instr].type == Kit`, any `TrackOut`; `kitOf(t)`
  returns that instrument or `nullptr`.
- Drum-track step encoding, `Step` unchanged:
  - `note`: `kNoteEmpty` = empty step, `kNoteOff` = note off (releases every lane), else 0..127 = the
    step's velocity for all its lanes (0 = the track's `defVel`). `hasNote()` is still `note < 128`.
  - `vel`: 8-bit lane mask, bit k = lane k+1 hits. A note step with mask 0 plays nothing (fx still run).
  - A melodic track's steps never carry bit 7 in `vel`. Changing a track's instrument between KIT and
    non-KIT reinterprets its steps; accepted (the manual says so).
- `Fx::ACC` ("ACCENT") appended before `Count`, value 0..255 = lane mask: lanes in the mask play at
  the step velocity, the others at 60 %. Drum tracks only: `fxDrumOnly(Fx)`; elsewhere the slot is kept
  but ignored (drawn dim, like synth fx on MIDI tracks). `fxFormat` prints two hex digits.
- Every other fx applies to the step as a whole, i.e. to all its lanes' notes (RAT ratchets every lane,
  NDG nudges them, DEC / FLT / DLY lock every lane's voice, CND / PRB gate the step). CHD, NRN, STR, TIE,
  ARP, SLD, VIB, SLC are meaningless on a drum track and ignored by `expandStep` there.
- Lane note uniqueness: the UI step functions skip notes other lanes hold. Assigning a SAMPLE-type
  instrument to a lane sets the lane's `note` to that instrument's `root` (the sample keeps its pitch; if
  taken, the next free note up); other types leave `note` alone.
- Project sample rename (`projSampleRename`) renames the name in every KIT lane too, as it does in
  instruments' `sample`. Sample delete leaves the lane name (silent, like a MISSING instrument sample).

## File format

- `KITS` chunk: count byte, then 16 records of `kKitRecSize = kKitLanes * 22 = 176` bytes: per lane
  `sample[17]`, `instr`, `vol`, `pitch`, `decay`, `note`. Reader clamps: `instr` ≥ 16 → `kNoInstr`, `vol`
  → 0..127, `pitch` → -24..24, `note` ≥ 128 → 60 + k, the name NUL-terminated and validated like
  instrument sample names (`fixInstrument`). Older files: no chunk → `Instrument()` defaults (no
  instrument is Kit in them). Written after `SYNI`.
- `INST`: `type` byte already stored; `inst_codec` accepts the new value (`< InstrType::Count`).
- `PATN`: `readPatn` keeps the `vel` byte as is (no 0..127 clamp). Where the CRC chunk ends the file
  (after `fixInstrument`), `loadProject` masks `vel &= 0x7F` on every step of every non-drum track: the
  track's type is known only after `INST` / `TRKS` / `KITS`. Writer unchanged.
- Presets: no KIT presets in this block. `preset_browser` skips the Kit type when cycling types; Save
  on a KIT instrument toasts `NO KIT PRESETS`; `inst_codec` never packs a Kit.

## Expansion / sequencer

- `ExpandCtx` gets `const Instrument* kit = nullptr` (the track's KIT, or null). `Sequencer::expand`
  fills it from `p_.kitOf(track)`.
- `expandStep`, after the control events, on `kit != nullptr`: skips the melodic path. Lanes =
  `s.vel`; accent mask = `ACC` slot value or 0xFF. Base velocity = `s.note ? s.note : t.defVel`, plus
  VRN, clamped 1..127. For every set lane k: a NoteOn with `note = kit->kit[k].note`, `vel = accent ?
  base : base * 3 / 5` (min 1) at `nudge + sub * i` per ratchet i, followed by its NoteOff after
  `gateUs` (drums ignore it; MIDI drum machines want it). Lanes with no source still go out on MIDI
  (the synth is what is silent). TIE is ignored (`out.tie = false`). OFF works as on melodic tracks.
- `kMaxStepEvents` = 8 ratchets x 8 lanes x 2 + kFxSlots = 134 (was 72 + kFxSlots); `ExpandOut` grows by
  ~370 bytes (static in the sequencer, fine).
- MIDI tracks: the NoteOns go out on the track's channel (or CHN) — a GM drum map is lane notes 36, 38,
  42, 46, … set per lane in INST.

## Synth

- A voice refers to its instrument by index (`Voice::instr`, read as `p_.instruments[v.instr]` in
  `noteOn` and `control`). A sampler lane has no instrument of its own, so `Voice` gets a scratch
  `Instrument laneInst` and a flag `lane` (true = use `laneInst`); `Synth::instrOf(const Voice&)` returns
  `v.lane ? v.laneInst : p_.instruments[v.instr]` and replaces the two lookups. 16 voices x ~330 bytes
  ≈ 5.3 KB more internal RAM for `Synth` (it already holds the 16 voices; check the heap log).
- `Synth::noteOn(track, note, vel)`: `ii = trackInstr(track)`; if `instruments[ii].type == Kit`: find
  lane k with `kit[k].note == note` (first match); none → return. Then:
  - INST mode (`kit[k].instr < kInstruments`): `ii = kit[k].instr`, `m = instruments[ii]`; a Kit there
    → return (no nesting). Heavy (FM / DRUM) lanes count against `kFmVoiceMax` as usual.
  - SAMPLE mode: `laneInst = Instrument()` with `type = Sample`, `sample = lane.sample`, `root =
    lane.note`, `transpose = lane.pitch`, `start 0`, `end 0xFFFF`, `loop Off`, `sliceCount 0`, `reverse
    false`, `attack 0`, `decay = lane.decay ? lane.decay : 0`, `sustain = lane.decay ? 0 : 127`,
    `release` = the value whose `envTimeMs` ≈ 10 ms, `fltMode Off`, `lfoDepth 0`, `vol = lane.vol`,
    `send = kit.send`, `mono = true`. Built in a local, then the chosen voice's `laneInst` is copied from
    it and `v.lane = true`, `v.instr = ii` (the KIT's index, for the lock / p-lock lookups that key on
    the track). The sample is looked up by name in the bank as for a SAMPLE instrument; not found (empty
    or missing) → return (silent lane). Live lane edits apply on the next hit.
  - Voice choice (both modes): the lane is mono with choke regardless of `m.mono`: a sounding voice
    with `x.track == track && x.note == note` is reused (`legato = true`; the drum / FM / DRUM choke
    paths retrigger click-free; for sample / CHIP lanes `overlap` is forced false so the sample
    restarts), else `allocVoice(voices_, track, false, …, heavy, kKitLanes)` — a new last parameter
    `polyMax` (default `kPolyPerTrack`) so eight lanes can sound on one track. Sampler lanes count as
    SAMPLE voices (no heavy cap). The "a sample chokes the track's earlier samples" rule is skipped for
    lanes (they are independent).
  - `v.note = note` (the lane note) so note-offs and the next hit find the voice.
- `noteOff(track, note)` and `releaseTrack` work unchanged. Voices started from a lane keep playing
  from `laneInst` even if the lane is edited meanwhile.
- Preview (`audio::preview(instr, kPreviewNote = 60)`) on a KIT: the lane whose note is 60 — lane 1 by
  default. GRID lane toggles preview the toggled lane's note.

## GRID

- `GridScreen` helpers `drumAt(tr)` / `drum()` (= `trackIsDrum`), `padShown()`.
- Overview cell of a drum track (56 px column): no note name; 8 squares of 5 x 5 px at `x + 2 + k * 6`,
  `y + 5`, filled `kText` when bit k is set (dim when the track is not audible), outlined `kDim`
  otherwise; no velocity bar on drum tracks; `kNoteOff` draws `OFF` as now; the fx dot moves to `y + 1`.
- Detail view, drum track: the NOTE field draws the squares the same way with a `kEditCursor` frame on
  `lane_` while editing; the VEL field shows the step velocity from `note` (`...` for 0) and editing VEL
  edits `note` 1..127 (Shift x10); encoder turn on NOTE moves `lane_` 0..7, Shift + turn toggles the
  lane under the cursor.
- Lane pad replaces the mini keyboard on drum tracks (`edit_ && curField_ == kNote && drum()`): 8
  buttons `kScreenW / 8 = 60` px wide, `kKbH` high, label = first 4 chars of the lane's sample name
  (SAMPLE mode) or instrument name (INST mode), `-` when the lane has no sample; lit (`kPlayBg`) when the
  step has the lane. Tap toggles the lane: `vel ^= 1 << k`; an empty step gets `note = 0`; a mask that
  becomes 0 stays a note step (Clear step removes it). Writes through `writeStep` (undo), previews.
- Track buttons 1–8 in edit mode on a drum track toggle lanes 1–8 (no `enterDegree`); the cursor does
  not advance. Outside edit: select / mute as now.
- "Note OFF" menu items write `kNoteOff` as now. Copy / Paste between drum and melodic tracks is
  byte-wise and allowed (the manual warns).
- `editTurn` on NOTE for an empty drum step: creates `note = 0`, `vel = 1 << lane_`.

## INST

- Type list: `… / KIT` (last). KIT pages: MAIN (Name, Type, Send) and one scrolling LANES list
  (`ParamList` scrolls with `setVisibleRows(kListRows)`): per lane a header-like row `L<k> Src`
  (`SAMPLE` / `INST`), then in SAMPLE mode `Sample` (project sample picker — the same `Sample` row the
  SAMPLE type uses, with its `warn` for a missing name), `Vol`, `Pitch`, `Decay`, `Note`; in INST mode
  `Instr` (INS1..16, red when it is a KIT), `Note`. Rows of the other mode are hidden by rebuilding the
  list on a `Src` change (the `Param` array holds every row; a `dim`/skip set rebuilds via
  `syncParams`-like refresh). ENV, FILT, LFO pages are not shown for KIT.
- Choosing a SAMPLE-type instrument in `Instr` sets the lane's `Note` to that instrument's `root` (or
  the next free note up).
- PRESET button on a KIT: toast `NO KIT PRESETS`, the browser does not open; in the Load browser the
  type arrows skip KIT.

## Euclid / Transpose / Import

- Euclid dialog on a drum track: a `Lane` row (1–8) replaces Fill / Range / Seed; `applyEuclid` with
  `lane >= 0` writes hits as `vel |= 1 << lane`, `note = 0` when the step was empty (replace mode first
  clears the lane bit on every step of the track, not the other lanes); accents set `note = accentVel`
  only when `note` is still 0. `EuclidParams::lane` (int8, -1 = melodic).
- Transpose (dialog and menu): drum tracks are skipped; a selection spanning both kinds transposes
  the melodic tracks only; a drum-only selection or track toasts `DRUM TRACK` and does nothing.
- MIDI import onto a drum track: a note maps to the lane with that `note`; no lane → dropped (counted
  in `notesDropped`); several notes on one step OR into the mask; the step velocity is the first note's.
  The import dialog marks drum targets `T5*`.

## Tests (native)

- `test_model`: Kit defaults from `kitSetDefaults` (all lanes SAMPLE mode, empty name, vol 100, notes
  60..67); `trackIsDrum` / `kitOf`; `kTypeOrder` covers Kit; `sizeof(KitLane) == 22`.
- `test_project_io`: `KITS` round trip with a SAMPLE-mode lane (name, vol, pitch, decay) and an
  INST-mode lane; an older file (no `KITS`) loads defaults; garbage `instr` / `pitch` / `note` clamped;
  a drum track's `vel` 0xA5 survives the load, a melodic track's becomes 0x25.
- `test_sample_set`: `projSampleRename` renames the name in KIT lanes as well as instruments.
- `test_fx_info`: `ACC` name, range 0..255, hex format, `fxDrumOnly`.
- `test_expand`: mask → NoteOns with lane notes (every set lane, source or not), default / step
  velocity, `ACC` 60 %, RAT x 8 lanes = 128 events ≤ `kMaxStepEvents`, TIE ignored, OFF cuts.
- `test_synth`: a SAMPLE-mode lane builds the expected scratch instrument (`root = note`, `transpose =
  pitch`, `vol`, decay 0 → sustain 127, decay n → decay n / sustain 0, `send` = the kit's) and plays
  from the bank; a lane whose sample is missing is silent; an INST-mode lane plays the referenced
  instrument (voice `instr` = it); a second hit on a lane reuses its voice (choke); 8 lanes sound at once
  on one track; an unknown note is silent.
- `test_euclid`: lane writes only its bit; replace clears only its bit.
- `test_edit`: copy / paste of a drum step is byte-exact (`vel` 0xA5 kept); transpose skips drum tracks.
- `test_import`: notes → lanes by lane note, unknown note dropped.
