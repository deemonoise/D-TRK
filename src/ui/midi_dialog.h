#pragma once
#include "hw/ble_midi.h"
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "midi_devices.h"
#include "theme.h"
#include "touch.h"

namespace ui {

class App;

// FILE -> MIDI devices: three pages, the shared PageBar switches them. DEVICES: the remembered BLE MIDI
// devices (ADD by scan, ON / OFF, role, delete, BLE clock) and a monitor of the last IN messages.
// PADS: the selected device's pad banks and learn (D5). KNOBS: the project's knob binds (D6).
// Fills the work area.
class MidiDialog {
 public:
  explicit MidiDialog(App& app) : app_(app) {}
  void open();
  void close();  // a learn in progress stops
  void projectReplaced();  // load / new: an open edit ends, nothing saved or toasted
  bool isOpen() const { return open_; }
  void poll();  // learn steps, redraw on a state / monitor change
  bool wantsRedraw();
  void onInput(const hw::InputEvent& ev);
  void onPage(int delta) { setPage(page_ + delta); }
  void onBack();  // B: cancel this learn, end an edit, or close
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y0);

 private:
  enum Page : int { kPgDevices, kPgPads, kPgKnobs, kPages };
  enum MenuId : int {
    kMenuOnOff = 100, kMenuRoleIn, kMenuRoleOut, kMenuRoleBoth, kMenuDelete, kMenuClock, kMenuClearAll, kMenuAdd, kMenuLearn, kMenuEdit, kMenuClear, kMenuKnobDelete, kMenuKnobClearAll, kMenuCancel, kMenuRolePick
  };
  // The edited field, in click order. PADS: CH, note. KNOBS: CC, CH, TRK (not for PROJ), MODE.
  enum Edit : int { kEditNone, kEditCh = 1, kEditNote, kEditLength, kKnobCc = 1, kKnobCh, kKnobTrk, kKnobMode };
  static constexpr int kMaxFound = 8;
  static constexpr int kMonLines = 2;
  static constexpr int kTitleH = 24;
  static constexpr int kHeaderH = kTitleH + 24;  // title, then the shared PageBar
  static constexpr int kRowH = 24;
  // Reserve two complete monitor lines above the tabs in either status-bar layout.
  static constexpr int kMonH = kAreaH < 272 ? 16 : 18;
  static constexpr int kMonY = kAreaH - kMonLines * kMonH - 2;
  static_assert(kHeaderH + (mt::kDevMax + 1) * kRowH <= kMonY, "Device rows and monitor must not overlap");
  static constexpr int kKnobRows = 7;
  static constexpr int kKnobMonY = kMonY;
  static constexpr int kKnobMonLines = 2;

  int rows() const;     // cursor rows on the page (device rows, pad rows)
  int& rowSel();        // the page's selected row
  mt::MidiDevice* padDev();  // the device PADS shows (dev_), null when the list is empty
  bool padDevIsIn() const;   // dev_ is the active IN (its table is the router's)
  void contextMenu();
  void moveSel(int delta);  // one cursor over the page bar and rows
  void rowClick();          // encoder click / tap on the selected row
  void setPage(int p);
  void addDevice();      // scan, then the menu of new devices, then the role menu
  void addFound(int f);  // found_[f] into the list with the chosen role
  void toggleOn();       // the selected device on / off
  void moreMenu();
  void setRole(int role);
  void removeSel();
  void changed(uint8_t offMask);  // save + apply, the stack up / down as needed, toast the offs
  void padTouch();        // before a change of the active IN's table: its sounding pads end
  void editOpen(int field);  // A + turn on a row with no edit: that field (a cleared pad row starts)
  void editTurn(int delta);  // the edited field
  void leaveEdit();       // PADS: saved when the edit changed something; both: clash toasts
  void padTurn(int delta);
  void padLeave();
  void clearRow();
  void clearAll();
  // KNOBS: the project's used binds, in slot order (re-read every time: a load / new replaces them).
  int knobList(int* slots) const;
  int knobSlot() const;   // the selected row's bind slot, -1 = none
  void knobFix();         // selection / scroll clamped; an edit of a freed slot ends
  void knobTurn(int delta);
  void knobLeave();
  void knobDelete();
  void knobClearAll();
  uint32_t knobsKey() const;
  void startLearn();
  void stopLearn();
  void learnPoll();  // end once the selected function is learned
  void endIdle();    // stack down when no device is active
  void armStep(int step);  // step 4 = done
  uint32_t stateKey() const;  // what draw() shows besides the monitor: changes trigger a redraw
  void drawDevices(LGFX_Sprite& s, int y0);
  void drawPads(LGFX_Sprite& s, int y0);
  void drawKnobs(LGFX_Sprite& s, int y0);
  void drawMonitor(LGFX_Sprite& s, int y, int lines, bool divider = true);

  App& app_;
  hw::ble::Found found_[kMaxFound] = {};
  int foundPick_ = -1;        // found_ index waiting for its role
  int page_ = kPgDevices;
  int dev_ = 0;               // selected device row (kept for PADS)
  int padRow_ = 0;            // selected PADS row (PadMode)
  int edit_ = kEditNone;      // PADS field being edited
  bool editTouched_ = false;  // the edit changed something (save / check on leaving)
  int knobRow_ = 0;           // selected KNOBS row (index into knobList())
  int dragAcc_ = 0;
  int knobTop_ = 0;           // first KNOBS row shown
  int editSlot_ = -1;         // KNOBS: the bind slot being edited
  int deviceRow_ = 0;        // device list cursor; count() is Add device
  bool bar_ = true;          // shared page-bar focus above the rows
  int learnStep_ = -1;        // PadMode being learned, -1 = none
  uint32_t learnConns_ = 0;   // App::midiConnects() when the step was armed (a reconnect disarms)
  uint32_t monSeq_ = 0;       // App::monitorSeq() at the last draw
  uint32_t stateKey_ = 0;
  int y0_ = kAreaY;
  bool open_ = false;
  bool redraw_ = false;
  bool menuOpen_ = false;  // a menu of ours is up (closing it may leave the stack idle)
};

}  // namespace ui
