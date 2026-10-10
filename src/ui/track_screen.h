#pragma once
#include "model.h"
#include "page_bar.h"
#include "param_list.h"
#include "screen.h"

namespace ui {

// Settings of App::curTrack(). MIDI-only rows are grey on an INT track. Shift+turn (or a tap on the header arrows) = track -+1.
// Name: click to edit, turn = character, Shift+turn = position.
// MIXER view (the MIX tab, setMixer): 8 strips of the half holding the cursor (A = 1-8, B = 9-16, as
// GRID's Overview), the master strip MAIN and the output scope below. Strip: name, volume fader
// (INT), delay / reverb send of its instrument (read only), M / S. Turn = master volume, Shift + turn
// = the other half, A + turn = master volume (Shift = x10); a track button held + turn = that track's
// volume, Shift + track button = mute (App, every screen). Touch: the fader follows the finger, M / S
// toggle (solo only here).
class TrackScreen : public Screen {
 public:
  explicit TrackScreen(App& app);
  void onEnter() override;
  void onLeave() override;
  void onInput(const hw::InputEvent& ev) override;
  void onTouch(const TouchEvent& ev) override;
  void onPage(int d) override;
  void onBack() override;
  void draw(LGFX_Sprite& s, int y0, int h) override;
  bool wantsRedraw(const engine::Status& st) override;
  // MIX tab (true) or TRACK tab (false): the mixer view or the track settings.
  void setMixer(bool on);

 private:
  // Rows in page order: MAIN, NOTE, MIDI (contiguous runs, see kPageFirst).
  enum Row : int { kName, kOut, kInstr, kVol, kMute, kSolo, kVel, kGate, kPatLen, kSpeed, kHumanize, kChannel, kCcA, kCcB, kProgram, kRows };
  enum Page : int { kPgMain, kPgNote, kPgMidi, kPages };
  static constexpr int kPageFirst[kPages + 1] = {kName, kVel, kChannel, kRows};
  static constexpr int kVisibleRows = 9;  // (kAreaH - kHeaderH - PageBar::kH) / ParamList::kRowH
  static constexpr int kHeaderH = 28;
  static constexpr int kArrowW = 96;  // header hit area on each side
  static constexpr int kNameLen = 8;

  mt::TrackCfg& cfg();
  bool internal() { return cfg().out == mt::TrackOut::Int; }
  void changeTrack(int d);
  void leaveEdit();
  void fixNames();  // empty name -> TRKn
  bool nameEdit() const { return page_ == kPgMain && list_.editing() && list_.sel() == kName; }
  void showPage(int page, bool bar);  // bar: the page bar keeps the cursor, else the first row
  void setPageRows(int page, bool bar);  // the list only: safe in the constructor
  int page_ = kPgMain;
  void editName(int delta);  // under lock
  // MIXER: 8 + 8 x 52 + 8 + 48 = 480.
  static constexpr int kStrips = 8, kStripW = 52, kStripX0 = 8;
  static constexpr int kMasterW = 48, kMasterX = kStripX0 + kStrips * kStripW + 8;  // 432
  static constexpr int kMaster = kStrips;  // hitStrip: the master strip
  static constexpr int kNameY = 2, kFaderY = 16, kFaderH = 64, kFaderW = 12, kValY = 84;
  static constexpr int kSendY = 100, kSendDy = 16, kBtnY = 134, kBtnW = 22, kBtnH = 16;
  // Scope under the strips: the waveform (auto gain) and a peak meter with CLIP.
  static constexpr int kScopeY = kBtnY + kBtnH + 6, kScopeH = kAreaH - kScopeY - 4, kMeterW = 10;
  static constexpr uint32_t kScopeMs = 50;  // ~20 frames a second while the MIX tab is shown
  void drawScope(LGFX_Sprite& s, int y0);
  uint32_t scopeMs_ = 0, clipMs_ = 0;
  int meter_ = 0;
  float scopeGain_ = 1;  // auto gain, follows the waveform's peak
  // Track level meters beside the faders: -48..0 dB as 0..1, fast up, falling ~1.2 per second.
  static constexpr int kMeterBarW = 5;
  void updateMeters();
  float level_[mt::kTracks] = {};
  uint32_t clipAt_[mt::kTracks] = {};  // millis() | 1 of the last full-scale peak, held 1 s
  enum class Part : uint8_t { Name, Fader, Mute, Solo };
  int firstTrack() const;
  void mixerInput(const hw::InputEvent& ev);
  void mixerTouch(const TouchEvent& ev);
  void drawMixer(LGFX_Sprite& s, int y0);
  void drawFader(LGFX_Sprite& s, int x, int y, int value, int max, uint16_t fill);
  bool hitStrip(int x, int y, int& strip, Part& part) const;
  void setVol(int track, int v);
  void setMasterVol(int v);  // project data: saved with it, in Render WAV
  void toggleMuteSolo(int track, bool solo);
  uint32_t mixSignature() const;
  bool mixer_ = false;
  uint32_t mixSig_ = 0;

  App& app_;
  Param params_[kRows];
  int editTrack_ = 0;       // the track the list shows (followTrack)
  void followTrack();
  int patLenPat_ = -1;      // the pattern of that run
  uint32_t patLenSeq_ = 0;  // App::editSeq() right after the last Pat len edit: one undo snapshot per run
  int speedPat_ = -1;       // the same for Speed
  uint32_t speedSeq_ = 0;
  ParamList list_{kAreaY + kHeaderH + PageBar::kH};
  int y0_ = kAreaY;
  int namePos_ = 0;
};

}  // namespace ui
