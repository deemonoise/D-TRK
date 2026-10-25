#pragma once
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "keyboard.h"
#include "model.h"
#include "param_list.h"
#include "touch.h"

namespace ui {

class App;

// INST -> PRESET (SAMPLE: the SAMPLE menu) -> Save one-shot...: one note of the instrument rendered on
// the synth board into a project sample (storage::bounce): through the insert of the current track
// when it plays this instrument (as PREVIEW), else dry; no sends, no master; until silence after the
// note-off or 10 s; trimmed, normalized to -0.3 dBFS. Rows Note, Hold (0.05..5 s), Vel, Name
// (keyboard; "<instrument>_N", the first free N), SAVE, Cancel; fills the work area. A playing
// transport is stopped first ("STOPPED"). A long encoder press cancels (also during the render).
class BounceDialog {
 public:
  explicit BounceDialog(App& app);
  // instr: the instrument; note: the default note (the preview's); insTrack: the insert (kTracks: dry).
  void open(int instr, uint8_t note, uint8_t insTrack);
  bool isOpen() const { return open_; }
  void cancel();
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y0);

 private:
  enum Row : int { kNote, kHold, kVel, kName, kSave, kCancel, kRows };
  static constexpr int kHeaderH = 28;
  static constexpr int kBtnY = 2, kBtnH = 20;
  static constexpr int kCancelX = 320, kCancelW = 72;
  static constexpr int kOkX = 400, kOkW = 64;
  static constexpr int kHoldMin = 1, kHoldMax = 100;  // x 0.05 s

  void save();  // checks, then Overwrite? or run()
  void run();
  bool action(int row);
  void drawButton(LGFX_Sprite& s, int x, int y, int w, const char* label, bool sel);

  App& app_;
  Param params_[kRows];
  ParamList list_{kAreaY + kHeaderH};
  Keyboard kb_;
  int instr_ = 0;
  uint8_t note_ = 60, vel_ = 127, ins_ = mt::kTracks;
  int hold_ = 10;  // x 0.05 s, kept between opens
  char name_[mt::kSampleNameMax + 1] = {0};
  int y0_ = kAreaY;
  bool open_ = false;
};

}  // namespace ui
