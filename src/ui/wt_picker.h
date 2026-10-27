#pragma once
#include <functional>
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "hw/sdcard.h"
#include "file_list.h"
#include "model.h"
#include "theme.h"
#include "touch.h"
#include "wt_draw.h"

namespace ui {

class App;

// Level 0 of a wavetable frame (mt::kWtFrameLen points >> 8, audio::wtFrame) as a polyline in
// [x0, x1) x [y0, y1].
void drawWtFrame(LGFX_Sprite& s, const int8_t* pts, int x0, int y0, int x1, int y1, uint16_t c);

// Imports the wavetable WAV at path into the bank and the project's list under a name made from
// base (sanitized, cut to 16; "-2", "-3"... when the name holds another table). out = the name used.
// Toasts and returns false on failure (also while the transport runs: STOP PLAYBACK FIRST).
bool wtImportFile(App& app, const char* path, const char* base, char out[mt::kSampleNameMax + 1]);

// SYNTH table picker over the work area (INST, OSC page), a FileList like every browser: row 0 is
// "< Back" (close, the table stays) or "< Up (..)" in a subfolder, a long press / B goes back. The
// table list holds the built-in tables ("*NAME"), the project's, DRAW... (WtDraw: keyframes from the
// oscillator's table, a new table), then IMPORT... (a browser of /wavetables and its subfolders:
// folders, *.wav; Back at its top returns to the tables, the cursor on IMPORT...). Turning / a first
// tap only moves the cursor (a file: Play / the first tap previews it); a click (a tap on the
// selected row) sets the table on the oscillator and closes. A picked file is imported (see
// wtImportFile) and set. The selected table's frame 0 is drawn right of the list. The import
// folder and its cursor are kept while powered.
class WtPicker {
 public:
  explicit WtPicker(App& app) : app_(app) {}
  void setOnClose(std::function<void()> f) { onClose_ = std::move(f); }
  void open(int instr, int osc);
  void close();
  bool isOpen() const { return open_; }
  bool drawOpen() const { return draw_.isOpen(); }  // the editor wants HDrag (strokes)
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  bool onPlay();  // WT import: previews the selected file
  void draw(LGFX_Sprite& s, int y0);

 private:
  static constexpr int kMaxEntries = 512;
  static constexpr int kHeaderH = FileList::kHeaderH;
  // Frame preview right of the table names.
  static constexpr int kPvX0 = 240, kPvX1 = 440, kPvY0 = 16, kPvY1 = 112;
  enum class Kind : uint8_t { Table, Import, Draw };  // the tables list's FileList tags

  mt::Instrument& inst();
  void listTables(const char* select);  // select: table name to put the cursor on, or nullptr
  void listFiles(const char* cursor);   // dir_, the cursor on that name
  void act(FileList::Act a);
  void choose();  // click / tap on the selected row
  void back();    // row 0, long press: up a folder, to the tables, or close
  void importFile(int i);
  void setTable(const char* name);

  App& app_;
  WtDraw draw_{app_};
  std::function<void()> onClose_;
  bool open_ = false;
  bool files_ = false;  // listing /wavetables
  int instr_ = 0, osc_ = 0;
  FileList list_{app_, kMaxEntries};  // tag = Kind
  int y0_ = kAreaY;
  char dir_[128] = "/wavetables";      // kept while powered
  char cursor_[hw::kNameMax] = {0};    // the import list's cursor when it was left
};

}  // namespace ui
