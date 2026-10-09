#include "inst_screen.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "page_bar.h"
#include "audio/audio.h"
#include "audio/bank.h"
#include "name_edit.h"
#include "note_name.h"
#include "sample_set.h"
#include "synth_drum_machines.h"
#include "synth_fm_machines.h"
#include "wt_builtin.h"
#include "wt_mip.h"

namespace ui {
namespace {

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

void waveName(uint8_t w, char* o, int n) {
  static const char* const kNames[] = {"PULSE", "TRI", "SAW", "NOISE", "METAL"};
  constexpr int kWt = static_cast<int>(mt::Wave::Wt1);
  if (w < kWt) snprintf(o, n, "%s", kNames[w]);
  else snprintf(o, n, "WT%d", w - kWt + 1);
}

}  // namespace

InstScreen::InstScreen(App& app) : app_(app) {
  Param* const sets[] = {chip_, sample_, fm_, drum_, syn_};
  for (Param* p : sets) {
    p[kName] = {"Name", [this](char* o, int n) { snprintf(o, n, "%s", inst().name); },
                [this](int d) { editNameChar(inst().name, kNameLen, namePos_, d); }};
    p[kType] = {"Type",
                [this](char* o, int n) {
                  static const char* const kNames[] = {"CHIP", "SAMPLE", "FM", "DRUM", "SYNTH", "KIT"};
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
  chip_[kWave] = {"Wave", [this](char* o, int n) { waveName(inst().wave, o, n); },
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
                   [this](char* o, int n) {
                     static const char* const kNames[] = {"KICK", "SNARE", "METAL", "PERC",
                                                          "TONE", "CHORD", "CLAP", "HAT"};
                     snprintf(o, n, "%s", kNames[inst().machine % 8]);
                   },
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
  // DRUM: the machine shapes the whole sound; DECAY is a number (its range differs per machine).
  auto always = [] { return true; };
  for (int r : {kAttack, kDecay, kSustain, kRelease, kMode, kGlide}) drum_[r].dim = always;
  drum_[kMachine] = {"Machine",
                     [this](char* o, int n) { snprintf(o, n, "%s", mt::drumMachineName(inst().machine)); },
                     [this](int d) {
                       const int v = clampi(inst().machine + d, 0, static_cast<int>(mt::DrumMachine::Count) - 1);
                       if (v != inst().machine) mt::drumSetMachine(inst(), static_cast<uint8_t>(v));
                     }};
  for (int k = 0; k < mt::kFmMacros; ++k)
    drum_[kMac0 + k] = {"-", macroNum(k), macroEdit(k), [this, k] { return !*mt::drumMacroName(inst().machine, k); }};
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
  initTail(chip_ + kChipRows, false);
  initTail(sample_ + kCommon, false);
  initTail(fm_ + kMacRows, true);
  initTail(drum_ + kMacRows, true);
  initTail(syn_ + kSynRows, true);
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

void InstScreen::initTail(Param* t, bool macros) {
  t[kDrive] = {"Drive",
               [this](char* o, int n) {
                 if (inst().drive) snprintf(o, n, "%u", inst().drive);
                 else snprintf(o, n, "OFF");
               },
               [this](int d) { inst().drive = static_cast<uint8_t>(clampi(inst().drive + d, 0, 127)); }};
  // Lo-fi after the drive (fx BIT / SRR lock them per step).
  t[kBit] = {"Bit crush",
             [this](char* o, int n) {
               const int b = inst().crushBits;
               if (b) snprintf(o, n, "%d  (%d BIT)", b, 16 - (b * 14 + 63) / 127);
               else snprintf(o, n, "OFF");
             },
             [this](int d) { inst().crushBits = static_cast<uint8_t>(clampi(inst().crushBits + d, 0, 127)); }};
  t[kSrr] = {"Downsample",
             [this](char* o, int n) {
               if (inst().crushRate) snprintf(o, n, "%u", inst().crushRate);
               else snprintf(o, n, "OFF");
             },
             [this](int d) { inst().crushRate = static_cast<uint8_t>(clampi(inst().crushRate + d, 0, 127)); }};
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
  auto noLfo = [cfg] { return cfg().depth == 0; };
  t[kLfoSel] = {"LFO",
                [this](char* o, int n) {
                  int on = 0;
                  for (int i = 0; i < mt::kLfos; ++i) on += mt::lfoAt(inst(), i).depth != 0;
                  snprintf(o, n, "%d  (%d ON)", lfoSel_ + 1, on);
                },
                [this](int d) { lfoSel_ = clampi(lfoSel_ + d, 0, mt::kLfos - 1); }};
  t[kLfoWave] = {"Wave",
                 [cfg](char* o, int n) {
                   static const char* const kNames[] = {"SINE", "TRI", "SAW", "SQR", "RND"};
                   snprintf(o, n, "%s", kNames[cfg().wave % 5]);
                 },
                 [cfg, lfo](int d) {
                   const uint8_t w = static_cast<uint8_t>(clampi(cfg().wave + d, 0, static_cast<int>(mt::LfoWave::Count) - 1));
                   lfo().wave = w;
                 },
                 noLfo};
  // Sync: the rate in Hz or a tempo division; Trig: NOTE (restart at note-on) or FREE (one phase per
  // track, restarted at playback start). Both live in the sync byte (kLfoTempo | kLfoFree).
  t[kLfoSync] = {"Sync", [cfg](char* o, int n) { snprintf(o, n, "%s", mt::lfoTempo(cfg().sync) ? "TEMPO" : "HZ"); },
                 [cfg, lfo](int d) {
                   const bool on = d > 0;
                   const uint8_t s = cfg().sync;
                   if (on == mt::lfoTempo(s)) return;
                   const mt::LfoRef l = lfo();
                   l.sync = static_cast<uint8_t>(on ? (s | mt::kLfoTempo) : (s & ~mt::kLfoTempo));
                   l.rate = on ? 6 : 64;  // 1/4, or about 1.6 Hz
                 },
                 noLfo};
  t[kLfoTrig] = {"Trig", [cfg](char* o, int n) { snprintf(o, n, "%s", mt::lfoFree(cfg().sync) ? "FREE" : "NOTE"); },
                 [cfg, lfo](int d) {
                   const uint8_t s = cfg().sync;
                   const uint8_t v = static_cast<uint8_t>(d > 0 ? (s | mt::kLfoFree) : (s & ~mt::kLfoFree));
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
                 [this, cfg](char* o, int n) {
                   static const char* const kNames[] = {"PITCH", "DECAY", "COLOR", "SHAPE", "SWEEP",
                                                        "CONTOUR", "VOL",  "CUTOFF", "DRIVE"};
                   static const char* const kSyn[] = {"PITCH", "SHP1", "SHP2", "MIX", "DET",
                                                      "SENV",  "VOL",  "CUTOFF", "DRIVE"};
                   constexpr int kN = static_cast<int>(mt::LfoDest::Count);
                   static_assert(sizeof(kNames) / sizeof(kNames[0]) == kN, "LFO dest names");
                   static_assert(sizeof(kSyn) / sizeof(kSyn[0]) == kN, "LFO dest names");
                   const int dest = cfg().dest % kN;
                   const bool syn = inst().type == mt::InstrType::Synth;
                   snprintf(o, n, "%s", syn ? kSyn[dest] : kNames[dest]);
                 },
                 [cfg, lfo, macros](int d) {
                   const uint8_t v = mt::lfoDestStep(cfg().dest, d, macros);
                   lfo().dest = v;
                 },
                 noLfo};
}

void InstScreen::relabel() {
  if (shown_ != mt::InstrType::Drum) return;
  for (int k = 0; k < mt::kFmMacros; ++k) {
    const char* l = mt::drumMacroName(inst().machine, k);
    drum_[kMac0 + k].label = *l ? l : "-";
  }
}

mt::Instrument& InstScreen::inst() { return app_.project().instruments[instr_]; }

// Each encoder step applies the type, and a type change resets its own fields (KIT lanes, macros,
// machine). Leaving a type keeps a copy: turning back to it restores them instead of the defaults.
void InstScreen::setType(mt::InstrType v) {
  mt::Instrument& m = inst();
  if (v == m.type) return;
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
    default: return chip_;
  }
}

int InstScreen::typeCount() const {
  switch (shown_) {
    case mt::InstrType::Sample: return 0;  // SampleEditor
    case mt::InstrType::Fm:
    case mt::InstrType::Drum: return kMacRows - kCommon;
    case mt::InstrType::Synth: return kSynRows - kCommon;
    default: return kChipRows - kCommon;
  }
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
  const bool bar = onEditor() ? editor_.barSelected() : list_.barSelected();
  showPage(physPage(), bar);
  if (page_ != kPgType && page_ != kPgType2 && !bar) {
    list_.setSel(sel);
    list_.setEdit(ed);
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
  leaveEdit();
  typeSnapInstr_ = -1;
  instr_ = app_.project().tracks[app_.curTrack()].instr % mt::kInstruments;
  editor_.bind(instr_);
  syncParams();
}

void InstScreen::onLeave() {
  if (presets_.isOpen()) presets_.close(false);
  if (wt_.isOpen()) wt_.close();
}

void InstScreen::onProjectReplaced() {
  typeSnapInstr_ = -1;
  // The instrument the browsers would restore / set belongs to the old project.
  if (presets_.isOpen()) presets_.close(true);
  if (wt_.isOpen()) wt_.close();
  onEnter();
}

void InstScreen::afterPresets() {
  editor_.bind(instr_);
  syncParams();
  fixNames();
}

void InstScreen::openTables(int osc) {
  leaveEdit();
  wt_.open(instr_, osc);
}

void InstScreen::presetMenu() {
  leaveEdit();
  if (inst().type == mt::InstrType::Kit) {
    app_.toast("NO KIT PRESETS");
    return;
  }
  enum : int { kLoad, kSave };
  const MenuItem items[] = {{"Load", kLoad}, {"Save", kSave}};
  app_.menu().open("PRESET", items, 2, [this](int id) {
    typeSnapInstr_ = -1;  // a loaded preset replaces the instrument
    presets_.open(id == kSave ? PresetBrowser::Mode::Save : PresetBrowser::Mode::Load, instr_);
  });
}

void InstScreen::changeInstr(int d) {
  leaveEdit();
  instr_ = ((instr_ + d) % mt::kInstruments + mt::kInstruments) % mt::kInstruments;
  editor_.bind(instr_);
  syncParams();
}

bool InstScreen::buttonsBusy() const { return !presets_.isOpen() && onEditor(); }

bool InstScreen::trackKey(int n, bool shift) {
  if (shift || presets_.isOpen() || !onEditor()) return false;
  editor_.playSlice(n);
  return true;
}

void InstScreen::preview() {
  audio::preview(static_cast<uint8_t>(instr_), onEditor() ? editor_.previewNote() : kPreviewNote);
}

void InstScreen::onInput(const hw::InputEvent& ev) {
  if (presets_.isOpen()) {
    presets_.onInput(ev);
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
  const int ov = onEditor() ? editor_.onInput(ev) : list_.onInput(ev);
  if (ov) {
    showPage(physPage() + ov, true);  // click (Shift+click) on the page bar
    return;
  }
  if (wasName && !nameEdit()) leaveEdit();
}

void InstScreen::onTouch(const TouchEvent& ev) {
  if (presets_.isOpen()) {
    presets_.onTouch(ev);
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
  static const char* const kTypeNames[] = {"OSC", "SMPL", "FM", "DRUM", "OSC", "LANES"};
  static_assert(sizeof(kTypeNames) / sizeof(kTypeNames[0]) == static_cast<int>(mt::InstrType::Count), "type page names");
  const int t = static_cast<int>(shown_);
  const char* const names[] = {"MAIN", "ENV", kTypeNames[t < static_cast<int>(mt::InstrType::Count) ? t : 0], "MOD", "FILT", "LFO"};
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
  // Rows Osc1..Shape1 show osc 1, the rest osc 2.
  const int k = kCommon + list_.sel() < kOsc2 ? 0 : 1;
  const mt::Instrument& m = inst();
  const int x0 = kWtX0, x1 = kWtX1, top = y + kWtY0, bot = y + kWtY1, mid = (top + bot) / 2;
  const int pad = (bot - top) / 8;
  const int hi = top + pad, lo = bot - pad;
  s.drawFastHLine(x0, mid, x1 - x0, kBeatBg);
  const uint8_t shape = m.macro[mt::kMacShp1 + k] > 127 ? 127 : m.macro[mt::kMacShp1 + k];
  switch (static_cast<mt::SynOsc>(m.synOsc[k] % 4)) {
    case mt::SynOsc::Wt: {
      const mt::WtSource* w = audio::wavetableSource();
      const int16_t* t = w && m.synWt[k][0] ? w->findWt(m.synWt[k]) : nullptr;
      if (t) drawWtFrame(s, t, (shape * (mt::kWtFrames - 1) + 63) / 127, x0, top, x1, bot, kText);
      else s.drawLine(x0, mid, x1, mid, kRed);  // missing: silent
      break;
    }
    case mt::SynOsc::Square: {
      // PW 0.5..0.95 over SHAPE (as the voice).
      const int xp = x0 + static_cast<int>((x1 - x0) * (0.5f + 0.45f * shape / 127.f));
      s.drawLine(x0, lo, x0, hi, kText);
      s.drawLine(x0, hi, xp, hi, kText);
      s.drawLine(xp, hi, xp, lo, kText);
      s.drawLine(xp, lo, x1, lo, kText);
      break;
    }
    case mt::SynOsc::Tri: {
      const int q = (x1 - x0) / 4;
      s.drawLine(x0, mid, x0 + q, hi, kText);
      s.drawLine(x0 + q, hi, x1 - q, lo, kText);
      s.drawLine(x1 - q, lo, x1, mid, kText);
      break;
    }
    default:  // Saw
      s.drawLine(x0, mid, (x0 + x1) / 2, hi, kText);
      s.drawLine((x0 + x1) / 2, hi, (x0 + x1) / 2, lo, kText);
      s.drawLine((x0 + x1) / 2, lo, x1, mid, kText);
      break;
  }
  s.setTextColor(kDim);
  s.drawString(k ? "OSC2" : "OSC1", x0, bot + 4);
}

void InstScreen::draw(LGFX_Sprite& s, int y0, int) {
  y0_ = y0;
  if (presets_.isOpen()) {
    presets_.draw(s, y0);
    return;
  }
  if (wt_.isOpen()) {
    wt_.draw(s, y0);
    return;
  }
  char buf[24];
  const int cy = y0 + kHeaderH / 2;
  const int ty = y0 + (kHeaderH - 4 - kCharH) / 2;
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  s.fillTriangle(24, cy - 2, 36, cy - 9, 36, cy + 5, kCursor);
  s.fillTriangle(kRightX0 + 36, cy - 2, kRightX0 + 24, cy - 9, kRightX0 + 24, cy + 5, kCursor);
  snprintf(buf, sizeof(buf), "INS%d %s", instr_ + 1, inst().name);
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
    else if (synth() && page_ == kPgType) drawOsc(s, ly);
  }
  if (nameEdit()) {
    const int uy = list_.rowY(kName) + (ParamList::kRowH + kCharH) / 2;
    s.fillRect(ParamList::kValueX + namePos_ * kCharW, uy, kCharW, 2, kEditCursor);
  }
}

}  // namespace ui
