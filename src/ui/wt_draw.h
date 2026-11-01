#pragma once
#include <functional>
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "keyboard.h"
#include "model.h"
#include "theme.h"
#include "touch.h"
#include "wt_editor.h"

namespace ui {
class App;
// Full-resolution, PSRAM-backed WT document. A raw canvas gesture is one Undo.
class WtDraw {
 public:
  explicit WtDraw(App& app) : app_(app) {}
  ~WtDraw() { close(); }
  bool open(const char* source, std::function<void(const char*)> onDone, bool morph = false);
  void close();
  bool isOpen() const { return storage_ != nullptr; }
  bool onPlay();
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y0);
 private:
  enum Action { Mode, Form, Smooth, Undo, Frames, Play, Save, Cancel, Actions };
  static constexpr int kCanX=16, kCanW=448, kCanTop=60, kCanH=112;
  static constexpr int kStripTop=178, kStripH=40, kFooterTop=228;
  void press(int action);
  void frameMenu();
  void select(int frame);
  void stop();
  void point(int x, int y);
  void brush(int delta);
  void ok();
  void save(const char* name);
  void cancel();
  App& app_;
  std::function<void(const char*)> onDone_;
  int16_t* storage_ = nullptr;
  mt::WtEditor doc_;
  int cur_=128, action_=Form, lastPt_=0, copyFrom_=-1;
  int16_t lastV_=0;
  bool editing_=false, playing_=false, stroke_=false, tapMoved_=false;
  uint16_t gesture_=0xFFFF;
  uint32_t playUntil_=0;
  int tapX_=0,tapY_=0;
  char base_[mt::kSampleNameMax+1]={0};
  char path_[64]={0};
  Keyboard kb_;
  int y0_=kAreaY;
};
} // namespace ui
