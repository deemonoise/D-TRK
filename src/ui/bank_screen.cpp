#include "bank_screen.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "names.h"

namespace ui {
void BankScreen::onEnter() {
  sel_ = app_.editPattern();
  copyFrom_ = -1;
  masksStale_ = true;  // GRID may have edited patterns
  rowEdit_ = false;
  if (app_.status().songPos >= 0) row_ = app_.status().songPos;
  clampRow();
  showRow(row_);
}

void BankScreen::onProjectReplaced() {
  row_ = top_ = 0;
  rowEdit_ = false;
  copyFrom_ = -1;
  masksStale_ = true;
}

bool BankScreen::wantsRedraw(const engine::Status& st) {
  const bool on = (millis() / kBlinkMs) & 1;
  if (on == blink_) return false;
  blink_ = on;
  return st.queued >= 0;
}

void BankScreen::refreshMasks() {
  const mt::Project& p = app_.project();
  for (int i = 0; i < mt::kPatterns; ++i) {
    uint16_t m = 0;
    for (int t = 0; t < mt::kTracks; ++t) {
      const mt::Step* row = p.patterns[i].steps[t];
      const int len = p.patterns[i].length;
      for (int s = 0; s < len; ++s)
        if (!row[s].isEmpty()) {
          m |= 1 << t;
          break;
        }
    }
    masks_[i] = m;
  }
  masksStale_ = false;
}

int BankScreen::tileAt(int x, int y) const {
  const int col = (x - kMarginX) / (kTileW + kGapX);
  const int row = (y - y0_ - kGapY / 2) / (kTileH + kGapY);
  if (x < kMarginX || y < y0_ || col >= kCols || row >= kCols || row < 0) return -1;
  return row * kCols + col;
}

void BankScreen::snapshot(int pat) {
  // No lock: the engine never writes pattern data.
  if (mt::Undo* u = app_.undo()) u->push(static_cast<uint8_t>(pat), app_.project().patterns[pat]);
  app_.markDirty();  // every caller writes pat right after
}

void BankScreen::releaseTiesIfHeard(int pat) {
  if (pat == app_.editPattern()) engine::post(engine::Cmd::ReleaseTies);
}

void BankScreen::copyTo(int dst) {
  const int src = copyFrom_;
  copyFrom_ = -1;
  if (src < 0 || dst == src) {
    app_.toast("COPY CANCELLED");
    return;
  }
  snapshot(dst);
  mt::Project& p = app_.project();
  engine::lockProject();
  p.patterns[dst] = p.patterns[src];
  engine::unlockProject();
  releaseTiesIfHeard(dst);
  masks_[dst] = masks_[src];
  char msg[32];
  snprintf(msg, sizeof(msg), "P%02d -> P%02d", src + 1, dst + 1);
  app_.toast(msg);
}

void BankScreen::activate(int idx, bool shift) {
  sel_ = idx;
  if (copyFrom_ >= 0) {
    copyTo(idx);
    return;
  }
  engine::post(shift ? engine::Cmd::SelectPattern : engine::Cmd::QueuePattern, static_cast<uint16_t>(idx));
}

void BankScreen::openMenu(int idx) {
  sel_ = idx;
  menuPat_ = idx;
  copyFrom_ = -1;
  const MenuItem items[] = {
      {"Copy to...", kCopyTo}, {"Clear", kClear},          {"Length 16", kLen16},
      {"Length 32", kLen32},   {"Length 64", kLen64},      {"Song mode", kSongOn},
  };
  char title[28];
  snprintf(title, sizeof(title), "PATTERN %02d", idx + 1);
  app_.menu().open(title, items, sizeof(items) / sizeof(items[0]), [this](int id) { onMenu(id); });
}

void BankScreen::onMenu(int id) {
  mt::Project& p = app_.project();
  const int pat = menuPat_;
  char title[28];
  switch (id) {
    case kCopyTo:
      copyFrom_ = pat;
      app_.toast("TAP TARGET");
      break;
    case kClear: {
      const MenuItem items[] = {{"Cancel", kCancel}, {"Clear! (confirm)", kClearConfirm}};
      snprintf(title, sizeof(title), "CLEAR P%02d?", pat + 1);
      app_.menu().open(title, items, 2, [this](int i) { onMenu(i); });
      break;
    }
    case kClearConfirm:
      snapshot(pat);
      engine::lockProject();
      p.patterns[pat].clear();
      engine::unlockProject();
      releaseTiesIfHeard(pat);
      masks_[pat] = 0;
      app_.toast("CLEARED");
      break;
    case kLen16:
    case kLen32:
    case kLen64:
      snapshot(pat);
      engine::lockProject();
      p.patterns[pat].length = id == kLen16 ? 16 : (id == kLen32 ? 32 : 64);
      engine::unlockProject();
      releaseTiesIfHeard(pat);
      masksStale_ = true;
      break;
    case kSongOn: setSong(true); break;
    case kSongOff: setSong(false); break;
    case kRowInsert: insertRow(row_, app_.editPattern()); break;
    case kRowDelete: deleteRow(row_); break;
    case kRowDup:
      if (row_ < chainLen()) insertRow(row_ + 1, p.chain[row_]);
      break;
    case kRowAppend: insertRow(chainLen(), app_.editPattern()); break;
    default: break;
  }
  app_.invalidate();
}

void BankScreen::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  if (songView()) {
    chainInput(ev);
    return;
  }
  switch (ev.type) {
    case InputType::EncTurn: sel_ = ((sel_ + ev.delta) % mt::kPatterns + mt::kPatterns) % mt::kPatterns; break;
    case InputType::EncClick: activate(sel_, ev.shift); break;
    case InputType::EncLong: openMenu(sel_); break;
    default: break;
  }
}

void BankScreen::onTouch(const TouchEvent& ev) {
  if (songView()) {
    chainTouch(ev);
    return;
  }
  if (ev.type == TouchType::Drag) return;
  const int idx = tileAt(ev.x, ev.y);
  if (idx < 0) return;
  if (ev.type == TouchType::LongPress) openMenu(idx);
  else activate(idx, app_.shift());
}

void BankScreen::draw(LGFX_Sprite& s, int y0, int) {
  y0_ = y0;
  if (songView()) {
    drawChain(s, y0);
    return;
  }
  if (masksStale_) refreshMasks();
  const bool blinkOn = (millis() / kBlinkMs) & 1;
  for (int i = 0; i < mt::kPatterns; ++i) {
    const int x = kMarginX + (i % kCols) * (kTileW + kGapX);
    const int y = y0 + kGapY / 2 + (i / kCols) * (kTileH + kGapY);
    drawTile(s, i, x, y, blinkOn);
  }
}

void BankScreen::drawTile(LGFX_Sprite& s, int idx, int x, int y, bool blinkOn) {
  const mt::Pattern& pt = app_.project().patterns[idx];
  const engine::Status& st = app_.status();
  const bool playing = idx == st.pattern;
  const bool queued = idx == st.queued;
  const bool empty = masks_[idx] == 0;
  char buf[16];

  if (playing) s.fillRect(x, y, kTileW, kTileH, kPlayBg);
  else if (!empty) s.fillRect(x, y, kTileW, kTileH, kBeatBg);
  uint16_t border = kDim;
  if (playing) border = kCursor;
  if (queued) border = blinkOn ? kCursor : kDim;
  if (idx == copyFrom_) border = kEditCursor;
  s.drawRect(x, y, kTileW, kTileH, border);
  if (queued || idx == copyFrom_) s.drawRect(x + 1, y + 1, kTileW - 2, kTileH - 2, border);
  if (idx == sel_) s.drawRect(x + 3, y + 3, kTileW - 6, kTileH - 6, kText);

  s.setTextColor(empty ? kDim : kText);
  snprintf(buf, sizeof(buf), "P%02d", idx + 1);
  s.drawString(buf, x + 8, y + 6);
  snprintf(buf, sizeof(buf), "%u", pt.length);
  s.drawString(buf, x + kTileW - 8 - static_cast<int>(strlen(buf)) * kCharW, y + 6);
  s.setTextColor(kDim);
  s.drawString(resName(pt.res), x + 8, y + 24);

  // Track dots in two rows: tracks 1-8 above 9-16 (the two button halves).
  constexpr int kHalf = mt::kTracks / 2;
  for (int t = 0; t < mt::kTracks; ++t) {
    const int dx = x + 8 + (t % kHalf) * 12;
    const int dy = y + 44 + (t / kHalf) * 8;
    if (masks_[idx] & (1 << t)) s.fillRect(dx, dy, 6, 6, kCursor);
    else s.drawRect(dx, dy, 6, 6, kDim);
  }
}

// ---- song chain ----

bool BankScreen::songView() const { return app_.project().songMode; }

int BankScreen::chainLen() const {
  const int n = app_.project().chainLen;
  return n > mt::kChainMax ? mt::kChainMax : n;
}

void BankScreen::setSong(bool on) {
  engine::lockProject();
  app_.project().songMode = on;
  postChainEdit(0, mt::ChainOp::Edit);
  engine::unlockProject();
  app_.markDirty();
  copyFrom_ = -1;
  rowEdit_ = false;
  app_.toast(on ? "SONG ON (NEXT LOOP)" : "SONG OFF");
}

// Posted under the lock so the engine sees it before planning on the edited chain. If the queue is
// full the edit still applies, only an advance already planned or a shifted position may be off once.
void BankScreen::postChainEdit(int row, mt::ChainOp op) {
  if (!engine::post(engine::Cmd::ChainEdit, static_cast<uint16_t>((row << 8) | static_cast<int>(op))))
    app_.toast("ENGINE BUSY");
}

void BankScreen::clampRow() {
  const int n = chainLen();
  if (row_ >= n) row_ = n - 1;
  if (row_ < 0) row_ = 0;
  if (n == 0) rowEdit_ = false;
}

void BankScreen::showRow(int row) {
  if (row < top_) top_ = row;
  if (row >= top_ + kRows) top_ = row - kRows + 1;
  const int maxTop = chainLen() > kRows ? chainLen() - kRows : 0;
  if (top_ > maxTop) top_ = maxTop;
  if (top_ < 0) top_ = 0;
}

void BankScreen::insertRow(int at, uint8_t pat) {
  mt::Project& p = app_.project();
  const int n = chainLen();
  if (n >= mt::kChainMax) {
    app_.toast("CHAIN FULL");
    return;
  }
  if (at < 0) at = 0;
  if (at > n) at = n;
  engine::lockProject();
  memmove(p.chain + at + 1, p.chain + at, n - at);
  p.chain[at] = pat < mt::kPatterns ? pat : mt::kPatterns - 1;
  p.chainLen = static_cast<uint8_t>(n + 1);
  postChainEdit(at, mt::ChainOp::Insert);
  engine::unlockProject();
  app_.markDirty();
  row_ = at;
  showRow(row_);
}

void BankScreen::deleteRow(int row) {
  mt::Project& p = app_.project();
  const int n = chainLen();
  if (row < 0 || row >= n) return;
  engine::lockProject();
  memmove(p.chain + row, p.chain + row + 1, n - row - 1);
  p.chainLen = static_cast<uint8_t>(n - 1);
  postChainEdit(row, mt::ChainOp::Delete);
  engine::unlockProject();
  app_.markDirty();
  clampRow();
  showRow(row_);
}

void BankScreen::editRow(int delta) {
  mt::Project& p = app_.project();
  if (delta == 0 || row_ >= chainLen()) return;
  int v = p.chain[row_] + delta;
  v = v < 0 ? 0 : (v >= mt::kPatterns ? mt::kPatterns - 1 : v);
  if (v == p.chain[row_]) return;
  engine::lockProject();
  p.chain[row_] = static_cast<uint8_t>(v);
  postChainEdit(row_, mt::ChainOp::Edit);
  engine::unlockProject();
  app_.markDirty();
}

void BankScreen::openRowMenu(int row) {
  const int n = chainLen();
  const bool valid = row >= 0 && row < n;
  const bool room = n < mt::kChainMax;
  const unsigned cur = app_.editPattern() + 1u;
  char ins[32], app[32], title[40];
  snprintf(ins, sizeof(ins), "Insert P%02u before", cur);
  snprintf(app, sizeof(app), "Append P%02u", cur);
  const MenuItem items[] = {
      {ins, kRowInsert, valid && room}, {"Delete", kRowDelete, valid}, {"Duplicate", kRowDup, valid && room},
      {app, kRowAppend, room},          {"Song mode off", kSongOff},
  };
  if (valid) {
    row_ = row;
    snprintf(title, sizeof(title), "SONG %02d: P%02u", row + 1, app_.project().chain[row] + 1u);
  } else {
    snprintf(title, sizeof(title), "SONG");
  }
  rowEdit_ = false;
  app_.menu().open(title, items, sizeof(items) / sizeof(items[0]), [this](int id) { onMenu(id); });
}

void BankScreen::chainInput(const hw::InputEvent& ev) {
  using hw::InputType;
  clampRow();
  const int n = chainLen();
  switch (ev.type) {
    case InputType::EncTurn:
      if (rowEdit_) {
        editRow(ev.delta);
      } else if (n > 0) {
        row_ = ((row_ + ev.delta) % n + n) % n;
        showRow(row_);
      }
      break;
    case InputType::EncClick:
      if (n == 0) insertRow(0, app_.editPattern());
      else rowEdit_ = !rowEdit_;
      break;
    case InputType::EncLong: openRowMenu(row_); break;
    default: break;
  }
}

void BankScreen::chainTouch(const TouchEvent& ev) {
  clampRow();
  const int n = chainLen();
  if (ev.type == TouchType::Drag) {
    dragAcc_ += ev.dy;
    if (rowEdit_) {
      const int d = dragAcc_ / TouchTracker::kDragStep;
      dragAcc_ -= d * TouchTracker::kDragStep;
      editRow(-d);
    } else {
      const int rows = dragAcc_ / kRowH;
      dragAcc_ -= rows * kRowH;
      top_ -= rows;
      showRow(top_);  // clamps top_
    }
    return;
  }
  dragAcc_ = 0;  // a new gesture
  if (ev.y < y0_ + kHeadH) {
    if (ev.type != TouchType::Tap) return;
    if (ev.x >= kAddX0 && ev.x < kAddX1) insertRow(n, app_.editPattern());
    else if (ev.x >= kOffX0 && ev.x < kOffX1) setSong(false);
    return;
  }
  const int r = top_ + (ev.y - y0_ - kHeadH) / kRowH;
  if (ev.type == TouchType::LongPress) {
    openRowMenu(r < n ? r : -1);
    return;
  }
  if (r >= n) {
    rowEdit_ = false;
    return;
  }
  if (r == row_) {
    rowEdit_ = !rowEdit_;
  } else {
    row_ = r;
    rowEdit_ = false;
  }
  showRow(row_);
}

void BankScreen::drawChain(LGFX_Sprite& s, int y0) {
  const mt::Project& p = app_.project();
  const engine::Status& st = app_.status();
  const int n = chainLen();
  clampRow();
  const int heard = (st.playing || st.paused) ? st.songPos : -1;
  if (heard != lastSongPos_) {  // follow playback when the heard entry changes
    lastSongPos_ = heard;
    if (heard >= 0 && heard < n && !rowEdit_) showRow(heard);
  }
  showRow(top_);  // the chain may have shrunk
  char buf[32];

  s.setTextColor(kCursor);
  snprintf(buf, sizeof(buf), "SONG %d/%d", n, mt::kChainMax);
  s.drawString(buf, 8, y0 + 4);
  s.setTextColor(n < mt::kChainMax ? kText : kDim);
  s.drawRect(kAddX0, y0 + 2, kAddX1 - kAddX0, kHeadH - 4, kDim);
  s.drawString("+ ADD", kAddX0 + (kAddX1 - kAddX0 - 5 * kCharW) / 2, y0 + 4);
  s.setTextColor(kText);
  s.drawRect(kOffX0, y0 + 2, kOffX1 - kOffX0, kHeadH - 4, kDim);
  s.drawString("SONG OFF", kOffX0 + (kOffX1 - kOffX0 - 8 * kCharW) / 2, y0 + 4);
  s.drawFastHLine(0, y0 + kHeadH - 1, kScreenW, kDim);

  const int ly = y0 + kHeadH;
  if (n == 0) {
    s.setTextColor(kDim);
    s.drawString("CHAIN EMPTY: THE CURRENT PATTERN LOOPS", 16, ly + 16);
    s.drawString("CLICK OR + ADD TO APPEND IT", 16, ly + 40);
    return;
  }
  for (int i = 0; i < kRows && top_ + i < n; ++i) {
    const int idx = top_ + i;
    const int y = ly + i * kRowH;
    const uint8_t pat = p.chain[idx] < mt::kPatterns ? p.chain[idx] : mt::kPatterns - 1;
    const mt::Pattern& pt = p.patterns[pat];
    if (idx == heard) s.fillRect(0, y, kScreenW - 8, kRowH, kPlayBg);
    else if (idx == row_) s.fillRect(0, y, kScreenW - 8, kRowH, kSelBg);
    if (idx == row_) s.drawRect(0, y, kScreenW - 8, kRowH, kText);
    s.setTextColor(idx == heard ? kCursor : kDim);
    if (idx == heard) s.drawString(">", 4, y + 4);
    snprintf(buf, sizeof(buf), "%02d", idx + 1);
    s.drawString(buf, 16, y + 4);
    s.setTextColor(idx == row_ && rowEdit_ ? kEditCursor : kText);
    snprintf(buf, sizeof(buf), "P%02u", pat + 1u);
    s.drawString(buf, 64, y + 4);
    s.setTextColor(kDim);
    snprintf(buf, sizeof(buf), "%3u STEPS  %s", pt.length, resName(pt.res));
    s.drawString(buf, 144, y + 4);
  }
  if (n > kRows) {  // scroll bar
    const int h = kRows * kRowH;
    const int bh = h * kRows / n;
    const int by = ly + (h - bh) * top_ / (n - kRows);
    s.fillRect(kScreenW - 5, by, 3, bh, kDim);
  }
}

}  // namespace ui
