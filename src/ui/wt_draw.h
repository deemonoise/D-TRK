#pragma once
#include <functional>
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "keyboard.h"
#include "model.h"
#include "theme.h"
#include "touch.h"
#include "wt_make.h"

namespace ui {

class App;

// Wavetable picker -> DRAW...: keyframes drawn on a canvas, the frames between them linear
// (mt::wtFromKeys). Starts from frames 0 and 63 of a table (audio::wtFrame). No sound here: OK asks for
// a name, writes /wavetables/<name>.wav on the synth board's card (Overwrite? if it exists) and imports
// it (wtImportFile); onDone gets the project name.
// Touch: draw on the canvas (lines between the points of a stroke); ruler: tap a key mark = select,
// tap elsewhere = show that frame, drag a mark = move the key; buttons below.
// Encoder: focus Ruler / Canvas / buttons (Shift + turn); Ruler: turn = key, A + turn = move it,
// click = to the canvas; Canvas: turn = cursor, click = value edit on / off (turn = value, an 8-point
// brush), A + turn = value; buttons: turn = button, click = press. Long press = CANCEL.
class WtDraw {
 public:
  explicit WtDraw(App& app) : app_(app) {}
  ~WtDraw() { close(); }
  // source: table name (built-in or the project's), "" = a sine.
  bool open(const char* source, std::function<void(const char*)> onDone);
  void close();
  bool isOpen() const { return keys_ != nullptr; }
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y0);

 private:
  enum Btn : int { kSin, kTri, kSaw, kSqr, kSmth, kKey, kDel, kOk, kCancel, kBtns };
  enum Focus : int { kFRuler, kFCanvas, kFBtn };
  static constexpr int kCanX = 48, kCanW = 384, kCanTop = 26, kCanH = 150;
  static constexpr int kRulTop = kCanTop + kCanH + 8, kRulH = 22, kRulStep = kCanW / mt::kWtFrames;
  static constexpr int kBtnH = 26, kBtnW = kScreenW / kBtns;

  int keyAt(int frame) const;   // key index at frame, -1
  float* editPts();             // the shown frame as a key (made from the interpolation), nullptr if full
  void selectKey(int i);
  void moveKey(int i, int frame);
  bool addKey(int frame);       // interpolated key at frame, selected; false: full / taken
  void press(int b);
  void setPoint(int pt, float v);
  void brush(int d);
  void ok();
  void save(const char* name);  // build, write, import
  void cancel();

  App& app_;
  std::function<void(const char*)> onDone_;
  mt::WtKey* keys_ = nullptr;  // PSRAM, kWtKeysMax, while open
  int n_ = 0, sel_ = 0, pos_ = 0, cur_ = 0;
  int focus_ = kFRuler, btn_ = kOk;
  bool editing_ = false, changed_ = false;
  // The stroke being drawn: gesture id, last point and value.
  uint16_t strokeId_ = 0xFFFF;
  int lastPt_ = -1;
  float lastV_ = 0;
  int dragKey_ = -1;  // ruler drag: key being moved
  uint16_t dragId_ = 0xFFFF;
  char base_[mt::kSampleNameMax + 1] = {0};
  char path_[64] = {0};
  Keyboard kb_;
  int y0_ = kAreaY;
};

}  // namespace ui
