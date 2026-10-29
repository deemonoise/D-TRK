#pragma once
#include "dialog_actions.h"
#include <functional>
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
// The sample is also copied to /samples/oneshots/<folder of the instrument>/ on the card ("SAVED
// name"; "SAVED, NO COPY" when the copy failed).
// KIT mode (openKit): rows Hold, Vel, SAVE, Cancel. SAVE bakes every lane that plays another
// instrument (mt::kitBakeLanes): the KIT rendered dry (the baked lane plays through the track's insert
// again) and not normalized (the kit's balance stays) at the lane's note into "<lane instrument>_N"
// (the folder: the lane instrument's), then the lane becomes a sampler on it (mt::kitLaneToSample).
// "LANE i/n" progress; a SILENT lane is skipped; cancel / an error stops (baked lanes stay).
class BounceDialog {
 public:
  explicit BounceDialog(App& app);
  // instr: the instrument; note: the default note (the preview's); insTrack: the insert (kTracks: dry).
  void open(int instr, uint8_t note, uint8_t insTrack);
  // KIT bake of instruments[kit]; onBaked after any lane changed (the KIT screen rebuilds its rows).
  void openKit(int kit, std::function<void()> onBaked);
  bool isOpen() const { return open_; }
  void cancel();
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y0);

 private:
  enum Row : int { kNote, kHold, kVel, kName, kSave, kCancel, kRows };
  static constexpr int kKitRows = 4;  // KIT mode: Hold, Vel, SAVE, Cancel
  static constexpr int kHeaderH = 28;
  static constexpr int kHoldMin = 1, kHoldMax = 100;  // x 0.05 s

  void save();  // checks, then Overwrite? or run()
  void run();
  void runKit();
  int rowId(int listRow) const;  // a list row as a Row (KIT mode shows a subset)
  bool action(int row);
  static bool progress(uint32_t done, uint32_t total, void* ctx);

  DialogActions actions_;
  App& app_;
  Param params_[kRows];
  Param kitParams_[kKitRows];
  ParamList list_{kAreaY + kHeaderH};
  Keyboard kb_;
  int instr_ = 0;
  uint8_t note_ = 60, vel_ = 127, ins_ = mt::kTracks;
  int hold_ = 10;  // x 0.05 s, kept between opens
  char name_[mt::kSampleNameMax + 1] = {0};
  int y0_ = kAreaY;
  bool open_ = false;
  bool kit_ = false;
  char progLabel_[16] = "SHOT";
  std::function<void()> onBaked_;
};

}  // namespace ui
