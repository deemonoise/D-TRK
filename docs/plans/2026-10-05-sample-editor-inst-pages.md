# Sample Editor (zoom, chops) + INST Pages Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (or subagent-driven-development) to implement this plan task-by-task.

**Goal:** INST split into 5 pages; SAMPLE page = waveform editor with zoom, Start/End/Loop/slice markers, auto chop (equal / transients), manual slices with Shift snap to transients; slices play by note (NOTE) or `SLC` fx (FX); slices saved in projects and presets.

**Design:** `docs/plans/2026-10-05-sample-editor-inst-pages-design.md` (read it first).

**Architecture:** Pure logic in `lib/core` (slice helpers, onset detection, codec, synth), native-tested with Unity. UI in `src/ui`: `TouchTracker` gets horizontal drag, `ParamList` gets page overflow, `InstScreen` gets pages, new `SampleEditor` component owns the SMPL page.

**Tech Stack:** C++17, PlatformIO (`env:native` tests, `env:wt32` firmware), LovyanGFX sprites, Unity.

**Commands:**
- one test suite: `pio test -e native -f test_slices`
- all native tests: `pio test -e native`
- firmware build: `pio run -e wt32`

**Project rules:** do NOT commit (user commits manually) — skip every commit step. Keep the code style of neighbours: terse comments, `mt::` namespace in core, `ui::` in UI, `clampi`-style helpers, one-byte fields where the audio task reads without a lock.

---

### Task 1: Slice fields and helpers in the model

**Files:**
- Modify: `lib/core/src/model.h` (Instrument, enums)
- Create: `lib/core/src/slices.h`, `lib/core/src/slices.cpp`
- Test: `test/test_slices/test_main.cpp`

**Step 1: model.h.** Next to `LoopMode`:

```cpp
// Stored in files: new values go before Count only.
enum class SliceMode : uint8_t { Off, Note, Fx, Count };  // NOTE: note - root = slice; FX: SLC picks it
enum class ChopMode : uint8_t { Equal, Trans, Count };
constexpr int kMaxSlices = 32;
```

In `Instrument`, after `reverse`:

```cpp
  // Slices: start points, fractions of the whole sample (/0xFFFF), ascending. Slice i plays
  // slices[i]..slices[i+1], the last one to end. Cleared when the sample changes.
  uint8_t sliceMode = 0;      // SliceMode
  uint8_t chopMode = 0;       // ChopMode, the CHOP button
  uint8_t chopN = 8;          // EQUAL: 2..kMaxSlices
  uint8_t chopThresh = 50;    // TRANS: 0..100, higher = more slices
  uint8_t sliceCount = 0;     // 0..kMaxSlices
  uint16_t slices[kMaxSlices] = {0};
```

**Step 2: failing tests** `test/test_slices/test_main.cpp`:

```cpp
#include <unity.h>
#include "slices.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_insert_keeps_order_and_rejects_duplicates() {
  Instrument m;
  TEST_ASSERT_EQUAL(0, sliceInsert(m, 0x8000));
  TEST_ASSERT_EQUAL(0, sliceInsert(m, 0x1000));
  TEST_ASSERT_EQUAL(2, sliceInsert(m, 0x9000));
  TEST_ASSERT_EQUAL(-1, sliceInsert(m, 0x8000));
  TEST_ASSERT_EQUAL(3, m.sliceCount);
  TEST_ASSERT_EQUAL_HEX16(0x1000, m.slices[0]);
  TEST_ASSERT_EQUAL_HEX16(0x8000, m.slices[1]);
  TEST_ASSERT_EQUAL_HEX16(0x9000, m.slices[2]);
}

void test_insert_full_fails() {
  Instrument m;
  for (int i = 0; i < kMaxSlices; ++i) TEST_ASSERT_TRUE(sliceInsert(m, static_cast<uint16_t>(i * 100 + 1)) >= 0);
  TEST_ASSERT_EQUAL(-1, sliceInsert(m, 0xF000));
}

void test_remove_and_clear() {
  Instrument m;
  sliceInsert(m, 10);
  sliceInsert(m, 20);
  sliceInsert(m, 30);
  sliceRemove(m, 1);
  TEST_ASSERT_EQUAL(2, m.sliceCount);
  TEST_ASSERT_EQUAL(30, m.slices[1]);
  sliceRemove(m, 5);  // out of range: no-op
  TEST_ASSERT_EQUAL(2, m.sliceCount);
  sliceClear(m);
  TEST_ASSERT_EQUAL(0, m.sliceCount);
}

void test_move_clamps_between_neighbours() {
  Instrument m;
  sliceInsert(m, 100);
  sliceInsert(m, 200);
  sliceInsert(m, 300);
  TEST_ASSERT_EQUAL(299, sliceMove(m, 1, 5000));
  TEST_ASSERT_EQUAL(101, sliceMove(m, 1, 0));
  TEST_ASSERT_EQUAL(0, sliceMove(m, 0, 0));
}

void test_region_of_slices() {
  Instrument m;
  m.end = 0xFFFF;
  sliceInsert(m, 0);
  sliceInsert(m, 0x8000);
  uint32_t a, b;
  TEST_ASSERT_TRUE(sliceRegion(m, 0, 1000, a, b));
  TEST_ASSERT_EQUAL(0, a);
  TEST_ASSERT_EQUAL(500, b);
  TEST_ASSERT_TRUE(sliceRegion(m, 1, 1000, a, b));
  TEST_ASSERT_EQUAL(500, a);
  TEST_ASSERT_EQUAL(1000, b);  // the last one ends at End
  TEST_ASSERT_FALSE(sliceRegion(m, 2, 1000, a, b));
}

void test_chop_equal_within_start_end() {
  Instrument m;
  m.start = 0;
  m.end = 0x8000;
  m.chopN = 4;
  chopEqual(m);
  TEST_ASSERT_EQUAL(4, m.sliceCount);
  TEST_ASSERT_EQUAL(0, m.slices[0]);
  TEST_ASSERT_UINT16_WITHIN(1, 0x2000, m.slices[1]);
  TEST_ASSERT_UINT16_WITHIN(1, 0x6000, m.slices[3]);
}

void test_chop_equal_clamps_n() {
  Instrument m;
  m.chopN = 99;
  chopEqual(m);
  TEST_ASSERT_EQUAL(kMaxSlices, m.sliceCount);
}

void test_frac_frame_roundtrip() {
  for (uint32_t len : {1000u, 44100u, 320000u})
    for (uint32_t f : {0u, 1u, len / 3, len - 1})
      TEST_ASSERT_UINT32_WITHIN(1, f, fracToFrame(frameToFrac(f, len), len));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_insert_keeps_order_and_rejects_duplicates);
  RUN_TEST(test_insert_full_fails);
  RUN_TEST(test_remove_and_clear);
  RUN_TEST(test_move_clamps_between_neighbours);
  RUN_TEST(test_region_of_slices);
  RUN_TEST(test_chop_equal_within_start_end);
  RUN_TEST(test_chop_equal_clamps_n);
  RUN_TEST(test_frac_frame_roundtrip);
  return UNITY_END();
}
```

**Step 3:** `pio test -e native -f test_slices` → FAIL (no slices.h).

**Step 4: implement** `slices.h`:

```cpp
#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

// Fraction of the sample (/0xFFFF) <-> frame, len = sample frames.
uint32_t fracToFrame(uint16_t f, uint32_t len);
uint16_t frameToFrac(uint32_t frame, uint32_t len);
// Sorted insert; index, or -1 when full or the position is taken.
int sliceInsert(Instrument& m, uint16_t pos);
void sliceRemove(Instrument& m, int i);  // out of range: no-op
void sliceClear(Instrument& m);
// Moves slice i, clamped strictly between its neighbours (0 / 0xFFFF at the ends); new position.
uint16_t sliceMove(Instrument& m, int i, int pos);
// Frames [from, to) of slice i in a sample of len frames; false if i >= sliceCount.
bool sliceRegion(const Instrument& m, int i, uint32_t len, uint32_t& from, uint32_t& to);
// chopN (clamped 2..kMaxSlices) equal slices of Start..End.
void chopEqual(Instrument& m);

}  // namespace mt
```

`slices.cpp`: straightforward. `fracToFrame = (uint64_t)f * len / 0xFFFF`; `frameToFrac = min(0xFFFF, ((uint64_t)frame * 0xFFFF + len / 2) / len)` (len 0 → 0). `sliceRegion`: `from = fracToFrame(slices[i])`, `to = i + 1 < count ? fracToFrame(slices[i + 1]) : fracToFrame(m.end)`; clamp `from < len`, `to <= len`, `to > from` (else `to = from + 1`). `chopEqual`: `n = clamp(chopN, 2, kMaxSlices)`, `slices[k] = start + (end - start) * k / n`, `sliceCount = n`. `sliceMove`: lo = i > 0 ? slices[i-1] + 1 : 0, hi = i + 1 < count ? slices[i+1] - 1 : 0xFFFF.

**Step 5:** `pio test -e native -f test_slices` → PASS. Run `pio test -e native -f test_model` too (Instrument changed) → PASS.

---

### Task 2: Onset (transient) detection and transient chop

**Files:**
- Create: `lib/core/src/onset.h`, `lib/core/src/onset.cpp`
- Test: `test/test_onset/test_main.cpp`

**API (`onset.h`):**

```cpp
#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

struct Onset {
  uint32_t pos;       // frame where the attack starts
  uint16_t strength;  // 1..1000, 1000 = the strongest found
};
constexpr int kMaxOnsets = 128;

// Transients of mono int16 data, ascending by pos; returns the count (<= max). Energy in windows
// of 256 frames, hop 128; onset = rise of log energy >= 3 dB above a -60 dBFS gate; peaks closer
// than 40 ms keep the stronger; position refined back to the first 32-frame block reaching half
// the peak level of the window. More than max candidates keep the strongest.
int detectOnsets(const int16_t* d, uint32_t frames, uint32_t rate, Onset* out, int max);
// Nearest onset to frame pos, -1 if n == 0.
int nearestOnset(const Onset* o, int n, uint32_t pos);
// First onset strictly after (dir > 0) or before (dir < 0) pos, -1 if none.
int stepOnset(const Onset* o, int n, uint32_t pos, int dir);
// TRANS chop of Start..End: a slice at Start, plus up to kMaxSlices - 1 strongest onsets inside
// with strength >= (100 - chopThresh) * 10, skipping those within 10 ms of Start.
void chopTransients(Instrument& m, const Onset* o, int n, uint32_t len, uint32_t rate);

}  // namespace mt
```

**Step 1: failing tests** (`test/test_onset/test_main.cpp`). Build a static `int16_t data[32000 * 2]` signal:
- `test_silence_has_no_onsets`: all zeros → 0.
- `test_clicks_found_at_their_positions`: rate 32000, low noise ±20 (LCG) everywhere, decaying bursts (amplitude 20000·e^(-t/800), random sign noise) starting at frames 4000, 16000, 40000 → exactly 3 onsets, each `pos` within 64 frames before/at the burst start (`TEST_ASSERT_UINT32_WITHIN(64, start, o[i].pos)` and `o[i].pos <= start + 32`).
- `test_strengths_normalized`: louder burst (20000) vs quieter (4000) → strongest == 1000, quieter < 1000 and > 0.
- `test_close_peaks_merged`: bursts at 8000 and 8400 (12.5 ms apart) → one onset near 8000.
- `test_max_keeps_strongest`: 10 bursts with rising amplitudes, max = 4 → 4 onsets, ascending pos, they are the last 4 bursts.
- `test_nearest_and_step`: hand-made `Onset o[3] = {{100,1},{500,1},{900,1}}`: nearest(480)=1, nearest(0)=0; step(500,+1)=2, step(500,-1)=0, step(900,+1)=-1; nearest with n=0 → -1.
- `test_chop_transients_threshold`: onsets `{{8000,1000},{16000,300},{24000,800}}`, len 32000, rate 32000, Start 0, End 0xFFFF: chopThresh 100 → 4 slices (0 + 3); chopThresh 50 (min strength 500) → 3 slices; chopThresh 0 (min 1000) → 2 slices; slices ascending; slices[0] == 0.
- `test_chop_transients_inside_region_only`: Start = 0x4000 (frame 8000), End 0xC000: onset at 4000 ignored, onset at 8100 (within 10 ms of Start) skipped, slices[0] == m.start.

**Step 2:** `pio test -e native -f test_onset` → FAIL.

**Step 3: implement `onset.cpp`.**
- `constexpr int kWin = 256, kHop = 128, kRefine = 32;`
- Stream windows k = 0.. while `k * kHop + kWin <= frames`: `e = mean(x²)`, `db = 10*log10(e / 32768² + 1e-12f)`. Keep previous db (start -120).
- flux[k] = db - prevDb if db > -60 else 0. Need flux[k-1], flux[k], flux[k+1] for a local peak: keep a 3-entry ring; peak at k when `flux[k] >= 3 && flux[k] > flux[k-1] && flux[k] >= flux[k+1]`.
- Refine: range `[max(0,(k-1)*kHop), min(frames, k*kHop + kWin))`; peak = max |x| in range; pos = start of first 32-frame block whose max |x| >= peak/2.
- Candidate gap: if `pos - last.pos < rate * 40 / 1000`, keep the one with bigger flux (replace in place).
- Storage: candidates in `out` with raw flux as float in a local `float fl[kMaxOnsets]` (max <= kMaxOnsets, clamp max). When full and a new candidate's flux > weakest, replace the weakest, then keep `out` sorted by pos (insertion).
- Normalize: strength = max(1, round(flux / maxFlux * 1000)).
- `chopTransients`: from = fracToFrame(start), to = fracToFrame(end); minS = (100 - clamp(chopThresh,0,100)) * 10; skip = rate / 100; pick candidates with `pos >= from + skip && pos < to && strength >= minS`; if more than kMaxSlices - 1, keep the strongest (partial selection sort by strength, then sort by pos). slices[0] = m.start, then `frameToFrac(pos, len)` each (skip duplicates of the previous value). sliceCount accordingly.

**Step 4:** `pio test -e native -f test_onset` → PASS.

---

### Task 3: Slice record codec and project chunk SLCE

**Files:**
- Modify: `lib/core/src/inst_codec.h`, `lib/core/src/inst_codec.cpp`
- Modify: `lib/core/src/project_io.cpp`
- Test: `test/test_project_io/test_main.cpp`

**Step 1: failing test** in test_project_io (follow the file's existing save/load helpers):
- `test_slices_roundtrip`: instrument 3: sliceMode Note, chopMode Trans, chopN 12, chopThresh 70, slices {0x100, 0x2000, 0x9000} → save, load → all fields equal; instrument 0 has sliceCount 0 and defaults (chopN 8, chopThresh 50).
- `test_slices_bad_order_truncated`: pack a record by hand with count 3 and positions {10, 5, 20} via `unpackSlices` → sliceCount 1 (longest ascending prefix). Count 40 → clamped to 32 then same rule.

**Step 2:** run → FAIL.

**Step 3: codec.** In `inst_codec.h`:

```cpp
constexpr size_t kSliceRecSize = 8 + 2 * kMaxSlices;  // modes, count, 3 reserved, positions
void packSlices(const Instrument& m, uint8_t* b);
void unpackSlices(const uint8_t* b, Instrument& m);
```

Layout: b[0] sliceMode, b[1] chopMode, b[2] chopN, b[3] chopThresh, b[4] count, b[5..7] 0, then u16 LE positions (all 32, unused 0). Unpack clamps: modes < Count else 0, chopN 2..32, chopThresh 0..100, count <= 32 and to the longest strictly ascending prefix.

**project_io.cpp:**
- `static_assert`s: replace the per-record asserts by `constexpr size_t kMaxRec = kSliceRecSize > kInstRecSize ? kSliceRecSize : kInstRecSize;` and use `uint8_t b[kMaxRec]` in `readRecords`; assert every rec size <= kMaxRec.
- Save: after FLTR, `o.chunk("SLCE", 1 + kInstruments * kSliceRecSize)`, count byte, packSlices per instrument.
- Load: `else if (memcmp(ch, "SLCE", 4) == 0) e = readSlce(in, size, out);` with `readRecords(..., kSliceRecSize, kInstruments, unpackSlices)`.
- Old files without SLCE: Instrument defaults (Project::reset) — nothing to do.

**Step 4:** `pio test -e native -f test_project_io` → PASS (existing tests too: check any test asserting exact file size and update the expected size by `8 + 1 + 16 * 72`).

---

### Task 4: Preset v2 with slices

**Files:**
- Modify: `lib/core/src/preset_io.h`, `lib/core/src/preset_io.cpp`
- Test: `test/test_preset_io/test_main.cpp`

**Step 1: tests:**
- update `test_..size`: `kPresetSize == 156` (8 + 48 + 16 + 8 + 72 + 4), and `kPresetSizeV1 == 84`.
- `test_v2_roundtrip_slices`: SAMPLE preset with slices → save/load → slices, modes equal.
- `test_v1_file_loads_without_slices`: build a v1 file by hand: header `MTI1`, version 1, type, 0, 0, packInst/packFm/packFlt, CRC over the 80 bytes → loads Ok, sliceCount 0.
- `test_apply_preset_sample_missing_keeps_slices`: dst has sample "a", root 50, slices {1,2}; src SAMPLE with sample "b", slices {7}, sliceMode Fx → `applyPreset(dst, src, false)` → sample "a", root 50, slices {1,2}, sliceMode Fx. With `true` → slices {7}.

**Step 2:** run → FAIL.

**Step 3:** `kPresetVersion = 2`; `kPresetSizeV1 = 8 + kInstRecSize + kFmRecSize + kFltRecSize + 4`; `kPresetSize = kPresetSizeV1 + kSliceRecSize`. save writes packSlices before CRC. load: after the 8-byte header pick `size = b[4] >= 2 ? kPresetSize : kPresetSizeV1`, read `size - 8`, CRC at `size - 4`, unpackSlices only for v2. applyPreset: when `!sampleFound` also restore `sliceCount` and `slices` from the old dst (copy before `dst = src`).

**Step 4:** `pio test -e native -f test_preset_io` → PASS; `-f test_presets_factory` → PASS.

---

### Task 5: Fx::SLC

**Files:**
- Modify: `lib/core/src/model.h` (enum Fx + comment), `lib/core/src/fx_info.cpp`, `lib/core/src/fx_info.h` (comment)
- Test: `test/test_fx_info/test_main.cpp`

**Step 1: tests:** change `fxNextCmd(Fx::None, -1) == Fx::SLC`, `fxNextCmd(Fx::SLC, 1) == Fx::None`, `fxNextCmd(Fx::RES, 1) == Fx::SLC`; add `fxName(Fx::SLC) == "SLC"`, `fxSynthOnly(Fx::SLC)`, `fxDefault(Fx::SLC) == 0`, `fxStep(Fx::SLC, 30, 5) == 31`.

**Step 2:** run → FAIL.

**Step 3:** enum: `..., FLT, RES, SLC, Count`. kInfo: `{"SLC", 0, kMaxSlices - 1, 0, false},  // SLC: slice of the next SAMPLE note-on (FX slice mode)`. Update comments "SLD..RES" → "SLD..SLC" in model.h / fx_info.h.

**Step 4:** `pio test -e native -f test_fx_info` → PASS.

---

### Task 6: Synth plays slices

**Files:**
- Modify: `lib/core/src/synth.h` (TrackRt), `lib/core/src/synth_voice.h` (Voice), `lib/core/src/synth.cpp`
- Test: `test/test_synth/test_main.cpp`

**Step 1: tests** (use `sampleInstr(400, kSynthRate)`, ramp data, helpers `stepStart`, `fx`, `renderSamples`, `outOf` in that file; add `#include "slices.h"`):

```cpp
static void fourSlices() {  // slices at frames 0, 100, 200, 300 of 400
  Instrument& m = p->instruments[0];
  for (int i = 0; i < 4; ++i) sliceInsert(m, frameToFrac(i * 100, 400));
}

void test_slice_note_mode_plays_slice_of_note() {
  sampleInstr(400, kSynthRate);
  fourSlices();
  p->instruments[0].sliceMode = static_cast<uint8_t>(SliceMode::Note);
  noteOn(0, 0, 62);  // root 60 -> slice 2, at the root's speed
  TEST_ASSERT_INT_WITHIN(1, 100, renderSamples(out, 1024));
  TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[200]), out[0]);
  TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[250]), out[50]);
}

void test_slice_note_mode_out_of_range_is_silent() {
  sampleInstr(400, kSynthRate);
  fourSlices();
  p->instruments[0].sliceMode = static_cast<uint8_t>(SliceMode::Note);
  noteOn(0, 0, 64);
  noteOn(0, 0, 59);
  TEST_ASSERT_TRUE(silentBlocks(4));
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_slice_note_mode_ignores_loop() {
  sampleInstr(400, kSynthRate);
  fourSlices();
  p->instruments[0].sliceMode = static_cast<uint8_t>(SliceMode::Note);
  p->instruments[0].loop = static_cast<uint8_t>(LoopMode::Forward);
  noteOn(0, 0, 61);
  TEST_ASSERT_INT_WITHIN(1, 100, renderSamples(out, 1024));
}

void test_slice_note_mode_reverse_inside_slice() {
  sampleInstr(400, kSynthRate);
  fourSlices();
  p->instruments[0].sliceMode = static_cast<uint8_t>(SliceMode::Note);
  p->instruments[0].reverse = true;
  noteOn(0, 0, 61);
  TEST_ASSERT_INT_WITHIN(1, 100, renderSamples(out, 1024));
  TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[199]), out[0]);
}

void test_slice_fx_mode_slc_picks_slice_with_pitch() {
  sampleInstr(400, kSynthRate);
  fourSlices();
  p->instruments[0].sliceMode = static_cast<uint8_t>(SliceMode::Fx);
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::SLC, 1);
  noteOn(0, 0, 72);  // octave up: 100 frames in 50 samples
  TEST_ASSERT_INT_WITHIN(1, 50, renderSamples(out, 1024));
  TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[100]), out[0]);
}

void test_slice_fx_mode_without_slc_plays_whole_region() {
  sampleInstr(400, kSynthRate);
  fourSlices();
  p->instruments[0].sliceMode = static_cast<uint8_t>(SliceMode::Fx);
  noteOn(0, 0, 60);
  TEST_ASSERT_INT_WITHIN(1, 400, renderSamples(out, 1024));
}

void test_slice_fx_mode_slc_out_of_range_plays_whole_region() {
  sampleInstr(400, kSynthRate);
  fourSlices();
  p->instruments[0].sliceMode = static_cast<uint8_t>(SliceMode::Fx);
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::SLC, 9);
  noteOn(0, 0, 60);
  TEST_ASSERT_INT_WITHIN(1, 400, renderSamples(out, 1024));
}

void test_slice_mode_without_slices_plays_whole_region() {
  sampleInstr(400, kSynthRate);
  p->instruments[0].sliceMode = static_cast<uint8_t>(SliceMode::Note);
  noteOn(0, 0, 65);  // count 0: like OFF (pitched)
  TEST_ASSERT_TRUE(renderSamples(out, 1024) > 0);
}

void test_slc_ignored_when_slice_mode_off() {
  sampleInstr(400, kSynthRate);
  fourSlices();
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::SLC, 2);
  noteOn(0, 0, 60);
  TEST_ASSERT_INT_WITHIN(1, 400, renderSamples(out, 1024));
}
```

Register them in `main()`. Note `ArrayBank::data` is 400 frames — fine.

**Step 2:** `pio test -e native -f test_synth` → FAIL (SLC/SliceMode compile or wrong output).

**Step 3: implement.**
- `TrackRt`: `bool slcSet; uint8_t slc;  // SLC: slice of the next SAMPLE note-on`. Reset with ofs: `r.slcSet = false` at both places where `r.ofsSet = false` (constructor/reset ~line 60 and step start ~line 151).
- `fx()`: `case Fx::SLC: r.slcSet = true; r.slc = val; break;`
- `Voice`: `int8_t slice = -1;  // played slice, -1 = the whole region`.
- `noteOn`, right after `TrackRt& r = rt_[track];`:

```cpp
  // Slices: NOTE maps note - root to a slice (none = silent), FX takes SLC (none = whole region).
  int slice = -1;
  bool slicePitch = false;  // NOTE: the slice plays at the root
  if (sample && m.sliceCount) {
    if (m.sliceMode == static_cast<uint8_t>(SliceMode::Note)) {
      slice = note - m.root;
      if (slice < 0 || slice >= m.sliceCount) return;
      slicePitch = true;
    } else if (m.sliceMode == static_cast<uint8_t>(SliceMode::Fx) && r.slcSet && r.slc < m.sliceCount) {
      slice = r.slc;
    }
  }
  if (sample) r.slcSet = false;
```

- pitch: `const float pitch = (slicePitch ? m.root : note) + ...` (keep transpose / fine).
- `restartSmp`: `sample && (!overlap || !v.sample || v.smp != smp || v.slice != slice)`; set `v.slice = static_cast<int8_t>(slice);` next to `v.smp = smp;` (before `startSample`).
- `startSample`: at the top

```cpp
  uint32_t from, to;
  const bool sliced = v.slice >= 0 && sliceRegion(m, v.slice, len, from, to);
  if (!sliced) { /* existing from / to computation */ }
```

and when `sliced`: `v.loopMode = 0` (LoopMode::Off; loop ignored), `lf = from`. Keep the OFS offset and reverse logic as is (they act inside [from, to)). Restructure minimally so the existing code path is unchanged when not sliced.

**Step 4:** `pio test -e native -f test_synth` → PASS; then full `pio test -e native` → all PASS.

---

### Task 7: Horizontal drag in TouchTracker

**Files:**
- Modify: `src/ui/touch.h`, `src/ui/touch.cpp`, `src/ui/screen.h`, `src/ui/app.cpp`

No native tests (Arduino code); verify by `pio run -e wt32`.

**Changes:**
- `enum class TouchType : uint8_t { Tap, LongPress, Drag, HDrag };`
- `TouchEvent` add `int16_t dx; int16_t x0; uint16_t id;` — `x0` start x of the gesture, `id` increments on every touch-down (a new gesture). Update every `ev = {...}` initializer in touch.cpp to fill all fields.
- Axis lock in `poll`: while neither `dragging_` nor `hdragging_` nor `longSent_`: `adx = |x - x0_|, ady = |y - y0_|`; `ady > kDragStart && ady >= adx` → `dragging_`; else `adx > kDragStart` → `hdragging_ = true; lastX_ = x0_;`. Keep `swiped_` (set on horizontal move > kDragStart; still suppresses Tap / LongPress). HDrag: `d = x - lastX_`; if `|d| >= kHDragStep (2)` → `lastX_ = x`, emit `{HDrag, x, y, 0, d, x0_, id_}`.
- Vertical Drag behaviour unchanged (screens keep using dy).
- `Screen`: `virtual bool wantsHDrag() const { return false; }  // HDrag events reach onTouch only if true`.
- `App::onTouch`, first thing after `dirty_ = true;`:

```cpp
  if (ev.type == TouchType::HDrag) {
    const int y0 = touch_.startY();
    if (!menu_.isOpen() && y0 >= kAreaY && y0 < kTabY && screen()->wantsHDrag()) screen()->onTouch(ev);
    return;
  }
```

**Verify:** `pio run -e wt32` → SUCCESS.

---

### Task 8: ParamList page overflow

**Files:**
- Modify: `src/ui/param_list.h`, `src/ui/param_list.cpp`

- `void setWrap(bool on) { wrap_ = on; }` (default true), member `bool wrap_ = true;`.
- `int onInput(const hw::InputEvent& ev);` returns 0, or -1 / +1 when a turn (not editing, wrap off) would move before the first / past the last row; the selection then stays. Existing callers ignore the result (no change needed).
- Comment in the class doc: "setWrap(false): turning past an end returns -1 / +1 (pages)".

**Verify:** `pio run -e wt32` → SUCCESS.

---

### Task 9: INST pages (MAIN / ENV / type / FILT / LFO)

**Files:**
- Modify: `src/ui/inst_screen.h`, `src/ui/inst_screen.cpp`

**Row reorder** so pages are contiguous ranges (constants only — the Param definitions use the names):

```cpp
  static constexpr int kName = 0, kType = 1, kVol = 2, kTranspose = 3, kFine = 4, kMode = 5, kGlide = 6,
                       kAttack = 7, kDecay = 8, kSustain = 9, kRelease = 10, kCommon = 11;
  static constexpr int kMainRows = 7;  // MAIN = [0, 7), ENV = [7, kCommon)
  // tail: FILT = [0, 7), LFO = [7, kTailRows)
  static constexpr int kFiltRows = 7;
```

SAMPLE: the type's own rows move to SampleEditor (Task 10), so `sample_` becomes `Param sample_[kCommon + kTailRows]` and its type page is the editor. Remove the sample row constants, `kWaveH`, `kWaveGap`, `kSampleVisibleRows`, the wave cache members, `updateWave`, `drawWave`, `bankIndex`, `sampleMissing` from InstScreen (they move to SampleEditor).

**Pages:**
- `enum Page : int { kPgMain, kPgEnv, kPgType, kPgFilt, kPgLfo, kPages };` member `int page_ = kPgMain;`
- `kPageBarH = 24`, `kListRows = 9` ((272 - 28 - 24) / 24).
- `Param* typeRows()` (array of the current type) and `int typeCount()` (own rows: CHIP 4, FM/DRUM kMacRows - kCommon, SAMPLE 0).
- `void showPage(int page, bool last)`: `page_ = (page % kPages + kPages) % kPages`; for list pages `list_.setParams(base + off, count)`, `list_.setVisibleRows(kListRows)`, `list_.setWrap(false)`, `list_.setSel(last ? count - 1 : 0)`, `leaveEdit()`. Ranges: MAIN `rows + 0, 7`; ENV `rows + 7, 4`; type `rows + kCommon, typeCount()`; FILT `rows + kCommon + typeCount(), 7`; LFO `... + 7, 4`. SAMPLE type page → `editor_.enter(last)`.
- `syncParams()` (type change) re-runs `showPage(page_, false)`; changing instruments keeps `page_`.
- `nameEdit()`: `page_ == kPgMain && list_.editing() && list_.sel() == kName`.
- Encoder: active editing = `onEditor() ? editor_.editing() : list_.editing()` (onEditor = SAMPLE && page_ == kPgType). Shift+turn changes the instrument only when not active-editing (as now). Otherwise `int ov = onEditor() ? editor_.onInput(ev) : list_.onInput(ev); if (ov) showPage(page_ + ov, ov < 0);`.
- EncLong: preview — note `onEditor() ? editor_.previewNote() : kPreviewNote`.
- Touch: header as now; tap in the page bar (`y0_ + kHeaderH .. + kPageBarH`) → `showPage(x / (kScreenW / kPages), false)`; below → editor or list.
- `bool wantsHDrag() const override { return !presets_.isOpen() && onEditor(); }`
- Draw: header, page bar (5 cells of 96 px; active `kPlayBg` with `kCursor` text, others `kBeatBg` with `kDim` text; labels MAIN, ENV, OSC/SMPL/FM/DRUM, FILT, LFO), then `editor_.draw(s, y)` or `list_.draw(s, y)`.
- ENV page: ADSR mini graph in `x 280..470`, `y list top + 8 .. + 112`: A, D, R segment widths ∝ `log2(1 + envTimeMs)`, sustain plateau fixed 30 px, level `sustain / 127`; polyline `kText` (`kDim` when the rows are dim: DRUM, or FM drum machines). Draw after the list so it overlays the empty right side (values end before x 280).

**Verify:** `pio run -e wt32` → SUCCESS.

---

### Task 10: SampleEditor — view, zoom, rows

**Files:**
- Create: `src/ui/sample_editor.h`, `src/ui/sample_editor.cpp`
- Modify: `src/ui/inst_screen.h/.cpp` (member `SampleEditor editor_{app_};`, `editor_.bind(instr_)` in onEnter / changeInstr / afterPresets / onProjectReplaced)

**Layout** (y = page area top, 220 px): wave `kWaveH = 148`, toolbar `kToolH = 24`, list 2 rows (48).

**Class:**

```cpp
// SAMPLE page of InstScreen: waveform with markers (Start green, End red, Loop yellow, slices
// cyan), zoom, toolbar (< > - + CHOP CLR) and the sample rows. Marker row: click = edit, turn =
// move the selected marker 1 px of the zoom, Shift+turn = to the next / previous transient.
// Touch: tap = nearest marker, drag a marker = move it (Shift: snap to a transient), drag elsewhere
// = scroll (zoomed), long press = new slice (Shift: at the nearest transient).
class SampleEditor {
 public:
  explicit SampleEditor(App& app);
  void bind(int instr);         // instrument index; resets zoom when the sample data changes
  void enter(bool last);        // page shown: selection on the first / last row
  int onInput(const hw::InputEvent& ev);  // -1 / +1: turned past the first / last row
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y);
  bool editing() const { return list_.editing(); }
  void leaveEdit() { list_.setEdit(false); }
  uint8_t previewNote();        // NOTE mode with a slice selected: root + slice, else C4
 ...
};
```

**Rows** (`Param rows_[kRows]`, list with `setVisibleRows(2)`, `setWrap(false)`):
`Marker` (format: `S 12.3%` / `E 80.0%` / `L 50.0%` / `#3 41.2%` — position as % of the whole sample; `edit` = nullptr, handled in onInput), `Sample` (moved from InstScreen; additionally `mt::sliceClear(inst())` when the sample changes), `Root`, `Start`, `End`, `Loop`, `Loop start`, `Reverse` (moved verbatim with `toPermille` / `fromPermille` / `permille` helpers), `Slices` (OFF / NOTE / FX), `Chop` (EQUAL / TRANS), `Amount` (label set before draw: `Chop N` → `"%u"` 2..32, or `Sens` → `"%u%%"` 0..100).

**Markers:** ids `kS = 0, kE = 1, kL = 2, kSlice0 = 3` (+ i). `uint32_t markerFrame(int id)`: S/E/slices via `fracToFrame`; L = from + loopStart · span / 0xFFFF, mirrored when reverse (as Synth::startSample). `void setMarkerFrame(int id, uint32_t f)` under `engine::lockProject()`: S clamped to [0, E], E to [S, len], L inverse of the above (clamp to the region), slice via `sliceMove`; then `app_.markDirty()`. Visible markers: S, E, L only if loop ≠ OFF, all slices. ◀ / ▶ pick the previous / next visible marker by frame order.

**View / zoom:** `zoom_` 0..zMax with `viewLen = max(kScreenW, len >> zoom_)`, zMax = largest z with `len >> z >= kScreenW`; `viewStart` clamped to `[0, len - viewLen]`; `−` / `+` change zoom centred on the selected marker. `frameAt(x) = viewStart + x * viewLen / kScreenW`, `xOf(frame)`. After moving a marker off screen, scroll so it is visible.

**Wave cache:** move `updateWave` from InstScreen, keyed additionally by `viewStart_, viewLen_`; columns over the view (probe stride as now, 1 frame per column at full zoom). Draw: background `kBeatBg`; columns inside Start..End `kText`, else `kDim`; markers as vertical lines, the selected one 3 px wide; slice color `kCyan = 0x07FF` (add to theme.h); slice numbers (1-based) at the top in `kCyan` when ≥ 24 px from the previous label; zoom label `x4` / `1:1` in the top-right corner in `kDim`. Missing / no sample → "MISSING" / "NO SAMPLE" as now; toolbar still drawn.

**Toolbar:** six 80 px buttons `<`, `>`, `-`, `+`, `CHOP`, `CLR` (`kPlayBg` fill, `kCursor` text, like the header buttons).

**Verify:** `pio run -e wt32` → SUCCESS.

---

### Task 11: SampleEditor — editing, onsets, chop

**Files:**
- Modify: `src/ui/sample_editor.h/.cpp`

- Onset cache: `mt::Onset onsets_[mt::kMaxOnsets]; int nOnsets_;` keyed by data / frames / bank generation; `ensureOnsets()` runs `mt::detectOnsets(data, frames, rate, ...)` lazily (first draw of the page, snap, CHOP TRANS).
- Encoder (`onInput`): if `list_.sel() == kMarkerRow && list_.editing() && ev.type == EncTurn`: `ev.shift` → repeat |delta| times `stepOnset(cur, sign)` and move there (no onset → stay); else move by `delta * max(1, viewLen / kScreenW)` frames. Otherwise `return list_.onInput(ev)`.
- Touch on the wave (`y` in wave rows):
  - `Tap`: nearest visible marker with `|xOf(frame) - x| <= 16` → select it (`list_.setSel(kMarkerRow)`).
  - `HDrag`: on a new gesture (`ev.id != dragId_`) decide the mode: a visible marker within 16 px of `ev.x0` → select it, mode Move; else mode Scroll if zoomed, else None. Move: `f = frameAt(ev.x)`; `app_.shift()` → `nearestOnset` frame if any; `setMarkerFrame(sel_, f)`. Scroll: `viewStart -= ev.dx * viewLen / kScreenW` (clamped).
  - `LongPress`: `f = frameAt(x)` (Shift → nearest onset), `sliceInsert(frameToFrac(f, len))` under lock, select it; full or taken → ignore.
- Toolbar taps: `<` / `>` marker selection; `-` / `+` zoom; `CHOP`: under lock, `chopMode == Equal ? chopEqual : chopTransients(onsets_, ...)`, markDirty, select slice 0; `CLR` tap: remove the selected slice (select previous marker); `CLR` long press: `sliceClear`, select S.
- List rows below: `list_.onTouch(ev)` (Tap / Drag).
- `previewNote()`: `sliceMode == NOTE && sel_ >= kSlice0` → `clamp(root + sel_ - kSlice0, 0, 127)`, else 60.
- `bind()`: instrument changed or data changed → `zoom_ = 0`, `viewStart_ = 0`, `sel_ = kS`; also clamp `sel_` when slices were removed elsewhere (sel_ ≥ kSlice0 + sliceCount → kS).

**Verify:** `pio run -e wt32` → SUCCESS; `pio test -e native` → all PASS.

---

### Task 12: Manual, final checks, memory

**Files:**
- Modify: `docs/manual.html` (INST: pages; SAMPLE editor: markers, zoom, chop, slices, Shift snap; fx list + table row `SLC 0–31 INT "Слайс следующей ноты SAMPLE (Slices = FX)"`; the SLD..RES sentence → include SLC)
- Modify: `src/ui/inst_screen.h` class comment (pages)

**Steps:**
1. `pio test -e native` → all PASS (report the count).
2. `pio run -e wt32` → SUCCESS (report flash / RAM usage).
3. Update memory `project-status.md` with a dated line (implemented, test count, not verified on hardware, not committed).
