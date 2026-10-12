#pragma once
#include "ab_keys.h"
#include "bank_screen.h"
#include "battery_gauge.h"
#include "edit_ops.h"
#include "engine/engine.h"
#include "file_screen.h"
#include "grid_screen.h"
#include "hw/battery.h"
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "inst_screen.h"
#include "menu.h"
#include "model.h"
#include "proj_screen.h"
#include "screen.h"
#include "song_screen.h"
#include "touch.h"
#include "track_screen.h"
#include "undo.h"

namespace ui {

enum class Tab : uint8_t { Grid, Track, Inst, Mix, Bank, Song, Proj, File, Count };  // MIX: TRACK's mixer view

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
  // SONG plays (or is paused): the sequencer ignores QueuePattern.
  bool songPlays() const { return status_.song && p_->songLen > 0 && (status_.playing || status_.paused); }
  // Track shared by GRID and TRACK screens.
  int curTrack() const { return curTrack_; }
  void setCurTrack(int t) { curTrack_ = static_cast<uint8_t>(((t % mt::kTracks) + mt::kTracks) % mt::kTracks); }

  // Shared by screens; nullptr when the allocation failed.
  mt::Undo* undo() { return undoBuf_ ? &undo_ : nullptr; }
  // Pattern-sized scratch (undo() uses it only inside its call); nullptr without PSRAM.
  mt::Pattern* undoScratch() { return undoBuf_ ? &undoBuf_[mt::Undo::kDepth].data : nullptr; }
  mt::Clipboard* clipboard() { return clip_; }
  // Colour theme (device setting): applied at once, saved to NVS once it stays put.
  int theme() const { return theme_; }
  void setTheme(int i);
  // Autosave interval in minutes, 0 = off (device setting, see autosaveIdle).
  int autosaveMin() const { return autosaveMin_; }
  void setAutosaveMin(int m);
  // Phones: live output level 0..100 % (device setting; the project's master is masterVol).
  int phones() const { return phones_; }
  void setPhones(int v);
  // Preview: notes entered on GRID sound on INT tracks (device setting; the project's byte is ignored).
  bool preview() const { return preview_; }
  void setPreview(bool on);
  void pushUndo() { pushUndo(editPattern()); }  // snapshot editPattern() before an edit
  void pushUndo(uint8_t pattern);                // no lock taken: callers may hold it
  bool doUndo();    // false when there is nothing to undo
  void dropUndo() {  // forget the last pushUndo() (edit cancelled)
    if (undoBuf_) undo_.drop();
  }
  void toast(const char* msg);
  // Battery (FILE SYSTEM row): the gauge and the last sample it took (false before the first or
  // when stale); batterySeq() changes with every new sample or freshness change.
  const mt::BatteryGauge& battery() const { return gauge_; }
  bool batterySample(hw::BatterySample& out) const {
    out = batSample_;
    return batFresh_;
  }
  uint32_t batterySeq() const { return batSeq_; }
  // Draws msg over the current frame and pushes it right away: call before blocking SD I/O.
  void showBusy(const char* msg);
  // showBusy("label NN%"), redrawn at most every 200 ms unless the label changes.
  void showProgress(const char* label, uint32_t done, uint32_t total);
  // storage::SyncProgress for sample sync / pull: "SAMPLE name NN%". ctx = App*.
  static void syncProgress(const char* file, uint32_t done, uint32_t total, void* app);
  // storage::RenderProgress: "RENDER NN%"; false (cancel) on a long encoder press or Play. ctx = App*.
  static bool renderProgress(uint32_t done, uint32_t total, void* app);
  // After a long operation: drops the input it queued meanwhile and the progress label.
  void endProgress();
  void dropInput(const hw::InputEvent& ev);
  // One toast after a load: what (e.g. "LOADED X", may be nullptr), missing samples, a failed sample
  // folder write, a dropped legacy song transpose (storage); leading parts are dropped when too long.
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
  static constexpr uint32_t kCpuMs = 500;   // status bar CPU: averaging window
  static constexpr uint32_t kCpuRedMs = 2000;  // status bar CPU: red held after an audio stall
  static constexpr uint32_t kBatMs = 250;        // battery poll (the reader samples once a second)
  static constexpr uint32_t kBatStaleMs = 5000;  // older sample = no data
  static constexpr int kTabW = kScreenW / static_cast<int>(Tab::Count);
  // Status bar hit areas.
  static constexpr int kModeX0 = 0, kModeX1 = 64;  // SONG / LOOP
  static constexpr int kBpmX0 = 64, kBpmX1 = 176;
  static constexpr int kTransX0 = 256, kTransX1 = 320;

  Screen* screen() { return screens_[static_cast<int>(tab_)]; }
  void onTouch(const TouchEvent& ev);
  void transport();
  void postSong(bool on);  // SetSong from the SONG cursor
  void trackKey(int n, bool shift);
  void trackRelease(int n);
  void fillDown();
  // Hold a track button + turn = its volume (encoder click while held: its pan, again: volume), here
  // and now.
  bool holdVolume() const;
  int heldTrack() const { return (curTrack_ / mt::kTrackButtons) * mt::kTrackButtons + heldTrackBtn_; }
  void nudgeTrackVol(int track, int d);
  void nudgeTrackPan(int track, int d);  // as nudgeTrackVol
  void holdToast(int track);             // "TRK3 VOL 100" / "TRK3 PAN L12" / "TRK3 MIDI"
  void fillUp();
  void saveSettingsIdle(uint32_t now);
  void autosaveIdle(uint32_t now);
  void pollCpu(uint32_t now);
  void pollBattery(uint32_t now);
  void setBpmEdit(bool on);
  void abAction(const mt::AbOut& o);  // A / B chord result
  void cycleTab(int d);
  void soloKey(int n);
  void queueKey(int n, bool shift);
  void draw();
  void drawStatus();
  void drawBattery();  // status bar, right of the loop counter
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
  SongScreen song_{*this};
  InstScreen inst_{*this};
  ProjScreen proj_{*this};
  FileScreen file_{*this};
  Screen* screens_[static_cast<int>(Tab::Count)] = {&grid_, &track_, &inst_, &track_, &bank_, &song_,
                                                       &proj_, &file_};
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
  mt::BatteryGauge gauge_;
  hw::BatterySample batSample_{0.f, 0.f, 0};  // last sample fed to gauge_
  bool batHave_ = false;     // batSample_ is valid
  uint32_t batAt_ = 0;       // last poll
  bool batFresh_ = false;    // batSample_ is no older than kBatStaleMs
  uint32_t batSeq_ = 0;      // bumped per new sample / freshness change
  int batPct_ = -1;          // percent (-1 when stale) and charging as last drawn
  bool batChg_ = false;
  int bpmTarget_ = 120;  // local while editing: p_->bpm lags behind the engine queue
  bool bpmEdit_ = false;
  int bpmOrig_ = 120;  // BPM when the edit began (Shift+click puts it back)
  uint8_t curTrack_ = 0;
  int theme_ = 0, savedTheme_ = 0;  // colour theme (device setting, NVS) and as stored
  int autosaveMin_ = 5, savedAutosaveMin_ = 5;  // autosave interval, minutes, 0 = off (device setting)
  int phones_ = 100, savedPhones_ = 100;  // phones level, % (device setting)
  bool preview_ = true, savedPreview_ = true;  // GRID note preview (device setting)
  uint32_t settingsChangedAt_ = 0;  // 0 = nothing pending
  uint32_t autosaveDue_ = 0;        // when the next autosave is due, 0 = not counting
  uint32_t autosavedSeq_ = 0;       // editSeq_ of the last autosave
  uint32_t lastInputMs_ = 0;
  uint32_t editSeq_ = 0, savedSeq_ = 0;
  bool shift_ = false;
  bool fillHeld_ = false;   // Shift + Play held while playing: fill
  mt::AbKeys ab_;  // buttons A / B
  int8_t heldTrackBtn_ = -1;  // track button held (0..7 of the visible half), -1 = none
  bool holdPan_ = false;      // the held button's turn sets the pan, not the volume (until released)
  uint32_t fillDownMs_ = 0;
  bool transportLocked_ = false;
  bool dirty_ = true;
  uint32_t lastDraw_ = 0;
};

}  // namespace ui
