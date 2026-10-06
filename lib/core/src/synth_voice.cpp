#include "synth_voice.h"

namespace mt {

int allocVoice(Voice (&v)[kVoices], uint8_t track, bool mono, uint32_t& ageCounter, bool& legato, bool heavy,
               int polyMax) {
  legato = false;
  int pick = -1;
  int trackCount = 0, trackOldest = -1, trackNewest = -1, freeIdx = -1, oldest = -1, oldestRel = -1;
  int hvCount = 0, hvOldest = -1, hvOldestRel = -1;
  for (int i = 0; i < kVoices; ++i) {
    const Voice& x = v[i];
    if (!x.on) {
      if (freeIdx < 0) freeIdx = i;
      continue;
    }
    if (x.track == track) {
      ++trackCount;
      if (trackOldest < 0 || x.age < v[trackOldest].age) trackOldest = i;
      if (trackNewest < 0 || x.age > v[trackNewest].age) trackNewest = i;
    }
    if (oldest < 0 || x.age < v[oldest].age) oldest = i;
    const bool rel = x.env.stage() == Env::Stage::Release || x.env.idle();  // idle: a filter tail
    if (rel && (oldestRel < 0 || x.age < v[oldestRel].age)) oldestRel = i;
    if (heavyLoad(x)) {
      ++hvCount;
      if (hvOldest < 0 || x.age < v[hvOldest].age) hvOldest = i;
      if (rel && (hvOldestRel < 0 || x.age < v[hvOldestRel].age)) hvOldestRel = i;
    }
  }
  if (mono && trackNewest >= 0) {
    pick = trackNewest;
    legato = true;
  } else if (!mono && trackCount >= polyMax) {
    pick = trackOldest;
  } else if (freeIdx >= 0) {
    pick = freeIdx;
  } else {
    pick = oldestRel >= 0 ? oldestRel : oldest;
  }
  // Heavy limit: a new heavy voice (not one reusing a heavy voice) past kFmVoiceMax steals the
  // oldest heavy voice, releasing first, and leaves CHIP / SAMPLE voices alone. The victim fades
  // out (a hard cut clicks) while the note takes a free voice, if any.
  if (heavy && hvCount >= kFmVoiceMax && !heavyLoad(v[pick])) {
    // A mono track moving from its CHIP / SAMPLE voice: release the old voice.
    if (legato) v[pick].env.gate(false);
    const int victim = hvOldestRel >= 0 ? hvOldestRel : hvOldest;
    if (freeIdx >= 0) {
      v[victim].env.fade(kStealMs);
      v[victim].stolen = true;
      pick = freeIdx;
    } else {
      pick = victim;
    }
    legato = false;
  }
  Voice& p = v[pick];
  p.on = true;
  p.track = track;
  p.stolen = false;
  p.age = ++ageCounter;
  return pick;
}

}  // namespace mt
