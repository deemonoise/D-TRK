#pragma once
#include "model.h"
#include "screen.h"

namespace ui {

// 4x4 pattern tiles. Tap / click = queue, Shift = select now, long = menu (copy to, clear, length).
class BankScreen : public Screen {
 public:
  explicit BankScreen(App& app) : app_(app) {}
  void onEnter() override;
  void onInput(const hw::InputEvent& ev) override;
  void onTouch(const TouchEvent& ev) override;
  void onProjectReplaced() override;
  void onBack() override;
  void draw(LGFX_Sprite& s, int y0, int h) override;
  bool wantsRedraw(const engine::Status& st) override;

 private:
  enum MenuId : int { kCopyTo, kClear, kClearConfirm, kCancel, kLen16, kLen32, kLen64, kLen128, kLength, kDouble, kDoubleApply };
  static constexpr int kCols = 4;
  static constexpr int kGapX = 6, kGapY = 4;
  static constexpr int kTileW = 112, kTileH = (kAreaH-kCols*kGapY)/kCols;
  static_assert(kTileH >= 58, "Pattern tile labels and two track-dot rows fit");
  static constexpr int kMarginX = (kScreenW - kCols * kTileW - (kCols - 1) * kGapX) / 2;
  static constexpr uint32_t kBlinkMs = 250;

  int tileAt(int x, int y) const;
  void activate(int idx, bool shift);  // queue / select, or the copy target
  void openMenu(int idx);
  void onMenu(int id);
  void openLengthMenu();
  void openDoubleMenu();
  void copyTo(int dst);
  void snapshot(int pat);  // undo entry for any pattern, marks the project dirty
  void releaseTiesIfHeard(int pat);
  void refreshMasks();
  void drawTile(LGFX_Sprite& s, int idx, int x, int y, bool blinkOn);

  App& app_;
  int y0_ = kAreaY;
  int sel_ = 0;
  int menuPat_ = 0;
  int copyFrom_ = -1;  // waiting for the copy target
  bool blink_ = false;
  bool masksStale_ = true;
  uint16_t masks_[mt::kPatterns] = {0};  // bit t = track t has data within the length
};

}  // namespace ui
