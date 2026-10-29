#pragma once
#include <functional>
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "hw/sdcard.h"
#include "file_list.h"
#include "keyboard.h"
#include "model.h"
#include "preset_paths.h"
#include "theme.h"
#include "touch.h"

namespace ui {

class App;

// Preset browser over the work area (INST), a FileList like every browser. Load's top: [FACTORY]
// (built-in, type -> category -> preset) and a folder per type with the user's folders and .mti
// files of /presets/<TYPE>; row 0 goes up ("< Up (..)"), at the top "< Back" cancels. A preset of
// another type changes the instrument's type; a factory KIT also writes its drums into INS25..32
// (INS17..24 for a kit there). Selecting a preset (turn, first tap) applies it at once and plays C4;
// a click / a tap on it again or OK keeps it and closes; CANCEL, or B / a long press at the top,
// puts the instrument (and the drum block) back. Each browser opens where it was left for the
// instrument's type (Load and Save apart; a folder gone: the type's root). A preset's recommended insert (drive, bits, rate, drive model) overwrites the current track's
// insert when that track plays the instrument (not for a KIT lane's instrument: the KIT track keeps
// its insert); CANCEL puts it back too. Save stores that track's insert as the recommendation.
// DEL removes a user preset (menu confirm). Save: the instrument's type's user folders only
// (B at its root closes); SAVE asks the name (keyboard) and confirms an overwrite, a click on a file
// (a tap on the selected one) saves over it; +DIR makes a folder.
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
  static constexpr int kMaxEntries = 512;
  static constexpr int kHeaderH = FileList::kHeaderH;
  static constexpr int kPreviewNote = 60;
  // Header: LOAD / SAVE <TYPE>, the path, SD status, three buttons (Load: OK DEL CANCEL, Save: SAVE +DIR CANCEL).
  static constexpr int kBtnX[3] = {296, 344, 392};
  static constexpr int kBtnX1[3] = {340, 388, 472};
  // FileList tags; index: factory preset index / category ordinal / InstrType (FactoryType, UserType).
  enum class Kind : uint8_t { Factory, FactoryType, Category, UserType, Folder, File, FactoryFile };
  // Where a browser was left (per mode and instrument type, PSRAM while powered).
  struct Place {
    bool valid;
    bool inFactory;
    mt::InstrType type, facType;
    char category[17];
    char dir[mt::kPresetDirMax];
    char cursor[hw::kNameMax];
  };
  enum MenuId : int { kCancel, kOverwrite, kDelete };

  mt::Instrument& inst();
  // Fills the list for type_ / dir_ / inFactory_ / facType_ / category_, the cursor on that name.
  void list(const char* cursor);
  void add(Kind k, int index, const char* name);
  Kind kindOf(int i) const { return static_cast<Kind>(list_.tag(i)); }
  bool isPreset(int i) const { return i >= 0 && i < list_.count() && list_.kind(i) == FileList::Kind::File; }
  bool atTop() const;   // Load: the type list; Save: the type's root
  void enter(int i);    // folder / type / factory type / category / [FACTORY]
  void back();          // row 0, long press, B: up (the cursor on the folder left); the top: cancel
  void act(FileList::Act a);
  void pick(int i);     // Load: apply + preview
  void choose();        // click / tap on the selected row
  Place* place();       // this mode's for backup_.type, nullptr without memory
  void remember();
  void restore();       // the remembered place, or the default one
  void apply(const mt::Instrument& src);  // applyPreset under lockProject, markDirty, preview
  void applyKit(int index);  // factory KIT: the kit + its drums in INS block_.., preview
  // The preview's insert: the current track when it plays instr_, else none (mt::kTracks).
  uint8_t previewInsert() const;
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
  FileList list_{app_, kMaxEntries};
  int picked_ = -1;  // entry applied last (Load), -1 after a new listing
  int y0_ = kAreaY;
  mt::Instrument backup_;
  int track_ = 0;              // the current track on open: takes the preset's insert (takePresetInsert)
  mt::TrackCfg trackBackup_;   // its insert on open (Load), restored by CANCEL
  mt::Instrument* blockBackup_ = nullptr;  // PSRAM, kKitBlock, Load while open: INS block_.. on open
  int block_ = 0;                          // factoryKitBlock(instr_)
  bool blockChanged_ = false;              // a factory KIT wrote its drums there
  uint32_t seqBefore_ = 0, seqAfter_ = 0;
  bool changed_ = false;
  char pending_[17] = {0};  // name a confirmation menu acts on
};

}  // namespace ui
