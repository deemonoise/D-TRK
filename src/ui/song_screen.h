#pragma once
#include "edit_ops.h"
#include "model.h"
#include "screen.h"
#include "sequencer.h"

namespace ui {

// Song slots, one row each: "01  P03  [16 track cells]" (filled = plays, frame = muted, dot = no
// data). Turn = row (in pattern edit: the pattern), Shift + turn = column, click = PAT: pattern edit /
// track: mute, A = add a copy below, A + turn = pattern edit (B cancels), long = row menu. Track
// buttons toggle the cursor slot's mutes; with PERF on (App::perf(), shared with GRID) they hold
// its effects while playing. Every write is under engine::lockProject() + SongEdit.
class SongScreen : public Screen {
 public:
  explicit SongScreen(App& app) : app_(app) {}
  void onEnter() override;
  void onPatternChange() override { masksStale_ = true; }
  void onProjectReplaced() override;
  void onInput(const hw::InputEvent& ev) override;
  void onTouch(const TouchEvent& ev) override;
  void onBack() override;
  void onATap(bool shift) override;
  void draw(LGFX_Sprite& s, int y0, int h) override;
  bool wantsRedraw(const engine::Status& st) override;
  int cursor() const;  // selected slot, 0 when the song is empty
  // Track button N: PERF on and playing: holds its effect; else toggles that track's mute in the
  // cursor slot; false (App mutes live) on Shift or an empty song.
  bool trackKey(int n, bool shift);
  void undo();  // with a toast

 private:
  enum MenuId : int {
    kAdd, kInsert, kDelete, kMutesLive, kMutesClear, kClear, kClearConfirm, kCancel, kPerf
  };
  static constexpr int kHeadH = 24, kRowH = 24;
  static constexpr int kRows = (kAreaH - kHeadH) / kRowH;  // 10
  static constexpr int kNumX = 4, kPatX = 30;
  static constexpr int kCellX0 = 66, kCellW = 24, kHalfGap = 4;
  static constexpr int kCols = 1 + mt::kTracks;  // PAT + tracks
  static constexpr uint32_t kFollowMs = 2000;    // no auto-scroll this long after user input
  static constexpr int kUndoDepth = 8;
  static_assert(kCellX0 + mt::kTracks * kCellW + kHalfGap <= 476, "cells fit");

  struct Snap {
    mt::SongOp op;  // the write it precedes: undo posts the inverse
    uint8_t row;
    uint8_t len;
    uint8_t song[mt::kSongMax];
    uint16_t mute[mt::kSongMax];
  };

  int len() const;
  static int cellX(int t) { return kCellX0 + t * kCellW + (t >= mt::kTracks / 2 ? kHalfGap : 0); }
  int colAt(int x) const;  // -1 = number column, 0 = PAT, 1..16 = track
  int rowAt(int y) const;  // slot index (may be >= len), -1 = header / below the rows
  void setRow(int r);      // clamps, scrolls, follows (stopped)
  void setCol(int c);
  void showRow(int r);
  void clampRow();
  void follow();  // stopped: GRID shows the cursor slot's pattern
  void touched() { userMs_ = millis(); }
  void refreshMasks();

  // Writes: begin (undo snapshot + lock) ... end (SongEdit, unlock, dirty).
  void beginWrite(bool undo = true, int row = 0, mt::SongOp op = mt::SongOp::Edit);
  void endWrite(int row, mt::SongOp op);
  void pushSongUndo(int row, mt::SongOp op);
  void add();
  void insertAt(int at, uint8_t pat, uint16_t mute);
  void deleteRow();
  void toggleMute(int track);
  void setMask(uint16_t m);
  void clearSong();
  void editPat(int delta);
  void snapRow();
  void cancelRow();
  void openMenu();
  void onMenu(int id);

  App& app_;
  int y0_ = kAreaY;
  int row_ = 0, col_ = 0, top_ = 0;
  bool patEdit_ = false;
  bool holdRow_ = false;      // pattern edit by A + turn (A release ends it)
  bool editPushed_ = false;   // the pattern edit took its undo snapshot
  int origRow_ = -1;
  uint8_t origPat_ = 0;
  uint16_t gesture_ = 0;      // touch gesture id seen last
  bool dragPat_ = false;      // the drag edits the pattern
  int dragAcc_ = 0;
  uint32_t userMs_ = 0;
  int followPat_ = -1;        // pattern follow() posted last
  uint16_t masks_[mt::kPatterns] = {0};
  bool masksStale_ = true;
  int8_t lastSongPos_ = -1;
  uint8_t lastPos_ = 0;
  bool lastSong_ = false;
  Snap* undo_ = nullptr;  // kUndoDepth ring, PSRAM; nullptr = undo off
  bool undoTried_ = false;
  int undoHead_ = 0, undoCount_ = 0;
};

}  // namespace ui
