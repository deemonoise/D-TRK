#pragma once
#include "edit_ops.h"
#include "euclid.h"
#include "euclid_dialog.h"
#include "model.h"
#include "screen.h"

namespace ui {

class GridScreen : public Screen {
 public:
  explicit GridScreen(App& app) : app_(app) {}
  void onEnter() override;
  void onLeave() override;
  void onProjectReplaced() override;
  void onPatternChange() override;
  void onInput(const hw::InputEvent& ev) override;
  void onTouch(const TouchEvent& ev) override;
  void draw(LGFX_Sprite& s, int y0, int h) override;
  bool wantsRedraw(const engine::Status& st) override;

 private:
  enum Field : uint8_t { kNote, kVel, kFx1, kVal1, kFx2, kVal2, kFields };
  // Context menu ids.
  enum MenuId : int {
    kCopyStep, kPaste, kClearStep, kCopyTrack, kClearTrack, kTrUp1, kTrDn1, kTrUp12, kTrDn12,
    kToggleView, kToggleFollow, kUndo, kCopySel, kClearSel, kDropSel, kNoteOff, kNoteOffSel, kEuclid
  };

  static constexpr int kNamesH = 16;
  static constexpr int kRowH = 16;
  static constexpr int kNumW = 32;
  static constexpr int kColW = 56;     // Overview track column
  static constexpr int kDetW = 224;    // Detail track column
  static constexpr int kFieldW = 36;   // Detail field pitch
  static constexpr int kFieldX = 4;    // first field offset inside a Detail column
  static constexpr int kKbH = 48;      // mini keyboard height
  static constexpr int kKeyW = 40;
  static constexpr uint32_t kFollowPauseMs = 2000;

  mt::Pattern& pat();
  int len() const;  // pattern length clamped to kMinSteps..kMaxSteps
  int cur();  // curStep_ clamped to the pattern length
  int track() const;
  int rowsFor(int h) const;
  void cursorMoved();
  void moveStep(int d);
  void moveTrack(int d);
  void moveField(int d);
  void setEdit(bool on);
  void toggleView();
  void undo();
  void editTurn(int delta, bool shift);
  void setNote(uint8_t note);
  void writeStep(const mt::Step& st);
  void openMenu();
  void openEuclid();
  void onMenu(int id);
  void apply(const mt::Sel& sel, int id);  // clear / transpose under pushUndo + lock
  mt::Sel trackSel() const;
  mt::Sel curSel() const;
  bool hit(int x, int y, int& step, int& tr, int& field) const;
  bool keyboardShown() const { return edit_ && curField_ == kNote; }
  void drawOverview(LGFX_Sprite& s, int gridY);
  void drawDetail(LGFX_Sprite& s, int gridY);
  void drawKeyboard(LGFX_Sprite& s, int y);
  bool selected(int tr, int step) const;

  App& app_;
  int y0_ = kAreaY;
  int h_ = kAreaH;
  int rows_ = (kAreaH - kNamesH) / kRowH;
  int curStep_ = 0;
  int curField_ = kNote;
  int top_ = 0;
  bool detail_ = false;
  bool edit_ = false;
  bool editPushed_ = false;    // undo snapshot taken in this edit session
  bool needVisible_ = true;    // scroll to the cursor on the next draw
  bool follow_ = true;
  bool dragFrozen_ = false;    // follow off after a Drag until the next Play
  bool wasPlaying_ = false;
  uint32_t lastMoveMs_ = 0;
  uint8_t lastNote_[mt::kTracks] = {60, 60, 60, 60, 60, 60, 60, 60};

  EuclidDialog euclid_{app_};
  mt::EuclidParams euclidParams_[mt::kTracks];  // per track, RAM only
  bool euclidInit_[mt::kTracks] = {};

  int menuPattern_ = -1;  // pattern the context menu was opened for
  bool selOn_ = false;
  int selT0_ = 0, selS0_ = 0, selT1_ = 0, selS1_ = 0;  // anchor and end, not normalized
};

}  // namespace ui
