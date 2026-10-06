#pragma once
#include "hw/sdcard.h"
#include "import_dialog.h"
#include "keyboard.h"
#include "render_dialog.h"
#include "screen.h"
#include "wifi_dialog.h"

namespace ui {

// Two sections, switched in the header: PROJECTS (Save / Save As / Load / New / Import MIDI / Wi-Fi
// transfer; Load shows /projects/*.mtp) and SAMPLES (the project's samples: import WAV from /samples and its
// subfolders, rename, delete; the flash bank is their cache: compact, clear unused entries).
class FileScreen : public Screen {
 public:
  explicit FileScreen(App& app) : app_(app), import_(app), render_(app), wifi_(app) {}
  void onEnter() override;
  void onLeave() override;
  void onProjectReplaced() override;
  void onInput(const hw::InputEvent& ev) override;
  bool onPlay() override;
  void onTouch(const TouchEvent& ev) override;
  void draw(LGFX_Sprite& s, int y0, int h) override;
  void poll() override { wifi_.poll(); }
  bool wantsRedraw(const engine::Status&) override { return wifi_.wantsRedraw(); }

 private:
  enum Action : int { kSave, kSaveAs, kLoad, kNew, kImport, kRender, kWifi, kRetry, kActions };
  enum MenuId : int {
    kCancel, kDiscardLoad, kDiscardNew, kLoadBak, kOverwrite, kSaveWifi, kDiscardWifi, kOverwriteSample,
    kRenameSample, kDeleteSample, kDeleteUsed, kClearCache
  };
  static constexpr int kSectionSel = kActions;  // PROJECTS focus on the header section switch
  // SAMPLES rows: Import, Compact, Clear cache, then the project's samples.
  enum SampleRow : int { kSwitchRow = -1, kImportRow, kCompactRow, kClearRow, kFirstSample };
  static constexpr int kHeaderH = 28;
  static constexpr int kActionH = 30;  // kActions rows fit under the header
  static constexpr int kRowH = 24;
  static constexpr int kMaxFiles = 512;
  static constexpr int kWavDepthMax = 4;  // subfolders below /samples
  static constexpr int kListRows = (kAreaH - kHeaderH) / kRowH;  // incl. the Back row
  static constexpr int kInfoH = 24;                                 // SAMPLES: free space bar
  static constexpr int kSampleRows = (kAreaH - kHeaderH - kInfoH) / kRowH;
  static_assert(kHeaderH + kActions * kActionH <= kAreaH, "FILE actions fit");
  static constexpr int kSwitchX = 208;  // "PROJECTS | SAMPLES" in the header
  static constexpr int kSwitchW = 18 * kCharW;

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
  void setSection(bool samples);
  // SAMPLES
  bool sampleEnabled(int row) const;
  int sampleRowCount() const;
  void sampleMove(int delta);
  void sampleRun(int row);
  void openWavList();
  // WAV preview in the import list (Play): stopPreview() also frees the buffer.
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
  bool samples_ = false;              // SAMPLES section shown
  int ssel_ = kImportRow;             // SampleRow or kFirstSample + project sample index
  int stop_ = 0;                      // first visible SAMPLES row
  bool wavList_ = false;              // names_ lists folders, then *.wav in wavDir_
  char wavDir_[128] = "/samples";     // kept between imports
  char wavFile_[hw::kNameMax] = {0};  // WAV being imported
  int16_t* pvBuf_ = nullptr;          // previewed WAV, PSRAM
  static constexpr int kPvLeft = 4;
  int16_t* pvLeft_[kPvLeft] = {};     // buffers whose stop the audio task did not acknowledge yet
  int pvLeftN_ = 0;
  int pvSel_ = -1;                    // listSel_ it belongs to
  const char* busyLabel_ = "";
  // cacheBytes() result for this bank generation / project edit.
  uint32_t cacheBytes_ = 0, cacheGen_ = 0, cacheSeq_ = 0;
  bool cacheValid_ = false;
};

}  // namespace ui
