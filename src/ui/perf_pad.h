#pragma once
#include <stdint.h>
#include "hw/lgfx_config.h"
#include "perf_fx.h"

namespace ui {

class App;

// PERF: punch-in effects on the master, held on the track buttons while playing. One mode shared
// by GRID, SONG and MIX (App owns it): the on flag, the held effects as the synth has them and the
// ring buffer, held only while PERF is on and a PERF tab (attach()) is shown.
class PerfPad {
 public:
  explicit PerfPad(App& app) : app_(app) {
    for (int8_t& h : held_) h = -1;
  }
  bool on() const { return on_; }
  void setOn(bool on);  // off releases everything and frees the buffer
  // A PERF tab is shown / left: the buffer is taken (PERF on) / freed, leaving releases everything.
  void attach();
  void detach();
  // Button n (0-7) pressed: its PROJ -> PERF effect at its start Value, with a toast. False (not
  // handled) when PERF is off or the transport is not playing.
  bool press(int n);
  void release(int n);  // the effect button n held ends (unless another button holds it too)
  // Encoder: the last pressed effect's depth (Shift x10 on coarse ranges), with a toast. False
  // (not consumed) when nothing is held.
  bool turn(int delta, bool shift);
  void allOff();  // every held effect ends; release all is sent even with nothing held
  // Every UI tick: a stop or an audio park releases the held effects; a lost message is resent.
  void poll(bool playing);
  int heldCount() const { return stack_.count(); }
  const mt::PerfStack& stack() const { return stack_; }  // the held effects and depths (PerfVis)
  // "PERF RPT FLT" (held shorts in press order) into out, as many as fit; "" when off. Its length.
  int badge(char* out, int size) const;
  // The badge in yellow at x, y; its right edge (x when off).
  int drawBadge(LGFX_Sprite& s, int x, int y) const;

 private:
  void takeBuffer();  // a toast when there is none
  void freeBuffer();

  App& app_;
  bool on_ = false;
  bool attached_ = false;  // a PERF tab is shown
  bool buffer_ = false;    // the ring buffer is held
  // The held effects and what each button holds (PerfFx, -1 = nothing). Two buttons on the same
  // effect: it ends with the last one.
  mt::PerfStack stack_;
  int8_t held_[mt::kPerfButtons];
  // A message lost in a full queue: release all is resent once nothing is held, until taken.
  bool lost_ = false;
  uint32_t parks_ = 0;  // audio::parkCount() last seen: a park resets the synth's PERF
};

}  // namespace ui
