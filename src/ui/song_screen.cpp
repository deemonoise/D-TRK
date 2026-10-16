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
  clampRow();
  showRow(row_);
}

void SongScreen::onProjectReplaced() {
  row_ = col_ = top_ = 0;
  patEdit_ = holdRow_ = false;
  origRow_ = -1;
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
  if (n == 0) patEdit_ = holdRow_ = false;
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
    patEdit_ = holdRow_ = false;
    origRow_ = -1;
  }
  follow();
}

void SongScreen::setCol(int c) {
  col_ = clampi(c, 0, kCols - 1);
  if (col_ == 0) return;
  patEdit_ = holdRow_ = false;
  app_.setCurTrack(col_ - 1);  // the track buttons address its half
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
  if (x < kPatX - 4) return -1;
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
  patEdit_ = holdRow_ = false;
  origRow_ = -1;
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
  endWrite(s.row, op);
  clampRow();
  showRow(row_);
  follow();
  app_.toast("UNDO");
}

void SongScreen::insertAt(int at, uint8_t pat, uint16_t mute) {
  if (len() >= mt::kSongMax) {
    app_.toast("SONG FULL");
    return;
  }
  at = clampi(at, 0, len());
  beginWrite(true, at, mt::SongOp::Insert);
  mt::songInsert(app_.project(), at, pat, mute);
  endWrite(at, mt::SongOp::Insert);
  setRow(at);
  touched();
}

// A: a copy of the cursor slot below it; empty song: the heard pattern, nothing muted.
void SongScreen::add() {
  const mt::Project& p = app_.project();
  if (len() == 0) insertAt(0, app_.editPattern(), 0);
  else insertAt(row_ + 1, p.song[row_], p.songMute[row_]);
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
  p.songLen = 0;
  endWrite(0, mt::SongOp::Edit);
  row_ = top_ = 0;
  patEdit_ = holdRow_ = false;
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

// ---- menu ----

void SongScreen::openMenu() {
  clampRow();
  const int n = len();
  const bool valid = n > 0;
  const bool room = n < mt::kSongMax;
  patEdit_ = holdRow_ = false;
  char ins[32], title[28];
  snprintf(ins, sizeof(ins), "Insert P%02u above", app_.editPattern() + 1u);
  const MenuItem items[] = {
      {"Add", kAdd, room},           {ins, kInsert, room},
      {"Delete", kDelete, valid},    {"Mutes <- live", kMutesLive, valid},
      {"Clear mutes", kMutesClear, valid}, {"Clear song", kClear, valid},
      {app_.perf().on() ? "Perf: ON" : "Perf: OFF", kPerf},
  };
  if (valid) snprintf(title, sizeof(title), "SLOT %02d: P%02u", row_ + 1, app_.project().song[row_] + 1u);
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
    case kClear: {
      const MenuItem items[] = {{"Cancel", kCancel}, {"Clear song: sure?", kClearConfirm}};
      app_.menu().open("CLEAR SONG?", items, 2, [this](int i) { onMenu(i); });
      break;
    }
    case kClearConfirm: clearSong(); break;
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
      if (ev.shift) setCol(col_ + ev.delta);
      else if (patEdit_) editPat(ev.delta);
      else if (n > 0) setRow(row_ + ev.delta);
      break;
    case InputType::EncClick:
      if (patEdit_ && ev.shift) {
        cancelRow();
        break;
      }
      holdRow_ = false;  // a click edit: A release must not end it
      if (n == 0) {
        add();
      } else if (col_ == 0) {
        patEdit_ = !patEdit_;
        if (patEdit_) snapRow();
      } else {
        toggleMute(col_ - 1);
      }
      break;
    case InputType::EncLong: openMenu(); break;
    // A + turn: hold edit of the cursor slot's pattern, release = keep, B = cancel.
    case InputType::EditTurn:
      if (app_.perf().xfadeTurn(ev.delta, ev.shift)) break;  // PERF, nothing held: the scene crossfader
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
      holdRow_ = false;
      break;
    case InputType::EditCancel:
      if (patEdit_) cancelRow();
      holdRow_ = false;
      break;
    default: break;
  }
}

// B: leave pattern edit, keeping the value; else PERF off.
void SongScreen::onBack() {
  if (patEdit_) {
    patEdit_ = holdRow_ = false;
  } else if (app_.perf().on()) {
    app_.perf().setOn(false);
    app_.toast("PERF OFF");
  }
}

void SongScreen::onATap(bool) {
  patEdit_ = holdRow_ = false;
  add();
}

bool SongScreen::trackKey(int n, bool shift) {
  if (!shift && app_.perf().press(n)) return true;  // PERF while playing; Shift + N still mutes
  if (shift || len() == 0 || n < 0 || n >= mt::kTrackButtons) return false;
  clampRow();
  patEdit_ = holdRow_ = false;  // a separate undo step from the pattern edit
  toggleMute((app_.curTrack() / mt::kTrackButtons) * mt::kTrackButtons + n);
  return true;
}

void SongScreen::onTouch(const TouchEvent& ev) {
  clampRow();
  const int n = len();
  touched();
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
  if (c > 0) toggleMute(c - 1);
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

  // Header: count, track numbers. The PERF badge replaces the count, grows with the held effects
  // and covers the numbers it reaches.
  const int badgeR = app_.perf().drawBadge(s, kNumX, y0 + 4);
  if (badgeR == kNumX) {
    s.setTextColor(kText);
    snprintf(buf, sizeof(buf), "%d/%d", n, mt::kSongMax);
    s.drawString(buf, kNumX, y0 + 4);
  }
  for (int t = 0; t < mt::kTracks; ++t) {
    snprintf(buf, sizeof(buf), "%d", t + 1);
    const int w = static_cast<int>(strlen(buf)) * kCharW;
    const int x = cellX(t) + (kCellW - w) / 2;
    if (x < badgeR + kCharW) continue;
    s.setTextColor(col_ == t + 1 ? kCursor : kDim);
    s.drawString(buf, x, y0 + 4);
  }
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
  }
  if (n > kRows) {  // scroll bar
    const int h = kRows * kRowH;
    const int bh = h * kRows / n;
    const int by = ly + (h - bh) * top_ / (n - kRows);
    s.fillRect(kScreenW - 4, by, 3, bh, kDim);
  }
}

}  // namespace ui
