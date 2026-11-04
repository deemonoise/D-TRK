#include "synth_voice.h"

namespace mt {

MT_CONTROL void collapseStealTail(Voice& v) {
  if (v.sample) v.hold = v.sampleTailHold;
  v.stolen = true;
  // `hold` already contains the voice's last post-filter sample, including its gain/envelope.
  // The engine state remains allocated for later reuse; only this 4 ms tail bypasses its DSP.
  v.fm = v.drum = v.syn = v.str = v.lofi = v.sample = v.heavy = false;
  v.fltOn = v.chipStep = false;
  v.wave = kStealTailWave;
  v.amp = v.ampNow = 1.f;
  v.ampStep = 0.f;
  v.env.hold(1.f);
  v.env.fade(kStealMs);
}

namespace {
MT_CONTROL void trimFullDspTailsWithPick(Voice (&v)[kVoices], int budget, int pick, int cost) {
  // Removing a voice from the nominal live sum did not remove its engine's work:
  // simultaneous note bursts can otherwise render the entire old pool for another 4 ms.
  int full = cost;
  for (int i = 0; i < kVoices; ++i)
    if (i != pick && v[i].on && !v[i].env.idle() && !isStealTail(v[i])) full += v[i].cost;
  while (full > budget) {
    int victim = -1;
    for (int i = 0; i < kVoices; ++i) {
      const Voice& x = v[i];
      if (i == pick || !x.on || !x.stolen || x.env.idle() || !x.cost || isStealTail(x)) continue;
      if (victim < 0 || x.age < v[victim].age) victim = i;
    }
    if (victim < 0) break;
    full -= v[victim].cost;
    collapseStealTail(v[victim]);
  }
}
}

MT_CONTROL void trimFullDspTails(Voice (&v)[kVoices], int budget) {
  if (budget >= kVoiceBudgetOff) return;
  trimFullDspTailsWithPick(v, budget, -1, 0);
}

int enforceBudget(Voice (&v)[kVoices], int pick, int cost, int budget) {
  cost = cost < 0 ? 0 : (cost > UINT16_MAX ? UINT16_MAX : cost);
  if (budget >= kVoiceBudgetOff) return cost;  // no limit: no scan
  int used = 0;
  for (int i = 0; i < kVoices; ++i)
    if (v[i].on && !v[i].stolen && i != pick) used += v[i].cost;  // the pick gives its cost back
  while (used + cost > budget) {
    int victim = -1;
    bool victimRel = false;
    for (int i = 0; i < kVoices; ++i) {
      const Voice& x = v[i];
      if (!x.on || x.stolen || !x.cost || i == pick) continue;
      const bool rel = x.env.stage() == Env::Stage::Release || x.env.idle();
      if (victim < 0 || (rel && !victimRel) || (rel == victimRel && x.age < v[victim].age)) {
        victim = i;
        victimRel = rel;
      }
    }
    if (victim < 0) break;
    v[victim].env.fade(kStealMs);
    v[victim].stolen = true;
    used -= v[victim].cost;
  }
  trimFullDspTailsWithPick(v, budget, pick, cost);
  return cost;
}

int allocVoice(Voice (&v)[kVoices], uint8_t track, bool mono, uint32_t& ageCounter, bool& legato, bool heavy,
               int polyMax, int cap, int cost, int budget) {
  legato = false;
  int pick = -1;
  int trackCount = 0, trackOldest = -1, trackNewest = -1, freeIdx = -1, oldest = -1, oldestRel = -1;
  int hvCount = 0, hvOldest = -1, hvOldestRel = -1, live = 0;
  for (int i = 0; i < kVoices; ++i) {
    const Voice& x = v[i];
    if (!x.on) {
      if (freeIdx < 0) freeIdx = i;
      continue;
    }
    live += !x.stolen;
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
  if (live >= cap) freeIdx = -1;  // the CPU guard: the pool is full at cap
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
  cost = enforceBudget(v, pick, cost, budget);
  Voice& p = v[pick];
  p.on = true;
  p.track = track;
  p.stolen = false;
  p.cost = static_cast<uint16_t>(cost);  // clamped by enforceBudget
  p.age = ++ageCounter;
  return pick;
}

}  // namespace mt
