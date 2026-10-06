#pragma once
#include "bank_screen.h"
#include "edit_ops.h"
#include "engine/engine.h"
#include "file_screen.h"
#include "grid_screen.h"
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "inst_screen.h"
#include "menu.h"
#include "model.h"
#include "proj_screen.h"
#include "screen.h"
#include "touch.h"
#include "track_screen.h"
#include "undo.h"

namespace ui {

enum class Tab : uint8_t { Grid, Track, Bank, Inst, Proj, File, Count };

class App {
 public:
  void begin(LGFX* lcd, mt::Project* p);
  void onInput(const hw::InputEvent& ev);
  void tick();  // touch, engine status, redraw (at most every kFrameMs)
  void setTab(Tab t);
  Tab tab() const { return tab_; }
  Menu& menu() { return menu_; }
  mt::Project& project() { return *p_; }
  const engine::Status& status() const { return status_; }
  bool shift() const { return shift_; }
  void invalidate() { dirty_ = true; }
  uint8_t editPattern() const { return status_.pattern; }
  // Track shared by GRID and TRACK screens.
  int curTrack() const { return curTrack_; }
  void setCurTrack(int t) { curTrack_ = static_cast<uint8_t>(((t % mt::kTracks) + mt::kTracks) % mt::kTracks); }

  // Shared by screens; nullptr when the allocation failed.
  mt::Undo* undo() { return undoBuf_ ? &undo_ : nullptr; }
  mt::Clipboard* clipboard() { return clip_; }
  void pushUndo();  // snapshot editPattern() before an edit
  bool doUndo();    // false when there is nothing to undo
  void dropUndo() {  // forget the last pushUndo() (edit cancelled)
    if (undoBuf_) undo_.drop();
  }
  void toast(const char* msg);
  // Draws msg over the current frame and pushes it right away: call before blocking SD I/O.
  void showBusy(const char* msg);
  // showBusy("label NN%"), redrawn at most every 200 ms unless the label changes.
  void showProgress(const char* label, uint32_t done, uint32_t total);
  // storage::SyncProgress for sample sync / pull: "SAMPLE name NN%". ctx = App*.
  static void syncProgress(const char* file, uint32_t done, uint32_t total, void* app);
  // One toast after a load: what (e.g. "LOADED X", may be nullptr), missing samples, a failed sample
  // folder write; leading parts are dropped when it gets too long.
  void loadedToast(const char* what, int missing, bool folderFail);

  // Unsaved changes: every UI write to the project calls markDirty().
  void markDirty() {
    ++editSeq_;
    dirty_ = true;
  }
  void markSaved() { savedSeq_ = editSeq_; }
  bool projectDirty() const { return editSeq_ != savedSeq_; }
  // Edit counter: a cancelled preview rewinds it to the value seen before, if nothing else edited.
  uint32_t editSeq() const { return editSeq_; }
  void rewindEditSeq(uint32_t seq) {
    editSeq_ = seq;
    dirty_ = true;
  }
  // Wi-Fi mode: Play and the status-bar transport only show a toast.
  void lockTransport(bool on) { transportLocked_ = on; }
  // After load / new: clears undo and the dirty flag, resets screen state.
  void projectReplaced();

 private:
  static constexpr uint32_t kFrameMs = 25;
  static constexpr uint32_t kToastMs = 1500;
  static constexpr uint32_t kFlashMs = 50;  // track LED flash per note
  static constexpr uint32_t kCpuMs = 500;   // status bar CPU: averaging window
  static constexpr uint32_t kCpuRedMs = 2000;  // status bar CPU: red held after an audio stall
  static constexpr int kTabW = kScreenW / static_cast<int>(Tab::Count);
  // Status bar hit areas.
  static constexpr int kBpmX0 = 64, kBpmX1 = 176;
  static constexpr int kTransX0 = 256, kTransX1 = 320;

  Screen* screen() { return screens_[static_cast<int>(tab_)]; }
  void onTouch(const TouchEvent& ev);
  void transport();
  void trackKey(int n, bool shift);
  void updateLeds(uint32_t now);
  void saveVolumeIdle(uint32_t now);
  void pollCpu(uint32_t now);
  void setBpmEdit(bool on);
  void draw();
  void drawStatus();
  void drawTabs();
  void drawBusy();

  LGFX* lcd_ = nullptr;
  LGFX_Sprite* spr_ = nullptr;
  mt::Project* p_ = nullptr;
  engine::Status status_{};
  TouchTracker touch_;
  Menu menu_;

  GridScreen grid_{*this};
  TrackScreen track_{*this};
  BankScreen bank_{*this};
  InstScreen inst_{*this};
  ProjScreen proj_{*this};
  FileScreen file_{*this};
  Screen* screens_[static_cast<int>(Tab::Count)] = {&grid_, &track_, &bank_, &inst_, &proj_, &file_};
  Tab tab_ = Tab::Grid;

  mt::Undo::Entry* undoBuf_ = nullptr;  // kDepth entries + 1 scratch
  mt::Undo undo_{nullptr};
  mt::Clipboard* clip_ = nullptr;

  const char* busy_ = nullptr;  // only set inside showBusy()
  char progLabel_[32] = {0};    // showProgress()
  char progMsg_[40] = {0};
  int progPct_ = -1;
  uint32_t progMs_ = 0;
  char toast_[48] = {0};
  uint32_t toastUntil_ = 0;
  uint16_t lastBpm_ = 0;
  int cpu_ = 0;              // audio load shown in the status bar, % (window average)
  uint16_t cpuColor_ = kDim;  // its color: kDim / kYellow / kRed
  uint32_t cpuAt_ = 0;       // last load taken
  uint32_t cpuRedUntil_ = 0;  // millis() until which the CPU readout stays red (audio stall)
  int bpmTarget_ = 120;  // local while editing: p_->bpm lags behind the engine queue
  bool bpmEdit_ = false;
  uint8_t curTrack_ = 0;
  uint32_t flashUntil_[mt::kTracks] = {};
  uint8_t savedVol_ = 0;     // master volume as stored in NVS
  uint8_t pendingVol_ = 0;   // changed value waiting to be saved
  uint32_t volChangedAt_ = 0;  // 0 = nothing pending
  uint32_t editSeq_ = 0, savedSeq_ = 0;
  bool shift_ = false;
  bool transportLocked_ = false;
  bool dirty_ = true;
  uint32_t lastDraw_ = 0;
};

}  // namespace ui
