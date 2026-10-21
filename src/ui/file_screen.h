#pragma once
#include "hw/sdcard.h"
#include "import_dialog.h"
#include "keyboard.h"
#include "render_dialog.h"
#include "page_bar.h"
#include "param_list.h"
#include "screen.h"
#include "wifi_dialog.h"

namespace ui {

// Three sections, switched in the header: PROJECTS (Save / Save As / Load / New / Import MIDI / Wi-Fi
// firmware; Load shows /projects/*.mtp), SAMPLES (the project's samples: import WAV from /samples and its
// subfolders, rename, delete; the flash bank is their cache: compact, clear unused entries) and SYSTEM
// (device settings saved in NVS, not in the project, and diagnostics: firmware, the synth board and its
// card, link, last reset, RAM, battery, CPU profile).
class FileScreen : public Screen {
 public:
  explicit FileScreen(App& app) : app_(app), import_(app), render_(app), wifi_(app) { initSystem(); }
  void onEnter() override;
  void onLeave() override;
  void onProjectReplaced() override;
  void onInput(const hw::InputEvent& ev) override;
  bool onPlay() override;
  void onBack() override;
  void onTouch(const TouchEvent& ev) override;
  void draw(LGFX_Sprite& s, int y0, int h) override;
  void onPage(int d) override;
  void poll() override { wifi_.poll(); }
  bool wantsRedraw(const engine::Status&) override;  // Wi-Fi, SYSTEM: Battery / CPU profile rows
  // FILE -> Wi-Fi as if chosen (asks about unsaved changes first); the synth version screen uses it.
  void openWifi() { run(kWifi); }
  bool wifiOpen() const { return wifi_.isOpen(); }
  // Update synth (also from the version-mismatch screen): /firmware/teensy.hex on the synth board's
  // card -> confirm -> FwFromFile -> its reboot.
  void askUpdateSynth();

 private:
  enum Action : int { kSave, kSaveAs, kLoad, kNew, kImport, kRender, kWifi, kRetry, kActions };
  enum MenuId : int {
    kCancel, kDiscardLoad, kDiscardNew, kRestoreAuto, kLoadBak, kOverwrite, kSaveWifi, kDiscardWifi, kOverwriteSample,
    kRenameSample, kDeleteSample, kDeleteUsed, kClearCache
  };
  enum Section : int { kSecProjects, kSecSamples, kSecSystem, kSections };
  static constexpr int kSectionSel = kActions;  // PROJECTS focus on the header section switch
  // SAMPLES rows: Import, Compact, Clear cache, then the project's samples.
  enum SampleRow : int { kSwitchRow = -1, kImportRow, kCompactRow, kClearRow, kFirstSample };
  static constexpr int kHeaderH = 28;
  static constexpr int kActionH = 27;  // kActions rows fit under the header and the page bar
  static constexpr int kRowH = 24;
  static constexpr int kMaxFiles = 512;
  static constexpr int kWavDepthMax = 4;  // subfolders below /samples
  static constexpr int kListRows = (kAreaH - kHeaderH) / kRowH;  // incl. the Back row
  static constexpr int kInfoH = 24;                                 // SAMPLES: free space bar
  static constexpr int kSampleRows = (kAreaH - kHeaderH - PageBar::kH - kInfoH) / kRowH;
  static_assert(kHeaderH + PageBar::kH + kActions * kActionH <= kAreaH, "FILE actions fit");
  // PROJECTS / SAMPLES / SYSTEM: the page bar under the header (also a focus position of the encoder:
  // kSectionSel in PROJECTS, kSwitchRow in SAMPLES, the list's page bar in SYSTEM).

  // SYSTEM rows.
  enum SysRow : int {
    kPhones, kPreview, kPerfVis, kTheme, kAutosave, kFirmware, kSynthFw, kUpdSynth, kCard, kSamples, kLink, kLastReset, kAudioRam, kBattery, kCpuProf, kSysRows
  };
  static constexpr int kSysListRows = (kAreaH - kHeaderH - PageBar::kH) / ParamList::kRowH;

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
  void startWifi();
  void listScroll(int rows);
  // Folder browsing (MIDI import from /midi, WAV import from /samples).
  bool browsing() const { return midiList_ || wavList_; }
  char* curDir() { return midiList_ ? midiDir_ : wavDir_; }
  const char* curDir() const { return midiList_ ? midiDir_ : wavDir_; }
  size_t curDirCap() const { return midiList_ ? sizeof(midiDir_) : sizeof(wavDir_); }
  const char* rootDir() const { return midiList_ ? "/midi" : "/samples"; }
  bool atRoot() const;
  void reopenList();  // lists curDir() again
  bool listUp();      // to the parent folder; false at the root
  void drawHeader(LGFX_Sprite& s, int y0);
  void setSection(int section);  // wraps around; the encoder focus goes to the page bar
  // SYSTEM
  void initSystem();
  void systemInput(const hw::InputEvent& ev);
  void systemTouch(const TouchEvent& ev);
  void toggleProfile();  // start, or stop and append the result to /diag/cpuprof.txt
  void sysAction(int row);  // a click on CPU profile / Update synth
  void updateSynth();
  uint32_t profSecs() const;  // seconds since the profile started
  // SAMPLES
  bool sampleEnabled(int row) const;
  int sampleRowCount() const;
  void sampleMove(int delta);
  void sampleRun(int row);
  void openWavList();
  // WAV preview in the import list (Play): streamed from the card by the synth board.
  void togglePreview();
  void stopPreview();
  void importAs(const char* initial);
  void doImport(const char* name);
  void renameSample(const char* initial);
  void doDelete(const char* name);
  void doCompact();
  void doClear();
  uint32_t cacheBytes();  // bank space of entries the project does not use
  bool playbackBusy();  // toasts STOP PLAYBACK FIRST
  int usedBy(const char* sample) const;  // first instrument playing it, or -1
  static void progress(uint32_t done, uint32_t total, void* ctx);  // ctx = this, label busyLabel_
  void samplesInput(const hw::InputEvent& ev);
  void samplesTouch(const TouchEvent& ev);
  void drawSamples(LGFX_Sprite& s, int top);

  App& app_;
  Keyboard kb_;
  ImportDialog import_;
  RenderDialog render_;
  WifiDialog wifi_;
  int sel_ = kSave;
  int y0_ = kAreaY;
  // Load / import list, PSRAM while open.
  char (*names_)[hw::kNameMax] = nullptr;
  int count_ = 0;
  int listSel_ = 0;  // 0 = Back, i + 1 = names_[i]
  int listTop_ = 0;
  int dragAcc_ = 0;
  bool midiList_ = false;  // names_ lists folders, then *.mid in midiDir_
  int dirCount_ = 0;       // folders at the start of names_
  char midiDir_[160] = "/midi";  // kept between imports
  char pending_[hw::kNameMax] = {0};  // target of a confirmation menu
  int section_ = kSecProjects;        // Section shown
  int ssel_ = kImportRow;             // SampleRow or kFirstSample + project sample index
  int stop_ = 0;                      // first visible SAMPLES row
  bool wavList_ = false;              // names_ lists folders, then *.wav in wavDir_
  char wavDir_[128] = "/samples";     // kept between imports
  char wavFile_[hw::kNameMax] = {0};  // WAV being imported
  int pvSel_ = -1;                    // listSel_ of the WAV previewed on the synth board, -1 none
  uint32_t pvEndMs_ = 0;              // millis() when it has played to its end
  const char* busyLabel_ = "";
  // New: template menu ids are the built-in index, kTplUser + k (userTplNames_[k]), kTplSave,
  // kTplDemos (the demo songs menu) or kTplDemo + i (demo song i).
  static constexpr int kUserTpl = 8, kTplUser = 100, kTplSave = 200, kTplDemos = 300, kTplDemo = 400;
  void openNewMenu();
  void onNewChoice(int id);  // a New menu id (see kTplUser ..)
  void saveTemplateAs();
  int newChoice_ = 0;
  int userTpl_ = 0;
  char userTplNames_[kUserTpl][17] = {};
  char userTplLabels_[kUserTpl][20] = {};
  bool autoAvail_ = false;  // /projects/<name>.auto exists (checked on enter / after file actions)
  void restoreAutosave();
  Param sysParams_[kSysRows];
  ParamList sysList_{kAreaY + kHeaderH + PageBar::kH};
  uint32_t profStartMs_ = 0;
  uint32_t batSeqDrawn_ = 0;    // App::batterySeq() at the last SYSTEM draw
  uint32_t profSecDrawn_ = 0;   // profSecs() at the last SYSTEM draw
};

}  // namespace ui
