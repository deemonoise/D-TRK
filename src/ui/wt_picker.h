#pragma once
#include <functional>
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "hw/sdcard.h"
#include "model.h"
#include "theme.h"
#include "touch.h"

namespace ui {

class App;

// Level 0 of wavetable frame f (0..63) of a mapped table as a polyline in [x0, x1) x [y0, y1].
void drawWtFrame(LGFX_Sprite& s, const int16_t* table, int f, int x0, int y0, int x1, int y1, uint16_t c);

// Imports the wavetable WAV at path into the bank and the project's list under a name made from
// base (sanitized, cut to 16; "-2", "-3"... when the name holds another table). out = the name used.
// Toasts and returns false on failure (also while the transport runs: STOP PLAYBACK FIRST).
bool wtImportFile(App& app, const char* path, const char* base, char out[mt::kSampleNameMax + 1]);

// SYNTH table picker over the work area (INST, OSC page), drawn and driven like FILE's import
// lists: row 0 is "< Back" (close, the table stays) or "< Up (..)" in a subfolder, the encoder turns
// round and round, a long press goes back. The table list holds the built-in tables ("*NAME"),
// the project's, then IMPORT... (a browser of /wavetables and its subfolders: folders, *.wav).
// Turning / a tap only moves the cursor; a click (a tap on the selected row) sets the table on the
// oscillator and closes. A picked file is imported (see wtImportFile) and set. The selected
// table's frame 0 is drawn right of the list.
class WtPicker {
 public:
  explicit WtPicker(App& app) : app_(app) {}
  void setOnClose(std::function<void()> f) { onClose_ = std::move(f); }
  void open(int instr, int osc);
  void close();
  bool isOpen() const { return open_; }
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y0);

 private:
  static constexpr int kMaxEntries = 512;
  static constexpr int kHeaderH = 28, kRowH = 24;
  static constexpr int kRows = (kAreaH - kHeaderH) / kRowH;  // incl. the Back row
  static constexpr int kDepthMax = 4;  // subfolders below /wavetables
  // Frame preview right of the table names.
  static constexpr int kPvX0 = 240, kPvX1 = 440, kPvY0 = 16, kPvY1 = 112;
  enum class Kind : uint8_t { Table, Import, Folder, File };

  mt::Instrument& inst();
  // Rows: 0 = Back / Up, i + 1 = entry i.
  int rowCount() const { return count_ + 1; }
  void listTables(const char* select);  // select: table name to put the cursor on, or nullptr
  void listFiles();                     // dir_
  void add(Kind k, const char* name);
  void setSel(int row);  // keeps it on screen
  void moveSel(int d);   // round and round
  void choose(int row);  // click / tap on the selected row
  void back();           // row 0, long press: up a folder, to the tables, or close
  void importFile(int i);
  void setTable(const char* name);

  App& app_;
  std::function<void()> onClose_;
  bool open_ = false;
  bool files_ = false;  // listing /wavetables
  int instr_ = 0, osc_ = 0;
  Kind kinds_[kMaxEntries];
  char (*names_)[hw::kNameMax] = nullptr;  // PSRAM, kMaxEntries, while open
  int count_ = 0, sel_ = 0, top_ = 0, dragAcc_ = 0;
  int y0_ = kAreaY;
  char dir_[128] = "/wavetables";  // kept while powered
};

}  // namespace ui
