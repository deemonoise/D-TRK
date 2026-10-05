#include "synth_voice.h"

namespace mt {

int allocVoice(Voice (&v)[kVoices], uint8_t track, bool mono, uint32_t& ageCounter, bool& legato, bool fm) {
  legato = false;
  int pick = -1;
  int trackCount = 0, trackOldest = -1, trackNewest = -1, freeIdx = -1, oldest = -1, oldestRel = -1;
  int fmCount = 0, fmOldest = -1, fmOldestRel = -1;
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
    const bool rel = x.env.stage() == Env::Stage::Release;
    if (rel && (oldestRel < 0 || x.age < v[oldestRel].age)) oldestRel = i;
    if (x.fm) {
      ++fmCount;
      if (fmOldest < 0 || x.age < v[fmOldest].age) fmOldest = i;
      if (rel && (fmOldestRel < 0 || x.age < v[fmOldestRel].age)) fmOldestRel = i;
    }
  }
  if (mono && trackNewest >= 0) {
    pick = trackNewest;
    legato = true;
  } else if (!mono && trackCount >= kPolyPerTrack) {
    pick = trackOldest;
  } else if (freeIdx >= 0) {
    pick = freeIdx;
  } else {
    pick = oldestRel >= 0 ? oldestRel : oldest;
  }
  // FM limit: a new FM voice (not one reusing an FM voice) past kFmVoiceMax takes the oldest FM
  // voice, releasing first, and leaves free / CHIP / SAMPLE voices alone.
  if (fm && fmCount >= kFmVoiceMax && !(v[pick].on && v[pick].fm)) {
    // A mono track moving from its CHIP / SAMPLE voice to the stolen one: release the old voice.
    if (legato) v[pick].env.gate(false);
    pick = fmOldestRel >= 0 ? fmOldestRel : fmOldest;
    legato = false;
  }
  Voice& p = v[pick];
  p.on = true;
  p.track = track;
  p.age = ++ageCounter;
  return pick;
}

}  // namespace mt
