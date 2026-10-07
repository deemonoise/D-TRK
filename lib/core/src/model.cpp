#include "model.h"
#include "hot.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace mt {

namespace {
constexpr float kRate = 32000.f;  // = kSynthRate (synth_osc.h)
}

void Pattern::clear() {
  length = kDefaultSteps;
  res = Resolution::Sixteenth;
  swing = 50;
  memset(trackLen, 0, sizeof(trackLen));
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
constexpr InstrType kTypeOrder[] = {InstrType::Fm, InstrType::Synth, InstrType::Drum, InstrType::Sample,
                                    InstrType::Chip, InstrType::Kit};
static_assert(sizeof(kTypeOrder) == static_cast<size_t>(InstrType::Count), "type order");
}  // namespace

InstrType instrTypeAt(int k) {
  return k >= 0 && k < static_cast<int>(InstrType::Count) ? kTypeOrder[k] : InstrType::Fm;
}

int instrTypePos(InstrType t) {
  for (int k = 0; k < static_cast<int>(InstrType::Count); ++k)
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
  masterVol = 40;
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
  djFilter = 0;
  for (int i = 0; i < kPerfButtons; ++i) perfMap[i] = static_cast<uint8_t>(i + 1);
  for (ProjSample& s : samples) s = ProjSample{};
  sampleCount = 0;
  for (ProjWavetable& w : wavetables) w = ProjWavetable{};
  wavetableCount = 0;
  hasSampleList = false;
  for (auto& p : patterns) p.clear();
  memset(chain, 0, sizeof(chain));
  chainLen = 0;
  memset(chainTr, 0, sizeof(chainTr));
  memset(chainRep, 1, sizeof(chainRep));
  memset(chainScene, 0, sizeof(chainScene));
  songMode = false;
  for (uint16_t& sc : scenes) sc = kSceneEmpty;
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

float lfoSyncHz(uint8_t div, uint16_t bpm) {
  // Beats (quarters) per LFO cycle.
  static const float kBeats[kLfoSyncSteps] = {0.125f, 1.f / 6, 0.25f, 1.f / 3, 0.5f, 2.f / 3, 1, 2, 4, 8, 16, 32};
  return (bpm ? bpm : 120) / 60.f / kBeats[div < kLfoSyncSteps ? div : kLfoSyncSteps - 1];
}

const char* lfoSyncName(uint8_t div) {
  static const char* const kNames[kLfoSyncSteps] = {"1/32", "1/16T", "1/16", "1/8T", "1/8",  "1/4T",
                                                    "1/4",  "1/2",   "1 BAR", "2 BARS", "4 BARS", "8 BARS"};
  return kNames[div < kLfoSyncSteps ? div : kLfoSyncSteps - 1];
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

float resoQ(float v) {
  v = v < 0 ? 0 : (v > 127 ? 127 : v);
  return 0.5f * powf(40.f, v / 127.f);
}

MT_HOT float filterEnv(uint32_t t, uint8_t fAtk, uint8_t fDec) {
  const uint32_t a = static_cast<uint32_t>(envTimeMs(fAtk) * kRate / 1000.f);
  if (t < a) return static_cast<float>(t) / a;
  const float d = envTimeMs(fDec) * kRate / 1000.f;
  if (d <= 0) return 1.f;
  return expf(-6.9077553f * (t - a) / d);  // ln(1000): -60 dB at d
}

void drumSetMachine(Instrument& m, uint8_t machine) {
  // DECAY, COLOR, SHAPE, SWEEP, CONTOUR per machine (see synth_drum_machines.cpp).
  static const uint8_t kDefaults[static_cast<int>(DrumMachine::Count)][kFmMacros] = {
      {90, 20, 10, 40, 50},  // BD8
      {55, 50, 70, 30, 64},  // SD8
      {75, 20, 0, 40, 50},   // TOM8
      {60, 64, 64, 40, 50},  // CP8
      {40, 64, 30, 20, 0},   // RS8
      {50, 64, 0, 10, 0},    // CL8
      {70, 64, 64, 50, 0},   // CB8
      {30, 64, 30, 64, 64},  // HH8: closed
      {90, 64, 40, 64, 64},  // CY8
      {75, 70, 40, 60, 40},  // BD9
      {55, 64, 80, 30, 64},  // SD9
      {70, 20, 10, 50, 50},  // TOM9
      {60, 70, 64, 50, 50},  // CP9
      {35, 64, 30, 20, 0},   // RS9
      {30, 70, 50, 64, 64},  // HH9: closed
      {95, 64, 50, 64, 64},  // CY9
  };
  constexpr int kLast = static_cast<int>(DrumMachine::Count) - 1;
  m.machine = static_cast<uint8_t>(machine > kLast ? kLast : machine);
  for (int k = 0; k < kFmMacros; ++k) m.macro[k] = kDefaults[m.machine][k];
}

void instrSetType(Instrument& m, InstrType t) {
  if (t >= InstrType::Count) t = InstrType::Chip;
  m.type = t;
  if (t == InstrType::Fm) fmSetMachine(m, m.machine);
  else if (t == InstrType::Drum) drumSetMachine(m, m.machine);
  else if (t == InstrType::Synth) {
    static const uint8_t kDef[kFmMacros] = {0, 0, 0, 64, 64};  // SHP1, SHP2, MIX, DET, SENV
    memcpy(m.macro, kDef, kFmMacros);
  } else if (t == InstrType::Kit) kitSetDefaults(m);
  for (int i = 0; i < kLfos; ++i) {
    uint8_t& dest = lfoRef(m, i).dest;
    const bool macroDest = dest >= static_cast<uint8_t>(LfoDest::Dec) && dest <= static_cast<uint8_t>(LfoDest::Con);
    if ((t == InstrType::Chip || t == InstrType::Sample) && macroDest) dest = static_cast<uint8_t>(LfoDest::Pitch);
  }
}

void kitSetDefaults(Instrument& m) {
  for (int k = 0; k < kKitLanes; ++k) {
    m.kit[k] = KitLane();
    m.kit[k].note = static_cast<uint8_t>(60 + k);
  }
}

uint8_t lfoDestStep(uint8_t dest, int d, bool macros) {
  constexpr int kLast = static_cast<int>(LfoDest::Count) - 1;
  constexpr int kMac0 = static_cast<int>(LfoDest::Dec), kMac1 = static_cast<int>(LfoDest::Con);
  int v = dest > kLast ? 0 : dest;
  const int step = d > 0 ? 1 : -1;
  for (int i = 0; i < (d > 0 ? d : -d); ++i) {
    int nv = v + step;
    while (!macros && nv >= kMac0 && nv <= kMac1) nv += step;
    if (nv < 0 || nv > kLast) break;
    v = nv;
  }
  return static_cast<uint8_t>(v);
}

bool Project::anySolo() const {
  for (const auto& t : tracks)
    if (t.solo) return true;
  return false;
}

}  // namespace mt
