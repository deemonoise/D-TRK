#include "inst_screen.h"
#include "ui_context.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <esp_random.h>
#include "app.h"
#include "page_bar.h"
#include "audio/audio.h"
#include "audio/bank.h"
#include "bytebeat_lib.h"
#include "name_edit.h"
#include "note_name.h"
#include "sample_set.h"
#include "synth_drum_machines.h"
#include "synth_fm_machines.h"
#include "synth_lofi.h"
#include "wt_builtin.h"
#include "wt_mip.h"
#include "mod_matrix.h"

namespace ui {
namespace {

const char* const matrixDepthNames[] = {"M01 depth", "M02 depth", "M03 depth", "M04 depth", "M05 depth", "M06 depth", "M07 depth", "M08 depth"};

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void envTime(uint8_t v, char* o, int n) {
  const unsigned ms = mt::envTimeMs(v);
  if (ms < 1000) snprintf(o, n, "%u ms", ms);
  else snprintf(o, n, "%u.%u s", ms / 1000, ms % 1000 / 100);
}

// Lane note n, or the next one in direction dir that no other lane of the kit holds (unique lane
// notes); the lane's own note if there is none in 0..127.
uint8_t freeNoteFrom(const mt::Instrument& k, int lane, int n, int dir = 1) {
  for (; n >= 0 && n <= 127; n += dir) {
    bool taken = false;
    for (int l = 0; l < mt::kKitLanes && !taken; ++l) taken = l != lane && k.kit[l].note == n;
    if (!taken) return static_cast<uint8_t>(n);
  }
  return k.kit[lane].note;
}

}  // namespace

InstScreen::InstScreen(App& app) : app_(app) {
  Param* const sets[] = {chip_, sample_, fm_, drum_, syn_, lofi_};
  for (Param* p : sets) {
    p[kName] = {"Name", [this](char* o, int n) { snprintf(o, n, "%s", inst().name); },
                [this](int d) { editNameChar(inst().name, kNameLen, namePos_, d); }};
    p[kType] = {"Type",
                [this](char* o, int n) {
                  static const char* const kNames[] = {"CHIP", "SAMPLE", "FM", "DRUM", "SYNTH", "KIT", "LOFI"};
                  static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<int>(mt::InstrType::Count), "type names");
                  const int t = static_cast<int>(inst().type);
                  snprintf(o, n, "%s", kNames[t < static_cast<int>(mt::InstrType::Count) ? t : 0]);
                },
                [this](int d) {
                  const int k = clampi(mt::instrTypePos(inst().type) + d, 0, static_cast<int>(mt::InstrType::Count) - 1);
                  setType(mt::instrTypeAt(k));
                }};
    p[kVol] = {"Volume", [this](char* o, int n) { snprintf(o, n, "%u", inst().vol); },
               [this](int d) { inst().vol = static_cast<uint8_t>(clampi(inst().vol + d, 0, 127)); }};
    p[kTranspose] = {"Transpose", [this](char* o, int n) { snprintf(o, n, "%+d", inst().transpose); },
                     [this](int d) { inst().transpose = static_cast<int8_t>(clampi(inst().transpose + d, -24, 24)); }};
    p[kFine] = {"Fine", [this](char* o, int n) { snprintf(o, n, "%+d ct", inst().fine); },
                [this](int d) { inst().fine = static_cast<int8_t>(clampi(inst().fine + d, -50, 50)); }};
    p[kAttack] = {"Attack", [this](char* o, int n) { envTime(inst().attack, o, n); },
                  [this](int d) { inst().attack = static_cast<uint8_t>(clampi(inst().attack + d, 0, 127)); }};
    p[kDecay] = {"Decay", [this](char* o, int n) { envTime(inst().decay, o, n); },
                 [this](int d) { inst().decay = static_cast<uint8_t>(clampi(inst().decay + d, 0, 127)); }};
    p[kSustain] = {"Sustain", [this](char* o, int n) { snprintf(o, n, "%d%%", (inst().sustain * 100 + 63) / 127); },
                   [this](int d) { inst().sustain = static_cast<uint8_t>(clampi(inst().sustain + d, 0, 127)); }};
    p[kRelease] = {"Release", [this](char* o, int n) { envTime(inst().release, o, n); },
                   [this](int d) { inst().release = static_cast<uint8_t>(clampi(inst().release + d, 0, 127)); }};
    p[kMode] = {"Mode", [this](char* o, int n) { snprintf(o, n, "%s", inst().mono ? "MONO" : "POLY"); },
                [this](int d) { inst().mono = d > 0; }};
    p[kGlide] = {"Glide",
                 [this](char* o, int n) {
                   if (inst().glide) snprintf(o, n, "%u ms", inst().glide * 4u);
                   else snprintf(o, n, "OFF");
                 },
                 [this](int d) { inst().glide = static_cast<uint8_t>(clampi(inst().glide + d, 0, 255)); },
                 [this] { return !inst().mono; }};
    p[kRsend] = {"Rvb send", [this](char* o, int n) { snprintf(o, n, "%u", inst().rsend); },
                 [this](int d) { inst().rsend = static_cast<uint8_t>(clampi(inst().rsend + d, 0, 127)); }};
    // Velocity: 64 changes nothing; full depth moves the cutoff +-6 octaves / DECAY +-63 at 127 / 0.
    p[kVelCut] = {"Vel>Cut", [this](char* o, int n) { snprintf(o, n, "%+d", inst().velCut); },
                  [this](int d) { inst().velCut = static_cast<int8_t>(clampi(inst().velCut + d, -64, 63)); },
                  [this] { return inst().fltMode == static_cast<uint8_t>(mt::FltMode::Off); }};
    p[kVelMac] = {"Vel>Dec", [this](char* o, int n) { snprintf(o, n, "%+d", inst().velMac); },
                  [this](int d) { inst().velMac = static_cast<int8_t>(clampi(inst().velMac + d, -64, 63)); },
                  [this] { return inst().type == mt::InstrType::Chip || inst().type == mt::InstrType::Sample; }};
    p[kSend] = {"Dly send", [this](char* o, int n) { snprintf(o, n, "%u", inst().send); },
                [this](int d) { inst().send = static_cast<uint8_t>(clampi(inst().send + d, 0, 127)); }};
  }
  chip_[kWave] = {"Wave", [this](char* o, int n) { mt::chipWaveName(inst().wave, o, n); },
                  [this](int d) { inst().wave = static_cast<uint8_t>(clampi(inst().wave + d, 0, mt::kWaveCount - 1)); }};
  auto notPulse = [this] { return inst().wave != static_cast<uint8_t>(mt::Wave::Pulse); };
  chip_[kDuty] = {"Duty", [this](char* o, int n) { snprintf(o, n, "%u%%", inst().duty); },
                  [this](int d) { inst().duty = static_cast<uint8_t>(clampi(inst().duty + d, 1, 99)); }, notPulse};
  chip_[kPwmRate] = {"PWM rate", [this](char* o, int n) { snprintf(o, n, "%u", inst().pwmRate); },
                     [this](int d) { inst().pwmRate = static_cast<uint8_t>(clampi(inst().pwmRate + d, 0, 127)); },
                     notPulse};
  chip_[kPwmDepth] = {"PWM depth", [this](char* o, int n) { snprintf(o, n, "%u%%", inst().pwmDepth); },
                      [this](int d) { inst().pwmDepth = static_cast<uint8_t>(clampi(inst().pwmDepth + d, 0, 49)); },
                      notPulse};
  auto isTone = [this] { return inst().machine == static_cast<uint8_t>(mt::FmMachine::Tone); };
  auto drum = [this] { return !mt::fmGated(inst().machine); };
  fm_[kAttack].dim = drum;
  fm_[kDecay].dim = [] { return true; };  // the DECAY macro replaces it
  fm_[kSustain].dim = drum;
  fm_[kRelease].dim = drum;
  fm_[kMode].dim = [isTone] { return !isTone(); };
  fm_[kGlide].dim = [this, isTone] { return !inst().mono || !isTone(); };
  fm_[kMachine] = {"Machine",
                   [this](char* o, int n) { snprintf(o, n, "%s", mt::fmMachineName(inst().machine)); },
                   [this](int d) {
                     const int v = clampi(inst().machine + d, 0, static_cast<int>(mt::FmMachine::Count) - 1);
                     if (v != inst().machine) mt::fmSetMachine(inst(), static_cast<uint8_t>(v));
                   }};
  auto macroEdit = [this](int k) {
    return [this, k](int d) { inst().macro[k] = static_cast<uint8_t>(clampi(inst().macro[k] + d, 0, 127)); };
  };
  auto macroNum = [this](int k) {
    return [this, k](char* o, int n) { snprintf(o, n, "%u", inst().macro[k]); };
  };
  fm_[kMac0 + mt::kMacDec] = {"DECAY",
                              [this](char* o, int n) {
                                const unsigned ms = mt::fmDecayMs(inst().macro[mt::kMacDec]);
                                if (ms < 1000) snprintf(o, n, "%u ms", ms);
                                else snprintf(o, n, "%u.%u s", ms / 1000, ms % 1000 / 100);
                              },
                              macroEdit(mt::kMacDec)};
  fm_[kMac0 + mt::kMacCol] = {"COLOR", macroNum(mt::kMacCol), macroEdit(mt::kMacCol)};
  fm_[kMac0 + mt::kMacShp] = {"SHAPE",
                              [this](char* o, int n) {
                                const uint8_t v = inst().macro[mt::kMacShp];
                                if (inst().machine == static_cast<uint8_t>(mt::FmMachine::Chord))
                                  snprintf(o, n, "%u %s", v, mt::fmChordName(v));
                                else snprintf(o, n, "%u", v);
                              },
                              macroEdit(mt::kMacShp)};
  fm_[kMac0 + mt::kMacSwp] = {"SWEEP", macroNum(mt::kMacSwp), macroEdit(mt::kMacSwp)};
  fm_[kMac0 + mt::kMacCon] = {"CONTOUR", macroNum(mt::kMacCon), macroEdit(mt::kMacCon)};
  fm_[kFmSat] = {"SAT", [this](char* o, int n) { snprintf(o, n, "%u", inst().fmSat); },
                 [this](int d) { inst().fmSat = static_cast<uint8_t>(clampi(inst().fmSat + d, 0, 127)); }};
  // DRUM: the machine shapes the whole sound; DECAY is a number (its range differs per machine).
  auto always = [] { return true; };
  for (int r : {kAttack, kDecay, kSustain, kRelease, kMode, kGlide}) drum_[r].dim = always;
  // Machine = the group, Model = the machine in it; both wrap around and load the machine's macros.
  auto wrap = [](int v, int n) { return ((v % n) + n) % n; };
  drum_[kMachine] = {"Machine",
                     [this](char* o, int n) { snprintf(o, n, "%s", mt::drumGroupName(mt::drumGroupOf(inst().machine))); },
                     [this, wrap](int d) {
                       const int g = wrap(mt::drumGroupOf(inst().machine) + d, mt::drumGroupCount());
                       const uint8_t v = mt::drumMachineAt(g, 0);
                       if (v != inst().machine) mt::drumSetMachine(inst(), v);
                     }};
  drum_[kDrumModel] = {"Model",
                       [this](char* o, int n) {
                         const uint8_t m = inst().machine;
                         snprintf(o, n, "%s", mt::drumModelName(mt::drumGroupOf(m), mt::drumModelOf(m)));
                       },
                       [this, wrap](int d) {
                         const int g = mt::drumGroupOf(inst().machine);
                         const uint8_t v = mt::drumMachineAt(g, wrap(mt::drumModelOf(inst().machine) + d, mt::drumModelCount(g)));
                         if (v != inst().machine) mt::drumSetMachine(inst(), v);
                       }};
  for (int k = 0; k < mt::kFmMacros; ++k)
    drum_[kDrumMac0 + k] = {"-", macroNum(k), macroEdit(k), [this, k] { return !*mt::drumMacroName(inst().machine, k); }};
  // LOFI: ADSR, mode and glide as CHIP. A machine change loads its default macros.
  lofi_[kMachine] = {"Machine",
                     [this](char* o, int n) { snprintf(o, n, "%s", mt::lofiMachineName(inst().machine)); },
                     [this](int d) {
                       const int v = clampi(inst().machine + d, 0, static_cast<int>(mt::LofiMachine::Count) - 1);
                       if (v != inst().machine) mt::lofiSetMachine(inst(), static_cast<uint8_t>(v));
                     }};
  auto notByte = [this] { return inst().machine != static_cast<uint8_t>(mt::LofiMachine::Byte); };
  lofi_[kFormula] = {"Formula",
                     [this](char* o, int n) { snprintf(o, n, "%s", mt::byteFormula(inst().lofiFormula).name); },
                     [this](int d) {
                       const int c = mt::byteFormulaCount();
                       inst().lofiFormula = static_cast<uint8_t>(((inst().lofiFormula + d) % c + c) % c);
                       mt::lofiLoadFormulaDefaults(inst());  // A B C jump to the formula's (not on load)
                     },
                     notByte};
  lofi_[kLofiSync] = {"Sync", [this](char* o, int n) { snprintf(o, n, "%s", inst().lofiSync ? "ON" : "OFF"); },
                      [this](int d) { inst().lofiSync = d > 0; }, notByte};
  for (int k = 0; k < mt::kFmMacros; ++k)
    lofi_[kLofiMac0 + k] = {"-", macroNum(k), macroEdit(k), [this, k, notByte] {
                              if (!*mt::lofiMacroName(inst().machine, k)) return true;
                              // BYTE A B C: grey when the formula does not read the variable.
                              return k < 3 && !notByte() && !(mt::byteUses(mt::byteCode(inst().lofiFormula)) >> k & 1);
                            }};
  // BYTE LOOP under SYNC: the loop length in steps.
  lofi_[kLofiMac0 + 4].format = [this](char* o, int n) {
    const mt::Instrument& m = inst();
    const uint8_t v = m.macro[4];
    if (m.machine != static_cast<uint8_t>(mt::LofiMachine::Byte) || !m.lofiSync) snprintf(o, n, "%u", v);
    else if (!v) snprintf(o, n, "OFF");
    else snprintf(o, n, "%d st", mt::lofiLoopSteps(v));
  };
  // SYNTH: ADSR, mode and glide as CHIP. Oscillator k rows, then the shared ones.
  for (int k = 0; k < 2; ++k) {
    const int base = k ? kOsc2 : kOsc1;
    const int shapeRow = k ? kShape2 : kShape1;
    syn_[base] = {k ? "Osc2" : "Osc1",
                  [this, k](char* o, int n) {
                    static const char* const kNames[] = {"SAW", "SQR", "TRI", "WT"};
                    snprintf(o, n, "%s", kNames[inst().synOsc[k] % 4]);
                  },
                  [this, k](int d) {
                    mt::Instrument& m = inst();
                    m.synOsc[k] = static_cast<uint8_t>(clampi(m.synOsc[k] + d, 0, static_cast<int>(mt::SynOsc::Count) - 1));
                    // A wavetable osc without a table would be silent: start from the first built-in.
                    if (m.synOsc[k] == static_cast<uint8_t>(mt::SynOsc::Wt) && !m.synWt[k][0])
                      strlcpy(m.synWt[k], mt::wtBuiltinName(0), sizeof(m.synWt[k]));
                  }};
    auto notWt = [this, k] { return inst().synOsc[k] != static_cast<uint8_t>(mt::SynOsc::Wt); };
    // No edit: a click / tap opens WtPicker (see onInput / onTouch).
    syn_[base + 1] = {k ? "Table2" : "Table1",
                      [this, k](char* o, int n) { snprintf(o, n, "%s", inst().synWt[k][0] ? inst().synWt[k] : "-"); },
                      nullptr, notWt,
                      [this, k, notWt] {
                        if (notWt()) return false;
                        const mt::WtSource* w = audio::wavetableSource();
                        return !inst().synWt[k][0] || !w || !w->findWt(inst().synWt[k]);
                      }};
    syn_[shapeRow] = {k ? "Shape2" : "Shape1", macroNum(mt::kMacShp1 + k), macroEdit(mt::kMacShp1 + k),
                      [this, k] {
                        const uint8_t v = inst().synOsc[k];
                        return v != static_cast<uint8_t>(mt::SynOsc::Square) && v != static_cast<uint8_t>(mt::SynOsc::Wt);
                      }};
  }
  syn_[kSemi] = {"Semi2", [this](char* o, int n) { snprintf(o, n, "%+d", inst().synSemi); },
                 [this](int d) { inst().synSemi = static_cast<int8_t>(clampi(inst().synSemi + d, -24, 24)); }};
  syn_[kDetune] = {"Detune",
                   [this](char* o, int n) { snprintf(o, n, "%+d ct", (inst().macro[mt::kMacDet] - 64) * 50 / 64); },
                   macroEdit(mt::kMacDet)};
  syn_[kSync] = {"Sync", [this](char* o, int n) { snprintf(o, n, "%s", inst().synSync ? "ON" : "OFF"); },
                 [this](int d) { inst().synSync = d > 0; }};
  syn_[kMix] = {"Mix", [this](char* o, int n) { snprintf(o, n, "%d%%", (inst().macro[mt::kMacMix] * 100 + 63) / 127); },
                macroEdit(mt::kMacMix)};
  syn_[kSub] = {"Sub", [this](char* o, int n) { snprintf(o, n, "%u", inst().synSub); },
                [this](int d) { inst().synSub = static_cast<uint8_t>(clampi(inst().synSub + d, 0, 127)); }};
  syn_[kSubOct] = {"Sub oct", [this](char* o, int n) { snprintf(o, n, "%s", inst().synSubOct ? "-2" : "-1"); },
                   [this](int d) { inst().synSubOct = d > 0 ? 1 : 0; }, [this] { return inst().synSub == 0; }};
  syn_[kNoise] = {"Noise", [this](char* o, int n) { snprintf(o, n, "%u", inst().synNoise); },
                  [this](int d) { inst().synNoise = static_cast<uint8_t>(clampi(inst().synNoise + d, 0, 127)); }};
  syn_[kSenv] = {"Env>Shp", [this](char* o, int n) { snprintf(o, n, "%+d", inst().macro[mt::kMacSenv] - 64); },
                 macroEdit(mt::kMacSenv)};
  auto noSenv = [this] { return inst().macro[mt::kMacSenv] == 64; };
  syn_[kEAtk] = {"Env atk", [this](char* o, int n) { envTime(inst().synEAtk, o, n); },
                 [this](int d) { inst().synEAtk = static_cast<uint8_t>(clampi(inst().synEAtk + d, 0, 127)); }, noSenv};
  syn_[kEDec] = {"Env dec",
                 [this](char* o, int n) {
                   if (inst().synEDec) envTime(inst().synEDec, o, n);
                   else snprintf(o, n, "HOLD");
                 },
                 [this](int d) { inst().synEDec = static_cast<uint8_t>(clampi(inst().synEDec + d, 0, 127)); }, noSenv};
  auto view = [this](char* o, int n) { snprintf(o,n,"%s",wave_.live()?"LIVE":"HOLD"); };
  syn_[kOscView] = {"View",view,nullptr,{}, {}, [] { return "CLICK TO TOGGLE HOLD / LIVE"; }};
  syn_[kModView] = {"View",view,nullptr,{}, {}, [] { return "CLICK TO TOGGLE HOLD / LIVE"; }};
  initTail(chip_ + kChipRows);
  initTail(sample_ + kCommon);
  initTail(fm_ + kFmRows);
  initTail(drum_ + kDrumRows);
  initTail(syn_ + kSynRows);
  initTail(lofi_ + kLofiRows);
  initKit();
  list_.setParams(chip_, kMainRows);  // MAIN of shown_ (Chip)
  list_.setVisibleRows(kListRows);
  list_.setPageBar(true);
  list_.setOnEdit([this] {
    app_.markDirty();
    syncParams();
  });
  // Shift+click while editing puts the whole instrument back (a type change resets other fields).
  list_.setEditScope([this]() -> void* { return &inst(); }, sizeof(mt::Instrument));
  list_.setOnCancel([this] { app_.toast("CANCEL"); });
  presets_.setOnClose([this] { afterPresets(); });
  initMatrix();
  wt_.setOnClose([this] { app_.invalidate(); });
}

void InstScreen::initKit() {
  kit_[0] = chip_[kName];
  kit_[1] = chip_[kType];
  kit_[2] = chip_[kSend];
  kit_[3] = chip_[kRsend];
  static const char* const kSrcLabels[] = {"L1 Src", "L2 Src", "L3 Src", "L4 Src",
                                           "L5 Src", "L6 Src", "L7 Src", "L8 Src"};
  static_assert(sizeof(kSrcLabels) / sizeof(kSrcLabels[0]) == mt::kKitLanes, "a label per lane");
  for (int l = 0; l < mt::kKitLanes; ++l) {
    Param* r = kit_ + kKitMain + l * kLaneRows;
    auto lane = [this, l]() -> mt::KitLane& { return inst().kit[l]; };
    // Src: SAMPLE (its own sampler) / INST (one of the instruments). SAMPLE drops the instrument.
    r[kLaneSrc] = {kSrcLabels[l],
                   [lane](char* o, int n) { snprintf(o, n, "%s", lane().instr < mt::kInstruments ? "INST" : "SAMPLE"); },
                   [lane](int d) {
                     mt::KitLane& ln = lane();
                     if (d > 0) {
                       if (ln.instr >= mt::kInstruments) ln.instr = 0;
                     } else {
                       ln.instr = mt::kNoInstr;
                     }
                   }};
    // Sample: project list order, "---" (none) before the first; red when missing from the bank.
    r[kLaneSample] = {"  Sample",
                      [lane](char* o, int n) { snprintf(o, n, "%s", lane().sample[0] ? lane().sample : "---"); },
                      [this, lane](int d) {
                        const mt::Project& p = app_.project();
                        mt::KitLane& ln = lane();
                        const int cur = mt::projSampleFind(p, ln.sample);
                        const int i = clampi(cur + d, -1, p.sampleCount - 1);
                        if (i == cur) return;
                        if (i < 0) ln.sample[0] = 0;
                        else snprintf(ln.sample, sizeof(ln.sample), "%s", p.samples[i].name);
                      },
                      {},
                      [this, lane] {
                        const mt::KitLane& ln = lane();
                        if (!ln.sample[0] || !audio::bankMounted()) return false;
                        const mt::Project& p = app_.project();
                        return mt::projSampleBank(p, audio::bank(), mt::projSampleFind(p, ln.sample)) < 0;
                      }};
    // Instr: INS1..16; a SAMPLE instrument moves the lane note to its root (the sample keeps its pitch).
    r[kLaneInstr] = {"  Instr",
                     [this, lane](char* o, int n) {
                       const uint8_t i = lane().instr < mt::kInstruments ? lane().instr : 0;
                       snprintf(o, n, "INS%u %s", i + 1u, app_.project().instruments[i].name);
                     },
                     [this, l, lane](int d) {
                       mt::KitLane& ln = lane();
                       ln.instr = static_cast<uint8_t>(clampi(ln.instr + d, 0, mt::kInstruments - 1));
                       const mt::Instrument& m = app_.project().instruments[ln.instr];
                       if (m.type == mt::InstrType::Sample) ln.note = freeNoteFrom(inst(), l, m.root);
                     },
                     {},
                     [this, lane] {
                       const uint8_t i = lane().instr;
                       return i < mt::kInstruments && app_.project().instruments[i].type == mt::InstrType::Kit;
                     }};
    r[kLaneVol] = {"  Volume", [lane](char* o, int n) { snprintf(o, n, "%u", lane().vol); },
                   [lane](int d) { lane().vol = static_cast<uint8_t>(clampi(lane().vol + d, 0, 127)); }};
    r[kLanePitch] = {"  Pitch", [lane](char* o, int n) { snprintf(o, n, "%+d", lane().pitch); },
                     [lane](int d) { lane().pitch = static_cast<int8_t>(clampi(lane().pitch + d, -24, 24)); }};
    r[kLaneDecay] = {"  Decay",
                     [lane](char* o, int n) {
                       if (lane().decay) envTime(lane().decay, o, n);
                       else snprintf(o, n, "FULL");
                     },
                     [lane](int d) { lane().decay = static_cast<uint8_t>(clampi(lane().decay + d, 0, 127)); }};
    // Note: sent on MIDI tracks and the pitch a sampler lane plays its sample at; skips other lanes' notes.
    r[kLaneNote] = {"  Note",
                    [lane](char* o, int n) {
                      char nn[4];
                      mt::noteName(lane().note, nn);
                      snprintf(o, n, "%s", nn);
                    },
                    [this, l, lane](int d) {
                      const int dir = d > 0 ? 1 : -1;
                      lane().note = freeNoteFrom(inst(), l, lane().note + d, dir);
                    }};
  }
}

void InstScreen::buildLanes(bool keep) {
  int n = 0;
  for (int l = 0; l < mt::kKitLanes; ++l) {
    const Param* r = kit_ + kKitMain + l * kLaneRows;
    const bool instMode = inst().kit[l].instr < mt::kInstruments;
    for (int k = 0; k < kLaneRows; ++k) {
      const bool sampleOnly = k == kLaneSample || k == kLaneVol || k == kLanePitch || k == kLaneDecay;
      if ((instMode && sampleOnly) || (!instMode && k == kLaneInstr)) continue;
      kitShown_[n++] = r[k];
    }
  }
  if (keep) list_.replaceParams(kitShown_, n);
  else list_.setParams(kitShown_, n);
}

void InstScreen::initTail(Param* t) {
  auto off = [this] { return inst().fltMode == static_cast<uint8_t>(mt::FltMode::Off); };
  auto noEnv = [this, off] { return off() || inst().fenv == 0; };
  t[kFltMode] = {"Filter",
                 [this](char* o, int n) {
                   static const char* const kNames[] = {"OFF", "LP", "BP", "HP"};
                   snprintf(o, n, "%s", kNames[inst().fltMode % 4]);
                 },
                 [this](int d) {
                   inst().fltMode = static_cast<uint8_t>(
                       clampi(inst().fltMode + d, 0, static_cast<int>(mt::FltMode::Count) - 1));
                 }};
  t[kCutoff] = {"Cutoff",
                [this](char* o, int n) {
                  const float hz = mt::cutoffHz(inst().cutoff);
                  if (hz < 1000) snprintf(o, n, "%u Hz", static_cast<unsigned>(hz + 0.5f));
                  else snprintf(o, n, "%.1f kHz", hz / 1000.f);
                },
                [this](int d) { inst().cutoff = static_cast<uint8_t>(clampi(inst().cutoff + d, 0, 127)); }, off};
  t[kReso] = {"Reso", [this](char* o, int n) { snprintf(o, n, "%u", inst().reso); },
              [this](int d) { inst().reso = static_cast<uint8_t>(clampi(inst().reso + d, 0, 127)); }, off};
  t[kFEnv] = {"Flt env", [this](char* o, int n) { snprintf(o, n, "%+d", inst().fenv); },
              [this](int d) { inst().fenv = static_cast<int8_t>(clampi(inst().fenv + d, -64, 63)); }, off};
  t[kFAtk] = {"Flt attack", [this](char* o, int n) { envTime(inst().fAtk, o, n); },
              [this](int d) { inst().fAtk = static_cast<uint8_t>(clampi(inst().fAtk + d, 0, 127)); }, noEnv};
  t[kFDec] = {"Flt decay",
              [this](char* o, int n) {
                if (inst().fDec) envTime(inst().fDec, o, n);
                else snprintf(o, n, "HOLD");
              },
              [this](int d) { inst().fDec = static_cast<uint8_t>(clampi(inst().fDec + d, 0, 127)); }, noEnv};
  t[kKeytrack] = {"Key track",
                  [this](char* o, int n) { snprintf(o, n, "%d%%", (inst().keytrack * 100 + 63) / 127); },
                  [this](int d) { inst().keytrack = static_cast<uint8_t>(clampi(inst().keytrack + d, 0, 127)); },
                  off};
  // LFO page: the selected LFO (1..4) of the instrument; the others keep running. Rows read a copy
  // (lfoAt) and write through lfoRef: GCC 14 (xtensa) miscompiled a read through the reference
  // struct in the Dest row into a load from address 0 (PANIC on any non-SYNTH instrument).
  auto cfg = [this] { return mt::lfoAt(inst(), lfoSel_); };
  auto lfo = [this] { return mt::lfoRef(inst(), lfoSel_); };
  auto noLfo = [this, cfg] { return cfg().depth == 0 && !(mt::modLfoMask(inst()) & (1u << lfoSel_)); };
  t[kLfoSel] = {"LFO",
                [this](char* o, int n) {
                  int on = 0;
                  for (int i = 0; i < mt::kLfos; ++i) on += mt::lfoAt(inst(), i).depth != 0;
                  snprintf(o, n, "%d  (%d ON)", lfoSel_ + 1, on);
                },
                [this](int d) { lfoSel_ = clampi(lfoSel_ + d, 0, mt::kLfos - 1); }};
  t[kLfoWave] = {"Wave",
                 [cfg](char* o, int n) {
                   static const char* const kNames[] = {"SINE", "TRI", "SAW", "SQR", "RND", "BYTE"};
                   static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<int>(mt::LfoWave::Count), "Wave");
                   snprintf(o, n, "%s", kNames[cfg().wave % static_cast<int>(mt::LfoWave::Count)]);
                 },
                 [cfg, lfo](int d) {
                   const uint8_t w = static_cast<uint8_t>(clampi(cfg().wave + d, 0, static_cast<int>(mt::LfoWave::Count) - 1));
                   lfo().wave = w;
                 },
                 noLfo};
  t[kLfoSync] = {"Sync", [cfg](char* o, int n) { snprintf(o, n, "%s", mt::lfoTempo(cfg().sync) ? "TEMPO" : "FREE"); },
                 [cfg, lfo](int d) {
                   const bool on = d > 0;
                   const uint8_t s = cfg().sync;
                   if (on == mt::lfoTempo(s)) return;
                   const mt::LfoRef l = lfo();
                   l.sync = static_cast<uint8_t>(on ? s | mt::kLfoTempo : s & ~mt::kLfoTempo);  // keeps Retrig
                   l.rate = on ? 6 : 64;  // 1/4, or about 1.6 Hz
                 },
                 noLfo};
  // Retrig OFF: one phase per instrument for all its voices (FREE runs on, TEMPO from the transport start).
  t[kLfoRetrig] = {"Retrig", [cfg](char* o, int n) { snprintf(o, n, "%s", mt::lfoFree(cfg().sync) ? "OFF" : "ON"); },
                   [cfg, lfo](int d) {
                     const uint8_t s = cfg().sync;
                     const uint8_t v = static_cast<uint8_t>(d > 0 ? s & ~mt::kLfoFree : s | mt::kLfoFree);
                     lfo().sync = v;
                   },
                   noLfo};
  t[kLfoRate] = {"Rate",
                 [cfg](char* o, int n) {
                   const mt::LfoCfg l = cfg();
                   if (mt::lfoTempo(l.sync)) snprintf(o, n, "%s", mt::lfoSyncName(l.rate));
                   else snprintf(o, n, "%.2f Hz", mt::lfoHz(l.rate));
                 },
                 [cfg, lfo](int d) {
                   const mt::LfoCfg l = cfg();
                   const uint8_t r = static_cast<uint8_t>(clampi(l.rate + d, 0, mt::lfoTempo(l.sync) ? mt::kLfoSyncSteps - 1 : 127));
                   lfo().rate = r;
                 },
                 noLfo};
  t[kLfoDepth] = {"Depth", [cfg](char* o, int n) { snprintf(o, n, "%+d", cfg().depth); },
                  [cfg, lfo](int d) {
                    const int8_t v = static_cast<int8_t>(clampi(cfg().depth + d, -64, 63));
                    lfo().depth = v;
                  }};
  // DRUM shows the generic macro names here, not the machine's; SYNTH its own.
  t[kLfoDest] = {"Dest",
                 [this, cfg](char* o, int n) { snprintf(o, n, "%s", mt::lfoDestName(cfg().dest, inst().type)); },
                 [this, cfg, lfo](int d) {
                   const uint8_t v = mt::lfoDestStep(cfg().dest, d, inst().type, lfoSel_);
                   lfo().dest = v;
                 },
                 noLfo};
  // Wave BYTE (Instrument::lfoByte of the selected LFO): the formula and its A; B / C are the formula's.
  // A formula change loads its A.
  auto notByte = [cfg, noLfo] { return noLfo() || cfg().wave != static_cast<uint8_t>(mt::LfoWave::Byte); };
  t[kLfoFormula] = {"Formula",
                    [this](char* o, int n) { snprintf(o, n, "%s", mt::byteFormula(inst().lfoByte[lfoSel_][0]).name); },
                    [this](int d) {
                      uint8_t* b = inst().lfoByte[lfoSel_];
                      const int c = mt::byteFormulaCount();
                      b[0] = static_cast<uint8_t>(((b[0] + d) % c + c) % c);
                      b[1] = mt::byteFormula(b[0]).a;
                    },
                    notByte};
  t[kLfoA] = {"A", [this](char* o, int n) { snprintf(o, n, "%u", inst().lfoByte[lfoSel_][1]); },
              [this](int d) {
                uint8_t* b = inst().lfoByte[lfoSel_];
                b[1] = static_cast<uint8_t>(clampi(b[1] + d, 0, 127));
              },
              [this, notByte] { return notByte() || !(mt::byteUses(mt::byteCode(inst().lfoByte[lfoSel_][0])) & 1); }};
  for (int r : {kCutoff, kReso, kFEnv}) t[r].hint = [] { return "Enable Filter"; };
  for (int r : {kFAtk, kFDec})
    t[r].hint = [off] { return off() ? "Enable Filter" : "Set Flt env above/below zero"; };
  t[kKeytrack].hint = [off] { return off() ? "Enable Filter" : "FORMANT has no key tracking"; };
  for (int r : {kLfoWave, kLfoSync, kLfoRetrig, kLfoRate, kLfoDest})
    t[r].hint = [] { return "Set LFO Depth above/below zero"; };
  t[kLfoFormula].hint = [noLfo] { return noLfo() ? "Set LFO Depth above/below zero" : "Select LFO Wave BYTE"; };
  t[kLfoA].hint = [noLfo, notByte] {
    return noLfo() ? "Set LFO Depth above/below zero" : notByte() ? "Select LFO Wave BYTE" : "Formula does not use A";
  };

}

void InstScreen::relabel() {
  if (shown_ == mt::InstrType::Lofi) {
    for (int k = 0; k < mt::kFmMacros; ++k) {
      const char* l = mt::lofiMacroName(inst().machine, k);
      lofi_[kLofiMac0 + k].label = *l ? l : "-";
    }
    return;
  }
  if (shown_ != mt::InstrType::Drum) return;
  for (int k = 0; k < mt::kFmMacros; ++k) {
    const char* l = mt::drumMacroName(inst().machine, k);
    drum_[kDrumMac0 + k].label = *l ? l : "-";
  }
}

mt::Instrument& InstScreen::inst() { return app_.project().instruments[instr_]; }

// Each encoder step applies the type, and a type change resets its own fields (KIT lanes, macros,
// machine). Leaving a type keeps a copy: turning back to it restores them instead of the defaults.
void InstScreen::setType(mt::InstrType v) {
  mt::Instrument& m = inst();
  if (v == m.type) return;
  wave_.reset();
  if (typeSnapInstr_ != instr_ || m.type == typeSnap_.type) {
    typeSnap_ = m;
    typeSnapInstr_ = instr_;
  }
  if (v != typeSnap_.type) {
    mt::instrSetType(m, v);
    return;
  }
  m.type = v;
  m.machine = typeSnap_.machine;
  memcpy(m.macro, typeSnap_.macro, sizeof(m.macro));
  memcpy(m.kit, typeSnap_.kit, sizeof(m.kit));
  m.lofiFormula = typeSnap_.lofiFormula;
  m.lofiSync = typeSnap_.lofiSync;
  m.lfoDest = typeSnap_.lfoDest;
  for (int i = 0; i < mt::kLfos - 1; ++i) m.lfo[i].dest = typeSnap_.lfo[i].dest;
}
const mt::Instrument& InstScreen::inst() const { return app_.project().instruments[instr_]; }

Param* InstScreen::typeRows() {
  switch (shown_) {
    case mt::InstrType::Sample: return sample_;
    case mt::InstrType::Fm: return fm_;
    case mt::InstrType::Drum: return drum_;
    case mt::InstrType::Synth: return syn_;
    case mt::InstrType::Lofi: return lofi_;
    default: return chip_;
  }
}

int InstScreen::typeCount() const {
  switch (shown_) {
    case mt::InstrType::Sample: return 0;  // SampleEditor
    case mt::InstrType::Fm: return kFmRows - kCommon;
    case mt::InstrType::Drum: return kDrumRows - kCommon;
    case mt::InstrType::Synth: return kSynRows - kCommon;
    case mt::InstrType::Lofi: return kLofiRows - kCommon;
    default: return kChipRows - kCommon;
  }
}

void InstScreen::initMatrix() {
  auto route = [this]() -> mt::ModRoute& { return inst().mod[matrixRoute_]; };
  auto off = [route] { return route().source == 0; };
  matrix_[0] = {"Route", [this](char* o, int n) { snprintf(o,n,"%d / %d",matrixRoute_+1,mt::kModRoutes); },
    [this](int d) { matrixRoute_=clampi(matrixRoute_+d,0,mt::kModRoutes-1); matrix_[3].label=matrixDepthNames[matrixRoute_]; }};
  matrix_[1] = {"Source", [route](char* o,int n) { snprintf(o,n,"%s",mt::modSourceName(route().source)); },
    [route](int d) { route().source=clampi(route().source+d,0,int(mt::ModSource::Count)-1); }};
  matrix_[2] = {"Target", [this,route](char* o,int n) { snprintf(o,n,"%s",mt::lfoDestName(route().dest,inst().type)); },
    [this,route](int d) { route().dest=mt::modDestStep(route().dest,d,inst().type); },off,
    [this,route] { return !mt::lfoDestValid(route().dest,inst().type); }};
  matrix_[3] = {matrixDepthNames[0], [route](char* o,int n) { snprintf(o,n,"%+d",route().amount); },
    [route](int d) { route().amount=clampi(route().amount+d,-64,63); },off};
  matrix_[4] = {"Polarity", [route](char* o,int n) {
      const char* const names[]={"NATIVE","0..1","-1..1"};
      snprintf(o,n,"%s",names[route().polarity<3?route().polarity:0]); },
    [route](int d) { route().polarity=clampi(route().polarity+d,0,2); },off};
  matrix_[5] = {"Depth via", [route](char* o,int n) { snprintf(o,n,"%s",mt::modSourceName(route().via)); },
    [route](int d) { route().via=clampi(route().via+d,0,int(mt::ModSource::Count)-1); },off};
  for (int i=2;i<6;++i) matrix_[i].hint=[] { return "Select a matrix Source"; };
}

// Actions are captured before opening a job; switching instruments cannot redirect its result.
void InstScreen::openSampleJob(int action) {
  int index; uint32_t from,to,rate;
  editor_.bind(instr_);
  const bool found=action==4?editor_.wtSource(index,from,to,rate):editor_.extractSource(action==1,index,from,to,rate);
  if (!found) { app_.toast(action==1?"SELECT A SLICE":"SAMPLE NOT CACHED"); return; }
  if (action==4) wtMake_.open(index,from,to,rate,inst().name);
  else if (action==2) extract_.openStretch(instr_,index,from,to,rate);
  else if (action==3) extract_.openTempo(instr_,index,from,to,rate);
  else extract_.open(instr_,index,from,to,rate);
  sampleJob_=true;
}
void InstScreen::syncSampleJob() {
  if (!sampleJob_ || extract_.isOpen() || wtMake_.isOpen()) return;
  sampleJob_=false; mut_.slot=-1; editor_.bind(instr_); syncParams(); app_.invalidate();
}

void InstScreen::syncParams() {
  const mt::InstrType t = inst().type;
  if (t == shown_) {
    // KIT lanes: a Src change or another KIT shows other rows.
    if (kit() && page_ == kPgType) buildLanes(true);
    return;
  }
  shown_ = t;
  // Same page in the new type's rows; outside the type page the row and edit state stay (Type edit).
  const int sel = list_.sel();
  const bool ed = list_.editing();
  const bool hold = list_.holding();  // A + turn on Type: releasing A must still end the edit
  const bool bar = onEditor() ? editor_.barSelected() : list_.barSelected();
  showPage(physPage(), bar);
  if (page_ != kPgType && page_ != kPgType2 && !bar) {
    list_.setSel(sel);
    list_.setEdit(ed, hold);
  }
}

int InstScreen::physPage() const {
  if (kit()) return page_ == kPgMain ? 0 : 1;
  if (synth() || page_ < kPgType2) return page_;
  return page_ == kPgType2 ? kPgType : page_ - 1;  // no MOD page: its rows are the type page's
}

int InstScreen::logicalPage(int phys) const {
  if (kit()) return phys ? kPgType : kPgMain;
  return synth() || phys < kPgType2 ? phys : phys + 1;
}

int InstScreen::tableOsc() const {
  if (!synth() || page_ != kPgType || list_.editing()) return -1;
  const int r = kCommon + list_.sel();
  return r == kTable1 ? 0 : (r == kTable2 ? 1 : -1);
}

void InstScreen::showPage(int phys, bool bar) {
  const int n = pageCount();
  page_ = logicalPage((phys % n + n) % n);
  leaveEdit();
  if (onEditor()) {
    editor_.enter(bar);
    return;
  }
  if (kit()) {
    if (page_ == kPgMain) list_.setParams(kit_, kKitMain);
    else buildLanes(false);
    list_.setVisibleRows(kListRows);
    if (bar) list_.selectBar();
    else list_.setSel(0);
    return;
  }
  if (page_==kPgMatrix) {
    list_.setParams(matrix_,6); list_.setVisibleRows(kListRows);
    if(bar) list_.selectBar(); else list_.setSel(0); return;
  }
  Param* const rows = typeRows();
  const int tail = kCommon + typeCount();
  int off = 0, count = kMainRows;
  switch (page_) {
    case kPgEnv: off = kMainRows, count = kCommon - kMainRows; break;
    case kPgType: off = kCommon, count = synth() ? kSynOscRows : typeCount(); break;
    case kPgType2: off = kSub, count = kSynRows - kSub; break;  // SYNTH only
    case kPgFilt: off = tail, count = kFiltRows; break;
    case kPgLfo: off = tail + kFiltRows, count = kTailRows - kFiltRows; break;
    default: break;
  }
  list_.setParams(rows + off, count);
  list_.setVisibleRows(kListRows);
  if (bar) list_.selectBar();
  else list_.setSel(0);
}

void InstScreen::fixNames() {
  mt::Project& p = app_.project();
  for (int i = 0; i < mt::kInstruments; ++i) {
    if (p.instruments[i].name[0]) continue;
    engine::lockProject();
    snprintf(p.instruments[i].name, sizeof(p.instruments[i].name), "INS%d", i + 1);
    engine::unlockProject();
  }
}

void InstScreen::leaveEdit() {
  list_.setEdit(false);
  editor_.leaveEdit();
  namePos_ = 0;
  fixNames();
}

void InstScreen::onEnter() {
  wave_.reset();
  leaveEdit();
  typeSnapInstr_ = -1;
  instr_ = app_.project().tracks[app_.curTrack()].instr % mt::kInstruments;
  editor_.bind(instr_);
  syncParams();
}

void InstScreen::onLeave() {
  wave_.reset();
  if (presets_.isOpen()) presets_.close(false);
  if (wt_.isOpen()) wt_.close();
  bounce_.cancel(); extract_.cancel(); wtMake_.cancel(); sampleJob_=false;
}

void InstScreen::onProjectReplaced() {
  wave_.reset();
  mut_.slot = -1;  // belongs to the old project
  typeSnapInstr_ = -1;
  // The instrument the browsers would restore / set belongs to the old project.
  if (presets_.isOpen()) presets_.close(true);
  if (wt_.isOpen()) wt_.close();
  bounce_.cancel(); extract_.cancel(); wtMake_.cancel(); sampleJob_=false;
  onEnter();
}

void InstScreen::afterPresets() {
  wave_.reset();
  editor_.bind(instr_);
  syncParams();
  fixNames();
}

void InstScreen::openTables(int osc) {
  wave_.reset();
  leaveEdit();
  wt_.open(instr_, osc);
}

void InstScreen::presetMenu() {
  leaveEdit();
  if (inst().type == mt::InstrType::Kit) {  // factory kits only: no Save
    enum : int { kKitLoad, kKitShot };
    const MenuItem items[] = {{"Load kit...", kKitLoad}, {"Save one-shot...", kKitShot}};
    char title[28];
    snprintf(title, sizeof(title), "KIT I%02d %s", instr_ + 1, inst().name);
    app_.menu().open(title, items, 2, [this](int id) {
      if (id == kKitLoad) {
        typeSnapInstr_ = -1;
        presets_.open(PresetBrowser::Mode::Load, instr_);
      } else if (id == kKitShot) {  // dry: the baked lanes play through the track's insert
        bounce_.openKit(instr_, [this] { syncParams(); });  // the baked lanes show SAMPLE rows
      }
    });
    return;
  }
  enum : int { kLoad, kSave, kChop, kAdd, kDel, kClear, kClearConfirm, kCancel, kToWt, kExtractRegion, kExtractSlice, kStretch, kBpm, kShot,
               kRecordGroup, kSlicesGroup, kConvertGroup, kPresetsGroup, kMutateGroup };
  auto choose = [this](int id) {
    switch (id) {
      case kChop: editor_.chop(); break;
      case kShot: {
        const int t = app_.curTrack();
        const uint8_t ins = app_.project().tracks[t].instr == instr_ ? static_cast<uint8_t>(t) : mt::kTracks;
        const uint8_t note=onEditor()?editor_.previewNote():kPreviewNote;
        if (note>127) app_.toast("USE EXTRACT SLICE");
        else bounce_.open(instr_,note,ins);
        break;
      }
      case kStretch: openSampleJob(2); break;
      case kBpm: openSampleJob(3); break;
      case kExtractRegion: openSampleJob(0); break;
      case kExtractSlice: openSampleJob(1); break;
      case kToWt: openSampleJob(4); break;
      case kAdd: editor_.addSlice(app_.shift()); break;
      case kDel: editor_.deleteSlice(); break;
      case kClear: {
        const MenuItem confirm[] = {{"Cancel", kCancel}, {"Clear slices", kClearConfirm}};
        app_.menu().openChild("CLEAR SLICES?", confirm, 2, [this](int i) {
          if (i == kClearConfirm) editor_.clearSlices();
        });
        break;
      }
      case kLoad:
      case kSave:
        typeSnapInstr_ = -1;
        presets_.open(id == kSave ? PresetBrowser::Mode::Save : PresetBrowser::Mode::Load, instr_);
        break;
      case kMutateGroup: openMutateMenu(); break;
      default: break;
    }
  };
  if (onEditor()) {
    editor_.bind(instr_);
    const MenuItem groups[] = {{"Render...", kRecordGroup}, {"Slices...", kSlicesGroup},
                               {"Convert...", kConvertGroup}, {"Presets...", kPresetsGroup},
                               {"Mutate...", kMutateGroup}};
    char title[28];
    snprintf(title, sizeof(title), "SAMPLE I%02d %s", instr_ + 1, inst().name);
    app_.menu().open(title, groups, 5, [this, choose](int group) {
      char title[28];
      if (group == kMutateGroup) { openMutateMenu(); return; }
      if (group == kRecordGroup) {
        const MenuItem rows[] = {{"Save one-shot...", kShot}};
        snprintf(title, sizeof(title), "RENDER I%02d", instr_ + 1);
        app_.menu().openChild(title, rows, 1, choose);
      } else if (group == kSlicesGroup) {
        const MenuItem rows[] = {{"Chop", kChop}, {"Add slice", kAdd},
                                {"Delete slice", kDel, editor_.sliceSelected()},
                                {"Clear slices...", kClear, editor_.hasSlices()}};
        snprintf(title, sizeof(title), "SLICES I%02d", instr_ + 1);
        app_.menu().openChild(title, rows, 4, choose);
      } else if (group == kConvertGroup) {
        int wi; uint32_t wf, wt, wr;
        MenuItem rows[5];
        int count = 0;
        rows[count++] = {"Time stretch...",kStretch,editor_.extractSource(false,wi,wf,wt,wr)};
        rows[count++] = {"Estimate BPM...",kBpm,editor_.extractSource(false,wi,wf,wt,wr)};
        rows[count++] = {"Extract region...", kExtractRegion, editor_.extractSource(false, wi, wf, wt, wr)};
        if (editor_.sliceSelected())
          rows[count++] = {"Extract slice...", kExtractSlice, editor_.extractSource(true, wi, wf, wt, wr)};
        rows[count++] = {"To wavetable...", kToWt, editor_.wtSource(wi, wf, wt, wr)};
        snprintf(title, sizeof(title), "CONVERT I%02d", instr_ + 1);
        app_.menu().openChild(title, rows, count, choose);
      } else if (group == kPresetsGroup) {
        const MenuItem rows[] = {{"Load preset...", kLoad}, {"Save preset...", kSave}};
        snprintf(title, sizeof(title), "PRESETS I%02d", instr_ + 1);
        app_.menu().openChild(title, rows, 2, choose);
      }
    });
    return;
  }
  const MenuItem items[] = {{"Load preset...", kLoad}, {"Save preset...", kSave},
                            {"Save one-shot...", kShot}, {"Mutate...", kMutateGroup}};
  char title[28];
  snprintf(title, sizeof(title), "PRESET I%02d %s", instr_ + 1, inst().name);
  app_.menu().open(title, items, 4, choose);
}

void InstScreen::openMutateMenu(bool refresh) {
  char strength[28], title[28];
  snprintf(strength, sizeof(strength), "Strength: %d%%", mutateStrength_);
  snprintf(title, sizeof(title), "MUTATE I%02d", instr_ + 1);
  const bool canUndo = mut_.slot == instr_ && mt::mutateCanRestore(app_.project(), mut_);
  const MenuItem rows[] = {{strength, 0}, {"Apply mutation", 1}, {"Undo mutation", 2, canUndo}};
  auto choose = [this](int id) {
    if (id == 0) {
      const MenuItem choices[] = {{"15%", 15}, {"30%", 30}, {"60%", 60}};
      app_.menu().openChild("MUTATE STRENGTH", choices, 3, [this](int strength) {
        mutateStrength_ = strength;
        app_.menu().back();
        openMutateMenu(true);
      });
    } else if (id == 1) mutate(mutateStrength_);
    else if (id == 2) undoMutate();
  };
  if (refresh) app_.menu().replace(title, rows, 3, choose);
  else app_.menu().openChild(title, rows, 3, choose);
}

void InstScreen::mutate(int strength) {
  leaveEdit();
  engine::lockProject();
  const bool ok = mt::mutateSlot(app_.project(), instr_, strength, esp_random(), mut_);
  engine::unlockProject();
  if (!ok) {
    app_.toast("NOTHING TO MUTATE");
    return;
  }
  app_.markDirty();
  syncParams();
  char msg[24];
  snprintf(msg, sizeof(msg), "MUTATE %d%%", strength);
  app_.toast(msg);
  preview();
}

void InstScreen::undoMutate() {
  if (mut_.slot != instr_) return;
  leaveEdit();
  engine::lockProject();
  const bool ok = mt::mutateRestore(app_.project(), mut_);
  engine::unlockProject();
  if (!ok) return;
  app_.markDirty();
  syncParams();
  app_.toast("UNDO MUTATE");
  preview();
}

void InstScreen::onATap(bool shift) {
  if (presets_.isOpen() || wt_.isOpen() || bounce_.isOpen() || extract_.isOpen() || wtMake_.isOpen() || !onEditor()) return;
  editor_.bind(instr_);
  editor_.stepMarker(shift ? -1 : 1);
}

void InstScreen::changeInstr(int d) {
  wave_.reset();
  leaveEdit();
  instr_ = ((instr_ + d) % mt::kInstruments + mt::kInstruments) % mt::kInstruments;
  editor_.bind(instr_);
  syncParams();
}

bool InstScreen::buttonsBusy() const { return !presets_.isOpen() && !bounce_.isOpen() && !extract_.isOpen() && !wtMake_.isOpen() && onEditor(); }

bool InstScreen::trackKey(int n, bool shift) {
  if (shift || presets_.isOpen() || bounce_.isOpen() || extract_.isOpen() || wtMake_.isOpen() || !onEditor()) return false;
  editor_.playSlice(n);
  return true;
}

// Through the current track's insert when that track plays this instrument, else dry (kTracks).
uint8_t InstScreen::previewIns() {
  const int t = app_.curTrack();
  return app_.project().tracks[t].instr == instr_ ? static_cast<uint8_t>(t) : mt::kTracks;
}

void InstScreen::preview() {
  const uint8_t note = onEditor() ? editor_.previewNote() : kPreviewNote;
  if (note > 127) editor_.playSlice(0);
  else audio::preview(static_cast<uint8_t>(instr_), note, previewIns());
}

void InstScreen::onInput(const hw::InputEvent& ev) {
  if (extract_.isOpen()) { extract_.onInput(ev); syncSampleJob(); return; }
  if (wtMake_.isOpen()) { wtMake_.onInput(ev); syncSampleJob(); return; }
  if (presets_.isOpen()) {
    presets_.onInput(ev);
    return;
  }
  if (bounce_.isOpen()) {
    bounce_.onInput(ev);
    return;
  }
  if (wt_.isOpen()) {
    wt_.onInput(ev);
    return;
  }
  if (ev.type == hw::InputType::EncClick && tableOsc() >= 0) {
    openTables(tableOsc());
    return;
  }
  if (ev.type == hw::InputType::EncClick && !list_.editing() && viewRow(list_.sel())) {
    wave_.toggleLive();
    return;
  }
  const bool wasName = nameEdit();
  if (ev.type == hw::InputType::EncLong) {
    if (ev.shift) presetMenu();
    else preview();
    return;
  }
  if (ev.type == hw::InputType::EncTurn && ev.shift) {
    if (wasName) {
      namePos_ = clampi(namePos_ + ev.delta, 0, kNameLen - 1);
      return;
    }
    if (!(onEditor() ? editor_.editing() : list_.editing())) {
      changeInstr(ev.delta);
      return;
    }
  }
  if (wasName && ev.type == hw::InputType::EncTurn) {
    list_.edit(ev.delta);  // no x10 for characters
    return;
  }
  // A + turn on the name row: a character, Shift = the position.
  if (ev.type == hw::InputType::EditTurn && page_ == kPgMain && list_.sel() == kName && list_.holdEdit()) {
    if (ev.shift) namePos_ = clampi(namePos_ + ev.delta, 0, kNameLen - 1);
    else list_.edit(ev.delta);
    return;
  }
  const int ov = onEditor() ? editor_.onInput(ev) : list_.onInput(ev);
  if (ov) {
    showPage(physPage() + ov, true);  // A + turn on the page bar
    return;
  }
  if (wasName && !nameEdit()) leaveEdit();
}

// B + Shift + turn: the previous / next page (not over the browsers); the cursor stays on the page bar.
void InstScreen::onPage(int d) {
  if (presets_.isOpen() || wt_.isOpen() || bounce_.isOpen() || extract_.isOpen() || wtMake_.isOpen()) return;
  showPage(physPage() + d, onEditor() ? editor_.barSelected() : list_.barSelected());
}

// B: closes the browser (as its long press), else leaves the edit with the value kept.
void InstScreen::onBack() {
  if (extract_.isOpen() || wtMake_.isOpen()) { extract_.cancel(); wtMake_.cancel(); syncSampleJob(); return; }
  if (presets_.isOpen()) presets_.onInput({hw::InputType::EncLong, 0, false});
  else if (bounce_.isOpen()) bounce_.onInput({hw::InputType::EncLong, 0, false});
  else if (wt_.isOpen()) wt_.onInput({hw::InputType::EncLong, 0, false});
  else if (onEditor() ? editor_.editing() : list_.editing()) leaveEdit();
}

void InstScreen::onTouch(const TouchEvent& ev) {
  if (extract_.isOpen()) { extract_.onTouch(ev); syncSampleJob(); return; }
  if (wtMake_.isOpen()) { wtMake_.onTouch(ev); syncSampleJob(); return; }
  if (presets_.isOpen()) {
    presets_.onTouch(ev);
    return;
  }
  if (bounce_.isOpen()) {
    bounce_.onTouch(ev);
    return;
  }
  if (wt_.isOpen()) {
    wt_.onTouch(ev);
    return;
  }
  if (ev.type == TouchType::Tap && ev.y < y0_ + kHeaderH) {
    if (ev.x < kLeftX1) changeInstr(-1);
    else if (ev.x >= kRightX0 && ev.x < kRightX1) changeInstr(1);
    else if (ev.x >= kPresetX0 && ev.x < kPresetX1) presetMenu();
    else if (ev.x >= kPrevX0 && ev.x < kPrevX1) preview();
    return;
  }
  if (ev.y >= y0_ + kHeaderH && ev.y < y0_ + kHeaderH + kPageBarH && ev.type != TouchType::Drag &&
      ev.type != TouchType::HDrag) {
    if (ev.type == TouchType::Tap) showPage(ev.x / pageW(), true);
    return;
  }
  const int waveTop = y0_ + kHeaderH + kPageBarH + kWaveY;
  if (ev.type == TouchType::Tap && synth() && (page_==kPgType || page_==kPgType2) &&
      ev.x>=kWaveX && ev.x<kWaveX+kWaveW && ev.y>=waveTop && ev.y<waveTop+kWaveH) {
    if (ev.y>=waveTop+kWaveH-kWaveToggleH) wave_.toggleLive();
    return;
  }
  if (ev.type == TouchType::Tap && !list_.editing() && viewRow(list_.rowAt(ev.y))) {
    list_.setSel(list_.rowAt(ev.y));
    wave_.toggleLive();
    return;
  }
  // Tap on a character of the name being edited moves the name cursor.
  if (ev.type == TouchType::Tap && nameEdit() && list_.rowAt(ev.y) == kName && ev.x >= ParamList::kValueX &&
      ev.x < ParamList::kValueX + kNameLen * kCharW) {
    namePos_ = (ev.x - ParamList::kValueX) / kCharW;
    return;
  }
  // Tap on a Table row (SYNTH OSC): the table picker.
  if (ev.type == TouchType::Tap && synth() && page_ == kPgType) {
    const int r = list_.rowAt(ev.y);
    if (r >= 0 && (kCommon + r == kTable1 || kCommon + r == kTable2)) {
      list_.setSel(r);
      openTables(kCommon + r == kTable2 ? 1 : 0);
      return;
    }
  }
  const bool wasName = nameEdit();
  if (onEditor()) editor_.onTouch(ev);
  else list_.onTouch(ev);
  if (wasName && !nameEdit()) leaveEdit();
}

void InstScreen::drawPageBar(LGFX_Sprite& s, int y) {
  static const char* const kTypeNames[] = {"OSC", "SMPL", "FM", "DRUM", "OSC", "LANES", "LOFI"};
  static_assert(sizeof(kTypeNames) / sizeof(kTypeNames[0]) == static_cast<int>(mt::InstrType::Count), "type page names");
  const int t = static_cast<int>(shown_);
  const char* const names[] = {"MAIN", "ENV", kTypeNames[t < static_cast<int>(mt::InstrType::Count) ? t : 0], "MOD", "FILT", "LFO", "MATRIX"};
  const int ty = y + (kPageBarH - 4 - kCharH) / 2;
  const int n = pageCount(), w = pageW(), cur = physPage();
  for (int i = 0; i < n; ++i) {
    const char* name = names[logicalPage(i)];
    const bool on = i == cur;
    s.fillRect(i * w + 1, y, w - 2, kPageBarH - 4, on ? kPlayBg : kBeatBg);
    s.setTextColor(on ? kCursor : kDim);
    s.drawString(name, i * w + (w - static_cast<int>(strlen(name)) * kCharW) / 2, ty);
  }
  if (onEditor() ? editor_.barSelected() : list_.barSelected()) PageBar::drawFocus(s, y, kPageBarH);
}

void InstScreen::drawEnv(LGFX_Sprite& s, int y) {
  // A, D, R widths ~ log2(1 + ms), the longest time (10 s) = a third of the room left by the plateau.
  const float kMaxLog = log2f(10001.f);
  constexpr int kRoom = (kEnvX1 - kEnvX0 - kEnvHold) / 3;
  const mt::Instrument& m = inst();
  auto w = [&](uint8_t v) { return static_cast<int>(log2f(1.f + mt::envTimeMs(v)) * kRoom / kMaxLog + 0.5f); };
  const int top = y + kEnvY0, bot = y + kEnvY1;
  const int sy = bot - m.sustain * (bot - top) / 127;
  const Param& a = typeRows()[kAttack];
  const uint16_t c = a.dim && a.dim() ? kDim : kText;
  const int x1 = kEnvX0 + w(m.attack), x2 = x1 + w(m.decay), x3 = x2 + kEnvHold, x4 = x3 + w(m.release);
  s.drawLine(kEnvX0, bot, x1, top, c);
  s.drawLine(x1, top, x2, sy, c);
  s.drawLine(x2, sy, x3, sy, c);
  s.drawLine(x3, sy, x4, bot, c);
}

void InstScreen::drawOsc(LGFX_Sprite& s, int y) {
  wave_.draw(s,inst(),kWaveX,y+kWaveY,kWaveW,kWaveH,page_==kPgType2,millis());
}

bool InstScreen::viewRow(int row) const {
  return synth() && ((page_==kPgType && row==kOscView-kCommon) ||
                     (page_==kPgType2 && row==kModView-kSub));
}

bool InstScreen::wantsRedraw(const engine::Status&) {
  return synth() && (page_==kPgType || page_==kPgType2) && !presets_.isOpen() &&
         !wt_.isOpen() && !bounce_.isOpen() && !extract_.isOpen() && !wtMake_.isOpen() &&
         wave_.wantsRedraw(millis());
}

void InstScreen::draw(LGFX_Sprite& s, int y0, int) {
  y0_ = y0;
  syncSampleJob();
  if (extract_.isOpen()) { extract_.draw(s,y0); return; }
  if (wtMake_.isOpen()) { wtMake_.draw(s,y0); return; }
  if (presets_.isOpen()) {
    presets_.draw(s, y0);
    return;
  }
  if (bounce_.isOpen()) {
    bounce_.draw(s, y0);
    return;
  }
  if (wt_.isOpen()) {
    wt_.draw(s, y0);
    return;
  }
  char buf[48];
  const int cy = y0 + kHeaderH / 2;
  const int ty = y0 + (kHeaderH - 4 - kCharH) / 2;
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  s.fillTriangle(24, cy - 2, 36, cy - 9, 36, cy + 5, kCursor);
  s.fillTriangle(kRightX0 + 36, cy - 2, kRightX0 + 24, cy - 9, kRightX0 + 24, cy + 5, kCursor);
  const int tr = app_.curTrack();
  const auto use = mt::instrumentUse(app_.project(), instr_, tr);
  char relation[20];
  snprintf(relation, sizeof(relation), !use.current ? "NOT ON T%02d" : use.currentViaKit ? "T%02d/KIT" : "T%02d", tr + 1);
  if (use.count > 1) snprintf(buf, sizeof(buf), "I%02d %s %s x%d", instr_ + 1, inst().name, relation, use.count);
  else snprintf(buf, sizeof(buf), "I%02d %s %s", instr_ + 1, inst().name, relation);
  s.setTextColor(kText);
  const int mid = (kLeftX1 + kRightX0) / 2;
  s.drawString(buf, mid - static_cast<int>(strlen(buf)) * kCharW / 2, ty);
  s.fillRect(kPresetX0, y0 + 2, kPresetX1 - kPresetX0, kHeaderH - 8, kPlayBg);
  s.fillRect(kPrevX0, y0 + 2, kPrevX1 - kPrevX0, kHeaderH - 8, kPlayBg);
  s.setTextColor(kCursor);
  s.drawString("PRESET", kPresetX0 + (kPresetX1 - kPresetX0 - 6 * kCharW) / 2, ty);
  s.drawString("PREVIEW", kPrevX0 + (kPrevX1 - kPrevX0 - 7 * kCharW) / 2, ty);

  drawPageBar(s, y0 + kHeaderH);
  const int ly = y0 + kHeaderH + kPageBarH;
  relabel();
  if (onEditor()) {
    editor_.draw(s, ly);
  } else {
    list_.draw(s, ly);
    if (page_ == kPgEnv) drawEnv(s, ly);
    else if (synth() && (page_ == kPgType || page_ == kPgType2)) drawOsc(s, ly);
  }
  if (nameEdit()) {
    const int uy = list_.rowY(kName) + (ParamList::kRowH + kCharH) / 2;
    s.fillRect(ParamList::kValueX + namePos_ * kCharW, uy, kCharW, 2, kEditCursor);
  }
}

}  // namespace ui

namespace ui {
bool InstScreen::onTrackInstr() const {
  return instr_ == app_.project().tracks[app_.curTrack()].instr % mt::kInstruments;
}
}
