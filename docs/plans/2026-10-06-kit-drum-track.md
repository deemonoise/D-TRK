# KIT Drum Track Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** A KIT instrument type holds eight lanes — each a mini sampler on a project sample, or a reference to one of the 16 instruments; a track using a KIT becomes a drum track whose steps hold lane masks, drawn and edited as an x-o-x grid, played as lane notes on MIDI and as per-lane mono voices on INT.

**Architecture:** `Step` is untouched: on a drum track `vel` is the lane mask and `note` the step velocity. `expandStep` gets the track's KIT through `ExpandCtx` and emits one NoteOn per lane; `Synth::noteOn` resolves a KIT's lane note to the lane, builds a scratch `Instrument` in the voice for sampler lanes (or redirects to the referenced instrument), and keeps each lane mono with choke. The GRID draws masks as squares and replaces the mini keyboard with a lane pad. A `KITS` chunk stores the lanes.

**Tech Stack:** C++17, PlatformIO (`pio test -e native`, `pio run -e wt32`), Unity. Design: `docs/plans/2026-10-06-kit-drum-track-design.md`. Requires block 1 (`docs/plans/2026-10-06-16-tracks.md`): `kTracks = 16`, `GridScreen::firstTrack()`, `App::trackKey` maps the 8 buttons onto the visible half.

**Rules for this project:** no commits (the user commits); do not stop for hardware checks — finish every task, then summarize. Do not touch `lib/core/src/synth_syn*`, `wt_*`, the SYNTH rows of `inst_screen.cpp` (another agent's work); if a file you edit has their uncommitted changes, keep both.

---

### Task 1: Model — `InstrType::Kit`, `KitLane`, `trackIsDrum`

**Files:**
- Modify: `lib/core/src/model.h` (`InstrType`, new `KitLane`, `Instrument::kit`, `Project`), `lib/core/src/model.cpp` (`kTypeOrder`, `instrSetType`, new `kitSetDefaults`), `lib/core/src/synth.h:98` (`kNoInstr` → alias of `mt::kNoInstr`)
- Test: `test/test_model/test_main.cpp`

**Step 1: Failing test**

```cpp
void test_kit_defaults_and_drum_track() {
  Project p;
  Instrument& k = p.instruments[3];
  instrSetType(k, InstrType::Kit);
  TEST_ASSERT_EQUAL(22, sizeof(KitLane));
  for (int i = 0; i < kKitLanes; ++i) {
    TEST_ASSERT_EQUAL(kNoInstr, k.kit[i].instr);
    TEST_ASSERT_EQUAL_STRING("", k.kit[i].sample);
    TEST_ASSERT_EQUAL(100, k.kit[i].vol);
    TEST_ASSERT_EQUAL(0, k.kit[i].pitch);
    TEST_ASSERT_EQUAL(0, k.kit[i].decay);
    TEST_ASSERT_EQUAL(60 + i, k.kit[i].note);
  }
  TEST_ASSERT_FALSE(p.trackIsDrum(0));
  p.tracks[0].instr = 3;
  TEST_ASSERT_TRUE(p.trackIsDrum(0));
  TEST_ASSERT_EQUAL_PTR(&k, p.kitOf(0));
  TEST_ASSERT_NULL(p.kitOf(1));
  TEST_ASSERT_EQUAL(static_cast<int>(InstrType::Kit),
                    static_cast<int>(instrTypeAt(static_cast<int>(InstrType::Count) - 1)));
}
```

**Step 2:** `pio test -e native -f test_model` → FAIL (no `Kit`).

**Step 3: Implement** — `model.h`:

```cpp
enum class InstrType : uint8_t { Chip, Sample, Fm, Drum, Synth, Kit, Count };
constexpr int kKitLanes = 8;
constexpr uint8_t kNoInstr = 0xFF;

// A KIT lane. instr == kNoInstr: a mini sampler on the project sample `sample` (empty / missing =
// silent), played at its own pitch (root = note) + pitch semitones, vol, decay (0 = whole sample, else
// envTimeMs(decay) then silence). instr < kInstruments: the lane plays that instrument at `note`.
// note is the lane's MIDI note: unique in the kit, sent on MIDI tracks. Stored in files (KITS).
struct KitLane {
  char sample[kSampleNameMax + 1] = {0};
  uint8_t instr = kNoInstr;
  uint8_t vol = 100;
  int8_t pitch = 0;   // -24..24
  uint8_t decay = 0;
  uint8_t note = 60;
};
static_assert(sizeof(KitLane) == kSampleNameMax + 6, "KitLane layout (file format)");
```

In `Instrument`, after the SYNTH fields: `KitLane kit[kKitLanes];  // KIT: the lanes (see kitSetDefaults)`. After `instrSetType`: `void kitSetDefaults(Instrument& m);  // KIT: every lane a silent sampler, notes 60..67`. In `Project`:

```cpp
  // A drum track: its instrument is a KIT (steps hold lane masks, see Step). Any Out.
  bool trackIsDrum(int t) const { return kitOf(t) != nullptr; }
  const Instrument* kitOf(int t) const {
    if (t < 0 || t >= kTracks) return nullptr;
    const Instrument& m = instruments[tracks[t].instr % kInstruments];
    return m.type == InstrType::Kit ? &m : nullptr;
  }
```

Document the drum-track step encoding above `struct Step` (design, "Model"). `model.cpp`: append `InstrType::Kit` to `kTypeOrder`; `instrSetType`: `else if (t == InstrType::Kit) kitSetDefaults(m);`

```cpp
void kitSetDefaults(Instrument& m) {
  for (int k = 0; k < kKitLanes; ++k) {
    m.kit[k] = KitLane();
    m.kit[k].note = static_cast<uint8_t>(60 + k);
  }
}
```

`synth.h:98`: `static constexpr uint8_t kNoInstr = mt::kNoInstr;`.

**Step 4:** `pio test -e native -f test_model` → PASS; `pio test -e native` — note failures (type enumerations in presets, Task 10).

---

### Task 2: `Fx::ACC` and `fxDrumOnly`

**Files:** `lib/core/src/model.h:170-173`, `lib/core/src/fx_info.h`, `lib/core/src/fx_info.cpp`; test `test/test_fx_info/test_main.cpp`

**Step 1: Failing test**

```cpp
void test_acc_lane_mask() {
  TEST_ASSERT_EQUAL_STRING("ACC", fxName(Fx::ACC));
  TEST_ASSERT_EQUAL_STRING("ACCENT", fxLongName(Fx::ACC));
  TEST_ASSERT_EQUAL(0xFF, fxDefault(Fx::ACC));
  TEST_ASSERT_EQUAL(0xFF, fxStep(Fx::ACC, 0xFE, 5));
  TEST_ASSERT_EQUAL(0, fxStep(Fx::ACC, 1, -5));
  char o[5];
  fxFormat(Fx::ACC, 0xA5, o);
  TEST_ASSERT_EQUAL_STRING(" A5", o);
  TEST_ASSERT_TRUE(fxDrumOnly(Fx::ACC));
  TEST_ASSERT_FALSE(fxDrumOnly(Fx::DLY));
  TEST_ASSERT_FALSE(fxSynthOnly(Fx::ACC));
}
```

**Step 2:** `pio test -e native -f test_fx_info` → FAIL.

**Step 3:** `model.h`: `… OFF, DLY, ACC, Count` + comment `// ACC: drum tracks, lane mask: lanes in it play at the step velocity, the others at 60 % (fxDrumOnly).` `fx_info.cpp` `kInfo`: `{"ACC", "ACCENT", 0, 255, 0xFF, false},`; `fxFormat`: `case Fx::ACC:` joins the `VIB` / `ARP` hex case; `fx_info.h/.cpp`: `bool fxDrumOnly(Fx f) { return f == Fx::ACC; }`.

**Step 4:** `pio test -e native -f test_fx_info` → PASS.

---

### Task 3: File format — `KITS` chunk, raw `vel` on drum tracks

**Files:** `lib/core/src/project_io.cpp` (`kMaxRec`, `kKitRecSize`, `readKits`, writer after `SYNI`, `readPatn:261`, `loadProject` end); test `test/test_project_io/test_main.cpp`

**Step 1: Failing tests** (save / load helpers as in `test_round_trip_full_project`)

```cpp
void test_kits_round_trip_and_defaults() {
  a.reset();
  Instrument& k = a.instruments[2];
  instrSetType(k, InstrType::Kit);
  strcpy(k.kit[0].sample, "kick");
  k.kit[0].vol = 90; k.kit[0].pitch = -3; k.kit[0].decay = 40;
  k.kit[5].instr = 7; k.kit[5].note = 100;
  ... save a → load into b ...
  TEST_ASSERT_EQUAL(static_cast<int>(InstrType::Kit), static_cast<int>(b.instruments[2].type));
  TEST_ASSERT_EQUAL_STRING("kick", b.instruments[2].kit[0].sample);
  TEST_ASSERT_EQUAL(90, b.instruments[2].kit[0].vol);
  TEST_ASSERT_EQUAL(-3, b.instruments[2].kit[0].pitch);
  TEST_ASSERT_EQUAL(40, b.instruments[2].kit[0].decay);
  TEST_ASSERT_EQUAL(kNoInstr, b.instruments[2].kit[0].instr);
  TEST_ASSERT_EQUAL(7, b.instruments[2].kit[5].instr);
  TEST_ASSERT_EQUAL(100, b.instruments[2].kit[5].note);
  std::vector<uint8_t> f = fileHeader();  // no KITS: defaults
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(kNoInstr, b.instruments[0].kit[0].instr);
  TEST_ASSERT_EQUAL(67, b.instruments[0].kit[7].note);
}

void test_kits_garbage_clamped() {
  std::vector<uint8_t> rec(16 * 176, 0);
  rec[17] = 40;   // lane 0 instr -> kNoInstr
  rec[18] = 200;  // vol -> 127
  rec[19] = 100;  // pitch -> 24
  rec[21] = 200;  // note -> 60
  std::vector<uint8_t> body = {16};
  body.insert(body.end(), rec.begin(), rec.end());
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "KITS", body);
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(kNoInstr, b.instruments[0].kit[0].instr);
  TEST_ASSERT_EQUAL(127, b.instruments[0].kit[0].vol);
  TEST_ASSERT_EQUAL(24, b.instruments[0].kit[0].pitch);
  TEST_ASSERT_EQUAL(60, b.instruments[0].kit[0].note);
}

void test_patn_vel_mask_only_on_drum_tracks() {
  a.reset();
  instrSetType(a.instruments[2], InstrType::Kit);
  a.tracks[1].instr = 2;
  a.patterns[0].steps[1][3].note = 100; a.patterns[0].steps[1][3].vel = 0xA5;
  a.patterns[0].steps[0][3].note = 60;  a.patterns[0].steps[0][3].vel = 0xA5;
  ... save a → load into b ...
  TEST_ASSERT_EQUAL_HEX8(0xA5, b.patterns[0].steps[1][3].vel);
  TEST_ASSERT_EQUAL_HEX8(0x25, b.patterns[0].steps[0][3].vel);
}
```

**Step 2:** `pio test -e native -f test_project_io` → FAIL.

**Step 3: Implement**

```cpp
constexpr size_t kKitLaneSize = sizeof(KitLane);               // 22: name 17, instr, vol, pitch, decay, note
constexpr size_t kKitRecSize = kKitLanes * kKitLaneSize;        // 176
```

Fold `kKitRecSize` into `kMaxRec`. Reader (the sample name copied / validated like `readInst` does for `Instrument::sample`):

```cpp
LoadErr readKits(CrcSource& in, uint32_t size, Project& p) {
  return readRecords(in, size, kKitRecSize, kInstruments, [&](int i, const uint8_t* b) {
    for (int k = 0; k < kKitLanes; ++k, b += kKitLaneSize) {
      KitLane& l = p.instruments[i].kit[k];
      memcpy(l.sample, b, kSampleNameMax);
      l.sample[kSampleNameMax] = 0;
      l.instr = b[17] < kInstruments ? b[17] : kNoInstr;
      l.vol = clampu(b[18], 0, 127);
      const int pt = static_cast<int8_t>(b[19]);
      l.pitch = static_cast<int8_t>(pt < -24 ? -24 : (pt > 24 ? 24 : pt));
      l.decay = b[20];
      l.note = b[21] < 128 ? b[21] : static_cast<uint8_t>(60 + k);
    }
  });
}
```

Dispatch `"KITS"`. Writer after the `SYNI` block:

```cpp
  if (!o.chunk("KITS", 1 + kInstruments * kKitRecSize) || !o.write(&count, 1)) return false;
  for (const Instrument& m : p.instruments)
    for (const KitLane& l : m.kit) {
      uint8_t b[kKitLaneSize] = {0};
      memcpy(b, l.sample, strnlen(l.sample, kSampleNameMax));
      b[17] = l.instr; b[18] = l.vol; b[19] = static_cast<uint8_t>(l.pitch); b[20] = l.decay; b[21] = l.note;
      if (!o.write(b, sizeof(b))) return false;
    }
```

`readPatn:261`: `st.vel = b[1];  // 0..127, or a lane mask on a drum track: masked in loadProject`. `loadProject`, after `fixInstrument` at the CRC chunk:

```cpp
      // Drum tracks are known only now: their steps keep vel as a lane mask, the others' are 0..127.
      for (int t = 0; t < kTracks; ++t) {
        if (out.trackIsDrum(t)) continue;
        for (Pattern& pt : out.patterns)
          for (Step& st : pt.steps[t]) st.vel &= 0x7F;
      }
```

**Step 4:** `pio test -e native -f test_project_io` → PASS (`test_garbage_values_clamped` may expect `vel` clamping: it now gets the masked value).

---

### Task 4: Sample rename reaches KIT lanes

**Files:** `lib/core/src/sample_set.cpp:144` (`projSampleRename`); test `test/test_sample_set/test_main.cpp`

**Step 1: Failing test** — `test_rename_updates_kit_lanes`: a project with sample "kick" at index 0, instrument 1 SAMPLE with `sample = "kick"`, instrument 2 Kit with `kit[3].sample = "kick"`; `projSampleRename(p, 0, "bd")` → both names are `"bd"`, `kit[4].sample` (other) untouched.

**Step 2:** `pio test -e native -f test_sample_set` → FAIL.

**Step 3:** Where `projSampleRename` loops `p.instruments[i].sample`, add the same compare-and-copy for every `m.kit[k].sample`.

**Step 4:** PASS.

---

### Task 5: `expandStep` on a drum track

**Files:** `lib/core/src/step_expand.h` (`ExpandCtx::kit`, `kMaxStepEvents`), `lib/core/src/step_expand.cpp`, `lib/core/src/sequencer.cpp:566`; test `test/test_expand/test_main.cpp`

**Step 1: Failing tests** (`track`, `rng`, `ctx` exist in the file)

```cpp
static Instrument kit() { Instrument k; instrSetType(k, InstrType::Kit); return k; }  // notes 60..67
static Step drumStep(uint8_t mask, uint8_t vel = 0) { Step s; s.note = vel; s.vel = mask; return s; }

void test_drum_mask_to_lane_notes() {
  const Instrument k = kit();
  ExpandCtx c = ctx; c.kit = &k;
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(drumStep(0b00000101), track, c, rng, out));
  TEST_ASSERT_EQUAL(4, out.count);  // 2 lanes x (on, off); a lane without a sample still goes out (MIDI)
  TEST_ASSERT_EQUAL(60, out.ev[0].note);
  TEST_ASSERT_EQUAL(track.defVel, out.ev[0].vel);
  TEST_ASSERT_EQUAL(static_cast<int>(EvKind::NoteOff), static_cast<int>(out.ev[1].kind));
  TEST_ASSERT_EQUAL(62, out.ev[2].note);
  TEST_ASSERT_EQUAL(0, out.ev[2].offsetUs);
  TEST_ASSERT_FALSE(out.tie);
}

void test_drum_velocity_and_accent() {
  const Instrument k = kit();
  ExpandCtx c = ctx; c.kit = &k;
  Step s = drumStep(0b00000101, 100);
  s.fx[0] = {Fx::ACC, 0b00000001};  // lane 1 full, lane 3 at 60 %
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, c, rng, out));
  TEST_ASSERT_EQUAL(100, out.ev[0].vel);
  TEST_ASSERT_EQUAL(60, out.ev[2].vel);
}

void test_drum_ratchet_fits_and_tie_ignored() {
  const Instrument k = kit();
  ExpandCtx c = ctx; c.kit = &k;
  Step s = drumStep(0xFF);
  s.fx[0] = {Fx::RAT, 8};
  s.fx[1] = {Fx::TIE, 0};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, c, rng, out));
  TEST_ASSERT_EQUAL(128, out.count);
  TEST_ASSERT_TRUE(out.count <= kMaxStepEvents);
  TEST_ASSERT_FALSE(out.tie);
}

void test_drum_empty_mask_is_note_step() {
  const Instrument k = kit();
  ExpandCtx c = ctx; c.kit = &k;
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(drumStep(0), track, c, rng, out));
  TEST_ASSERT_EQUAL(0, out.count);
}
```

**Step 2:** `pio test -e native -f test_expand` → FAIL.

**Step 3: Implement** — `step_expand.h`: `constexpr int kMaxStepEvents = 8 * kKitLanes * 2 + kFxSlots;  // 8 ratchets x 8 lanes x (on, off) + controls`; `ExpandCtx`: `const Instrument* kit = nullptr;  // the track's KIT: steps are lane masks (drum track)`. `step_expand.cpp`: after `if (!s.hasNote()) return out.count > 0 || off;` insert `if (c.kit) { expandDrum(s, t, c, rng, ch, nudge, off, out); return true; }` and add the static helper (the ratchet / gate arithmetic is the melodic path's — factor it into a small shared static if the duplication bothers):

```cpp
// Drum track: vel = lane mask, note = step velocity (0 = default). One NoteOn per set lane per ratchet,
// note = the lane's note, velocity x 0.6 for lanes outside ACC. TIE, CHD, NRN, STR are ignored.
void expandDrum(const Step& s, const TrackCfg& t, const ExpandCtx& c, Rng& rng, uint8_t ch, int32_t nudge,
                const FxSlot* off, ExpandOut& out) {
  const Instrument& k = *c.kit;
  int vel = s.note ? s.note : t.defVel;
  if (const FxSlot* f = s.find(Fx::VRN)) vel += spread(rng, f->val);
  vel = vel < 1 ? 1 : (vel > 127 ? 127 : vel);
  uint8_t accent = 0xFF;
  if (const FxSlot* f = s.find(Fx::ACC)) accent = f->val;
  int rat = 1;
  if (const FxSlot* r = s.find(Fx::RAT)) rat = r->val < 2 ? 2 : (r->val > 8 ? 8 : r->val);
  uint32_t gatePct = gatePercent(t.defGate);
  if (const FxSlot* g = s.find(Fx::GAT)) gatePct = gatePercent(g->val);
  out.tie = false;
  if (off && out.offUs < nudge + static_cast<int32_t>(kMinGateUs)) out.offUs = nudge + static_cast<int32_t>(kMinGateUs);
  const uint32_t sub = c.stepUs / rat;
  uint32_t gateUs = rat > 1 ? static_cast<uint32_t>(static_cast<uint64_t>(sub) * (gatePct > 95 ? 95 : gatePct) / 100)
                            : static_cast<uint32_t>(static_cast<uint64_t>(c.stepUs) * gatePct / 100);
  if (gateUs < kMinGateUs) gateUs = kMinGateUs;
  for (int i = 0; i < rat; ++i) {
    const int32_t on = nudge + static_cast<int32_t>(sub * i);
    if (off && on >= out.offUs) continue;
    for (int l = 0; l < kKitLanes && out.count < kMaxStepEvents - 1; ++l) {
      if (!(s.vel & (1u << l))) continue;
      int v = (accent & (1u << l)) ? vel : vel * 3 / 5;
      if (v < 1) v = 1;
      const uint8_t n = k.kit[l].note;
      out.ev[out.count++] = {on, EvKind::NoteOn, ch, n, static_cast<uint8_t>(v)};
      int32_t offT = on + static_cast<int32_t>(gateUs);
      if (off && offT > out.offUs) offT = out.offUs;
      if (offT < on + static_cast<int32_t>(kMinGateUs)) offT = on + static_cast<int32_t>(kMinGateUs);
      out.ev[out.count++] = {offT, EvKind::NoteOff, ch, n, 0};
    }
  }
}
```

`sequencer.cpp:566` `Sequencer::expand`: `ExpandCtx c = ctx; c.kit = p_.kitOf(track);` and pass `c`.

**Step 4:** `pio test -e native -f test_expand -f test_sequencer` → PASS.

---

### Task 6: Synth — lane resolution, scratch sampler instrument, per-lane choke, `polyMax`

**Files:**
- Modify: `lib/core/src/synth_voice.h` (`Voice::lane`, `Voice::laneInst`; `allocVoice(..., int polyMax)`), `lib/core/src/synth_voice.cpp:33`, `lib/core/src/synth.h` (`instrOf`), `lib/core/src/synth.cpp:251-345` (`noteOn`), `:496` (`control`)
- Test: `test/test_synth/test_main.cpp`, `test/test_voices/test_main.cpp`

**Step 1: Failing tests**

`test_voices`: `test_poly_max_eight` — `polyMax = 8`: nine poly notes on one track → the first eight get distinct free voices, the ninth takes the track's oldest.

`test_synth` (the file's `p`, `s`, `buf`, bank helper that registers a test sample — reuse whatever `test_sample_*` in this file uses to make `bank_->find` succeed):

```cpp
static void makeKitTrack(int track, int kitIdx) {
  Instrument& k = p->instruments[kitIdx];
  instrSetType(k, InstrType::Kit);
  k.send = 50;
  for (int l = 0; l < kKitLanes; ++l) strcpy(k.kit[l].sample, "tick");  // a sample the test bank has
  k.kit[0].vol = 80; k.kit[0].pitch = 5; k.kit[0].decay = 30;
  p->tracks[track].out = TrackOut::Int;
  p->tracks[track].instr = static_cast<uint8_t>(kitIdx);
}
static int onlyVoice() { int f = -1; for (int v = 0; v < kVoices; ++v) if (s->voice(v).on) f = v; return f; }

void test_kit_sampler_lane_builds_scratch_instrument() {
  makeKitTrack(0, 0);
  const uint8_t on[3] = {0x90, 60, 100};
  TEST_ASSERT_TRUE(s->event(0, 0, on, 3));
  s->render(buf);
  const int v = onlyVoice();
  TEST_ASSERT_TRUE(v >= 0);
  const Voice& x = s->voice(v);
  TEST_ASSERT_TRUE(x.lane);
  TEST_ASSERT_TRUE(x.sample);
  TEST_ASSERT_EQUAL(static_cast<int>(InstrType::Sample), static_cast<int>(x.laneInst.type));
  TEST_ASSERT_EQUAL(60, x.laneInst.root);
  TEST_ASSERT_EQUAL(5, x.laneInst.transpose);
  TEST_ASSERT_EQUAL(80, x.laneInst.vol);
  TEST_ASSERT_EQUAL(30, x.laneInst.decay);
  TEST_ASSERT_EQUAL(0, x.laneInst.sustain);
  TEST_ASSERT_EQUAL(50, x.laneInst.send);
  TEST_ASSERT_EQUAL(0, x.laneInst.fltMode);
  TEST_ASSERT_EQUAL(0, x.laneInst.sliceCount);
}

void test_kit_decay_zero_is_one_shot() {
  makeKitTrack(0, 0);
  const uint8_t on[3] = {0x90, 61, 100};  // lane 2: decay 0
  s->event(0, 0, on, 3);
  s->render(buf);
  TEST_ASSERT_EQUAL(0, s->voice(onlyVoice()).laneInst.decay);
  TEST_ASSERT_EQUAL(127, s->voice(onlyVoice()).laneInst.sustain);
}

void test_kit_missing_sample_silent() {
  makeKitTrack(0, 0);
  strcpy(p->instruments[0].kit[0].sample, "nope");
  const uint8_t on[3] = {0x90, 60, 100};
  s->event(0, 0, on, 3);
  s->render(buf);
  TEST_ASSERT_EQUAL(-1, onlyVoice());
}

void test_kit_inst_lane_plays_instrument() {
  makeKitTrack(0, 0);
  p->instruments[0].kit[2].instr = 5;
  instrSetType(p->instruments[5], InstrType::Chip);
  const uint8_t on[3] = {0x90, 62, 100};
  s->event(0, 0, on, 3);
  s->render(buf);
  const Voice& x = s->voice(onlyVoice());
  TEST_ASSERT_FALSE(x.lane);
  TEST_ASSERT_EQUAL(5, x.instr);
  TEST_ASSERT_EQUAL(62, x.note);
}

void test_kit_eight_lanes_sound_and_rehit_chokes() {
  makeKitTrack(0, 0);
  for (int l = 0; l < kKitLanes; ++l) { const uint8_t on[3] = {0x90, static_cast<uint8_t>(60 + l), 100}; s->event(0, 0, on, 3); }
  s->render(buf);
  int n = 0;
  for (int v = 0; v < kVoices; ++v) n += s->voice(v).on && s->voice(v).track == 0;
  TEST_ASSERT_EQUAL(8, n);
  const uint8_t again[3] = {0x90, 60, 100};
  s->event(0, 0, again, 3);
  s->render(buf);
  n = 0;
  for (int v = 0; v < kVoices; ++v) n += s->voice(v).on && s->voice(v).track == 0;
  TEST_ASSERT_EQUAL(8, n);
}

void test_kit_unknown_note_silent() {
  makeKitTrack(0, 0);
  const uint8_t on[3] = {0x90, 40, 100};
  s->event(0, 0, on, 3);
  s->render(buf);
  TEST_ASSERT_EQUAL(-1, onlyVoice());
}
```

**Step 2:** `pio test -e native -f test_synth` → FAIL (no `lane`).

**Step 3: Implement**

`synth_voice.h`: in `Voice`: `bool lane = false;  // KIT sampler lane: the instrument is laneInst, not p_.instruments[instr]` and `Instrument laneInst;  // scratch sampler built at note-on (16 voices x sizeof(Instrument), ~5 KB)`. `allocVoice(..., bool heavy = false, int polyMax = kPolyPerTrack);` doc "polyMax: poly voices the track may hold (KIT lanes: kKitLanes)"; `synth_voice.cpp:33`: `trackCount >= polyMax`.

`synth.h`: `const Instrument& instrOf(const Voice& v) const { return v.lane ? v.laneInst : p_.instruments[v.instr]; }`; `control` (`synth.cpp:496`) uses it: `const Instrument& m = instrOf(v);` — the voice refers to its instrument by index today; this is the only indirection added (grep `instruments[v.instr]` / `instruments[x.instr]` for any other site and switch it too).

`noteOn`, top:

```cpp
  uint8_t ii = trackInstr(track);
  bool kitLane = false;   // a KIT lane: mono per lane (choke on the same note), poly across lanes
  Instrument scratch;     // sampler lane: built from the lane, copied into the voice
  const Instrument* mp = &p_.instruments[ii];
  if (mp->type == InstrType::Kit) {
    const Instrument& k = *mp;
    int lane = -1;
    for (int l = 0; l < kKitLanes && lane < 0; ++l)
      if (k.kit[l].note == note) lane = l;
    if (lane < 0) return;
    const KitLane& ln = k.kit[lane];
    kitLane = true;
    if (ln.instr < kInstruments) {
      if (p_.instruments[ln.instr].type == InstrType::Kit) return;  // no kit in a kit
      ii = ln.instr;
      mp = &p_.instruments[ii];
    } else {
      // Mini sampler: the lane's sample at its own pitch (root = lane note) + pitch, one-shot or decay.
      scratch.type = InstrType::Sample;
      memcpy(scratch.sample, ln.sample, sizeof(scratch.sample));
      scratch.root = ln.note;
      scratch.transpose = ln.pitch;
      scratch.vol = ln.vol;
      scratch.attack = 0;
      scratch.decay = ln.decay;
      scratch.sustain = ln.decay ? 0 : 127;
      scratch.release = kLaneRelease;  // envTimeMs ~10 ms (pick the value from envTimeMs' curve)
      scratch.mono = true;
      scratch.fltMode = 0;
      scratch.lfoDepth = 0;
      scratch.send = k.send;
      // start 0, end 0xFFFF, loop Off, sliceCount 0, reverse false: Instrument() defaults
      mp = &scratch;
    }
  }
  const Instrument& m = *mp;
```

The existing sample lookup then uses `m.sample` (empty / missing → `return`: silent lane). Voice choice: `const bool mono = kitLane ? false : (…as now…)`; skip the SAMPLE track-choke loop when `kitLane`; before `allocVoice`:

```cpp
  if (kitLane && held < 0)
    for (int x = 0; x < kVoices; ++x)
      if (voices_[x].on && voices_[x].track == track && voices_[x].note == note) held = x;  // the lane's voice: choke
```

`const bool overlap = legato && !wasReleasing && !drum && !prevDrum && !kitLane;` (a re-hit restarts the sample). `allocVoice(voices_, track, mono, age_, legato, heavy, kitLane ? kKitLanes : kPolyPerTrack)`. After `Voice& v = voices_[vi];`: `v.lane = kitLane && mp == &scratch; if (v.lane) v.laneInst = scratch;` — `v.instr = ii` stays (the KIT's index for a sampler lane, the referenced instrument's for INST mode). Everything else unchanged. Note the heavy flag: computed from `m`, so a sampler lane is not heavy and an FM / DRUM referenced instrument is.

**Step 4:** `pio test -e native -f test_synth -f test_voices` → PASS. Preview needs nothing: `audio::preview(kitIdx, 60)` hits lane 1.

---

### Task 7: Euclid lane, Transpose skip, MIDI import lanes, edit ops byte-exact

**Files:** `lib/core/src/euclid.h/.cpp`, `lib/core/src/edit_ops.h/.cpp` (`transposeSel(..., const bool* drumTracks = nullptr)`), `lib/core/src/midi_import.cpp:138-150`; tests `test_euclid`, `test_edit`, `test_import`

**Step 1: Failing tests**
- `test_euclid`: `test_lane_writes_only_its_bit` — lane 2 set on steps 0 and 4 (`vel = 0b10`, `note = 0`); `EuclidParams{hits 4, length 16, lane 0, merge false}` → steps 0, 4, 8, 12 have bit 0; steps 0 and 4 keep bit 1; a step with `note = 90` keeps it.
- `test_edit`: `test_transpose_skips_drum_tracks` — `bool drum[kTracks] = {false, true}`; sel over tracks 0–1: track 0 moved, track 1 bytes unchanged. `test_copy_paste_drum_step_byte_exact` — `vel 0xA5, note 100` survives copy / paste.
- `test_import`: `test_notes_to_kit_lanes` — target track's instrument is a Kit (notes 60..67): SMF notes 60 and 62 on one step → `vel = 0b101`, `note = <velocity>`; note 40 dropped (`notesDropped` + 1).

**Step 2:** → FAIL.

**Step 3:** `EuclidParams`: `int8_t lane = -1;  // drum track: the lane written (bit), -1 = melodic`. `applyEuclid`, `lane >= 0`: replace mode first clears `vel &= ~(1 << lane)` on every step of the track; hits: `st.vel |= 1 << lane; if (!st.hasNote()) st.note = 0;`; accents set `st.note = e.accentVel` only when `st.note == 0`; Fill / range ignored. `transposeSel`: `if (drum && drum[t]) continue;`. `midi_import.cpp:138`: with `const Instrument* k = p.kitOf(pl.track)`: lane by `k->kit[l].note == pl.note`; none → `++r.notesDropped; continue;`; existing note step → `s.vel |= bit`; new step → `ns.note = (sn.vel == tc.defVel ? 0 : sn.vel); ns.vel = bit;`.

**Step 4:** `pio test -e native` → all PASS.

---

### Task 8: GRID — squares, lane pad, lane cursor, track buttons

**Files:** `src/ui/grid_screen.h`, `src/ui/grid_screen.cpp` (`drawOverview`, `drawDetail`, keyboard call sites, `rowsFor`, touch, `editTurn`, `trackKey`, `transpose`)

**Step 1: Helpers** (`grid_screen.h`):

```cpp
  static constexpr int kPadW = kScreenW / mt::kKitLanes;  // lane pad button
  bool drumAt(int tr) const { return app_.project().trackIsDrum(tr); }
  bool drum() const { return drumAt(track()); }
  bool padShown() const { return edit_ && curField_ == kNote && drum(); }
  void toggleLane(int lane);
  void drawMask(LGFX_Sprite& s, int x, int y, uint8_t mask, bool audible, int cursorLane);
  void drawPad(LGFX_Sprite& s, int y);
  int lane_ = 0;  // Detail NOTE field on a drum track: the lane under the encoder
```

`keyboardShown()` → `edit_ && curField_ == kNote && !drum()`; `rowsFor` and the bottom reservation use `keyboardShown() || padShown()`.

**Step 2: Drawing**

```cpp
// 8 squares, 6 px pitch from x: filled = lane hit; cursorLane >= 0 frames that lane.
void GridScreen::drawMask(LGFX_Sprite& s, int x, int y, uint8_t mask, bool audible, int cursorLane) {
  for (int l = 0; l < mt::kKitLanes; ++l) {
    const int sx = x + 2 + l * 6;
    if (mask & (1u << l)) s.fillRect(sx, y + 5, 5, 5, audible ? kText : kDim);
    else s.drawRect(sx, y + 5, 5, 5, kDim);
    if (l == cursorLane) s.drawRect(sx - 1, y + 4, 7, 7, kEditCursor);
  }
}
```

`drawOverview` cell: `if (drumAt(tr) && c.hasNote()) drawMask(s, x, y, c.vel, p.trackAudible(tr), -1); else { note name; velocity bar }`; the fx dot at `y + 1` on drum tracks. `drawDetail`: NOTE field → `drawMask(…, edit_ && curField_ == kNote ? lane_ : -1)`; VEL field text = `c.note` (`...` for 0) on drum tracks. Bottom: `if (padShown()) drawPad(…); else if (keyboardShown()) drawKeyboard(…)`.

```cpp
void GridScreen::drawPad(LGFX_Sprite& s, int y) {
  const mt::Project& p = app_.project();
  const mt::Instrument* k = p.kitOf(track());
  const mt::Step& c = pat().steps[track()][curStep_];
  s.fillRect(0, y, kScreenW, kKbH, kBg);
  for (int l = 0; l < mt::kKitLanes; ++l) {
    const int x = l * kPadW;
    const bool on = c.hasNote() && (c.vel & (1u << l));
    s.fillRect(x + 1, y + 1, kPadW - 2, kKbH - 2, on ? kPlayBg : kBeatBg);
    if (l == lane_) s.drawRect(x, y, kPadW, kKbH, kEditCursor);
    char nm[5] = "-";
    if (k) {
      const mt::KitLane& ln = k->kit[l];
      if (ln.instr < mt::kInstruments) snprintf(nm, sizeof(nm), "%.4s", p.instruments[ln.instr].name);
      else if (ln.sample[0]) snprintf(nm, sizeof(nm), "%.4s", ln.sample);
    }
    s.setTextColor(on ? kText : kDim);
    s.drawString(nm, x + 4, y + 4);
    snprintf(nm, sizeof(nm), "%d", l + 1);
    s.drawString(nm, x + 4, y + kKbH - 14);
  }
}
```

**Step 3: Editing**

```cpp
// Drum track: flips lane bit; an empty step becomes a note step at the default velocity.
void GridScreen::toggleLane(int lane) {
  mt::Step st = pat().steps[track()][cur()];
  if (!st.hasNote()) { st.note = 0; st.vel = 0; }
  st.vel ^= static_cast<uint8_t>(1u << lane);
  lane_ = lane;
  writeStep(st);
  if (st.vel & (1u << lane))
    if (const mt::Instrument* k = app_.project().kitOf(track())) previewNote(k->kit[lane].note);
}
```

- Pad touch (`padShown() && ev.y >= y0_ + h_ - kKbH`, Tap): `toggleLane(clampi(ev.x / kPadW, 0, 7))`.
- `editTurn` `kNote` on `drum()`: `if (shift) toggleLane(lane_); else lane_ = clampi(lane_ + delta, 0, 7);` redraw, return. `kVel` on `drum()`: edits `st.note` (`clampi(st.note + delta * (shift ? 10 : 1), 1, 127)` from a note step; `kNoteOff` untouched).
- `trackKey`: `if (edit_) { if (drum()) toggleLane(n); else enterDegree(n, shift); return true; }`.
- `transpose`: `bool drum[mt::kTracks]` from `drumAt`, passed to `transposeSel`; every track in `sel` drum → `app_.toast("DRUM TRACK")`, return without undo.
- Fx dim rules (Overview `live`, Detail `ignored`): `fxDrumOnly(f.cmd) && !drumAt(tr)` is not live.

**Step 4:** `pio run -e wt32` → compiles.

---

### Task 9: Euclid dialog `Lane`

**Files:** `src/ui/euclid_dialog.cpp`, `src/ui/grid_screen.cpp` (`openEuclid`: `params.lane = drum() ? 0 : -1`)

**Step 1:** When `params.lane >= 0` the rows show `Lane` (1–8) instead of `Fill`, `Range`, `Seed / Reseed`; `Hits`, `Length`, `Rotation`, `Vel`, `Accent`, `Merge` stay. The dialog already re-applies through `applyEuclid`.

**Step 2:** `pio run -e wt32` → compiles.

---

### Task 10: INST — type KIT, lanes page, no presets

**Files:** `src/ui/inst_screen.h` (`kitRows_`), `src/ui/inst_screen.cpp` (`typeCount` / `typeRows` / `pageCount` / `showPage`, `Type` edit, `Instr` edit → note from root, PRESET button), `src/ui/preset_browser.cpp` (type cycling skips Kit), `src/ui/names.h` (`KIT`); tests: `test_preset_paths`, `test_presets_factory`, `test_preset_io` must still pass.

**Step 1: Rows** — a `Param kitRows_[kKitLanes * 6]` built once; for lane `l` the rows `L<l+1> Src`, `Sample`, `Instr`, `Vol`, `Pitch`, `Decay`, `Note`, with `dim` hiding the rows of the other mode (`Sample`/`Vol`/`Pitch`/`Decay` dim in INST mode, `Instr` dim in SAMPLE mode — if `ParamList` cannot skip dim rows, rebuild the visible list on a `Src` change the way `syncParams` rebuilds on a type change). Key lambdas:

```cpp
// Src: SAMPLE (own sampler) / INST (an instrument). Switching to SAMPLE drops the instrument.
format: [this, l](char* o, int n) { snprintf(o, n, "%s", inst().kit[l].instr < mt::kInstruments ? "INST" : "SAMPLE"); }
edit:   [this, l](int d) { mt::KitLane& ln = inst().kit[l]; if (d > 0) { if (ln.instr >= mt::kInstruments) ln.instr = 0; } else ln.instr = mt::kNoInstr; }
// Sample: the project sample picker the SAMPLE type uses (same format / edit / warn lambdas, on ln.sample).
// Instr: INS1..16 (red when a KIT); a SAMPLE-type pick sets the lane note to its root (next free up).
edit: [this, l](int d) {
  mt::Instrument& k = inst();
  mt::KitLane& ln = k.kit[l];
  ln.instr = static_cast<uint8_t>(clampi(ln.instr + d, 0, mt::kInstruments - 1));
  const mt::Instrument& m = app_.project().instruments[ln.instr];
  if (m.type == mt::InstrType::Sample) ln.note = freeNoteFrom(k, l, m.root);
}
warn: [this, l]() { const uint8_t s = inst().kit[l].instr; return s < mt::kInstruments && app_.project().instruments[s].type == mt::InstrType::Kit; }
// Vol 0..127, Pitch -24..24, Decay: "FULL" for 0 else envTimeMs ms (0..127).
// Note: noteName; turning moves a semitone skipping notes other lanes hold.
edit: [this, l](int d) { mt::Instrument& k = inst(); k.kit[l].note = freeNoteFrom(k, l, k.kit[l].note + (d > 0 ? 1 : -1), d > 0 ? 1 : -1); }
```

with a static `uint8_t freeNoteFrom(const mt::Instrument& k, int lane, int n, int dir = 1)` that clamps to 0..127 and steps by `dir` while another lane holds `n` (≤ 128 tries).

**Step 2: Pages** — for `InstrType::Kit`: `pageCount() == 2` (MAIN with `Name`, `Type`, `Send`; `LANES` = `kitRows_`, `setVisibleRows(kListRows)` scrolls). Page bar label `LANES`. Do not restructure the SYNTH branches.

**Step 3: Presets** — PRESET button: `if (inst().type == mt::InstrType::Kit) { app_.toast("NO KIT PRESETS"); return; }`. `preset_browser.cpp` type arrows skip `Kit`. `presets_factory` / `preset_paths`: exclude Kit from per-type folders. Run `pio test -e native -f test_preset_paths -f test_presets_factory -f test_preset_io` → PASS (adjust any "every type has a folder" assertion to skip Kit).

**Step 4:** `pio run -e wt32` → compiles.

---

### Task 11: Import dialog, sweep, docs

**Files:** `src/ui/import_dialog.cpp:174`, `README.md` (Звук: тип KIT — 8 лейнов, свой сэмпл или инструмент; GRID: драм-дорожка), `docs/manual.html` (GRID драм-дорожка: маска, пад, кнопки дорожек; INST KIT: Src / Sample / Instr / Vol / Pitch / Decay / Note; fx `ACC` в таблице; Euclid Lane; «смена инструмента KIT ↔ не-KIT переосмысливает шаги дорожки»; «Copy между видами дорожек — побайтно»; переименование сэмпла обновляет лейны, удаление — лейн молчит).

**Step 1:** `snprintf(tr, sizeof(tr), "T%d%s", t + 1, app_.project().trackIsDrum(t) ? "*" : "")`.

**Step 2:** Docs as listed. fx row: `ACC | 00–FF | только драм-дорожки | маска лейнов с полной громкостью; остальные 60 %`.

**Step 3: Final** — `pio test -e native` → all PASS; `pio run -e wt32` → compiles; read the boot heap log for the `Synth` growth (16 x `sizeof(Instrument)`). Summarize: chunk `KITS` (176-byte records), `Fx::ACC`, `kMaxStepEvents` 134, `Voice::laneInst` + `instrOf`, the `polyMax` parameter, anything deferred.
