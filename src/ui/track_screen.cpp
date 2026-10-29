#include "track_screen.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "audio/audio.h"
#include "bytebeat_lib.h"
#include "fx_info.h"
#include "mut.h"
#include "name_edit.h"
#include "note_name.h"
#include "esp_random.h"
#include "scene.h"

namespace ui {
namespace {

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

constexpr int kVarGridW = 128;  // VAR header: the cells' width at most (1 px per step at kMaxSteps)
mt::VarTrack heardVarSt;        // UI task only (VAR header)

void onOff(bool v, char* out, int n) { snprintf(out, n, "%s", v ? "ON" : "OFF"); }

}  // namespace

TrackScreen::TrackScreen(App& app) : app_(app) {
  params_[kName] = {"Name", [this](char* o, int n) { snprintf(o, n, "%s", cfg().name); },
                    [this](int d) { editName(d); }};
  params_[kOut] = {"Out", [this](char* o, int n) { snprintf(o, n, "%s", internal() ? "INT" : "MIDI"); },
                   [this](int d) {
                     const int v = clampi(static_cast<int>(cfg().out) + d, 0, static_cast<int>(mt::TrackOut::Count) - 1);
                     if (v == static_cast<int>(cfg().out)) return;
                     cfg().out = static_cast<mt::TrackOut>(v);
                     // Unheard steps are replanned for the new output, sounding notes end.
                     engine::post(engine::Cmd::ReleaseTies);
                     engine::post(engine::Cmd::TrackOut, static_cast<uint16_t>(app_.curTrack()));
                   }};
  params_[kInstr] = {"Instr",
                     [this](char* o, int n) {
                       const int i = cfg().instr % mt::kInstruments;
                       snprintf(o, n, "%d %s", i + 1, app_.project().instruments[i].name);
                     },
                     [this](int d) {
                       const int v = clampi(cfg().instr + d, 0, mt::kInstruments - 1);
                       if (v == cfg().instr) return;
                       cfg().instr = static_cast<uint8_t>(v);
                       if (internal()) engine::post(engine::Cmd::SendProgram, static_cast<uint16_t>(app_.curTrack()));
                     },
                     [this] { return !internal(); }};
  params_[kVol] = {"Volume", [this](char* o, int n) { snprintf(o, n, "%u", cfg().vol); },
                   [this](int d) { cfg().vol = static_cast<uint8_t>(clampi(cfg().vol + d, 0, 127)); },
                   [this] { return !internal(); }};
  params_[kPan] = {"Pan",
                   [this](char* o, int n) {
                     char v[5];
                     mt::fxFormat(mt::Fx::PAN, cfg().pan, v);
                     snprintf(o, n, "%s", v);
                   },
                   [this](int d) { cfg().pan = static_cast<uint8_t>(clampi(cfg().pan + d, 0, 127)); },
                   [this] { return !internal(); }};
  auto midiOnly = [this] { return internal(); };
  // The insert chain (INT): drive and its curve, then the lo-fi (fx DRV / DTY / BIT / SRR lock them
  // per step).
  auto offOr = [](uint8_t v, char* o, int n) {
    if (v) snprintf(o, n, "%u", v);
    else snprintf(o, n, "OFF");
  };
  params_[kDrive] = {"Drive", [this, offOr](char* o, int n) { offOr(cfg().drive, o, n); },
                     [this](int d) { cfg().drive = static_cast<uint8_t>(clampi(cfg().drive + d, 0, 127)); },
                     [this] { return !internal(); }};
  // The drive's curve: grey while the drive is off (drive 0 bypasses every type).
  params_[kDriveType] = {"Drive type",
                         [this](char* o, int n) {
                           static const char* const kNames[] = {"TANH", "CLIP", "FOLD", "RECT", "ASYM"};
                           static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<int>(mt::DriveModel::Count),
                                         "Drive type names");
                           const uint8_t m = cfg().driveModel;
                           snprintf(o, n, "%s", kNames[m < static_cast<uint8_t>(mt::DriveModel::Count) ? m : 0]);
                         },
                         [this](int d) {
                           const int m = cfg().driveModel < static_cast<uint8_t>(mt::DriveModel::Count) ? cfg().driveModel : 0;
                           cfg().driveModel = static_cast<uint8_t>(
                               clampi(m + d, 0, static_cast<int>(mt::DriveModel::Count) - 1));
                         },
                         [this] { return !internal() || cfg().drive == 0; }};
  params_[kBitCrush] = {"Bit crush",
                        [this](char* o, int n) {
                          const int b = cfg().crushBits;
                          if (b) snprintf(o, n, "%d  (%d BIT)", b, 16 - (b * 14 + 63) / 127);
                          else snprintf(o, n, "OFF");
                        },
                        [this](int d) { cfg().crushBits = static_cast<uint8_t>(clampi(cfg().crushBits + d, 0, 127)); },
                        [this] { return !internal(); }};
  params_[kDownsample] = {"Downsample", [this, offOr](char* o, int n) { offOr(cfg().crushRate, o, n); },
                          [this](int d) { cfg().crushRate = static_cast<uint8_t>(clampi(cfg().crushRate + d, 0, 127)); },
                          [this] { return !internal(); }};
  // Trance gate (INT): depth (OFF = bypass), then its pattern; the rows below dim while it is OFF.
  auto gateDim = [this] { return !internal() || cfg().gateDepth == 0; };
  params_[kTrance] = {"Trance", [this, offOr](char* o, int n) { offOr(cfg().gateDepth, o, n); },
                      [this](int d) { cfg().gateDepth = static_cast<uint8_t>(clampi(cfg().gateDepth + d, 0, 127)); },
                      [this] { return !internal(); }};
  params_[kTrPattern] = {"Pattern",
                         [this](char* o, int n) {
                           const int i = mt::gatePresetOf(cfg().gateMask);
                           snprintf(o, n, "%s", i < 0 ? "USER" : mt::kGatePreset[i].name);
                         },
                         [this](int d) {
                           const int i = mt::gatePresetOf(cfg().gateMask);
                           constexpr int kLast = mt::kGatePresets - 1;
                           const int ni = i < 0 ? (d > 0 ? 0 : kLast) : clampi(i + d, 0, kLast);
                           cfg().gateMask = mt::kGatePreset[ni].mask;
                         },
                         gateDim};
  // Drawn as cells (drawSteps); edit = the cursor (drag), the input goes through onInput.
  params_[kTrSteps] = {"Steps", [](char* o, int) { o[0] = 0; },
                       [this](int d) { gateCur_ = clampi(gateCur_ + d, 0, 15); }, gateDim};
  params_[kTrRate] = {"Rate",
                      [this](char* o, int n) {
                        static const char* const kNames[] = {"1/32", "1/16", "1/8"};
                        static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<int>(mt::GateRate::Count),
                                      "Rate names");
                        const uint8_t r = cfg().gateRate;
                        snprintf(o, n, "%s", kNames[r < static_cast<uint8_t>(mt::GateRate::Count) ? r : 1]);
                      },
                      [this](int d) {
                        const int r = cfg().gateRate < static_cast<uint8_t>(mt::GateRate::Count) ? cfg().gateRate : 1;
                        cfg().gateRate =
                            static_cast<uint8_t>(clampi(r + d, 0, static_cast<int>(mt::GateRate::Count) - 1));
                      },
                      gateDim};
  params_[kTrLen] = {"Length", [this](char* o, int n) { snprintf(o, n, "%u%%", cfg().gateLen); },
                     [this](int d) { cfg().gateLen = static_cast<uint8_t>(clampi(cfg().gateLen + d, 1, 100)); },
                     gateDim};
  auto ms = [](uint8_t v, char* o, int n) {
    const float m = mt::gateMs(v);
    if (m < 10.f) snprintf(o, n, "%.1f ms", static_cast<double>(m));
    else snprintf(o, n, "%d ms", static_cast<int>(m + 0.5f));
  };
  params_[kTrAtk] = {"Attack", [this, ms](char* o, int n) { ms(cfg().gateAtk, o, n); },
                     [this](int d) { cfg().gateAtk = static_cast<uint8_t>(clampi(cfg().gateAtk + d, 0, 127)); },
                     gateDim};
  params_[kTrRel] = {"Release", [this, ms](char* o, int n) { ms(cfg().gateRel, o, n); },
                     [this](int d) { cfg().gateRel = static_cast<uint8_t>(clampi(cfg().gateRel + d, 0, 127)); },
                     gateDim};
  // The MOD send bus (PROJ > MOD): post-fader like the instrument's delay / reverb sends; fx MOD
  // locks it per step. Grey while the bus is OFF.
  params_[kModSend] = {"Mod send", [this](char* o, int n) { snprintf(o, n, "%u", cfg().modSend); },
                       [this](int d) { cfg().modSend = static_cast<uint8_t>(clampi(cfg().modSend + d, 0, 127)); },
                       [this] { return !internal() || app_.project().modType == 0; }};
  // Envelope follower (INT): this track's target follows the level of track Source (OFF = off).
  auto folDim = [this] { return !internal() || cfg().folSrc == 0; };
  params_[kFolSrc] = {"Source",
                      [this](char* o, int n) {
                        const int src = cfg().folSrc;
                        if (src < 1 || src > mt::kTracks) snprintf(o, n, "OFF");
                        else snprintf(o, n, "%d %s", src, app_.project().tracks[src - 1].name);
                      },
                      [this](int d) { cfg().folSrc = static_cast<uint8_t>(clampi(cfg().folSrc + d, 0, mt::kTracks)); },
                      [this] { return !internal(); }};
  params_[kFolDest] = {"Target",
                       [this](char* o, int n) {
                         const mt::InstrType ty = app_.project().instruments[cfg().instr % mt::kInstruments].type;
                         snprintf(o, n, "%s", mt::lfoDestName(cfg().folDest, ty));
                       },
                       [this](int d) {
                         const mt::InstrType ty = app_.project().instruments[cfg().instr % mt::kInstruments].type;
                         cfg().folDest = mt::folDestStep(cfg().folDest, d, ty);
                       },
                       folDim};
  params_[kFolAmt] = {"Amount", [this](char* o, int n) { snprintf(o, n, "%+d", cfg().folAmt); },
                      [this](int d) { cfg().folAmt = static_cast<int8_t>(clampi(cfg().folAmt + d, -64, 63)); },
                      folDim};
  auto envMs = [](uint8_t v, char* o, int n) {
    const int m = mt::envTimeMs(v);
    if (m >= 1000) snprintf(o, n, "%.1f s", m / 1000.0);
    else snprintf(o, n, "%d ms", m);
  };
  params_[kFolAtk] = {"Attack", [this, envMs](char* o, int n) { envMs(cfg().folAtk, o, n); },
                      [this](int d) { cfg().folAtk = static_cast<uint8_t>(clampi(cfg().folAtk + d, 0, 127)); },
                      folDim};
  params_[kFolRel] = {"Release", [this, envMs](char* o, int n) { envMs(cfg().folRel, o, n); },
                      [this](int d) { cfg().folRel = static_cast<uint8_t>(clampi(cfg().folRel + d, 0, 127)); },
                      folDim};
  params_[kChannel] = {"Channel", [this](char* o, int n) { snprintf(o, n, "%u", cfg().channel + 1); },
                       [this](int d) { cfg().channel = static_cast<uint8_t>(clampi(cfg().channel + d, 0, 15)); },
                       midiOnly};
  params_[kVel] = {"Def vel", [this](char* o, int n) { snprintf(o, n, "%u", cfg().defVel); },
                   [this](int d) { cfg().defVel = static_cast<uint8_t>(clampi(cfg().defVel + d, 1, 127)); }};
  params_[kGate] = {"Def gate", [this](char* o, int n) { snprintf(o, n, "%u%%", mt::gatePercent(cfg().defGate)); },
                    [this](int d) { cfg().defGate = static_cast<uint8_t>(clampi(cfg().defGate + d, 1, 200)); }};
  // The track's own length in the edited pattern (polymeter): OFF = the pattern length.
  params_[kPatLen] = {"Pat len",
                      [this](char* o, int n) {
                        const mt::Pattern& pt = app_.project().patterns[app_.editPattern()];
                        const uint8_t v = pt.trackLen[app_.curTrack()];
                        if (v && v < pt.length) snprintf(o, n, "%u / %u", v, pt.length);
                        else snprintf(o, n, "OFF");
                      },
                      [this](int d) {
                        mt::Pattern& pt = app_.project().patterns[app_.editPattern()];
                        uint8_t& v = pt.trackLen[app_.curTrack()];
                        // OFF below 1; the pattern length itself is OFF too.
                        const int cur = v && v < pt.length ? v : (d > 0 ? 0 : pt.length);
                        int nv = clampi(cur + d, 0, pt.length);
                        if (nv >= pt.length) nv = 0;
                        if (nv == v) return;
                        // Whole-pattern snapshot, once per run of Pat len edits on one pattern.
                        if (app_.editSeq() != patLenSeq_ || app_.editPattern() != patLenPat_) app_.pushUndo();
                        v = static_cast<uint8_t>(nv);
                        patLenSeq_ = app_.editSeq() + 1;  // onEdit marks dirty next
                        patLenPat_ = app_.editPattern();
                      }};
  // The track's speed in the edited pattern: 1/4 1/2 x1 x2 x4 (left to right).
  params_[kSpeed] = {"Speed",
                     [this](char* o, int n) {
                       const mt::Pattern& pt = app_.project().patterns[app_.editPattern()];
                       snprintf(o, n, "%s", mt::speedName(mt::toSpeed(pt.trackSpeed[app_.curTrack()])));
                     },
                     [this](int d) {
                       static constexpr mt::TrackSpeed kOrder[] = {mt::TrackSpeed::Quarter, mt::TrackSpeed::Half,
                                                                   mt::TrackSpeed::X1, mt::TrackSpeed::X2,
                                                                   mt::TrackSpeed::X4};
                       constexpr int kN = sizeof(kOrder) / sizeof(kOrder[0]);
                       mt::Pattern& pt = app_.project().patterns[app_.editPattern()];
                       uint8_t& v = pt.trackSpeed[app_.curTrack()];
                       int i = 0;
                       while (i < kN && kOrder[i] != mt::toSpeed(v)) ++i;
                       if (i == kN) i = 2;  // x1
                       const int ni = clampi(i + d, 0, kN - 1);
                       if (kOrder[ni] == mt::toSpeed(v)) return;
                       // Whole-pattern snapshot, once per run of Speed edits on one pattern (as Pat len).
                       if (app_.editSeq() != speedSeq_ || app_.editPattern() != speedPat_) app_.pushUndo();
                       v = static_cast<uint8_t>(kOrder[ni]);
                       speedSeq_ = app_.editSeq() + 1;  // onEdit marks dirty next
                       speedPat_ = app_.editPattern();
                     }};
  params_[kHumanize] = {"Humanize",
                        [this](char* o, int n) {
                          if (cfg().humanize) snprintf(o, n, "%u", cfg().humanize);
                          else snprintf(o, n, "OFF");
                        },
                        [this](int d) { cfg().humanize = static_cast<uint8_t>(clampi(cfg().humanize + d, 0, 100)); }};
  // GEN page (Project::gen): every edit replans the unheard steps.
  auto genDimF = [this] { return genDim(); };
  auto regDimF = [this] { return genRegDim(); };
  params_[kGenMode] = {"Gen",
                       [this](char* o, int n) {
                         static const char* const kN[] = {"OFF", "PITCH", "GATE", "BYTE PITCH", "BYTE GATE"};
                         static_assert(sizeof(kN) / sizeof(kN[0]) == static_cast<int>(mt::GenMode::Count), "Gen");
                         if (app_.project().trackIsDrum(app_.curTrack())) snprintf(o, n, "DRUM");
                         else snprintf(o, n, "%s", kN[gen().mode < static_cast<int>(mt::GenMode::Count) ? gen().mode : 0]);
                       },
                       [this](int d) {
                         gen().mode = static_cast<uint8_t>(
                             clampi(gen().mode + d, 0, static_cast<int>(mt::GenMode::Count) - 1));
                         genEdited();
                       },
                       [this] { return app_.project().trackIsDrum(app_.curTrack()); }};
  params_[kGenLen] = {"Length", [this](char* o, int n) { snprintf(o, n, "%u", gen().len); },
                      [this](int d) {
                        gen().len = static_cast<uint8_t>(clampi(gen().len + d, mt::kGenLenMin, mt::kGenLenMax));
                        genEdited();
                      },
                      regDimF};
  params_[kGenFlip] = {"Flip",
                       [this](char* o, int n) {
                         char v[8];
                         mt::genFlipText(gen().flip, v);
                         snprintf(o, n, "%s", v);
                       },
                       [this](int d) {
                         gen().flip = static_cast<uint8_t>(clampi(gen().flip + d, 0, 100));
                         genEdited();
                       },
                       regDimF};
  params_[kGenLow] = {"Low",
                      [this](char* o, int n) {
                        char v[4];
                        mt::noteName(gen().low, v);
                        snprintf(o, n, "%s", v);
                      },
                      [this](int d) {
                        gen().low = static_cast<uint8_t>(clampi(gen().low + d, 0, 127));
                        genEdited();
                      },
                      genDimF};
  params_[kGenOct] = {"Range", [this](char* o, int n) { snprintf(o, n, "%u OCT", gen().oct); },
                      [this](int d) {
                        gen().oct = static_cast<uint8_t>(clampi(gen().oct + d, 1, mt::kGenOctMax));
                        genEdited();
                      },
                      genDimF};
  params_[kGenDens] = {"Density", [this](char* o, int n) { snprintf(o, n, "%u%%", gen().density); },
                       [this](int d) {
                         gen().density = static_cast<uint8_t>(clampi(gen().density + d, 0, 100));
                         genEdited();
                       },
                       [this] { return genDim() || !mt::genGateMode(gen().mode); }};
  params_[kGenSeed] = {"Seed", [this](char* o, int n) { snprintf(o, n, "%u", gen().seed); },
                       [this](int d) {
                         gen().seed = static_cast<uint16_t>(clampi(gen().seed + d, 0, 65535));
                         engine::post(engine::Cmd::GenReseed, static_cast<uint16_t>(app_.curTrack()));
                       },
                       regDimF};
  // Drawn as cells (drawGen); no edit.
  params_[kGenReg] = {"Register", [](char* o, int) { o[0] = 0; }, nullptr, regDimF};
  // BYTE modes: the formula (bytebeat_lib.h) and its macros A B C (grey when the formula does not read
  // one), Zoom = the clock's window (1 << zoom ticks). A formula change loads its A B C.
  auto byteDimF = [this] { return genByteDim(); };
  params_[kGenFormula] = {"Formula", [this](char* o, int n) { snprintf(o, n, "%s", mt::byteFormula(gen().formula).name); },
                          [this](int d) {
                            const int c = mt::byteFormulaCount();
                            gen().formula = static_cast<uint8_t>(((gen().formula + d) % c + c) % c);
                            const mt::ByteFormula& f = mt::byteFormula(gen().formula);
                            gen().a = f.a;
                            gen().b = f.b;
                            gen().c = f.c;
                            genEdited();
                          },
                          byteDimF};
  static const char* const kMacN[] = {"A", "B", "C"};
  for (int k = 0; k < 3; ++k) {
    params_[kGenA + k] = {kMacN[k],
                          [this, k](char* o, int n) {
                            const uint8_t* m[] = {&gen().a, &gen().b, &gen().c};
                            snprintf(o, n, "%u", *m[k]);
                          },
                          [this, k](int d) {
                            uint8_t* m[] = {&gen().a, &gen().b, &gen().c};
                            *m[k] = static_cast<uint8_t>(clampi(*m[k] + d, 0, 127));
                            genEdited();
                          },
                          [this, k] { return genByteDim() || !(mt::byteUses(mt::byteCode(gen().formula)) >> k & 1); }};
  }
  params_[kGenZoom] = {"Zoom", [this](char* o, int n) { snprintf(o, n, "%u", gen().zoom); },
                       [this](int d) {
                         gen().zoom = static_cast<uint8_t>(clampi(gen().zoom + d, 0, mt::kGenZoomMax));
                         genEdited();
                       },
                       byteDimF};
  // MUT page (mut.h): Mut = the mode, one per turn with wrap through the modes the track offers (mutCycle); the
  // rows below are the active mode's (mutRows). Every edit replans the unheard steps.
  params_[kMutMode] = {"Mut",
                       [this](char* o, int n) {
                         static const char* const kN[] = {"OFF", "CA1D", "CA2D", "MARKOV", "EVO"};
                         static_assert(sizeof(kN) / sizeof(kN[0]) == static_cast<int>(mt::VarMode::Count), "Mut");
                         snprintf(o, n, "%s", kN[static_cast<int>(mutMode())]);
                       },
                       [this](int d) {  // one mode per turn (Shift too), from the shown one
                         const mt::VarMode next = mt::mutCycle(app_.project(), app_.curTrack(), d);
                         if (next == mutMode()) return;
                         mt::mutSetMode(app_.project(), app_.curTrack(), next);
                         mutEdited();
                       }};
  // EVO (Project::evo). Amount 1..100: OFF is the Mut row's (mutSetMode turns 0 into kEvoAmountOn).
  params_[kEvoAmt] = {"Amount", [this](char* o, int n) { snprintf(o, n, "%u%%", evo().amount); },
                      [this](int d) {
                        evo().amount = static_cast<uint8_t>(clampi(evo().amount + d, 1, 100));
                        mutEdited();
                      }};
  params_[kEvoEvery] = {"Every", [this](char* o, int n) { snprintf(o, n, "%u PASS", evo().every); },
                        [this](int d) {
                          evo().every = static_cast<uint8_t>(clampi(evo().every + d, 1, mt::kEvoEveryMax));
                          mutEdited();
                        }};
  params_[kEvoHome] = {"Home", [this](char* o, int n) { snprintf(o, n, "%u%%", evo().home); },
                       [this](int d) {
                         evo().home = static_cast<uint8_t>(clampi(evo().home + d, 0, 100));
                         mutEdited();
                       }};
  auto evoBit = [this](const char* label, uint8_t b, std::function<bool()> dim) {
    return Param{label, [this, b](char* o, int n) { snprintf(o, n, "%s", (evo().mask & b) ? "ON" : "OFF"); },
                 [this, b](int d) {
                   if (d > 0) evo().mask = static_cast<uint8_t>(evo().mask | b);
                   else if (d < 0) evo().mask = static_cast<uint8_t>(evo().mask & ~b);
                   mutEdited();
                 },
                 dim};
  };
  params_[kEvoNote] = evoBit("Note", mt::kEvoNote, [this] {  // drum / GEN tracks: never
    const mt::Project& p = app_.project();
    return p.trackIsDrum(app_.curTrack()) || p.genOn(app_.curTrack());
  });
  params_[kEvoRhythm] = evoBit("Rhythm", mt::kEvoRhythm, [this] {  // GEN GATE makes the rhythm
    return !mt::evoRhythmOk(app_.project(), app_.curTrack());
  });
  params_[kEvoRtrg] = evoBit("Rtrg+Vol", mt::kEvoRtrgVol, nullptr);
  params_[kEvoShift] = evoBit("Shift", mt::kEvoShift, nullptr);
  // CA1D / CA2D / MARKOV (Project::var). Every: both; Rule: CA1D's number, CA2D's rule2 name.
  params_[kVarEvery] = {"Every", [this](char* o, int n) { snprintf(o, n, "%u PASS", var().every); },
                        [this](int d) {
                          var().every = static_cast<uint8_t>(clampi(var().every + d, 1, mt::kVarEveryMax));
                          mutEdited();
                        }};
  params_[kVarRule] = {"Rule",
                       [this](char* o, int n) {
                         static const char* const kN[] = {"LIFE", "HIGHLIFE", "SEEDS", "DAYNIGHT", "MAZE"};
                         static_assert(sizeof(kN) / sizeof(kN[0]) == static_cast<int>(mt::VarRule2::Count), "Rule2");
                         if (mutMode() == mt::VarMode::Ca2d)
                           snprintf(o, n, "%s", kN[var().rule2 < static_cast<int>(mt::VarRule2::Count) ? var().rule2 : 0]);
                         else snprintf(o, n, "%u", var().rule);
                       },
                       [this](int d) {
                         if (mutMode() == mt::VarMode::Ca2d)
                           var().rule2 = static_cast<uint8_t>(clampi(var().rule2 + d, 0, static_cast<int>(mt::VarRule2::Count) - 1));
                         else var().rule = static_cast<uint8_t>(clampi(var().rule + d, 0, 255));
                         mutEdited();
                       }};
  params_[kVarReset] = {"Reset",
                        [this](char* o, int n) {
                          if (var().reset) snprintf(o, n, "%u GEN", var().reset);
                          else snprintf(o, n, "NEVER");
                        },
                        [this](int d) {
                          var().reset = static_cast<uint8_t>(clampi(var().reset + d, 0, mt::kVarResetMax));
                          mutEdited();
                        }};
  params_[kVarNoise] = {"Noise", [this](char* o, int n) { snprintf(o, n, "%u%%", var().noise); },
                        [this](int d) {
                          var().noise = static_cast<uint8_t>(clampi(var().noise + d, 0, 100));
                          mutEdited();
                        }};
  params_[kVarAmt] = {"Amount", [this](char* o, int n) { snprintf(o, n, "%u%%", var().amount); },
                      [this](int d) {
                        var().amount = static_cast<uint8_t>(clampi(var().amount + d, 0, 100));
                        mutEdited();
                      }};
  params_[kVarOrder] = {"Order", [this](char* o, int n) { snprintf(o, n, "%u", var().order); },
                        [this](int d) {
                          var().order = static_cast<uint8_t>(clampi(var().order + d, 1, 2));
                          mutEdited();
                        }};
  params_[kVarRhythm] = {"Rhythm", [this](char* o, int n) { onOff(var().rhythm, o, n); },
                         [this](int d) {
                           var().rhythm = d > 0;
                           mutEdited();
                         }};
  params_[kCcA] = {"CC A", [this](char* o, int n) { snprintf(o, n, "%u", cfg().ccA); },
                   [this](int d) { cfg().ccA = static_cast<uint8_t>(clampi(cfg().ccA + d, 0, 127)); }, midiOnly};
  params_[kCcB] = {"CC B", [this](char* o, int n) { snprintf(o, n, "%u", cfg().ccB); },
                   [this](int d) { cfg().ccB = static_cast<uint8_t>(clampi(cfg().ccB + d, 0, 127)); }, midiOnly};
  params_[kProgram] = {"Program",
                       [this](char* o, int n) {
                         if (cfg().program == mt::kNoProgram) snprintf(o, n, "---");
                         else snprintf(o, n, "%u", cfg().program);
                       },
                       [this](int d) {
                         const int v = cfg().program == mt::kNoProgram ? -1 : cfg().program;
                         const int nv = clampi(v + d, -1, 127);
                         cfg().program = nv < 0 ? mt::kNoProgram : static_cast<uint8_t>(nv);
                         if (!internal()) engine::post(engine::Cmd::SendProgram, static_cast<uint16_t>(app_.curTrack()));
                       },
                       midiOnly};
  params_[kMute] = {"Mute", [this](char* o, int n) { onOff(cfg().mute, o, n); },
                    [this](int d) { cfg().mute = d > 0; }};
  params_[kSolo] = {"Solo", [this](char* o, int n) { onOff(cfg().solo, o, n); },
                    [this](int d) { cfg().solo = d > 0; }};
  // The song slots' TRN plays on this track (Project::songTrnMask); a KIT track never follows it.
  params_[kSongTrn] = {"Song trn",
                       [this](char* o, int n) {
                         if (app_.project().trackIsDrum(app_.curTrack())) snprintf(o, n, "DRUM");
                         else onOff((app_.project().songTrnMask >> app_.curTrack()) & 1, o, n);
                       },
                       [this](int d) {
                         mt::Project& p = app_.project();
                         const uint16_t bit = static_cast<uint16_t>(1u << app_.curTrack());
                         const uint16_t m = d > 0 ? static_cast<uint16_t>(p.songTrnMask | bit)
                                                  : static_cast<uint16_t>(p.songTrnMask & ~bit);
                         if (m == p.songTrnMask) return;
                         // Already under lockProject() (ParamList::edit; the mutex is not recursive):
                         // the replan is posted before the engine plans on the new mask.
                         p.songTrnMask = m;
                         engine::post(engine::Cmd::SongEdit, static_cast<uint16_t>(mt::SongOp::Edit));  // replan
                       },
                       [this] { return app_.project().trackIsDrum(app_.curTrack()); }};
  for (int r : {kInstr, kVol, kPan, kDrive, kBitCrush, kDownsample, kTrance, kFolSrc})
    params_[r].hint = [] { return "INT tracks only"; };
  params_[kDriveType].hint = [this] { return !internal() ? "INT tracks only" : "Enable Drive"; };
  for (int r : {kTrPattern, kTrSteps, kTrRate, kTrLen, kTrAtk, kTrRel})
    params_[r].hint = [this] { return !internal() ? "INT tracks only" : "Enable Trance gate"; };
  params_[kModSend].hint = [this] { return !internal() ? "INT tracks only" : "Enable PROJ > MOD"; };
  for (int r : {kFolDest, kFolAmt, kFolAtk, kFolRel})
    params_[r].hint = [this] { return !internal() ? "INT tracks only" : "Choose follower Source"; };
  for (int r : {kChannel, kCcA, kCcB, kProgram})
    params_[r].hint = [] { return "MIDI tracks only"; };
  params_[kSongTrn].hint = [] { return "KIT tracks do not transpose"; };
  list_.setOnEdit([this] { app_.markDirty(); });
  // Cancel: the name is copied back (several characters may have changed); the other rows step
  // back through their edit(), which also sends what they send (program, output).
  list_.setEditScope([this]() -> void* { return nameEdit() ? cfg().name : nullptr; }, sizeof(mt::TrackCfg::name));
  list_.setOnCancel([this] {
    if (stepsActive_) {  // the Steps edit: its toggles went straight into the mask
      engine::lockProject();
      cfg().gateMask = stepsOrig_;
      engine::unlockProject();
      stepsActive_ = false;
    }
    namePos_ = 0;
    fixNames();
    app_.toast("CANCEL");
  });
  setPageRows(kPgMain, false);  // no leaveEdit(): the App is a global, built before the project exists
}

void TrackScreen::setPageRows(int page, bool bar) {
  page_ = (page % kPages + kPages) % kPages;
  list_.setPageBar(true);
  if (page_ == kPgMut) syncMut(false);
  else list_.setParams(params_ + kPageFirst[page_], kPageFirst[page_ + 1] - kPageFirst[page_]);
  list_.setVisibleRows(kVisibleRows);
  if (bar) list_.selectBar();
  else list_.setSel(0);
}

mt::TrackCfg& TrackScreen::cfg() { return app_.project().tracks[app_.curTrack()]; }

mt::GenCfg& TrackScreen::gen() { return app_.project().gen[app_.curTrack()]; }

bool TrackScreen::genDim() { return !app_.project().genOn(app_.curTrack()); }

bool TrackScreen::genRegDim() { return genDim() || mt::genByteMode(gen().mode); }

bool TrackScreen::genByteDim() { return genDim() || !mt::genByteMode(gen().mode); }

void TrackScreen::genEdited() { engine::post(engine::Cmd::GenEdit); }

void TrackScreen::reseed() {
  engine::lockProject();
  gen().seed = static_cast<uint16_t>(esp_random() & 0xFFFF);
  engine::unlockProject();
  engine::post(engine::Cmd::GenReseed, static_cast<uint16_t>(app_.curTrack()));
  app_.markDirty();
  char msg[20];
  snprintf(msg, sizeof(msg), "SEED %u", gen().seed);
  app_.toast(msg);
}

mt::EvoCfg& TrackScreen::evo() { return app_.project().evo[app_.curTrack()]; }

mt::VarMode TrackScreen::mutMode() { return mt::mutMode(app_.project(), app_.curTrack()); }

void TrackScreen::mutEdited() { engine::post(engine::Cmd::MutEdit); }

int TrackScreen::mutRows(mt::VarMode m, int* out) {
  static constexpr int kEvo[] = {kEvoAmt, kEvoEvery, kEvoHome, kEvoNote, kEvoRhythm, kEvoRtrg, kEvoShift};
  static constexpr int kCa[] = {kVarEvery, kVarRule, kVarReset, kVarNoise};
  static constexpr int kMarkov[] = {kVarEvery, kVarAmt, kVarOrder, kVarRhythm};
  const int* r = nullptr;
  int c = 0;
  switch (m) {
    case mt::VarMode::Evo: r = kEvo, c = sizeof(kEvo) / sizeof(kEvo[0]); break;
    case mt::VarMode::Ca1d:
    case mt::VarMode::Ca2d: r = kCa, c = sizeof(kCa) / sizeof(kCa[0]); break;
    case mt::VarMode::Markov: r = kMarkov, c = sizeof(kMarkov) / sizeof(kMarkov[0]); break;
    default: break;
  }
  int n = 0;
  out[n++] = kMutMode;
  for (int i = 0; i < c && n < kMutRowsMax; ++i) out[n++] = r[i];
  return n;
}

// Not from inside a row's edit (it replaces the Params): from draw / onInput / onTouch, and setPageRows.
void TrackScreen::syncMut(bool keep) {
  const mt::VarMode m = mutMode();
  if (keep && m == shownMut_) return;
  if (keep && list_.editing() && list_.sel() != 0) leaveEdit();  // the edited row may be gone
  shownMut_ = m;
  const int sel = list_.sel();
  const int was = keep && sel >= 0 && sel < mutShownN_ ? mutShownIds_[sel] : -1;  // the Row under the cursor
  mutShownN_ = mutRows(m, mutShownIds_);
  for (int i = 0; i < mutShownN_; ++i) mutShown_[i] = params_[mutShownIds_[i]];
  if (!keep) {
    list_.setParams(mutShown_, mutShownN_);
    return;
  }
  list_.replaceParams(mutShown_, mutShownN_);
  if (sel < 0) return;  // the page bar
  int at = 0;           // the row gone: Mut
  for (int i = 0; i < mutShownN_; ++i)
    if (mutShownIds_[i] == was) at = i;
  list_.setSel(at);
}

engine::EvoInfo TrackScreen::heardEvo(int& len) {
  engine::lockProject();
  len = mt::evoLen(app_.project().patterns[app_.status().pattern % mt::kPatterns], app_.curTrack());
  const engine::EvoInfo e = engine::heardEvoInfo(app_.curTrack(), len);
  engine::unlockProject();
  return e;
}

// MUT header in EVO: "MUT n  PASS c/every" left of the title, "CHG k/len" right of it (the heard state).
void TrackScreen::drawEvo(LGFX_Sprite& s, int y0) {
  int len = 0;
  const engine::EvoInfo e = heardEvo(len);
  shownEvoN_ = e.n;
  shownEvoPass_ = e.passes;
  shownEvoChg_ = e.changed;
  char buf[24];
  const int ty = y0 + (kHeaderH - 4 - kCharH) / 2;
  s.setTextColor(kCursor);
  snprintf(buf, sizeof(buf), "MUT %lu  PASS %u/%u", static_cast<unsigned long>(e.n), e.passes + 1u,
           static_cast<unsigned>(evo().every));
  s.drawString(buf, 44, ty);
  snprintf(buf, sizeof(buf), "CHG %d/%d", e.changed, len);
  s.drawString(buf, kScreenW - 44 - static_cast<int>(strlen(buf)) * kCharW, ty);
}

mt::VarCfg& TrackScreen::var() { return app_.project().var[app_.curTrack()]; }

engine::VarInfo TrackScreen::heardVarInfo() {
  engine::lockProject();
  const engine::VarInfo v = engine::heardVarInfo(app_.curTrack());
  engine::unlockProject();
  return v;
}

// MUT header in CA / MARKOV: "GEN g  PASS c/every" left of the title (g: generations since generation 0; MARKOV: since
// the start), right of it the heard state over the track's length: CA alive cells (KIT: lane 1 on top),
// MARKOV changed steps. Generation 0 (or a state of another mode) plays the pattern: its cells, no change.
void TrackScreen::drawVar(LGFX_Sprite& s, int y0) {
  const mt::Project& p = app_.project();
  const int tr = app_.curTrack();
  const mt::VarMode m = mutMode();
  uint8_t passes = 0;
  int len = 0;
  mt::VarApply a;
  engine::lockProject();
  const mt::Pattern& pt = p.patterns[app_.status().pattern % mt::kPatterns];
  engine::heardVar(tr, heardVarSt, passes);
  len = mt::evoLen(pt, tr);
  a = mt::varApplyOf(p, tr);
  const bool gen0 = heardVarSt.sinceReset == 0 || heardVarSt.mode != a.mode;
  if (gen0 && m != mt::VarMode::Markov) mt::varGen0(pt, tr, a.kit, heardVarSt.cells);
  engine::unlockProject();
  shownVarN_ = heardVarSt.n;
  shownVarGen_ = heardVarSt.sinceReset;
  shownVarPass_ = passes;
  char buf[24];
  const int ty = y0 + (kHeaderH - 4 - kCharH) / 2;
  const bool off = m == mt::VarMode::Off;
  s.setTextColor(off ? kDim : kCursor);
  const unsigned long g = m == mt::VarMode::Markov ? heardVarSt.n : heardVarSt.sinceReset;
  snprintf(buf, sizeof(buf), "GEN %lu  PASS %u/%u", g, passes + 1u, static_cast<unsigned>(var().every));
  s.drawString(buf, 44, ty);
  if (off || len <= 0) return;
  // kVarGridW px wide: cells of kVarGridW / len (>= 1) px, a 1 px gap from 3 px on.
  const int cw = len < kVarGridW ? kVarGridW / len : 1, x0 = kScreenW - 44 - cw * len;
  const int w = cw >= 3 ? cw - 1 : cw;
  const bool markov = m == mt::VarMode::Markov;
  const int rows = a.kit && !markov ? mt::kVarRows : 1;
  // KIT: 16 rows of 1 px in groups of 4 lanes, 2 px between groups: 16 + 3 * 2 = 22 px of the 24 px header.
  const int rh = rows > 1 ? 1 : 8, gap = 2;
  const int h = rows > 1 ? rows * rh + (rows / 4 - 1) * gap : rh;
  static_assert(mt::kVarRows * 1 + (mt::kVarRows / 4 - 1) * 2 <= kHeaderH - 4, "MUT CA rows fit the header");
  const int top = y0 + (kHeaderH - 4 - h) / 2;
  for (int r = 0; r < rows; ++r)
    for (int i = 0; i < len; ++i) {
      bool on;
      if (markov) on = !gen0 && heardVarSt.src[i] != i;
      else on = mt::varBit(heardVarSt.cells.r[r], i);
      s.fillRect(x0 + i * cw, top + r * rh + (r / 4) * gap, w, rh, on ? kCursor : kDim);
    }
}

mt::GenState TrackScreen::heardGen() {
  engine::lockProject();
  const mt::GenState g = engine::heardGen(app_.curTrack());
  engine::unlockProject();
  return g;
}

// GEN page: the Register row's cells and, in the header, the note (and the gate in GATE) it gives now.
void TrackScreen::drawGen(LGFX_Sprite& s, int y0) {
  const mt::Project& p = app_.project();
  const mt::GenCfg& g = gen();
  const mt::GenState st = heardGen();
  if (st.n != shownGenN_ && st.flipped) flipAt_ = millis() | 1;
  shownGen_ = st.reg;
  shownGenN_ = st.n;
  flipShown_ = flipAt_ && millis() - flipAt_ < kFlipMs;
  const bool dim = genDim();
  const int y = list_.rowY(kGenReg - kGenMode);
  if (list_.rowAt(y + 1) == kGenRegRow) {
    const uint16_t col = genRegDim() ? kDim : kText;  // BYTE: the register shifts on, unused
    const int cy = y + (ParamList::kRowH - kCellW) / 2;
    const int len = clampi(g.len, mt::kGenLenMin, mt::kGenLenMax);
    for (int i = 0; i < len; ++i) {
      const int x = cellX(i);
      const bool on = (st.reg >> i) & 1;
      const uint16_t c = i == 0 && flipShown_ ? kEditCursor : col;
      if (on) s.fillRect(x, cy, kCellW, kCellW, c);
      else s.drawRect(x, cy, kCellW, kCellW, c);
    }
  }
  if (dim) return;
  char nn[4], buf[12];
  mt::noteName(mt::genNote(st, g, p.scaleRoot, static_cast<mt::ScaleType>(p.scaleType)), nn);
  snprintf(buf, sizeof(buf), "%s", nn);
  const int x = kScreenW - 44 - 5 * kCharW, ty = y0 + (kHeaderH - 4 - kCharH) / 2;
  s.setTextColor(kCursor);
  s.drawString(buf, x, ty);
  if (mt::genGateMode(g.mode) && mt::genGate(st, g))
    s.fillCircle(x + 4 * kCharW + 3, y0 + (kHeaderH - 4) / 2, 3, kCursor);
}

void TrackScreen::fixNames() {
  mt::Project& p = app_.project();
  for (int t = 0; t < mt::kTracks; ++t) {
    if (p.tracks[t].name[0]) continue;
    engine::lockProject();
    snprintf(p.tracks[t].name, sizeof(p.tracks[t].name), "TRK%d", t + 1);
    engine::unlockProject();
  }
}

void TrackScreen::leaveEdit() {
  list_.setEdit(false);
  namePos_ = 0;
  fixNames();
}

void TrackScreen::onLeave() {
  if (recShown()) app_.rec().hide();  // a SD take records on (App polls it)
  leaveEdit();
  panSel_ = -1;
  xfSel_ = kXfNone;
  setSceneView(false);
  audio::setMeters(false);  // the level meters cost render time: only while MIX is shown
}

// A new project: drop the SCENE view without restoring the parked fader (it belonged to the old one).
void TrackScreen::onProjectReplaced() {
  xfSaved_ = -1;
  scnView_ = scnCopy_ = false;
  scnPending_ = -1;
  xfSel_ = kXfNone;
  panSel_ = -1;
}

bool TrackScreen::knobRow(int& page, const char*& label) {
  if (mixer_) return false;
  // Name (the cursor's character) and Steps (the cursor) edit what the screen points at.
  if ((page_ == kPgMain && list_.sel() == kName) || (page_ == kPgIns && list_.sel() == kStepsRow)) return false;
  const Param* r = list_.knobRow(mt::kKnobLabel - 1);
  if (!r) return false;
  page = page_;
  label = r->label;
  return true;
}

bool TrackScreen::knobTurn(int, int page, const char* label, int delta, char* val, int n) {
  if (page < 0 || page >= kPages) return false;
  if (page == kPgMut) {  // the rows of the track's mode
    int ids[kMutRowsMax];
    const int c = mutRows(mutMode(), ids);
    for (int i = 0; i < c; ++i)
      if (turnRow(&params_[ids[i]], 1, label, mt::kKnobLabel - 1, delta, val, n)) return true;
    return false;
  }
  return turnRow(params_ + kPageFirst[page], kPageFirst[page + 1] - kPageFirst[page], label, mt::kKnobLabel - 1,
                 delta, val, n);
}

void TrackScreen::showPage(int page, bool bar) {
  leaveEdit();
  setPageRows(page, bar);
}

// The track changed under an open edit (a track button, GRID): the edit belonged to the old one.
void TrackScreen::followTrack() {
  if (app_.curTrack() == editTrack_) return;
  if (list_.editing()) leaveEdit();
  editTrack_ = app_.curTrack();
}

void TrackScreen::onEnter() {
  leaveEdit();
  editTrack_ = app_.curTrack();
  audio::setMeters(mixer_);
}

void TrackScreen::changeTrack(int d) {
  leaveEdit();
  app_.setCurTrack(app_.curTrack() + d);
}

void TrackScreen::editName(int delta) { editNameChar(cfg().name, kNameLen, namePos_, delta); }

void TrackScreen::setMixer(bool on) {
  if (on == mixer_) return;
  leaveEdit();
  panSel_ = -1;
  xfSel_ = kXfNone;
  if (!on) setSceneView(false);
  mixer_ = on;
  audio::setMeters(on);
  app_.invalidate();
}

void TrackScreen::onInput(const hw::InputEvent& ev) {
  followTrack();
  if (mixer_ && recShown()) {
    app_.rec().onInput(ev);
    return;
  }
  if (mixer_) {
    mixerInput(ev);
    return;
  }
  if (page_ == kPgMut) syncMut(true);
  syncSteps();
  // Steps editor: turn = cursor, Shift + turn = rotate, A + turn = toggle and step on.
  if (page_ == kPgIns && list_.sel() == kStepsRow) {
    if (stepsEdit() && ev.type == hw::InputType::EncTurn) {
      if (ev.shift) rotateSteps(ev.delta);
      else gateCur_ = clampi(gateCur_ + ev.delta, 0, 15);
      return;
    }
    if (ev.type == hw::InputType::EditTurn) {
      if (list_.holdEdit()) {
        syncSteps();
        const int dir = ev.delta > 0 ? 1 : -1;
        for (int k = 0; k < (ev.delta > 0 ? ev.delta : -ev.delta); ++k) {
          toggleStep(gateCur_);
          gateCur_ = clampi(gateCur_ + dir, 0, 15);
        }
      }
      return;
    }
  }
  // GEN: a click on Seed (not editing) = a new random seed.
  if (page_ == kPgGen && ev.type == hw::InputType::EncClick && !ev.shift && !list_.editing() &&
      list_.sel() == kGenSeedRow && !genRegDim()) {
    reseed();
    return;
  }
  const bool wasName = nameEdit();
  if (ev.type == hw::InputType::EncTurn && ev.shift) {
    if (wasName) {
      namePos_ = clampi(namePos_ + ev.delta, 0, kNameLen - 1);
      return;
    }
    if (!list_.editing()) {
      changeTrack(ev.delta);
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
  if (const int ov = list_.onInput(ev)) {  // A + turn on the page bar: the previous / next page
    showPage(page_ + ov, true);
    return;
  }
  syncSteps();
  if (wasName && !nameEdit()) leaveEdit();
}

void TrackScreen::syncSteps() {
  const bool on = stepsEdit();
  if (on && !stepsActive_) stepsOrig_ = cfg().gateMask;
  stepsActive_ = on;
}

void TrackScreen::toggleStep(int i) {
  engine::lockProject();
  cfg().gateMask = static_cast<uint16_t>(cfg().gateMask ^ (1u << (i & 15)));
  engine::unlockProject();
  app_.markDirty();
}

// Rotates the mask: +1 moves every step one later (step 16 wraps to 1).
void TrackScreen::rotateSteps(int d) {
  const int r = ((d % 16) + 16) % 16;
  if (!r) return;
  engine::lockProject();
  const uint32_t m = cfg().gateMask;
  cfg().gateMask = static_cast<uint16_t>((m << r | m >> (16 - r)) & 0xFFFF);
  engine::unlockProject();
  app_.markDirty();
}

bool TrackScreen::trackKey(int n, bool shift) {
  followTrack();
  syncSteps();
  if (sceneView() && !shift && n >= 0 && n < mt::kScenes) {  // SCENE: on release without a turn (trackUp)
    scnPending_ = n;
    return true;
  }
  if (shift || mixer_ || !stepsEdit() || n < 0 || n >= 8) return false;
  gateCur_ = (gateCur_ / 8) * 8 + n;
  toggleStep(gateCur_);
  return true;
}

// SCENE view: a track button released without a turn = edit scene N, or the CPY target.
void TrackScreen::trackUp(int n) {
  if (!sceneView() || n != scnPending_) {
    scnPending_ = -1;
    return;
  }
  scnPending_ = -1;
  {
    if (scnCopy_) {
      engine::lockProject();
      app_.project().scenes[n] = app_.project().scenes[scnEdit_];
      mt::syncSceneLive(app_.project());
      engine::unlockProject();
      app_.markDirty();
      char msg[24];
      snprintf(msg, sizeof(msg), "SCENE %d > %d", scnEdit_ + 1, n + 1);
      app_.toast(msg);
      scnCopy_ = false;
    }
    scnEdit_ = n;
    parkFader();
    app_.invalidate();
  }
}

int TrackScreen::gateStep() {
  const mt::TrackCfg& c = cfg();
  const float gp = audio::gatePos();
  if (gp < 0 || c.gateDepth == 0 || !internal()) return -1;
  return static_cast<int>(gp / mt::gateSixteenths(c.gateRate)) & 15;
}

// The Steps row's cells (4 groups of 4): filled = open; the cursor while editing, the playhead.
void TrackScreen::drawSteps(LGFX_Sprite& s) {
  const int y = list_.rowY(kStepsRow);
  if (list_.rowAt(y + 1) != kStepsRow) return;  // scrolled out
  const mt::TrackCfg& c = cfg();
  const bool edit = stepsEdit();
  const bool dim = !internal() || c.gateDepth == 0;
  const uint16_t col = edit ? kEditCursor : (dim ? kDim : kText);
  const int play = gateStep();
  shownGateStep_ = play;
  const int cy = y + (ParamList::kRowH - kCellW) / 2;
  for (int i = 0; i < 16; ++i) {
    const int x = cellX(i);
    const bool on = (c.gateMask >> i) & 1;
    if (i == play) s.fillRect(x - 1, cy - 1, kCellW + 2, kCellW + 2, kCursor);
    if (on) s.fillRect(x, cy, kCellW, kCellW, col);
    else {
      if (i == play) s.fillRect(x, cy, kCellW, kCellW, kBg);
      s.drawRect(x, cy, kCellW, kCellW, col);
    }
    if (edit && i == gateCur_) s.fillRect(x, cy + kCellW + 2, kCellW, 2, kCursor);
  }
}

// B + Shift + turn: the previous / next page; the cursor stays on the page bar if it was there.
void TrackScreen::onPage(int d) {
  if (mixer_) return;
  showPage(page_ + d, list_.barSelected());
}

// B: leaves the edit with the value kept (as the second click), a name edit gets fixNames(). MIX:
// drops the pan bar / crossfader selection, else PERF on / off.
void TrackScreen::onBack() {
  if (mixer_ && recShown()) {
    app_.rec().stop();  // a take: saved (the window stays, READY); none: back to the mixer
    app_.invalidate();
  } else if (mixer_ && scnView_) {
    if (scnCopy_) scnCopy_ = false;
    else setSceneView(false);
    app_.invalidate();
  } else if (mixer_ && (panSel_ >= 0 || xfSel_ != kXfNone)) {
    panSel_ = -1;
    xfSel_ = kXfNone;
  } else if (mixer_) {
    togglePerf();
  }
  else if (list_.editing()) leaveEdit();
}

bool TrackScreen::recShown() const { return app_.rec().shownIn(RecDialog::Mode::Sd); }

void TrackScreen::openRec() {
  if (!mixer_) return;
  setSceneView(false);
  panSel_ = -1;
  xfSel_ = kXfNone;
  app_.rec().openSd();
  app_.invalidate();
}

// REC: red and filled while a SD take records (blinking while armed), else a red outline.
void TrackScreen::drawRecBtn(LGFX_Sprite& s, int y0) {
  const RecDialog& r = app_.rec();
  const bool on = r.sdTaking() && (r.recording() || (millis() / 300) & 1);
  const int x = kMasterX + 2, y = y0 + kXfY + (kXfH - kBtnH) / 2, w = kMasterW - 4;
  if (on) s.fillRect(x, y, w, kBtnH, kRed);
  else s.drawRect(x, y, w, kBtnH, kRed);
  s.setTextColor(on ? kText : kRed);
  s.drawString("REC", x + (w - 3 * kCharW) / 2, y + (kBtnH - kCharH) / 2);
}

void TrackScreen::togglePerf() {
  const bool on = !app_.perf().on();
  app_.perf().setOn(on);
  app_.toast(on ? "PERF: HOLD A BUTTON" : "PERF OFF");
  app_.invalidate();
}

void TrackScreen::onTouch(const TouchEvent& ev) {
  followTrack();
  if (mixer_ && recShown()) {
    if (ev.type != TouchType::HDrag) app_.rec().onTouch(ev);
    return;
  }
  if (mixer_) {
    mixerTouch(ev);
    return;
  }
  if (page_ == kPgMut) syncMut(true);
  if (ev.type == TouchType::Tap && ev.y < y0_ + kHeaderH) {
    if (ev.x < kArrowW) changeTrack(-1);
    else if (ev.x >= kScreenW - kArrowW) changeTrack(1);
    return;
  }
  if (ev.y >= y0_ + kHeaderH && ev.y < y0_ + kHeaderH + PageBar::kH) {
    if (ev.type == TouchType::Tap) showPage(PageBar::at(ev.x, kPages), true);
    return;
  }
  // Tap on a character of the name being edited moves the name cursor.
  if (ev.type == TouchType::Tap && nameEdit() && list_.rowAt(ev.y) == kName && ev.x >= ParamList::kValueX &&
      ev.x < ParamList::kValueX + kNameLen * kCharW) {
    namePos_ = (ev.x - ParamList::kValueX) / kCharW;
    return;
  }
  // Tap on a cell of the Steps row being edited: toggles it.
  syncSteps();
  if (ev.type == TouchType::Tap && stepsEdit() && list_.rowAt(ev.y) == kStepsRow && ev.x >= cellX(0) &&
      ev.x < cellX(15) + kCellPitch) {
    for (int i = 15; i >= 0; --i)
      if (ev.x >= cellX(i)) {
        gateCur_ = i;
        toggleStep(i);
        break;
      }
    return;
  }
  const bool wasName = nameEdit();
  list_.onTouch(ev);
  syncSteps();
  if (wasName && !nameEdit()) leaveEdit();
}

void TrackScreen::draw(LGFX_Sprite& s, int y0, int) {
  y0_ = y0;
  followTrack();
  if (mixer_ && recShown()) {
    app_.rec().draw(s, y0);
    return;
  }
  if (mixer_) {
    drawMixer(s, y0);
    return;
  }
  char buf[48];
  const int t = app_.curTrack();
  const int cy = y0 + kHeaderH / 2;
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  s.fillTriangle(24, cy - 2, 36, cy - 9, 36, cy + 5, kCursor);
  s.fillTriangle(kScreenW - 24, cy - 2, kScreenW - 36, cy - 9, kScreenW - 36, cy + 5, kCursor);
  const auto& track = app_.project().tracks[t];
  if (track.out == mt::TrackOut::Int) {
    const int instrument = track.instr % mt::kInstruments;
    snprintf(buf, sizeof(buf), "T%02d %s | I%02d %s", t + 1, track.name, instrument + 1,
             app_.project().instruments[instrument].name);
  } else snprintf(buf, sizeof(buf), "T%02d %s | MIDI CH%02d", t + 1, track.name, track.channel + 1);
  s.setTextColor(kText);
  s.drawString(buf, (kScreenW - static_cast<int>(strlen(buf)) * kCharW) / 2, y0 + (kHeaderH - 4 - kCharH) / 2);

  static const char* const kNames[kPages] = {"MAIN", "INS", "FOL", "NOTE", "GEN", "MUT", "MIDI"};
  if (page_ == kPgMut) syncMut(true);  // a Mut edit, a track change or a load: the mode's rows
  PageBar::draw(s, y0 + kHeaderH, kNames, kPages, page_);
  if (list_.barSelected()) PageBar::drawFocus(s, y0 + kHeaderH);
  list_.draw(s, y0 + kHeaderH + PageBar::kH);
  if (nameEdit()) {
    const int uy = list_.rowY(kName) + (ParamList::kRowH + kCharH) / 2;
    s.fillRect(ParamList::kValueX + namePos_ * kCharW, uy, kCharW, 2, kEditCursor);
  }
  if (page_ == kPgIns) drawSteps(s);
  if (page_ == kPgFol) drawFolBar(s, y0);
  if (page_ == kPgGen) drawGen(s, y0);
  if (page_ == kPgMut) {  // the active mode's state (syncMut ran above)
    if (shownMut_ == mt::VarMode::Evo) drawEvo(s, y0);
    else if (shownMut_ != mt::VarMode::Off) drawVar(s, y0);
  }
}

int TrackScreen::folBarPx() const {
  const int t = app_.curTrack();
  const mt::TrackCfg& c = app_.project().tracks[t];
  if (c.out != mt::TrackOut::Int || c.folSrc == 0) return -1;
  const float l = audio::followerLevel(t);
  return clampi(static_cast<int>(l * kFolBarW + 0.5f), 0, kFolBarW);
}

// Right of the title: the envelope 0..1, grey frame (empty while the follower is off).
void TrackScreen::drawFolBar(LGFX_Sprite& s, int y0) {
  const int px = folBarPx();
  shownFolPx_ = px;
  const int x = kScreenW - 44 - kFolBarW, y = y0 + (kHeaderH - 4) / 2 - 4;
  s.drawRect(x - 1, y - 1, kFolBarW + 2, 10, kDim);
  if (px > 0) s.fillRect(x, y, px, 8, kCursor);
}

// ---- MIXER ----

int TrackScreen::firstTrack() const { return app_.curTrack() / kStrips * kStrips; }

// Mutes, solos, the half's volumes and pans, the pan selection and the master: the mixer redraws when they change (from here, the
// track buttons or another screen); the scope adds its own frames.
uint32_t TrackScreen::mixSignature() const {
  const mt::Project& p = app_.project();
  uint32_t h = 2166136261u;
  auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
  mix(static_cast<uint32_t>(app_.curTrack()));
  mix(p.masterVol);
  mix(static_cast<uint32_t>(panSel_));
  mix(p.xfade | p.sceneA << 8 | p.sceneB << 12 | static_cast<uint32_t>(xfSel_) << 16 |
      static_cast<uint32_t>(scnView_) << 20 | static_cast<uint32_t>(scnCopy_) << 21);
  mix(static_cast<uint32_t>(scnEdit_ | scnPar_ << 4 | scnMst_ << 8));
  if (scnView_) {
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&p.scenes[scnEdit_]);
    for (size_t i = 0; i < sizeof(mt::Scene); ++i) mix(b[i]);
  }
  for (int t = 0; t < mt::kTracks; ++t) mix(p.sceneLive[0].trkMask[t] | p.sceneLive[1].trkMask[t] << 16);
  mix(p.sceneLive[0].mstMask | p.sceneLive[1].mstMask << 16);
  for (int t = 0; t < mt::kTracks; ++t) mix(p.tracks[t].mute | p.tracks[t].solo << 1 | static_cast<uint32_t>(p.tracks[t].out) << 2);
  for (int k = 0; k < kStrips; ++k) {
    const mt::TrackCfg& t = p.tracks[firstTrack() + k];
    const mt::Instrument& in = p.instruments[t.instr % mt::kInstruments];
    mix(t.vol | in.send << 8 | in.rsend << 16 | static_cast<uint32_t>(t.pan) << 24);
  }
  return h;
}

bool TrackScreen::wantsRedraw(const engine::Status&) {
  if (!mixer_) {
    if (page_ == kPgFol) return folBarPx() != shownFolPx_;  // the follower's bar moved
    if (page_ == kPgGen) {  // the register moved, or a flip blink ends
      if (genDim()) return false;
      const mt::GenState g = heardGen();
      const bool blink = flipAt_ && millis() - flipAt_ < kFlipMs;
      return g.reg != shownGen_ || g.n != shownGenN_ || blink != flipShown_;
    }
    if (page_ == kPgMut) {  // the mode changed (other rows), a mutation / generation, a pass counted or a Capture
      const mt::VarMode m = mutMode();
      if (m != shownMut_) return true;
      if (m == mt::VarMode::Off) return false;
      if (m == mt::VarMode::Evo) {
        int len = 0;
        const engine::EvoInfo e = heardEvo(len);
        return e.n != shownEvoN_ || e.passes != shownEvoPass_ || e.changed != shownEvoChg_;
      }
      const engine::VarInfo v = heardVarInfo();
      return v.n != shownVarN_ || v.sinceReset != shownVarGen_ || v.passes != shownVarPass_;
    }
    return page_ == kPgIns && gateStep() != shownGateStep_;  // the gate's playhead moved
  }
  const uint32_t sig = mixSignature();
  const bool scope = !scnView_ && millis() - scopeMs_ >= kScopeMs;  // the SCENE view has no scope
  if (sig == mixSig_ && !scope) return false;
  mixSig_ = sig;
  return true;
}

void TrackScreen::setVol(int track, int v) {
  mt::TrackCfg& t = app_.project().tracks[track];
  if (t.out != mt::TrackOut::Int) return;  // MIDI: no volume here
  v = clampi(v, 0, 127);
  if (v == t.vol) return;
  engine::lockProject();
  t.vol = static_cast<uint8_t>(v);
  engine::unlockProject();
  app_.markDirty();
}

void TrackScreen::setPan(int track, int v) {
  mt::TrackCfg& t = app_.project().tracks[track];
  if (t.out != mt::TrackOut::Int) return;  // MIDI: no pan
  v = clampi(v, 0, 127);
  if (v == t.pan) return;
  app_.motionObserve(true, track);
  engine::lockProject();
  t.pan = static_cast<uint8_t>(v);
  engine::unlockProject();
  app_.motionObserve(false);
  app_.markDirty();
}

void TrackScreen::setMasterVol(int v) {
  v = clampi(v, 0, mt::kMasterVolMax);
  if (v == app_.project().masterVol) return;
  engine::lockProject();
  app_.project().masterVol = static_cast<uint8_t>(v);
  engine::unlockProject();
  app_.markDirty();
}

void TrackScreen::toggleMuteSolo(int track, bool solo) {
  mt::TrackCfg& t = app_.project().tracks[track];
  engine::lockProject();
  if (solo) t.solo = !t.solo;
  else t.mute = !t.mute;
  engine::unlockProject();
  app_.markDirty();
}

void TrackScreen::mixerInput(const hw::InputEvent& ev) {
  if (scnView_) {
    sceneInput(ev);
    return;
  }
  // The crossfader row selected: turn / A + turn move the fader or step the end's scene (Shift x8).
  if (xfSel_ != kXfNone && (ev.type == hw::InputType::EditTurn || ev.type == hw::InputType::EncTurn)) {
    const mt::Project& p = app_.project();
    if (xfSel_ == kXfFader) setXfade(p.xfade + ev.delta * (ev.shift ? 8 : 1));
    else setSceneEnd(xfSel_ == kXfEndB, ((xfSel_ == kXfEndB ? p.sceneB : p.sceneA) + ev.delta % mt::kScenes +
                                         mt::kScenes) % mt::kScenes);
    return;
  }
  // The selected pan bar went away (another half, a MIDI output, a project load): MAIN again.
  if (panSel_ >= 0 && (panSel_ / kStrips != firstTrack() / kStrips ||
                       app_.project().tracks[panSel_].out != mt::TrackOut::Int))
    panSel_ = -1;
  if (ev.type == hw::InputType::EditTurn) {  // A + turn: the selected pan (Shift x10), else the crossfader (x8)
    if (panSel_ >= 0) setPan(panSel_, app_.project().tracks[panSel_].pan + ev.delta * (ev.shift ? 10 : 1));
    else setXfade(app_.project().xfade + ev.delta * (ev.shift ? 8 : 1));
    return;
  }
  if (ev.type == hw::InputType::EncLong) {  // REC: a running SD take stops, else its window opens
    if (app_.rec().sdTaking()) app_.rec().stop();
    else openRec();
    app_.invalidate();
    return;
  }
  if (ev.type != hw::InputType::EncTurn) return;
  if (app_.perf().turn(ev.delta, ev.shift)) return;  // a held PERF effect's depth
  if (ev.shift) {  // A / B: the same strip in the other half
    panSel_ = -1;
    app_.setCurTrack((app_.curTrack() + kStrips) % mt::kTracks);
    app_.invalidate();
    return;
  }
  if (panSel_ >= 0) setPan(panSel_, app_.project().tracks[panSel_].pan + ev.delta);
  else setMasterVol(app_.project().masterVol + ev.delta);
}

bool TrackScreen::hitStrip(int x, int y, int& strip, Part& part) const {
  const int ly = y - y0_;
  if (x >= kMasterX && x < kMasterX + kMasterW) {
    strip = kMaster;
    if (ly < kFaderY) part = Part::Name;
    else if (ly < kFaderY + kFaderH + 8) part = Part::Fader;
    else if (ly >= kBtnY - 4 && ly < kBtnY + kBtnH + 4) part = Part::Perf;
    else return false;
    return true;
  }
  if (x < kStripX0 || x >= kStripX0 + kStrips * kStripW) return false;
  strip = (x - kStripX0) / kStripW;
  const int lx = (x - kStripX0) % kStripW;
  if (ly < kFaderY) part = Part::Name;
  else if (ly < kFaderY + kFaderH + 8) part = Part::Fader;
  else if (ly >= kPanY - 2 && ly < kPanY + 16) part = Part::Pan;  // the text and the bar
  else if (ly >= kBtnY - 4 && ly < kBtnY + kBtnH + 4) part = lx < 3 + kBtnW + 1 ? Part::Mute : Part::Solo;
  else return false;
  return true;
}

void TrackScreen::mixerTouch(const TouchEvent& ev) {
  if (ev.type == TouchType::HDrag) return;
  if (xfTouch(ev)) return;
  if (scnView_) {
    sceneTouch(ev);
    return;
  }
  // REC, beside the crossfader in the MAIN column.
  if (ev.type == TouchType::Tap && ev.x >= kMasterX && ev.y >= y0_ + kXfY && ev.y < y0_ + kXfY + kXfH + 2) {
    openRec();
    return;
  }
  int strip;
  Part part;
  // A drag keeps to the fader it started on.
  const int sx = ev.type == TouchType::Drag ? ev.x0 : ev.x, sy = ev.type == TouchType::Drag ? ev.y0 : ev.y;
  if (!hitStrip(sx, sy, strip, part)) return;
  if (ev.type == TouchType::Drag && part != Part::Fader) return;
  const int fy = y0_ + kFaderY;
  const int pos = clampi(ev.y - fy, 0, kFaderH);  // 0 = top
  if (strip == kMaster) {
    if (part == Part::Perf) {
      if (ev.type == TouchType::Tap) togglePerf();
      return;
    }
    if (part == Part::Fader) setMasterVol(mt::kMasterVolMax - pos * mt::kMasterVolMax / kFaderH);
    xfSel_ = kXfNone;
    app_.invalidate();
    return;
  }
  const int tr = firstTrack() + strip;
  if (ev.type == TouchType::Tap && (part == Part::Mute || part == Part::Solo)) {
    toggleMuteSolo(tr, part == Part::Solo);
    return;
  }
  if (part == Part::Pan) {  // tap: select / drop the pan bar (INT tracks); the encoder turns it
    if (ev.type == TouchType::Tap && app_.project().tracks[tr].out == mt::TrackOut::Int) {
      panSel_ = panSel_ == tr ? -1 : tr;
      xfSel_ = kXfNone;
      app_.invalidate();
    }
    return;
  }
  if (part == Part::Fader) setVol(tr, 127 - pos * 127 / kFaderH);
  app_.invalidate();
}

void TrackScreen::drawFader(LGFX_Sprite& s, int x, int y, int value, int max, uint16_t fill) {
  s.drawRect(x, y, kFaderW, kFaderH, kDim);
  const int h = clampi(value, 0, max) * (kFaderH - 2) / max;
  if (h > 0) s.fillRect(x + 1, y + kFaderH - 1 - h, kFaderW - 2, h, fill);
}

// Output scope under the strips: the last samples to the speaker, scaled to the recent peak (quiet
// material still fills the box; the meter shows the real level), the peak meter and CLIP (held 1 s).
void TrackScreen::drawScope(LGFX_Sprite& s, int y0) {
  scopeMs_ = millis();
  constexpr int kX = kStripX0, kW = kScopeW, kH = kScopeH;
  constexpr int kN = kW - kMeterW - 4;  // one sample per pixel
  const int y = y0 + kScopeY, mid = y + kH / 2;
  s.fillRect(kX, y, kW, kH, kBeatBg);
  s.drawFastHLine(kX, mid, kN, kDim);
  int16_t buf[kN];
  audio::scopeRead(buf, kN);
  int pk = 0;
  for (int i = 0; i < kN; ++i) pk = buf[i] < 0 ? (-buf[i] > pk ? -buf[i] : pk) : (buf[i] > pk ? buf[i] : pk);
  // Auto gain: the frame's peak fills ~90 % of the half height; fast down (louder), slow up, at most
  // x32 (-30 dB), so silence stays a line instead of magnified noise.
  const float want = pk > 0 ? 0.9f * 32768.f / pk : 32.f;
  const float target = want > 32.f ? 32.f : (want < 1.f ? 1.f : want);
  scopeGain_ = target < scopeGain_ ? target : scopeGain_ + (target - scopeGain_) * 0.1f;
  const float k = scopeGain_ * (kH / 2 - 1) / 32768.f;
  int prev = mid;
  for (int i = 0; i < kN; ++i) {
    int yy = mid - static_cast<int>(buf[i] * k);
    yy = clampi(yy, y, y + kH - 1);
    if (i) s.drawLine(kX + i - 1, prev, kX + i, yy, kGreen);
    prev = yy;
  }
  const int16_t peak = audio::scopePeak();
  if (peak >= 32000) clipMs_ = scopeMs_ | 1;
  meter_ = peak > meter_ ? peak : meter_ * 7 / 8;  // fast up, slow down
  const int mx = kX + kW - kMeterW;
  const int mh = meter_ * kH / 32768;
  s.fillRect(mx, y + kH - mh, kMeterW, mh, meter_ > 29000 ? kYellow : kGreen);
  s.setTextColor(kDim);
  char gain[8];
  snprintf(gain, sizeof(gain), "x%d", static_cast<int>(scopeGain_ + 0.5f));
  s.drawString(gain, kX + 4, y + 2);
  if (clipMs_ && scopeMs_ - clipMs_ < 1000) {
    s.setTextColor(kRed);
    s.drawString("CLIP", mx - 4 * kCharW - 6, y + 2);
  }
}

void TrackScreen::updateMeters() {
  float pk[mt::kTracks];
  audio::trackPeaks(pk);
  const uint32_t now = millis();
  for (int t = 0; t < mt::kTracks; ++t) {
    const float db = pk[t] > 1e-6f ? 20.f * log10f(pk[t]) : -120.f;
    float v = (db + 48.f) * (1.f / 48.f);
    v = v < 0 ? 0 : (v > 1 ? 1 : v);
    level_[t] = v > level_[t] ? v : (level_[t] - 0.06f > v ? level_[t] - 0.06f : v);
    if (pk[t] >= 0.99f) clipAt_[t] = now | 1;
  }
}

void TrackScreen::drawMixer(LGFX_Sprite& s, int y0) {
  if (scnView_) {
    drawScene(s, y0);
    drawPanel(s, y0);
    return;
  }
  const mt::Project& p = app_.project();
  char buf[12];
  updateMeters();
  const uint32_t now = millis();
  for (int k = 0; k < kStrips; ++k) {
    const int tr = firstTrack() + k;
    const mt::TrackCfg& t = p.tracks[tr];
    const bool internal = t.out == mt::TrackOut::Int;
    const bool audible = p.trackAudible(tr);
    const int x = kStripX0 + k * kStripW;
    snprintf(buf, sizeof(buf), "%.6s", t.name);  // 6 x 8 px fit the strip
    // A scene end holds one of the track's parameters: the name in yellow (the base may not be what sounds).
    bool scn = false;
    for (int k = 0; k < mt::kScnTrk && internal && !scn; ++k) scn = mt::sceneTrkLive(p, tr, k);
    s.setTextColor(t.solo ? kCursor : (scn ? kYellow : (audible ? kText : kDim)));
    s.drawString(buf, x + 2, y0 + kNameY);
    const int fx = x + (kStripW - 2 - kFaderW) / 2, fy = y0 + kFaderY;
    if (internal) drawFader(s, fx, fy, t.vol, 127, audible ? kText : kDim);
    else s.drawRect(fx, fy, kFaderW, kFaderH, kDim);
    if (internal && mt::sceneTrkLive(p, tr, mt::kScnVol)) {  // what sounds: a yellow tick over the base
      const int ev = mt::sceneTrkEff(p, tr, mt::kScnVol);
      s.fillRect(fx - 2, fy + kFaderH - 1 - ev * (kFaderH - 2) / 127, kFaderW + 4, 2, kYellow);
    }
    if (internal) {  // level meter: green, yellow above -6 dB, red for a second after full scale
      const int mx = fx + kFaderW + 3;
      s.fillRect(mx, fy, kMeterBarW, kFaderH, kBeatBg);
      const int mh = static_cast<int>(level_[tr] * kFaderH + 0.5f);
      const int yel = static_cast<int>(kFaderH * 42.f / 48.f);  // -6 dB
      const bool clip = clipAt_[tr] && now - clipAt_[tr] < 1000;
      if (mh > 0) s.fillRect(mx, fy + kFaderH - (mh < yel ? mh : yel), kMeterBarW, mh < yel ? mh : yel, kGreen);
      if (mh > yel) s.fillRect(mx, fy + kFaderH - mh, kMeterBarW, mh - yel, kYellow);
      if (clip) s.fillRect(mx, fy, kMeterBarW, 3, kRed);
    }
    if (internal) snprintf(buf, sizeof(buf), "%3u", t.vol);
    else snprintf(buf, sizeof(buf), "MIDI");
    s.setTextColor(internal ? kText : kDim);
    s.drawString(buf, x + 6, y0 + kValY);
    if (internal) {  // pan: "C" / "L12" / "R40" centred, the bar with a tick at the position under it
      const bool sel = panSel_ == tr;
      const int py = y0 + kPanY;
      if (sel) s.fillRect(x + 1, py - 2, kStripW - 4, 18, kBeatBg);
      mt::fxFormat(mt::Fx::PAN, t.pan, buf);
      s.setTextColor(sel ? kCursor : (audible ? kText : kDim));
      s.drawString(buf, x + (kStripW - 2 - static_cast<int>(strlen(buf)) * kCharW) / 2, py - 2);
      const int bx = x + 4, bw = kStripW - 10, by = py + kPanBarDy;
      s.fillRect(bx, by, bw, 2, kDim);
      s.fillRect(bx + t.pan * (bw - 2) / 127, by - 1, 2, 4, sel ? kCursor : kText);
      if (mt::sceneTrkLive(p, tr, mt::kScnPan))
        s.fillRect(bx + mt::sceneTrkEff(p, tr, mt::kScnPan) * (bw - 2) / 127, by - 3, 2, 3, kYellow);
    }
    const mt::Instrument& in = p.instruments[t.instr % mt::kInstruments];
    s.setTextColor(kDim);
    if (internal) snprintf(buf, sizeof(buf), "S%3u", in.send);
    else snprintf(buf, sizeof(buf), "S --");
    s.drawString(buf, x + 6, y0 + kSendY);
    if (internal) snprintf(buf, sizeof(buf), "R%3u", in.rsend);
    else snprintf(buf, sizeof(buf), "R --");
    s.drawString(buf, x + 6, y0 + kSendY + kSendDy);
    const int by = y0 + kBtnY;
    s.fillRect(x + 3, by, kBtnW, kBtnH, t.mute ? kRed : kBeatBg);
    s.fillRect(x + 3 + kBtnW + 2, by, kBtnW, kBtnH, t.solo ? kCursor : kBeatBg);
    s.setTextColor(t.mute ? kText : kDim);
    s.drawString("M", x + 3 + (kBtnW - kCharW) / 2, by + (kBtnH - kCharH) / 2);
    s.setTextColor(t.solo ? kBg : kDim);
    s.drawString("S", x + 3 + kBtnW + 2 + (kBtnW - kCharW) / 2, by + (kBtnH - kCharH) / 2);
  }
  // Master: masterVol 0..200 %; above 100 % the fill turns yellow (the soft clip works harder).
  s.drawFastVLine(kMasterX - 4, y0, kScopeY - 4, kDim);  // the crossfader stays left of it, the scope spans the width
  s.setTextColor(kText);
  s.drawString("MAIN", kMasterX + (kMasterW - 4 * kCharW) / 2, y0 + kNameY);
  const int mv = p.masterVol;
  drawFader(s, kMasterX + (kMasterW - kFaderW) / 2, y0 + kFaderY, mv, mt::kMasterVolMax, mv > 100 ? kCursor : kText);
  snprintf(buf, sizeof(buf), "%u%%", mv);
  s.drawString(buf, kMasterX + (kMasterW - static_cast<int>(strlen(buf)) * kCharW) / 2, y0 + kValY);
  // The half shown: A = tracks 1-8, B = 9-16 (Shift + turn).
  s.setTextColor(kCursor);
  const char* half = firstTrack() ? "B" : "A";
  s.drawString(half, kMasterX + (kMasterW - kCharW) / 2, y0 + kSendY);
  s.setTextColor(kDim);
  snprintf(buf, sizeof(buf), "%d-%d", firstTrack() + 1, firstTrack() + kStrips);
  s.drawString(buf, kMasterX + (kMasterW - static_cast<int>(strlen(buf)) * kCharW) / 2, y0 + kSendY + kSendDy);
  drawXfade(s, y0);
  // PERF (B or a tap): yellow while on, as the GRID / SONG badge.
  const bool perf = app_.perf().on();
  s.fillRect(kMasterX + 2, y0 + kBtnY, kMasterW - 4, kBtnH, perf ? kYellow : kBeatBg);
  s.setTextColor(perf ? kBg : kDim);
  s.drawString("PERF", kMasterX + (kMasterW - 4 * kCharW) / 2, y0 + kBtnY + (kBtnH - kCharH) / 2);
  drawRecBtn(s, y0);
  drawScope(s, y0);
}

// ---- crossfader and SCENE view (scene.h) ----

void TrackScreen::setXfade(int v) {
  v = clampi(v, 0, 127);
  mt::Project& p = app_.project();
  if (v == p.xfade) return;
  engine::lockProject();
  p.xfade = static_cast<uint8_t>(v);
  engine::unlockProject();
  app_.markDirty();
}

void TrackScreen::setSceneEnd(bool b, int idx) {
  mt::Project& p = app_.project();
  idx = clampi(idx, 0, mt::kScenes - 1);
  engine::lockProject();
  (b ? p.sceneB : p.sceneA) = static_cast<uint8_t>(idx);
  mt::syncSceneLive(p);
  engine::unlockProject();
  app_.markDirty();
  char msg[16];
  snprintf(msg, sizeof(msg), "%c = SCENE %d", b ? 'B' : 'A', idx + 1);
  app_.toast(msg);
}

// The crossfader row: A3 (tap: select the end), the bar (tap / drag: position, a tap selects it), B5;
// under the 8 strips only. In the SCENE view the panel (pnlTouch) has the area instead.
bool TrackScreen::xfTouch(const TouchEvent& ev) {
  if (scnView_) return false;
  const int sy = ev.type == TouchType::Drag ? ev.y0 : ev.y, sx = ev.type == TouchType::Drag ? ev.x0 : ev.x;
  if (sy < y0_ + kXfY || sy >= y0_ + kXfY + kXfH + 2) return false;  // above: the M / S buttons
  if (sx < kStripX0 || sx >= kXfX1) return false;                      // the MAIN column
  if (ev.type == TouchType::LongPress) return true;
  if (sx < kXfBarX0 - 2 || sx >= kXfBarX1 + 2) {  // an end label
    if (ev.type == TouchType::Tap) {
      const int want = sx < kXfBarX0 ? kXfEndA : kXfEndB;
      xfSel_ = xfSel_ == want ? kXfNone : want;
      panSel_ = -1;
      app_.invalidate();
    }
    return true;
  }
  const int v = clampi((ev.x - kXfBarX0) * 127 / (kXfBarX1 - kXfBarX0), 0, 127);
  if (ev.type == TouchType::Drag) {
    const uint32_t now = millis();
    if (now - xfMs_ < 10) return true;  // at most one write per 10 ms: the link sends the last
    xfMs_ = now;
    setXfade(v);
  } else if (ev.type == TouchType::Tap) {
    setXfade(v);
    xfSel_ = kXfFader;
    panSel_ = -1;
  }
  app_.invalidate();
  return true;
}

void TrackScreen::drawXfade(LGFX_Sprite& s, int y0) {
  const mt::Project& p = app_.project();
  const int y = y0 + kXfY, ty = y + (kXfH - kCharH) / 2;
  char buf[24];
  const bool live = !mt::sceneEmpty(p.sceneLive[0]) || !mt::sceneEmpty(p.sceneLive[1]);
  snprintf(buf, sizeof(buf), "A:%d", p.sceneA + 1);
  s.setTextColor(xfSel_ == kXfEndA ? kCursor : kText);
  if (xfSel_ == kXfEndA) s.fillRect(kStripX0, y, kXfLabW, kXfH, kBeatBg);
  s.drawString(buf, kStripX0 + 4, ty);
  snprintf(buf, sizeof(buf), "B:%d", p.sceneB + 1);
  s.setTextColor(xfSel_ == kXfEndB ? kCursor : kText);
  if (xfSel_ == kXfEndB) s.fillRect(kXfX1 - kXfLabW, y, kXfLabW, kXfH, kBeatBg);
  s.drawString(buf, kXfX1 - kXfLabW + 4, ty);
  const int my = y + kXfH / 2;
  s.fillRect(kXfBarX0, my - 1, kXfBarX1 - kXfBarX0, 2, live ? kText : kDim);
  const int kx = kXfBarX0 + p.xfade * (kXfBarX1 - kXfBarX0) / 127;
  const uint16_t kc = xfSel_ == kXfFader ? kCursor : (live ? kYellow : kDim);
  s.fillRect(kx - 4, y + 2, 9, kXfH - 4, kc);
}

void TrackScreen::parkFader() {
  if (!scnView_) return;
  const mt::Project& p = app_.project();
  if (scnEdit_ == p.sceneA) setXfade(0);
  else if (scnEdit_ == p.sceneB) setXfade(127);
}

void TrackScreen::setSceneView(bool on) {
  if (on == scnView_) return;
  if (on) {
    xfSaved_ = app_.project().xfade;
    scnView_ = true;
    scnCopy_ = false;
    xfSel_ = kXfNone;
    panSel_ = -1;
    scnEdit_ = app_.project().sceneB;
    parkFader();
  } else {
    scnView_ = scnCopy_ = false;
    if (xfSaved_ >= 0) setXfade(xfSaved_);
    xfSaved_ = -1;
  }
}

void TrackScreen::scnSetTrk(int track, int v) {
  mt::Project& p = app_.project();
  if (p.tracks[track].out != mt::TrackOut::Int) return;  // MIDI: no scene parameters
  engine::lockProject();
  mt::sceneSetTrk(p.scenes[scnEdit_], track, scnPar_, v);
  mt::syncSceneLive(p);
  engine::unlockProject();
  app_.markDirty();
}

void TrackScreen::scnNudgeTrk(int track, int d) {
  const mt::Project& p = app_.project();
  const mt::Scene& sc = p.scenes[scnEdit_];
  const int cur = mt::sceneTrkIsSet(sc, track, scnPar_) ? sc.trk[track][scnPar_] : mt::sceneTrkBase(p, track, scnPar_);
  scnSetTrk(track, clampi(cur + d, mt::sceneTrkMin(scnPar_), mt::sceneTrkMax(scnPar_)));
}

void TrackScreen::scnUnsetTrk(int track) {
  mt::Project& p = app_.project();
  engine::lockProject();
  mt::sceneUnsetTrk(p.scenes[scnEdit_], track, scnPar_);
  mt::syncSceneLive(p);
  engine::unlockProject();
  app_.markDirty();
}

void TrackScreen::scnNudgeMst(int d) {
  mt::Project& p = app_.project();
  mt::Scene& sc = p.scenes[scnEdit_];
  const int cur = mt::sceneMstIsSet(sc, scnMst_) ? sc.mst[scnMst_] : mt::sceneMstBase(p, scnMst_);
  engine::lockProject();
  mt::sceneSetMst(sc, scnMst_, cur + d);
  mt::syncSceneLive(p);
  engine::unlockProject();
  app_.markDirty();
}

void TrackScreen::sceneHoldTurn(int track, int d) {
  if (!sceneView()) return;
  scnPending_ = -1;  // the hold edited a value: no scene change on release
  scnNudgeTrk(track, d);
  char msg[24];
  const mt::Scene& sc = app_.project().scenes[scnEdit_];
  if (mt::sceneTrkIsSet(sc, track, scnPar_))
    snprintf(msg, sizeof(msg), "TRK%d %s %d", track + 1, mt::sceneTrkName(scnPar_), sc.trk[track][scnPar_]);
  else snprintf(msg, sizeof(msg), "TRK%d MIDI", track + 1);
  app_.toast(msg);
}

void TrackScreen::sceneHoldClick(int track) {
  if (!sceneView()) return;
  scnPending_ = -1;  // the hold did something: no scene change on release
  char msg[24];
  if (app_.project().tracks[track].out != mt::TrackOut::Int) {
    snprintf(msg, sizeof(msg), "TRK%d MIDI", track + 1);
  } else {
    scnUnsetTrk(track);
    snprintf(msg, sizeof(msg), "TRK%d %s ---", track + 1, mt::sceneTrkName(scnPar_));
  }
  app_.toast(msg);
}

void TrackScreen::sceneMasterParam(int d) {
  if (!sceneView()) return;
  scnMst_ = ((scnMst_ + d) % mt::kScnMst + mt::kScnMst) % mt::kScnMst;
  app_.invalidate();
}

void TrackScreen::sceneMasterUnset() {
  if (!sceneView()) return;
  mt::Project& p = app_.project();
  engine::lockProject();
  mt::sceneUnsetMst(p.scenes[scnEdit_], scnMst_);
  mt::syncSceneLive(p);
  engine::unlockProject();
  app_.markDirty();
  char msg[24];
  snprintf(msg, sizeof(msg), "%s ---", mt::sceneMstName(scnMst_));
  app_.toast(msg);
}

void TrackScreen::onATap(bool shift) {
  if (!mixer_ || !shift || recShown()) return;
  setSceneView(!scnView_);
  app_.invalidate();
}

void TrackScreen::scnCapture() {
  engine::lockProject();
  mt::sceneCaptureMix(app_.project(), scnEdit_);  // syncs sceneLive
  engine::unlockProject();
  app_.markDirty();
  app_.toast("CAPTURE MIX");
}

void TrackScreen::scnClear() {
  engine::lockProject();
  mt::sceneClear(app_.project(), scnEdit_);  // syncs sceneLive
  engine::unlockProject();
  app_.markDirty();
  app_.toast("SCENE CLEARED");
}

void TrackScreen::scnCopyArm() {
  scnCopy_ = true;
  app_.toast("COPY TO: TRACK BUTTON");
}

void TrackScreen::scnMenu() {
  enum { kCap, kClr, kCpy };
  const MenuItem items[] = {{"Capture mix", kCap}, {"Clear scene", kClr}, {"Copy scene", kCpy}};
  char title[16];
  snprintf(title, sizeof(title), "SCENE %d", scnEdit_ + 1);
  app_.menu().open(title, items, 3, [this](int id) {
    if (!sceneView()) return;
    if (id == kCap) scnCapture();
    else if (id == kClr) scnClear();
    else if (id == kCpy) scnCopyArm();
    app_.invalidate();
  });
}

// SCENE view encoder: turn alone nothing, Shift + turn = the track parameter, A + turn = the master
// value (Shift x8), long press = the menu. Held track buttons, B + turn and A + click come from App.
void TrackScreen::sceneInput(const hw::InputEvent& ev) {
  if (ev.type == hw::InputType::EditTurn) {
    scnNudgeMst(ev.delta * (ev.shift ? 8 : 1));
    return;
  }
  if (ev.type == hw::InputType::EncLong) {
    scnMenu();
    return;
  }
  if (ev.type != hw::InputType::EncTurn || !ev.shift) return;
  scnPar_ = ((scnPar_ + ev.delta) % mt::kScnTrk + mt::kScnTrk) % mt::kScnTrk;
  app_.invalidate();
}

// The panel's buttons: a point in a button or in the gap right of / under it counts for that button.
int TrackScreen::pnlHit(int x, int y) const {
  const int ly = y - y0_;
  if (ly < kPnlY || ly >= kAreaH || x < kStripX0 || x >= kStripX0 + kPnlW) return -1;
  const int col = clampi((x - kStripX0) / (kPnlBtnW + kPnlGap), 0, kPnlCols - 1);
  const int row = clampi((ly - kPnlY) / (kPnlRowH + kPnlGap), 0, 1);
  return row * kPnlCols + col;
}

void TrackScreen::pnlTouch(const TouchEvent& ev) {
  if (ev.type != TouchType::Tap) return;
  const int b = pnlHit(ev.x, ev.y);
  const bool left = ev.x < pnlX(b % kPnlCols) + kPnlBtnW / 2;  // < or > of SCN / VOL
  switch (b) {
    case kPbScn:
      scnEdit_ = (scnEdit_ + (left ? mt::kScenes - 1 : 1)) % mt::kScenes;
      scnCopy_ = false;
      parkFader();
      break;
    case kPbPar: scnPar_ = (scnPar_ + (left ? mt::kScnTrk - 1 : 1)) % mt::kScnTrk; break;
    case kPbCap: scnCapture(); break;
    case kPbClr: scnClear(); break;
    case kPbCpy:
      if (scnCopy_) scnCopy_ = false;
      else scnCopyArm();
      break;
    case kPbDone: setSceneView(false); break;
    default: return;  // =A / =B, X64: shown only
  }
  app_.invalidate();
}

void TrackScreen::sceneTouch(const TouchEvent& ev) {
  int strip;
  Part part;
  const int sx = ev.type == TouchType::Drag ? ev.x0 : ev.x, sy = ev.type == TouchType::Drag ? ev.y0 : ev.y;
  if (sy - y0_ >= kPnlY) {
    pnlTouch(ev);
    return;
  }
  if (!hitStrip(sx, sy, strip, part)) return;
  const int pos = clampi(ev.y - (y0_ + kFaderY), 0, kFaderH);  // 0 = top
  mt::Project& p = app_.project();
  if (strip == kMaster) {
    if (part == Part::Name && ev.type == TouchType::Tap) scnMst_ = (scnMst_ + 1) % mt::kScnMst;
    else if (part == Part::Fader && ev.type == TouchType::Tap && app_.shift()) {
      engine::lockProject();
      mt::sceneUnsetMst(p.scenes[scnEdit_], scnMst_);
      mt::syncSceneLive(p);
      engine::unlockProject();
      app_.markDirty();
    } else if (part == Part::Fader) {
      const int lo = mt::sceneMstMin(scnMst_), hi = mt::sceneMstMax(scnMst_);
      engine::lockProject();
      mt::sceneSetMst(p.scenes[scnEdit_], scnMst_, hi - pos * (hi - lo) / kFaderH);
      mt::syncSceneLive(p);
      engine::unlockProject();
      app_.markDirty();
    }
    app_.invalidate();
    return;
  }
  const int tr = firstTrack() + strip;
  if (part == Part::Name && ev.type == TouchType::Tap) app_.setCurTrack(tr);
  else if ((part == Part::Mute || part == Part::Solo) && ev.type == TouchType::Tap)
    toggleMuteSolo(tr, part == Part::Solo);
  else if (part == Part::Fader && ev.type == TouchType::Tap && app_.shift()) scnUnsetTrk(tr);
  else if (part == Part::Fader) {
    const int hi = mt::sceneTrkMax(scnPar_);
    scnSetTrk(tr, hi - (pos * hi + kFaderH / 2) / kFaderH);
  }
  app_.invalidate();
}

// The SCENE view's strips: the edited scene's parameter for the half's tracks (--- unset; the base under
// it dim), the master strip one master parameter.
void TrackScreen::drawScene(LGFX_Sprite& s, int y0) {
  const mt::Project& p = app_.project();
  const mt::Scene& sc = p.scenes[scnEdit_];
  char buf[12];
  for (int k = 0; k < kStrips; ++k) {
    const int tr = firstTrack() + k;
    const mt::TrackCfg& t = p.tracks[tr];
    const bool internal = t.out == mt::TrackOut::Int;
    const int x = kStripX0 + k * kStripW;
    if (tr == app_.curTrack()) s.fillRect(x, y0 + kNameY - 1, kStripW - 2, kCharH + 2, kBeatBg);
    snprintf(buf, sizeof(buf), "%.6s", t.name);
    s.setTextColor(tr == app_.curTrack() ? kCursor : (internal ? kText : kDim));
    s.drawString(buf, x + 2, y0 + kNameY);
    const int fx = x + (kStripW - 2 - kFaderW) / 2, fy = y0 + kFaderY;
    const bool set = internal && mt::sceneTrkIsSet(sc, tr, scnPar_);
    const int hi = mt::sceneTrkMax(scnPar_);
    if (set) drawFader(s, fx, fy, sc.trk[tr][scnPar_], hi, kYellow);
    else s.drawRect(fx, fy, kFaderW, kFaderH, kDim);
    if (!internal) snprintf(buf, sizeof(buf), "MIDI");
    else if (!set) snprintf(buf, sizeof(buf), "---");
    else if (scnPar_ == mt::kScnPan) mt::fxFormat(mt::Fx::PAN, sc.trk[tr][scnPar_], buf);
    else snprintf(buf, sizeof(buf), "%3u", sc.trk[tr][scnPar_]);
    s.setTextColor(set ? kYellow : kDim);
    s.drawString(buf, x + 6, y0 + kValY);
    if (internal) {  // the project's value, for reference
      snprintf(buf, sizeof(buf), "b%u", mt::sceneTrkBase(p, tr, scnPar_));
      s.setTextColor(kDim);
      s.drawString(buf, x + 6, y0 + kPanY - 2);
    }
    const int by = y0 + kBtnY;
    s.fillRect(x + 3, by, kBtnW, kBtnH, t.mute ? kRed : kBeatBg);
    s.fillRect(x + 3 + kBtnW + 2, by, kBtnW, kBtnH, t.solo ? kCursor : kBeatBg);
    s.setTextColor(t.mute ? kText : kDim);
    s.drawString("M", x + 3 + (kBtnW - kCharW) / 2, by + (kBtnH - kCharH) / 2);
    s.setTextColor(t.solo ? kBg : kDim);
    s.drawString("S", x + 3 + kBtnW + 2 + (kBtnW - kCharW) / 2, by + (kBtnH - kCharH) / 2);
  }
  s.drawFastVLine(kMasterX - 4, y0, kPnlY - 4, kDim);  // the panel below spans the width
  s.setTextColor(kCursor);
  const char* mn = mt::sceneMstName(scnMst_);
  s.drawString(mn, kMasterX + (kMasterW - static_cast<int>(strlen(mn)) * kCharW) / 2, y0 + kNameY);
  const bool mset = mt::sceneMstIsSet(sc, scnMst_);
  const int fx = kMasterX + (kMasterW - kFaderW) / 2;
  if (mset) drawFader(s, fx, y0 + kFaderY, sc.mst[scnMst_], mt::sceneMstMax(scnMst_), kYellow);
  else s.drawRect(fx, y0 + kFaderY, kFaderW, kFaderH, kDim);
  if (mset) snprintf(buf, sizeof(buf), "%u", sc.mst[scnMst_]);
  else snprintf(buf, sizeof(buf), "---");
  s.setTextColor(mset ? kYellow : kDim);
  s.drawString(buf, kMasterX + (kMasterW - static_cast<int>(strlen(buf)) * kCharW) / 2, y0 + kValY);
  snprintf(buf, sizeof(buf), "b%u", mt::sceneMstBase(p, scnMst_));
  s.setTextColor(kDim);
  s.drawString(buf, kMasterX + (kMasterW - static_cast<int>(strlen(buf)) * kCharW) / 2, y0 + kPanY - 2);
}

// The SCENE view's panel: `< SCN n >`, `< VOL >`, =A / =B, X64 (shown only); CAP, CLR, CPY, END.
void TrackScreen::drawPanel(LGFX_Sprite& s, int y0) {
  const mt::Project& p = app_.project();
  char buf[16];
  for (int b = 0; b < 2 * kPnlCols; ++b) {
    const int x = pnlX(b % kPnlCols), y = y0 + pnlY(b / kPnlCols);
    const bool info = b == kPbEnd || b == kPbXf;
    const bool on = b == kPbCpy && scnCopy_;
    s.fillRect(x, y, kPnlBtnW, kPnlRowH, on ? kCursor : (info ? kBg : kBeatBg));
    s.drawRect(x, y, kPnlBtnW, kPnlRowH, info ? kBeatBg : kDim);
    uint16_t col = on ? kBg : kText;
    switch (b) {
      case kPbScn: snprintf(buf, sizeof(buf), "EDIT %d", scnEdit_ + 1); col = kCursor; break;
      case kPbPar: snprintf(buf, sizeof(buf), "%s", mt::sceneTrkName(scnPar_)); break;
      case kPbEnd:
        snprintf(buf, sizeof(buf), "A:%d B:%d", p.sceneA + 1, p.sceneB + 1);
        col = kDim;
        break;
      case kPbXf: snprintf(buf, sizeof(buf), "X%d", p.xfade); col = kDim; break;
      case kPbCap: snprintf(buf, sizeof(buf), "CAPTURE"); break;
      case kPbClr: snprintf(buf, sizeof(buf), "CLEAR"); break;
      case kPbCpy: snprintf(buf, sizeof(buf), "COPY"); break;
      default: snprintf(buf, sizeof(buf), "DONE"); break;
    }
    const int ty = y + (kPnlRowH - kCharH) / 2;
    s.setTextColor(col);
    s.drawString(buf, x + (kPnlBtnW - static_cast<int>(strlen(buf)) * kCharW) / 2, ty);
    if (b == kPbScn || b == kPbPar) {  // the arrows: a tap on the left / right half
      s.setTextColor(kText);
      s.drawString("<", x + 8, ty);
      s.drawString(">", x + kPnlBtnW - 8 - kCharW, ty);
    }
  }
}

}  // namespace ui
