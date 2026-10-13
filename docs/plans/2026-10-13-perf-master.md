# PERF on the master — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use executing-plans (or subagent-driven-development) to implement this plan task-by-task.

**Goal:** PERF buttons become punch-in effects on the whole internal mix (PO-33 / EP-133 style), the DJ filter moves into PERF, the encoder sets the depth of the last pressed held effect, several held effects work together.

**Architecture:** Read `docs/plans/2026-10-13-perf-master-design.md` first. A pure `PerfStack` (held slots, depths, last pressed, active buffer effect) is shared by the UI (toasts, header) and the synth. `PerfBus` (lib/core) owns the PSRAM ring buffer and all DSP, sitting where `Synth::djFilter` was. The UI sends press / release / turn / all-off to the audio task as a synth message `0xF6 op val` on the preview track through the existing UI ring (`audio::perf`). The sequencer loses its per-track PERF code.

**Tech Stack:** C++17, PlatformIO (ESP32-S3 `env:wt32`, host tests `env:native` with Unity).

**Commits:** per the project rule, no per-task commits. Everything is committed once after the device check (Task 13).

Run all native tests with `pio test -e native`; one suite with `pio test -e native -f test_perf`.

---

### Task 1: PerfFx enum, tables, names, values

**Files:**
- Create: `lib/core/src/perf_fx.h`, `lib/core/src/perf_fx.cpp`
- Modify: `lib/core/src/model.h:284-291` (old `PerfFx` enum goes, include `perf_fx.h`; `perfMap` default), `lib/core/src/model.cpp:84`
- Modify: `lib/core/src/fx_info.h:16`, `lib/core/src/fx_info.cpp:237-244` (remove `perfFxName`)
- Test: `test/test_perf/test_main.cpp` (new), fix `test/test_fx_info/test_main.cpp:291-294`, `test/test_synth_crush/test_main.cpp:50` (delete those perf asserts)

**Step 1: Write the failing tests** (`test/test_perf/test_main.cpp`)

```cpp
#include <unity.h>
#include <string.h>
#include "perf_fx.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_names_and_short_names() {
  TEST_ASSERT_EQUAL_STRING("REPEAT", perfName(PerfFx::Repeat));
  TEST_ASSERT_EQUAL_STRING("FREEZE", perfName(PerfFx::Freeze));
  TEST_ASSERT_EQUAL_STRING("", perfName(PerfFx::None));
  TEST_ASSERT_EQUAL_STRING("", perfName(PerfFx::Count));
  TEST_ASSERT_EQUAL_STRING("RPT", perfShort(PerfFx::Repeat));
  TEST_ASSERT_EQUAL_STRING("FLT", perfShort(PerfFx::Filter));
}

void test_defaults_and_ranges() {
  TEST_ASSERT_EQUAL(0, perfRange(PerfFx::Filter).def);
  TEST_ASSERT_EQUAL(-64, perfRange(PerfFx::Filter).lo);
  TEST_ASSERT_EQUAL(63, perfRange(PerfFx::Filter).hi);
  TEST_ASSERT_EQUAL(6, perfRange(PerfFx::Crush).def);
  TEST_ASSERT_EQUAL(100, perfRange(PerfFx::DelayThrow).def);
  for (int f = 1; f < static_cast<int>(PerfFx::Count); ++f) {
    const PerfRange r = perfRange(static_cast<PerfFx>(f));
    TEST_ASSERT_TRUE(r.lo <= r.def && r.def <= r.hi);
  }
}

void test_values() {
  char b[16];
  perfValue(PerfFx::Repeat, perfRange(PerfFx::Repeat).def, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("1/16", b);
  perfValue(PerfFx::Filter, 0, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("OFF", b);
  perfValue(PerfFx::Filter, -20, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("LP 20", b);
  perfValue(PerfFx::Filter, 20, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("HP 20", b);
  perfValue(PerfFx::Crush, 6, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("6 BIT", b);
  perfValue(PerfFx::Reverse, 2, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("1 BAR", b);
  perfValue(PerfFx::Downsample, 1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("8 KHZ", b);
  perfValue(PerfFx::Freeze, 50, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("DRY 50", b);
}

void test_legacy_map() {
  // Old enum: None Rat2 Rat4 FltLow FltHigh DlyMax Crush Fade Mute DecShort Rat3 Rat8 RatUp RvbMax Srr Drive
  const PerfFx want[16] = {PerfFx::None,       PerfFx::Repeat, PerfFx::Repeat, PerfFx::Filter,
                           PerfFx::Filter,     PerfFx::DelayThrow, PerfFx::Crush, PerfFx::None,
                           PerfFx::None,       PerfFx::None,   PerfFx::Repeat, PerfFx::Repeat,
                           PerfFx::Roll,       PerfFx::ReverbThrow, PerfFx::Downsample, PerfFx::Drive};
  for (int i = 0; i < 16; ++i) TEST_ASSERT_EQUAL(static_cast<int>(want[i]), static_cast<int>(perfFromLegacy(i)));
  TEST_ASSERT_EQUAL(0, static_cast<int>(perfFromLegacy(200)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_names_and_short_names);
  RUN_TEST(test_defaults_and_ranges);
  RUN_TEST(test_values);
  RUN_TEST(test_legacy_map);
  return UNITY_END();
}
```

**Step 2:** `pio test -e native -f test_perf` — FAIL (`perf_fx.h` missing).

**Step 3: Implement** `perf_fx.h`:

```cpp
#pragma once
#include <stdint.h>

namespace mt {

// Punch-in effects on the master (PERF mode); Project::perfMap says which one each track button
// holds. File format: PRF2 bytes, append only.
enum class PerfFx : uint8_t {
  None, Repeat, Roll, Reverse, Scratch, TapeStop, Gate, Filter, Crush, Downsample, Drive, DelayThrow,
  ReverbThrow, Freeze, Count
};
constexpr int kPerfButtons = 8;

// Buffer effects share the ring buffer's read head: one plays at a time.
constexpr bool perfBuffered(PerfFx f) { return f >= PerfFx::Repeat && f <= PerfFx::TapeStop; }

// Encoder range of an effect's depth; every press starts at def.
struct PerfRange {
  int8_t lo, hi, def;
  bool coarse;  // Shift turns x10
};
PerfRange perfRange(PerfFx f);
const char* perfName(PerfFx f);   // toast / PROJ text, "" for None and out of range
const char* perfShort(PerfFx f);  // 3 letters for the GRID header
void perfValue(PerfFx f, int depth, char* out, int n);  // toast value: "1/16", "LP 20", "6 BIT"
PerfFx perfFromLegacy(uint8_t old);  // PRFM byte (pre-2026-10-13 enum) -> the new effect

// Lengths behind the list-valued depths, in steps (sixteenths).
float perfRepeatSteps(int depth);   // Repeat: 1/2 1/4 1/8 1/8T 1/16 1/16T 1/32
float perfGateSteps(int depth);     // Gate:   1/4 1/8 1/8T 1/16 1/16T 1/32
float perfReverseSteps(int depth);  // Reverse: 1/4 1/2 1 2 bars
float perfScratchSteps(int depth);  // Scratch period: 1/4 1/8 1/16 1/32
float perfTapeSteps(int depth);     // Tape stop: 1/16 1/8 1/4 1/2 1 bar
int perfDownHz(int depth);          // 16000 8000 4000 2000 1000 500

}  // namespace mt
```

`perf_fx.cpp` — tables, in enum order:

| fx | lo | hi | def | coarse |
|---|---|---|---|---|
| Repeat | 0 | 6 | 4 | no |
| Roll | 1 | 4 | 2 | no |
| Reverse | 0 | 3 | 2 | no |
| Scratch | 0 | 3 | 1 | no |
| TapeStop | 0 | 4 | 2 | no |
| Gate | 0 | 5 | 3 | no |
| Filter | -64 | 63 | 0 | yes |
| Crush | 1 | 12 | 6 | no |
| Downsample | 0 | 5 | 1 | no |
| Drive | 0 | 100 | 60 | yes |
| DelayThrow | 0 | 127 | 100 | yes |
| ReverbThrow | 0 | 127 | 100 | yes |
| Freeze | 0 | 100 | 50 | yes |

Names: `REPEAT ROLL REVERSE SCRATCH "TAPE STOP" GATE FILTER CRUSH DOWNSAMPLE DRIVE "DELAY THROW" "REVERB THROW" FREEZE`; shorts `RPT ROL REV SCR TPS GAT FLT CRS DWN DRV DLY RVB FRZ` (static_assert on table sizes like `fx_info.cpp:241`).
Values: list effects print their list label (`1/8T`, `1 BAR`, `2 BAR`, `1/2 BAR`, `1/4 BAR`); Roll `N 2`; Filter `OFF` / `LP n` / `HP n`; Crush `n BIT`; Downsample `16 KHZ` … `500 HZ`; Drive / throws plain number; Freeze `DRY n`. Clamp depth into the range first.
Legacy table exactly as in the test.

`model.h`: delete the old `PerfFx` enum and `kPerfButtons`, `#include "perf_fx.h"`; default
`perfMap = {Repeat, Roll, Filter, Gate, DelayThrow, ReverbThrow, Crush, TapeStop}` (as uint8_t), and the same in `model.cpp:84` (replace the `i + 1` loop with a copy of a constexpr table in `perf_fx.h`: `constexpr uint8_t kPerfDefaultMap[kPerfButtons]`).
Remove `perfFxName` from `fx_info.*`; its users get fixed in Tasks 8 and 10 (expect build breaks until then in `src/` — native tests do not build `src/`).

**Step 4:** `pio test -e native -f test_perf` — PASS. `pio test -e native -f test_fx_info` and `-f test_synth_crush` — PASS after removing the perf asserts.

---

### Task 2: Project file — PRF2, legacy PRFM, DJ filter out

**Files:**
- Modify: `lib/core/src/model.h:447-449` (delete `djFilter`), `lib/core/src/model.cpp:83`
- Modify: `lib/core/src/project_io.cpp:28` (comment), `:289` (byte 13 ignored), `:370-376` (`readPrfm` maps legacy), add `readPrf2` + its chunk dispatch next to PRFM, `:590` (write 64), `:646` (write `PRF2` instead of `PRFM`)
- Modify: `src/storage/storage.cpp:82` (delete the djFilter copy)
- Test: `test/test_project_io/test_main.cpp:87,186`

**Step 1: Tests** — in `test_project_io`:
- change `:87` to `p.perfMap[3] = static_cast<uint8_t>(PerfFx::ReverbThrow);` (roundtrip keeps working through PRF2);
- add `test_prfm_legacy_mapped`: build a file with a hand-written `PRFM` chunk `{1,2,3,4,5,6,7,8}` (copy how other legacy-chunk tests in this file build raw chunks) and assert the loaded `perfMap` is `{Repeat, Repeat, Filter, Filter, DelayThrow, Crush, None, None}`;
- add `test_prf2_wins_over_prfm`: both chunks present → PRF2 values;
- add `test_audi_dj_byte_ignored`: a file whose AUDI byte 13 is 10 loads fine, and a saved file has byte 13 = 64.

**Step 2:** `pio test -e native -f test_project_io` — FAIL.

**Step 3: Implement.** `readPrfm`: `p.perfMap[i] = static_cast<uint8_t>(perfFromLegacy(b[i]))`, but only when no PRF2 was read (keep a `bool prf2Seen` in the load context, or read PRF2 after PRFM in chunk order — the writer puts PRF2 where PRFM was, so old files only ever have PRFM; set a flag anyway). `readPrf2`: values `< PerfFx::Count` else 0. Writer: `o.chunk("PRF2", kPerfButtons)`. AUDI byte 13: not read; written `64` with a comment `// byte 13: the old DJ filter, neutral for older firmware`.

**Step 4:** `pio test -e native -f test_project_io` — PASS.

---

### Task 3: PerfStack — held slots, depths, last pressed

**Files:**
- Modify: `lib/core/src/perf_fx.h/.cpp`
- Test: `test/test_perf/test_main.cpp`

**Step 1: Tests**

```cpp
void test_stack_press_turn_release() {
  PerfStack s;
  s.press(PerfFx::Filter);
  TEST_ASSERT_EQUAL(0, s.depth(PerfFx::Filter));
  s.turn(-20);
  TEST_ASSERT_EQUAL(-20, s.depth(PerfFx::Filter));
  s.turn(-100);
  TEST_ASSERT_EQUAL(-64, s.depth(PerfFx::Filter));  // clamped
  s.release(PerfFx::Filter);
  TEST_ASSERT_FALSE(s.held(PerfFx::Filter));
  s.press(PerfFx::Filter);
  TEST_ASSERT_EQUAL(0, s.depth(PerfFx::Filter));  // every press starts at the default
}

void test_stack_last_pressed_gets_the_encoder() {
  PerfStack s;
  s.press(PerfFx::Crush);
  s.press(PerfFx::Drive);
  s.turn(5);
  TEST_ASSERT_EQUAL(6, s.depth(PerfFx::Crush));
  TEST_ASSERT_EQUAL(65, s.depth(PerfFx::Drive));
  s.release(PerfFx::Drive);
  TEST_ASSERT_EQUAL(static_cast<int>(PerfFx::Crush), static_cast<int>(s.last()));
}

void test_stack_buffer_effects_one_at_a_time() {
  PerfStack s;
  s.press(PerfFx::Repeat);
  s.press(PerfFx::Gate);
  s.press(PerfFx::Reverse);
  TEST_ASSERT_EQUAL(static_cast<int>(PerfFx::Reverse), static_cast<int>(s.buffered()));
  s.release(PerfFx::Reverse);
  TEST_ASSERT_EQUAL(static_cast<int>(PerfFx::Repeat), static_cast<int>(s.buffered()));
  s.releaseAll();
  TEST_ASSERT_EQUAL(static_cast<int>(PerfFx::None), static_cast<int>(s.buffered()));
  TEST_ASSERT_EQUAL(0, s.count());
}

void test_stack_same_fx_twice_is_one_slot() {
  PerfStack s;  // two buttons mapped to the same effect
  s.press(PerfFx::Crush);
  s.press(PerfFx::Crush);
  TEST_ASSERT_EQUAL(1, s.count());
}
```

**Step 2:** FAIL.

**Step 3: Implement** in `perf_fx.h`:

```cpp
// The held effects in press order (the last one gets the encoder) with their depths.
class PerfStack {
 public:
  void press(PerfFx f);           // to the top at its default depth (again: back to the default)
  void release(PerfFx f);
  void releaseAll() { n_ = 0; }
  void turn(int delta);           // the last pressed, clamped to its range
  bool held(PerfFx f) const;
  int depth(PerfFx f) const;      // default when not held
  PerfFx last() const { return n_ ? slot_[n_ - 1].fx : PerfFx::None; }
  PerfFx buffered() const;        // the newest held buffer effect, None if none
  int count() const { return n_; }
  PerfFx at(int i) const { return slot_[i].fx; }  // press order

 private:
  struct Slot {
    PerfFx fx;
    int8_t depth;
  };
  Slot slot_[kPerfButtons];
  int n_ = 0;
};
```

Ignore `None` / out-of-range in `press`. `press` of a held effect removes it and pushes it on top at the default.

**Step 4:** PASS.

---

### Task 4: PerfBus skeleton, grid clock, tone chain (FILTER, CRUSH, DOWNSAMPLE, DRIVE, GATE)

**Files:**
- Create: `lib/core/src/synth_perf.h`, `lib/core/src/synth_perf.cpp`
- Test: `test/test_perf/test_main.cpp`

The bus works on the synth's float mix (nominal ±4 at full scale: MAIN 100 % multiplies by 0.25 before the soft clip).

**API (`synth_perf.h`):**

```cpp
#pragma once
#include <stdint.h>
#include "perf_fx.h"
#include "synth_filter.h"
#include "synth_drive.h"

namespace mt {

// Master punch-in effects (PERF): after the compressor and the key track, before MAIN. Writes the
// mix into a stereo ring buffer (int16, caller-owned, PSRAM on the device) for the buffer effects.
// Nothing held: the mix is untouched (bit-exact) and only the buffer is written.
class PerfBus {
 public:
  static constexpr int kXfade = 64;  // 2 ms crossfades: effect in / out, loop seams
  static constexpr float kScale = 8191.f;  // float mix +-4 <-> int16

  // frames stereo frames (buf holds 2 x frames int16), nullptr = buffer effects silent. Clears it.
  void setBuffer(int16_t* buf, uint32_t frames);
  void press(PerfFx f);
  void release(PerfFx f);
  void turn(int delta);
  void releaseAll();
  const PerfStack& stack() const { return stack_; }
  // Grid: 0xFA restarts it at step 0; it advances only while running (not paused, playing).
  void transportStart();
  // Throws and freeze, read by the synth for this block (before delay / reverb).
  float delaySend() const;   // 0..1, x the mono mix into the delay send
  float reverbSend() const;  // 0..1
  bool freeze() const { return stack_.held(PerfFx::Freeze); }
  float freezeDry() const;   // live mix gain under a frozen reverb, 0..1
  // The return levels stay up for a while after a throw / freeze ends (its tail).
  bool sendTail() const { return tail_ > 0; }
  void process(float* l, float* r, int n, float samplesPerStep, bool running);

 private:
  ...
};

}  // namespace mt
```

**Step 1: Tests** (helper: fill L/R with a sine, run `process` in 128-sample blocks, `samplesPerStep = 2000`):

```cpp
void test_bus_idle_is_bit_exact();          // nothing held: output == input, with and without a buffer
void test_bus_filter_zero_is_passthrough(); // FILTER held at 0: bit-exact
void test_bus_filter_lp_cuts_highs();       // depth -50: an 8 kHz sine loses > 20 dB, a 100 Hz one < 3 dB
void test_bus_filter_hp_cuts_lows();        // depth +40: 100 Hz loses > 20 dB
void test_bus_crush_quantizes();            // 2 bits: outputs only take <= 4 distinct values (after the xfade)
void test_bus_downsample_holds();           // 500 Hz: runs of 64 equal samples
void test_bus_gate_follows_grid();          // 1/16 (one step): first half of a step ~input, second half ~0 (after 32-sample ramps)
void test_bus_release_returns_live();       // press CRUSH, release, 2 blocks later output == input
void test_bus_stack_filter_crush_gate();    // all three held: output differs from each alone and stays finite
```

Write each with concrete numbers as in the comments (RMS helper over the second half of the buffer to skip transients).

**Step 2:** FAIL.

**Step 3: Implement.**
- `stack_`, `gridPos_` (double, steps since `transportStart`, advanced by `n / samplesPerStep` per block when `running`).
- Wet amount per tone effect: a `fade_` 0..1 per effect stepping by `1/kXfade` per sample towards held/released, output `x + (fx(x) − x)·fade`. Keep effect state (Svf, hold counters) while fading out; reset when `fade_` reaches 0 so the next press starts clean.
- FILTER: the old `Synth::djFilter` math moved here verbatim (`synth.cpp:1312-1328`): `a = v<0 ? -v/64 : v/63`, LP `20000·(100/20000)^a`, HP `20·(8000/20)^a`, Q 0.9, `Svf` per channel. Depth 0 = bypass (filter reset).
- CRUSH: on `x·0.25` (±1): `step = 2/(1 << (bits−1))`, round to nearest (no `floorf`, as in `synth_crush.h`), back ×4.
- DOWNSAMPLE: hold every `round(32000 / perfDownHz(depth))` samples (both channels together).
- DRIVE: `Drive::set(depth·127/100)` on `x·0.25`, ×4 back.
- GATE: `len = perfGateSteps(depth)`; open while `frac(gridPos / len) < 0.5`; gain moves towards 0/1 by `1/32` per sample.
- Order: buffer stage (Task 5, pass-through for now) → GATE → FILTER → CRUSH → DOWNSAMPLE → DRIVE.
- `delaySend/reverbSend`: `depth/127` while held, else 0. `freezeDry`: `depth/100`. `tail_`: set to 4 s of samples (128000) whenever a throw or freeze is held, counted down by `n` otherwise.
- The whole `process` returns at once (after writing the buffer) when the stack is empty and all fades are 0.

**Step 4:** PASS.

---

### Task 5: Ring buffer, REPEAT, ROLL

**Files:** `lib/core/src/synth_perf.h/.cpp`, `test/test_perf/test_main.cpp`

**Step 1: Tests** (`samplesPerStep = 1000`, buffer 64000 frames, input = a ramp / unique-per-sample signal so positions are checkable):

```cpp
void test_repeat_loops_the_last_step();
// play 3.5 steps of a ramp, press REPEAT (1/16 = 1 step): after the 64-sample fade-in, the output
// repeats input[3000..4000) (the step started at sample 3000) with period 1000, within int16 rounding
// (tolerance 1/kScale x 4).
void test_repeat_seam_is_smooth();
// sine input whose period does not divide the loop: the max |out[i] - out[i-1]| around every seam
// stays below 3x the sine's own max slope.
void test_repeat_length_change_at_next_loop();
// turn to 1/8 mid-loop: the current loop finishes at 1000, the next lasts 2000.
void test_roll_halves();
// ROLL N=1: loop lengths 2000, 1000, 500, 250, 125, 125 ...
void test_buffer_release_returns_live();
// release REPEAT: after kXfade samples output == input.
void test_two_buffer_fx_last_wins_and_returns();
// REPEAT held, press REVERSE (Task 6 may stub it as REPEAT until then; write this test in Task 6).
```

**Step 2:** FAIL.

**Step 3: Implement.**
- Buffer: interleaved int16, write index `w_` (frames). Every sample: write the bus *input* (live, before any effect), clamped to ±4 then ×`kScale`. Writing never stops.
- `struct Head { PerfFx fx; double pos; double start; double len; double speed; int loops; }` — reads `buf` at `pos` (frames, wrapped; linear interpolation for fractional pos), advances by `speed`.
- Active head `head_`, outgoing head `out_` with `xf_` counter: on any source switch (live → head, head → head, head → live, loop seam) the old source keeps running for `kXfade` samples, linear crossfade. Live is source `None`.
- `samplesSinceStep = (gridPos − floor(gridPos)) · samplesPerStep`.
- REPEAT press: `start = w − samplesSinceStep`, `len = perfRepeatSteps(depth)·samplesPerStep`, `pos = start`, speed 1. At `pos ≥ start + len`: seam — `out_` = copy of the head (keeps reading past the end, valid past audio), head `pos = start`, `len` recomputed from the current depth.
- ROLL: like REPEAT with `len = 2 steps` first; after every `depth` loops `len /= 2`, floor 0.25 step.
- Clip every length to `frames − 2·kBlock` (the head must stay behind `w_`).
- Changing `stack_.buffered()` (press / release) switches heads with a crossfade; releasing the last buffer effect crossfades back to live.

**Step 4:** PASS.

---

### Task 6: REVERSE, SCRATCH, TAPE STOP

**Files:** `lib/core/src/synth_perf.h/.cpp`, `test/test_perf/test_main.cpp`

**Step 1: Tests** (ramp input, `samplesPerStep = 1000`):

```cpp
void test_reverse_reads_backwards();   // press at w=10000, window 1/4 bar (4 steps): out[k] ~ in[9999 - k] for k < 4000, then loops
void test_scratch_stays_in_the_past(); // positions read never exceed the write index; output not equal to input
void test_tape_stop_slows_to_silence();// 1/4 bar: speed falls linearly over 4000 samples; after that output is 0
void test_two_buffer_fx_last_wins_and_returns(); // REPEAT then REVERSE held: reverse plays; release REVERSE -> repeat again (new capture at that moment)
```

**Step 2:** FAIL.

**Step 3: Implement.**
- REVERSE: `start = w` at press, `speed = −1`, `pos = start − 1`; at `pos < start − len` (len = `perfReverseSteps(depth)·samplesPerStep`, clipped): seam back to `start − 1`.
- SCRATCH: `T = perfScratchSteps(depth)·samplesPerStep`, `A = T/π` (peak speed 2); `pos(t) = start − A + A·sin(2π t/T)`, `start = w` at press; `t` counts samples. Max read `start` + 0 ≤ w (w grows) — always the past.
- TAPE STOP: `T = perfTapeSteps(depth)·samplesPerStep`; `speed` from 1 down by `1/T` per sample to 0; `pos += speed`; once 0 the head outputs silence (fade over `kXfade`). Start: `pos = w`.
- A buffer effect returning after another's release captures afresh at that moment (no resume).

**Step 4:** PASS. Run the whole suite `pio test -e native -f test_perf`.

---

### Task 7: Synth integration, throws, freeze

**Files:**
- Modify: `lib/core/src/synth.h:108` (`kProfDj` → `kProfPerf`), `:261-262` (Svf pair + `djFilter` → `PerfBus perf_`), public `setPerfBuffer(int16_t*, uint32_t)`, comment block above `class Synth` (document `0xF6`)
- Modify: `lib/core/src/synth.cpp:238-243` (`0xF6` dispatch), `:1144`-ish `transportStart` (call `perf_.transportStart()`), `:1312-1328` (delete `djFilter`), `:1488-1502` (chain), `:1539` (profile name `"perf"`)
- Modify: `lib/core/src/synth_reverb.h/.cpp` (`process(..., bool freeze)`)
- Modify: `src/audio/audio.h:24` `kProfStages` unchanged (same count)
- Test: `test/test_synth/test_main.cpp`, `test/test_synth_reverb/test_main.cpp`

**Message:** `0xF6 op val` on any synth track (the UI uses `kPreviewTrack`): op 0 press `PerfFx val`, 1 release `val`, 2 turn `int8_t val`, 3 release all. In `Synth::apply` add `else if (e.b[0] == 0xF6) { if (e.len >= 3) perfMsg(e.b[1], e.b[2]); }`.

**Chain** (replace `synth.cpp:1488-1502`):
```cpp
  // PERF throws: the whole mix (mono, before delay / reverb) into the sends while held.
  if (const float ds = perf_.delaySend(), rs = perf_.reverbSend(); ds > 0.f || rs > 0.f)
    for (int i = 0; i < kBlock; ++i) {
      const float m = (mixL_[i] + mixR_[i]) * 0.5f;
      send_[i] += m * ds;
      rsend_[i] += m * rs;
    }
  const bool tail = perf_.sendTail();  // a throw / freeze (or its tail): the returns at least 100
  const uint8_t dl = tail && p_.dlyLevel < 100 ? 100 : p_.dlyLevel;
  const uint8_t rl = tail && p_.rvbLevel < 100 ? 100 : p_.rvbLevel;
  delay_.process(send_, mixL_, mixR_, kBlock, delaySamples(), p_.dlyFb, p_.dlyTone, dl);
  mark(kProfDelay);
  if (perf_.freeze()) {  // the live mix ducks under the frozen tail
    const float g = perf_.freezeDry();
    for (int i = 0; i < kBlock; ++i) {
      mixL_[i] *= g;
      mixR_[i] *= g;
    }
  }
  reverb_.process(rsend_, mixL_, mixR_, kBlock, p_.rvbSize, p_.rvbDamp, rl, perf_.freeze());
  ...comp, keyAround as now...
  perf_.process(mixL_, mixR_, kBlock, 6.f * tickSamples(), !paused_);
  mark(kProfPerf);
```
(`delaySamples` etc. unchanged; check `Delay::process` really takes the level last — `synth_delay.h:19`.) Note the throw sends are added *before* `delay_`/`reverb_` read them, the mix taken before their returns are added.

**Reverb freeze:** with `freeze`: `fb = 1`, `d = 0`, input treated as 0, never goes idle (skip the quiet counter); level as given.

**Step 1: Tests**
- `test_synth`: `test_perf_idle_bit_exact` — the same short project rendered by two synths, one with `setPerfBuffer(buf, 32000)`, the other without: identical int16 output over 50 blocks.
- `test_synth`: `test_perf_message_crush` — send `{0xF6, 0, Crush}` then render: output differs from a synth without it; `{0xF6, 3, 0}` then 2 blocks later identical again (both synths kept in step).
- `test_synth`: `test_reverb_throw_with_level_zero` — `rvbLevel = 0`, press REVERB THROW: the output gains reverb energy after the note ends (RMS of a tail window > 0).
- `test_synth_reverb`: `test_freeze_holds_tail` — feed a burst, set freeze, 3 s of zero input: the tail RMS stays within 1 dB of the RMS right after the burst.
- Fix callers of `Reverb::process` in tests (add `false`).

**Step 2:** FAIL. **Step 3:** implement. **Step 4:** `pio test -e native` — all PASS except the sequencer PERF tests (Task 8).

---

### Task 8: Sequencer and engine lose per-track PERF

**Files:**
- Modify: `lib/core/src/sequencer.h:58-59,124-128,156,233,255-256`, `lib/core/src/sequencer.cpp:66,139-140,307-308,417,656-723` (`adjustStep` loses the perf part — delete the function and its call if nothing else remains in it; `perfSlot`, `perfOn`, `perfOff`, `audible`'s perf Mute part, `perfMuted_` and its undo field)
- Modify: `src/engine/engine.h:12-15` (`PerfOn`, `PerfOff` out of `Cmd`; keep the enum values of the others in order), `src/engine/engine.cpp:62-63`
- Test: `test/test_sequencer/test_main.cpp:1960-2040,2175-2190` (delete the PERF tests)

**Step 1:** Delete the tests. **Step 2:** remove the code. **Step 3:** `pio test -e native` — all PASS.

---

### Task 9: Audio task — buffer and `audio::perf`

**Files:**
- Modify: `src/audio/audio.h` (declare), `src/audio/audio.cpp:~844` (allocation), the uiQ producer next to `preview()`

```cpp
// PERF (UI task): op 0 press fx, 1 release fx, 2 turn (int8 delta), 3 release all
// (Synth message 0xF6). Goes through the preview queue: applied with the next block.
void perf(uint8_t op, uint8_t val);
```

Implementation: push `{0xF6, op, val}` for `mt::kPreviewTrack` stamped `engine::nowUs()` into `uiQ`, the same way `preview()` does (without the note-on bookkeeping in `drain` — that branch only matches `0x90`).

Buffer: after the delay line,
```cpp
  // PERF ring buffer: 6 s stereo int16 (~768 KB) = 2 bars down to 80 BPM.
  constexpr uint32_t kPerfFrames = 6 * kRate;
  if (auto* pb = static_cast<int16_t*>(heap_caps_malloc(kPerfFrames * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM)))
    synth->setPerfBuffer(pb, kPerfFrames);
  else
    Serial.println("audio: no PSRAM for the PERF buffer");
```
and log `heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024` once after it.

**Check:** `pio run -e wt32` compiles (UI still broken until Task 10 — do Task 10 before building if needed).

---

### Task 10: UI — GRID PERF, PROJ

**Files:**
- Modify: `src/ui/grid_screen.h:29,147` (`perfBtn_`, `perfTrack_` → `mt::PerfStack perf_stack_; int8_t perfHeld_[mt::kPerfButtons]` button → fx, -1 none)
- Modify: `src/ui/grid_screen.cpp:264-279` (`setPerf`, `perfRelease` → `perfAllOff()`), `:403-420` (EncTurn), `:515-525` (`trackKey`), `:537-539` (`trackRelease`), `:1148-1155` (header), `poll()` / `onLeave()` (stop → all off)
- Modify: `src/ui/proj_screen.h:25`, `src/ui/proj_screen.cpp:109-127` (DJ filter row out; PERF rows use `perfName`, range `0..PerfFx::Count-1`)

**Behaviour:**
- `trackKey(n)` in PERF while playing, no Shift: `fx = perfMap[n]`; `---` → toast `PERF ---`, nothing else. Else `perfHeld_[n] = fx`, `perf_stack_.press(fx)`, `audio::perf(0, fx)`, toast `PERF REPEAT 1/16` (`perfName` + `perfValue` of its depth).
- `trackRelease(n)`: if `perfHeld_[n]` set: unless another held button maps to the same fx, `perf_stack_.release(fx)`, `audio::perf(1, fx)`; clear `perfHeld_[n]`.
- `EncTurn` while `perf_ && perf_stack_.count()`: `d = ev.delta · (ev.shift && perfRange(last).coarse ? 10 : 1)`, `perf_stack_.turn(d)`, `audio::perf(2, static_cast<uint8_t>(static_cast<int8_t>(clamp(d, -127, 127))))`, toast `FILTER LP 20`. The normal step move does not happen.
- `perfAllOff()`: clears `perfHeld_`, `perf_stack_.releaseAll()`, `audio::perf(3, 0)`. Called by `setPerf(false)`, `onLeave()`, and `poll()` when `perf_stack_.count() && !app_.status().playing`.
- Header: after `PERF`, the shorts of `perf_stack_.at(i)` in press order separated by spaces (fits: 13 × 4 chars max 8 held → clip at the header width).
- PROJ: delete `kDjFilter` from the enum and its `params_` entry; PERF rows: `perfName(f)` or `---`, turning `0..Count−1`.

**Check:** `pio run -e wt32` — compiles without warnings in the touched files.

---

### Task 11: Demos

**Files:** `lib/core/src/demos.cpp:540` (DEMO-LOFI), `:959-961` (DEMO-HOUSE), `test/test_demos/test_main.cpp` if it checks either

- DEMO-HOUSE map: `{Filter, Repeat, Roll, DelayThrow, ReverbThrow, Gate, TapeStop, Freeze}`.
- DEMO-LOFI: delete `p.djFilter = -18;`; instead lower the Cutoff of its melodic instruments (find them in the demo builder) by about the same amount: −18 on the master was ~5.5 kHz; set each instrument's filter LP with cutoff matching ~5 kHz if its filter is OFF (Filter type LP, cutoff value per the instrument cutoff curve — check `synth_filter` mapping to pick the value), keep the drums as they are.
- `pio test -e native -f test_demos` — PASS.

---

### Task 12: Manual and README

**Files:** `docs/manual.md`, `docs/manual_ru.md` (sections REC и PERF ~226-240, PROJ FX row ~1214, PROJ PERF row ~1221, DEMO rows ~1350-1354, pan note ~1552), `README.md:36,225`

- Replace the PERF table with the 13 effects (what, encoder, default), stacking rules, "last pressed gets the encoder", "every press starts at the default", "Shift ×10 for FILTER / DRIVE / THROW / FREEZE", "INT sound only — MIDI tracks are not affected", "live only: Render / Resample do not include PERF".
- Remove the DJ filter row from PROJ → FX; the FX page list says "delay, reverb".
- README: "DJ filter" → "PERF punch-in effects on the master (repeat, roll, reverse, scratch, tape stop, gate, DJ filter, crush, throws, freeze)".
- Both languages updated in the same way.

---

### Task 13: Device check, then commit

1. `pio test -e native` — all PASS (paste the summary line).
2. `pio run -e wt32 -t upload` (or `-e wt32-ota`), read the boot log: free PSRAM after the PERF buffer. If < 512 KB, drop `kPerfFrames` to `4 * kRate` and note it in the design doc.
3. Ask the user to check on the device: each effect, stacking FILTER + REPEAT + DELAY THROW, encoder on the last pressed, toasts and header, CPU % while REPEAT + FILTER + CRUSH are held (expect ≤ +2 %), old project with PERF map loads with mapped buttons, DEMO-LOFI and DEMO-HOUSE.
4. After the user confirms: one commit.

```bash
git add -A lib src test docs README.md
git commit -m "feat: PERF punch-in effects on the master, DJ filter moves into PERF

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
