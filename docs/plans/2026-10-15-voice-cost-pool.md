# Voice Pool by Cost Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** The voice pool keeps the summed CPU cost of live voices under a budget, stealing the oldest
voices before the CPU guard has to.

**Architecture:** A pure `voiceCost()` (new `lib/core/src/voice_cost.h/.cpp`) prices a note at note-on
from the instrument. `allocVoice` gets `cost` / `budget` arguments, stores `Voice::cost` and steals the
oldest live voices (releasing first) until the new note fits. `Synth` holds the budget (default
unlimited); the Teensy firmware sets it. Design: `docs/plans/2026-10-15-voice-cost-pool-design.md`.

**Tech Stack:** C++17, PlatformIO, Unity tests (`pio test -e native`), Teensy 4.1.

Rules for the implementer: no commits (skip Commit steps); never touch `enclosure/`; no libm / allocation
in note-on paths; parallel builds use `PLATFORMIO_BUILD_DIR=.pio/build-pool`.

---

### Task 1: `voiceCost()`

**Files:**
- Create: `lib/core/src/voice_cost.h`, `lib/core/src/voice_cost.cpp`
- Test: `test/test_voice_cost/test_main.cpp`
- Modify: `src/teensy/imxrt1062_t41_mt.ld` (put `voice_cost.cpp.o` `.text` in the flash list, both
  `*:` and `*` forms, like `synth_string`)

Units: hundredths of a percent of the Teensy CPU per voice, including the voice's LP filter and envelope
(bench-max, 2026-10-15, future-teensy.md). Constants, one per line with a short comment:

```cpp
// voice_cost.h
#pragma once
#include <stdint.h>
#include "model.h"
namespace mt {
// Voice cost: hundredths of a percent of the Teensy 4.1 CPU (bench-max 2026-10-15, LP filter,
// envelope and track strip share included). Used by allocVoice against Synth::voiceBudget().
constexpr int kCostSample = 100;   // SD / flash stream
constexpr int kCostChip = 119;
constexpr int kCostSynBl = 130;    // SYNTH without a wavetable oscillator (estimate, below WT)
constexpr int kCostSynWt = 240;
constexpr int kCostFm = 195;
constexpr int kCostDrum = 290;
constexpr int kCostMacro[] = {148, 233, 166, 219, 191, 229, 174, 174};  // MacroModel order
constexpr int kCostLadder = 35, kCostMs20 = 23, kCostComb = 34, kCostFormant = 40;  // over LP
constexpr int kCostFold = 39, kCostRing = 9;  // SYNTH, only when on
constexpr int kVoiceBudgetTeensy = 7000;     // 70 % for voices + ≈5 % fx: under the guard's 80 %
constexpr int kVoiceBudgetOff = 1 << 30;
// The price of one note of m (sample: a SAMPLE note or a KIT sampler lane).
int voiceCost(const Instrument& m, bool sample);
}  // namespace mt
```

Static-assert `sizeof(kCostMacro)/sizeof(int) == MacroModel::Count`. `voiceCost`:
- `sample` -> `kCostSample` (no filter add-on: sample voices use the same LP).
- By `m.type`: Chip -> Chip; Fm -> Fm; Drum -> Drum; Synth -> SynWt if either `synOsc` is `SynOsc::Wt`
  else SynBl, plus Fold if `synFold`, Ring if `synRing`; Macro -> `kCostMacro[machine]` (clamped);
  String -> `kCostSynWt` for now (plan C replaces it after its bench); Kit / other -> Chip.
- Filter add-on for every type except `sample`: Ladder / Ms20 / Comb / Formant by `m.fltMode`.

**Step 1:** Write tests: CHIP LP = 119; CHIP LADDER = 154; SYNTH WT fold 0 ring 0 = 240, fold 100 = 279,
fold + ring + LADDER = 323; SYNTH BL = 130; MACRO each model = table; out-of-range machine clamps;
`sample = true` = 100. Add the test env if `platformio.ini` lists tests explicitly (check).
**Step 2:** `pio test -e native -f test_voice_cost` -> FAIL (no header).
**Step 3:** Implement.
**Step 4:** Test -> PASS.

### Task 2: `allocVoice` with cost / budget

**Files:**
- Modify: `lib/core/src/synth_voice.h` (`Voice::cost`, declaration + comment), `lib/core/src/synth_voice.cpp`
- Test: `test/test_synth_voice/test_main.cpp`

New signature (defaults keep every existing call and test unchanged):

```cpp
int allocVoice(Voice (&v)[kVoices], uint8_t track, bool mono, uint32_t& ageCounter, bool& legato,
               bool heavy = false, int polyMax = kPolyPerTrack, int cap = kVoices,
               int cost = 0, int budget = kVoiceBudgetOff);
```

`Voice` gains `uint16_t cost = 0;  // voiceCost at note-on (allocVoice, budget)`.

Logic, after the existing pick / heavy-steal code and before `p.on = true`:
- `used` = sum of `cost` over voices `on && !stolen`, minus `v[pick].cost` if `v[pick]` is on and not
  stolen (it is reused).
- While `used + cost > budget`: victim = oldest voice that is `on && !stolen && i != pick`, releasing
  (`Release` stage or `env.idle()`) first; none -> break (the note always plays). Victim:
  `env.fade(kStealMs)`, `stolen = true`, `used -= victim.cost`.
- Then `p.cost = cost`.
(The scan loop can gather `used`; the victim search runs only when over budget — rare, fine as a
second loop.)

**Step 1:** Tests (with `budget` small, costs in round numbers):
- budget off: 40 notes alloc identical picks to the current behaviour (existing tests already cover; add
  one explicit check that nothing is stolen with default args).
- budget 1000, cost 300: 3 notes on different tracks fit, 4th steals the oldest (`stolen`, fading), 4th
  plays.
- releasing voice preferred as victim over an older held one.
- a big note (cost 900) with three 300 voices steals the oldest two... (exactly as many as needed: 900 +
  300 ≤ 1000 needs two stolen? compute: used 900, 900+900 > 1000 -> steal until used ≤ 100 -> all three).
  Write the expectation from the rule, not by hand-waving.
- mono legato on the same track: reuses its voice, its own cost is not counted twice, nothing stolen.
- cheap notes under budget: nothing stolen.
**Step 2:** run `pio test -e native -f test_synth_voice` -> FAIL.
**Step 3:** implement. **Step 4:** PASS, then full `pio test -e native` (test_voices too).

### Task 3: Synth wiring

**Files:**
- Modify: `lib/core/src/synth.h` (`void setVoiceBudget(int b)`, `int voiceBudget() const`, member
  `int budget_ = kVoiceBudgetOff;`), `lib/core/src/synth.cpp` note-on (~line 811), SLD `held` path
- Modify: `src/teensy/main.cpp` (`synth.setVoiceBudget(mt::kVoiceBudgetTeensy);` next to the other
  `synth.set...` setup calls), `src/teensy/bench_max.cpp` (keep budget off for the ceiling ladders —
  they measure raw cost; see Task 4)
- Test: `test/test_synth/test_main.cpp`

Note-on: `const int cost = voiceCost(m, sample || kitLane);` (check how a KIT sampler lane is detected
there and pass the right flag), pass `cost, budget_` to `allocVoice`. In the SLD `held` branch (no
allocVoice) set `voices_[vi].cost = cost` so the sum stays right. Every note-on path that takes a voice
without `allocVoice` must set `cost` (grep `voices_[` assignments of `.on = true`).

**Step 1:** Tests in test_synth: with `setVoiceBudget(500)` and CHIP notes (119) on 5 tracks, the 5th
note steals one (count voices `on && !stolen` = 4); with the default budget 32 CHIP notes on 16 tracks ×2
keep 32 live; a WT SYNTH LADDER instrument budget check.
**Step 2:** FAIL. **Step 3:** implement. **Step 4:** full `pio test -e native` PASS; `pio run -e wt32`,
`pio run -e teensy41` (RAM1 free ≥ 24 KB, ITCM ≤ 8 banks; report numbers).

### Task 4: Bench group and docs

**Files:**
- Modify: `src/teensy/bench_max.cpp`: new group 5 (`BENCH_MAX_GROUP=5`): budget `kVoiceBudgetTeensy`,
  the plan A worst case (16 WT fold 100 ring 64 LADDER + CHIP LADDER) and `16 MACRO MORPH + CHIP`,
  stepping the requested voices 16..32 by 4 and printing the live voice count with the load (follow the
  existing ladder / print style; check `bench_max.cpp` for how groups are selected).
- Modify: `docs/manual.md`, `docs/manual_ru.md` (one paragraph where the voice pool / CPU guard is
  described), `CHANGELOG.md`, `CHANGELOG_ru.md` (one entry), `docs/plans/future-teensy.md` (paragraph
  left for the bench results).

**Step 1:** build `PLATFORMIO_BUILD_FLAGS="-DBENCH_MAX_GROUP=5" pio run -e teensy41-bench-max`.
**Step 2:** the user flashes and runs it; results go into future-teensy.md. Target: ≤75 % with the guard
not shedding (`voiceCap` stays at `kVoices`).
