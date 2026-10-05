#pragma once
#include "model.h"
#include "param_list.h"
#include "screen.h"

namespace ui {

// Project settings; length / resolution / swing apply to the heard pattern.
class ProjScreen : public Screen {
 public:
  explicit ProjScreen(App& app);
  void onEnter() override;
  void onInput(const hw::InputEvent& ev) override;
  void onTouch(const TouchEvent& ev) override;
  void draw(LGFX_Sprite& s, int y0, int h) override;

 private:
  enum Row : int { kBpm, kRoot, kScale, kLength, kRes, kSwing, kVolume, kPreview, kRows };
  static constexpr int kHeaderH = 28;
  static constexpr uint32_t kBpmSettleMs = 300;

  mt::Pattern& pat();
  int bpm();  // local target while it is ahead of the engine, else p.bpm
  void editBpm(int delta);

  App& app_;
  Param params_[kRows];
  ParamList list_{kAreaY + kHeaderH};
  int bpmTarget_ = 120;
  uint32_t bpmPostMs_ = 0;
};

}  // namespace ui
