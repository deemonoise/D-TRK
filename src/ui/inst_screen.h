#pragma once
#include "model.h"
#include "param_list.h"
#include "screen.h"

namespace ui {

// Instrument editor. Shift+turn (or a tap on the header arrows) = instrument -+1; entering the tab
// selects the instrument of App::curTrack(). PREVIEW (tap, or a long press) plays C4 for 300 ms.
// Name: click to edit, turn = character, Shift+turn = position.
// SAMPLE: a waveform view above the list with start (green), end (red) and loop (yellow) markers.
// FM: machine, macros (DECAY..CONTOUR), LFO; ADSR rows grey where the machine ignores them.
class InstScreen : public Screen {
 public:
  explicit InstScreen(App& app);
  void onEnter() override;
  void onProjectReplaced() override { onEnter(); }
  void onInput(const hw::InputEvent& ev) override;
  void onTouch(const TouchEvent& ev) override;
  void draw(LGFX_Sprite& s, int y0, int h) override;

 private:
  // Rows of every type first, then the type's own.
  enum Row : int {
    kName, kType, kVol, kTranspose, kFine, kAttack, kDecay, kSustain, kRelease, kMode, kGlide, kCommon,
    kWave = kCommon, kDuty, kPwmRate, kPwmDepth, kChipRows,
    kSample = kCommon, kRoot, kStart, kEnd, kLoop, kLoopStart, kReverse, kSampleRows,
    kMachine = kCommon, kMacDecay, kMacColor, kMacShape, kMacSweep, kMacContour,
    kLfoWave, kLfoRate, kLfoDepth, kLfoDest, kFmRows
  };
  static constexpr int kHeaderH = 28;
  static constexpr int kVisibleRows = 10;  // (kAreaH - kHeaderH) / ParamList::kRowH
  static constexpr int kWaveH = 60;        // SAMPLE waveform view, below the header
  static constexpr int kWaveGap = 4;
  static constexpr int kSampleVisibleRows = 7;  // (kAreaH - kHeaderH - kWaveH - kWaveGap) / kRowH
  static constexpr int kNameLen = 8;
  static constexpr int kPreviewNote = 60;
  // Header hit areas: left arrow, right arrow, PREVIEW button.
  static constexpr int kLeftX1 = 80;
  static constexpr int kRightX0 = 288, kRightX1 = 360;
  static constexpr int kPrevX0 = 368;

  mt::Instrument& inst();
  void changeInstr(int d);
  void syncParams();  // rows of the current type
  void leaveEdit();
  void fixNames();  // empty name -> INSn
  bool nameEdit() const { return list_.editing() && list_.sel() == kName; }
  void preview();
  int bankIndex();      // of inst().sample, -1 if absent or no bank
  bool sampleMissing();  // a name that is not in the bank
  void updateWave();    // min/max column cache of the current sample, only when it changed
  void drawWave(LGFX_Sprite& s, int y);

  App& app_;
  Param chip_[kChipRows];
  Param sample_[kSampleRows];
  Param fm_[kFmRows];
  mt::InstrType shown_ = mt::InstrType::Chip;
  ParamList list_{kAreaY + kHeaderH};
  int instr_ = 0;
  int y0_ = kAreaY;
  int namePos_ = 0;
  // Waveform cache: per screen column min / max of the sample, scaled to int8.
  int8_t waveMin_[kScreenW] = {0};
  int8_t waveMax_[kScreenW] = {0};
  const int16_t* waveData_ = nullptr;
  uint32_t waveFrames_ = 0;
  uint32_t waveGen_ = 0;  // SampleBank::generation(): same place and length may hold new data
};

}  // namespace ui
