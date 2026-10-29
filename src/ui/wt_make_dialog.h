#pragma once
#include "dialog_actions.h"
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "keyboard.h"
#include "model.h"
#include "param_list.h"
#include "touch.h"
#include "wt_make.h"

namespace ui {

class App;

// SAMPLE -> To wavetable...: the Start..End region of a sample into a new project wavetable, made on
// the synth board (audio::wtRender) as /wavetables/<name>.wav and imported (wtImportFile). Rows Mode
// (CYCLE / WINDOW), Norm (FRAME / TABLE), Seam (ON / OFF), Name (keyboard), MAKE, Cancel; fills the
// work area. Mode / Norm / Seam are kept between opens. A long encoder press cancels.
class WtMakeDialog {
 public:
  explicit WtMakeDialog(App& app);
  // Project sample index, its region [from, to) at rate; name = the default table name.
  void open(int index, uint32_t from, uint32_t to, uint32_t rate, const char* name);
  bool isOpen() const { return open_; }
  void cancel();
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y0);

 private:
  enum Row : int { kMode, kNorm, kSeam, kName, kMake, kCancel, kRows };
  static constexpr int kHeaderH = 28;

  void make();  // checks, then Overwrite? or run()
  void run();
  bool action(int row);

  DialogActions actions_;
  App& app_;
  Param params_[kRows];
  ParamList list_{kAreaY + kHeaderH};
  Keyboard kb_;
  mt::WtMakeParams p_;
  int index_ = 0;
  uint32_t from_ = 0, to_ = 0, rate_ = 0;
  char name_[mt::kSampleNameMax + 1] = {0};
  char path_[64] = {0};
  int y0_ = kAreaY;
  bool open_ = false;
};

}  // namespace ui
