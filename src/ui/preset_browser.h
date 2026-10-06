#pragma once
#include <functional>
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "hw/sdcard.h"
#include "keyboard.h"
#include "model.h"
#include "preset_paths.h"
#include "theme.h"
#include "touch.h"

namespace ui {

class App;

// Preset browser over the work area (INST). Load: the type's root lists [FACTORY] (built-in, by
// category), the user's folders and .mti files of /presets/<TYPE>; ".." goes up. Selecting a preset
// applies it to the instrument at once and plays C4; OK keeps it, CANCEL (or EncLong) puts the
// instrument back. DEL removes a user preset (menu confirm). Header arrows (or Shift+turn) switch the
// type. Save: the instrument's type, user folders only; SAVE asks the name (keyboard) and confirms
// an overwrite, a click on a file saves over it; +DIR makes a folder. The last folder per type is
// kept while powered.
class PresetBrowser {
 public:
  enum class Mode : uint8_t { Load, Save };
  explicit PresetBrowser(App& app) : app_(app) {}
  // Runs after every close (the instrument's type may have changed).
  void setOnClose(std::function<void()> f) { onClose_ = std::move(f); }
  void open(Mode mode, int instr);
  void close(bool keep);  // Load: false restores the instrument
  bool isOpen() const { return open_; }
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y0);

 private:
  static constexpr int kMaxEntries = 64;
  static constexpr int kHeaderH = 28, kRowH = 24;
  static constexpr int kRows = (kAreaH - kHeaderH) / kRowH;
  static constexpr int kPreviewNote = 60;
  // Header hit areas: type arrows (Load), path, three buttons (Load: OK DEL CANCEL, Save: SAVE +DIR
  // CANCEL).
  static constexpr int kLeftX1 = 40, kRightX0 = 96, kRightX1 = 136;
  static constexpr int kPathX = 144;
  static constexpr int kBtnX[3] = {296, 344, 392};
  static constexpr int kBtnX1[3] = {340, 388, 472};
  enum class Kind : uint8_t { Up, Factory, Category, Folder, File, FactoryFile };
  struct Entry {
    Kind kind;
    int16_t index;  // factory preset index / category ordinal
  };
  enum MenuId : int { kCancel, kOverwrite, kDelete };

  mt::Instrument& inst();
  void list();          // fills the entries for type_ / dir_ / inFactory_
  void add(Kind k, int index, const char* name);
  void enter(int i);    // folder / category / [FACTORY] / ".."
  void pick(int i);     // Load: apply + preview
  void choose(int i);   // click / tap on a row
  void moveSel(int d);
  void apply(const mt::Instrument& src);  // applyPreset under lockProject, markDirty, preview
  void resolveTables();  // SYNTH: imports tables missing from the project from /wavetables
  void askName();       // Save: keyboard
  void saveAs(const char* name);
  void askFolder();
  void askDelete();
  void onMenu(int id);
  void switchType(int d);
  void button(int b);   // header button 0..2
  void drawHeader(LGFX_Sprite& s, int y0);

  App& app_;
  Keyboard kb_;
  std::function<void()> onClose_;
  bool open_ = false;
  Mode mode_ = Mode::Load;
  int instr_ = 0;
  mt::InstrType type_ = mt::InstrType::Chip;
  char dir_[mt::kPresetDirMax] = {0};
  bool inFactory_ = false;
  char category_[17] = {0};  // inside [FACTORY]: "" = category list
  Entry entries_[kMaxEntries];
  char (*names_)[hw::kNameMax] = nullptr;  // PSRAM, kMaxEntries, while open
  int count_ = 0, sel_ = 0, top_ = 0, dragAcc_ = 0;
  int picked_ = -1;  // entry applied last (Load), -1 after a new listing
  int y0_ = kAreaY;
  mt::Instrument backup_;
  uint32_t seqBefore_ = 0, seqAfter_ = 0;
  bool changed_ = false;
  char pending_[17] = {0};  // name a confirmation menu acts on
  // Last folder per type (RAM).
  char lastDir_[static_cast<int>(mt::InstrType::Count)][mt::kPresetDirMax] = {};
};

}  // namespace ui
