#include "song_screen.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "esp_heap_caps.h"
#include "track_buttons.h"

namespace ui {
namespace {
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

void SongScreen::onEnter() {
  masksStale_ = true;  // GRID may have edited patterns
  patEdit_ = holdRow_ = false;
  folField_ = valEdit_ = -1;
  holdFol_ = holdVal_ = false;
  clampRow();
  showRow(row_);
}

void SongScreen::onProjectReplaced() {
  row_ = col_ = top_ = 0;
  patEdit_ = holdRow_ = false;
  origRow_ = -1;
  folField_ = origFolRow_ = -1;
  holdFol_ = false;
  valEdit_ = origValRow_ = -1;
  holdVal_ = false;
  undoHead_ = undoCount_ = 0;
  followPat_ = -1;
  masksStale_ = true;
}

bool SongScreen::wantsRedraw(const engine::Status& st) {
  if (st.songPos == lastSongPos_ && st.pos == lastPos_ && st.song == lastSong_) return false;
  lastSongPos_ = st.songPos;
  lastPos_ = st.pos;
  lastSong_ = st.song;
  return true;
}

int SongScreen::len() const {
  const int n = app_.project().songLen;
  return n > mt::kSongMax ? mt::kSongMax : n;
}

int SongScreen::cursor() const {
  const int n = len();
  return n == 0 ? 0 : clampi(row_, 0, n - 1);
}

void SongScreen::refreshMasks() {
  const mt::Project& p = app_.project();
  for (int i = 0; i < mt::kPatterns; ++i) masks_[i] = mt::patternTrackMask(p, i);
  masksStale_ = false;
}

// ---- cursor ----

void SongScreen::clampRow() {
  const int n = len();
  row_ = n == 0 ? 0 : clampi(row_, 0, n - 1);
  if (n == 0) {
    patEdit_ = holdRow_ = holdFol_ = holdVal_ = false;
    folField_ = valEdit_ = -1;
  }
}

void SongScreen::showRow(int r) {
  if (r < top_) top_ = r;
  if (r >= top_ + kRows) top_ = r - kRows + 1;
  const int maxTop = len() > kRows ? len() - kRows : 0;
  top_ = clampi(top_, 0, maxTop);
}

void SongScreen::setRow(int r) {
  const int old = row_;
  row_ = r;
  clampRow();
  showRow(row_);
  if (row_ != old) {
    patEdit_ = holdRow_ = holdFol_ = holdVal_ = false;
    origRow_ = -1;
    folField_ = valEdit_ = -1;
  }
  follow();
}

// View 1: PAT, a track, FOL; view 2: PAT, TRN, BPM, FOL.
void SongScreen::setCol(int c) {
  col_ = view2_ ? (c == 0 || valCol(c) || c == kFolCol ? c : 0) : clampi(c, 0, kCols - 1);
  if (col_ != kFolCol) {
    folField_ = -1;
    holdFol_ = false;
  }
  if (col_ != valEdit_) {
    valEdit_ = -1;
    holdVal_ = false;
  }
  if (col_ == 0) return;
  patEdit_ = holdRow_ = false;
  if (col_ == kFolCol || valCol(col_)) return;
  app_.setCurTrack(col_ - 1);  // the track buttons address its half
}

void SongScreen::stepCol(int dir) {
  if (!view2_) {
    setCol(col_ + dir);
    return;
  }
  static constexpr int kOrder[] = {0, kTrnCol, kBpmCol, kFolCol};
  int i = 0;
  while (i < 4 && kOrder[i] != col_) ++i;
  if (i == 4) i = 0;
  setCol(kOrder[clampi(i + dir, 0, 3)]);
}

// Leaves every edit; a column the other view lacks becomes PAT.
void SongScreen::toggleView() {
  patEdit_ = holdRow_ = holdFol_ = holdVal_ = false;
  origRow_ = -1;
  folField_ = valEdit_ = -1;
  view2_ = !view2_;
  if (col_ != 0 && col_ != kFolCol && view2_ != valCol(col_)) col_ = 0;
  app_.invalidate();
}

void SongScreen::follow() {
  const engine::Status& st = app_.status();
  if (st.playing || st.paused || len() == 0) return;
  const uint8_t pat = app_.project().song[row_];
  // status() lags the posts: A -> B -> A must post A again.
  if (pat >= mt::kPatterns || (pat == st.pattern && pat == followPat_)) return;
  followPat_ = engine::post(engine::Cmd::QueuePattern, pat) ? pat : -1;
}

int SongScreen::colAt(int x) const {
  if (x >= kFolX) return kFolCol;
  if (x < kPatX - 4) return -1;
  if (view2_) {
    if (x < kTrnX - 4) return 0;
    if (x < kBpmX - 4) return kTrnCol;
    return x < kBpmX + kValW + 4 ? kBpmCol : -1;
  }
  if (x < kCellX0) return 0;
  int xx = x - kCellX0;
  constexpr int kHalfW = mt::kTracks / 2 * kCellW;
  if (xx >= kHalfW) xx = xx - kHalfGap < kHalfW ? kHalfW : xx - kHalfGap;
  const int t = xx / kCellW;
  return t < mt::kTracks ? t + 1 : mt::kTracks;
}

int SongScreen::rowAt(int y) const {
  const int ly = y0_ + kHeadH;
  if (y < ly || y >= ly + kRows * kRowH) return -1;
  return top_ + (y - ly) / kRowH;
}

// ---- writes ----

void SongScreen::pushSongUndo(int row, mt::SongOp op) {
  if (!undoTried_) {
    undoTried_ = true;
    undo_ = static_cast<Snap*>(heap_caps_malloc(sizeof(Snap) * kUndoDepth, MALLOC_CAP_SPIRAM));
    if (!undo_) Serial.println("ui: no memory for song undo, disabled");
  }
  if (!undo_) return;
  const mt::Project& p = app_.project();
  Snap& s = undo_[undoHead_];
  s.op = op;
  s.row = static_cast<uint8_t>(row);
  s.len = p.songLen;
  memcpy(s.song, p.song, sizeof(s.song));
  memcpy(s.mute, p.songMute, sizeof(s.mute));
  memcpy(s.fol, p.songFol, sizeof(s.fol));
  memcpy(s.trn, p.songTrn, sizeof(s.trn));
  memcpy(s.bpm, p.songBpm, sizeof(s.bpm));
  undoHead_ = (undoHead_ + 1) % kUndoDepth;
  if (undoCount_ < kUndoDepth) ++undoCount_;
}

void SongScreen::beginWrite(bool undo, int row, mt::SongOp op) {
  if (undo) pushSongUndo(row, op);  // no lock: the UI is the only song writer
  engine::lockProject();
}

// Posted under the lock so the engine sees it before planning on the edited song.
void SongScreen::endWrite(int row, mt::SongOp op) {
  const bool ok = engine::post(engine::Cmd::SongEdit, static_cast<uint16_t>((row << 8) | static_cast<int>(op)));
  engine::unlockProject();
  if (!ok) app_.toast("ENGINE BUSY");
  app_.markDirty();
}

void SongScreen::undo() {
  patEdit_ = holdRow_ = holdFol_ = holdVal_ = false;
  origRow_ = -1;
  folField_ = valEdit_ = -1;
  if (!undo_ || undoCount_ == 0) {
    app_.toast("NOTHING TO UNDO");
    return;
  }
  undoHead_ = (undoHead_ + kUndoDepth - 1) % kUndoDepth;
  --undoCount_;
  const Snap& s = undo_[undoHead_];
  // The inverse op keeps the heard slot: undo of Insert at r = Delete at r, of Delete = Insert.
  mt::SongOp op = mt::SongOp::Edit;
  if (s.op == mt::SongOp::Insert) op = mt::SongOp::Delete;
  else if (s.op == mt::SongOp::Delete) op = mt::SongOp::Insert;
  mt::Project& p = app_.project();
  beginWrite(false);
  p.songLen = s.len;
  memcpy(p.song, s.song, sizeof(p.song));
  memcpy(p.songMute, s.mute, sizeof(p.songMute));
  memcpy(p.songFol, s.fol, sizeof(p.songFol));
  memcpy(p.songTrn, s.trn, sizeof(p.songTrn));
  memcpy(p.songBpm, s.bpm, sizeof(p.songBpm));
  endWrite(s.row, op);
  clampRow();
  showRow(row_);
  follow();
  app_.toast("UNDO");
}

void SongScreen::insertAt(int at, uint8_t pat, uint16_t mute, mt::SongFollow fol, int8_t trn, uint16_t bpm) {
  if (len() >= mt::kSongMax) {
    app_.toast("SONG FULL");
    return;
  }
  at = clampi(at, 0, len());
  beginWrite(true, at, mt::SongOp::Insert);
  mt::songInsert(app_.project(), at, pat, mute, fol, trn, bpm);
  endWrite(at, mt::SongOp::Insert);
  setRow(at);
  touched();
}

// A: a copy of the cursor slot (follow, TRN, BPM too) below it; empty song: the heard pattern, nothing muted.
void SongScreen::add() {
  const mt::Project& p = app_.project();
  if (len() == 0) insertAt(0, app_.editPattern(), 0);
  else insertAt(row_ + 1, p.song[row_], p.songMute[row_], p.songFol[row_], p.songTrn[row_], p.songBpm[row_]);
}

void SongScreen::deleteRow() {
  if (row_ >= len()) return;
  beginWrite(true, row_, mt::SongOp::Delete);
  mt::songDelete(app_.project(), row_);
  endWrite(row_, mt::SongOp::Delete);
  const int r = row_;
  row_ = -1;  // force setRow's reset
  setRow(r);
}

void SongScreen::toggleMute(int track) {
  if (row_ >= len() || track < 0 || track >= mt::kTracks) return;
  mt::Project& p = app_.project();
  beginWrite();
  p.songMute[row_] ^= static_cast<uint16_t>(1u << track);
  endWrite(row_, mt::SongOp::Edit);
}

void SongScreen::setMask(uint16_t m) {
  mt::Project& p = app_.project();
  if (row_ >= len() || p.songMute[row_] == m) return;
  beginWrite();
  p.songMute[row_] = m;
  endWrite(row_, mt::SongOp::Edit);
}

void SongScreen::clearSong() {
  if (len() == 0) return;
  mt::Project& p = app_.project();
  beginWrite();
  memset(p.song, 0, sizeof(p.song));
  memset(p.songMute, 0, sizeof(p.songMute));
  for (mt::SongFollow& f : p.songFol) f = mt::SongFollow();
  memset(p.songTrn, 0, sizeof(p.songTrn));
  memset(p.songBpm, 0, sizeof(p.songBpm));
  p.songLen = 0;
  endWrite(0, mt::SongOp::Edit);
  row_ = top_ = 0;
  patEdit_ = holdRow_ = holdFol_ = holdVal_ = false;
  folField_ = valEdit_ = -1;
  app_.toast("SONG CLEARED");
}

void SongScreen::editPat(int delta) {
  mt::Project& p = app_.project();
  if (delta == 0 || row_ >= len()) return;
  const int v = clampi(p.song[row_] + delta, 0, mt::kPatterns - 1);
  if (v == p.song[row_]) return;
  beginWrite(!editPushed_);  // one undo step per edit
  editPushed_ = true;
  p.song[row_] = static_cast<uint8_t>(v);
  endWrite(row_, mt::SongOp::Edit);
  follow();
}

void SongScreen::snapRow() {
  editPushed_ = false;
  if (row_ >= len()) {
    origRow_ = -1;
    return;
  }
  origRow_ = row_;
  origPat_ = app_.project().song[row_];
}

void SongScreen::cancelRow() {
  patEdit_ = holdRow_ = false;
  mt::Project& p = app_.project();
  if (origRow_ < 0 || origRow_ >= len()) return;
  const int r = origRow_;
  origRow_ = -1;
  if (p.song[r] == origPat_) return;
  if (editPushed_ && undoCount_ > 0) {  // the snapshot holds what we restore
    undoHead_ = (undoHead_ + kUndoDepth - 1) % kUndoDepth;
    --undoCount_;
  }
  editPushed_ = false;
  beginWrite(false);
  p.song[r] = origPat_;
  endWrite(r, mt::SongOp::Edit);
  follow();
  app_.toast("CANCEL");
}

// ---- follow edit ----

bool SongScreen::folFieldOk(int f) const {
  const mt::SongFollow& fo = app_.project().songFol[row_];
  if (f == 1) return fo.act == static_cast<uint8_t>(mt::FollowAct::Jump);
  if (f == 2) return fo.act != static_cast<uint8_t>(mt::FollowAct::Next);  // NEXT: no chance
  return f == 0;
}

void SongScreen::snapFol() {
  if (row_ >= len()) return;
  editPushed_ = false;
  origFolRow_ = row_;
  origFol_ = app_.project().songFol[row_];
  folField_ = 0;
  patEdit_ = holdRow_ = false;
}

void SongScreen::setFol(const mt::SongFollow& f) {
  mt::Project& p = app_.project();
  if (row_ >= len() || memcmp(&f, &p.songFol[row_], sizeof(f)) == 0) return;
  beginWrite(!editPushed_);  // one undo step per edit
  editPushed_ = true;
  p.songFol[row_] = f;
  endWrite(row_, mt::SongOp::Edit);
}

// Action and target clamp (no wrap); chance in steps of 5 %. A JUMP gets a target inside the song.
void SongScreen::editFol(int delta) {
  if (delta == 0 || folField_ < 0 || row_ >= len()) return;
  mt::SongFollow f = app_.project().songFol[row_];
  if (folField_ == 0) {
    f.act = static_cast<uint8_t>(clampi(f.act + delta, 0, static_cast<int>(mt::FollowAct::Count) - 1));
    if (f.act == static_cast<uint8_t>(mt::FollowAct::Jump) && f.target >= len()) f.target = static_cast<uint8_t>(len() - 1);
  } else if (folField_ == 1) {
    f.target = static_cast<uint8_t>(clampi(f.target + delta, 0, len() - 1));
  } else {
    f.chance = static_cast<uint8_t>(clampi(f.chance + 5 * delta, 0, 100));
  }
  setFol(f);
  if (!folFieldOk(folField_)) folField_ = 0;
}

void SongScreen::nextFolField(int dir) {
  for (int k = 1; k <= 3; ++k) {
    const int f = ((folField_ + (dir > 0 ? k : -k)) % 3 + 3) % 3;
    if (folFieldOk(f)) {
      folField_ = f;
      return;
    }
  }
}

void SongScreen::cancelFol() {
  folField_ = -1;
  holdFol_ = false;
  mt::Project& p = app_.project();
  if (origFolRow_ < 0 || origFolRow_ >= len()) return;
  const int r = origFolRow_;
  origFolRow_ = -1;
  if (memcmp(&p.songFol[r], &origFol_, sizeof(origFol_)) == 0) return;
  if (editPushed_ && undoCount_ > 0) {  // the snapshot holds what we restore
    undoHead_ = (undoHead_ + kUndoDepth - 1) % kUndoDepth;
    --undoCount_;
  }
  editPushed_ = false;
  beginWrite(false);
  p.songFol[r] = origFol_;
  endWrite(r, mt::SongOp::Edit);
  app_.toast("CANCEL");
}

// ---- TRN / BPM edit ----

void SongScreen::snapVal() {
  if (row_ >= len() || !valCol(col_)) return;
  editPushed_ = false;
  origValRow_ = row_;
  origTrn_ = app_.project().songTrn[row_];
  origBpm_ = app_.project().songBpm[row_];
  valEdit_ = col_;
  patEdit_ = holdRow_ = false;
}

void SongScreen::setVal(int8_t trn, uint16_t bpm) {
  mt::Project& p = app_.project();
  if (row_ >= len() || (p.songTrn[row_] == trn && p.songBpm[row_] == bpm)) return;
  beginWrite(!editPushed_);  // one undo step per edit
  editPushed_ = true;
  p.songTrn[row_] = trn;
  p.songBpm[row_] = bpm;
  endWrite(row_, mt::SongOp::Edit);
}

// TRN -24..24 (coarse: an octave); BPM "-" or 20..300 (coarse: x10), "-" up starts at the project BPM.
void SongScreen::editVal(int delta, bool coarse) {
  if (delta == 0 || valEdit_ < 0 || row_ >= len()) return;
  const mt::Project& p = app_.project();
  int8_t trn = p.songTrn[row_];
  uint16_t bpm = p.songBpm[row_];
  if (valEdit_ == kTrnCol) {
    trn = mt::songTrnClamp(trn + delta * (coarse ? 12 : 1));
  } else if (bpm == 0) {
    if (delta > 0) bpm = engine::projectBpm(p);
  } else {
    const int v = bpm + delta * (coarse ? 10 : 1);
    bpm = v < mt::kSongBpmMin ? 0 : mt::songBpmClamp(v);
  }
  setVal(trn, bpm);
}

void SongScreen::cancelVal() {
  valEdit_ = -1;
  holdVal_ = false;
  mt::Project& p = app_.project();
  if (origValRow_ < 0 || origValRow_ >= len()) return;
  const int r = origValRow_;
  origValRow_ = -1;
  if (p.songTrn[r] == origTrn_ && p.songBpm[r] == origBpm_) return;
  if (editPushed_ && undoCount_ > 0) {  // the snapshot holds what we restore
    undoHead_ = (undoHead_ + kUndoDepth - 1) % kUndoDepth;
    --undoCount_;
  }
  editPushed_ = false;
  beginWrite(false);
  p.songTrn[r] = origTrn_;
  p.songBpm[r] = origBpm_;
  endWrite(r, mt::SongOp::Edit);
  app_.toast("CANCEL");
}

// ---- menu ----

void SongScreen::openMenu() {
  clampRow();
  const int n = len();
  const bool valid = n > 0;
  const bool room = n < mt::kSongMax;
  patEdit_ = holdRow_ = holdFol_ = holdVal_ = false;
  folField_ = valEdit_ = -1;
  char ins[32], append[28], title[28];
  snprintf(append,sizeof(append),"Append P%02u",app_.editPattern()+1u);
  snprintf(ins, sizeof(ins), "Insert P%02u above", app_.editPattern() + 1u);
  const MenuItem items[] = {
      {append, kAdd, room},           {ins, kInsert, room},
      {"Delete", kDelete, valid},    {"Slot mutes <- live", kMutesLive, valid},
      {"Clear slot mutes", kMutesClear, valid}, {"Follow: NEXT", kFolReset, valid},
      {"Clear song", kClear, valid},
      {app_.perf().on() ? "Perf: ON" : "Perf: OFF", kPerf},
      {view2_ ? "View: MUTES" : "View: TRN BPM", kView},
  };
  if (valid) snprintf(title, sizeof(title), "S%02d P%02u (%d/%d)", row_ + 1, app_.project().song[row_] + 1u,n,mt::kSongMax);
  else snprintf(title, sizeof(title), "SONG");
  app_.menu().open(title, items, sizeof(items) / sizeof(items[0]), [this](int id) { onMenu(id); });
}

void SongScreen::onMenu(int id) {
  const mt::Project& p = app_.project();
  switch (id) {
    case kAdd: add(); break;
    case kInsert: insertAt(row_, app_.editPattern(), 0); break;
    case kDelete: deleteRow(); break;
    case kMutesLive: {
      uint16_t m = 0;
      for (int t = 0; t < mt::kTracks; ++t)
        if (p.tracks[t].mute) m |= static_cast<uint16_t>(1u << t);
      setMask(m);
      break;
    }
    case kMutesClear: setMask(0); break;
    case kFolReset:
      editPushed_ = false;  // its own undo step
      setFol(mt::SongFollow());
      break;
    case kClear: {
      const MenuItem items[] = {{"Cancel", kCancel}, {"Clear song: sure?", kClearConfirm}};
      app_.menu().openChild("CLEAR SONG?", items, 2, [this](int i) { onMenu(i); });
      break;
    }
    case kClearConfirm: clearSong(); break;
    case kView: toggleView(); break;
    case kPerf: {  // shared with GRID's menu; on leaves pattern edit
      const bool on = !app_.perf().on();
      if (on) patEdit_ = holdRow_ = false;
      app_.perf().setOn(on);
      app_.toast(on ? "PERF: HOLD A BUTTON" : "PERF OFF");
      break;
    }
    default: break;
  }
  app_.invalidate();
}

// ---- input ----

void SongScreen::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  clampRow();
  const int n = len();
  switch (ev.type) {
    case InputType::EncTurn:
      if (app_.perf().turn(ev.delta, ev.shift)) break;  // a held PERF effect's depth; the cursor stays
      touched();
      if (folField_ >= 0) {
        if (ev.shift) nextFolField(ev.delta);
        else editFol(ev.delta);
      } else if (valEdit_ >= 0) editVal(ev.delta, ev.shift);
      else if (ev.shift) stepCol(ev.delta);
      else if (patEdit_) editPat(ev.delta);
      else if (n > 0) setRow(row_ + ev.delta);
      break;
    case InputType::EncClick:
      if (folField_ >= 0) {  // done; Shift: cancel
        if (ev.shift) cancelFol();
        else folField_ = -1;
        holdFol_ = false;
        break;
      }
      if (valEdit_ >= 0) {  // done; Shift: cancel
        if (ev.shift) cancelVal();
        else valEdit_ = -1;
        holdVal_ = false;
        break;
      }
      if (patEdit_ && ev.shift) {
        cancelRow();
        break;
      }
      if (ev.shift) {
        toggleView();
        break;
      }
      holdRow_ = false;  // a click edit: A release must not end it
      if (n == 0) {
        add();
      } else if (col_ == 0) {
        patEdit_ = !patEdit_;
        if (patEdit_) snapRow();
      } else if (col_ == kFolCol) {
        snapFol();
      } else if (valCol(col_)) {
        snapVal();
      } else {
        toggleMute(col_ - 1);
      }
      break;
    case InputType::EncLong: openMenu(); break;
    // A + turn: hold edit of the cursor slot's pattern, release = keep, B = cancel.
    case InputType::EditTurn:
      // PERF, nothing held: the scene crossfader (not on FOL: there A + turn edits the follow)
      if (col_ != kFolCol && !valCol(col_) && app_.perf().xfadeTurn(ev.delta, ev.shift)) break;
      if (valCol(col_) && n > 0) {  // on TRN / BPM: hold edit of the value
        if (valEdit_ < 0) {
          snapVal();
          holdVal_ = true;
        }
        touched();
        editVal(ev.delta, ev.shift);
        break;
      }
      if (col_ == kFolCol && n > 0) {  // on FOL: hold edit of the follow, from the action
        if (folField_ < 0) {
          snapFol();
          holdFol_ = true;
        }
        touched();
        editFol(ev.delta);
        break;
      }
      if (!patEdit_) {
        if (n == 0) break;
        patEdit_ = holdRow_ = true;
        col_ = 0;
        snapRow();
      }
      touched();
      editPat(ev.delta);
      break;
    case InputType::EditEnd:
      if (holdRow_) patEdit_ = false;
      if (holdFol_) folField_ = -1;
      if (holdVal_) valEdit_ = -1;
      holdRow_ = holdFol_ = holdVal_ = false;
      break;
    case InputType::EditCancel:
      if (patEdit_) cancelRow();
      if (folField_ >= 0) cancelFol();
      if (valEdit_ >= 0) cancelVal();
      holdRow_ = holdFol_ = holdVal_ = false;
      break;
    default: break;
  }
}

// B: leave follow / pattern edit, keeping the value; else PERF off.
void SongScreen::onBack() {
  if (folField_ >= 0) {
    folField_ = -1;
    holdFol_ = false;
  } else if (valEdit_ >= 0) {
    valEdit_ = -1;
    holdVal_ = false;
  } else if (patEdit_) {
    patEdit_ = holdRow_ = false;
  } else if (app_.perf().on()) {
    app_.perf().setOn(false);
    app_.toast("PERF OFF");
  }
}

void SongScreen::onATap(bool) {
  patEdit_ = holdRow_ = holdFol_ = holdVal_ = false;
  folField_ = valEdit_ = -1;
  add();
}

bool SongScreen::trackKey(int n, bool shift) {
  if (!shift && app_.perf().press(n)) return true;  // PERF while playing; Shift + N still mutes
  if (shift || len() == 0 || n < 0 || n >= mt::kTrackButtons) return false;
  clampRow();
  patEdit_ = holdRow_ = holdFol_ = holdVal_ = false;  // a separate undo step from the pattern / follow edit
  folField_ = valEdit_ = -1;
  toggleMute((app_.curTrack() / mt::kTrackButtons) * mt::kTrackButtons + n);
  return true;
}

void SongScreen::onTouch(const TouchEvent& ev) {
  clampRow();
  const int n = len();
  touched();
  if(ev.type==TouchType::Tap && ev.y<y0_+kHeadH && ev.x<kCellX0){toggleView();return;}
  if (ev.type == TouchType::Drag) {
    if (ev.id != gesture_) {  // first drag event of this gesture: pattern drag or scroll
      gesture_ = ev.id;
      dragAcc_ = 0;
      dragPat_ = n > 0 && colAt(ev.x0) == 0 && rowAt(ev.y0) == row_;
      if (dragPat_) {
        patEdit_ = false;
        snapRow();
      }
    }
    dragAcc_ += ev.dy;
    const int d = dragAcc_ / kRowH;
    dragAcc_ -= d * kRowH;
    if (d == 0) return;
    if (dragPat_) {
      editPat(-d);  // up = higher
    } else {
      top_ -= d;
      showRow(top_);  // clamps top_
    }
    return;
  }
  gesture_ = ev.id;
  dragPat_ = false;
  if (ev.type != TouchType::Tap && ev.type != TouchType::LongPress) return;
  const int r = rowAt(ev.y);
  if (r < 0) return;
  if (n == 0) {
    if (r == 0 && ev.type == TouchType::Tap) add();
    else if (r == 0) openMenu();
    return;
  }
  if (r >= n) return;
  patEdit_ = holdRow_ = false;
  setRow(r);
  if (ev.type == TouchType::LongPress) {
    openMenu();
    return;
  }
  const int c = colAt(ev.x);
  if (c < 0) return;
  setCol(c);
  if (c == kFolCol) snapFol();
  else if (valCol(c)) snapVal();
  else if (c > 0) toggleMute(c - 1);
}

// ---- drawing ----

void SongScreen::draw(LGFX_Sprite& s, int y0, int) {
  y0_ = y0;
  if (masksStale_) refreshMasks();
  const mt::Project& p = app_.project();
  const engine::Status& st = app_.status();
  const int n = len();
  clampRow();
  const int heard = st.song && st.songPos >= 0 && st.songPos < n ? st.songPos : -1;
  if (heard >= 0 && st.playing && millis() - userMs_ >= kFollowMs) showRow(heard);
  showRow(top_);  // the song may have shrunk
  char buf[16];

  // Tapping the always visible left caption switches views; slot count is in the menu title.
  s.setTextColor(kCursor);
  s.drawString(view2_ ? "TRN BPM" : "MUTES",kNumX,y0+4);
  if(heard>=0 && col_!=kFolCol && (st.playing || st.paused)){
    const int transpose=mt::songTrnClamp(p.songTrn[heard]);
    if(view2_){snprintf(buf,sizeof(buf),"T%+d",transpose);s.setTextColor(kDim);s.drawString(buf,168,y0+4);}
    else if(transpose){snprintf(buf,sizeof(buf),"%+d",transpose);s.setTextColor(kDim);s.drawString(buf,44,y0+4);}
  }
  const int badgeR=kCellX0;
  char perf[35];
  const int perfLength=col_!=kFolCol ? app_.perf().badge(perf,sizeof(perf)) : 0;
  if (view2_) {
    s.setTextColor(col_ == kTrnCol ? kCursor : kDim);
    s.drawString("TRN", kTrnX, y0 + 4);
    s.setTextColor(col_ == kBpmCol ? kCursor : kDim);
    s.drawString("BPM", kBpmX, y0 + 4);
    if (n > 0 && col_ == kFolCol) drawFolHeader(s, kBpmX + kValW + kCharW, y0 + 4);
  } else if (n > 0 && col_ == kFolCol) {
    drawFolHeader(s, badgeR > kCellX0 ? badgeR + kCharW : kCellX0, y0 + 4);
  }
  for (int t = 0; t < mt::kTracks && !view2_ && !(n > 0 && col_ == kFolCol); ++t) {
    snprintf(buf, sizeof(buf), "%d", t + 1);
    const int w = static_cast<int>(strlen(buf)) * kCharW;
    const int x = cellX(t) + (kCellW - w) / 2;
    if (x < badgeR + kCharW || (perfLength && x>=208)) continue;
    s.setTextColor(col_ == t + 1 ? kCursor : kDim);
    s.drawString(buf, x, y0 + 4);
  }
  if(perfLength){s.setTextColor(kYellow);s.drawString(perf,208,y0+4);}
  s.drawFastHLine(0, y0 + kHeadH - 1, kScreenW, kDim);

  const int ly = y0 + kHeadH;
  if (n == 0) {
    snprintf(buf, sizeof(buf), "+ ADD P%02u", app_.editPattern() + 1u);
    s.setTextColor(kText);
    s.drawString(buf, kNumX + 4, ly + 4);
    s.drawRect(kNumX, ly + 2, static_cast<int>(strlen(buf)) * kCharW + 8, kRowH - 4, kCursor);
    return;
  }
  for (int i = 0; i < kRows && top_ + i < n; ++i) {
    const int idx = top_ + i;
    const int y = ly + i * kRowH;
    const uint8_t pat = p.song[idx] < mt::kPatterns ? p.song[idx] : mt::kPatterns - 1;
    const uint16_t mute = p.songMute[idx];
    const bool isHeard = idx == heard;
    if (isHeard) {
      s.fillRect(0, y, kScreenW - 6, kRowH, kPlayBg);
      const int plen = p.patterns[pat].length;
      const int w = plen > 0 ? 24 * (st.pos < plen ? st.pos + 1 : plen) / plen : 0;
      s.fillRect(kNumX, y + kRowH - 3, w, 2, kCursor);
    }
    s.setTextColor(isHeard ? kCursor : (idx == row_ ? kText : kDim));
    snprintf(buf, sizeof(buf), "%02d", idx + 1);
    s.drawString(buf, kNumX, y + 4);
    const bool cur = idx == row_;
    s.setTextColor(cur && patEdit_ ? kEditCursor : kText);
    snprintf(buf, sizeof(buf), "P%02u", pat + 1u);
    s.drawString(buf, kPatX, y + 4);
    if (cur && col_ == 0) s.drawRect(kPatX - 3, y + 2, 3 * kCharW + 6, kRowH - 4, patEdit_ ? kEditCursor : kCursor);
    if (view2_) {
      drawVals(s, y, idx, cur);
      drawFol(s, y, p.songFol[idx], cur && col_ == kFolCol);
      continue;
    }
    if (p.songTrn[idx] || p.songBpm[idx]) s.fillRect(kPatX + 3 * kCharW + 4, y + 5, 3, 3, kCursor);  // TRN / BPM
    for (int t = 0; t < mt::kTracks; ++t) {
      const int x = cellX(t);
      const bool data = (masks_[pat] >> t) & 1;
      const bool muted = (mute >> t) & 1;
      if (data && !muted) s.fillRect(x + 2, y + 4, kCellW - 4, kRowH - 8, kText);
      else if (data) s.drawRect(x + 2, y + 4, kCellW - 4, kRowH - 8, kText);
      else if (muted) s.drawRect(x + 2, y + 4, kCellW - 4, kRowH - 8, kDim);
      else s.fillRect(x + kCellW / 2 - 1, y + kRowH / 2 - 1, 2, 2, kDim);
      if (cur && col_ == t + 1) s.drawRect(x, y + 2, kCellW, kRowH - 4, kCursor);
    }
    drawFol(s, y, p.songFol[idx], cur && col_ == kFolCol);
  }
  if (n > kRows) {  // scroll bar
    const int h = kRows * kRowH;
    const int bh = h * kRows / n;
    const int by = ly + (h - bh) * top_ / (n - kRows);
    s.fillRect(kScreenW - 4, by, 3, bh, kDim);
  }
}

// View 2: "+5" / "-3" / "..." (TRN 0) and "128" / "---" (BPM: keeps the tempo); the edited one red.
void SongScreen::drawVals(LGFX_Sprite& s, int y, int idx, bool cur) {
  const mt::Project& p = app_.project();
  char buf[8];
  const int trn = mt::songTrnClamp(p.songTrn[idx]);
  if (trn) snprintf(buf, sizeof(buf), "%+d", trn);
  else snprintf(buf, sizeof(buf), "...");
  s.setTextColor(trn ? kText : kDim);
  s.drawString(buf, kTrnX, y + 4);
  const uint16_t bpm = mt::songBpmClamp(p.songBpm[idx]);
  if (bpm) snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(bpm));
  else snprintf(buf, sizeof(buf), "---");
  s.setTextColor(bpm ? kText : kDim);
  s.drawString(buf, kBpmX, y + 4);
  if (!cur || !valCol(col_)) return;
  const int x = col_ == kTrnCol ? kTrnX : kBpmX;
  s.drawRect(x - 3, y + 2, kValW + 6, kRowH - 4, valEdit_ == col_ ? kEditCursor : kCursor);
}

void SongScreen::drawFol(LGFX_Sprite& s, int y, const mt::SongFollow& f, bool cur) {
  static const char kGlyph[] = {'.', '<', '=', '>', '?'};
  static_assert(sizeof(kGlyph) == static_cast<int>(mt::FollowAct::Count), "follow glyphs");
  const uint8_t a = f.act < static_cast<uint8_t>(mt::FollowAct::Count) ? f.act : 0;
  if (a == 0) {
    s.fillRect(kFolX + kFolW / 2 - 1, y + kRowH / 2 - 1, 2, 2, kDim);  // NEXT: as an empty cell
  } else {
    const char g[2] = {kGlyph[a], 0};
    s.setTextColor(f.chance >= 100 ? kText : kDim);
    s.drawString(g, kFolX + (kFolW - kCharW) / 2, y + 4);
  }
  if (cur) s.drawRect(kFolX, y + 2, kFolW, kRowH - 4, folField_ >= 0 ? kEditCursor : kCursor);
}

// "FOLLOW JUMP >07 75%": the cursor slot's follow, the edited field red. Returns the x after it.
int SongScreen::drawFolHeader(LGFX_Sprite& s, int x, int y) {
  const mt::SongFollow& f = app_.project().songFol[row_];
  const bool jump = f.act == static_cast<uint8_t>(mt::FollowAct::Jump);
  const bool next = f.act >= static_cast<uint8_t>(mt::FollowAct::Count) || f.act == 0;
  s.setTextColor(kDim);
  s.drawString("FOLLOW", x, y);
  x += 7 * kCharW;
  auto part = [&](const char* t, int field) {
    s.setTextColor(folField_ == field ? kEditCursor : kText);
    s.drawString(t, x, y);
    x += (static_cast<int>(strlen(t)) + 1) * kCharW;
  };
  char buf[8];
  part(mt::followName(f.act), 0);
  if (jump) {
    snprintf(buf, sizeof(buf), ">%02d", f.target + 1);
    part(buf, 1);
  }
  if (!next) {
    snprintf(buf, sizeof(buf), "%d%%", f.chance);
    part(buf, 2);
  }
  return x;
}

}  // namespace ui
