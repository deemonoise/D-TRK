#pragma once
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "midi_import.h"
#include "param_list.h"
#include "theme.h"
#include "touch.h"

namespace ui {

class App;

// FILE -> Import MIDI: reads /midi/<name>.mid, then a mapping list (source -> track, transpose,
// globals) with IMPORT / CANCEL. Fills the work area. All buffers live in PSRAM while open and
// are freed by close() (CANCEL, IMPORT done, EncLong, leaving the tab).
class ImportDialog {
 public:
  static constexpr size_t kMaxFileSize = 512 * 1024;
  static constexpr uint32_t kNoteCap = 16384;

  explicit ImportDialog(App& app) : app_(app) {}
  ~ImportDialog() { close(); }
  // Reads and parses the file; false (with a toast) when nothing can be imported.
  bool open(const char* name);
  void close();
  bool isOpen() const { return importRow_ >= 0; }  // rows built (not while reading)
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y0);

 private:
  static constexpr int kHeaderH = 28;
  static constexpr int kGlobals = 8;
  static constexpr int kMaxRows = mt::kSmfMaxSources * 2 + kGlobals + 2;
  enum MenuId : int { kCancel, kDoImport };
  struct State;

  bool readAndParse(const char* name);
  void buildRows();
  bool isAction(int row) const { return row == importRow_ || row == cancelRow_; }
  void action(int row);
  void confirm();
  void commit();

  App& app_;
  State* state_ = nullptr;        // PSRAM
  mt::SmfNote* notes_ = nullptr;  // PSRAM, kNoteCap
  uint32_t noteCount_ = 0;
  int importRow_ = -1, cancelRow_ = -1;
  ParamList list_{kAreaY + kHeaderH};
};

}  // namespace ui
