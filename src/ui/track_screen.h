#pragma once
#include "model.h"
#include "page_bar.h"
#include "param_list.h"
#include "screen.h"

namespace ui {

// Settings of App::curTrack(). GEN page: the track generator (shift_gen.h); click on Seed = a new random seed. MIDI-only rows are grey on an INT track. Shift+turn (or a tap on the header arrows) = track -+1.
// Name: click to edit, turn = character, Shift+turn = position.
// INS page, Steps (the trance gate's 16-step mask): click = edit; turn = cursor, Shift + turn =
// rotate the mask, A + turn = toggle at the cursor and step on, a tap on a cell or track button 1-8
// (the cursor's half: steps 1-8 / 9-16) = toggle; click / B = done, cancel = the mask as it was.
// While playing, the gate's current step is highlighted.
// MIXER view (the MIX tab, setMixer): 8 strips of the half holding the cursor (A = 1-8, B = 9-16, as
// GRID's Overview), the master strip MAIN and the output scope below. Strip: name, volume fader
// (INT), pan bar (INT), delay / reverb send of its instrument (read only), M / S. Turn = master
// volume, Shift + turn = the other half, A + turn = master volume (Shift = x10); a tap on a pan bar
// selects it: turn / A + turn then change that track's pan instead (tap again, B, Shift + turn or
// leaving the tab drop it). A track button held + turn = that track's volume (+ encoder click: its
// pan), Shift + track button = mute (App, every screen). Touch: the fader follows the finger, M / S
// toggle (solo only here).
// Crossfader (scene.h) under the strips: `A3 ---o--- B5`. Tap / drag on the bar = position (a tap also
// selects it: turn / A + turn move it, Shift x8); a tap on A3 / B5 selects that end (turn: scene 1..8);
// B, a second tap or leaving the tab drop the selection. Long tap on the bar: the SCENE view.
// SCENE view: the strips show one parameter of the edited scene for the 8 tracks (--- = unset), the
// master strip one master parameter. Turn = the cursor track's value, Shift + turn = the parameter,
// A + turn = the master value (Shift x8), drag a fader = set, Shift + tap = unset, a held track button +
// turn = that track's value, a track button = edit scene N; tap a strip name = cursor, MAIN = the next
// master parameter. Row buttons: CAP (VOL + PAN of the INT tracks), CLR, CPY (then a track button N),
// END. While open, the fader parks on the edited scene's end (when it is A or B).
class TrackScreen : public Screen {
 public:
  explicit TrackScreen(App& app);
  void onEnter() override;
  void onLeave() override;
  void onInput(const hw::InputEvent& ev) override;
  void onTouch(const TouchEvent& ev) override;
  void onPage(int d) override;
  void onBack() override;
  void draw(LGFX_Sprite& s, int y0, int h) override;
  bool wantsRedraw(const engine::Status& st) override;
  // MIX tab (true) or TRACK tab (false): the mixer view or the track settings.
  void setMixer(bool on);
  // Track buttons while the Steps row is edited: toggle a step of the cursor's half (Shift: not taken).
  bool trackKey(int n, bool shift);
  bool buttonsBusy() const { return (!mixer_ && stepsEdit()) || sceneView(); }
  bool sceneView() const { return mixer_ && scnView_; }
  void sceneHoldTurn(int track, int d);  // a held track button + turn in the SCENE view
  void trackUp(int n);                   // track button n released (MIX)

 private:
  // Rows in page order: MAIN, INS (the INT track's insert chain, then its MOD send), FOL (its envelope follower), NOTE,
  // MIDI (contiguous runs, see
  // kPageFirst).
  enum Row : int { kName, kOut, kInstr, kVol, kPan, kMute, kSolo, kDrive, kDriveType, kBitCrush, kDownsample,
                   kTrance, kTrPattern, kTrSteps, kTrRate, kTrLen, kTrAtk, kTrRel, kModSend,
                   kFolSrc, kFolDest, kFolAmt, kFolAtk, kFolRel, kVel,
                   kGate, kPatLen, kSpeed, kHumanize,
                   kGenMode, kGenLen, kGenFlip, kGenLow, kGenOct, kGenDens, kGenSeed, kGenReg,
                   kChannel, kCcA, kCcB, kProgram, kRows };
  enum Page : int { kPgMain, kPgIns, kPgFol, kPgNote, kPgGen, kPgMidi, kPages };
  static constexpr int kPageFirst[kPages + 1] = {kName, kDrive, kFolSrc, kVel, kGenMode, kChannel, kRows};
  static constexpr int kVisibleRows = 9;  // (kAreaH - kHeaderH - PageBar::kH) / ParamList::kRowH
  static constexpr int kHeaderH = 28;
  static constexpr int kArrowW = 96;  // header hit area on each side
  static constexpr int kNameLen = 8;

  mt::TrackCfg& cfg();
  bool internal() { return cfg().out == mt::TrackOut::Int; }
  void changeTrack(int d);
  void leaveEdit();
  void fixNames();  // empty name -> TRKn
  bool nameEdit() const { return page_ == kPgMain && list_.editing() && list_.sel() == kName; }
  // The trance gate's Steps row (list index on the INS page) and its editor.
  static constexpr int kStepsRow = kTrSteps - kDrive;
  static constexpr int kCellPitch = 15, kCellW = 12, kGroupGap = 4;
  bool stepsEdit() const { return page_ == kPgIns && list_.editing() && list_.sel() == kStepsRow; }
  static int cellX(int i) { return ParamList::kValueX + i * kCellPitch + (i / 4) * kGroupGap; }
  void syncSteps();          // notes the mask when a Steps edit begins (cancel restores it)
  void toggleStep(int i);    // takes the lock
  void rotateSteps(int d);   // takes the lock
  int gateStep();            // the gate's current step while playing, -1 = none
  void drawSteps(LGFX_Sprite& s);
  int gateCur_ = 0;
  bool stepsActive_ = false;
  uint16_t stepsOrig_ = 0;
  int shownGateStep_ = -1;
  // FOL page: the follower's envelope as a bar in the header (redrawn when it moves a pixel).
  static constexpr int kFolBarW = 64;
  int folBarPx() const;
  void drawFolBar(LGFX_Sprite& s, int y0);
  int shownFolPx_ = -1;
  // GEN page: the track generator (Project::gen). Register row: the bits as cells (bit 0, the newest, left),
  // the note it gives in the header; a flipped newest bit blinks yellow for kFlipMs.
  static constexpr int kGenRegRow = kGenReg - kGenMode, kGenSeedRow = kGenSeed - kGenMode;
  static constexpr uint32_t kFlipMs = 100;
  mt::GenCfg& gen();
  bool genDim();  // a drum track or Gen OFF
  void genEdited();  // under lock: the engine replans
  void reseed();     // a random seed (click on Seed)
  mt::GenState heardGen();
  void drawGen(LGFX_Sprite& s, int y0);
  uint32_t shownGen_ = 0xFFFFFFFF, shownGenN_ = 0, flipAt_ = 0;
  bool flipShown_ = false;
  void showPage(int page, bool bar);  // bar: the page bar keeps the cursor, else the first row
  void setPageRows(int page, bool bar);  // the list only: safe in the constructor
  int page_ = kPgMain;
  void editName(int delta);  // under lock
  // MIXER: 8 + 8 x 52 + 8 + 48 = 480.
  static constexpr int kStrips = 8, kStripW = 52, kStripX0 = 8;
  static constexpr int kMasterW = 48, kMasterX = kStripX0 + kStrips * kStripW + 8;  // 432
  static constexpr int kMaster = kStrips;  // hitStrip: the master strip
  static constexpr int kNameY = 2, kFaderY = 16, kFaderH = 64, kFaderW = 12, kValY = 84;
  // Pan row: the text at kPanY - 2 and the bar (2 px line, 4 px tick) under it, at kPanY + kPanBarDy.
  static constexpr int kPanY = 100, kPanBarDy = 13;
  static constexpr int kSendY = 116, kSendDy = 16, kBtnY = 150, kBtnW = 22, kBtnH = 16;
  // Scope under the strips: the waveform (auto gain) and a peak meter with CLIP.
  static constexpr int kXfY = kBtnY + kBtnH + 4, kXfH = 16;
  static constexpr int kXfLabW = 24, kXfBarX0 = kStripX0 + kXfLabW + 6, kXfBarX1 = kScreenW - kStripX0 - kXfLabW - 6;
  static constexpr int kScopeY = kXfY + kXfH + 4, kScopeH = kAreaH - kScopeY - 4, kMeterW = 10;
  static constexpr uint32_t kScopeMs = 50;  // ~20 frames a second while the MIX tab is shown
  void drawScope(LGFX_Sprite& s, int y0);
  uint32_t scopeMs_ = 0, clipMs_ = 0;
  int meter_ = 0;
  float scopeGain_ = 1;  // auto gain, follows the waveform's peak
  // Track level meters beside the faders: -48..0 dB as 0..1, fast up, falling ~1.2 per second.
  static constexpr int kMeterBarW = 5;
  void updateMeters();
  float level_[mt::kTracks] = {};
  uint32_t clipAt_[mt::kTracks] = {};  // millis() | 1 of the last full-scale peak, held 1 s
  enum class Part : uint8_t { Name, Fader, Pan, Mute, Solo, Perf };  // Perf: the master column's box
  int firstTrack() const;
  void mixerInput(const hw::InputEvent& ev);
  void mixerTouch(const TouchEvent& ev);
  void drawMixer(LGFX_Sprite& s, int y0);
  void drawFader(LGFX_Sprite& s, int x, int y, int value, int max, uint16_t fill);
  bool hitStrip(int x, int y, int& strip, Part& part) const;
  void setVol(int track, int v);
  void setPan(int track, int v);
  void setMasterVol(int v);  // project data: saved with it, in Render WAV
  void toggleMuteSolo(int track, bool solo);
  void togglePerf();  // MIX: B (no pan bar selected) or a tap on the PERF box
  uint32_t mixSignature() const;
  bool mixer_ = false;
  int panSel_ = -1;  // track whose pan bar the encoder turns, -1 = none (the encoder turns MAIN)
  // Crossfader: what the encoder turns (kXfNone: MAIN / the pan as before).
  enum XfSel : int8_t { kXfNone, kXfFader, kXfEndA, kXfEndB };
  int xfSel_ = kXfNone;
  uint32_t xfMs_ = 0;  // the last drag write (at most one per 10 ms)
  void setXfade(int v);
  void setSceneEnd(bool b, int idx);
  bool xfTouch(const TouchEvent& ev);  // true: the row took it
  void drawXfade(LGFX_Sprite& s, int y0);
  // SCENE view.
  bool scnView_ = false, scnCopy_ = false;
  int scnEdit_ = 0, scnPar_ = 0, scnMst_ = 0;
  int xfSaved_ = -1;  // the fader before the view parked it
  int scnPending_ = -1;  // a track button pressed in the SCENE view, acted on at its release
  void setSceneView(bool on);
  void parkFader();
  void scnSetTrk(int track, int v);  // clamped, sets the lock
  void scnNudgeTrk(int track, int d);  // from the base when unset
  void scnUnsetTrk(int track);
  void scnNudgeMst(int d);
  void scnEdited();  // live sync + dirty
  void sceneInput(const hw::InputEvent& ev);
  void sceneTouch(const TouchEvent& ev);
  void drawScene(LGFX_Sprite& s, int y0);
  uint32_t mixSig_ = 0;

  App& app_;
  Param params_[kRows];
  int editTrack_ = 0;       // the track the list shows (followTrack)
  void followTrack();
  int patLenPat_ = -1;      // the pattern of that run
  uint32_t patLenSeq_ = 0;  // App::editSeq() right after the last Pat len edit: one undo snapshot per run
  int speedPat_ = -1;       // the same for Speed
  uint32_t speedSeq_ = 0;
  ParamList list_{kAreaY + kHeaderH + PageBar::kH};
  int y0_ = kAreaY;
  int namePos_ = 0;
};

}  // namespace ui
