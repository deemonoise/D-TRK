#pragma once
#include "model.h"
#include "param_list.h"
#include "screen.h"

namespace ui {

// Settings of App::curTrack(). Shift+turn (or a tap on the header arrows) = track -+1.
// Name: click to edit, turn = character, Shift+turn = position.
class TrackScreen : public Screen {
 public:
  explicit TrackScreen(App& app);
  void onEnter() override;
  void onInput(const hw::InputEvent& ev) override;
  void onTouch(const TouchEvent& ev) override;
  void draw(LGFX_Sprite& s, int y0, int h) override;

 private:
  enum Row : int { kName, kChannel, kVel, kGate, kCcA, kCcB, kProgram, kMute, kSolo, kRows };
  static constexpr int kHeaderH = 28;
  static constexpr int kArrowW = 96;  // header hit area on each side
  static constexpr int kNameLen = 8;

  mt::TrackCfg& cfg();
  void changeTrack(int d);
  void leaveEdit();
  void fixNames();  // empty name -> TRKn
  bool nameEdit() const { return list_.editing() && list_.sel() == kName; }
  void editName(int delta);  // under lock

  App& app_;
  Param params_[kRows];
  ParamList list_{kAreaY + kHeaderH};
  int y0_ = kAreaY;
  int namePos_ = 0;
};

}  // namespace ui
