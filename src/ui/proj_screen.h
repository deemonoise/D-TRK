#pragma once
#include "dice.h"
#include "hw/sdcard.h"
#include "model.h"
#include "page_bar.h"
#include "param_list.h"
#include "screen.h"

namespace ui {

// Project settings on pages SONG, DLY (send delay), RVB (reverb, shimmer), MOD (the MOD send bus),
// MSTR (compressor, sidechain, master EQ), PERF (the effect and start value of each track button in
// PERF mode);
// length / resolution / swing apply to the heard pattern. Device settings and diagnostics are on
// FILE > SYSTEM.
// Header DICE (tap, or a long encoder press): Roll all / Tempo + scale / Sounds / Pattern /
// Sounds + pattern / Undo dice (mt::diceDraft on the heard pattern outside the project lock, then
// mt::diceApply under it; one undo step, PSRAM).
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

 private:
  enum Row : int {
    kBpm, kRoot, kScale, kTuning, kTuneRoot, kSeed, kLength, kRes, kSwing, kGroove,
    kDlyTime, kDlyMode, kDlyFb, kDlyTone, kDlyChar, kDlyLevel,
    kRvbSize, kRvbMode, kRvbDamp, kRvbShim, kShimInt, kRvbLevel,
    kModType, kModRate, kModDepth, kModFb, kModLevel,
    kCompAmt, kCompRel, kScTrack, kScDepth, kEqLow, kEqHigh, kPerf1,
    kPerfLast = kPerf1 + 2 * mt::kPerfButtons - 1,  // Button N, then its start Value
    kPerfX, kPerfY,  // the XY pad's axes (PERF, A + B)
    kRows
  };
  // Pages: contiguous runs of rows.
  enum Page : int { kPgSong, kPgDly, kPgRvb, kPgMod, kPgMstr, kPgPerf, kPages };
  static constexpr int kPageFirst[kPages + 1] = {kBpm, kDlyTime, kRvbSize, kModType, kCompAmt, kPerf1, kRows};
  char perfLabels_[8][10] = {};  // "Button 1".. (Param labels must outlive the list)
  static constexpr int kHeaderH = 28;
  static constexpr int kListRows = 9;  // (kAreaH - kHeaderH - PageBar::kH) / ParamList::kRowH
  static constexpr uint32_t kBpmSettleMs = 300;

  mt::Pattern& pat();
  const mt::Project& P();
  void snapPattern();
  void showPage(int page, bool bar);  // bar: the page bar keeps the cursor, else the first row
  int bpm();  // local target while it is ahead of the engine, else p.bpm
  int bpmFrom(int cur);  // bpm() from the project BPM read by the caller
  void editBpm(int delta);

  static constexpr int kDiceX0 = 384, kDiceX1 = 472;  // header DICE button
  void diceMenu();
  void roll(uint8_t layers);
  void undoDice();
  static void evoClearTracks(uint16_t mask);  // holding lockProject()
  void afterDice(uint8_t layers);  // engine messages for what a roll / its undo wrote
  mt::DiceSnap* dice_ = nullptr;   // PSRAM, on the first roll; nullptr = never rolled

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
