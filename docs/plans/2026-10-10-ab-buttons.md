# Buttons A and B Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task.

**Goal:** Make buttons A (P16) and B (P17) work: hold-A editing, tab/page cycling, undo, back, solo, pattern queue, quick note entry.

**Architecture:** `input.cpp` debounces A/B into `ADown/AUp/BDown/BUp`. A pure chord state machine `mt::AbKeys` (lib/core, native-tested) turns them plus encoder turns and track buttons into actions. `App::onInput` runs it and dispatches: global actions in App, edit actions as new `InputType`s (`EditTurn/EditEnd/EditCancel`) to the screen, plus new `Screen` virtuals `onBack/onPage/onATap`.

**Tech Stack:** ESP32-S3 Arduino (PlatformIO), C++17, Unity native tests.

Design: `docs/plans/2026-10-10-ab-buttons-design.md`.

**Project rules:**
- NO git commits (user commits after a hardware test).
- Match surrounding code style: terse comments, English comments in code, 2-space indent, 120 cols.
- Build firmware: `pio run -e wt32` (must end with `SUCCESS`). Native tests: `pio test -e native -f <name>`.

---

### Task 1: AbKeys chord state machine (TDD)

**Files:**
- Create: `lib/core/src/ab_keys.h`
- Create: `test/test_ab_keys/test_main.cpp`

**Step 1: Write the failing test** `test/test_ab_keys/test_main.cpp` (style as `test/test_track_leds/test_main.cpp`):

```cpp
#include <unity.h>
#include "ab_keys.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static void expect(AbAction a, const AbOut& o) { TEST_ASSERT_EQUAL_INT(static_cast<int>(a), static_cast<int>(o.act)); }

void test_idle_passes_through() {
  AbKeys k;
  expect(AbAction::Pass, k.turn(3, false));
  expect(AbAction::Pass, k.track(2, false));
}

void test_a_turn_edits_and_release_ends() {
  AbKeys k;
  expect(AbAction::None, k.aDown());
  AbOut o = k.turn(-2, true);
  expect(AbAction::EditTurn, o);
  TEST_ASSERT_EQUAL_INT(-2, o.delta);
  TEST_ASSERT_TRUE(o.shift);
  expect(AbAction::EditTurn, k.turn(1, false));
  expect(AbAction::EditEnd, k.aUp(false));
  expect(AbAction::Pass, k.turn(1, false));
}

void test_a_tap() {
  AbKeys k;
  k.aDown();
  AbOut o = k.aUp(true);
  expect(AbAction::ATap, o);
  TEST_ASSERT_TRUE(o.shift);
}

void test_b_during_a_edit_cancels() {
  AbKeys k;
  k.aDown();
  k.turn(1, false);
  expect(AbAction::EditCancel, k.bDown());
  expect(AbAction::None, k.bUp(false));  // no Back
  expect(AbAction::None, k.aUp(false));  // no EditEnd, no tap
}

void test_b_during_a_without_edit_does_nothing() {
  AbKeys k;
  k.aDown();
  expect(AbAction::None, k.bDown());
  expect(AbAction::None, k.bUp(false));
  expect(AbAction::None, k.aUp(false));
}

void test_b_turn_tabs_and_pages() {
  AbKeys k;
  k.bDown();
  AbOut o = k.turn(1, false);
  expect(AbAction::TabTurn, o);
  TEST_ASSERT_EQUAL_INT(1, o.delta);
  expect(AbAction::PageTurn, k.turn(-1, true));
  expect(AbAction::None, k.bUp(false));  // used: no Back
}

void test_b_tap_back_and_shift_undo() {
  AbKeys k;
  k.bDown();
  expect(AbAction::Back, k.bUp(false));
  k.bDown();
  expect(AbAction::Undo, k.bUp(true));
}

void test_track_chords() {
  AbKeys k;
  k.aDown();
  AbOut o = k.track(5, false);
  expect(AbAction::Solo, o);
  TEST_ASSERT_EQUAL_INT(5, o.delta);
  expect(AbAction::None, k.aUp(false));  // used: no tap
  k.bDown();
  o = k.track(3, true);
  expect(AbAction::QueuePattern, o);
  TEST_ASSERT_EQUAL_INT(3, o.delta);
  TEST_ASSERT_TRUE(o.shift);
  expect(AbAction::None, k.bUp(false));
}

void test_b_wins_over_a() {
  AbKeys k;
  k.aDown();
  k.bDown();
  expect(AbAction::TabTurn, k.turn(1, false));
  expect(AbAction::QueuePattern, k.track(0, false));
}

void test_reset_clears_held() {
  AbKeys k;
  k.aDown();
  k.turn(1, false);
  k.reset();
  TEST_ASSERT_FALSE(k.aHeld());
  expect(AbAction::Pass, k.turn(1, false));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_idle_passes_through);
  RUN_TEST(test_a_turn_edits_and_release_ends);
  RUN_TEST(test_a_tap);
  RUN_TEST(test_b_during_a_edit_cancels);
  RUN_TEST(test_b_during_a_without_edit_does_nothing);
  RUN_TEST(test_b_turn_tabs_and_pages);
  RUN_TEST(test_b_tap_back_and_shift_undo);
  RUN_TEST(test_track_chords);
  RUN_TEST(test_b_wins_over_a);
  RUN_TEST(test_reset_clears_held);
  return UNITY_END();
}
```

(Check the `main` signature used by other tests in `test/` and copy it.)

**Step 2:** `pio test -e native -f test_ab_keys` — expect compile FAIL (`ab_keys.h` missing).

**Step 3: Implement** `lib/core/src/ab_keys.h`:

```cpp
#pragma once
#include <stdint.h>

namespace mt {

enum class AbAction : uint8_t {
  None,          // consumed, nothing to do
  Pass,          // not a chord: handle the event as usual
  EditTurn,      // A + turn: edit the value under the cursor (delta, shift)
  EditEnd,       // A released after editing: commit
  EditCancel,    // B pressed while A edits: restore the value
  ATap,          // A pressed and released alone (shift)
  TabTurn,       // B + turn (delta)
  PageTurn,      // B + Shift + turn (delta)
  Undo,          // Shift + B tap
  Back,          // B tap
  Solo,          // A + track button (delta = button)
  QueuePattern,  // B + track button (delta = button, shift)
};

struct AbOut {
  AbAction act;
  int8_t delta;
  bool shift;
};

// Chords of buttons A and B with the encoder and the track buttons. A tap counts on release,
// only if nothing else happened while the button was held. With both held B wins.
class AbKeys {
 public:
  AbOut aDown() {
    a_ = true;
    aUsed_ = editing_ = false;
    return out(AbAction::None);
  }
  AbOut aUp(bool shift) {
    a_ = false;
    if (editing_) {
      editing_ = false;
      return out(AbAction::EditEnd);
    }
    return out(aUsed_ ? AbAction::None : AbAction::ATap, 0, shift);
  }
  AbOut bDown() {
    b_ = true;
    bUsed_ = false;
    if (!a_) return out(AbAction::None);
    aUsed_ = bUsed_ = true;
    if (!editing_) return out(AbAction::None);
    editing_ = false;
    return out(AbAction::EditCancel);
  }
  AbOut bUp(bool shift) {
    b_ = false;
    if (bUsed_) return out(AbAction::None);
    return out(shift ? AbAction::Undo : AbAction::Back);
  }
  AbOut turn(int delta, bool shift) {
    if (b_) {
      bUsed_ = true;
      return out(shift ? AbAction::PageTurn : AbAction::TabTurn, delta);
    }
    if (a_) {
      aUsed_ = editing_ = true;
      return out(AbAction::EditTurn, delta, shift);
    }
    return out(AbAction::Pass);
  }
  AbOut track(int n, bool shift) {
    if (b_) {
      bUsed_ = true;
      return out(AbAction::QueuePattern, n, shift);
    }
    if (a_) {
      aUsed_ = true;
      return out(AbAction::Solo, n);
    }
    return out(AbAction::Pass);
  }
  bool aHeld() const { return a_; }
  bool bHeld() const { return b_; }
  // Input was dropped (long operation): forget held buttons.
  void reset() { a_ = b_ = aUsed_ = bUsed_ = editing_ = false; }

 private:
  static AbOut out(AbAction a, int d = 0, bool s = false) { return {a, static_cast<int8_t>(d), s}; }
  bool a_ = false, b_ = false, aUsed_ = false, bUsed_ = false, editing_ = false;
};

}  // namespace mt
```

**Step 4:** `pio test -e native -f test_ab_keys` — expect all PASS.

---

### Task 2: Input driver and event types

**Files:**
- Modify: `src/hw/input.h` (enum `InputType`)
- Modify: `src/hw/input.cpp`
- Modify: `src/hw/pins.h:30` (comment)

**Step 1:** In `InputType` append: `ADown, AUp, BDown, BUp, EditTurn, EditEnd, EditCancel`. Comment: `EditTurn/EditEnd/EditCancel` are made by `ui::App` from A chords (delta, shift as in EncTurn); never queued by drivers.

**Step 2:** In `input.cpp` add `Debounce aBtn{pins::kBtnABit}; Debounce bBtn{pins::kBtnBBit};` and in `task()` next to Play:

```cpp
    const int ab = aBtn.update();
    if (ab == 1) emit(InputType::ADown);
    else if (ab == -1) emit(InputType::AUp);
    const int bb = bBtn.update();
    if (bb == 1) emit(InputType::BDown);
    else if (bb == -1) emit(InputType::BUp);
```

Add `AUp`, `BUp` to the `release` list in `emit()` (releases must not be lost).

**Step 3:** `pins.h`: replace "not used by the firmware yet" with "chords with the encoder / track buttons, see ui::App".

**Step 4:** `pio run -e wt32` — SUCCESS (switches on InputType with `default:` stay fine; check warnings for unhandled enum values in switches without default and add `default: break;` if any).

---

### Task 3: App dispatch and Screen hooks

**Files:**
- Modify: `src/ui/screen.h`
- Modify: `src/ui/app.h`, `src/ui/app.cpp`

**Step 1: Screen virtuals** (`screen.h`, after `onPlay`):

```cpp
  // Button B tap: close the open overlay / step back. Nothing by default.
  virtual void onBack() {}
  // B + Shift + turn: previous / next page.
  virtual void onPage(int) {}
  // Button A tap (alone).
  virtual void onATap(bool) {}
```

Update the `onInput` comment: also `EditTurn/EditEnd/EditCancel`.

**Step 2: App members** (`app.h`): `#include "ab_keys.h"`, member `mt::AbKeys ab_;`, private methods:
`void abAction(const mt::AbOut& o, const hw::InputEvent& ev);`, `void cycleTab(int d);`, `void soloKey(int n);`, `void queueKey(int n, bool shift);`.

**Step 3: App::onInput** — at the start of the switch handle:

```cpp
    case InputType::ADown: abAction(ab_.aDown(), ev); return;
    case InputType::AUp: abAction(ab_.aUp(ev.shift), ev); return;
    case InputType::BDown: abAction(ab_.bDown(), ev); return;
    case InputType::BUp: abAction(ab_.bUp(ev.shift), ev); return;
```

In `EncTurn` case, before the hold-volume check:

```cpp
      if (const mt::AbOut o = ab_.turn(ev.delta, ev.shift); o.act != mt::AbAction::Pass) {
        abAction(o, ev);
        return;
      }
```

In `TrackPress`: set `heldTrackBtn_` only for a non-chord press:

```cpp
    case InputType::TrackPress:
      if (const mt::AbOut o = ab_.track(ev.delta, ev.shift); o.act != mt::AbAction::Pass) {
        abAction(o, ev);
        return;
      }
      heldTrackBtn_ = ev.delta;
      ...
```

**Step 4: abAction** (app.cpp):

```cpp
// A / B chords (mt::AbKeys).
void App::abAction(const mt::AbOut& o, const hw::InputEvent& ev) {
  using mt::AbAction;
  hw::InputEvent e{hw::InputType::EditTurn, o.delta, o.shift};
  switch (o.act) {
    case AbAction::EditTurn:
      if (menu_.isOpen()) return;
      if (bpmEdit_) {  // already editing the BPM: a plain turn
        e.type = hw::InputType::EncTurn;
        break;
      }
      break;
    case AbAction::EditEnd: e.type = hw::InputType::EditEnd; break;
    case AbAction::EditCancel: e.type = hw::InputType::EditCancel; break;
    case AbAction::TabTurn: cycleTab(o.delta); return;
    case AbAction::PageTurn:
      if (!menu_.isOpen()) screen()->onPage(o.delta);
      return;
    case AbAction::Undo:
      if (menu_.isOpen()) return;
      if (tab_ == Tab::Grid) grid_.undo();
      else {
        const bool ok = doUndo();
        if (ok) engine::post(engine::Cmd::ReleaseTies);
        toast(ok ? "UNDO" : "NOTHING TO UNDO");
      }
      return;
    case AbAction::Back:
      if (menu_.isOpen()) menu_.close();
      else if (bpmEdit_) setBpmEdit(false);
      else screen()->onBack();
      return;
    case AbAction::ATap:
      if (!menu_.isOpen() && !bpmEdit_) screen()->onATap(o.shift);
      return;
    case AbAction::Solo: soloKey(o.delta); return;
    case AbAction::QueuePattern: queueKey(o.delta, o.shift); return;
    default: return;
  }
  (void)ev;
  onInput(e);  // EditTurn / EditEnd / EditCancel through the usual menu / BPM / screen routing
}
```

`onInput` must then route `EditTurn` like a turn: in the `bpmEdit_` block treat `EditTurn` as `EncTurn` and ignore `EditEnd`/`EditCancel` there (do NOT let them close the BPM edit: currently any non-turn event ends it — guard with `if (ev.type == EditEnd || ev.type == EditCancel) return;` before). Menu open: `Menu::onInput` ignores unknown types already. Simplify the EditTurn/bpmEdit branch above accordingly (remove the duplicate translation if onInput handles it). `GridScreen::undo()` is private — make it public.

**Step 5: helpers**:

```cpp
void App::cycleTab(int d) {
  if (menu_.isOpen()) menu_.close();
  setBpmEdit(false);
  constexpr int n = static_cast<int>(Tab::Count);
  setTab(static_cast<Tab>(((static_cast<int>(tab_) + d) % n + n) % n));
}

void App::soloKey(int n) {
  if (n < 0 || n >= mt::kTrackLeds) return;
  const int track = (curTrack_ / mt::kTrackLeds) * mt::kTrackLeds + n;
  mt::TrackCfg& t = p_->tracks[track];
  engine::lockProject();
  t.solo = !t.solo;
  engine::unlockProject();
  markDirty();
  char msg[16];
  snprintf(msg, sizeof(msg), "TRACK %d %s", track + 1, t.solo ? "SOLO" : "ON");
  toast(msg);
}

void App::queueKey(int n, bool shift) {
  if (n < 0 || n >= mt::kTrackLeds) return;
  const int pat = (editPattern() / mt::kTrackLeds) * mt::kTrackLeds + n;
  if (pat >= mt::kPatterns) return;
  engine::post(shift ? engine::Cmd::SelectPattern : engine::Cmd::QueuePattern, static_cast<uint16_t>(pat));
  char msg[20];
  snprintf(msg, sizeof(msg), "PATTERN %02d%s", pat + 1, shift ? "" : " NEXT");
  toast(msg);
}
```

Verify `setTab` handles leaving the screen (onLeave/onEnter); check how GRID mute/solo is written elsewhere (`grid_screen.cpp:512`) and match (lock + markDirty). Check song mode: BANK's `activate` is the reference for queue/select.

**Step 6: dropInput**: on `AUp`/`BUp` call `ab_.reset()` only if neither is held afterwards — simplest: `case AUp: case BUp: ab_.reset(); break;` and in `endProgress()` also `ab_.reset()`.

**Step 7:** `pio run -e wt32` — SUCCESS.

---

### Task 4: ParamList hold edit, name rows, dialogs

**Files:**
- Modify: `src/ui/param_list.h`, `src/ui/param_list.cpp`
- Modify: `src/ui/track_screen.cpp`, `src/ui/inst_screen.cpp` (name rows)
- Modify: `src/ui/transpose_dialog.cpp` (octave), check `fill_dialog.cpp`, `import_dialog.cpp`, `render_dialog.cpp`

**Step 1: ParamList** — member `bool hold_ = false;` (edit started by A). Public helper:

```cpp
  // A + turn: enters the edit of the selected row if needed (hold edit). False = row not editable.
  bool holdEdit();
```

```cpp
bool ParamList::holdEdit() {
  if (sel_ < 0 || sel_ >= count_ || !params_[sel_].edit) return false;
  if (!edit_) {
    ensureVisible();
    edit_ = true;
    hold_ = true;
    beginEdit();
  }
  return true;
}
```

In `onInput` add:

```cpp
    case InputType::EditTurn:
      if (holdEdit()) edit(ev.delta * (ev.shift ? 10 : 1));
      break;
    case InputType::EditEnd:
      if (hold_ && edit_) edit_ = false;
      hold_ = false;
      break;
    case InputType::EditCancel:
      cancelEdit();
      hold_ = false;
      break;
```

Clear `hold_` wherever `edit_` is forced off or toggled by click/tap (`setEdit`, `selectBar`, click toggle, tap) so a later EditEnd cannot close a click edit. Update the class comment (A + turn = hold edit, release = commit, B while held = cancel).

**Step 2: name rows** — in `TrackScreen::onInput` and `InstScreen::onInput` the name row edits chars on `EncTurn` while editing (`wasName`). Add before the `list_.onInput` call:

```cpp
  if (ev.type == hw::InputType::EditTurn && list_.holdEdit() && nameEdit()) {
    if (ev.shift) namePos_ = clampi(namePos_ + ev.delta, 0, kNameLen - 1);
    else list_.edit(ev.delta);  // no x10 for characters
    return;
  }
```

Check what `nameEdit()` tests and whether entering name edit needs extra setup (`leaveEdit()` on exit — after `EditEnd`/`EditCancel` the existing `if (wasName && !nameEdit()) leaveEdit();` must still run). In INST the editor page uses `editor_` (SampleEditor) instead of `list_` — route EditTurn there via `editor_.onInput` (Task 6 covers marker).

**Step 3: transpose** — mirror the octave special case:

```cpp
    case InputType::EditTurn:
      if (list_.sel() == kAmount && ev.shift && list_.holdEdit()) {
        list_.edit(ev.delta * octave());
        return;
      }
      break;
```

Read fill/import/render `onInput`: they pass other events to `list_.onInput`, so EditTurn works; make sure nothing intercepts it (e.g. fill's keyboard path).

**Step 4:** `pio run -e wt32` — SUCCESS.

---

### Task 5: GRID

**Files:**
- Modify: `src/ui/grid_screen.h`, `src/ui/grid_screen.cpp`

**Step 1:** member `bool holdEdit_ = false;`. In `onInput` switch (after fill/transpose forwarding, which already passes events to the dialogs):

```cpp
    case InputType::EditTurn:
      if (!edit_) {
        selOn_ = false;
        setEdit(true);
        holdEdit_ = true;
      }
      editTurn(ev.delta, ev.shift);
      break;
    case InputType::EditEnd:
      if (holdEdit_ && edit_) setEdit(false);
      holdEdit_ = false;
      break;
    case InputType::EditCancel:
      if (edit_) cancelCell();
      holdEdit_ = false;
      break;
```

Reset `holdEdit_ = false` wherever `edit_` turns off otherwise (setEdit(false) paths, setRec, setPerf, pattern change, click toggle).

**Step 2: onATap**:

```cpp
// A tap: empty step = the track's last note, a note step = hear it; Shift + A = clear the step.
void GridScreen::onATap(bool shift) {
  if (fill_.isOpen() || transpose_.isOpen()) return;
  cur();
  const mt::Step& st = pat().steps[track()][cur()];
  if (shift) { clear the step exactly like menu kClearStep does (reuse its code path) ; return; }
  if (st.hasNote()) previewNote(st.note);
  else setNote(lastNote_[track()]);
}
```

Read `kClearStep` handling (`grid_screen.cpp:822`) and reuse it (it likely uses `apply()` with an undo snapshot). For a drum track (`drum()`), an empty step: `toggleLane(lane_)` instead of `setNote`; a note step: preview the kit lane note — mirror `toggleLane` preview code.

**Step 3: onBack**:

```cpp
void GridScreen::onBack() {
  if (fill_.isOpen()) { fill_.onInput({hw::InputType::EncLong, 0, false}); return; }
  if (transpose_.isOpen()) { transpose_.onInput({hw::InputType::EncLong, 0, false}); return; }
  if (selOn_) { selOn_ = false; return; }
  if (edit_) setEdit(false);
  else if (rec_) setRec(false);
  else if (perf_) setPerf(false);
}
```

(Check how the screen draws rec/perf state toggles — setRec/setPerf may need `app_.toast` or invalidate as elsewhere.) Declare overrides in the header.

**Step 4:** `pio run -e wt32` — SUCCESS.

---

### Task 6: BANK, SampleEditor, MIX, onBack / onPage elsewhere

**Files:**
- Modify: `src/ui/bank_screen.{h,cpp}`, `src/ui/sample_editor.{h,cpp}`, `src/ui/track_screen.{h,cpp}`, `src/ui/inst_screen.{h,cpp}`, `src/ui/proj_screen.{h,cpp}`, `src/ui/file_screen.{h,cpp}`

**Step 1: BANK chain** (`chainInput`): `holdRow_` flag.
- `EditTurn`: if `!rowEdit_` and chain non-empty: `rowEdit_ = true; rowField_ = kFPat; snapRow(); holdRow_ = true;` (empty chain: ignore). Then same as EncTurn while editing: `ev.shift ? move rowField_ : editRow(ev.delta)`.
- `EditEnd`: `if (holdRow_) rowEdit_ = false; holdRow_ = false;`
- `EditCancel`: `if (rowEdit_) cancelRow(); holdRow_ = false;`
Pattern view (non-chain) ignores them.
- `onBack`: copy mode (`copyFrom_ >= 0`) → `copyFrom_ = -1; toast("CANCEL")`; else `rowEdit_ = false`.

**Step 2: SampleEditor** — in `onInput`, marker row:

```cpp
  if (list_.sel() == kMarkerRow && ev.type == hw::InputType::EditTurn && list_.holdEdit()) {
    hw::InputEvent t = ev;
    t.type = hw::InputType::EncTurn;
    return onInput(t);  // the editing marker branch above
  }
```

(Make sure the existing marker branch runs when `list_.editing()` is now true.) Other rows: `list_.onInput` handles EditTurn.

**Step 3: MIX** (`TrackScreen::mixerInput`): `EditTurn` → `setMasterVol(masterVol + ev.delta * (ev.shift ? 10 : 1))`.

**Step 4: onPage**: TRACK `showPage(page_ + d, false)` (not in mixer), INST `showPage(physPage() + d, false)`, PROJ `showPage(page_ + d, false)`. Check what `bar` argument means (keep selection on the bar or row 0) — use the value that keeps the list usable (row 0, `false`). Leave any running edit first (`leaveEdit()` / list `setEdit(false)`) if showPage does not do it.

**Step 5: onBack** per screen — forward a synthetic `{EncLong, 0, false}` to the open overlay's `onInput` (their EncLong = close/cancel):
- TRACK: name edit / list edit → leave edit (`list_.cancelEdit()` is not wanted; use `list_.setEdit(false)` + `leaveEdit()`); mixer: nothing.
- INST: `presets_` / `wt_` open → forward EncLong; else leave edit.
- PROJ: leave edit.
- FILE: `kb_`, `import_`, `render_`, `wifi_` open → forward EncLong; file list open → `closeList()`; sample sub-view — read file_screen.cpp around :920 and close it the way EncLong does if that exists.
- BANK: Step 1.
Verify for each overlay that EncLong really closes it (ImportDialog: first EncLong only leaves edit — acceptable).

**Step 6:** `pio run -e wt32` — SUCCESS. `pio test -e native` — all PASS.

---

### Task 7: Docs

**Files:** `docs/manual.md`, `docs/manual_ru.md`, `README.md`, `README_ru.md`

Add a "Buttons A and B" section (table from the design doc, short), mention hold-A editing next to existing click-to-edit text, pins P16/P17 in the wiring/pins part of README (find where Shift/Play P14/P15 are documented and add next to them; also `docs/wiring*.md` if pins are listed there).
