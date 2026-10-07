#pragma once
#include "model.h"
#include "page_bar.h"
#include "param_list.h"
#include "screen.h"

namespace ui {

// Project settings on pages SONG, FX, COMP, PERF (the effect of each track button in PERF mode),
// SYS; length / resolution / swing apply to the heard
// pattern. SYS also holds the device's colour theme (not saved in the project).
class ProjScreen : public Screen {
 public:
  explicit ProjScreen(App& app);
  void onEnter() override;
  void onInput(const hw::InputEvent& ev) override;
  void onTouch(const TouchEvent& ev) override;
  void draw(LGFX_Sprite& s, int y0, int h) override;

 private:
  enum Row : int {
    kBpm, kRoot, kScale, kLength, kRes, kSwing, kGroove, kDlyTime, kDlyFb, kDlyTone, kDlyLevel,
    kRvbSize, kRvbDamp, kRvbLevel, kDjFilter, kCompAmt, kCompRel, kScTrack, kScDepth, kPerf1, kPerf8 = kPerf1 + 7, kPreview, kTheme, kAutosave, kFirmware, kLastReset, kAudioRam, kCpuProf, kRows
  };
  // Pages: contiguous runs of rows.
  enum Page : int { kPgSong, kPgFx, kPgComp, kPgPerf, kPgSys, kPages };
  static constexpr int kPageFirst[kPages + 1] = {kBpm, kDlyTime, kCompAmt, kPerf1, kPreview, kRows};
  char perfLabels_[8][10] = {};  // "Button 1".. (Param labels must outlive the list)
  static constexpr int kHeaderH = 28;
  static constexpr int kListRows = 9;  // (kAreaH - kHeaderH - PageBar::kH) / ParamList::kRowH
  static constexpr uint32_t kBpmSettleMs = 300;

  mt::Pattern& pat();
  void snapPattern();
  void showPage(int page, bool last);
  bool onProfileRow() const { return kPageFirst[page_] + list_.sel() == kCpuProf; }
  void toggleProfile();  // start, or stop and append the result to /projects/cpuprof.txt
  uint32_t profStartMs_ = 0;
  int bpm();  // local target while it is ahead of the engine, else p.bpm
  void editBpm(int delta);

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
