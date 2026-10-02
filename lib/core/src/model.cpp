#include "model.h"
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
    snprintf(tracks[i].name, sizeof(tracks[i].name), "TRK%d", i + 1);
  }
  for (auto& p : patterns) p.clear();
  memset(chain, 0, sizeof(chain));
  chainLen = 0;
  songMode = false;
}

bool Project::anySolo() const {
  for (const auto& t : tracks)
    if (t.solo) return true;
  return false;
}

}  // namespace mt
