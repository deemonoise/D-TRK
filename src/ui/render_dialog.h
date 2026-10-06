#pragma once
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "param_list.h"
#include "render.h"
#include "theme.h"
#include "touch.h"

namespace ui {

class App;

// FILE -> Render WAV: Source (PATTERN 01..16, SONG when the chain has rows), Tracks (ALL / SOLOED
// when a solo is on), RENDER / CANCEL. Renders the internal synth offline into
// /samples/render/<project>_P01.wav (or _SONG.wav) with a 2 s tail (storage::renderWav), asking before
// it overwrites. Fills the work area.
class RenderDialog {
 public:
  explicit RenderDialog(App& app);
  void open();
  void close() { open_ = false; }
  bool isOpen() const { return open_; }
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y0);

 private:
  enum Row : int { kSource, kTracks, kRender, kCancel, kRows };
  enum MenuId : int { kMenuCancel, kMenuOverwrite };
  static constexpr int kHeaderH = 28;
  static constexpr uint32_t kTailBlocks = 500;  // 2 s

  bool songOk() const;
  bool action(int row);  // RENDER / CANCEL rows: runs it, true when handled
  void confirm();        // RENDER: checks, then asks before overwriting
  void run();

  App& app_;
  Param params_[kRows];
  ParamList list_{kAreaY + kHeaderH};
  int source_ = 0;      // 0..kPatterns-1 = pattern, kPatterns = SONG
  bool soloed_ = false;  // Tracks: SOLOED
  bool open_ = false;
};

}  // namespace ui
