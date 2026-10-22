#pragma once
#include "export.h"
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "param_list.h"
#include "render.h"
#include "theme.h"
#include "touch.h"

namespace ui {

class App;

// FILE -> Export...: Source (PATTERN 01..16, SONG when the song has slots), Loops (1 2 4 8 16, PATTERN
// only), Max slots (SONG only: the render follows the follow-action path from slot 0 (project seed) and
// stops after the last slot or after this many slot passes), Tail (AUTO / OFF / 2 s / 4 s / 8 s / WRAP, WRAP
// for PATTERN only), Tracks (ALL, SOLOED when a solo is on: what the master holds), Stems (OFF / TRACKS /
// TRACKS+FX; a SONG stem: the tracks of the slots on the path), Format (24 / 16 BIT), EXPORT / CANCEL.
// Renders the internal synth offline into /export/<project>/<src>/ (storage::exportSet): the master and
// the dry stems of one length, asking before it overwrites the folder. The header shows the folder and an
// estimate (size x files, length, passes, MIDI tracks skipped). Fills the work area.
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
  enum Row : int { kSource, kLoops, kSlots, kTail, kTracks, kStems, kFormat, kExport, kCancel, kRows };
  enum MenuId : int { kMenuCancel, kMenuOverwrite };
  static constexpr int kHeaderH = 44;

  bool songOk() const;
  bool isSong() const { return source_ >= mt::kPatterns; }
  mt::ExportSpec spec() const;
  bool action(int row);  // EXPORT / CANCEL rows: runs it, true when handled
  void confirm();        // EXPORT: checks, then asks before overwriting
  void run();
  void estimate(char* out, int n) const;

  App& app_;
  Param params_[kRows];
  ParamList list_{kAreaY + kHeaderH};
  int source_ = 0;  // 0..kPatterns-1 = pattern, kPatterns = SONG
  int loops_ = 0;   // index into 1 2 4 8 16
  int maxSlots_ = mt::kSongMax;  // kept across open() until reboot
  mt::ExportTail tail_ = mt::ExportTail::Auto;
  bool soloed_ = false;
  mt::ExportStems stems_ = mt::ExportStems::Off;
  bool bits16_ = false;
  bool open_ = false;
};

}  // namespace ui
