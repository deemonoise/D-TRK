#pragma once
#include "dialog_actions.h"
#include "keyboard.h"
#include "sample_extract.h"
#include "tempo_detect.h"

namespace ui {
class App;

// Off-line copy of Start..End or a selected slice. The original asset is retained.
class SampleExtractDialog {
 public:
  explicit SampleExtractDialog(App& app);
  void open(int instrument, int index, uint32_t from, uint32_t to, uint32_t rate);
  void openStretch(int instrument, int index, uint32_t from, uint32_t to, uint32_t rate);
  void openTempo(int instrument, int index, uint32_t from, uint32_t to, uint32_t rate);
  bool isOpen() const { return open_; }
  void cancel();
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y0);

 private:
  enum Row : int { kName, kNormalize, kRows = 6 };
  enum StretchRow : int { kSourceBpm=1, kTargetBpm, kDetect, kHalf, kDouble };
  static constexpr int kHeaderH = 28;
  void create();
  void detectTempo();
  bool rowAction(int row);
  int rowCount() const { return stretch_ ? kRows : 2; }
  void editName();
  void resultToast(mt::SampleExtractResult result);
  DialogActions actions_;
  App& app_;
  Param params_[kRows];
  ParamList list_{kAreaY + kHeaderH};
  Keyboard kb_;
  mt::SampleExtractTarget target_;
  char name_[mt::kSampleNameMax + 1] = {};
  uint32_t rate_ = 32000;
  int y0_ = kAreaY;
  bool normalize_ = false, open_ = false, stretch_ = false, detected_ = false;
  int sourceBpm10_ = 1200, targetBpm10_ = 1200;
  mt::TempoEstimate tempo_;
};
}  // namespace ui
