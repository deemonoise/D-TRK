#include "model.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace mt {

void Pattern::clear() {
  length = kDefaultSteps;
  res = Resolution::Sixteenth;
  swing = 50;
  for (auto& tr : steps)
    for (auto& s : tr) s = Step();
}

bool Pattern::isEmpty() const {
  for (const auto& tr : steps)
    for (const auto& s : tr)
      if (!s.isEmpty()) return false;
  return true;
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
    snprintf(instruments[i].name, sizeof(instruments[i].name), "INS%d", i + 1);
  }
  masterVol = 40;
  preview = true;
  for (auto& p : patterns) p.clear();
  memset(chain, 0, sizeof(chain));
  chainLen = 0;
  songMode = false;
}

uint16_t envTimeMs(uint8_t v) {
  if (v == 0) return 0;
  if (v > 127) v = 127;
  return static_cast<uint16_t>(lroundf(powf(10000.f, v / 127.f)));
}

uint16_t fmDecayMs(uint8_t v) {
  if (v > 127) v = 127;
  return static_cast<uint16_t>(5.f * powf(800.f, v / 127.f) + 0.5f);
}

float lfoHz(uint8_t v) {
  if (v > 127) v = 127;
  return 0.05f * powf(600.f, v / 127.f);
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

bool Project::anySolo() const {
  for (const auto& t : tracks)
    if (t.solo) return true;
  return false;
}

}  // namespace mt
