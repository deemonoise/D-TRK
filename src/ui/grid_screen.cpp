#include "grid_screen.h"
#include <stdio.h>
#include "app.h"
#include "audio/audio.h"
#include "fx_info.h"
#include "note_name.h"
#include "scale.h"

namespace ui {
namespace {

constexpr uint16_t kKeyWhite = 0xC618;
constexpr uint16_t kKeyBlack = 0x18C3;

const char* const kFieldNames[] = {"NOTE",    "VEL", "FX1", "FX1 VAL", "FX2", "FX2 VAL", "FX3",
                                   "FX3 VAL", "FX4", "FX4 VAL", "FX5", "FX5 VAL", "FX6", "FX6 VAL"};
static_assert(sizeof(kFieldNames) / sizeof(kFieldNames[0]) == 2 + 2 * mt::kFxSlots, "a name per Detail field");

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool isBlackKey(int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

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

int GridScreen::rowsFor(int h) const { return (h - kNamesH - (keyboardShown() ? kKbH : 0)) / kRowH; }

void GridScreen::onEnter() {
  setEdit(false);
  cur();
  needVisible_ = true;
}

void GridScreen::onLeave() {
  euclid_.cancel();
  transpose_.cancel();
}

void GridScreen::onProjectReplaced() {
  euclid_.abandon();
  transpose_.cancel();
}

// The Euclid dialog keeps editing the pattern it was opened for.
void GridScreen::onPatternChange() {
  curStep_ = 0;
  top_ = 0;
  edit_ = false;
  editPushed_ = false;
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
  if (on && !edit_) {
    editPushed_ = false;
    fxCycled_ = false;
    app_.toast(kFieldNames[curField_]);
  }
  edit_ = on;
  needVisible_ = true;  // the keyboard changes the row count
}

void GridScreen::toggleView() {
  detail_ = !detail_;
  if (!detail_) curField_ = kNote;
  setEdit(false);
}

void GridScreen::undo() {
  editPushed_ = false;  // the next edit takes a fresh snapshot
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

// Entered notes sound on INT tracks (PROJ Preview), with the track's instrument.
void GridScreen::previewNote(uint8_t note) {
  const mt::Project& p = app_.project();
  const mt::TrackCfg& t = p.tracks[track()];
  if (!p.preview || t.out != mt::TrackOut::Int || note > 127) return;
  audio::preview(t.instr < mt::kInstruments ? t.instr : 0, note);
}

// Octave from the note under the cursor, else the track's last note (60 at start).
void GridScreen::enterDegree(int button, bool octaveUp) {
  const int tr = track();
  const mt::Step& st = pat().steps[tr][cur()];
  const int ref = st.hasNote() ? st.note : lastNote_[tr];
  const mt::Project& p = app_.project();
  const int base = ref / 12 * 12 + (octaveUp ? 12 : 0);
  setNote(static_cast<uint8_t>(
      mt::degreeNote(button, p.scaleRoot, static_cast<mt::ScaleType>(p.scaleType), base)));
  moveStep(1);
}

void GridScreen::editTurn(int delta, bool shift) {
  const int tr = track();
  mt::Step st = pat().steps[tr][cur()];
  const mt::Project& p = app_.project();
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
        f.val = mt::fxStep(f.cmd, f.val, delta * mult);
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
  if (euclid_.isOpen()) {
    euclid_.onInput(ev);
    return;
  }
  if (transpose_.isOpen()) {
    transpose_.onInput(ev);
    return;
  }
  cur();
  switch (ev.type) {
    case InputType::EncTurn:
      if (edit_) editTurn(ev.delta, ev.shift);
      else if (!ev.shift) moveStep(ev.delta);
      else if (detail_) moveField(ev.delta);
      else moveTrack(ev.delta);
      break;
    case InputType::EncClick:
      if (ev.shift) toggleView();
      else if (selOn_) selOn_ = false;  // click ends a selection instead of entering edit
      else setEdit(!edit_);
      break;
    case InputType::EncLong:
      if (ev.shift) {
        undo();
      } else {
        setEdit(false);
        openMenu();
      }
      break;
    default: break;
  }
}

bool GridScreen::trackKey(int n, bool shift) {
  if (euclid_.isOpen() || transpose_.isOpen()) return true;
  cur();
  if (edit_) {
    enterDegree(n, shift);
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
  if (!detail_) {
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
  if (euclid_.isOpen()) {
    euclid_.onTouch(ev);
    return;
  }
  if (transpose_.isOpen()) {
    transpose_.onTouch(ev);
    return;
  }
  cur();
  if (ev.type == TouchType::Drag) {
    const int n = len();
    const int maxTop = n > rows_ ? n - rows_ : 0;
    top_ = clampi(top_ - ev.dy / kRowH, 0, maxTop);
    dragFrozen_ = true;
    needVisible_ = false;
    return;
  }
  if (ev.type == TouchType::LongPress && app_.shift()) {
    undo();
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

  // Track names: mute, Shift = solo.
  if (ev.y < y0_ + kNamesH) {
    if (ev.type != TouchType::Tap || ev.x < kNumW) return;
    const int col = (ev.x - kNumW) / kColW;
    if (!detail_ && col >= kOverviewTracks) return;
    const int tr = detail_ ? track() : firstTrack() + col;
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
  if (selOn_) {
    const mt::Sel s = curSel();
    snprintf(title, sizeof(title), "SEL T%d-%d  %d-%d", s.t0 + 1, s.t1 + 1, s.s0 + 1, s.s1 + 1);
    add("Copy sel", kCopySel, canCopy);
    add("Paste", kPaste, canPaste);
    add("Clear sel", kClearSel, true);
    add("Note OFF sel", kNoteOffSel, true);
    add("Transpose...", kTranspose, true);
  } else {
    snprintf(title, sizeof(title), "STEP %d  %s", cur() + 1, app_.project().tracks[track()].name);
    add("Copy step", kCopyStep, canCopy);
    add("Paste", kPaste, canPaste);
    add("Clear step", kClearStep, true);
    add("Note OFF", kNoteOff, true);
    add("Select", kSelect, true);
    add("Copy track", kCopyTrack, canCopy);
    add("Clear track", kClearTrack, true);
    add("Transpose track...", kTranspose, true);
    add("Euclid...", kEuclid, true);
  }
  if (!selOn_) {
    add(detail_ ? "Overview" : "Detail view", kToggleView, true);
    add(follow_ ? "Follow: ON" : "Follow: OFF", kToggleFollow, true);
  }
  add("Undo", kUndo, canUndo);
  if (selOn_) add("Clear selection", kDropSel, true);
  menuPattern_ = app_.editPattern();
  app_.menu().open(title, items, n, [this](int id) { onMenu(id); });
}

void GridScreen::openEuclid() {
  const int tr = track();
  mt::EuclidParams& e = euclidParams_[tr];
  if (!euclidInit_[tr]) {  // first use: base = last note on the track, else the last entered note
    euclidInit_[tr] = true;
    e.baseNote = lastNote_[tr];
    const mt::Step* steps = pat().steps[tr];
    for (int i = len() - 1; i >= 0; --i)
      if (steps[i].hasNote()) {
        e.baseNote = steps[i].note;
        break;
      }
  }
  setEdit(false);
  euclid_.open(app_.editPattern(), tr, &e);
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
  app_.pushUndo();
  engine::lockProject();
  mt::transposeSel(pat(), sel, amount, degrees, p.scaleRoot, static_cast<mt::ScaleType>(p.scaleType));
  engine::unlockProject();
  app_.markDirty();
  engine::post(engine::Cmd::ReleaseTies);
  app_.toast("TRANSPOSED");
}

void GridScreen::onMenu(int id) {
  if (app_.editPattern() != menuPattern_) {  // heard pattern changed while the menu was open
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
    case kToggleView: toggleView(); break;
    case kToggleFollow:
      follow_ = !follow_;
      dragFrozen_ = false;
      app_.toast(follow_ ? "FOLLOW ON" : "FOLLOW OFF");
      break;
    case kUndo: undo(); break;
    case kDropSel: selOn_ = false; break;
    case kEuclid: openEuclid(); break;
    default: break;
  }
  app_.invalidate();
}

// ---- drawing ----

void GridScreen::draw(LGFX_Sprite& s, int y0, int h) {
  if (euclid_.isOpen()) {
    euclid_.draw(s, y0);
    return;
  }
  if (transpose_.isOpen()) {
    transpose_.draw(s, y0);
    return;
  }
  y0_ = y0;
  h_ = h;
  rows_ = rowsFor(h);
  const engine::Status& st = app_.status();
  const int n = len();
  const int step = cur();

  if (needVisible_) {
    if (step < top_) top_ = step;
    if (step >= top_ + rows_) top_ = step - rows_ + 1;
    needVisible_ = false;
  } else if (follow_ && !dragFrozen_ && st.playing && !edit_ && st.pos < n &&
             millis() - lastMoveMs_ >= kFollowPauseMs) {
    // Smooth follow: play row stays in the middle, view scrolls one row per step.
    top_ = st.pos - rows_ / 2;
    if (top_ > n - rows_) top_ = n - rows_;
  }
  if (top_ >= n) top_ = n > rows_ ? n - rows_ : 0;
  if (top_ < 0) top_ = 0;

  const int gridY = y0 + kNamesH;
  if (detail_) drawDetail(s, gridY);
  else drawOverview(s, gridY);
  if (keyboardShown()) drawKeyboard(s, y0 + h - kKbH);
}

void GridScreen::drawOverview(LGFX_Sprite& s, int gridY) {
  const mt::Project& p = app_.project();
  const engine::Status& st = app_.status();
  const mt::Pattern& pt = pat();
  const int n = len();
  char buf[12];

  // Header: the visible half over the step numbers, track names coloured by mute/solo.
  s.setTextColor(kDim);
  s.drawString(half() ? "9-16" : "1-8", 0, gridY - kNamesH);  // 4 chars fill kNumW
  for (int col = 0; col < kOverviewTracks; ++col) {
    const int tr = firstTrack() + col;
    const mt::TrackCfg& t = p.tracks[tr];
    s.setTextColor(t.solo ? kCursor : (p.trackAudible(tr) ? kText : kDim));
    s.drawString(t.name, kNumW + col * kColW + 4, gridY - kNamesH);
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
      char nn[4];
      mt::noteName(c.note, nn);
      s.setTextColor(c.hasNote() && p.trackAudible(tr) ? kText : kDim);
      s.drawString(nn, x + 4, y);
      if (c.hasNote()) {
        const uint8_t vel = c.vel ? c.vel : p.tracks[tr].defVel;
        s.fillRect(x + 32, y + 12, (vel * 20) / 127, 2, kDim);
      }
      if (c.hasFx()) {
        // Dim when every fx is a synth fx on a MIDI track (ignored there).
        bool live = false;
        for (const mt::FxSlot& f : c.fx)
          live |= f.cmd != mt::Fx::None && (p.trackInternal(tr) || !mt::fxSynthOnly(f.cmd));
        s.fillRect(x + 48, y + 6, 3, 3, live ? kCursor : kDim);
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
  char buf[16];

  // Header: track number over the step numbers, its name over NOTE / VEL, slots over the commands.
  s.setTextColor(t.solo ? kCursor : (audible ? kText : kDim));
  snprintf(buf, sizeof(buf), "T%d", tr + 1);
  s.drawString(buf, 2, gridY - kNamesH);
  s.drawString(t.name, kNumW + 1, gridY - kNamesH);  // 8 chars fit 2 fields
  s.setTextColor(kDim);
  for (int k = 0; k < mt::kFxSlots; ++k) {
    snprintf(buf, sizeof(buf), "FX%d", k + 1);
    s.drawString(buf, kNumW + (kFx1 + 2 * k) * kFieldW + 4, gridY - kNamesH);
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

    const mt::Step& c = pt.steps[tr][step];
    if (selected(tr, step)) s.fillRect(kNumW, y, kDetW, kRowH, kSelBg);
    for (int f = 0; f < kFields; ++f) {
      char txt[5];
      const bool has = fieldText(c, f, txt);
      // Synth fx do nothing on a MIDI track.
      const bool ignored = f >= kFx1 && midi && mt::fxSynthOnly(c.fx[(f - kFx1) / 2].cmd);
      s.setTextColor(has && audible && !ignored ? kText : kDim);
      s.drawString(txt, kNumW + f * kFieldW + 4, y);
    }
    if (step == curStep_)
      s.drawRect(kNumW + curField_ * kFieldW, y, kFieldW, kRowH, edit_ ? kEditCursor : kCursor);
  }
  // Separators before each fx pair.
  const int h = kNamesH + rows_ * kRowH;
  for (int k = 0; k < mt::kFxSlots; ++k) s.drawFastVLine(kNumW + (kFx1 + 2 * k) * kFieldW - 1, gridY - kNamesH, h, kDim);
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
