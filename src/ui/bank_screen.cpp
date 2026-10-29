#include "bank_screen.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "edit_ops.h"
#include "pattern_tools.h"
#include "names.h"

namespace ui {
void BankScreen::onEnter() {
  sel_ = app_.editPattern();
  copyFrom_ = -1;
  masksStale_ = true;  // GRID may have edited patterns
}

void BankScreen::onProjectReplaced() {
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
  for (int i = 0; i < mt::kPatterns; ++i) masks_[i] = mt::patternTrackMask(p, i);
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
  if (!shift && app_.songPlays()) {  // as App::queueKey
    app_.toast("SONG MODE");
    return;
  }
  engine::post(shift ? engine::Cmd::SelectPattern : engine::Cmd::QueuePattern, static_cast<uint16_t>(idx));
}

void BankScreen::openMenu(int idx) {
  sel_ = idx;
  menuPat_ = idx;
  copyFrom_ = -1;
  const MenuItem items[] = {
      {"Copy to...", kCopyTo}, {"Clear...", kClear}, {"Length...", kLength},
      {"Pattern x2...", kDouble, app_.project().patterns[idx].length*2<=mt::kMaxSteps},
  };
  char title[28];
  snprintf(title, sizeof(title), "PATTERN %02d", idx + 1);
  app_.menu().open(title, items, sizeof(items) / sizeof(items[0]), [this](int id) { onMenu(id); });
}

void BankScreen::openLengthMenu() {
  const int length=app_.project().patterns[menuPat_].length;
  const int values[]={16,32,64,128};
  char labels[4][20],title[28]; MenuItem rows[4];
  for(int i=0;i<4;++i){
    snprintf(labels[i],sizeof(labels[i]),"%s%d steps",length==values[i]?"> ":"",values[i]);
    rows[i]={labels[i],kLen16+i};
  }
  snprintf(title,sizeof(title),"P%02d LENGTH: %d",menuPat_+1,length);
  app_.menu().openChild(title,rows,4,[this](int id){onMenu(id);});
}

void BankScreen::openDoubleMenu() {
  const int length=app_.project().patterns[menuPat_].length;
  char title[28],label[28];
  snprintf(title,sizeof(title),"PATTERN P%02d x2",menuPat_+1);
  snprintf(label,sizeof(label),"Apply: %d -> %d steps",length,length*2);
  const MenuItem rows[]={{"All 16 tracks",kCancel,false},{label,kDoubleApply,length*2<=mt::kMaxSteps}};
  app_.menu().openChild(title,rows,2,[this,length](int id){
    if(app_.project().patterns[menuPat_].length!=length){app_.toast("TARGET CHANGED");return;}
    onMenu(id);
  });
}

void BankScreen::onMenu(int id) {
  mt::Project& p = app_.project();
  const int pat = menuPat_;
  char title[28];
  switch (id) {
    case kLength: openLengthMenu(); break;
    case kDouble: openDoubleMenu(); break;
    case kDoubleApply: {
      auto& target=p.patterns[pat];
      if(target.length*2>mt::kMaxSteps){app_.toast("128 STEPS MAX");break;}
      if(!app_.undo()){app_.toast("NO MEMORY");break;}
      app_.setRecording(false,false);app_.pushUndo(pat);
      engine::lockProject();const auto result=mt::doublePattern(target);engine::unlockProject();
      if(result!=mt::ToolResult::Changed){app_.dropUndo();app_.toast("NO CHANGE");break;}
      app_.markDirty();releaseTiesIfHeard(pat);masksStale_=true;app_.toast("PATTERN DOUBLED");
      break;
    }
    case kCopyTo:
      copyFrom_ = pat;
      app_.toast("TAP TARGET");
      break;
    case kClear: {
      const MenuItem items[] = {{"Cancel", kCancel}, {"Clear! (confirm)", kClearConfirm}};
      snprintf(title, sizeof(title), "CLEAR P%02d?", pat + 1);
      app_.menu().openChild(title, items, 2, [this](int i) { onMenu(i); });
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
    case kLen128:
      if(p.patterns[pat].length==(id==kLen16?16:id==kLen32?32:id==kLen64?64:128)){app_.toast("NO CHANGE");break;}
      snapshot(pat);
      engine::lockProject();
      p.patterns[pat].length = id == kLen16 ? 16 : id == kLen32 ? 32 : id == kLen64 ? 64 : 128;
      p.patterns[pat].fitTrackLen();
      engine::unlockProject();
      releaseTiesIfHeard(pat);
      masksStale_ = true;
      break;
    default: break;
  }
  app_.invalidate();
}

void BankScreen::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  switch (ev.type) {
    case InputType::EncTurn: sel_ = ((sel_ + ev.delta) % mt::kPatterns + mt::kPatterns) % mt::kPatterns; break;
    case InputType::EncClick: activate(sel_, ev.shift); break;
    case InputType::EncLong: openMenu(sel_); break;
    default: break;
  }
}

void BankScreen::onTouch(const TouchEvent& ev) {
  if (ev.type == TouchType::Drag) return;
  const int idx = tileAt(ev.x, ev.y);
  if (idx < 0) return;
  if (ev.type == TouchType::LongPress) openMenu(idx);
  else activate(idx, app_.shift());
}

void BankScreen::draw(LGFX_Sprite& s, int y0, int) {
  y0_ = y0;
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

// B: leave copy mode.
void BankScreen::onBack() {
  if (copyFrom_ < 0) return;
  copyFrom_ = -1;
  app_.toast("CANCEL");
}

}  // namespace ui
