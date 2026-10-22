#pragma once
#include <stdint.h>
#include "hw/lgfx_config.h"
#include "perf_fx.h"
#include "perf_axis.h"

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
  // XY pad (A + B on GRID / SONG / MIX with PERF on): the work area is a touch pad whose axes hold
  // the effects of Project::perfXy. Off: the axis effects end (unless a button holds them too).
  bool xy() const { return xy_; }
  void setXy(bool on);
  // Axis a's effect (0 = X, 1 = Y), as mt::perfXyAxis: None when unset, out of range, or Y on X's
  // effect (X drives it) or a buffer effect on both.
  mt::PerfFx xyAxis(int a) const;
  // Finger on the pad (px from its top-left, pad w x h, y down): both axis effects at the depths
  // under it (PerfStack::setDepth: only press / turn go out). False (nothing held) when PERF or XY
  // is off or the transport is not playing. Move follows; up releases them unless a button holds
  // them. A stop meanwhile (poll / allOff) ends the touch: moves are ignored until the next down.
  bool xyDown(int x, int y, int w, int h);
  void xyMove(int x, int y, int w, int h);
  void xyUp();
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
  static constexpr int kXyHolder = mt::kPerfButtons;  // held_[kXyHolder + a]: axis a
  static constexpr int kHolders = mt::kPerfButtons + 2;
  void releaseHolder(int h);  // h's effect ends unless another holder has it
  void xyApply(int x, int y, int w, int h);
  void send(const mt::PerfMsg* m, int n);
  bool xy_ = false;
  bool xyTouch_ = false;  // a pad touch holds the axis effects

  App& app_;
  bool on_ = false;
  bool attached_ = false;  // a PERF tab is shown
  bool buffer_ = false;    // the ring buffer is held
  // The held effects and what each button and XY axis holds (PerfFx, -1 = nothing). Two holders
  // on the same effect: it ends with the last one.
  mt::PerfStack stack_;
  int8_t held_[kHolders];
  // A message lost in a full queue: release all is resent once nothing is held, until taken.
  bool lost_ = false;
  uint32_t parks_ = 0;  // audio::parkCount() last seen: a park resets the synth's PERF
};

}  // namespace ui
