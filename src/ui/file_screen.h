#pragma once
#include "hw/sdcard.h"
#include "import_dialog.h"
#include "keyboard.h"
#include "screen.h"

namespace ui {

// Save / Save As / Load / New / Import MIDI (stage 6). Load shows /projects/*.mtp.
class FileScreen : public Screen {
 public:
  explicit FileScreen(App& app) : app_(app), import_(app) {}
  void onEnter() override;
  void onLeave() override;
  void onInput(const hw::InputEvent& ev) override;
  void onTouch(const TouchEvent& ev) override;
  void draw(LGFX_Sprite& s, int y0, int h) override;

 private:
  enum Action : int { kSave, kSaveAs, kLoad, kNew, kImport, kRetry, kActions };
  enum MenuId : int { kCancel, kDiscardLoad, kDiscardNew, kLoadBak, kOverwrite };
  static constexpr int kHeaderH = 28;
  static constexpr int kActionH = 36;
  static constexpr int kRowH = 24;
  static constexpr int kMaxFiles = 128;
  static constexpr int kListRows = (kAreaH - kHeaderH) / kRowH;  // incl. the Back row

  bool enabled(int a) const;
  void moveSel(int delta);
  void run(int a);
  void onMenu(int id);
  void save();
  void saveAs(const char* initial);
  void doSave(const char* name);
  void openList(bool midi);
  void closeList();
  void chooseFile(int idx);
  void doLoad(bool bak);
  void doNew();
  void listScroll(int rows);

  App& app_;
  Keyboard kb_;
  ImportDialog import_;
  int sel_ = kSave;
  int y0_ = kAreaY;
  // Load / import list, PSRAM while open.
  char (*names_)[hw::kNameMax] = nullptr;
  int count_ = 0;
  int listSel_ = 0;  // 0 = Back, i + 1 = names_[i]
  int listTop_ = 0;
  int dragAcc_ = 0;
  bool midiList_ = false;  // names_ lists /midi/*.mid
  char pending_[hw::kNameMax] = {0};  // target of a confirmation menu
};

}  // namespace ui
