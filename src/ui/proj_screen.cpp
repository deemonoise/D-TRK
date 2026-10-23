#include "proj_screen.h"
#include <Arduino.h>
#include <esp_random.h>
#include <math.h>
#include <new>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "dice.h"
#include "esp_heap_caps.h"
#include "engine/engine.h"
#include "fx_info.h"
#include "groove.h"
#include "names.h"
#include "scale.h"
#include "synth_eq.h"
#include "synth_fxutil.h"
#include "note_name.h"
#include "perf_axis.h"
#include "storage/tuning_store.h"
#include "tuning_factory.h"

namespace ui {
namespace {

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
int wrapi(int v, int n) { return ((v % n) + n) % n; }

}  // namespace

ProjScreen::ProjScreen(App& app) : app_(app) {
  params_[kBpm] = {"BPM", [this](char* o, int n) { snprintf(o, n, "%d", bpm()); }, [this](int d) { editBpm(d); }};
  params_[kRoot] = {"Scale root", [this](char* o, int n) { snprintf(o, n, "%s", kRootNames[app_.project().scaleRoot % 12]); },
                    [this](int d) { app_.project().scaleRoot = static_cast<uint8_t>(wrapi(app_.project().scaleRoot + d, 12)); }};
  params_[kScale] = {"Scale",
                     [this](char* o, int n) {
                       snprintf(o, n, "%s", mt::scaleName(static_cast<mt::ScaleType>(app_.project().scaleType)));
                     },
                     [this](int d) {
                       constexpr int count = static_cast<int>(mt::ScaleType::Count);
                       app_.project().scaleType = static_cast<uint8_t>(wrapi(app_.project().scaleType + d, count));
                     }};
  params_[kTuning] = {"Tuning",
                      [this](char* o, int n) {
                        const mt::TuningTable& t = app_.project().tuning;
                        snprintf(o, n, "%s", tunPick_ >= 0 ? tunName(tunPick_) : (t.count ? t.name : "12-TET"));
                      },
                      [this](int d) {
                        const int cur = tunPick_ >= 0 ? tunPick_ : tunCurrent();
                        const int v = clampi(cur < 0 ? (d > 0 ? d - 1 : 0) : cur + d, 0, tunCount() - 1);
                        if (v != cur) tunPick_ = v;
                      }};
  params_[kTuneRoot] = {"Tuning root",
                        [this](char* o, int n) {
                          const uint8_t r = app_.project().tuning.root;
                          if (r == mt::kTuneRootAuto) {
                            snprintf(o, n, "AUTO");
                            return;
                          }
                          char nm[4];
                          mt::noteName(r, nm);
                          snprintf(o, n, "%s", nm);
                        },
                        [this](int d) {
                          uint8_t& r = app_.project().tuning.root;
                          const int v = clampi((r == mt::kTuneRootAuto ? -1 : r) + d, -1, 127);
                          r = v < 0 ? mt::kTuneRootAuto : static_cast<uint8_t>(v);
                        },
                        [this] { return app_.project().tuning.count == 0; }};
  // Read at the sequencer's start(): a change takes effect at the next Play (a render: at once).
  params_[kSeed] = {"Seed", [this](char* o, int n) { snprintf(o, n, "%u", app_.project().seed); },
                    [this](int d) {
                      app_.project().seed = static_cast<uint16_t>(clampi(app_.project().seed + d, 1, 9999));
                    }};
  params_[kLength] = {"Length", [this](char* o, int n) { snprintf(o, n, "%u", pat().length); },
                      [this](int d) {
                        const int v = clampi(pat().length + d, mt::kMinSteps, mt::kMaxSteps);
                        if (v == pat().length) return;
                        snapPattern();
                        pat().length = static_cast<uint8_t>(v);
                        pat().fitTrackLen();
                      }};
  params_[kRes] = {"Resolution",
                   [this](char* o, int n) { snprintf(o, n, "%s", resName(pat().res)); },
                   [this](int d) {
                     const int r = clampi(static_cast<int>(pat().res) + d, 0, static_cast<int>(mt::Resolution::Count) - 1);
                     if (r == static_cast<int>(pat().res)) return;
                     snapPattern();
                     pat().res = static_cast<mt::Resolution>(r);
                   }};
  params_[kSwing] = {"Swing", [this](char* o, int n) { snprintf(o, n, "%u%%", pat().swing); },
                     [this](int d) {
                       const int v = clampi(pat().swing + d, 50, 75);
                       if (v == pat().swing) return;
                       snapPattern();
                       pat().swing = static_cast<uint8_t>(v);
                     },
                     [this] { return pat().groove != 0; }};
  params_[kGroove] = {"Groove", [this](char* o, int n) { snprintf(o, n, "%s", mt::grooveAt(pat().groove).name); },
                      [this](int d) {
                        const int v = clampi(pat().groove + d, 0, mt::grooveCount() - 1);
                        if (v == pat().groove) return;
                        snapPattern();
                        pat().groove = static_cast<uint8_t>(v);
                      }};
  auto u7 = [this](uint8_t mt::Project::*f) {
    return [this, f](int d) { app_.project().*f = static_cast<uint8_t>(clampi(app_.project().*f + d, 0, 127)); };
  };
  auto num = [this](uint8_t mt::Project::*f) {
    return [this, f](char* o, int n) { snprintf(o, n, "%u", app_.project().*f); };
  };
  // A mode row: 0 .. count - 1, clamped, shown by name.
  auto mode = [this](uint8_t mt::Project::*f, int count) {
    return [this, f, count](int d) { app_.project().*f = static_cast<uint8_t>(clampi(app_.project().*f + d, 0, count - 1)); };
  };
  auto named = [this](uint8_t mt::Project::*f, const char* const* names, int count) {
    return [this, f, names, count](char* o, int n) {
      const uint8_t v = app_.project().*f;
      snprintf(o, n, "%s", names[v < count ? v : 0]);
    };
  };
  // Greyed-out tests read the live project: P() inside a [this] lambda.
  auto noDelay = [this] { return P().dlyLevel == 0; };
  static const char* const kDlyModes[] = {"DIGITAL", "TAPE", "PING-PONG"};
  static_assert(sizeof(kDlyModes) / sizeof(kDlyModes[0]) == static_cast<int>(mt::DlyMode::Count), "delay modes");
  params_[kDlyTime] = {"Delay", [this](char* o, int n) { snprintf(o, n, "%u/16", app_.project().dlyTime); },
                       [this](int d) {
                         app_.project().dlyTime = static_cast<uint8_t>(clampi(app_.project().dlyTime + d, 1, mt::kDlyTimeMax));
                       },
                       noDelay};
  params_[kDlyMode] = {"Dly mode", named(&mt::Project::dlyMode, kDlyModes, 3),
                       mode(&mt::Project::dlyMode, static_cast<int>(mt::DlyMode::Count)), noDelay};
  params_[kDlyFb] = {"Feedback", num(&mt::Project::dlyFb), u7(&mt::Project::dlyFb), noDelay};
  params_[kDlyTone] = {"Tone", num(&mt::Project::dlyTone), u7(&mt::Project::dlyTone), noDelay};
  // TAPE: Wear (wow / flutter, saturation, thinner repeats); PING-PONG: Width (draw() sets the label).
  params_[kDlyChar] = {"Wear", num(&mt::Project::dlyChar), u7(&mt::Project::dlyChar), [this] {
                         return P().dlyLevel == 0 || P().dlyMode == static_cast<uint8_t>(mt::DlyMode::Digital);
                       }};
  params_[kDlyLevel] = {"Dly level", num(&mt::Project::dlyLevel), u7(&mt::Project::dlyLevel)};
  auto noReverb = [this] { return P().rvbLevel == 0; };
  auto noShim = [this] { return P().rvbLevel == 0 || P().rvbMode != static_cast<uint8_t>(mt::RvbMode::Shimmer); };
  static const char* const kRvbModes[] = {"PLAIN", "SHIMMER"};
  static const char* const kShimInts[] = {"+12", "+7", "+24"};
  static_assert(sizeof(kRvbModes) / sizeof(kRvbModes[0]) == static_cast<int>(mt::RvbMode::Count), "reverb modes");
  static_assert(sizeof(kShimInts) / sizeof(kShimInts[0]) == static_cast<int>(mt::ShimInt::Count), "shimmer intervals");
  params_[kRvbSize] = {"Reverb", num(&mt::Project::rvbSize), u7(&mt::Project::rvbSize), noReverb};
  params_[kRvbMode] = {"Rvb mode", named(&mt::Project::rvbMode, kRvbModes, 2),
                       mode(&mt::Project::rvbMode, static_cast<int>(mt::RvbMode::Count)), noReverb};
  params_[kRvbDamp] = {"Rvb damp", num(&mt::Project::rvbDamp), u7(&mt::Project::rvbDamp), noReverb};
  params_[kRvbShim] = {"Shimmer", num(&mt::Project::rvbShim), u7(&mt::Project::rvbShim), noShim};
  params_[kShimInt] = {"Shim int", named(&mt::Project::shimInt, kShimInts, 3),
                       mode(&mt::Project::shimInt, static_cast<int>(mt::ShimInt::Count)), noShim};
  params_[kRvbLevel] = {"Rvb level", num(&mt::Project::rvbLevel), u7(&mt::Project::rvbLevel)};
  // MOD bus: the tracks' Mod send (TRACK) into one effect, its stereo return into the mix.
  static const char* const kModTypes[] = {"OFF", "CHORUS", "ENSEMBLE", "FLANGER", "PHASER"};
  static_assert(sizeof(kModTypes) / sizeof(kModTypes[0]) == static_cast<int>(mt::ModType::Count), "mod types");
  auto noMod = [this] { return P().modType == 0 || P().modLevel == 0; };
  params_[kModType] = {"Mod type", named(&mt::Project::modType, kModTypes, 5),
                       mode(&mt::Project::modType, static_cast<int>(mt::ModType::Count))};
  params_[kModRate] = {"Mod rate",
                       [this](char* o, int n) {
                         const float hz = mt::fxRateHz(app_.project().modRate);
                         snprintf(o, n, hz < 1.f ? "%.2f Hz" : "%.1f Hz", hz);
                       },
                       u7(&mt::Project::modRate), noMod};
  params_[kModDepth] = {"Mod depth", num(&mt::Project::modDepth), u7(&mt::Project::modDepth), noMod};
  params_[kModFb] = {"Mod fb", num(&mt::Project::modFb), u7(&mt::Project::modFb), [this] {
                       const uint8_t t = P().modType;
                       return P().modLevel == 0 || (t != static_cast<uint8_t>(mt::ModType::Flanger) &&
                                                    t != static_cast<uint8_t>(mt::ModType::Phaser));
                     }};
  params_[kModLevel] = {"Mod level", num(&mt::Project::modLevel), u7(&mt::Project::modLevel),
                        [this] { return P().modType == 0; }};
  // Master compressor: Comp 0 = off; SC track ducks the mix to that track (e.g. the kick).
  auto noComp = [this] { return app_.project().compAmt == 0; };
  params_[kCompAmt] = {"Comp",
                       [this](char* o, int n) {
                         if (app_.project().compAmt) snprintf(o, n, "%u", app_.project().compAmt);
                         else snprintf(o, n, "OFF");
                       },
                       u7(&mt::Project::compAmt)};
  params_[kCompRel] = {"Comp rel",
                       [this](char* o, int n) {
                         const float ms = 20.f * powf(50.f, (app_.project().compRel > 127 ? 127 : app_.project().compRel) / 127.f);
                         snprintf(o, n, "%u ms", static_cast<unsigned>(ms + 0.5f));
                       },
                       u7(&mt::Project::compRel), noComp};
  params_[kScTrack] = {"SC track",
                       [this](char* o, int n) {
                         const uint8_t t = app_.project().scTrack;
                         if (t >= 1 && t <= mt::kTracks) snprintf(o, n, "T%u %s", t, app_.project().tracks[t - 1].name);
                         else snprintf(o, n, "OFF");
                       },
                       [this](int d) {
                         app_.project().scTrack = static_cast<uint8_t>(clampi(app_.project().scTrack + d, 0, mt::kTracks));
                       },
                       noComp};
  params_[kScDepth] = {"SC depth", num(&mt::Project::scDepth), u7(&mt::Project::scDepth),
                       [this] { return app_.project().compAmt == 0 || app_.project().scTrack == 0; }};
  // Master EQ: low shelf 120 Hz, high shelf 5 kHz, -12..+12 dB (64 = flat); opposite settings tilt.
  auto db = [this](uint8_t mt::Project::*f) {
    return [this, f](char* o, int n) {
      const uint8_t v = app_.project().*f;
      if (v == 64) snprintf(o, n, "0 dB");
      else snprintf(o, n, "%+.1f dB", mt::MasterEq::db(v));
    };
  };
  params_[kEqLow] = {"EQ low", db(&mt::Project::eqLow), u7(&mt::Project::eqLow)};
  params_[kEqHigh] = {"EQ high", db(&mt::Project::eqHigh), u7(&mt::Project::eqHigh)};
  // PERF: Button N picks the effect (a new one starts at its default), Value its start depth.
  for (int b = 0; b < mt::kPerfButtons; ++b) {
    snprintf(perfLabels_[b], sizeof(perfLabels_[b]), "Button %d", b + 1);
    params_[kPerf1 + 2 * b] = {perfLabels_[b],
                               [this, b](char* o, int n) {
                                 const uint8_t f = app_.project().perfMap[b];
                                 snprintf(o, n, "%s", f ? mt::perfName(static_cast<mt::PerfFx>(f)) : "---");
                               },
                               [this, b](int d) {
                                 constexpr int kLast = static_cast<int>(mt::PerfFx::Count) - 1;
                                 mt::Project& p = app_.project();
                                 const int f = clampi(p.perfMap[b] + d, 0, kLast);
                                 if (f == p.perfMap[b]) return;
                                 p.perfMap[b] = static_cast<uint8_t>(f);
                                 p.perfDepth[b] = mt::perfRange(static_cast<mt::PerfFx>(f)).def;
                               }};
    params_[kPerf1 + 2 * b + 1] = {"  Value",
                                   [this, b](char* o, int n) {
                                     const mt::Project& p = app_.project();
                                     if (p.perfMap[b]) mt::perfValue(static_cast<mt::PerfFx>(p.perfMap[b]), p.perfDepth[b], o, n);
                                     else snprintf(o, n, "---");
                                   },
                                   [this, b](int d) {
                                     mt::Project& p = app_.project();
                                     const mt::PerfRange r = mt::perfRange(static_cast<mt::PerfFx>(p.perfMap[b]));
                                     p.perfDepth[b] = static_cast<int8_t>(clampi(p.perfDepth[b] + d, r.lo, r.hi));
                                   },
                                   [this, b] { return app_.project().perfMap[b] == 0; }};
  }
  // XY pad: the effect on each axis (--- = that axis does nothing). Y is dim where the pad ignores
  // it (mt::perfXyAxis): on X's effect (X drives it), or a buffer effect on both.
  for (int a = 0; a < 2; ++a)
    params_[kPerfX + a] = {a ? "XY pad Y" : "XY pad X",
                           [this, a](char* o, int n) {
                             const uint8_t f = app_.project().perfXy[a];
                             snprintf(o, n, "%s", f ? mt::perfName(static_cast<mt::PerfFx>(f)) : "---");
                           },
                           [this, a](int d) {
                             constexpr int kLast = static_cast<int>(mt::PerfFx::Count) - 1;
                             uint8_t& f = app_.project().perfXy[a];
                             f = static_cast<uint8_t>(clampi(f + d, 0, kLast));
                           },
                           [this, a] {
                             const uint8_t* xy = app_.project().perfXy;
                             return a == 1 && xy[1] != 0 && mt::perfXyAxis(xy, 1) == mt::PerfFx::None;
                           }};
  // Shift+click cancel on PERF restores the effect and its depth together (a new effect resets the depth),
  // and the XY axes.
  static_assert(offsetof(mt::Project, perfDepth) == offsetof(mt::Project, perfMap) + sizeof(mt::Project::perfMap),
                "perfDepth follows perfMap");
  static_assert(offsetof(mt::Project, perfXy) == offsetof(mt::Project, perfDepth) + sizeof(mt::Project::perfDepth),
                "perfXy follows perfDepth");
  list_.setEditScope([this]() -> void* { return page_ == kPgPerf ? app_.project().perfMap : nullptr; },
                     sizeof(mt::Project::perfMap) + sizeof(mt::Project::perfDepth) + sizeof(mt::Project::perfXy));
  // Every row is project data (device settings live on FILE > SYSTEM).
  list_.setOnEdit([this] { app_.markDirty(); });
  list_.setOnCancel([this] { app_.toast("CANCEL"); });
  showPage(kPgSong, false);
}

void ProjScreen::showPage(int page, bool bar) {
  page_ = (page % kPages + kPages) % kPages;
  list_.setEdit(false);
  list_.setPageBar(true);
  list_.setParams(params_ + kPageFirst[page_], kPageFirst[page_ + 1] - kPageFirst[page_]);
  list_.setVisibleRows(kListRows);
  if (bar) list_.selectBar();
  else list_.setSel(0);
}

const mt::Project& ProjScreen::P() { return app_.project(); }

mt::Pattern& ProjScreen::pat() { return app_.project().patterns[app_.editPattern()]; }

// Length / Resolution / Swing are pattern data: an undo snapshot before the first of a run of
// edits on one pattern (else a later undo of a step edit would silently revert them too).
void ProjScreen::snapPattern() {
  if (app_.editSeq() != patSeq_ || app_.editPattern() != patIdx_) app_.pushUndo();
  patSeq_ = app_.editSeq() + 1;  // onEdit marks dirty next
  patIdx_ = app_.editPattern();
}

int ProjScreen::bpm() {
  const int cur = app_.project().bpm;
  if (bpmTarget_ != cur && millis() - bpmPostMs_ >= kBpmSettleMs) bpmTarget_ = cur;  // changed elsewhere
  return bpmTarget_;
}

void ProjScreen::editBpm(int delta) {
  bpmTarget_ = clampi(bpm() + delta, 20, 300);
  bpmPostMs_ = millis();
  engine::post(engine::Cmd::SetBpm, static_cast<uint16_t>(bpmTarget_));
}

void ProjScreen::onEnter() {
  list_.setEdit(false);
  bpmTarget_ = app_.project().bpm;
  if (!tunUser_) tunUser_ = new (std::nothrow) char[kTunUserMax][hw::kNameMax];
  tunUserN_ = tunUser_ ? storage::listTunings(tunUser_, kTunUserMax) : 0;
  tunPick_ = -1;
}

int ProjScreen::tunCount() const { return 1 + mt::factoryTuningCount() + tunUserN_; }

const char* ProjScreen::tunName(int i) const {
  if (i <= 0) return "12-TET";
  if (i <= mt::factoryTuningCount()) return mt::factoryTuningName(i - 1);
  i -= 1 + mt::factoryTuningCount();
  return i < tunUserN_ ? tunUser_[i] : "?";
}

int ProjScreen::tunCurrent() const {
  const mt::TuningTable& t = app_.project().tuning;
  if (t.count == 0) return 0;
  const int f = mt::factoryTuningFind(t.name);
  if (f >= 0) return 1 + f;
  for (int i = 0; i < tunUserN_; ++i)
    if (strcmp(tunUser_[i], t.name) == 0) return 1 + mt::factoryTuningCount() + i;
  return -1;
}

// The pending pick: a factory table, or the card's file read and parsed here (UI task, no project lock
// held), then copied into the project under the lock. The root stays. A bad file: a toast, no change.
void ProjScreen::applyTuning() {
  const int i = tunPick_;
  tunPick_ = -1;
  if (i < 0 || i == tunCurrent()) return;
  mt::TuningTable t;
  t.root = app_.project().tuning.root;
  if (i == 0) {
    mt::tuningClear(t);
    t.root = app_.project().tuning.root;
  } else if (i <= mt::factoryTuningCount()) {
    if (!mt::factoryTuning(i - 1, t)) return;
  } else {
    mt::ParseErr err;
    int line;
    const storage::Result r = storage::loadTuning(tunName(i), t, err, line);
    if (r != storage::Result::Ok) {
      char msg[48];
      if (err == mt::ParseErr::Ok) snprintf(msg, sizeof(msg), "%s", storage::resultText(r));
      else if (line > 0) snprintf(msg, sizeof(msg), "SCL L%d: %s", line, mt::sclErrName(err));
      else snprintf(msg, sizeof(msg), "SCL: %s", mt::sclErrName(err));
      app_.toast(msg);
      return;
    }
  }
  engine::lockProject();
  app_.project().tuning = t;
  engine::unlockProject();
  app_.markDirty();
}

void ProjScreen::onProjectReplaced() {
  if (dice_) {  // its undo (and a draft) belong to the old project
    dice_->layers = 0;
    dice_->drafted = 0;
  }
}

void ProjScreen::diceMenu() {
  list_.setEdit(false);
  constexpr int kUndoDice = 100;
  const MenuItem items[] = {{"Roll all", mt::kDiceAll},
                            {"Tempo + scale", mt::kDiceTempo},
                            {"Sounds", mt::kDiceSounds},
                            {"Pattern", mt::kDicePattern},
                            {"Sounds + pattern", mt::kDiceSounds | mt::kDicePattern},
                            {"Undo dice", kUndoDice, dice_ && dice_->layers != 0}};
  app_.menu().open("DICE", items, 6, [this](int id) {
    if (id == kUndoDice) undoDice();
    else roll(static_cast<uint8_t>(id));
  });
}

// The evolve drift is indexed by step: on steps a roll (or its undo) rewrote it would play over notes it
// never saw. Holding lockProject(): cleared before the next plan.
void ProjScreen::evoClearTracks(uint16_t mask) {
  for (int t = 0; t < mt::kTracks; ++t)
    if (mask & (1u << t)) {
      engine::evoClear(t);
      engine::varClear(t);  // a rewritten track starts over at VAR generation 0 too
    }
}

void ProjScreen::afterDice(uint8_t layers) {
  const mt::Project& p = app_.project();
  if (layers & mt::kDiceTempo) {
    bpmTarget_ = p.bpm;
    engine::postWait(engine::Cmd::SetBpm, p.bpm);  // the sequencer applies it (p.bpm already holds it)
  }
  if (layers & (mt::kDiceSounds | mt::kDicePattern)) engine::postWait(engine::Cmd::ReleaseTies);
  if (layers & mt::kDiceSounds) app_.inst().dropMutateUndo();  // its snapshot is of the old instrument
  if (layers & mt::kDiceSounds)
    for (int k = 0; k < mt::kDiceRoles; ++k) {  // as TRACK's Out / Instr rows
      engine::postWait(engine::Cmd::TrackOut, mt::kDiceTrack[k]);
      engine::postWait(engine::Cmd::SendProgram, mt::kDiceTrack[k]);
    }
  // The planned steps of the rolled tracks (and their GEN, now OFF) are planned again.
  if (layers & mt::kDicePattern) engine::postWait(engine::Cmd::GenEdit);
  app_.markDirty();
}

void ProjScreen::roll(uint8_t layers) {
  if (!dice_) {
    void* m = heap_caps_malloc(sizeof(mt::DiceSnap), MALLOC_CAP_SPIRAM);
    if (!m) {
      app_.toast("NO MEMORY");
      return;
    }
    dice_ = new (m) mt::DiceSnap();
  }
  mt::Project& p = app_.project();
  // The generators run without the lock (the UI is the only writer of what they read); the engine only
  // waits for the swap.
  const mt::DiceResult r = mt::diceDraft(p, layers, app_.editPattern(), esp_random(), *dice_);
  engine::lockProject();
  mt::diceApply(p, *dice_);
  evoClearTracks(dice_->patMask);
  engine::unlockProject();
  afterDice(layers);
  char msg[48];
  if (r.genre >= 0) {
    snprintf(msg, sizeof(msg), "DICE %s %u %s %s", mt::diceGenre(r.genre).name, static_cast<unsigned>(p.bpm),
             kRootNames[p.scaleRoot % 12], mt::scaleName(static_cast<mt::ScaleType>(p.scaleType)));
  } else {
    snprintf(msg, sizeof(msg), "DICE %s",
             layers == mt::kDiceSounds ? "SOUNDS" : (layers == mt::kDicePattern ? "PATTERN" : "SOUNDS+PATTERN"));
  }
  app_.toast(msg);
}

void ProjScreen::undoDice() {
  if (!dice_) return;
  engine::lockProject();
  const uint16_t rolled = dice_->patMask;  // diceUndo empties it
  const uint8_t l = mt::diceUndo(app_.project(), *dice_);
  if (l) evoClearTracks(rolled);
  engine::unlockProject();
  if (!l) return;
  afterDice(l);
  app_.toast("UNDO DICE");
}

// A + turn on the page bar: the previous / next page.
void ProjScreen::onInput(const hw::InputEvent& ev) {
  if (ev.type == hw::InputType::EncLong) {  // the DICE menu (the list has no long press)
    diceMenu();
    return;
  }
  if (const int ov = list_.onInput(ev)) showPage(page_ + ov, true);
}

// B + Shift + turn: the previous / next page; the cursor stays on the page bar if it was there.
void ProjScreen::onPage(int d) { showPage(page_ + d, list_.barSelected()); }

// B: leaves the edit with the value kept.
void ProjScreen::onBack() { list_.setEdit(false); }

void ProjScreen::onTouch(const TouchEvent& ev) {
  if (ev.type == TouchType::Tap && ev.y < kAreaY + kHeaderH) {
    if (ev.x >= kDiceX0 && ev.x < kDiceX1) diceMenu();
    return;
  }
  const int barY = kAreaY + kHeaderH;
  if (ev.y >= barY && ev.y < barY + PageBar::kH) {
    if (ev.type == TouchType::Tap) showPage(PageBar::at(ev.x, kPages), true);
    return;
  }
  list_.onTouch(ev);
}

void ProjScreen::draw(LGFX_Sprite& s, int y0, int) {
  if (tunPick_ >= 0) applyTuning();
  char buf[24];
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  snprintf(buf, sizeof(buf), "PROJECT  (P%02d)", app_.editPattern() + 1);
  s.setTextColor(kText);
  s.drawString(buf, ParamList::kLabelX, y0 + (kHeaderH - 4 - kCharH) / 2);
  s.fillRect(kDiceX0, y0 + 2, kDiceX1 - kDiceX0, kHeaderH - 8, kPlayBg);
  s.setTextColor(kCursor);
  s.drawString("DICE", kDiceX0 + (kDiceX1 - kDiceX0 - 4 * kCharW) / 2, y0 + (kHeaderH - 4 - kCharH) / 2);
  static const char* const kNames[kPages] = {"SONG", "DLY", "RVB", "MOD", "MSTR", "PERF"};
  params_[kDlyChar].label = app_.project().dlyMode == static_cast<uint8_t>(mt::DlyMode::PingPong) ? "Width" : "Wear";
  PageBar::draw(s, y0 + kHeaderH, kNames, kPages, page_);
  if (list_.barSelected()) PageBar::drawFocus(s, y0 + kHeaderH);
  list_.draw(s, y0 + kHeaderH + PageBar::kH);
}

}  // namespace ui
