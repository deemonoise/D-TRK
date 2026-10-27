#pragma once
#include <functional>
#include <stddef.h>
#include <stdint.h>
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "hw/sdcard.h"
#include "theme.h"
#include "touch.h"

namespace ui {

class App;

// The list of every browser (FILE Load / Import MIDI / Import WAV, the preset browser, the SYNTH
// table picker and its WT import), so they look and behave the same:
// - rows of 24 px under a 28 px header: row 0 is "< Back" (at the top) or "< Up (..)", then the
//   entries (row = entry + 1); folders end in "/"; names are cut ("~") before the ^ / v marks;
// - the encoder turns round and round, Shift + turn moves a page; a click opens the row;
// - a long press (and B, via the owner's onBack) is Back: the owner goes up a folder (the cursor on
//   the folder left) or closes at the top;
// - a tap on a folder, an action or Back opens it at once; a file's first tap selects it (and
//   previews it, see setPreviewDir), a tap on the selected one confirms; a drag scrolls;
// - an empty list shows a dim EMPTY FOLDER (NO SD CARD without a card) line.
// Entries live in PSRAM while the list is allocated.
class FileList {
 public:
  // Dir: "/", opens on a tap. File: a tap selects, a tap on the selected one confirms. Action: opens
  // on a tap (DRAW..., IMPORT...).
  enum class Kind : uint8_t { Dir, File, Action };
  // What an input asks of the owner.
  enum class Act : uint8_t {
    None,
    Moved,  // the cursor moved (presets audition the new row)
    Back,   // row 0 chosen or a long press: up a folder / close
    Open,   // the selected entry chosen: open the folder, confirm the file, run the action
  };
  static constexpr int kHeaderH = 28, kRowH = 24;
  static constexpr int kRows = (kAreaH - kHeaderH) / kRowH;  // incl. the Back row
  static constexpr int kMarkX = kScreenW - 24;                // ^ / v
  static constexpr int kDepthMax = 4;                         // folders below a browser's root

  FileList(App& app, int cap) : app_(app), cap_(cap) {}
  bool alloc();  // PSRAM; toasts NO MEMORY on failure
  void free();   // stops a preview too
  bool ready() const { return names_ != nullptr; }

  // A new listing: no entries, cursor on Back, top row 0, no preview.
  void clear();
  bool add(Kind k, const char* name, uint8_t tag = 0, int16_t index = 0);
  // Folders of dir (sorted), then its files ending in one of exts (full names, sorted); the tags
  // given to them.
  void addFolder(const char* dir, const char* const* exts, int extCount, bool folders = true, uint8_t dirTag = 0,
                 uint8_t fileTag = 0);
  // Files of dir ending in ext, the extension cut (accept: base names kept), sorted.
  void addFiles(const char* dir, const char* ext, bool (*accept)(const char*), uint8_t tag = 0);
  // Ends a listing: the cursor on the entry named cursor (any case; Dir only when dirOnly), else on
  // the first entry (Back when empty).
  void finish(const char* cursor = nullptr, bool dirOnly = false);

  int count() const { return count_; }
  int rows() const { return count_ + 1; }
  int sel() const { return sel_; }    // row
  int entry() const { return sel_ - 1; }  // -1: Back
  void select(int row);               // kept on screen
  Kind kind(int i) const { return meta_[i].kind; }
  const char* name(int i) const { return names_[i]; }
  uint8_t tag(int i) const { return meta_[i].tag; }
  int16_t index(int i) const { return meta_[i].index; }
  // The selected entry's name, "" on Back (remembered between openings).
  void cursorName(char* out, size_t cap) const;

  Act onInput(const hw::InputEvent& ev);
  Act onTouch(const TouchEvent& ev, int top);  // top: the first row's y
  // Play: a file previewed (setPreviewDir); true when the list takes Play.
  bool onPlay();

  // Look.
  void setBack(bool top) { backTop_ = top; }   // "< Back" / "< Up (..)"
  void setNameRight(int x) { nameRight_ = x; }  // names end before x (default: the ^ / v marks)
  std::function<uint16_t(int)> color;          // entry colour (default kText)
  std::function<bool(int)> mark;               // ">" left of the entry (applied / set now)
  void draw(LGFX_Sprite& s, int top) const;
  // The header bar: title, the path's tail ("..." when cut), SD OK / NO SD CARD ending at right.
  static void drawHeader(LGFX_Sprite& s, int y0, const char* title, const char* path, int right = kScreenW - 16);

  // Play / the first tap on a file previews dir/name on the synth board (WAV import, WT import);
  // nullptr: no preview. Moving off the file stops it.
  void setPreviewDir(const char* dir) { pvDir_ = dir; }
  void previewStop();

 private:
  struct Meta {
    Kind kind;
    uint8_t tag;
    int16_t index;
  };
  void previewToggle();
  void moved();  // stops a preview left behind

  App& app_;
  const int cap_;
  char (*names_)[hw::kNameMax] = nullptr;  // PSRAM, cap_ (hw::sdList* write here)
  Meta* meta_ = nullptr;                    // PSRAM, cap_, right after names_
  int count_ = 0, sel_ = 0, top_ = 0, dragAcc_ = 0;
  bool backTop_ = true;
  int nameRight_ = kMarkX - 8;
  const char* pvDir_ = nullptr;
  int pvSel_ = -1;        // row previewed, -1 none
  uint32_t pvEndMs_ = 0;  // millis() when it has played to its end
};

}  // namespace ui
