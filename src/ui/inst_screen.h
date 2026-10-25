#pragma once
#include "bounce_dialog.h"
#include "model.h"
#include "mutate.h"
#include "param_list.h"
#include "preset_browser.h"
#include "rec_dialog.h"
#include "sample_editor.h"
#include "screen.h"
#include "wt_make_dialog.h"
#include "wt_picker.h"

namespace ui {

// Instrument editor. Shift+turn (or a tap on the header arrows) = instrument -+1; entering the tab
// selects the instrument of App::curTrack(). PREVIEW (tap, or a long press) plays C4 for 300 ms.
// PRESET (tap, or Shift + long press): LOAD / SAVE menu, then the PresetBrowser over the list.
// On the SMPL page that menu is SAMPLE: Chop, Add slice (Shift on the click: at a transient), Delete
// slice, Clear slices (confirm), Load / Save preset; A tap / Shift + A tap = next / previous marker.
// PRESET menu (not on a KIT): also Mutate 15 / 30 / 60 % (mutateSlot: filter, envelopes, macros of
// this instrument, seeded by esp_random) and Undo mutate (one step, this instrument only).
// Name: click to edit, turn = character, Shift+turn = position.
// Pages (tab bar under the header, or turning past the first / last row, wrapping): MAIN, ENV (with an
// ADSR graph), the type's own (OSC / SMPL / FM / DRUM / LOFI / RES; SYNTH: OSC and MOD), FILT, LFO. The page is
// kept across instruments and type changes (SYNTH MOD -> the type page of the others).
// SAMPLE type page: SampleEditor (waveform with markers, zoom, slices, the sample rows). Its SAMPLE menu
// starts with Record... (RecDialog over the work area); leaving the screen while a take runs = STOP.
// Save one-shot... (PRESET menu; SAMPLE: the SAMPLE menu; not KIT): BounceDialog over the work area.
// FM: machine, macros (DECAY..CONTOUR), SAT; ADSR rows grey where the machine ignores them.
// DRUM: machine, macros named per machine (grey "-" where unused); ADSR, mode and glide grey.
// LOFI: machine (BYTE / APC), Formula (BYTE: the library formula by name, turn = next / previous,
// wrapping, loads its default A B C; grey on APC), Sync (BYTE: t from the tempo, LOOP in steps; grey on
// APC), macros named per machine (grey "-" where unused; BYTE: A B C grey where the formula does not read
// them). ADSR, mode and glide as CHIP.
// SYNTH: six pages, MAIN / ENV / OSC / MOD / FILT / LFO. OSC: Osc1, Table1, Shape1 (SHP1), Osc2, Table2,
// Shape2 (SHP2), Semi2, Detune (DET), Sync, Mix (MIX), with the selected oscillator's waveform (WT: the
// frame under SHAPE) on the right; MOD: Sub, Sub oct, Noise, Ring, Fold (0 = OFF), Env>Shp (SENV),
// Env atk, Env dec. ADSR, mode and glide as CHIP. A click / tap on a Table row opens WtPicker; a missing table is shown red.
// STRING: MAIN / ENV / RES / FILT / LFO. RES: Model, Damp (as T60), Bright, Struct, Position (%), Excite
// (CLICK at 0, else the burst in ms). ADSR, mode and glide as SYNTH. The Filter row skips COMB.
// Filter and LFO rows on every type, after the type's own (CHIP / SAMPLE: no macro LFO targets).
// KIT: two pages, MAIN (Name, Type, Dly send) and LANES: per lane Src (SAMPLE / INST), then SAMPLE:
// Sample, Volume, Pitch, Decay, Note; INST: Instr (red when it is a KIT), Note. The other mode's rows
// are hidden. Lane notes stay unique: Note skips notes other lanes hold. No KIT presets.
class InstScreen : public Screen {
 public:
  explicit InstScreen(App& app);
  void onEnter() override;
  void onLeave() override;
  // The track buttons play slices (sample editor page): no hold-button volume then.
  bool buttonsBusy() const;
  void onProjectReplaced() override;
  void dropMutateUndo() { mut_.slot = -1; }  // Dice SOUNDS rewrote the instruments
  void onInput(const hw::InputEvent& ev) override;
  void onPage(int d) override;
  void onBack() override;
  void onATap(bool shift) override;
  void onTouch(const TouchEvent& ev) override;
  void draw(LGFX_Sprite& s, int y0, int h) override;
  bool wantsHDrag() const override {
    return !rec_.isOpen() &&
           (wt_.drawOpen() || (!presets_.isOpen() && !wt_.isOpen() && !wtMake_.isOpen() && !bounce_.isOpen() && onEditor()));
  }
  void poll() override { rec_.poll(); }
  bool wantsRedraw(const engine::Status&) override { return rec_.isOpen(); }  // the meter
  // Track button n: on the sample editor plays slices (see SampleEditor::playSlice). False = not taken.
  bool trackKey(int n, bool shift);

 private:
  // Rows of every type first, then the type's own. Plain ints: they are added across groups.
  static constexpr int kName = 0, kType = 1, kVol = 2, kTranspose = 3, kFine = 4, kMode = 5, kGlide = 6,
                       kSend = 7, kRsend = 8, kAttack = 9, kDecay = 10, kSustain = 11, kRelease = 12,
                       kVelCut = 13, kVelMac = 14, kCommon = 15;
  static constexpr int kMainRows = 9;  // MAIN = [0, 9), ENV = [9, kCommon)
  static constexpr int kWave = kCommon, kDuty = kCommon + 1, kPwmRate = kCommon + 2, kPwmDepth = kCommon + 3,
                       kChipRows = kCommon + 4;
  static constexpr int kMachine = kCommon, kMac0 = kCommon + 1, kMacRows = kMac0 + mt::kFmMacros;  // FM, DRUM
  static constexpr int kFmSat = kMacRows, kFmRows = kFmSat + 1;  // FM only: SAT after the macros (not DRUM)
  // LOFI: Machine (kMachine), Formula, Sync, then the macros.
  static constexpr int kFormula = kCommon + 1, kLofiSync = kCommon + 2, kLofiMac0 = kCommon + 3,
                       kLofiRows = kLofiMac0 + mt::kFmMacros;
  // SYNTH: OSC page rows, then MOD page rows.
  static constexpr int kOsc1 = kCommon, kTable1 = kCommon + 1, kShape1 = kCommon + 2, kOsc2 = kCommon + 3,
                       kTable2 = kCommon + 4, kShape2 = kCommon + 5, kSemi = kCommon + 6, kDetune = kCommon + 7,
                       kSync = kCommon + 8, kMix = kCommon + 9, kSynOscRows = 10;
  static constexpr int kSub = kOsc1 + kSynOscRows, kSubOct = kSub + 1, kNoise = kSub + 2, kRing = kSub + 3,
                       kFold = kSub + 4, kSenv = kSub + 5, kEAtk = kSub + 6, kEDec = kSub + 7, kSynRows = kSub + 8;
  // STRING: one RES page.
  static constexpr int kStrModel = kCommon, kStrDamp = kCommon + 1, kStrBright = kCommon + 2,
                       kStrStruct = kCommon + 3, kStrPos = kCommon + 4, kStrExc = kCommon + 5,
                       kStringRows = kCommon + 6;
  // Filter and LFO: after the type's own rows (index = the type's row count + tail row).
  static constexpr int kFltMode = 0, kCutoff = 1, kReso = 2, kFEnv = 3, kFAtk = 4, kFDec = 5, kKeytrack = 6,
                       kLfoSel = 7, kLfoWave = 8, kLfoSync = 9, kLfoRetrig = 10, kLfoRate = 11, kLfoDepth = 12,
                       kLfoDest = 13, kLfoFormula = 14, kLfoA = 15, kTailRows = 16;
  // tail: FILT = [0, 7) (drive and crush are the track's, TRACK -> INS), LFO
  static constexpr int kFiltRows = 7;
  // KIT: MAIN rows, then kLaneRows per lane.
  static constexpr int kKitMain = 4;  // Name, Type, Send, Rvb send
  static constexpr int kLaneSrc = 0, kLaneSample = 1, kLaneInstr = 2, kLaneVol = 3, kLanePitch = 4,
                       kLaneDecay = 5, kLaneNote = 6, kLaneRows = 7;
  static constexpr int kKitRows = kKitMain + mt::kKitLanes * kLaneRows;
  // Logical pages; kPgType2 (SYNTH MOD) only on SYNTH, so the others have one page less. KIT: MAIN and
  // kPgType (LANES) only.
  enum Page : int { kPgMain, kPgEnv, kPgType, kPgType2, kPgFilt, kPgLfo };
  static constexpr int kHeaderH = 28;
  static constexpr int kPageBarH = 24;
  static constexpr int kListRows = 9;  // (kAreaH - kHeaderH - kPageBarH) / ParamList::kRowH
  // ENV page ADSR graph, right of the values; y from the list top.
  static constexpr int kEnvX0 = 280, kEnvX1 = 470, kEnvY0 = 8, kEnvY1 = 112;
  static constexpr int kEnvHold = 30;  // sustain plateau width
  // OSC page (SYNTH) frame preview, right of the values (table names reach x 336); y from the list top.
  static constexpr int kWtX0 = 336, kWtX1 = 456, kWtY0 = 8, kWtY1 = 112;
  static constexpr int kNameLen = 8;
  static constexpr int kPreviewNote = 60;
  // Header hit areas: left arrow, right arrow, PRESET and PREVIEW buttons.
  static constexpr int kLeftX1 = 56;
  static constexpr int kRightX0 = 232, kRightX1 = 288;
  static constexpr int kPresetX0 = 296, kPresetX1 = 376;
  static constexpr int kPrevX0 = 384, kPrevX1 = 472;

  mt::Instrument& inst();
  void setType(mt::InstrType v);  // under the lock (a ParamList edit)
  const mt::Instrument& inst() const;
  void changeInstr(int d);
  void syncParams();  // rows of the current type
  Param* typeRows();  // row array of the current type
  int typeCount() const;  // the type's own rows
  bool onEditor() const { return shown_ == mt::InstrType::Sample && page_ == kPgType; }
  bool synth() const { return shown_ == mt::InstrType::Synth; }
  bool kit() const { return shown_ == mt::InstrType::Kit; }
  int pageCount() const { return kit() ? 2 : (synth() ? 6 : 5); }
  void initKit();
  void buildLanes(bool keep);  // LANES rows of the current lane modes into kitShown_; keep: scroll stays
  int pageW() const { return kScreenW / pageCount(); }
  int physPage() const;                // tab index of page_
  int logicalPage(int phys) const;     // Page of a tab index
  void showPage(int phys, bool bar);  // tab index, wraps; bar: the page bar keeps the cursor, else the first row
  int tableOsc() const;                // SYNTH OSC page on a Table row: its oscillator, else -1
  void drawPageBar(LGFX_Sprite& s, int y);
  void drawEnv(LGFX_Sprite& s, int y);  // ADSR graph, y = list top
  void drawOsc(LGFX_Sprite& s, int y);  // SYNTH OSC page: frame / waveform of the selected osc
  void initTail(Param* t);  // t = &rows[type's row count]
  void relabel();           // DRUM / LOFI macro labels of the current machine
  void leaveEdit();
  void fixNames();  // empty name -> INSn
  bool nameEdit() const { return page_ == kPgMain && list_.editing() && list_.sel() == kName; }
  void preview();
  void presetMenu();
  void mutate(int strength);  // mutateSlot under the lock, toast, preview
  void undoMutate();
  void afterPresets();  // the browser closed: the type may have changed
  void openTables(int osc);

  App& app_;
  Param chip_[kChipRows + kTailRows];
  Param sample_[kCommon + kTailRows];  // the type page is editor_
  Param fm_[kFmRows + kTailRows];
  Param drum_[kMacRows + kTailRows];
  Param lofi_[kLofiRows + kTailRows];
  Param syn_[kSynRows + kTailRows];
  Param string_[kStringRows + kTailRows];
  Param kit_[kKitRows];       // every KIT row
  Param kitShown_[kKitRows];  // LANES page: the rows of each lane's mode
  mt::InstrType shown_ = mt::InstrType::Chip;
  ParamList list_{kAreaY + kHeaderH + kPageBarH};
  int page_ = kPgMain;
  PresetBrowser presets_{app_};
  WtPicker wt_{app_};
  WtMakeDialog wtMake_{app_};
  BounceDialog bounce_{app_};  // Save one-shot...
  int instr_ = 0;
  int lfoSel_ = 0;  // LFO page: which of the 4 LFOs the rows edit
  mt::Instrument typeSnap_;     // the instrument as it was in the type being left (setType)
  int typeSnapInstr_ = -1;      // whose copy typeSnap_ is, -1 = none
  mt::MutateUndo mut_;          // the last Mutate (slot -1 = none); dropped on load / New, refused once the slot is edited
  int y0_ = kAreaY;
  int namePos_ = 0;
  SampleEditor editor_{app_, kAreaY + kHeaderH + kPageBarH};
  RecDialog rec_{app_};
};

}  // namespace ui
