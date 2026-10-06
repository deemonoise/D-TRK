#pragma once
#include "model.h"
#include "param_list.h"
#include "screen.h"

namespace ui {

// Settings of App::curTrack(). MIDI-only rows are grey on an INT track. Shift+turn (or a tap on the header arrows) = track -+1.
// Name: click to edit, turn = character, Shift+turn = position.
class TrackScreen : public Screen {
 public:
  explicit TrackScreen(App& app);
  void onEnter() override;
  void onInput(const hw::InputEvent& ev) override;
  void onTouch(const TouchEvent& ev) override;
  void draw(LGFX_Sprite& s, int y0, int h) override;

 private:
  enum Row : int { kName, kOut, kInstr, kVol, kChannel, kVel, kGate, kPatLen, kCcA, kCcB, kProgram, kMute, kSolo, kRows };
  static constexpr int kVisibleRows = 10;  // (kAreaH - kHeaderH) / ParamList::kRowH
  static constexpr int kHeaderH = 28;
  static constexpr int kArrowW = 96;  // header hit area on each side
  static constexpr int kNameLen = 8;

  mt::TrackCfg& cfg();
  bool internal() { return cfg().out == mt::TrackOut::Int; }
  void changeTrack(int d);
  void leaveEdit();
  void fixNames();  // empty name -> TRKn
  bool nameEdit() const { return list_.editing() && list_.sel() == kName; }
  void editName(int delta);  // under lock

  App& app_;
  Param params_[kRows];
  uint32_t patLenSeq_ = 0;  // App::editSeq() right after the last Pat len edit: one undo snapshot per run
  ParamList list_{kAreaY + kHeaderH};
  int y0_ = kAreaY;
  int namePos_ = 0;
};

}  // namespace ui
