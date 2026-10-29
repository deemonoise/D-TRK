#include "grid_screen.h"
#include "edit_ops.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "audio/audio.h"
#include "fx_info.h"
#include "mut.h"
#include "note_name.h"
#include "record.h"
#include "roll.h"
#include "scale.h"
#include "render_ui.h"
#include "storage/render_io.h"

namespace ui {
namespace {

const char* const kFieldNames[] = {"NOTE",    "VEL", "FX1", "FX1 VAL", "FX2", "FX2 VAL", "FX3",
                                   "FX3 VAL", "FX4", "FX4 VAL", "FX5", "FX5 VAL", "FX6", "FX6 VAL"};
static_assert(sizeof(kFieldNames) / sizeof(kFieldNames[0]) == 2 + 2 * mt::kFxSlots, "a name per Detail field");

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool isBlackKey(int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

// A KIT lane's label: its instrument's or sample's name (4 chars), "-" when empty or no kit.
void laneName(const mt::Project& p, const mt::Instrument* k, int lane, char out[8]) {
  snprintf(out, 8, "-");
  if (!k) return;
  const mt::KitLane& ln = k->kit[lane];
  if (ln.instr < mt::kInstruments) snprintf(out, 8, "%.4s", p.instruments[ln.instr].name);
  else if (ln.sample[0]) snprintf(out, 8, "%.4s", ln.sample);
}

// Text and "has content" flag of one Detail field; out holds 4 chars + 0.
bool fieldText(const mt::Step& c, int field, char out[5]) {
  switch (field) {
    case 0:
      mt::noteName(c.note, out);
      return c.note != mt::kNoteEmpty;
    case 1:
      if (!c.vel) {
        snprintf(out, 5, "...");
        return false;
      }
      snprintf(out, 5, "%3u", c.vel);
      return true;
    default: {
      const mt::FxSlot& f = c.fx[(field - 2) / 2];
      if (field % 2 == 0) snprintf(out, 5, "%s", mt::fxName(f.cmd));
      else mt::fxFormat(f.cmd, f.val, out);
      return f.cmd != mt::Fx::None;
    }
  }
}

}  // namespace

mt::Pattern& GridScreen::pat() { return app_.project().patterns[app_.editPattern()]; }

int GridScreen::len() const {
  return clampi(app_.project().patterns[app_.editPattern()].length, mt::kMinSteps, mt::kMaxSteps);
}

int GridScreen::cur() {
  const int n = len();
  if (curStep_ >= n) curStep_ = n - 1;
  if (selOn_ && (selS0_ < selS1_ ? selS0_ : selS1_) >= n) selOn_ = false;  // selection fell off the end
  if (curStep_ < 0) curStep_ = 0;
  return curStep_;
}

int GridScreen::track() const { return app_.curTrack(); }

bool GridScreen::drumAt(int tr) const { return app_.project().trackIsDrum(tr); }

int GridScreen::rowsFor(int h) const {
  return (h - kNamesH - (keyboardShown() || padShown() ? kKbH : 0)) / kRowH;
}

void GridScreen::onEnter() {
  setEdit(false);
  cur();
  needVisible_ = true;
  if (perf()) setRec(false);  // PERF may have been turned on in SONG
}

// App releases the PERF effects and keeps / frees the buffer.
void GridScreen::onLeave() {
  fill_.cancel();
  transpose_.cancel();
}

void GridScreen::onProjectReplaced() {
  fill_.abandon();
  transpose_.cancel();
}

// The Fill dialog keeps editing the pattern it was opened for.
void GridScreen::onPatternChange() {
  recLoop_ = UINT32_MAX;  // REC hits on the new pattern get their own undo snapshot
  curStep_ = 0;
  top_ = 0;
  edit_ = false;
  holdEdit_ = false;
  editPushed_ = false;
  rollHeld_ = false;
  selOn_ = false;
  needVisible_ = true;
}

bool GridScreen::wantsRedraw(const engine::Status& st) {
  if (st.playing && !wasPlaying_) dragFrozen_ = false;  // Play re-enables follow after a Drag
  wasPlaying_ = st.playing;
  return false;
}

// ---- cursor ----

void GridScreen::selFollow() {
  if (!selOn_) return;
  selT1_ = track();
  selS1_ = curStep_;
}

void GridScreen::cursorMoved() {
  fxCycled_ = false;
  if (edit_) snapCell();
  selFollow();
  needVisible_ = true;
  lastMoveMs_ = millis();
}

void GridScreen::moveStep(int d) {
  const int n = len();
  curStep_ = ((cur() + d) % n + n) % n;
  cursorMoved();
}

void GridScreen::moveTrack(int d) {
  app_.setCurTrack(track() + d);
  cursorMoved();
}

void GridScreen::moveField(int d) {
  constexpr int n = mt::kTracks * kFields;
  const int i = ((track() * kFields + curField_ + d) % n + n) % n;
  app_.setCurTrack(i / kFields);
  curField_ = i % kFields;
  cursorMoved();
}

void GridScreen::setEdit(bool on) {
  if (on) {  // edit is exclusive with the live modes
    setRec(false);
    setPerf(false);
  }
  if (on && !edit_) {
    editPushed_ = false;
    fxCycled_ = false;
    app_.toast(kFieldNames[curField_]);
  }
  edit_ = on;
  if (!on) holdEdit_ = false;
  if (on) snapCell();
  needVisible_ = true;  // the keyboard changes the row count
}

void GridScreen::snapCell() {
  cellOthers_ = editPushed_;
  cellPat_ = app_.editPattern();
  cellTr_ = track();
  cellStep_ = cur();
  cellOrig_ = pat().steps[cellTr_][cellStep_];
}

void GridScreen::cancelCell() {
  setEdit(false);
  if (cellPat_ < 0 || cellPat_ >= mt::kPatterns) return;
  mt::Step& st = app_.project().patterns[cellPat_].steps[cellTr_][cellStep_];
  if (memcmp(&st, &cellOrig_, sizeof(st)) != 0) {
    engine::lockProject();
    st = cellOrig_;
    engine::unlockProject();
    engine::post(engine::Cmd::ReleaseTies);  // a held TIE may have lost its step
    // Only this cell was written in the edit: its snapshot is not an undo step any more.
    if (editPushed_ && !cellOthers_) app_.dropUndo();
    else app_.markDirty();
  }
  editPushed_ = false;
  cellPat_ = -1;
  app_.toast("CANCEL");
}

void GridScreen::toggleView() {
  setView(view_ == View::Overview ? View::Detail : (view_ == View::Detail ? View::Roll : View::Overview));
}

void GridScreen::setView(View v) {
  view_ = v;
  if (v != View::Detail) curField_ = kNote;
  setEdit(false);
  rollHeld_ = false;
  if (v == View::Roll) {  // the cursor row: the note under the cursor, else the track's last one
    const mt::Step& st = pat().steps[track()][cur()];
    rollNote_ = st.hasNote() && !drum() ? st.note : lastNote_[track()];
    needVisible_ = true;
  }
}

void GridScreen::undo() {
  editPushed_ = false;  // the next edit takes a fresh snapshot
  recLoop_ = UINT32_MAX;  // so do the next REC hits
  const bool ok = app_.doUndo();
  if (ok) engine::post(engine::Cmd::ReleaseTies);
  app_.toast(ok ? "UNDO" : "NOTHING TO UNDO");
}

// ---- editing ----

void GridScreen::writeStep(const mt::Step& st) {
  const int tr = track(), step = cur();
  if (!editPushed_) {
    app_.pushUndo();
    editPushed_ = true;
  }
  engine::lockProject();
  pat().steps[tr][step] = st;
  engine::unlockProject();
  app_.markDirty();
}

void GridScreen::setNote(uint8_t note) {
  mt::Step st = pat().steps[track()][cur()];
  st.note = note;
  lastNote_[track()] = note;
  writeStep(st);
  previewNote(note);
}

// Entered notes sound on INT tracks (FILE > SYSTEM Preview, a device setting), with the track's instrument.
void GridScreen::previewNote(uint8_t note) {
  const mt::Project& p = app_.project();
  const mt::TrackCfg& t = p.tracks[track()];
  if (!app_.preview() || t.out != mt::TrackOut::Int || note > 127) return;
  audio::preview(t.instr < mt::kInstruments ? t.instr : 0, note, static_cast<uint8_t>(track()));
}

// Drum track: flips the lane's bit; an empty (or OFF) step becomes a note step at the track's velocity.
// A mask that becomes 0 stays a note step (Clear step removes it).
void GridScreen::toggleLane(int lane) {
  mt::Step st = pat().steps[track()][cur()];
  if (!st.hasNote()) {
    st.note = 0;
    mt::setLaneMask(st, 0);
  }
  mt::setLane(st, lane, !mt::laneOn(st, lane));
  lane_ = lane;
  writeStep(st);
  if (mt::laneOn(st, lane))
    if (const mt::Instrument* k = app_.project().kitOf(track())) previewNote(k->kit[lane].note);
}

uint8_t GridScreen::degreeNote(int button, bool octaveUp, int ref) const {
  const mt::Project& p = app_.project();
  const int base = ref / 12 * 12 + (octaveUp ? 12 : 0);
  return static_cast<uint8_t>(mt::degreeNote(button, p.scaleRoot, static_cast<mt::ScaleType>(p.scaleType), base));
}

// Octave from the note under the cursor, else the track's last note (60 at start).
void GridScreen::enterDegree(int button, bool octaveUp) {
  const int tr = track();
  const mt::Step& st = pat().steps[tr][cur()];
  setNote(degreeNote(button, octaveUp, st.hasNote() ? st.note : lastNote_[tr]));
  if (editStep_) moveStep(editStep_);
}

void GridScreen::setRec(bool on) {
  if (on) {
    setPerf(false);
    edit_ = false;
    holdEdit_ = false;
    recLoop_ = UINT32_MAX;
  }
  rec_ = on;
}

void GridScreen::setPerf(bool on) {
  if (on) {
    rec_ = false;
    edit_ = false;
    holdEdit_ = false;
  }
  app_.perf().setOn(on);
}

bool GridScreen::perf() const { return app_.perf().on(); }

bool GridScreen::buttonsBusy() const { return edit_ || rec_ || perf(); }

// The track step heard at the play position: trackStepAt, sub-step 0 on a fast track. A slow
// track holds the step it played last (back to the last pattern step where c % D == 0, maybe in
// the previous pass).
int GridScreen::heardTrackStep(int tr) {
  const engine::Status& st = app_.status();
  const mt::Pattern& pt = pat();
  const int d = mt::speedDiv(mt::toSpeed(pt.trackSpeed[tr]));
  if (d <= 1) return mt::trackStepAt(pt, tr, st.pos, st.loop, 0);
  const int tl = pt.trackLen[tr] && pt.trackLen[tr] < pt.length ? pt.trackLen[tr] : pt.length;
  if (tl <= 0) return 0;
  const uint64_t c = static_cast<uint64_t>(st.loop) * pt.length + st.pos;
  return static_cast<int>(((c - c % d) / d) % tl);
}

// REC: the note lands on the heard step of the track (its own length and speed), or the next one
// past its half. Melodic: button N = scale degree in the octave of the track's last note, Shift + N
// clears the step's note. Drum track: button N sets lane N, Shift + N clears it.
bool GridScreen::recordKey(int n, bool shift) {
  const int tr = track();
  if (drumAt(tr)) recordAt(tr, n, 0, 0, shift, true, true);
  else recordAt(tr, -1, shift ? 0 : degreeNote(n, false, lastNote_[tr]), 0, shift, true, true);
  return true;
}

int GridScreen::recordAt(int tr, int lane, uint8_t note, uint8_t vel, bool clear, bool preview, bool setLast) {
  const engine::Status& st = app_.status();
  const mt::Pattern& pt = pat();
  const int tl = pt.trackLen[tr] && pt.trackLen[tr] < len() ? pt.trackLen[tr] : len();
  const mt::TrackSpeed sp = mt::toSpeed(pt.trackSpeed[tr]);
  const int mul = mt::speedMul(sp);
  const int div = mt::speedDiv(sp);
  uint8_t phase = engine::stepPhase(st);
  // xN: the pattern step holds N track steps, the phase picks one and the phase inside it. A slow
  // track step spans D pattern steps: the phase counts over all of them.
  if (div > 1) {
    const uint64_t c = static_cast<uint64_t>(st.loop) * pt.length + st.pos;
    phase = static_cast<uint8_t>((static_cast<int>(c % div) * 256 + phase) / div);
  }
  const int sub = phase * mul / 256;
  const int step = mt::recordStepFor((heardTrackStep(tr) + sub) % tl, static_cast<uint8_t>((phase * mul) % 256), tl);
  if (st.loop != recLoop_ || app_.editPattern() != recPat_) {  // one undo snapshot per pass and pattern
    app_.pushUndo();
    recLoop_ = st.loop;
    recPat_ = app_.editPattern();
  }
  mt::Step s = pt.steps[tr][step];
  uint8_t sound = 0xFF;  // note to preview
  if (lane >= 0) {
    if (clear) {
      if (s.hasNote()) mt::setLane(s, lane, false);
    } else {
      if (!s.hasNote()) {
        s.note = 0;
        mt::setLaneMask(s, 0);
      }
      mt::setLane(s, lane, true);
      if (const mt::Instrument* k = app_.project().kitOf(tr)) sound = k->kit[lane].note;
    }
  } else if (clear) {
    mt::clearNote(s);
  } else {
    s.note = note;
    if (vel) s.vel = vel > 127 ? 127 : vel;
    if (setLast) lastNote_[tr] = s.note;
    sound = s.note;
  }
  engine::lockProject();
  pat().steps[tr][step] = s;
  engine::unlockProject();
  app_.markDirty();
  if (preview && sound <= 127) previewNote(sound);
  return step;
}

// ---- piano roll ----

void GridScreen::rollMove(int d) {
  if (drum()) lane_ = clampi(lane_ + d, 0, mt::kKitLanes - 1);
  else rollNote_ = clampi(rollNote_ + d, 0, 127);
  needVisible_ = true;
  lastMoveMs_ = millis();
}

// The row's note into the cursor step: a note step keeps its fx and velocity, an empty (or OFF) one
// takes the track's velocity and length (its GAT goes: the track's Def gate plays).
void GridScreen::rollPlace() {
  const int tr = track();
  mt::Step st = pat().steps[tr][cur()];
  const uint8_t note = static_cast<uint8_t>(rollNote_);
  lastNote_[tr] = note;
  if (st.note != note) {
    if (!st.hasNote()) {
      st.vel = mt::kVelDefault;
      st.lanesHi = 0;
      for (mt::FxSlot& f : st.fx)
        if (f.cmd == mt::Fx::GAT) f = mt::FxSlot();
    }
    st.note = note;
    writeStep(st);
  }
  previewNote(note);
}

// A + turn: the first turn of the press places the row's note (if the step holds another), then each
// turn moves its GAT value as GRID's GAT value edit does (gatStep: a unit, Shift +-10 % as shown),
// from the step's GAT, else Def gate; Def gate itself removes GAT. KIT: no lengths.
void GridScreen::rollStretch(int d, bool shift) {
  if (!rollHeld_) {  // a new A press: its own undo snapshot, the cell kept for A + B (chordClear)
    rollHeld_ = true;
    editPushed_ = false;
    snapCell();
  }
  if (drum()) return;
  const int tr = track();
  if (pat().steps[tr][cur()].note != rollNote_) rollPlace();
  mt::Step st = pat().steps[tr][cur()];
  const uint8_t def = app_.project().tracks[tr].defGate;
  const int was = mt::rollGateVal(st, def);
  const int v = mt::gatStep(static_cast<uint8_t>(was), d, shift);
  if (v == was) return;
  if (mt::rollSetGate(st, static_cast<uint8_t>(v), def) == mt::RollLen::NoSlot) {
    app_.toast("NO FX SLOT");
    return;
  }
  writeStep(st);
  char val[5], msg[16];
  mt::fxFormat(mt::Fx::GAT, static_cast<uint8_t>(v), val);  // as the fx column shows it
  const char* t = val;
  while (*t == ' ') ++t;
  snprintf(msg, sizeof(msg), "GATE %s", t);
  app_.toast(msg);
}

int GridScreen::rollRows(int h) const {
  if (drum()) return mt::kKitLanes;
  const int r = (h - kNamesH) / kRowH;
  return r > 128 ? 128 : (r < 1 ? 1 : r);
}

// Tap: the cursor to the cell (step and row). Long tap: there, then the menu. Drag: scrolls the pitch.
void GridScreen::rollTouch(const TouchEvent& ev) {
  const int rows = rollRows(h_);
  if (ev.type == TouchType::Drag) {
    if (!drum()) rollLow_ = clampi(rollLow_ + ev.dy / kRowH, 0, 128 - rows);
    needVisible_ = false;
    return;
  }
  if (ev.type == TouchType::LongPress && app_.shift()) return;  // nothing (undo is Shift + B only)
  const int gridY = y0_ + kNamesH;
  if (ev.x < kNumW || ev.y < gridY) return;
  const int col = (ev.x - kNumW) / kRollColW;
  const int rowH = drum() ? (h_ - kNamesH) / mt::kKitLanes : kRowH;
  const int row = (ev.y - gridY) / (rowH > 0 ? rowH : 1);
  const int step = rollLeft_ + col;
  if (col >= kRollCols || step >= len() || row >= rows) return;
  if (ev.type != TouchType::Tap && ev.type != TouchType::LongPress) return;
  curStep_ = step;
  if (drum()) lane_ = row;
  else rollNote_ = clampi(rollLow_ + rows - 1 - row, 0, 127);
  selOn_ = false;
  cursorMoved();
  if (ev.type == TouchType::LongPress) openMenu();
}

void GridScreen::editTurn(int delta, bool shift) {
  const int tr = track();
  mt::Step st = pat().steps[tr][cur()];
  const mt::Project& p = app_.project();
  if (drum() && curField_ == kNote) {  // turn: lane cursor; Shift + turn: toggle the lane
    if (shift) toggleLane(lane_);
    else lane_ = clampi(lane_ + delta, 0, mt::kKitLanes - 1);
    return;
  }
  if (drum() && curField_ == kVel) {  // the step velocity lives in note
    if (!st.hasNote()) return;
    st.note = static_cast<uint8_t>(clampi((st.note ? st.note : p.tracks[tr].defVel) + delta * (shift ? 10 : 1), 1, 127));
    writeStep(st);
    return;
  }
  switch (curField_) {
    case kNote:
      if (st.note == mt::kNoteOff) st.note = 60;
      else if (!st.hasNote()) st.note = lastNote_[tr];
      else if (shift) st.note = static_cast<uint8_t>(clampi(st.note + delta * 12, 0, 127));
      else st.note = static_cast<uint8_t>(
               mt::moveDegrees(st.note, delta, p.scaleRoot, static_cast<mt::ScaleType>(p.scaleType)));
      lastNote_[tr] = st.note;
      writeStep(st);
      previewNote(st.note);
      return;
    case kVel: st.vel = static_cast<uint8_t>(clampi(st.vel + delta * (shift ? 10 : 1), 0, 127)); break;
    default: {  // a command or its value
      const int slot = (curField_ - kFx1) / 2;
      const bool isCmd = (curField_ - kFx1) % 2 == 0;
      mt::FxSlot& f = st.fx[slot];
      mt::FxSlot& last = lastFx_[tr][slot];
      if (f.cmd == mt::Fx::None && last.cmd != mt::Fx::None && !fxCycled_) {
        f = last;  // empty slot: the first turn repeats the last FX with its value
      } else if (isCmd) {
        f.cmd = mt::fxNextCmd(f.cmd, delta);
        f.val = f.cmd == mt::Fx::None ? 0 : mt::fxDefault(f.cmd);
      } else {
        if (f.cmd == mt::Fx::None) return;
        const int mult = shift && f.cmd != mt::Fx::CND ? 10 : 1;
        f.val = f.cmd == mt::Fx::GAT ? mt::gatStep(f.val, delta, shift)  // Shift: +-10 % as shown
                                     : mt::fxStep(f.cmd, f.val, delta * mult);
      }
      if (f.cmd != mt::Fx::None) last = f;
      if (isCmd) fxCycled_ = true;
      if (isCmd && f.cmd != mt::Fx::None) app_.toast(mt::fxLongName(f.cmd));
      break;
    }
  }
  writeStep(st);
}

// ---- input ----

void GridScreen::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  if (fill_.isOpen()) {
    fill_.onInput(ev);
    return;
  }
  if (transpose_.isOpen()) {
    transpose_.onInput(ev);
    return;
  }
  cur();
  switch (ev.type) {
    case InputType::EncTurn:
      if (app_.perf().turn(ev.delta, ev.shift)) break;  // the last pressed effect's depth; the cursor stays
      if (edit_) editTurn(ev.delta, ev.shift);
      else if (!ev.shift) moveStep(ev.delta);
      else if (roll()) rollMove(ev.delta);
      else if (detail()) moveField(ev.delta);
      else moveTrack(ev.delta);
      break;
    case InputType::EncClick:
      if (ev.shift && edit_) cancelCell();
      else if (ev.shift) toggleView();
      else if (selOn_) selOn_ = false;  // click ends a selection instead of entering edit
      else if (!roll()) setEdit(!edit_);  // Roll has no edit mode: A writes
      break;
    case InputType::EncLong:
      if (ev.shift) break;  // nothing: undo is Shift + B only
      setEdit(false);
      openMenu();
      break;
    case InputType::EditTurn:  // A + turn: edits the cell, entering edit for as long as A is held
      if (app_.perf().xfadeTurn(ev.delta, ev.shift)) break;  // PERF, nothing held: the scene crossfader
      if (roll()) {
        rollStretch(ev.delta, ev.shift);
        break;
      }
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
      if (rollHeld_) editPushed_ = false;  // the next A press is its own undo step
      rollHeld_ = false;
      break;
    case InputType::EditCancel:  // B while A + turn edits: A + B clears (the edit mode entered by A ends)
      if (edit_ || rollHeld_) {
        chordClear();
        if (holdEdit_) setEdit(false);
      }
      holdEdit_ = false;
      rollHeld_ = false;
      break;
    default: break;
  }
}

// Hold A + B: the selection, else the whole step under the cursor (fx too). One undo step: an A press
// that already wrote this cell (A + turn) shares its snapshot with the clear, and a cell left as it was
// before the press drops it. An empty step outside such a press: no snapshot.
bool GridScreen::chordClear() {
  if (dialogOpen()) return false;
  const int tr = track(), step = cur();
  const bool own = (holdEdit_ || rollHeld_) && editPushed_ && !cellOthers_ && cellPat_ == app_.editPattern() &&
                   cellTr_ == tr && cellStep_ == step;
  const mt::Sel sel = selOn_ ? curSel() : mt::makeSel(tr, step, tr, step);
  if (!own && !selOn_ && pat().steps[tr][step].isEmpty()) return true;  // nothing to clear
  if (!own) app_.pushUndo();
  engine::lockProject();
  mt::clearSel(pat(), sel);
  engine::unlockProject();
  engine::post(engine::Cmd::ReleaseTies);  // a held TIE may have lost its step
  if (own && !selOn_ && memcmp(&pat().steps[tr][step], &cellOrig_, sizeof(mt::Step)) == 0) app_.dropUndo();
  else app_.markDirty();
  editPushed_ = false;  // the clear has its own snapshot; edit continues from the cleared cell
  if (edit_) snapCell();
  return true;
}

// A tap: empty step = the track's last note (drum: the lane under the cursor), a note step = hear it.
// Shift + A does nothing here (clear is hold A + B). In edit, an entered note moves the cursor by Edit
// step like a track button.
void GridScreen::onATap(bool shift) {
  if (dialogOpen() || shift) return;
  const int tr = track(), step = cur();
  if (!edit_) editPushed_ = false;  // outside edit every tap is its own undo step
  if (roll()) {  // the row's note, KIT: the lane's bit
    if (drum()) toggleLane(lane_);
    else rollPlace();
    return;
  }
  const mt::Step& st = pat().steps[tr][step];
  if (drum()) {
    if (!st.hasNote()) {
      toggleLane(lane_);  // the cursor stays, as with a track button
    } else if (const mt::Instrument* k = app_.project().kitOf(tr)) {
      int lane = lane_;  // the cursor lane, else the first lane of the step
      for (int l = 0; !mt::laneOn(st, lane) && l < mt::kKitLanes; ++l) lane = l;
      if (mt::laneOn(st, lane)) previewNote(k->kit[lane].note);
    }
    return;
  }
  if (st.hasNote()) {
    previewNote(st.note);
    return;
  }
  setNote(lastNote_[tr]);
  if (edit_ && editStep_) moveStep(editStep_);
}

// Back: closes a dialog, else drops the selection, else leaves edit / REC / PERF.
void GridScreen::onBack() {
  if (fill_.isOpen()) {
    fill_.onInput({hw::InputType::EncLong, 0, false});
    return;
  }
  if (transpose_.isOpen()) {
    transpose_.onInput({hw::InputType::EncLong, 0, false});
    return;
  }
  if (selOn_) {
    selOn_ = false;
  } else if (edit_) {
    setEdit(false);
  } else if (rec_) {
    setRec(false);
    app_.toast("REC OFF");
  } else if (perf()) {
    setPerf(false);
    app_.toast("PERF OFF");
  }
}

bool GridScreen::trackKey(int n, bool shift) {
  if (fill_.isOpen() || transpose_.isOpen()) return true;
  cur();
  const bool playing = app_.status().playing;
  if (rec_ && playing) return recordKey(n, shift);
  // Button N holds its PROJ -> PERF effect while playing; Shift + N still mutes.
  if (!shift && app_.perf().press(n)) return true;
  if (edit_) {
    if (drum()) toggleLane(shift ? n + mt::kKitLanesLo : n);  // lanes 1-8, Shift: 9-16; the cursor stays
    else enterDegree(n, shift);
    return true;
  }
  if (shift) return false;
  app_.setCurTrack(firstTrack() + n);  // button N = track N of the visible half
  cursorMoved();
  return true;
}

bool GridScreen::hit(int x, int y, int& step, int& tr, int& field) const {
  const int gridY = y0_ + kNamesH;
  if (y < gridY || x < kNumW) return false;
  const int row = (y - gridY) / kRowH;
  if (row >= rows_) return false;
  step = top_ + row;
  if (step >= len()) return false;
  if (!detail()) {
    const int col = (x - kNumW) / kColW;
    if (col >= kOverviewTracks) return false;
    tr = firstTrack() + col;
    field = kNote;
    return true;
  }
  tr = track();
  field = clampi((x - kNumW) / kFieldW, 0, kFields - 1);
  return true;
}

void GridScreen::onTouch(const TouchEvent& ev) {
  if (fill_.isOpen()) {
    fill_.onTouch(ev);
    return;
  }
  if (transpose_.isOpen()) {
    transpose_.onTouch(ev);
    return;
  }
  cur();
  if (roll()) {
    rollTouch(ev);
    return;
  }
  if (ev.type == TouchType::Drag) {
    const int n = len();
    const int maxTop = n > rows_ ? n - rows_ : 0;
    top_ = clampi(top_ - ev.dy / kRowH, 0, maxTop);
    dragFrozen_ = true;
    needVisible_ = false;
    return;
  }
  if (ev.type == TouchType::LongPress && app_.shift()) return;  // nothing (undo is Shift + B only)

  // Lane pad.
  if (padShown() && ev.y >= y0_ + h_ - kKbH) {
    if (ev.type == TouchType::Tap) {
      const int col = clampi(ev.x / kPadW, 0, kPadCols - 1);
      const int row = clampi((ev.y - (y0_ + h_ - kKbH)) / kPadRowH, 0, 1);
      toggleLane(clampi(row * kPadCols + col, 0, mt::kKitLanes - 1));
    }
    return;
  }

  // Mini keyboard.
  if (keyboardShown() && ev.y >= y0_ + h_ - kKbH) {
    if (ev.type != TouchType::Tap) return;
    const mt::Step& st = pat().steps[track()][curStep_];
    const int base = ((st.hasNote() ? st.note : lastNote_[track()]) / 12) * 12;
    const int n = base + clampi(ev.x / kKeyW, 0, 11);
    if (n <= 127) setNote(static_cast<uint8_t>(n));
    return;
  }

  // Track names: mute, Shift = solo. REC: picks the track to record into (the buttons are notes).
  if (ev.y < y0_ + kNamesH) {
    if (ev.type != TouchType::Tap || ev.x < kNumW) return;
    const int col = (ev.x - kNumW) / kColW;
    if (!detail() && col >= kOverviewTracks) return;
    const int tr = detail() ? track() : firstTrack() + col;
    if (rec_ && !app_.shift()) {
      app_.setCurTrack(tr);
      cursorMoved();
      return;
    }
    mt::TrackCfg& t = app_.project().tracks[tr];
    engine::lockProject();
    if (app_.shift()) t.solo = !t.solo;
    else t.mute = !t.mute;
    engine::unlockProject();
    app_.markDirty();
    return;
  }

  int step, tr, field;
  if (!hit(ev.x, ev.y, step, tr, field)) return;
  const bool same = step == curStep_ && tr == track() && field == curField_;
  auto moveTo = [&]() {
    curStep_ = step;
    app_.setCurTrack(tr);
    curField_ = field;
    cursorMoved();
  };

  if (ev.type == TouchType::LongPress) {
    setEdit(false);
    moveTo();
    openMenu();
    return;
  }
  // Tap.
  if (app_.shift()) {  // selection: anchor, then end
    if (!selOn_) {
      selOn_ = true;
      selT0_ = tr;
      selS0_ = step;
    }
    selT1_ = tr;
    selS1_ = step;
    setEdit(false);
    moveTo();
    return;
  }
  selOn_ = false;
  if (same) {
    setEdit(!edit_);
  } else {
    setEdit(false);
    moveTo();
  }
}

// ---- context menu ----

bool GridScreen::selected(int tr, int step) const {
  if (!selOn_) return false;
  const int t0 = selT0_ < selT1_ ? selT0_ : selT1_, t1 = selT0_ < selT1_ ? selT1_ : selT0_;
  const int s0 = selS0_ < selS1_ ? selS0_ : selS1_, s1 = selS0_ < selS1_ ? selS1_ : selS0_;
  return tr >= t0 && tr <= t1 && step >= s0 && step <= s1;
}

mt::Sel GridScreen::curSel() const {
  const int last = len() - 1;
  return mt::makeSel(selT0_, clampi(selS0_, 0, last), selT1_, clampi(selS1_, 0, last));
}

mt::Sel GridScreen::trackSel() const {
  const int last = len() - 1;
  return mt::makeSel(track(), 0, track(), last);
}

void GridScreen::openMenu() {
  mt::Clipboard* cb = app_.clipboard();
  const bool canCopy = cb != nullptr;
  const bool canPaste = cb && cb->tracks > 0 && cb->steps > 0;
  const bool canUndo = app_.undo() && app_.undo()->size() > 0;
  MenuItem items[Menu::kMaxItems];
  int n = 0;
  auto add = [&](const char* label, int id, bool en) { items[n++] = MenuItem{label, id, en}; };
  char title[28];
  char stepLabel[20];
  if (selOn_) {
    const mt::Sel s = curSel();
    snprintf(title, sizeof(title), "SEL T%d-%d  %d-%d", s.t0 + 1, s.t1 + 1, s.s0 + 1, s.s1 + 1);
    add("Copy sel", kCopySel, canCopy);
    add("Paste", kPaste, canPaste);
    add("Clear sel", kClearSel, true);
    add("Note OFF sel", kNoteOffSel, true);
    add("Transpose...", kTranspose, true);
    add("Fill...", kFill, true);
  } else {
    // Most used first (the first 12 show without scrolling); slow / rare ones at the end.
    snprintf(title, sizeof(title), "STEP %d  %s", cur() + 1, app_.project().tracks[track()].name);
    snprintf(stepLabel, sizeof(stepLabel), "Edit step: %d", editStep_);
    add(stepLabel, kEditStep, true);
    add("Copy step", kCopyStep, canCopy);
    add("Paste", kPaste, canPaste);
    add("Clear step", kClearStep, true);
    add("Note OFF", kNoteOff, true);
    add("Undo", kUndo, canUndo);
    add("Select", kSelect, true);
    add("Copy track", kCopyTrack, canCopy);
    add("Clear track", kClearTrack, true);
    add("Transpose track...", kTranspose, true);
    add("Fill...", kFill, true);
    if (app_.project().genOn(track()) || mt::mutOn(app_.project(), track())) add("Capture live", kCaptureLive, true);
    add(rec_ ? "Rec: ON" : "Rec: OFF", kRec, true);
    add(perf() ? "Perf: ON" : "Perf: OFF", kPerf, true);
    add(view_ == View::Overview ? "Detail view" : "Overview", kToggleView, true);
    if (!roll()) add("Piano roll", kRollView, true);
    add(follow_ ? "Follow: ON" : "Follow: OFF", kToggleFollow, true);
    add("Resample track", kResampleTrack, true);
    add("Resample pattern", kResamplePattern, true);
  }
  if (selOn_) {
    add("Undo", kUndo, canUndo);
    add("Clear selection", kDropSel, true);
  }
  menuPattern_ = app_.editPattern();
  app_.menu().open(title, items, n, [this](int id) { onMenu(id); });
}

// 0..kEditStepMax: how far the cursor moves after a note entered with a track button.
void GridScreen::openEditStepMenu() {
  static_assert(kEditStepMax + 1 <= Menu::kMaxItems, "Edit step values fit the menu");
  MenuItem items[kEditStepMax + 1];
  char labels[kEditStepMax + 1][12];
  for (int v = 0; v <= kEditStepMax; ++v) {
    snprintf(labels[v], sizeof(labels[v]), "%s%d%s", v == editStep_ ? "> " : "  ", v, v == 0 ? " (stay)" : "");
    items[v] = MenuItem{labels[v], kEditStepVal + v, true};
  }
  app_.menu().open("EDIT STEP", items, kEditStepMax + 1, [this](int id) { onMenu(id); });
}

// The selection, else the current track up to its own length (polymeter). The column under the
// cursor picks what is filled.
void GridScreen::openFill() {
  const int tr = track();
  mt::Sel sel = selOn_ ? curSel() : trackSel();
  if (!selOn_) {
    const int tl = pat().trackLen[tr];
    if (tl > 0 && tl - 1 < sel.s1) sel.s1 = static_cast<uint8_t>(tl - 1);
  }
  mt::FillSpec& f = fillSpec_;
  const mt::FillTarget was = f.target;
  const uint8_t wasSlot = f.slot;
  if (curField_ == kVel) {
    f.target = mt::FillTarget::Vel;
  } else if (curField_ >= kFx1) {
    f.target = mt::FillTarget::Fx;
    f.slot = static_cast<uint8_t>((curField_ - kFx1) / 2);
  } else {
    f.target = mt::FillTarget::Note;
  }
  if (!fillInit_ || f.target != was || (f.target == mt::FillTarget::Fx && f.slot != wasSlot)) {
    fillInit_ = true;
    if (f.target == mt::FillTarget::Note) {  // from the last note entered on the track
      f.from = lastNote_[tr];
      f.to = static_cast<uint8_t>(f.from + 12 > 127 ? 127 : f.from + 12);
    } else if (f.target == mt::FillTarget::Vel) {
      f.from = 100;
      f.to = 127;
    } else {  // the command in that slot under the cursor, else the last one written there
      const mt::FxSlot& here = pat().steps[tr][cur()].fx[f.slot];
      const mt::FxSlot& src = here.cmd != mt::Fx::None ? here : lastFx_[tr][f.slot];
      f.cmd = src.cmd;
      f.from = f.to = src.cmd == mt::Fx::None ? 0 : src.val;
    }
  }
  if (drum()) f.lane = static_cast<uint8_t>(lane_);
  if (!arpInit_) {
    arpInit_ = true;
    arpSpec_.root = lastNote_[tr];
    bassSpec_.root = static_cast<uint8_t>(36 + lastNote_[tr] % 12);  // its pitch class in octave 2
  }
  // default: the first track of the selection
  arpSpec_.dest = chordSpec_.dest = bassSpec_.dest = sel.t0;
  setEdit(false);
  if (!fill_.open(app_.editPattern(), sel, &f, &arpSpec_, &chordSpec_, &bassSpec_, drum())) app_.toast("NO MEMORY");
}

void GridScreen::captureLive() {
  const int tr = track();
  const engine::Status& es = app_.status();
  if ((es.playing || es.paused) && es.pattern != app_.editPattern()) {  // the state is the heard pattern's
    app_.toast("CAPTURE: OTHER PATTERN");
    return;
  }
  mt::Pattern* scratch = app_.undoScratch();  // free outside the undo calls
  if (!scratch) {
    app_.toast("NO MEMORY");
    return;
  }
  app_.pushUndo(app_.editPattern(), tr);  // the GenCfg too: the capture turns GEN off
  engine::lockProject();
  const mt::LiveCapture r = engine::captureLive(tr, app_.editPattern(), scratch->steps[0]);
  engine::unlockProject();
  if (r != mt::LiveCapture::Ok) {
    app_.dropUndo();
    app_.toast(r == mt::LiveCapture::OtherPattern ? "CAPTURE: OTHER PATTERN" : "CAPTURE: GEN / MUT OFF");
    return;
  }
  app_.markDirty();
  engine::post(engine::Cmd::ReleaseTies);
  app_.toast("LIVE CAPTURED");
}

// Renders the heard pattern offline (the track alone, or every audible track) into a project sample
// RS1, RS2, ...: exactly one loop (the pattern, or the track's own length), the delay / reverb tail
// wrapped into its start, at the level as played (mt::resamplePlan). Playback is stopped first.
void GridScreen::resample(bool wholePattern) {
  mt::Project& p = app_.project();
  if (!wholePattern && !p.trackInternal(track())) {
    app_.toast("MIDI TRACK");
    return;
  }
  if (!renderReady(app_)) return;
  uint16_t mask = 0;
  for (int t = 0; t < mt::kTracks; ++t)
    if (wholePattern ? p.trackAudible(t) : t == track()) mask |= static_cast<uint16_t>(1u << t);
  const mt::ResamplePlan plan =
      mt::resamplePlan(p, app_.editPattern(), mask, wholePattern ? -1 : track(), storage::kResampleMaxFrames);
  char name[mt::kSampleNameMax + 1];
  storage::RenderStats rs;
  const storage::Result r = storage::resample(p, plan, name, rs, App::renderProgress, &app_);
  app_.endProgress();
  if (r != storage::Result::Ok && r != storage::Result::Capped) {
    renderResultToast(app_, r);
    return;
  }
  app_.markDirty();
  char msg[48];
  mt::renderDoneText(name, rs.frames, rs.clips, msg, sizeof msg);
  if (r == storage::Result::Capped) strlcat(msg, ", 60 S CAP", sizeof msg);
  app_.toast(msg);
}

void GridScreen::apply(const mt::Sel& sel, int id) {
  app_.pushUndo();
  engine::lockProject();
  mt::Pattern& pt = pat();
  switch (id) {
    case kNoteOff:
    case kNoteOffSel:
      for (int t = sel.t0; t <= sel.t1; ++t)
        for (int i = sel.s0; i <= sel.s1; ++i) {
          pt.steps[t][i].note = mt::kNoteOff;
          pt.steps[t][i].vel = mt::kVelDefault;
          pt.steps[t][i].lanesHi = 0;  // a drum step's lanes 9-16 go with its lanes 1-8
        }
      break;
    default: mt::clearSel(pt, sel); break;
  }
  engine::unlockProject();
  app_.markDirty();
  engine::post(engine::Cmd::ReleaseTies);  // a held TIE may have lost its step
}

void GridScreen::openTranspose() {
  char title[28];
  const mt::Sel sel = selOn_ ? curSel() : trackSel();
  if (selOn_) snprintf(title, sizeof(title), "SEL T%d-%d %d-%d", sel.t0 + 1, sel.t1 + 1, sel.s0 + 1, sel.s1 + 1);
  else snprintf(title, sizeof(title), "T%d %s", sel.t0 + 1, app_.project().tracks[sel.t0].name);
  setEdit(false);
  const int pattern = app_.editPattern();
  transpose_.open(title, [this, sel, pattern](int amount, bool degrees) {
    if (app_.editPattern() != pattern) {  // heard pattern changed while the dialog was open
      app_.toast("PATTERN CHANGED");
      return;
    }
    transpose(sel, amount, degrees);
  });
}

void GridScreen::transpose(const mt::Sel& sel, int amount, bool degrees) {
  const mt::Project& p = app_.project();
  bool drumTr[mt::kTracks];
  bool any = false;  // a melodic track in the selection
  for (int t = 0; t < mt::kTracks; ++t) {
    drumTr[t] = drumAt(t);
    if (t >= sel.t0 && t <= sel.t1 && !drumTr[t]) any = true;
  }
  if (!any) {
    app_.toast("DRUM TRACK");
    return;
  }
  app_.pushUndo();
  engine::lockProject();
  mt::transposeSel(pat(), sel, amount, degrees, p.scaleRoot, static_cast<mt::ScaleType>(p.scaleType), drumTr);
  engine::unlockProject();
  app_.markDirty();
  engine::post(engine::Cmd::ReleaseTies);
  app_.toast("TRANSPOSED");
}

void GridScreen::onMenu(int id) {
  // The heard pattern changed while the menu was open: edits of steps would hit the wrong one.
  // View, follow, undo, REC / PERF and dropping the selection do not depend on it.
  if (id == kEditStep) {
    openEditStepMenu();
    return;
  }
  if (id >= kEditStepVal && id <= kEditStepVal + kEditStepMax) {
    editStep_ = id - kEditStepVal;
    char msg[20];
    snprintf(msg, sizeof(msg), "EDIT STEP %d", editStep_);
    app_.toast(msg);
    return;
  }
  const bool anyPattern = id == kToggleView || id == kRollView || id == kToggleFollow || id == kUndo || id == kRec || id == kPerf ||
                          id == kDropSel;
  if (!anyPattern && app_.editPattern() != menuPattern_) {
    app_.toast("PATTERN CHANGED");
    return;
  }
  const bool selId = id == kCopySel || id == kClearSel || id == kNoteOffSel || id == kDropSel;
  if (selId && !selOn_) return;
  mt::Clipboard* cb = app_.clipboard();
  const int tr = track(), step = cur();
  switch (id) {
    case kCopyStep:
    case kCopyTrack:
    case kCopySel:
      if (!cb) return;
      mt::copySel(pat(),
                  id == kCopyStep ? mt::makeSel(tr, step, tr, step) : (id == kCopyTrack ? trackSel() : curSel()),
                  *cb);
      if (id == kCopySel) selOn_ = false;  // free the cursor to pick the paste position
      app_.toast("COPIED");
      break;
    case kPaste:
      if (!cb || !cb->tracks || !cb->steps) return;
      app_.pushUndo();
      engine::lockProject();
      if (selOn_) {
        const mt::Sel s = curSel();
        mt::pasteAt(pat(), *cb, s.t0, s.s0);
      } else {
        mt::pasteAt(pat(), *cb, tr, step);
      }
      engine::unlockProject();
      app_.markDirty();
      engine::post(engine::Cmd::ReleaseTies);
      app_.toast("PASTED");
      break;
    case kClearStep:
    case kNoteOff: apply(mt::makeSel(tr, step, tr, step), id); break;
    case kNoteOffSel: apply(curSel(), id); break;
    case kClearTrack: apply(trackSel(), id); break;
    case kClearSel: apply(curSel(), id); break;
    case kTranspose: openTranspose(); break;
    case kSelect:
      selOn_ = true;
      selT0_ = selT1_ = tr;
      selS0_ = selS1_ = step;
      app_.toast("SELECT: TURN TO EXTEND");
      break;
    case kToggleView: setView(view_ == View::Overview ? View::Detail : View::Overview); break;
    case kRollView: setView(View::Roll); break;
    case kToggleFollow:
      follow_ = !follow_;
      dragFrozen_ = false;
      app_.toast(follow_ ? "FOLLOW ON" : "FOLLOW OFF");
      break;
    case kUndo: undo(); break;
    case kDropSel: selOn_ = false; break;
    case kFill: openFill(); break;
    case kCaptureLive: captureLive(); break;
    case kResampleTrack: resample(false); break;
    case kResamplePattern: resample(true); break;
    case kRec:
      setRec(!rec_);
      app_.toast(rec_ ? "REC: BUTTONS WRITE WHILE PLAYING" : "REC OFF");
      break;
    case kPerf:
      setPerf(!perf());
      app_.toast(perf() ? "PERF: HOLD A BUTTON" : "PERF OFF");
      break;
    default: break;
  }
  app_.invalidate();
}

// ---- drawing ----

void GridScreen::draw(LGFX_Sprite& s, int y0, int h) {
  if (fill_.isOpen()) {
    fill_.draw(s, y0);
    return;
  }
  if (transpose_.isOpen()) {
    transpose_.draw(s, y0);
    return;
  }
  y0_ = y0;
  h_ = h;
  rows_ = rowsFor(h);
  if (roll()) {
    drawRoll(s, y0, h);
    return;
  }
  const engine::Status& st = app_.status();
  const int n = len();
  const int step = cur();

  if (needVisible_) {
    if (step < top_) top_ = step;
    if (step >= top_ + rows_) top_ = step - rows_ + 1;
    needVisible_ = false;
  } else if (follow_ && !dragFrozen_ && st.playing && !edit_ && st.pos < n &&
             millis() - lastMoveMs_ >= kFollowPauseMs) {
    // Smooth follow: play row stays in the middle, view scrolls one row per step (detail: the
    // track's own step).
    top_ = (detail() ? heardTrackStep(track()) : st.pos) - rows_ / 2;
    if (top_ > n - rows_) top_ = n - rows_;
  }
  if (top_ >= n) top_ = n > rows_ ? n - rows_ : 0;
  if (top_ < 0) top_ = 0;

  const int gridY = y0 + kNamesH;
  if (detail()) drawDetail(s, gridY);
  else drawOverview(s, gridY);
  if (padShown()) drawPad(s, y0 + h - kKbH);
  else if (keyboardShown()) drawKeyboard(s, y0 + h - kKbH);
}

// Piano roll of the current track: step numbers on top, keys (KIT: lane names) on the left, a row per
// semitone (KIT: per lane). Cells: every 4th step lighter, the scale root's rows (each octave) a stripe
// tinted toward the cursor colour, the cursor's row and column highlighted over it, the heard step lit,
// steps past the track's own length grey, the cursor cell framed. Keys: out-of-scale ones greyed (white
// keys darker, black keys lighter: a step between black and white), the root tinted.
// Notes are bars as long as their gate (cut by the next note), as bright as their velocity; CHD's other
// notes are outlines.
void GridScreen::drawRoll(LGFX_Sprite& s, int y0, int h) {
  const mt::Project& p = app_.project();
  const engine::Status& st = app_.status();
  const mt::Pattern& pt = pat();
  const int n = len();
  const int tr = track();
  const int step = cur();
  const mt::TrackCfg& t = p.tracks[tr];
  const bool drumTr = drum();
  const bool audible = p.trackAudible(tr);
  const mt::ScaleType scale = static_cast<mt::ScaleType>(p.scaleType);
  const int tl = pt.trackLen[tr] && pt.trackLen[tr] < n ? pt.trackLen[tr] : n;
  const int heard = st.playing || st.paused ? heardTrackStep(tr) : -1;
  const int rows = rollRows(h);
  const int rowH = drumTr ? (h - kNamesH) / mt::kKitLanes : kRowH;
  const int gridY = y0 + kNamesH;
  char buf[8];

  // The page: the cursor's, or the heard step's while following (as Detail does).
  if (follow_ && !dragFrozen_ && st.playing && millis() - lastMoveMs_ >= kFollowPauseMs && heard >= 0)
    rollLeft_ = mt::rollPage(heard, kRollCols);
  else rollLeft_ = mt::rollPage(step, kRollCols);
  if (needVisible_) {
    rollLow_ = mt::rollFollow(rollLow_, rollNote_, rows, 128);
    needVisible_ = false;
  }
  rollLow_ = clampi(rollLow_, 0, 128 - rows > 0 ? 128 - rows : 0);
  const int cols = n - rollLeft_ < kRollCols ? n - rollLeft_ : kRollCols;

  // Header: the track number (or the REC / PERF badge), the step numbers.
  const int badgeW = drawBadge(s, y0);
  if (!badgeW) {
    s.setTextColor(t.solo ? kCursor : (audible ? kText : kDim));
    snprintf(buf, sizeof(buf), "T%d", tr + 1);
    s.drawString(buf, 2, y0);
  }
  for (int c = 0; c < cols; ++c) {
    const int i = rollLeft_ + c;
    const int x = kNumW + c * kRollColW;
    if (x < badgeW) continue;
    if (i == heard) s.fillRect(x, y0, kRollColW - 1, kNamesH, kPlayBg);
    s.setTextColor(i == step ? kCursor : (i < tl && i % 4 == 0 ? kText : kDim));
    snprintf(buf, sizeof(buf), "%d", i + 1);
    s.drawString(buf, x + (kRollColW - 1 - static_cast<int>(strlen(buf)) * kCharW) / 2, y0);
    if (pt.steps[tr][i].note == mt::kNoteOff) s.fillRect(x, gridY - 2, kRollColW - 1, 2, kRed);  // OFF
  }

  // Keys / lane names and the cells.
  const mt::Instrument* kit = drumTr ? p.kitOf(tr) : nullptr;
  constexpr int kBlackW = kNumW * 6 / 10;  // 19 px of 32
  const uint16_t keyLine = mt::rollMix565(kKeyWhite, kKeyBlack, 110);
  auto whiteKey = [&](int nt) -> uint16_t {  // a white key's surface by the scale
    const mt::RollKey k = mt::rollKey(nt, p.scaleRoot, scale);
    if (k == mt::RollKey::Root) return mt::rollMix565(kKeyWhite, kCursor, 110);
    if (k == mt::RollKey::Out) return mt::rollMix565(kKeyWhite, kKeyBlack, 80);
    return kKeyWhite;
  };
  // The cells' backgrounds once per frame: plain / beat, and the root stripe over each (a third toward
  // the cursor colour: seen in every theme, still darker than the note bars).
  const uint16_t cellBg = kBg, beatBg = mt::rollMix565(kBg, kBeatBg, 200);
  const uint16_t rootBg = mt::rollMix565(cellBg, kCursor, 80), rootBeatBg = mt::rollMix565(beatBg, kCursor, 80);
  for (int r = 0; r < rows; ++r) {
    const int y = gridY + r * rowH;
    const int note = drumTr ? -1 : rollLow_ + rows - 1 - r;
    const bool curRow = drumTr ? r == lane_ : note == rollNote_;
    const mt::RollKey key = drumTr ? mt::RollKey::Chromatic : mt::rollKey(note, p.scaleRoot, scale);
    if (drumTr) {
      s.fillRect(0, y, kNumW - 1, rowH - 1, kBeatBg);
      laneName(p, kit, r, buf);
      const bool unnamed = buf[0] == '-' && !buf[1];
      if (unnamed) snprintf(buf, sizeof(buf), "%d", r + 1);  // an empty lane: its number, dim
      s.setTextColor(curRow ? kCursor : (unnamed ? kDim : kText));
      s.drawString(buf, 0, y + (rowH > kCharH ? (rowH - kCharH) / 2 : 0));
      if (!unnamed && rowH >= 2 * kCharH) {
        snprintf(buf, sizeof(buf), "%d", r + 1);
        s.setTextColor(kDim);
        s.drawString(buf, 0, y + kCharH);
      }
    } else {
      // A vertical piano: white keys one light surface, black bars over its left kBlackW.
      const mt::RollKeyRow kr = mt::rollKeyRow(note);
      if (kr.black) {
        const int half = rowH / 2;
        s.fillRect(0, y, kNumW - 1, half, whiteKey(kr.upper));
        s.fillRect(0, y + half, kNumW - 1, rowH - half, whiteKey(kr.lower));
        s.drawFastHLine(kBlackW, y + half, kNumW - 1 - kBlackW, keyLine);  // the two white keys meet
        uint16_t bc = kKeyBlack;
        if (key == mt::RollKey::Root) bc = mt::rollMix565(kKeyBlack, kCursor, 150);
        else if (key == mt::RollKey::Out) bc = mt::rollMix565(kKeyBlack, kKeyWhite, 90);
        s.fillRect(0, y + 1, kBlackW, rowH - 2, bc);
      } else {
        s.fillRect(0, y, kNumW - 1, rowH, whiteKey(note));
        if (kr.lineBelow) s.drawFastHLine(0, y + rowH - 1, kNumW - 1, keyLine);
        if (note % 12 == 0) {  // C only: "C4", octave -1 as 'm'
          mt::noteName(static_cast<uint8_t>(note), buf);
          buf[1] = buf[2];
          buf[2] = 0;
          s.setTextColor(kKeyBlack);
          s.drawString(buf, kNumW - 4 - 2 * kCharW, y);  // clear of the cursor strip
        }
      }
      if (curRow) s.fillRect(kNumW - 4, y, 3, rowH, kCursor);  // the cursor row, as the crosshair
      s.drawFastVLine(kNumW - 1, y, rowH, keyLine);  // the edge to the grid
    }
    const bool rootRow = key == mt::RollKey::Root;
    for (int c = 0; c < cols; ++c) {
      const int i = rollLeft_ + c;
      uint16_t bg = i % 4 == 0 ? (rootRow ? rootBeatBg : beatBg) : (rootRow ? rootBg : cellBg);
      if (curRow || i == step) bg = mt::rollMix565(bg, kSelBg, 160);  // the crosshair
      if (i == heard) bg = kPlayBg;
      if (i >= tl) bg = mt::rollMix565(bg, kDim, 64);  // never played
      s.fillRect(kNumW + c * kRollColW, y, kRollColW - 1, rowH - 1, bg);
    }
  }

  auto velColor = [&](int vel, bool lit) {
    return lit ? mt::rollMix565(kDim, kText, 64 + vel * 192 / 127) : kDim;
  };
  const int x0 = kNumW, x1 = kNumW + cols * kRollColW;  // the cells' span
  if (drumTr) {  // KIT: a block per lane hit, the step velocity (note) as brightness
    for (int c = 0; c < cols; ++c) {
      const int i = rollLeft_ + c;
      const mt::Step& cs = pt.steps[tr][i];
      if (!cs.hasNote()) continue;
      const uint16_t col = velColor(cs.note ? cs.note : t.defVel, audible && i < tl);
      for (int l = 0; l < mt::kKitLanes; ++l)
        if (mt::laneOn(cs, l))
          s.fillRect(kNumW + c * kRollColW + 3, gridY + l * rowH + 3, kRollColW - 7, rowH - 7, col);
    }
  } else {  // bars, from every step: one that starts on an earlier page may reach this one
    for (int i = 0; i < n && i < rollLeft_ + cols; ++i) {
      const mt::Step& cs = pt.steps[tr][i];
      if (!cs.hasNote()) continue;
      const int span = mt::rollSpan(pt.steps[tr], i < tl ? tl : n, i, t.defGate);
      int bx = kNumW + (i - rollLeft_) * kRollColW;
      int bw = span * kRollColW / 100 - 1;
      if (bw < 2) bw = 2;
      if (bx + bw <= x0) continue;
      if (bx < x0) {
        bw -= x0 - bx;
        bx = x0;
      }
      if (bx + bw > x1) bw = x1 - bx;
      if (bw <= 0) continue;
      const uint16_t col = velColor(cs.vel ? cs.vel : t.defVel, audible && i < tl);
      auto rowOf = [&](int nt) { return rollLow_ + rows - 1 - nt; };
      const int r = rowOf(cs.note);
      if (r >= 0 && r < rows) s.fillRect(bx, gridY + r * rowH + 2, bw, rowH - 5, col);
      uint8_t extra[4];
      const int k = mt::rollChordExtra(cs, p.scaleRoot, scale, extra);
      for (int e = 0; e < k; ++e) {
        const int er = rowOf(extra[e]);
        if (er >= 0 && er < rows) s.drawRect(bx, gridY + er * rowH + 2, bw, rowH - 5, col);
      }
    }
  }
  // Where the track loops, and the cursor cell.
  if (tl < n && tl >= rollLeft_ && tl < rollLeft_ + cols)
    s.drawFastVLine(kNumW + (tl - rollLeft_) * kRollColW - 1, gridY, rows * rowH, kCursor);
  const int cr = drumTr ? lane_ : rollLow_ + rows - 1 - rollNote_;
  if (cr >= 0 && cr < rows && step >= rollLeft_ && step < rollLeft_ + cols)
    s.drawRect(kNumW + (step - rollLeft_) * kRollColW - 1, gridY + cr * rowH - 1, kRollColW + 1, rowH + 1,
               rollHeld_ ? kEditCursor : kCursor);
}

void GridScreen::drawOverview(LGFX_Sprite& s, int gridY) {
  const mt::Project& p = app_.project();
  const engine::Status& st = app_.status();
  const mt::Pattern& pt = pat();
  const int n = len();
  char buf[12];

  // Header: the visible half over the step numbers, track names coloured by mute/solo. The PERF
  // badge grows with the held effects and covers the names it reaches.
  const int badgeW = drawBadge(s, gridY - kNamesH);
  if (!badgeW) {
    if (edit_) {
      snprintf(buf, sizeof(buf), "ST%d", editStep_);  // Edit step while editing
      s.setTextColor(kEditCursor);
    } else {
      snprintf(buf, sizeof(buf), "%s", half() ? "9-16" : "1-8");
      s.setTextColor(kDim);
    }
    s.drawString(buf, 0, gridY - kNamesH);  // 4 chars fill kNumW
  }
  for (int col = 0; col < kOverviewTracks; ++col) {
    const int tr = firstTrack() + col;
    const mt::TrackCfg& t = p.tracks[tr];
    if (kNumW + col * kColW + 4 < badgeW) continue;
    s.setTextColor(t.solo ? kCursor : (p.trackAudible(tr) ? kText : kDim));
    s.drawString(t.name, kNumW + col * kColW + 4, gridY - kNamesH);
    if (p.genOn(tr)) {  // the track generator plays its notes
      const int gx = kNumW + (col + 1) * kColW - kCharW - 1;
      s.fillRect(gx - 1, gridY - kNamesH, kCharW + 1, kNamesH, kBg);
      s.setTextColor(kCursor);
      s.drawString("G", gx, gridY - kNamesH);
    }
  }

  for (int row = 0; row < rows_; ++row) {
    const int step = top_ + row;
    if (step >= n) break;
    const int y = gridY + row * kRowH;
    if ((st.playing || st.paused) && step == st.pos) s.fillRect(0, y, kScreenW, kRowH, kPlayBg);
    else if (step % 4 == 0) s.fillRect(0, y, kScreenW, kRowH, kBeatBg);

    s.setTextColor(kDim);
    snprintf(buf, sizeof(buf), "%3d", step + 1);
    s.drawString(buf, 2, y);

    for (int col = 0; col < kOverviewTracks; ++col) {
      const int tr = firstTrack() + col;
      const mt::Step& c = pt.steps[tr][step];
      const int x = kNumW + col * kColW;
      if (selected(tr, step)) s.fillRect(x, y, kColW, kRowH, kSelBg);
      // Past the track's own length: never played, drawn grey; a line marks where it loops.
      const uint8_t tl = pt.trackLen[tr];
      const bool past = tl && tl < n && step >= tl;
      if (tl && tl < n && step == tl) s.drawFastHLine(x, y, kColW, kCursor);
      const bool lit = p.trackAudible(tr) && !past;
      const bool drumTr = drumAt(tr);
      if (drumTr && c.hasNote()) {
        drawMask(s, x, y, mt::laneMask(c), lit, -1, false);
      } else {
        char nn[4];
        mt::noteName(c.note, nn);
        s.setTextColor(c.hasNote() && lit ? kText : kDim);
        s.drawString(nn, x + 4, y);
      }
      if (c.hasNote() && !drumTr) {
        const uint8_t vel = c.vel ? c.vel : p.tracks[tr].defVel;
        s.fillRect(x + 32, y + 12, (vel * 20) / 127, 2, kDim);
      }
      if (c.hasFx()) {
        // Dim when every fx is ignored: a synth fx on a MIDI track, a drum fx off a drum track.
        bool live = false;
        for (const mt::FxSlot& f : c.fx)
          live |= f.cmd != mt::Fx::None && (p.trackInternal(tr) || !mt::fxSynthOnly(f.cmd)) &&
                  (drumTr || !mt::fxDrumOnly(f.cmd));
        s.fillRect(drumTr ? x + 51 : x + 48, drumTr ? y + 1 : y + 6, 3, 3,  // drum: right of the mask (ends x + 48)
                    live ? kCursor : kDim);
      }
      if (step == curStep_ && tr == track()) s.drawRect(x, y, kColW, kRowH, edit_ ? kEditCursor : kCursor);
    }
  }
}

void GridScreen::drawDetail(LGFX_Sprite& s, int gridY) {
  const mt::Project& p = app_.project();
  const engine::Status& st = app_.status();
  const mt::Pattern& pt = pat();
  const int n = len();
  const int tr = track();
  const mt::TrackCfg& t = p.tracks[tr];
  const bool audible = p.trackAudible(tr);
  const bool midi = !p.trackInternal(tr);
  const bool genTr = p.genOn(tr);
  const bool drumTr = drumAt(tr);
  char buf[16];

  // Header: track number over the step numbers, its name over NOTE / VEL, slots over the commands.
  // The PERF badge grows with the held effects and covers the labels it reaches.
  s.setTextColor(t.solo ? kCursor : (audible ? kText : kDim));
  const int badgeW = drawBadge(s, gridY - kNamesH);
  if (!badgeW) {
    if (edit_) {
      snprintf(buf, sizeof(buf), "ST%d", editStep_);  // Edit step while editing
      s.setTextColor(kEditCursor);
    } else {
      snprintf(buf, sizeof(buf), "T%d", tr + 1);
    }
    s.drawString(buf, 2, gridY - kNamesH);
  }
  if (kNumW + 1 >= badgeW) {
    s.setTextColor(t.solo ? kCursor : (audible ? kText : kDim));
    s.drawString(t.name, kNumW + 1, gridY - kNamesH);  // 8 chars fit 2 fields
    if (p.genOn(tr)) {
      s.setTextColor(kCursor);
      s.drawString("G", kNumW + 1 + 9 * kCharW, gridY - kNamesH);
    }
  }
  s.setTextColor(kDim);
  for (int k = 0; k < mt::kFxSlots; ++k) {
    const int x = kNumW + (kFx1 + 2 * k) * kFieldW + 4;
    if (x < badgeW) continue;
    snprintf(buf, sizeof(buf), "FX%d", k + 1);
    s.drawString(buf, x, gridY - kNamesH);
  }

  const int heard = st.playing || st.paused ? heardTrackStep(tr) : -1;  // the track's own step
  for (int row = 0; row < rows_; ++row) {
    const int step = top_ + row;
    if (step >= n) break;
    const int y = gridY + row * kRowH;
    if (step == heard) s.fillRect(0, y, kScreenW, kRowH, kPlayBg);
    else if (step % 4 == 0) s.fillRect(0, y, kScreenW, kRowH, kBeatBg);

    s.setTextColor(kDim);
    snprintf(buf, sizeof(buf), "%3d", step + 1);
    s.drawString(buf, 2, y);

    const mt::Step& c = pt.steps[tr][step];
    if (selected(tr, step)) s.fillRect(kNumW, y, kDetW, kRowH, kSelBg);
    const uint8_t tl = pt.trackLen[tr];
    const bool past = tl && tl < n && step >= tl;  // past the track's own length
    if (tl && tl < n && step == tl) s.drawFastHLine(kNumW, y, kDetW, kCursor);
    for (int f = 0; f < kFields; ++f) {
      const bool laneCur = edit_ && step == curStep_ && curField_ == kNote;  // the lane cursor, even on an empty step
      if (drumTr && f == kNote && (c.hasNote() || laneCur)) {
        drawMask(s, kNumW, y, c.hasNote() ? mt::laneMask(c) : 0, audible && !past, laneCur ? lane_ : -1, true);
        continue;
      }
      char txt[5];
      bool has;
      if (drumTr && f == kVel) {  // the step velocity, held in note
        has = c.hasNote() && c.note;
        if (has) snprintf(txt, sizeof(txt), "%3u", c.note);
        else snprintf(txt, sizeof(txt), "...");
      } else {
        has = fieldText(c, f, txt);
      }
      // Synth fx do nothing on a MIDI track, drum fx off a drum track.
      const mt::Fx cmd = f >= kFx1 ? c.fx[(f - kFx1) / 2].cmd : mt::Fx::None;
      // FLP does nothing without the track generator; its notes are the generator's (the pattern's: dim).
      const bool ignored = (f >= kFx1 && ((midi && mt::fxSynthOnly(cmd)) || (!drumTr && mt::fxDrumOnly(cmd)) ||
                                          (cmd == mt::Fx::FLP && !genTr))) ||
                           (f == kNote && genTr);
      s.setTextColor(has && audible && !ignored && !past ? kText : kDim);
      s.drawString(txt, kNumW + f * kFieldW + (strlen(txt) > 3 ? 0 : 4), y);  // 4 chars (CHD "MA72") fill the field
    }
    if (step == curStep_) {
      const uint16_t fc = edit_ ? kEditCursor : kCursor;
      const int fx = kNumW + curField_ * kFieldW;
      if (drumTr && curField_ == kNote) {  // the mask fills the field's width: top and bottom lines only
        s.drawFastHLine(fx, y, kFieldW, fc);
        s.drawFastHLine(fx, y + kRowH - 1, kFieldW, fc);
      } else {
        s.drawRect(fx, y, kFieldW, kRowH, fc);
      }
    }
  }
  // Separators before each fx pair.
  const int h = kNamesH + rows_ * kRowH;
  for (int k = 0; k < mt::kFxSlots; ++k) s.drawFastVLine(kNumW + (kFx1 + 2 * k) * kFieldW - 1, gridY - kNamesH, h, kDim);
}

// REC (red) / PERF (yellow) over the step numbers instead of the header text; 0 when neither.
// PERF appends the held effects' short names in press order ("PERF RPT FLT CRS"), as many as the
// header line holds (PerfPad::drawBadge).
int GridScreen::drawBadge(LGFX_Sprite& s, int y) {
  if (!rec_) return app_.perf().drawBadge(s, 0, y);
  s.setTextColor(kRed);
  s.drawString("REC", 0, y);
  return 3 * kCharW;
}

// Two rows of 8 (lanes 1-8 over 9-16) in a 16 px row; filled = the lane hits.
// Overview (56 px column): 5 px squares at a 6 / 7 px pitch, 48 px wide. Detail (compact, the 32 px
// NOTE field): 3 x 5 px cells at a 4 / 7 px pitch, 31 px wide; cursorLane >= 0 draws that lane in the
// edit cursor colour (no room for a frame between 1 px gaps; the lane pad below shows it framed).
void GridScreen::drawMask(LGFX_Sprite& s, int x, int y, uint16_t mask, bool audible, int cursorLane, bool compact) {
  const int w = compact ? 3 : 5, pitch = compact ? 4 : 6;
  for (int l = 0; l < mt::kKitLanes; ++l) {
    const int sx = x + (compact ? 1 : 2) + (l % 8) * pitch;
    const int sy = y + 2 + (l / 8) * 7;
    const bool on = (mask >> l) & 1;
    const uint16_t c = l == cursorLane ? kEditCursor : (on && audible ? kText : kDim);
    if (on) s.fillRect(sx, sy, w, 5, c);
    else s.drawRect(sx, sy, w, 5, c);
  }
}

// Lane pad: a button per lane, lit when the step has it, named after its sample or instrument.
void GridScreen::drawPad(LGFX_Sprite& s, int y) {
  const mt::Project& p = app_.project();
  const mt::Instrument* k = p.kitOf(track());
  const mt::Step& c = pat().steps[track()][curStep_];
  s.fillRect(0, y, kScreenW, kKbH, kBg);
  for (int l = 0; l < mt::kKitLanes; ++l) {
    const int x = (l % kPadCols) * kPadW, py = y + (l / kPadCols) * kPadRowH;
    const bool on = c.hasNote() && mt::laneOn(c, l);
    s.fillRect(x + 1, py + 1, kPadW - 2, kPadRowH - 2, on ? kPlayBg : kBeatBg);
    if (l == lane_) s.drawRect(x, py, kPadW, kPadRowH, kEditCursor);
    char nm[8], txt[12];
    laneName(p, k, l, nm);
    // "16 KICK" = 7 chars (56 px) in the 60 px button; an empty lane shows its number only.
    if (nm[0] == '-' && !nm[1]) snprintf(txt, sizeof(txt), "%d", l + 1);
    else snprintf(txt, sizeof(txt), "%d %.4s", l + 1, nm);
    s.setTextColor(on ? kText : kDim);
    s.drawString(txt, x + 2, py + (kPadRowH - kCharH) / 2);
  }
}

void GridScreen::drawKeyboard(LGFX_Sprite& s, int y) {
  const mt::Project& p = app_.project();
  const mt::ScaleType scale = static_cast<mt::ScaleType>(p.scaleType);
  const mt::Step& c = pat().steps[track()][curStep_];
  const int curNote = c.hasNote() ? c.note : -1;
  const int base = ((c.hasNote() ? c.note : lastNote_[track()]) / 12) * 12;
  s.fillRect(0, y, kScreenW, kKbH, kBg);
  for (int k = 0; k < 12; ++k) {
    const int n = base + k;
    const int x = k * kKeyW;
    const bool black = isBlackKey(k);
    if (n > 127) {
      s.drawRect(x + 1, y + 1, kKeyW - 2, kKbH - 2, kDim);
      continue;
    }
    s.fillRect(x + 1, y + 1, kKeyW - 2, kKbH - 2, black ? kKeyBlack : kKeyWhite);
    if (mt::inScale(n, p.scaleRoot, scale)) s.fillRect(x + 1, y + kKbH - 7, kKeyW - 2, 6, kCursor);
    if (n == curNote) {
      s.drawRect(x, y, kKeyW, kKbH, kEditCursor);
      s.drawRect(x + 1, y + 1, kKeyW - 2, kKbH - 2, kEditCursor);
    }
    char nn[4];
    mt::noteName(static_cast<uint8_t>(n), nn);
    s.setTextColor(black ? kText : kBg);
    s.drawString(nn, x + 8, y + 12);
  }
}

}  // namespace ui
