#pragma once
#include "model.h"
#include "param_list.h"
#include "screen.h"

namespace ui {

// Settings of App::curTrack(). MIDI-only rows are grey on an INT track. Shift+turn (or a tap on the header arrows) = track -+1.
// Name: click to edit, turn = character, Shift+turn = position.
// MIXER view (Shift + click, Shift + tap on the TRACK tab, long press menu): 8 strips of the half
// holding the cursor (as GRID's Overview) and the master strip MAIN. Strip: name, volume fader (INT),
// delay / reverb send of its instrument (read only), M / S. Turn = volume of the selected strip
// (Shift x10), Shift + turn = strip (0..7, MAIN), click = mute; touch: name selects, fader follows the
// finger, M / S toggle.
class TrackScreen : public Screen {
 public:
  explicit TrackScreen(App& app);
  void onEnter() override;
  void onInput(const hw::InputEvent& ev) override;
  void onTouch(const TouchEvent& ev) override;
  void draw(LGFX_Sprite& s, int y0, int h) override;
  bool wantsRedraw(const engine::Status& st) override;
  // MIX tab (true) or TRACK tab (false): the mixer view or the track settings.
  void setMixer(bool on);

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
  // MIXER: 8 + 8 x 52 + 8 + 48 = 480.
  static constexpr int kStrips = 8, kStripW = 52, kStripX0 = 8;
  static constexpr int kMasterW = 48, kMasterX = kStripX0 + kStrips * kStripW + 8;  // 432
  static constexpr int kMaster = kStrips;  // mixSel_ of the master strip
  static constexpr int kNameY = 4, kFaderY = 24, kFaderH = 120, kFaderW = 12, kValY = 148;
  static constexpr int kSendY = 168, kBtnY = 208, kBtnW = 22, kBtnH = 18;
  enum class Part : uint8_t { Name, Fader, Mute, Solo };
  int firstTrack() const;
  void mixerInput(const hw::InputEvent& ev);
  void mixerTouch(const TouchEvent& ev);
  void drawMixer(LGFX_Sprite& s, int y0);
  void drawFader(LGFX_Sprite& s, int x, int y, int value, int max, uint16_t fill);
  bool hitStrip(int x, int y, int& strip, Part& part) const;
  void setVol(int track, int v);
  void setMasterVol(int v);  // device setting: App saves it to NVS, the project is not marked dirty
  void toggleMuteSolo(int track, bool solo);
  uint32_t mixSignature() const;
  bool mixer_ = false;
  int mixSel_ = 0;  // 0..7 = strip of the half (follows curTrack), kMaster
  uint32_t mixSig_ = 0;

  App& app_;
  Param params_[kRows];
  uint32_t patLenSeq_ = 0;  // App::editSeq() right after the last Pat len edit: one undo snapshot per run
  ParamList list_{kAreaY + kHeaderH};
  int y0_ = kAreaY;
  int namePos_ = 0;
};

}  // namespace ui
