#include "model.h"
#include "bytebeat_lib.h"
#include "fastmath.h"
#include "hot.h"
#include "synth_osc.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace mt {

void Pattern::clear() {
  length = kDefaultSteps;
  res = Resolution::Sixteenth;
  swing = 50;
  memset(trackLen, 0, sizeof(trackLen));
  memset(trackSpeed, 0, sizeof(trackSpeed));
  for (auto& tr : steps)
    for (auto& s : tr) s = Step();
}

void Pattern::fitTrackLen() {
  for (uint8_t& n : trackLen)
    if (n > length) n = length;
}

bool Pattern::isEmpty() const {
  for (const auto& tr : steps)
    for (const auto& s : tr)
      if (!s.isEmpty()) return false;
  return true;
}

namespace {
constexpr InstrType kTypeOrder[] = {InstrType::Fm,     InstrType::Synth, InstrType::String, InstrType::Drum,
                                    InstrType::Sample, InstrType::Chip,  InstrType::Lofi,   InstrType::Kit};
static_assert(sizeof(kTypeOrder) == static_cast<size_t>(kInstrTypesUi), "type order");
}  // namespace

InstrType instrTypeAt(int k) {
  return k >= 0 && k < kInstrTypesUi ? kTypeOrder[k] : InstrType::Fm;
}

const char* fmMachineName(uint8_t machine) {
  static const char* const kNames[] = {"KICK", "SNARE", "METAL", "PERC", "TONE", "CHORD", "CLAP", "HAT"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<int>(FmMachine::Count), "FM names");
  return kNames[machine < static_cast<uint8_t>(FmMachine::Count) ? machine : 0];
}

void chipWaveName(uint8_t w, char* o, int n) {
  static const char* const kNames[] = {"PULSE", "TRI", "SAW", "NOISE", "METAL"};
  static const char* const kExt[] = {"SID TS", "SID PS", "SID PT", "SIDPTS", "SID NZ",
                                     "AY SAW", "AY TRI", "AY NZ",  "AY TN"};
  static_assert(sizeof(kExt) / sizeof(kExt[0]) == kWaveCount - kWaveExtFirst, "wave names");
  constexpr int kWt = static_cast<int>(Wave::Wt1);
  if (w >= kWaveCount) w = 0;
  if (w < kWt) snprintf(o, n, "%s", kNames[w]);
  else if (w < kWaveExtFirst) snprintf(o, n, "WT%d", w - kWt + 1);
  else snprintf(o, n, "%s", kExt[w - kWaveExtFirst]);
}

const char* stringModelName(uint8_t model) {
  static const char* const kNames[] = {"STRING", "MODAL"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<int>(StringModel::Count), "STRING model names");
  return kNames[model < static_cast<uint8_t>(StringModel::Count) ? model : 0];
}

int instrTypePos(InstrType t) {
  for (int k = 0; k < kInstrTypesUi; ++k)
    if (kTypeOrder[k] == t) return k;
  return 0;
}

void Project::reset() {
  strncpy(name, "untitled", sizeof(name) - 1);
  name[sizeof(name) - 1] = 0;
  bpm = 120;
  scaleRoot = 0;
  scaleType = 0;
  for (int i = 0; i < kTracks; ++i) {
    tracks[i] = TrackCfg();
    tracks[i].channel = i;
    tracks[i].instr = static_cast<uint8_t>(i);
    snprintf(tracks[i].name, sizeof(tracks[i].name), "TRK%d", i + 1);
  }
  for (int i = 0; i < kInstruments; ++i) {
    instruments[i] = Instrument();
    instrSetType(instruments[i], InstrType::Fm);
    fmSetMachine(instruments[i], static_cast<uint8_t>(FmMachine::Tone));
    snprintf(instruments[i].name, sizeof(instruments[i].name), "INS%d", i + 1);
  }
  masterVol = 100;
  preview = true;
  dlyTime = 3;
  dlyFb = 50;
  dlyTone = 90;
  dlyLevel = 100;
  rvbSize = 60;
  rvbDamp = 70;
  rvbLevel = 80;
  compAmt = 0;
  compRel = 50;
  scTrack = 0;
  scDepth = 64;
  memcpy(perfMap, kPerfDefaultMap, sizeof(perfMap));
  perfDefaultDepths();
  memcpy(perfXy, kPerfXyDefault, sizeof(perfXy));
  for (KnobBind& k : knobs) k = KnobBind{};
  for (int i = 0; i < kTracks; ++i) {
    gen[i] = GenCfg();
    gen[i].seed = static_cast<uint16_t>(i + 1);
  }
  for (EvoCfg& e : evo) e = EvoCfg();
  for (VarCfg& v : var) v = VarCfg();
  for (ProjSample& s : samples) s = ProjSample{};
  sampleCount = 0;
  for (ProjWavetable& w : wavetables) w = ProjWavetable{};
  wavetableCount = 0;
  tuningClear(tuning);
  hasSampleList = false;
  dlyMode = 0;
  dlyChar = 40;
  rvbMode = 0;
  rvbShim = 60;
  modType = 0;
  modRate = 30;
  modDepth = 64;
  modFb = 40;
  modLevel = 100;
  eqLow = 64;
  eqHigh = 64;
  shimInt = 0;
  xfade = 0;
  for (Scene& sc : scenes) sc = Scene();
  sceneA = 0;
  sceneB = 1;
  sceneLive[0] = sceneLive[1] = Scene();
  for (auto& p : patterns) p.clear();
  memset(song, 0, sizeof(song));
  memset(songMute, 0, sizeof(songMute));
  songLen = 0;
  for (SongFollow& f : songFol) f = SongFollow();
  memset(songTrn, 0, sizeof(songTrn));
  memset(songBpm, 0, sizeof(songBpm));
  songTrnMask = 0xFFFF;
  seed = 1;
}

void Project::perfDefaultDepths() {
  for (int i = 0; i < kPerfButtons; ++i) perfDepth[i] = perfRange(static_cast<PerfFx>(perfMap[i])).def;
}

// Tables built on first use: powf is costly on the ESP32 and these run per voice at control rate.
// A race on the first use only writes the same values twice.
MT_HOT uint16_t envTimeMs(uint8_t v) {
  static uint16_t table[128];
  static bool ready = false;
  if (!ready) {
    for (int i = 1; i < 128; ++i) table[i] = static_cast<uint16_t>(lroundf(powf(10000.f, i / 127.f)));
    ready = true;
  }
  return table[v > 127 ? 127 : v];
}

uint16_t fmDecayMs(uint8_t v) {
  if (v > 127) v = 127;
  return static_cast<uint16_t>(5.f * powf(800.f, v / 127.f) + 0.5f);
}

MT_HOT float lfoSyncHz(uint8_t div, uint16_t bpm) {
  // Beats (quarters) per LFO cycle.
  // Cycles per beat (1 / beats per cycle): per control with a synced LFO, no __divsf3.
  MT_DATA static const float kPerBeat[kLfoSyncSteps] = {8, 6, 4, 3, 2, 1.5f, 1, 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f};
  return (bpm ? bpm : 120) * (1.f / 60.f) * kPerBeat[div < kLfoSyncSteps ? div : kLfoSyncSteps - 1];
}

const char* lfoSyncName(uint8_t div) {
  static const char* const kNames[kLfoSyncSteps] = {"1/32", "1/16T", "1/16", "1/8T", "1/8",  "1/4T",
                                                    "1/4",  "1/2",   "1 BAR", "2 BARS", "4 BARS", "8 BARS"};
  return kNames[div < kLfoSyncSteps ? div : kLfoSyncSteps - 1];
}

const char* speedName(TrackSpeed s) {
  static const char* const kNames[] = {"x1", "x2", "x4", "1/2", "1/4"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<int>(TrackSpeed::Count), "speed names");
  const uint8_t i = static_cast<uint8_t>(s);
  return i < static_cast<uint8_t>(TrackSpeed::Count) ? kNames[i] : "x1";
}

LfoRef lfoRef(Instrument& m, int i) {
  if (i <= 0 || i >= kLfos) return {m.lfoWave, m.lfoRate, m.lfoDepth, m.lfoDest, m.lfoSync};
  LfoCfg& l = m.lfo[i - 1];
  return {l.wave, l.rate, l.depth, l.dest, l.sync};
}

MT_HOT float lfoHz(uint8_t v) {
  static float table[128];
  static bool ready = false;
  if (!ready) {
    for (int i = 0; i < 128; ++i) table[i] = 0.05f * powf(600.f, i / 127.f);
    ready = true;
  }
  return table[v > 127 ? 127 : v];
}

bool tuningValid(const TuningTable& t) {
  if (t.count < 1 || t.count > kTuneDegMax) return false;
  if (t.root > 127 && t.root != kTuneRootAuto) return false;
  const int32_t per = t.mc[t.count - 1];
  if (per <= 0 || per > kTunePeriodMaxMc) return false;
  for (int i = 0; i < t.count; ++i)
    if (t.mc[i] > kTuneDegAbsMaxMc || t.mc[i] < -kTuneDegAbsMaxMc) return false;
  return true;
}

int tuningRootKey(const TuningTable& t, uint8_t scaleRoot) {
  return t.root == kTuneRootAuto ? 60 + scaleRoot % 12 : (t.root > 127 ? 127 : t.root);
}

void tuningClear(TuningTable& t) { t = TuningTable(); }

bool fmGated(uint8_t machine) {
  return machine == static_cast<uint8_t>(FmMachine::Tone) || machine == static_cast<uint8_t>(FmMachine::Chord);
}

void fmSetMachine(Instrument& m, uint8_t machine) {
  // DECAY, COLOR, SHAPE, SWEEP, CONTOUR per machine: sounds right away.
  static const uint8_t kDefaults[static_cast<int>(FmMachine::Count)][kFmMacros] = {
      {85, 40, 32, 70, 50},  // Kick
      {70, 64, 40, 30, 64},  // Snare
      {90, 64, 0, 0, 64},    // Metal
      {65, 50, 32, 40, 40},  // Perc
      {64, 40, 0, 0, 64},    // Tone: sine
      {64, 30, 0, 0, 64},    // Chord: maj
      {70, 64, 40, 40, 64},  // Clap
      {40, 64, 90, 64, 64},  // Hat: closed
  };
  constexpr int kLast = static_cast<int>(FmMachine::Count) - 1;
  m.machine = static_cast<uint8_t>(machine > kLast ? kLast : machine);
  for (int k = 0; k < kFmMacros; ++k) m.macro[k] = kDefaults[m.machine][k];
}

float cutoffHz(float v) {
  v = v < 0 ? 0 : (v > 127 ? 127 : v);
  return 20.f * powf(700.f, v / 127.f);
}

MT_HOT float resoQ(float v) {  // per control with an LFO on RESO: no powf
  v = v < 0 ? 0 : (v > 127 ? 127 : v);
  return 0.5f * fastExp2(v * (5.32192809f / 127.f));  // 0.5 x 40^(v / 127)
}

MT_HOT float filterEnv(uint32_t t, uint8_t fAtk, uint8_t fDec) {
  // x kSynthRate / 1000: a constant, no __divsf3.
  const uint32_t a = static_cast<uint32_t>(envTimeMs(fAtk) * (kSynthRate / 1000.f));
  if (t < a) return static_cast<float>(t) / a;
  const float d = envTimeMs(fDec) * (kSynthRate / 1000.f);
  if (d <= 0) return 1.f;
  return fastExp(-6.9077553f * (t - a) / d);  // ln(1000): -60 dB at d
}

void drumSetMachine(Instrument& m, uint8_t machine) {
  // DECAY, COLOR, SHAPE, SWEEP, CONTOUR per machine (see synth_drum_machines.cpp).
  static const uint8_t kDefaults[static_cast<int>(DrumMachine::Count)][kFmMacros] = {
      {90, 20, 10, 40, 50},  // BD8
      {55, 50, 70, 30, 64},  // SD8
      {75, 0, 0, 40, 50},    // TOM8
      {64, 64, 64, 64, 64},  // CP8
      {64, 64, 64, 64, 0},   // RS8
      {64, 64, 0, 0, 0},     // CL8
      {64, 64, 64, 64, 0},   // CB8
      {38, 64, 30, 64, 64},  // HH8: closed
      {90, 64, 40, 64, 64},  // CY8
      {75, 70, 40, 60, 40},  // BD9
      {55, 64, 80, 30, 64},  // SD9
      {70, 64, 0, 50, 50},   // TOM9
      {64, 64, 64, 64, 64},  // CP9
      {64, 64, 64, 64, 0},   // RS9
      {30, 64, 59, 64, 64},  // HH9: closed
      {95, 64, 127, 64, 64}, // CY9: crash
      {80, 40, 50, 40, 0},   // BDA
      {55, 64, 70, 0, 0},    // SDA
      {75, 50, 30, 60, 40},  // BDS
      {55, 60, 70, 40, 0},   // SDS
      {60, 64, 64, 64, 0},   // CPX
      {40, 64, 40, 0, 0},    // RSX
      {80, 40, 50, 40, 0},   // Unused22: as BDA
      {30, 64, 30, 0, 0},    // HHR
  };
  constexpr int kLast = static_cast<int>(DrumMachine::Count) - 1;
  m.machine = static_cast<uint8_t>(machine > kLast ? kLast : machine);
  if (m.machine == static_cast<uint8_t>(DrumMachine::Unused22)) m.machine = kDrum2First;  // was BDF: BDA
  for (int k = 0; k < kFmMacros; ++k) m.macro[k] = kDefaults[m.machine][k];
}

void lofiSetMachine(Instrument& m, uint8_t machine) {
  // A B C TONE LOOP (BYTE, A B C from the formula below) / WIDTH TONE DRIFT SMOOTH - (APC).
  static const uint8_t kDefaults[static_cast<int>(LofiMachine::Count)][kFmMacros] = {
      {0, 0, 0, 0, 0},     // BYTE
      {20, 100, 0, 0, 0},  // APC
  };
  constexpr int kLast = static_cast<int>(LofiMachine::Count) - 1;
  m.machine = static_cast<uint8_t>(machine > kLast ? kLast : machine);
  for (int k = 0; k < kFmMacros; ++k) m.macro[k] = kDefaults[m.machine][k];
  if (m.machine == static_cast<uint8_t>(LofiMachine::Byte)) lofiLoadFormulaDefaults(m);
}

void lofiLoadFormulaDefaults(Instrument& m) {
  const ByteFormula& f = byteFormula(m.lofiFormula);
  m.macro[0] = f.a;
  m.macro[1] = f.b;
  m.macro[2] = f.c;
}

const char* lofiMachineName(uint8_t machine) {
  static const char* const kNames[] = {"BYTE", "APC"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<int>(LofiMachine::Count), "LOFI names");
  return kNames[machine < static_cast<int>(LofiMachine::Count) ? machine : 0];
}

const char* lofiMacroName(uint8_t machine, int k) {
  static const char* const kNames[static_cast<int>(LofiMachine::Count)][kFmMacros] = {
      {"A", "B", "C", "TONE", "LOOP"},
      {"WIDTH", "TONE", "DRIFT", "SMOOTH", ""},
  };
  if (k < 0 || k >= kFmMacros) return "";
  return kNames[machine < static_cast<int>(LofiMachine::Count) ? machine : 0][k];
}

void instrSetType(Instrument& m, InstrType t) {
  if (t >= InstrType::Count || t == InstrType::Unused7) t = InstrType::Chip;
  m.type = t;
  if (t == InstrType::Fm) fmSetMachine(m, m.machine);
  else if (t == InstrType::Drum) drumSetMachine(m, m.machine);
  else if (t == InstrType::Synth) {
    static const uint8_t kDef[kFmMacros] = {0, 0, 0, 64, 64};  // SHP1, SHP2, MIX, DET, SENV
    memcpy(m.macro, kDef, kFmMacros);
  } else if (t == InstrType::String) {
    static const uint8_t kDef[kFmMacros] = {80, 64, 0, 30, 10};  // DAMP, BRIGHT, STRUCT, POS, EXC
    memcpy(m.macro, kDef, kFmMacros);
    if (m.machine >= static_cast<uint8_t>(StringModel::Count)) m.machine = 0;
    // COMB needs the voice's delay line, which STRING takes (the synth would run it as Off).
    if (m.fltMode == static_cast<uint8_t>(FltMode::Comb)) m.fltMode = static_cast<uint8_t>(FltMode::Off);
  } else if (t == InstrType::Lofi) {
    m.lofiFormula = 0;
    m.lofiSync = 0;
    lofiSetMachine(m, static_cast<uint8_t>(LofiMachine::Byte));
  } else if (t == InstrType::Kit) kitSetDefaults(m);
  for (int i = 0; i < kLfos; ++i) {
    uint8_t& dest = lfoRef(m, i).dest;
    if (!lfoDestValid(dest, t)) dest = static_cast<uint8_t>(LfoDest::Pitch);
  }
}

void kitSetDefaults(Instrument& m) {
  for (int k = 0; k < kKitLanes; ++k) {
    m.kit[k] = KitLane();
    m.kit[k].note = static_cast<uint8_t>(60 + k);
  }
}

uint16_t kitLanesUsed(const Instrument& k) {
  uint16_t m = 0;
  for (int l = 0; l < kKitLanes; ++l)
    if (k.kit[l].instr != kNoInstr || k.kit[l].sample[0]) m = static_cast<uint16_t>(m | (1u << l));
  return m;
}

uint8_t kitFreeNote(const Instrument& k, int lane, int n, int dir) {
  for (; n >= 0 && n <= 127; n += dir) {
    bool taken = false;
    for (int l = 0; l < kKitLanes && !taken; ++l) taken = l != lane && k.kit[l].note == n;
    if (!taken) return static_cast<uint8_t>(n);
  }
  return k.kit[lane].note;
}

bool lfoDestValid(uint8_t dest, InstrType t) {
  if (dest >= static_cast<uint8_t>(LfoDest::Count)) return false;
  const auto d = static_cast<LfoDest>(dest);
  if (d >= LfoDest::Dec && d <= LfoDest::Con)
    return t == InstrType::Fm || t == InstrType::Drum || t == InstrType::Synth || t == InstrType::String ||
           t == InstrType::Lofi;
  if (d == LfoDest::Duty) return t == InstrType::Chip;
  if (d == LfoDest::Sub || d == LfoDest::Noise || d == LfoDest::Semi2 || d == LfoDest::Fold ||
      d == LfoDest::Ring)
    return t == InstrType::Synth;
  if (d == LfoDest::Sat) return t == InstrType::Fm;
  return true;
}

// The Dest list order: the enum's, with PAN and MOD (appended for the file format) next to the sends
// and FOLD, RING (likewise) after SEMI2, and SAT (likewise) after CONTOUR.
constexpr LfoDest kDestOrder[] = {
    LfoDest::Pitch,   LfoDest::Dec,     LfoDest::Col,     LfoDest::Shp,     LfoDest::Swp,     LfoDest::Con,
    LfoDest::Sat,     LfoDest::Vol,     LfoDest::Cutoff,  LfoDest::Drive,   LfoDest::Reso,    LfoDest::Fenv,
    LfoDest::Dly,     LfoDest::Rvb,     LfoDest::ModSend, LfoDest::Pan,     LfoDest::Bit,     LfoDest::Srr,
    LfoDest::Fine,    LfoDest::Duty,    LfoDest::Sub,     LfoDest::Noise,   LfoDest::Semi2,   LfoDest::Fold,
    LfoDest::Ring,    LfoDest::Rate1,   LfoDest::Rate2,   LfoDest::Rate3,   LfoDest::Rate4,   LfoDest::Depth1,
    LfoDest::Depth2,  LfoDest::Depth3,  LfoDest::Depth4,  LfoDest::Rtrg1,   LfoDest::Rtrg2,   LfoDest::Rtrg3,
    LfoDest::Rtrg4};
constexpr int kDestN = sizeof(kDestOrder) / sizeof(kDestOrder[0]);
static_assert(kDestN == static_cast<int>(LfoDest::Count), "kDestOrder must list every LfoDest once");

uint8_t lfoDestStep(uint8_t dest, int d, InstrType t, int self) {
  const int selfRtrg = self >= 0 && self < kLfos ? static_cast<int>(LfoDest::Rtrg1) + self : -1;
  auto at = [](int i) { return static_cast<int>(kDestOrder[i]); };
  auto ok = [&](int i) { return at(i) != selfRtrg && lfoDestValid(static_cast<uint8_t>(at(i)), t); };
  int v = 0;  // position in kDestOrder; out of range = PITCH
  for (int i = 0; i < kDestN; ++i)
    if (at(i) == dest) v = i;
  const int step = d > 0 ? 1 : -1;
  for (int i = 0; i < (d > 0 ? d : -d); ++i) {
    int nv = v + step;
    while (nv >= 0 && nv < kDestN && !ok(nv)) nv += step;
    if (nv < 0 || nv >= kDestN) break;
    v = nv;
  }
  return static_cast<uint8_t>(at(v));
}

const char* lfoDestName(uint8_t dest, InstrType t) {
  static const char* const kNames[] = {
      "PITCH", "DECAY", "COLOR", "SHAPE", "SWEEP", "CONTOUR", "VOL", "CUTOFF", "DRIVE",
      "RESO", "FENV", "DLY", "RVB", "BIT", "SRR", "FINE", "DUTY", "SUB", "NOISE", "SEMI2",
      "L1 RATE", "L2 RATE", "L3 RATE", "L4 RATE", "L1 DEPTH", "L2 DEPTH", "L3 DEPTH", "L4 DEPTH",
      "L1 RTRG", "L2 RTRG", "L3 RTRG", "L4 RTRG", "PAN", "MOD", "FOLD", "RING", "SAT"};
  static const char* const kSyn[] = {"SHP1", "SHP2", "MIX", "DET", "SENV"};
  static const char* const kStr[] = {"DAMP", "BRIGHT", "STRUCT", "POS", "EXC"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<int>(LfoDest::Count), "LFO dest names");
  if (dest >= static_cast<uint8_t>(LfoDest::Count)) dest = 0;
  const int mac = dest - static_cast<int>(LfoDest::Dec);
  if (t == InstrType::Synth && mac >= 0 && mac < kFmMacros) return kSyn[mac];
  if (t == InstrType::String && mac >= 0 && mac < kFmMacros) return kStr[mac];
  return kNames[dest];
}

uint8_t folDestStep(uint8_t dest, int d, InstrType t) {
  auto rtrg = [](uint8_t x) {
    return x >= static_cast<uint8_t>(LfoDest::Rtrg1) && x <= static_cast<uint8_t>(LfoDest::Rtrg4);
  };
  const int step = d > 0 ? 1 : -1;
  for (int i = 0; i < (d > 0 ? d : -d); ++i) {
    uint8_t n = dest;
    do {
      const uint8_t m = lfoDestStep(n, step, t);
      if (m == n) return dest;  // the end of the list: stay on the last real target
      n = m;
    } while (rtrg(n));
    dest = n;
  }
  return dest;
}

// The follower / gate helpers (updateFollowers, a gate ATK / REL change): per control update or per
// change (folLevel / folCoef hot, gateMs in flash); fastmath's inline exp / log (no newlib call),
// accurate to float precision.
MT_HOT float folLevel(float pk) {
  const float x = pk * 0.25f;
  if (x < 0.00398f) return 0.f;  // -48 dB
  const float l = (6.0205999f * fastLog2(x) + 48.f) * (1.f / 48.f);
  return l < 0.f ? 0.f : (l > 1.f ? 1.f : l);
}

namespace {
constexpr float kFolStepMs = 32.f * 1000.f / kSynthRate;  // Synth::kControl samples
}
float folStepMs() { return kFolStepMs; }

MT_HOT float folCoef(uint8_t v) {
  const uint16_t ms = envTimeMs(v);
  return ms == 0 ? 1.f : 1.f - fastExp(-kFolStepMs * fastRcp(static_cast<float>(ms)));  // no __divsf3
}

bool Project::anySolo() const {
  for (const auto& t : tracks)
    if (t.solo) return true;
  return false;
}

float gateMs(uint8_t v) {
  const float x = (v > 127 ? 127 : v) * (1.f / 127.f);
  return 0.5f * fastExp(x * 4.6051702f);  // 0.5 x 100^x
}

const GatePreset kGatePreset[kGatePresets] = {
    {"8THS", 0x5555},        // x.x.x.x.x.x.x.x.
    {"OFFBEAT", 0xAAAA},     // .x.x.x.x.x.x.x.x
    {"16THS", 0xFFFF},       // xxxxxxxxxxxxxxxx
    {"QUARTERS", 0x1111},    // x...x...x...x...
    {"OFF 4THS", 0x4444},    // ..x...x...x...x.
    {"X.XX", 0xDDDD},        // x.xxx.xxx.xxx.xx
    {"XX.X", 0xBBBB},        // xx.xxx.xxx.xxx.x
    {"3-3-2", 0x4949},       // x..x..x.x..x..x.
    {"3-3-2 FILL", 0xDBDB},  // xx.xx.xxxx.xx.xx
    {"HALF BAR", 0x00FF},    // xxxxxxxx........
    {"PAIRS", 0x3333},       // xx..xx..xx..xx..
    {"SKIP", 0x52A5},        // x.x..x.x.x..x.x.
    {"SYNCOPE", 0x2449},     // x..x..x...x..x..
    {"STUTTER", 0x3CCF},     // xxxx..xx..xxxx..
    {"BUILD", 0xF511},       // x...x...x.x.xxxx
    {"GALLOP", 0x5D5D},      // x.xxx.x.x.xxx.x.
};

int gatePresetOf(uint16_t mask) {
  for (int i = 0; i < kGatePresets; ++i)
    if (kGatePreset[i].mask == mask) return i;
  return -1;
}

}  // namespace mt
