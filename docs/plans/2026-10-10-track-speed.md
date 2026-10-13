# Track Speed Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task.

**Goal:** Per pattern, per track speed `1/4, 1/2, x1, x2, x4`: the track plays its steps faster or slower than the pattern grid.

**Architecture:** `Pattern::trackSpeed[]` + one index function `trackStepAt()` in `model.h` shared by the sequencer and GRID. `Sequencer::scheduleStep` keeps its per-pattern-step outer loop and history; the per-track body becomes `playTrackStep()`, called N times (xN, sub-steps `su/N` apart) or every D-th pattern step (/D, duration `su*D`), with the track's own step duration in `ExpandCtx` / `pushStepStart` / ARS. Saved in an optional `TSPD` chunk.

**Tech Stack:** ESP32-S3 Arduino (PlatformIO), C++17, Unity native tests.

Design: `docs/plans/2026-10-10-track-speed-design.md`. Runs before `docs/plans/2026-10-10-virus-arp.md`.

**Project rules:**
- NO git commits (user commits after a hardware test).
- Match surrounding code style: terse comments, English comments in code, 2-space indent, 120 cols.
- Native tests: `pio test -e native -f <suite>`. Firmware: `pio run -e wt32` (must end with `SUCCESS`).
- Every existing test must keep passing: x1 behaviour is unchanged.

---

### Task 1: Model — TrackSpeed and trackStepAt (TDD)

**Files:**
- Modify: `lib/core/src/model.h` (enum + helpers near `Resolution`, field in `Pattern` after `trackLen`)
- Modify: `lib/core/src/model.cpp` (`Pattern::clear`)
- Test: `test/test_model/test_main.cpp`

**Step 1: Write the failing tests** (register in `main()`):

```cpp
void test_track_speed_factors() {
  TEST_ASSERT_EQUAL(1, speedMul(TrackSpeed::X1));
  TEST_ASSERT_EQUAL(2, speedMul(TrackSpeed::X2));
  TEST_ASSERT_EQUAL(4, speedMul(TrackSpeed::X4));
  TEST_ASSERT_EQUAL(1, speedMul(TrackSpeed::Half));
  TEST_ASSERT_EQUAL(2, speedDiv(TrackSpeed::Half));
  TEST_ASSERT_EQUAL(4, speedDiv(TrackSpeed::Quarter));
  TEST_ASSERT_EQUAL(1, speedDiv(TrackSpeed::X4));
  TEST_ASSERT_EQUAL_STRING("x2", speedName(TrackSpeed::X2));
  TEST_ASSERT_EQUAL_STRING("1/4", speedName(TrackSpeed::Quarter));
}

void test_track_step_at() {
  Pattern pt;
  pt.clear();
  pt.length = 16;
  // x1: as stepIndex today, with polymeter.
  TEST_ASSERT_EQUAL(5, trackStepAt(pt, 0, 5, 0, 0));
  pt.trackLen[0] = 3;
  TEST_ASSERT_EQUAL(2, trackStepAt(pt, 0, 5, 7, 0));
  pt.trackLen[0] = 0;
  // x2: two track steps per pattern step, wraps at the track length.
  pt.trackSpeed[1] = static_cast<uint8_t>(TrackSpeed::X2);
  TEST_ASSERT_EQUAL(10, trackStepAt(pt, 1, 5, 0, 0));
  TEST_ASSERT_EQUAL(11, trackStepAt(pt, 1, 5, 0, 1));
  TEST_ASSERT_EQUAL(2, trackStepAt(pt, 1, 9, 0, 0));  // 18 % 16
  // 1/2: plays on even counts, the whole track over two passes.
  pt.trackSpeed[2] = static_cast<uint8_t>(TrackSpeed::Half);
  TEST_ASSERT_EQUAL(2, trackStepAt(pt, 2, 4, 0, 0));
  TEST_ASSERT_EQUAL(-1, trackStepAt(pt, 2, 5, 0, 0));  // not this pattern step
  TEST_ASSERT_EQUAL(8, trackStepAt(pt, 2, 0, 1, 0));   // (16 + 0) / 2
  TEST_ASSERT_EQUAL(15, trackStepAt(pt, 2, 14, 1, 0));
  TEST_ASSERT_EQUAL(0, trackStepAt(pt, 2, 0, 2, 0));
  // Bad stored value plays as x1.
  pt.trackSpeed[3] = 99;
  TEST_ASSERT_EQUAL(5, trackStepAt(pt, 3, 5, 0, 0));
}
```

**Step 2: Run, expect compile FAIL:** `pio test -e native -f test_model`

**Step 3: Implement** in `model.h` (after `ticksPerStep`; `trackStepAt` after `struct Pattern`):

```cpp
// Per pattern, per track: x2 / x4 play 2 / 4 track steps per pattern step, 1/2 / 1/4 one every 2 / 4.
// 0 = x1, so zeroed (old) patterns play as before.
enum class TrackSpeed : uint8_t { X1, X2, X4, Half, Quarter, Count };
inline TrackSpeed toSpeed(uint8_t v) { return v < static_cast<uint8_t>(TrackSpeed::Count) ? static_cast<TrackSpeed>(v) : TrackSpeed::X1; }
inline int speedMul(TrackSpeed s) { return s == TrackSpeed::X2 ? 2 : (s == TrackSpeed::X4 ? 4 : 1); }
inline int speedDiv(TrackSpeed s) { return s == TrackSpeed::Half ? 2 : (s == TrackSpeed::Quarter ? 4 : 1); }
const char* speedName(TrackSpeed s);  // "1/4", "1/2", "x1", "x2", "x4"
```

`Pattern`: after `trackLen`: `uint8_t trackSpeed[kTracks] = {0};  // TrackSpeed, per track`

```cpp
// The step of `track` at pattern step pos, sub-step sub (0..speedMul-1), pass = passes of the
// pattern since it started. -1: a slow track does not play on this pattern step. The track loops
// on its own length (polymeter); slow tracks count through pass ends.
inline int trackStepAt(const Pattern& pt, int track, int pos, uint32_t pass, int sub) {
  const uint8_t n = pt.trackLen[track];
  const int len = n && n < pt.length ? n : pt.length;
  if (len <= 0) return 0;
  const TrackSpeed s = toSpeed(pt.trackSpeed[track]);
  const int d = speedDiv(s);
  if (d > 1) {
    const uint64_t c = static_cast<uint64_t>(pass) * pt.length + pos;
    return c % d ? -1 : static_cast<int>((c / d) % len);
  }
  return (pos * speedMul(s) + sub) % len;
}
```

`model.cpp`: `speedName` (table), and in `Pattern::clear()` add `memset(trackSpeed, 0, sizeof(trackSpeed));`.

**Step 4: Run** `pio test -e native -f test_model` — PASS.

---

### Task 2: File — TSPD chunk (TDD)

**Files:**
- Modify: `lib/core/src/project_io.cpp` (`patternStored`, writer after TLEN ~line 556, `readTspd` after `readTlen` ~line 336, dispatch ~line 666)
- Test: `test/test_project_io/test_main.cpp` (next to `test_tlen_round_trip_and_default`)

**Step 1: Write the failing tests:**

```cpp
void test_tspd_round_trip_and_default() {
  a.reset();
  a.patterns[2].steps[0][0].note = 60;
  a.patterns[2].trackSpeed[5] = static_cast<uint8_t>(TrackSpeed::X2);
  a.patterns[4].trackSpeed[15] = static_cast<uint8_t>(TrackSpeed::Quarter);  // stored only for its speed
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_EQUAL(static_cast<int>(TrackSpeed::X2), b.patterns[2].trackSpeed[5]);
  TEST_ASSERT_EQUAL(0, b.patterns[2].trackSpeed[4]);
  TEST_ASSERT_EQUAL(static_cast<int>(TrackSpeed::Quarter), b.patterns[4].trackSpeed[15]);
  const std::vector<uint8_t> f = withoutChunks(out.buf, {"TSPD"});
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(0, b.patterns[2].trackSpeed[5]);
}

void test_tspd_absent_without_speeds_and_bad_values() {
  a.reset();
  a.patterns[1].steps[0][0].note = 60;
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_TRUE(withoutChunks(out.buf, {"TSPD"}) == out.buf);  // no chunk written
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "PATN", patn(1, 8));
  std::vector<uint8_t> sp(1 + kTracks, 0);
  sp[0] = 1;
  sp[1] = 200;  // unknown: x1
  sp[2] = static_cast<uint8_t>(TrackSpeed::X4);
  putChunk(f, "TSPD", sp);
  sp[0] = 40;  // no such pattern: skipped
  putChunk(f, "TSPD", sp);
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(0, b.patterns[1].trackSpeed[0]);
  TEST_ASSERT_EQUAL(static_cast<int>(TrackSpeed::X4), b.patterns[1].trackSpeed[1]);
  f = fileHeader();
  putChunk(f, "TSPD", {1, 2, 3});  // wrong size
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
}
```

**Step 2: Run, expect FAIL:** `pio test -e native -f test_project_io`

**Step 3: Implement** — mirror TLEN:
- `bool trackSpeedSet(const Pattern& p)` (any non-zero); `patternStored` ORs it.
- Writer after the TLEN block: chunk `TSPD`, `{index, trackSpeed[0..15]}`.
- `readTspd`: size must be `1 + kTracks` (else `BadValue`), pattern index >= kPatterns skipped, each value through `toSpeed()`.
- Dispatch: `else if (memcmp(ch, "TSPD", 4) == 0) e = readTspd(in, size, out);`.
- Check the project version rule at the top of `project_io.cpp`: an optional chunk old firmware skips needs no version bump (TLEN did the same; confirm how unknown chunks are handled and follow it).

**Step 4: Run** `pio test -e native -f test_project_io` — PASS (all old tests too).

---

### Task 3: Sequencer — fast tracks (xN) (TDD)

**Files:**
- Modify: `lib/core/src/sequencer.h` (`ctx`, `pushStepStart`, `arpStep` signatures; new `playTrackStep`; `stepIndex` replaced)
- Modify: `lib/core/src/sequencer.cpp` (`scheduleStep` ~380-512, `pushStepStart` ~515, `skipStep` ~553, `arpStep` ~725)
- Test: `test/test_sequencer/test_main.cpp`

**Step 1: Write the failing tests** (120 BPM, 1/16: step = 125000 us; helpers `run`, `sink->times` exist):

```cpp
void test_speed_x2_plays_two_steps_per_step() {
  Pattern& pt = p->patterns[0];
  pt.trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::X2);
  pt.steps[0][0].note = 60;
  pt.steps[0][1].note = 62;
  pt.steps[0][2].note = 64;
  seq->start(0, *sink);
  run(0, 130000);
  TEST_ASSERT_EQUAL(0, sink->times(0x90, 60)[0]);
  TEST_ASSERT_EQUAL(62500, sink->times(0x90, 62)[0]);
  TEST_ASSERT_EQUAL(125000, sink->times(0x90, 64)[0]);
  TEST_ASSERT_EQUAL(31250, sink->times(0x80, 60)[0]);  // default gate: half of the track's own step
}

void test_speed_x2_with_x1_track_in_sync() {
  Pattern& pt = p->patterns[0];
  pt.length = 4;
  pt.trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::X2);
  for (int i = 0; i < 4; ++i) pt.steps[0][i].note = 60;  // x2: 4 steps loop twice per pass
  pt.steps[1][0].note = 36;                              // x1: once per pass
  seq->start(0, *sink);
  run(0, 999999);
  TEST_ASSERT_EQUAL(16, sink->times(0x90, 60).size());  // 2 passes x 8
  auto kick = sink->times(0x90, 36);
  TEST_ASSERT_EQUAL(2, kick.size());
  TEST_ASSERT_EQUAL(500000, kick[1]);
}

void test_speed_x4_heap_holds() {
  Pattern& pt = p->patterns[0];
  for (int tr = 0; tr < kTracks; ++tr) {
    pt.trackSpeed[tr] = static_cast<uint8_t>(TrackSpeed::X4);
    for (int i = 0; i < pt.length; ++i) {
      pt.steps[tr][i].note = static_cast<uint8_t>(40 + tr);
      pt.steps[tr][i].fx[0] = {Fx::RAT, 4};
    }
  }
  seq->start(0, *sink);
  run(0, 2000000);  // one pass
  // 16 tracks x 64 track steps x 4 ratchets: nothing dropped.
  size_t on = 0;
  for (int tr = 0; tr < kTracks; ++tr) on += sink->times(0x90, 40 + tr).size();
  TEST_ASSERT_EQUAL(16u * 64u * 4u, on);
}

void test_speed_x2_replans_after_edit() {
  Pattern& pt = p->patterns[0];
  pt.trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::X2);
  pt.steps[0][3].note = 60;  // sub-step 1 of pattern step 1: 187500
  seq->start(0, *sink);
  run(0, 10000);             // planned ahead, not heard
  pt.steps[0][3].note = 62;
  seq->releaseTies(10000, *sink);  // replans the unheard steps
  run(10000, 200000);
  TEST_ASSERT_EQUAL(0, sink->times(0x90, 60).size());
  TEST_ASSERT_EQUAL(187500, sink->times(0x90, 62)[0]);
}
```

Check the RAT value encoding in `fx_info.cpp` / `step_expand.cpp` before relying on `{Fx::RAT, 4}`; adjust to the value that means 4 repeats. If the heap test fails on capacity, report it (do not silently change `EventHeap::kCap`): raising it costs RAM on the engine task, the user decides.

**Step 2: Run, expect FAIL** (62 at 125000 etc.): `pio test -e native -f test_sequencer`

**Step 3: Implement**
1. `ctx(uint32_t su, uint16_t ticks)` — `ExpandCtx{su, loop_, …, ticks}`; callers pass the track's values.
2. `pushStepStart(…, uint16_t tps)` — use the passed ticks for `tps` instead of `ticks()`.
3. `arpStep(track, t, earliest, aud, uint32_t su)` — `span = su * a.div` (line ~730).
4. Move the body of the `for (int tr …)` loop in `scheduleStep` (from `Step s = …` to the end of the iteration) into
   `void playTrackStep(int tr, const Step& raw, uint64_t t, const ExpandCtx& c, uint16_t tps, int64_t earliest)` — `continue` becomes `return`. Keep the code otherwise identical.
5. New loop in `scheduleStep`:

```cpp
  for (int tr = 0; tr < kTracks; ++tr) {
    const TrackSpeed sp = toSpeed(pat.trackSpeed[tr]);
    const int mul = speedMul(sp), div = speedDiv(sp);
    const uint32_t tsu = su / mul * div;
    const uint16_t tt = static_cast<uint16_t>(ticks() / mul * div);
    ExpandCtx tc = ctx(tsu, tt);
    tc.velPct = c.velPct;  // the groove accent of the pattern step
    for (int k = 0; k < mul; ++k) {
      const int idx = trackStepAt(pat, tr, pos_, loop_, k);
      if (idx < 0) break;  // a slow track rests on this pattern step
      playTrackStep(tr, pat.steps[tr][idx], t + static_cast<uint64_t>(tsu) * k, tc, tt, earliest);
    }
  }
```

   (`ticks()` is 96/48/24/12/32/16, all divisible by 4. `su / mul` truncates by < 1 us; sub-step
   times use `t + tsu * k`, so the drift stays under 4 us and resets every pattern step.)
6. `skipStep`: same structure (sub-steps at `t + tsu * k`, controls still at `now`), passing `tsu` to `arpStep`.
7. Remove `stepIndex()`; grep for other users (`grep -n stepIndex lib src`) and switch them to `trackStepAt(…, 0)`.

**Step 4: Run** `pio test -e native -f test_sequencer` — all PASS, old ones included.

---

### Task 4: Sequencer — slow tracks (/D) (TDD)

**Files:** same as Task 3.

**Step 1: Write the failing tests:**

```cpp
void test_speed_half_plays_every_other_step() {
  Pattern& pt = p->patterns[0];
  pt.length = 4;
  pt.trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::Half);
  pt.steps[0][0].note = 60;
  pt.steps[0][1].note = 62;
  pt.steps[0][2].note = 64;
  pt.steps[0][3].note = 65;
  seq->start(0, *sink);
  run(0, 999999);  // two passes of 4 steps
  TEST_ASSERT_EQUAL(0, sink->times(0x90, 60)[0]);
  TEST_ASSERT_EQUAL(250000, sink->times(0x90, 62)[0]);
  TEST_ASSERT_EQUAL(500000, sink->times(0x90, 64)[0]);  // the second pass goes on, not from 0
  TEST_ASSERT_EQUAL(750000, sink->times(0x90, 65)[0]);
  TEST_ASSERT_EQUAL(125000, sink->times(0x80, 60)[0]);  // gate: half of a two-step-long step
}

void test_speed_half_tie_holds_over_skipped_step() {
  // As test_tie_holds_until_next_note_with_overlap, on a 1/2 track: track step 1 = pattern step 2.
  Pattern& pt = p->patterns[0];
  pt.trackSpeed[0] = static_cast<uint8_t>(TrackSpeed::Half);
  pt.steps[0][0].note = 60;
  pt.steps[0][0].fx[0] = {Fx::TIE, 0};
  pt.steps[0][1].note = 64;
  seq->start(0, *sink);
  run(0, 300000);
  auto off60 = sink->times(0x80, 60);
  TEST_ASSERT_EQUAL(1, off60.size());
  TEST_ASSERT_EQUAL(251000, off60[0]);  // held through the skipped pattern step 1
  TEST_ASSERT_EQUAL(250000, sink->times(0x90, 64)[0]);
}
```

**Step 2: Run, expect FAIL.**

**Step 3: Implement** — Task 3's loop already handles `div` (`idx < 0` skips). Fix whatever the tests show (most likely nothing beyond Task 3; the count uses `loop_`, restored by rewind).

**Step 4: Run** `pio test -e native` — all suites PASS.

---

### Task 5: UI — TRACK Speed row

**Files:**
- Modify: `src/ui/track_screen.h:33` (Row enum: `kSpeed` after `kPatLen`)
- Modify: `src/ui/track_screen.cpp` (param after `kPatLen` ~line 54-75; undo like `Pat len`)

**Step 1:** Add the param:

```cpp
  // The track's speed in the edited pattern: 1/4 1/2 x1 x2 x4 (left to right).
  params_[kSpeed] = {"Speed",
                     [this](char* o, int n) {
                       const mt::Pattern& pt = app_.project().patterns[app_.editPattern()];
                       snprintf(o, n, "%s", mt::speedName(mt::toSpeed(pt.trackSpeed[app_.curTrack()])));
                     },
                     [this](int d) {
                       static constexpr mt::TrackSpeed kOrder[] = {mt::TrackSpeed::Quarter, mt::TrackSpeed::Half,
                                                                   mt::TrackSpeed::X1, mt::TrackSpeed::X2,
                                                                   mt::TrackSpeed::X4};
                       mt::Pattern& pt = app_.project().patterns[app_.editPattern()];
                       uint8_t& v = pt.trackSpeed[app_.curTrack()];
                       int i = 2;
                       for (int k = 0; k < 5; ++k)
                         if (kOrder[k] == mt::toSpeed(v)) i = k;
                       const int ni = clampi(i + d, 0, 4);
                       if (ni == i) return;
                       // same undo rule as Pat len (copy its pushUndo / seq guard)
                       v = static_cast<uint8_t>(kOrder[ni]);
                       app_.markDirty();  // use whatever Pat len calls after a change
                     }};
```

Mirror Pat len exactly for undo and dirty marking (read lines 54-75 first). If the engine reads the project under a lock (check how Pat len's write is synchronized with the engine task), do the same.

**Step 2:** `pio run -e wt32` — SUCCESS.

---

### Task 6: UI — GRID play row and REC follow the track

**Files:**
- Modify: `src/ui/grid_screen.cpp` (Detail view play row ~line 1078, follow ~line 955, `recordKey` ~line 283)

**Step 1:** Add a helper in `GridScreen`: `int heardTrackStep(int tr) const` — `trackStepAt(pat(), tr, st.pos, st.loop, 0)`; for a slow track returning -1, use the last step it played: `trackStepAt` at `st.pos - st.pos % div` (pass the same `loop`).
**Step 2:** Detail view: `step == st.pos` → `step == heardTrackStep(tr)`; follow uses it too. Overview stays on `st.pos`.
**Step 3:** `recordKey`: the track step = `heardTrackStep(tr)` plus, for xN, the sub-step from the phase: `sub = phase * N / 256`, `phase' = (phase * N) % 256`, then `recordStepFor(base + sub, phase', tl)` where `base = trackStepAt(pt, tr, st.pos, st.loop, 0)`. /D tracks: phase over D pattern steps is not tracked — use `recordStepFor(heardTrackStep(tr), phase, tl)` (documented limitation).
**Step 4:** `pio run -e wt32` — SUCCESS.

---

### Task 7: Docs

**Files:** `docs/manual.md`, `docs/manual_ru.md` (TRACK parameters table next to Pat len; GRID polymeter notes), `README.md` / `README_ru.md` (feature list, if it lists Pat len / polymeter).

- Speed row: `1/4, 1/2, x1, x2, x4`, per pattern; x2 plays two of its steps per pattern step (1/32 at 1/16), 1/2 one step per two (it goes on over pass ends); GAT / RAT / NDG / TIE / ARS count from the track's own step; swing and groove move the pattern step, sub-steps are even; works with Pat len.
- GRID: Detail view's play row follows the track's step; REC on 1/2 / 1/4 tracks lands on the heard step.
- Russian text in `manual_ru.md` with proper orthography.

---

### Task 8: Verify

**Step 1:** `pio test -e native` — all suites PASS (paste the summary).
**Step 2:** `pio run -e wt32` — SUCCESS; note RAM / flash change.
**Step 3:** Continue with `docs/plans/2026-10-10-virus-arp.md` (its docs task adds: "the original Virus tempo = track Speed x2").
**Step 4:** Device check by the user (both features): a x2 arp track against x1 drums stays in sync over several passes, 1/2 track walks through all its steps, Speed saved / loaded with the project, Copy to… keeps it. Commit only after the user's go-ahead.
