#pragma once
#include "dice.h"
#include "dice_dialog.h"
#include "hw/sdcard.h"
#include "model.h"
#include "page_bar.h"
#include "proj_pages.h"
#include "param_list.h"
#include "screen.h"

namespace ui {

// Project settings on pages MAIN, PAT, DLY (send delay), RVB (reverb, shimmer), MOD (the MOD send bus),
// MSTR (compressor, sidechain, master EQ), PERF (the effect and start value of each track button in
// PERF mode);
// length / resolution / swing apply to the heard pattern. Device settings and diagnostics are on
// FILE > SYSTEM.
// Header DICE opens the genre/style/form dialog. Generation runs outside the project lock,
// then the bounded draft is swapped in under it. One PSRAM snapshot preserves Undo dice.
class ProjScreen : public Screen {
 public:
  explicit ProjScreen(App& app);
  void onEnter() override;
  void onProjectReplaced() override;
  void onInput(const hw::InputEvent& ev) override;
  void onTouch(const TouchEvent& ev) override;
  void onPage(int d) override;
  void onBack() override;
  void draw(LGFX_Sprite& s, int y0, int h) override;
  void loadDicePrefs() { diceDlg_.loadPrefs(); }
  void saveDicePrefs() { diceDlg_.savePrefs(); }
  bool relocateDiceUndo();  // UI-thread: preserve Undo while coalescing a project-load staging block

 private:
  enum Row : int {
    kBpm, kRoot, kScale, kTuning, kTuneRoot, kSeed, kLength, kRes, kSwing, kGroove,
    kDlyTime, kDlyMode, kDlyFb, kDlyTone, kDlyChar, kDlyLevel,
    kRvbSize, kRvbMode, kRvbDamp, kRvbShim, kShimInt, kRvbLevel,
    kModType, kModRate, kModDepth, kModFb, kModLevel,
    kCompAmt, kCompAtk, kCompRel, kScTrack, kScDepth, kEqLow, kEqHigh, kPerf1,
    kPerfLast = kPerf1 + 2 * mt::kPerfButtons - 1,  // Button N, then its start Value
    kPerfX, kPerfY,  // the XY pad's axes (PERF, A + B)
    kRows
  };
  // Pages: contiguous runs of rows.
  // Logical IDs stay stable; PAT is displayed between MAIN and DLY.
  enum Page : int { kPgSong=mt::kProjectMain, kPgDly=mt::kProjectDelay, kPgRvb=mt::kProjectReverb,
    kPgMod=mt::kProjectMod, kPgMstr=mt::kProjectMaster, kPgPerf=mt::kProjectPerf,
    kPgPattern=mt::kProjectPattern, kPages=mt::kProjectPageCount };
  int physicalPage() const;
  int firstRow(int page) const;
  int rowCount(int page) const;
  void stepPage(int delta, bool bar);
  static constexpr int kPageFirst[7] = {kBpm, kDlyTime, kRvbSize, kModType, kCompAmt, kPerf1, kRows};
  char perfLabels_[8][10] = {};  // "Button 1".. (Param labels must outlive the list)
  static constexpr int kHeaderH = 28;
  static constexpr int kListRows = (kAreaH - kHeaderH - PageBar::kH) / ParamList::kRowH;
  static constexpr uint32_t kBpmSettleMs = 300;

  mt::Pattern& pat();
  const mt::Project& P();
  void snapPattern();
  void showPage(int page, bool bar);  // bar: the page bar keeps the cursor, else the first row
  int bpm();  // local target while it is ahead of the engine, else p.bpm
  int bpmFrom(int cur);  // bpm() from the project BPM read by the caller
  void editBpm(int delta);

  static constexpr int kDiceX0 = 384, kDiceX1 = 472;  // header DICE button
  void roll(const mt::DiceOpts& opts);
  void undoDice();
  static void evoClearTracks(uint16_t mask);  // holding lockProject()
  void afterDice(uint8_t layers);  // engine messages for what a roll / its undo wrote
  DiceDialog diceDlg_;
  mt::DiceSnap* dice_ = nullptr;  // one PSRAM undo snapshot, released on project replacement

  // Tuning row: 0 = 12-TET, then the factory tunings, then the card's /presets/TUN files (listed at
  // onEnter). An edit (under the project lock) only picks; draw() loads and applies the pick, outside it.
  static constexpr int kTunUserMax = 64;
  int tunCount() const;
  const char* tunName(int i) const;
  int tunCurrent() const;  // the project's tuning in the list, -1 if it is not there (file gone)
  void applyTuning();
  char (*tunUser_)[hw::kNameMax] = nullptr;
  int tunUserN_ = 0;
  int tunPick_ = -1;  // pending pick, -1 = none

  App& app_;
  Param params_[kRows];
  ParamList list_{kAreaY + kHeaderH + PageBar::kH};
  int bpmTarget_ = 120;
  uint32_t bpmPostMs_ = 0;
  uint32_t patSeq_ = 0;  // App::editSeq() right after the last pattern-field edit (snapPattern)
  int patIdx_ = -1;      // and its pattern
  int page_ = kPgSong;
};

}  // namespace ui
