#pragma once
#include "arp_gen.h"
#include "edit_ops.h"
#include "fill.h"
#include "hw/input.h"
#include "hw/sdcard.h"
#include "keyboard.h"
#include "hw/lgfx_config.h"
#include "model.h"
#include "param_list.h"
#include "touch.h"

namespace ui {

class App;

// Fill over GRID (Polyend style), fills the work area. Type FILL: where (every N / Euclid /
// random %), what (note, velocity or an FX slot), the value (const / ramp / random) and the mode.
// Type ARP: the arp generator (arp_gen.h) writes a chord or the selection's notes as an arp into
// the Dest track; Capture saves Dest's range (as it was at open) as a user pattern. Live preview:
// every change restores the range as it was at open() and fills it again. OK keeps the result (one
// undo step); Cancel, a long encoder press or leaving GRID restores it and drops the undo step.
// OK / CANCEL: header buttons (touch) and the last rows (encoder). Shift+click or the Reseed row =
// new random seed. Edits the pattern captured at open(), even if the heard one changes.
// The original pattern is kept in App's undo scratch (the dialog is modal: no undo while open).
class FillDialog {
 public:
  explicit FillDialog(App& app);
  // pattern must be App::editPattern() (pushUndo() snapshots it). f and a are edited in place.
  // drum: the first track of sel is a drum track (Lane row instead of note values). False: no memory.
  bool open(int pattern, const mt::Sel& sel, mt::FillSpec* f, mt::ArpSpec* a, bool drum);
  bool isOpen() const { return open_; }
  void ok();
  void cancel();
  // Project replaced under the dialog: close without touching the (new) data.
  void abandon() {
    kb_.close();
    open_ = false;
  }
  void onInput(const hw::InputEvent& ev);
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y0);

 private:
  enum Row : int {
    kType, kWhere, kEvery, kOffset, kHits, kLength, kRotation, kDensity, kTarget, kCmd, kLane, kValue, kFrom,
    kTo, kMode, kSeed, kASource, kARoot, kAChord, kADest, kAMode, kAOct, kAPattern, kARate, kARotate, kAGate,
    kASwing, kAVelLo, kAVelHi, kASlide, kARoll, kAGhost, kAMutate, kACapture, kReseed, kOk, kCancel, kRows
  };
  static constexpr int kHeaderH = 28;
  // Header buttons.
  static constexpr int kBtnY = 2, kBtnH = 20;
  static constexpr int kCancelX = 320, kCancelW = 72;
  static constexpr int kOkX = 400, kOkW = 64;
  static constexpr int kUserMax = 32;  // user arp patterns listed

  int rangeLen() const { return sel_.s1 - sel_.s0 + 1; }
  int targetIndex() const;  // 0 NOTE, 1 VEL, 2.. FX slot
  void setTarget(int i);
  void valueDefaults();     // from / to for the target (and command)
  void clampParams();
  void formatValue(uint8_t v, char* out, int n) const;
  uint8_t stepValue(uint8_t v, int d) const;
  uint32_t& seed() { return f_->arp ? a_->seed : f_->seed; }  // of the current type
  int patternCount() const;         // factory, then user arp patterns
  const char* patternName() const;  // of a_->pattern
  void resolvePattern();            // arpPat_ from a_->pattern
  void syncArp();                   // lists user patterns once, then resolvePattern()
  void capture(const char* name);   // Dest's range at open() as a user pattern
  void preview();
  void restore();           // the touched tracks back from the scratch copy
  void reseed();
  bool action(int row);     // OK / Cancel / Reseed row: runs it, true when handled
  void buildRows();         // the rows of the current settings into shown_
  int rowId(int listRow) const { return listRow >= 0 && listRow < shownCount_ ? shownIds_[listRow] : -1; }
  int listRow(int id) const;
  void drawButton(LGFX_Sprite& s, int x, int y, int w, const char* label, bool sel);

  App& app_;
  Param params_[kRows];
  Param shown_[kRows];  // rows of the current settings, as the list shows them
  int shownIds_[kRows] = {};
  int shownCount_ = 0;
  ParamList list_{kAreaY + kHeaderH};
  mt::Pattern* saved_ = nullptr;  // the pattern at open() (App's undo scratch)
  mt::FillSpec* f_ = nullptr;
  mt::ArpSpec* a_ = nullptr;
  mt::ArpPattern arpPat_;  // the pattern of a_->pattern
  int resolved_ = -1;      // a_->pattern arpPat_ was resolved from
  bool listed_ = false;    // user patterns listed since open()
  uint16_t touched_ = 0;   // tracks written by previews since open() (restore / undo swap these)
  static_assert(mt::kTracks <= 16, "touched_ bits");
  Keyboard kb_;            // Capture's name
  char (*userNames_)[hw::kNameMax] = nullptr;  // kUserMax names, allocated on the first open, kept
  int userCount_ = 0;
  mt::Sel sel_{0, 0, 0, 0};
  bool drum_ = false;
  int pattern_ = 0;
  uint32_t seqAtOpen_ = 0;  // App::editSeq() before the first preview
  uint32_t previews_ = 0;   // markDirty() calls made by this dialog
  int y0_ = kAreaY;
  bool open_ = false;
};

}  // namespace ui
