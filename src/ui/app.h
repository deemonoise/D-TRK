#pragma once
#include "shift_gen.h"
#include "ab_keys.h"
#include "bank_screen.h"
#include "battery_gauge.h"
#include "edit_ops.h"
#include "engine/engine.h"
#include "file_screen.h"
#include "grid_screen.h"
#include "automation.h"
#include "hw/battery.h"
#include "ble_midi_parse.h"
#include "midi_devices.h"
#include "midi_router.h"
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "inst_screen.h"
#include "menu.h"
#include "model.h"
#include "perf_pad.h"
#include "perf_vis.h"
#include "proj_screen.h"
#include "rec_dialog.h"
#include "screen.h"
#include "song_screen.h"
#include "touch.h"
#include "track_screen.h"
#include "undo.h"
#include "xy_pad.h"

namespace ui {

enum class Tab : uint8_t { Grid, Track, Inst, Mix, Bank, Song, Proj, File, Count };  // MIX: TRACK's mixer view

class App {
 public:
  void begin(LGFX* lcd, mt::Project* p);
  void onInput(const hw::InputEvent& ev);
  bool automationMuted(int track) const { return track >= 0 && track < mt::kTracks && ((automationMute_ >> track) & 1); }
  void toggleAutomationMute(int track);
  bool recordPermit() const;
  void syncRecordArm();
  void setRecording(bool notes, bool motion);
  void openRecordingSetup(bool child = false, bool refresh = false);
  void setPracticePrefs(mt::PracticePrefs prefs);
  bool motionRecording() const { return motionRec_; }
  void setMotionRecording(bool on);
  void motionObserve(bool before, int track = -1);
  void pollMotion();
  void tick();  // touch, engine status, redraw (at most every kFrameMs)
  void setTab(Tab t);
  Tab tab() const { return tab_; }
  Menu& menu() { return menu_; }
  mt::Project& project() { return *p_; }
  const mt::Project& project() const { return *p_; }
  const engine::Status& status() const { return status_; }
  bool shift() const { return shift_; }
  void invalidate() { dirty_ = true; }
  uint8_t editPattern() const { return status_.pattern; }
  // SONG plays (or is paused): the sequencer ignores QueuePattern.
  bool songPlays() const { return status_.song && p_->songLen > 0 && (status_.playing || status_.paused); }
  // Track shared by GRID and TRACK screens.
  int curTrack() const { return curTrack_; }
  void setCurTrack(int t) { curTrack_ = static_cast<uint8_t>(((t % mt::kTracks) + mt::kTracks) % mt::kTracks); }
  // PERF, shared by GRID, SONG and MIX (the tabs that play it).
  PerfPad& perf() { return perf_; }
  InstScreen& inst() { return inst_; }
  // The one recorder (SAMPLE page and MIX): polled here every frame, shown by the screen that opened it.
  RecDialog& rec() { return rec_; }
  // A device setting changed outside the setters below (the REC window's): saved once it stays put.
  void settingsChanged() { settingsChangedAt_ = millis() | 1; }

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
  // Perf visuals: held PERF effects show on the whole screen (device setting).
  bool perfVisuals() const { return visuals_; }
  void setPerfVisuals(bool on);
  void pushUndo() { pushUndo(editPattern()); }  // snapshot editPattern() before an edit
  void pushUndo(uint8_t pattern, int genTrack = -1);  // no lock taken: callers may hold it; genTrack: its GenCfg too
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
  // The same with its own label (e.g. "EXPORT 1/2"). total 0: the length is unknown (one-shot), done is
  // the frames rendered: "LABEL 1.2s".
  static bool labeledProgress(const char* label, uint32_t done, uint32_t total, void* app);
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

  // BLE MIDI controller (MIDI dialog): the router (learn arming), the learned pad banks, and the
  // last kMonitor IN messages oldest first (n = count); monitorSeq() bumps with every message.
  static constexpr int kMonitor = 8;
  bool midiRecHold() const { return midiRecHold_; }
  void setMidiRecHold(bool on);
  mt::MidiRouter& midiRouter() { return midi_; }
  // The active IN device's learned pad banks (all unlearned when there is none).
  const mt::PadLearn& pads() const;
  void padsForget(mt::PadMode m);  // that bank not learned (saved); the pads sounding end
  // The remembered BLE MIDI devices (UI task only). After any change: devicesChanged() saves the list
  // and hands the active IN / OUT to hw::ble; a new active IN ends the sounding pads.
  mt::MidiDevices& devices() { return devs_; }
  void devicesChanged();
  void devicesSave();  // the list to NVS only (a pad table or name edit: hw::ble and the router keep going)
  // The active IN / OUT to hw::ble without saving; a new active IN ends the sounding pads and drops
  // the old controller's queued messages.
  void applyDevices();
  void padsAllOff();  // every sounding NOTE pad and held PERF pad ends
  // A bind edited / deleted outside a learn (MIDI dialog): a pending learn of that slot (its rel still
  // to decide) ends; -1 = every slot.
  void knobEdited(int slot) {
    if (slot < 0 || slot == learnSlot_) learnSlot_ = -1;
  }
  bool padsHeld() const;  // a NOTE pad sounds or a PERF pad is held
  uint32_t devNamesGen() const { return devNamesGen_; }  // bumps when a device takes its GAP name
  uint32_t midiConnects() const { return bleConnects_; }  // the IN connection pollMidi saw last
  int monitor(mt::MidiMsg* out, int max) const;
  uint32_t monitorSeq() const { return monSeq_; }

 private:
  static constexpr uint32_t kFrameMs = 25;
  static constexpr uint32_t kToastMs = 1500;
  static constexpr uint32_t kCpuMs = 500;   // status bar CPU: averaging window
  static constexpr uint32_t kCpuRedMs = 2000;  // status bar CPU: red held after an audio stall
  static constexpr uint32_t kSyncWaitMs = 10000;  // Play waits this long at most for the synth's sound state
  static constexpr uint32_t kSyncShowMs = 300;    // the status bar shows SYNC after this long unsynced
  static constexpr uint32_t kBatMs = 250;        // battery poll (the reader samples once a second)
  static constexpr uint32_t kBatStaleMs = 5000;  // older sample = no data
  static constexpr int kTabW = kScreenW / static_cast<int>(Tab::Count);
  // Status bar hit areas.
  static constexpr int kModeX0 = 0, kModeX1 = 64;  // SONG / LOOP
  static constexpr int kBpmX0 = 64, kBpmX1 = 176;
  static constexpr int kTransX0 = 256, kTransX1 = 320;
  static constexpr int kRecX0 = 384;  // a REC -> SD take: "REC mm:ss" over the battery % and CPU, to the end
  // Synth version screen: its Wi-Fi button.
  static constexpr int kBarrierBtnW = 148, kBarrierBtnH = 36;
  static constexpr int kBarrierBtnY = kAreaY + 176;

  Screen* screen() { return screens_[static_cast<int>(tab_)]; }
  static bool perfTab(Tab t) { return t == Tab::Grid || t == Tab::Song || t == Tab::Mix; }
  void onTouch(const TouchEvent& ev);
  void transport();
  void recordShortcut(bool motionOnly);
  void postSong(bool on);  // SetSong from the SONG cursor
  void startSong();
  void postStart(engine::Cmd c);  // a start, held back until the synth board is synced
  void trackKey(int n, bool shift);
  void trackRelease(int n);
  void fillDown();
  // Hold a track button + turn = its volume (encoder click while held: its pan, again: volume), here
  // and now.
  bool holdVolume() const;
  bool sceneKeys() const;  // MIX SCENE view takes the hold / B + turn / A + click chords
  // Held PERF effects show on the screen (PerfVis) instead of a plain pushSprite.
  bool perfVisible() const { return visuals_ && perf_.heldCount() > 0 && perfTab(tab_); }
  // The XY pad covers the work area: PERF's XY on a PERF tab.
  bool xyMode() const { return perf_.xy() && perfTab(tab_); }
  void toggleXy();                      // A + B
  void xyTouch(const TouchEvent& ev);   // raw Down / Move / Up on the pad
  int heldTrack() const { return (curTrack_ / mt::kTrackButtons) * mt::kTrackButtons + heldTrackBtn_; }
  void nudgeTrackVol(int track, int d);
  void nudgeTrackPan(int track, int d);  // as nudgeTrackVol
  void nudgeTrackFlip(int track, int d);  // the track generator's Flip, as nudgeTrackVol
  void holdToast(int track);             // "TRK3 VOL 100" / "TRK3 PAN L12" / "TRK3 FLIP 30%" / "TRK3 MIDI"
  void fillUp();
  void saveSettingsIdle(uint32_t now);
  void autosaveIdle(uint32_t now);
  void pollCpu(uint32_t now);
  void pollSynth(uint32_t now);
  // The synth board answers but speaks another protocol: the work area shows how to update it.
  bool versionBarrier();
  void barrierInput(const hw::InputEvent& ev);
  void drawBarrier(int y0);
  void pollBattery(uint32_t now);
  void setBpmEdit(bool on);
  void abAction(const mt::AbOut& o);  // A / B chord result
  void cycleTab(int d);
  void soloKey(int n);
  void queueKey(int n);
  void queuePattern(int pat);  // "PATTERN nn [NEXT]" (SONG plays: "SONG MODE")
  void toggleMute(int track);  // with a toast
  // BLE MIDI controller: drains its IN queue every tick (pads, learn); a lost connection ends what
  // the pads hold.
  void pollMidi();
  void midiAction(const mt::Action& a, const mt::MidiMsg& m);  // m: the message routed into a
  // Knobs (KnobBind::screen: 1 INST, 2 TRACK / MIX, 3 PROJ). Shift + turn on a selected row learns the
  // knob (rebinds a bound one; the row it is bound to already: unbinds), once per turn (not per Shift
  // press: the turn's first Shift message); the rest of that turn edits nothing. Else a bound knob turns
  // its row for its track, whatever the tab.
  static constexpr uint32_t kTurnGapMs = 300;  // a pause this long between a knob's messages ends its turn
  int knobScreenId(const Screen* s) const;
  Screen* knobScreen(int id);
  void knobLearn(const mt::MidiMsg& m);
  void knobTurn(int bind, int delta);
  bool knobRelDecide(const mt::MidiMsg& m);  // a pending learn's rel; true = m taken
  void knobNote(const char* msg);  // a toast at most once a second (hints, skipped turns)
  void padNoteOn(int note, uint8_t vel, uint8_t ch);
  void padNoteOff(int note, uint8_t ch);
  void releaseMidiSlot(int slot);
  void monitorPush(const mt::MidiMsg& m);
  void draw();
  void drawStatus();
  void drawBattery();  // status bar, right of the loop counter
  int btMark() const;  // status bar BT: 0 none, 1 searching, 2 every set slot connected
  void drawRec();      // status bar: a REC -> SD take's red dot and time
  void drawTabs();
  void drawBusy();

  LGFX* lcd_ = nullptr;
  LGFX_Sprite* spr_ = nullptr;
  mt::Project* p_ = nullptr;
  engine::Status status_{};
  TouchTracker touch_;
  Menu menu_;
  PerfPad perf_{*this};
  PerfVis vis_;
  XyPad xyPad_;

  uint16_t automationMute_ = 0;
  mt::PracticePrefs practicePrefs_;
  uint8_t savedPracticeBits_ = 0, savedClickLevel_ = 64;
  bool recordBatch_ = false;
  uint32_t recordDoneSeen_ = 0;
  bool motionRec_ = false, motionObserving_ = false;
  int motionTrack_ = 0, motionPattern_ = -1, motionInstr_ = 0;
  mt::InstrType motionType_ = mt::InstrType::Chip;
  uint32_t motionLoop_ = UINT32_MAX;
  int16_t motionBefore_[mt::kAutomationParams]{};
  mt::MotionLatch motion_[mt::kTracks];
  GridScreen grid_{*this};
  TrackScreen track_{*this};
  BankScreen bank_{*this};
  SongScreen song_{*this};
  InstScreen inst_{*this};
  ProjScreen proj_{*this};
  FileScreen file_{*this};
  RecDialog rec_{*this};
  mt::RecPrefs savedRec_;     // the REC window's settings as stored (device settings)
  uint32_t recShownSec_ = 0;  // the status bar's REC time as drawn
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
  uint32_t sdRedUntil_ = 0;   // millis() until which the CPU field shows SD! (a sample voice underran)
  bool sdShown_ = false;      // the CPU field shows SD!
  enum class Synth : uint8_t { Ok, Sync, Down, Mismatch };
  Synth synth_ = Synth::Ok;  // the synth board as the status bar shows it
  int8_t playWait_ = -1;     // a start (engine::Cmd) held back until the synth is synced, -1 = none
  uint32_t playWaitAt_ = 0;
  uint32_t syncSince_ = 0;  // millis() the synth board fell behind the model, 0: in sync
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
  int bpmOrig_ = 120;  // project BPM when the edit began (Shift+click puts it back)
  bool bpmTurned_ = false;  // the edit changed the BPM
  uint8_t curTrack_ = 0;
  int theme_ = 0, savedTheme_ = 0;  // colour theme (device setting, NVS) and as stored
  int autosaveMin_ = 5, savedAutosaveMin_ = 5;  // autosave interval, minutes, 0 = off (device setting)
  int phones_ = 100, savedPhones_ = 100;  // phones level, % (device setting)
  bool preview_ = true, savedPreview_ = true;  // GRID note preview (device setting)
  bool visuals_ = true, savedVisuals_ = true;  // perf visuals (device setting)
  uint32_t settingsChangedAt_ = 0;  // 0 = nothing pending
  uint32_t autosaveDue_ = 0;        // when the next autosave is due, 0 = not counting
  uint32_t autosavedSeq_ = 0;       // editSeq_ of the last autosave
  uint32_t lastInputMs_ = 0;
  uint32_t editSeq_ = 0, savedSeq_ = 0;
  bool shift_ = false;
  bool fillHeld_ = false;   // Shift + Play held while playing: fill
  mt::AbKeys ab_;  // buttons A / B
  int8_t heldTrackBtn_ = -1;  // track button held (0..7 of the visible half), -1 = none
  mt::HoldMode holdMode_ = mt::HoldMode::Vol;  // what the held button's turn sets (until released)
  uint32_t fillDownMs_ = 0;
  bool transportLocked_ = false;
  bool dirty_ = true;
  uint32_t lastDraw_ = 0;

  mt::MidiRouter midi_;
  mt::PadLearn* padsMut();  // the active IN device's banks, a scratch set when there is none
  void adoptBleNames();  // address-named devices take the connected GAP name
  mt::MidiDevices devs_;
  char activeIn_[mt::kDevAddr] = {};  // the active IN address hw::ble got last ("" = none)
  bool panicPending_ = false;  // a MIDI panic for a fresh OUT the engine queue refused: retried
  uint32_t bleConnects_ = 0;  // hw::ble::connects() last seen
  int btDrawn_ = 0;           // btMark() as last drawn
  uint8_t turnCh_ = 0xFF, turnCc_ = 0xFF;  // the knob turned last and its last message
  uint32_t turnMs_ = 0;
  bool turnLearned_ = false;  // this turn learned / unbound already
  // The bind learned last, until its knob's next message decides rel. Learning another knob before that
  // drops the decision (the first stays absolute; relearning it fixes that).
  int8_t learnSlot_ = -1;
  uint32_t devNamesGen_ = 0;
  uint8_t learnVal_ = 0;      // the value it was learned from
  uint32_t knobHintMs_ = 0;   // the last "SHIFT+TURN TO LEARN" toast
  // What each NOTE pad started (its release ends that note even after the octave or the current
  // track changed); padTrack_ -1 = not sounding.
  static constexpr int kLiveNotes = 32;
  uint8_t padChannel_[kLiveNotes] = {};
  bool midiRecHold_ = false, savedMidiRecHold_ = false;
  struct PadRecording {
    bool active = false;
    uint8_t pattern = 0, track = 0, step = 0;
    mt::Step written;
    uint64_t started = 0;
    uint32_t stepUs = 0;
  } padRec_[kLiveNotes];
  void finishPadRecording(int pad);
  uint8_t padNote_[kLiveNotes] = {};
  int8_t padTrack_[kLiveNotes] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
  mt::MidiMsg mon_[kMonitor] = {};
  uint8_t monHead_ = 0, monCount_ = 0;  // ring: next slot, messages in it
  uint32_t monSeq_ = 0;
};

}  // namespace ui
