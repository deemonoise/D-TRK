#pragma once
#include "euclid.h"
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "model.h"
#include "param_list.h"
#include "touch.h"

namespace ui {

class App;

// Euclidean rhythm generator over GRID, fills the work area. Live preview: every change restores
// the track as it was at open() and applies the parameters again. OK keeps the result (one undo
// step); Cancel, a long encoder press or leaving GRID restores the track and drops the undo step.
// OK / CANCEL: header buttons (touch) and the last rows (encoder). Shift+click or the Reseed row =
// new random seed. Edits the pattern captured at open(), even if the heard one changes.
class EuclidDialog {
 public:
  explicit EuclidDialog(App& app);
  // pattern must be App::editPattern() (pushUndo() snapshots it). e is edited in place.
  void open(int pattern, int track, mt::EuclidParams* e);
  bool isOpen() const { return open_; }
  void ok();
  void cancel();
  // Project replaced under the dialog: close without touching the (new) data.
  void abandon() { open_ = false; }
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y0);

 private:
  enum Row : int {
    kHits, kLength, kRotation, kFill, kBase, kRange, kVel, kAccEvery, kAccVel, kSeed, kReseed, kMode,
    kOk, kCancel, kRows
  };
  static constexpr int kHeaderH = 28;
  // Header buttons.
  static constexpr int kBtnY = 2, kBtnH = 20;
  static constexpr int kCancelX = 320, kCancelW = 72;
  static constexpr int kOkX = 400, kOkW = 64;

  int patLen() const;
  void clampParams();
  void preview();
  void reseed();
  bool action(int row);  // OK / Cancel / Reseed row: runs it, true when handled
  void drawButton(LGFX_Sprite& s, int x, int y, int w, const char* label, bool sel);

  App& app_;
  Param params_[kRows];
  ParamList list_{kAreaY + kHeaderH};
  mt::Step saved_[mt::kMaxSteps];  // the track at open()
  mt::EuclidParams* e_ = nullptr;
  int pattern_ = 0;
  int track_ = 0;
  uint32_t seqAtOpen_ = 0;  // App::editSeq() before the first preview
  uint32_t previews_ = 0;   // markDirty() calls made by this dialog
  int y0_ = kAreaY;
  bool open_ = false;
};

}  // namespace ui
