#pragma once
#include "model.h"
#include "sequencer.h"
#include "screen.h"

namespace ui {

// 4x4 pattern tiles. Tap / click = queue, Shift = select now, long = menu (copy, clear, length, song mode).
// In song mode the chain list replaces the tiles: header buttons "+ ADD" (appends the current pattern)
// and "SONG OFF"; rows "01 P03", the heard entry highlighted. Turn / tap = select, click / tap on the
// selected row = edit (turn or drag changes the pattern), long = row menu (insert, delete, duplicate).
class BankScreen : public Screen {
 public:
  explicit BankScreen(App& app) : app_(app) {}
  void onEnter() override;
  void onInput(const hw::InputEvent& ev) override;
  void onTouch(const TouchEvent& ev) override;
  void onProjectReplaced() override;
  void draw(LGFX_Sprite& s, int y0, int h) override;
  bool wantsRedraw(const engine::Status& st) override;

 private:
  enum MenuId : int {
    kCopyTo, kClear, kClearConfirm, kCancel, kLen16, kLen32, kLen64, kSongOn,
    kRowInsert, kRowDelete, kRowDup, kRowAppend, kSongOff
  };
  static constexpr int kCols = 4;
  static constexpr int kTileW = 112, kTileH = 64;
  static constexpr int kGapX = 6, kGapY = 4;
  static constexpr int kMarginX = (kScreenW - kCols * kTileW - (kCols - 1) * kGapX) / 2;
  static constexpr uint32_t kBlinkMs = 250;
  // Song chain view.
  static constexpr int kHeadH = 24, kRowH = 24;
  static constexpr int kRows = (kAreaH - kHeadH) / kRowH;
  static constexpr int kAddX0 = 296, kAddX1 = 376;   // "+ ADD"
  static constexpr int kOffX0 = 384, kOffX1 = 472;   // "SONG OFF"

  int tileAt(int x, int y) const;
  void activate(int idx, bool shift);  // queue / select, or the copy target
  void openMenu(int idx);
  void onMenu(int id);
  void copyTo(int dst);
  void snapshot(int pat);  // undo entry for any pattern, marks the project dirty
  void releaseTiesIfHeard(int pat);
  void refreshMasks();
  void drawTile(LGFX_Sprite& s, int idx, int x, int y, bool blinkOn);

  // Song chain view; every chain write is under engine::lockProject() + markDirty().
  bool songView() const;
  int chainLen() const;
  void setSong(bool on);
  void postChainEdit(int row, mt::ChainOp op);  // call while holding engine::lockProject()
  void chainInput(const hw::InputEvent& ev);
  void chainTouch(const TouchEvent& ev);
  void openRowMenu(int row);
  void insertRow(int at, uint8_t pat);
  void deleteRow(int row);
  void editRow(int delta);
  void clampRow();
  void showRow(int row);  // scroll so row is visible
  void drawChain(LGFX_Sprite& s, int y0);

  App& app_;
  int y0_ = kAreaY;
  int sel_ = 0;
  int menuPat_ = 0;
  int copyFrom_ = -1;  // waiting for the copy target
  bool blink_ = false;
  bool masksStale_ = true;
  uint16_t masks_[mt::kPatterns] = {0};  // bit t = track t has data within the length
  int row_ = 0;       // selected chain row
  int top_ = 0;       // first visible chain row
  int dragAcc_ = 0;   // drag px not yet turned into rows / edits
  int lastSongPos_ = -1;
  bool rowEdit_ = false;
};

}  // namespace ui
