#pragma once
#include "edit_ops.h"
#include "fill.h"
#include "fill_dialog.h"
#include "model.h"
#include "screen.h"
#include "track_leds.h"
#include "transpose_dialog.h"

namespace ui {

class GridScreen : public Screen {
 public:
  explicit GridScreen(App& app) : app_(app) {
    for (uint8_t& n : lastNote_) n = 60;
  }
  void onEnter() override;
  void onLeave() override;
  void onProjectReplaced() override;
  void onPatternChange() override;
  void onInput(const hw::InputEvent& ev) override;
  void onBack() override;
  void onATap(bool shift) override;
  void onTouch(const TouchEvent& ev) override;
  void draw(LGFX_Sprite& s, int y0, int h) override;
  bool wantsRedraw(const engine::Status& st) override;
  // Track button N: false when App should handle it (Shift + N outside edit = mute).
  bool trackKey(int n, bool shift);
  void trackRelease(int n);  // PERF: the held effect ends
  // The track buttons play notes / lanes / effects here (edit, REC, PERF): no hold-and-turn volume.
  bool buttonsBusy() const { return edit_ || rec_ || perf_; }
  void undo();  // with a toast; the next edit takes a fresh snapshot
  bool dialogOpen() const { return fill_.isOpen() || transpose_.isOpen(); }  // Fill / Transpose preview

 private:
  // Fx field f: slot (f - kFx1) / 2, the command on even (f - kFx1), its value on odd.
  enum Field : uint8_t { kNote, kVel, kFx1, kFields = kFx1 + 2 * mt::kFxSlots };
  // Context menu ids.
  enum MenuId : int {
    kCopyStep, kPaste, kClearStep, kCopyTrack, kClearTrack, kTranspose, kSelect,
    kToggleView, kToggleFollow, kUndo, kCopySel, kClearSel, kDropSel, kNoteOff, kNoteOffSel, kFill,
    kRec, kPerf, kResampleTrack, kResamplePattern, kEditStep,
    kEditStepVal = 100  // + N: the Edit step submenu
  };
  static constexpr int kEditStepMax = 16;

  static constexpr int kNamesH = 16;
  static constexpr int kRowH = 16;
  static constexpr int kNumW = 32;
  static constexpr int kColW = 56;     // Overview track column
  static constexpr int kOverviewTracks = mt::kTrackLeds;  // overview columns: one half of the tracks
  static_assert(kNumW + kOverviewTracks * kColW <= kScreenW, "Overview columns must fit");
  static_assert(mt::kTracks % kOverviewTracks == 0, "halves must tile the tracks");
  static constexpr int kDetW = kScreenW - kNumW;  // Detail: the current track only
  static constexpr int kFieldW = 32;   // Detail field pitch: kFields fill kDetW
  static_assert(kFields * kFieldW <= kDetW, "Detail fields must fit");
  static constexpr int kKbH = 48;      // mini keyboard height
  static constexpr int kKeyW = 40;
  static constexpr int kPadW = kScreenW / mt::kKitLanes;  // lane pad button (drum track)
  static constexpr uint32_t kFollowPauseMs = 2000;

  mt::Pattern& pat();
  int len() const;  // pattern length clamped to kMinSteps..kMaxSteps
  int cur();  // curStep_ clamped to the pattern length
  int track() const;
  // The overview shows the half of the tracks holding the cursor; the half is not state.
  int half() const { return track() / kOverviewTracks; }  // 0 = tracks 1-8, 1 = 9-16
  int firstTrack() const { return half() * kOverviewTracks; }
  int rowsFor(int h) const;
  void cursorMoved();
  void moveStep(int d);
  void moveTrack(int d);
  void moveField(int d);
  void setEdit(bool on);
  void snapCell();    // edit: the cell under the cursor as it is now, for cancelCell()
  void cancelCell();  // Shift+click in edit: the cell back as it was, edit off
  void openEditStepMenu();
  void toggleView();
  void editTurn(int delta, bool shift);
  void setNote(uint8_t note);
  void previewNote(uint8_t note);
  void enterDegree(int button, bool octaveUp);
  uint8_t degreeNote(int button, bool octaveUp, int ref) const;  // scale degree in the octave of ref
  void setRec(bool on);
  void setPerf(bool on);
  void perfRelease();
  bool recordKey(int n, bool shift);  // REC while playing: button N into the heard step
  int heardTrackStep(int tr);  // the heard step of track tr (own length and speed); slow: the last one played
  void writeStep(const mt::Step& st);
  void openMenu();
  void openFill();
  void resample(bool wholePattern);  // the pattern (current track / audible tracks) into a sample RSn
  void openTranspose();
  void transpose(const mt::Sel& sel, int amount, bool degrees);
  void selFollow();  // selection end follows the cursor
  void onMenu(int id);
  void apply(const mt::Sel& sel, int id);  // clear / note off under pushUndo + lock
  mt::Sel trackSel() const;
  mt::Sel curSel() const;
  bool hit(int x, int y, int& step, int& tr, int& field) const;
  // Drum track: the track's instrument is a KIT, steps are lane masks.
  bool drumAt(int tr) const;
  bool drum() const { return drumAt(track()); }
  // Not while A holds the edit: the grid keeps its rows.
  bool keyboardShown() const { return edit_ && !holdEdit_ && curField_ == kNote && !drum(); }
  bool padShown() const { return edit_ && !holdEdit_ && curField_ == kNote && drum(); }  // lane pad instead of the keyboard
  void toggleLane(int lane);
  void drawOverview(LGFX_Sprite& s, int gridY);
  void drawDetail(LGFX_Sprite& s, int gridY);
  void drawKeyboard(LGFX_Sprite& s, int y);
  // Lane mask as squares: one row of 8 (Overview column) or compact 2 x 4 (Detail NOTE field).
  void drawMask(LGFX_Sprite& s, int x, int y, uint8_t mask, bool audible, int cursorLane, bool compact);
  void drawPad(LGFX_Sprite& s, int y);
  bool drawBadge(LGFX_Sprite& s, int y);
  bool selected(int tr, int step) const;

  App& app_;
  int y0_ = kAreaY;
  int h_ = kAreaH;
  int rows_ = (kAreaH - kNamesH) / kRowH;
  int curStep_ = 0;
  int curField_ = kNote;
  int top_ = 0;
  bool detail_ = false;
  bool edit_ = false;
  bool editPushed_ = false;    // undo snapshot taken in this edit session
  bool holdEdit_ = false;      // edit entered by A + turn: releasing A ends it
  // Edit: the cell under the cursor when edit began or the cursor arrived, and whether a step was
  // written before that (then cancelCell() keeps the undo snapshot).
  mt::Step cellOrig_{};
  int cellPat_ = -1, cellTr_ = 0, cellStep_ = 0;
  bool cellOthers_ = false;
  int editStep_ = 1;  // steps the cursor moves after a note from a track button (0 = stays)
  bool needVisible_ = true;    // scroll to the cursor on the next draw
  bool follow_ = true;
  bool dragFrozen_ = false;    // follow off after a Drag until the next Play
  bool wasPlaying_ = false;
  uint32_t lastMoveMs_ = 0;
  uint8_t lastNote_[mt::kTracks];  // set to 60 in the constructor
  int lane_ = 0;  // drum track, NOTE field in edit: the lane under the encoder
  // Live modes, exclusive with each other and with edit. REC: track buttons write into the heard
  // step while playing (one undo snapshot per pass). PERF: a held button = punch-in effect.
  bool rec_ = false;
  bool perf_ = false;
  uint32_t recLoop_ = UINT32_MAX;  // pass of the last REC undo snapshot
  int recPat_ = -1;                // and its pattern
  int perfBtn_ = -1;   // button holding the punch-in effect
  int perfTrack_ = 0;  // its track

  FillDialog fill_{app_};
  mt::FillSpec fillSpec_;  // RAM only, shared by the tracks
  bool fillInit_ = false;  // values set from the cursor once
  mt::ArpSpec arpSpec_;    // RAM only, FILL's arp page
  bool arpInit_ = false;   // root set from the track once
  TransposeDialog transpose_{app_};
  mt::FxSlot lastFx_[mt::kTracks][mt::kFxSlots] = {};  // last FX written per track and slot, offered on empty slots
  bool fxCycled_ = false;  // a command was turned on this cell: passing "..." does not offer lastFx_ again

  int menuPattern_ = -1;  // pattern the context menu was opened for
  bool selOn_ = false;
  int selT0_ = 0, selS0_ = 0, selT1_ = 0, selS1_ = 0;  // anchor and end, not normalized
};

}  // namespace ui
