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

// Preset browser over the work area (INST). Load opens at the top: [FACTORY] (built-in, type ->
// category -> preset) and a folder per type with the user's folders and .mti files of
// /presets/<TYPE>; ".." goes up. A preset of another type changes the instrument's type; a factory
// KIT also writes its drums into INS25..32 (INS17..24 for a kit there). Selecting a preset applies it
// at once and plays C4; OK keeps it, CANCEL (or EncLong) puts the instrument (and the drum block)
// back. DEL removes a user preset (menu confirm). Save: the instrument's type root, user folders
// only; SAVE asks the name (keyboard) and confirms an overwrite, a click on a file saves over it;
// +DIR makes a folder.
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
  // Header: path (Save: after the type), three buttons (Load: OK DEL CANCEL, Save: SAVE +DIR CANCEL).
  static constexpr int kPathX = 144;
  static constexpr int kBtnX[3] = {296, 344, 392};
  static constexpr int kBtnX1[3] = {340, 388, 472};
  enum class Kind : uint8_t { Up, Factory, FactoryType, Category, UserType, Folder, File, FactoryFile };
  struct Entry {
    Kind kind;
    int16_t index;  // factory preset index / category ordinal / InstrType (FactoryType, UserType)
  };
  enum MenuId : int { kCancel, kOverwrite, kDelete };

  mt::Instrument& inst();
  void list();          // fills the entries for type_ / dir_ / inFactory_ / facType_ / category_
  void add(Kind k, int index, const char* name);
  void enter(int i);    // folder / type / factory type / category / [FACTORY] / ".."
  void pick(int i);     // Load: apply + preview
  void choose(int i);   // click / tap on a row
  void moveSel(int d);
  void apply(const mt::Instrument& src);  // applyPreset under lockProject, markDirty, preview
  void applyKit(int index);  // factory KIT: the kit + its drums in INS block_.., preview
  void resolveTables();  // SYNTH: imports tables missing from the project from /wavetables
  void askName();       // Save: keyboard
  void saveAs(const char* name);
  void askFolder();
  void askDelete();
  void onMenu(int id);
  void button(int b);   // header button 0..2
  void drawHeader(LGFX_Sprite& s, int y0);

  App& app_;
  Keyboard kb_;
  std::function<void()> onClose_;
  bool open_ = false;
  Mode mode_ = Mode::Load;
  int instr_ = 0;
  mt::InstrType type_ = mt::InstrType::Chip;  // Load: Count = the top
  char dir_[mt::kPresetDirMax] = {0};
  bool inFactory_ = false;
  mt::InstrType facType_ = mt::InstrType::Count;  // inside [FACTORY]: Count = type list
  char category_[17] = {0};  // inside a factory type: "" = category list
  Entry entries_[kMaxEntries];
  char (*names_)[hw::kNameMax] = nullptr;  // PSRAM, kMaxEntries, while open
  int count_ = 0, sel_ = 0, top_ = 0, dragAcc_ = 0;
  int picked_ = -1;  // entry applied last (Load), -1 after a new listing
  int y0_ = kAreaY;
  mt::Instrument backup_;
  mt::Instrument* blockBackup_ = nullptr;  // PSRAM, kKitLanes, Load while open: INS block_.. on open
  int block_ = 0;                          // factoryKitBlock(instr_)
  bool blockChanged_ = false;              // a factory KIT wrote its drums there
  uint32_t seqBefore_ = 0, seqAfter_ = 0;
  bool changed_ = false;
  char pending_[17] = {0};  // name a confirmation menu acts on
  // Last folder per type (RAM).
};

}  // namespace ui
