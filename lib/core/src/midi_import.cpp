#include "midi_import.h"

namespace mt {

namespace {

// floor(a / b) for b > 0.
int64_t floorDiv(int64_t a, int64_t b) {
  const int64_t q = a / b;
  return (a % b != 0 && a < 0) ? q - 1 : q;
}

// round(a / b), halves away from zero, b > 0.
int64_t roundDiv(int64_t a, int64_t b) { return a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b); }

struct Placed {
  int pattern;  // relative to firstPattern
  int step;
  int track;
  uint8_t note;
};

// Grid: one step = q / 96 file ticks, q = ppq * ticksPerStep(quant). Kept as a fraction so
// triplets on any ppq do not drift.
struct Ctx {
  const SmfInfo& info;
  const ImportMap& m;
  int64_t q;
  int64_t offset;
  int len;
  int first;
  int avail;  // patterns available from first..P16
};

// Returns false for notes that are ignored (unmapped, before the offset). Sets `range` false
// for notes out of 0..127 after transpose. rel = tick - offset is returned for microtiming.
bool place(const Ctx& c, const SmfNote& n, Placed& out, bool& inRange, int64_t& rel) {
  if (n.src >= c.info.sourceCount || n.src >= kSmfMaxSources) return false;
  const int t = c.m.target[n.src];
  if (t < 0 || t >= kTracks) return false;
  rel = static_cast<int64_t>(n.tick) - c.offset;
  const int64_t step = floorDiv(2 * rel * kPpqn + c.q, 2 * c.q);  // round half up
  if (step < 0) return false;
  const int note = n.note + c.m.transpose[n.src];
  inRange = note >= 0 && note <= 127;
  const int64_t pat = step / c.len;
  out.pattern = pat > 0x7FFF ? 0x7FFF : static_cast<int>(pat);
  out.step = static_cast<int>(step % c.len);
  out.track = t;
  out.note = inRange ? static_cast<uint8_t>(note) : 0;
  return true;
}

}  // namespace

uint8_t nearestGate(uint32_t len, int64_t q) {
  if (q <= 0) return 1;
  const int64_t want = static_cast<int64_t>(len) * kPpqn * 100;  // percent * q
  // The error |gatePercent(v) * q - want| is unimodal in v, so the answer is one of the two
  // values bracketing want: 1..100 step 1 %, 100..200 step 7 % (100 + (v - 100) * 7).
  int v;
  if (want <= 100 * q) {
    const int64_t f = want / q;
    v = f < 1 ? 1 : (f > 99 ? 99 : static_cast<int>(f));
  } else {
    const int64_t k = (want - 100 * q) / (7 * q);
    v = k > 99 ? 199 : 100 + static_cast<int>(k);
  }
  auto err = [&](int g) {
    const int64_t e = static_cast<int64_t>(gatePercent(static_cast<uint8_t>(g))) * q - want;
    return e < 0 ? -e : e;
  };
  return static_cast<uint8_t>(err(v + 1) < err(v) ? v + 1 : v);
}

namespace {

// The tempo after the import (bpm when the file's is not used).
uint16_t importBpm(const SmfInfo& info, const ImportMap& m, uint16_t bpm) {
  if (!m.useTempo || info.firstTempoUsPerQ == 0) return bpm;
  uint32_t b = (60000000u + info.firstTempoUsPerQ / 2) / info.firstTempoUsPerQ;
  return static_cast<uint16_t>(b < 20 ? 20 : (b > 300 ? 300 : b));
}

Ctx makeCtx(const SmfInfo& info, const ImportMap& m) {
  int len = m.patternLen;
  if (len < kMinSteps) len = kMinSteps;
  if (len > kMaxSteps) len = kMaxSteps;
  const int first = m.firstPattern < kPatterns ? m.firstPattern : kPatterns;
  return Ctx{info, m, static_cast<int64_t>(info.ppq) * ticksPerStep(m.quant),
             static_cast<int64_t>(m.offsetBars) * 4 * info.ppq, len, first, kPatterns - first};
}

// Pass 1: how many patterns the material needs.
int patternsNeeded(const Ctx& c, const SmfNote* notes, uint32_t n) {
  int needed = 0;
  for (uint32_t i = 0; i < n; ++i) {
    Placed pl;
    bool ok;
    int64_t rel;
    if (!place(c, notes[i], pl, ok, rel) || !ok) continue;
    if (pl.pattern < c.avail && pl.pattern + 1 > needed) needed = pl.pattern + 1;
  }
  // Trailing silence up to the end of the file (end rounded to the nearest step).
  if (needed > 0 && static_cast<int64_t>(c.info.lastTick) > c.offset) {
    const int64_t rel = static_cast<int64_t>(c.info.lastTick) - c.offset;
    const int64_t steps = floorDiv(2 * rel * kPpqn + c.q, 2 * c.q);
    const int64_t pats = (steps + c.len - 1) / c.len;
    const int clamped = pats > c.avail ? c.avail : static_cast<int>(pats);
    if (clamped > needed) needed = clamped;
  }
  return needed;
}

}  // namespace

ImportResult importPlan(const SmfInfo& info, const SmfNote* notes, uint32_t n, const ImportMap& m, uint16_t bpm) {
  ImportResult r{0, importBpm(info, m, bpm), 0};
  if (info.ppq == 0) return r;
  r.patternsWritten = static_cast<uint8_t>(patternsNeeded(makeCtx(info, m), notes, n));
  return r;
}

ImportResult importSmf(const SmfInfo& info, const SmfNote* notes, uint32_t n, const ImportMap& m,
                       Project& p) {
  p.bpm = importBpm(info, m, p.bpm);
  ImportResult r{0, p.bpm, 0};
  if (info.ppq == 0) return r;
  const Ctx c = makeCtx(info, m);
  const int len = c.len, first = c.first;
  const int needed = patternsNeeded(c, notes, n);
  r.patternsWritten = static_cast<uint8_t>(needed);

  bool isTarget[kTracks] = {false};
  for (int s = 0; s < info.sourceCount && s < kSmfMaxSources; ++s)
    if (m.target[s] >= 0 && m.target[s] < kTracks) isTarget[m.target[s]] = true;

  for (int i = 0; i < needed; ++i) {
    Pattern& pat = p.patterns[first + i];
    pat.length = static_cast<uint8_t>(len);
    pat.res = m.quant;
    for (int t = 0; t < kTracks; ++t)
      if (isTarget[t]) {
        for (Step& s : pat.steps[t]) s = Step();
        pat.trackLen[t] = 0;  // the full pattern length: no imported step may fall past a loop
      }
    pat.fitTrackLen();
  }
  // Pass 2: write.
  for (uint32_t i = 0; i < n; ++i) {
    const SmfNote& sn = notes[i];
    Placed pl;
    bool ok;
    int64_t rel;
    if (!place(c, sn, pl, ok, rel)) continue;
    if (!ok || pl.pattern >= needed) {
      ++r.notesDropped;
      continue;
    }
    Step& s = p.patterns[first + pl.pattern].steps[pl.track][pl.step];
    const TrackCfg& tc = p.tracks[pl.track];
    Step ns;
    // Drum track: the note picks the lane with that note (none: dropped); notes on one step OR
    // into its lane mask, the step velocity is the first note's.
    const Instrument* kit = p.kitOf(pl.track);
    if (kit) {
      int lane = -1;
      for (int l = 0; l < kKitLanes && lane < 0; ++l)
        if (kit->kit[l].note == pl.note) lane = l;
      if (lane < 0) {
        ++r.notesDropped;
        continue;
      }
      if (s.hasNote()) {
        s.vel |= static_cast<uint8_t>(1u << lane);
        continue;
      }
      ns.note = sn.vel == tc.defVel ? 0 : sn.vel;
      ns.vel = static_cast<uint8_t>(1u << lane);
    } else {
      if (s.hasNote()) {
        ++r.notesDropped;
        const bool replace = (m.mono == MonoMode::Highest && pl.note > s.note) ||
                             (m.mono == MonoMode::Lowest && pl.note < s.note);
        if (!replace) continue;
      }
      ns.note = pl.note;
      ns.vel = sn.vel == tc.defVel ? kVelDefault : sn.vel;
    }
    int slot = 0;
    const uint8_t srcCh = info.src[sn.src].channel & 0x0F;
    if (m.useSourceChannel && srcCh != (tc.channel & 0x0F))
      ns.fx[slot++] = FxSlot{Fx::CHN, static_cast<uint8_t>(srcCh + 1)};
    const uint8_t g = nearestGate(sn.len, c.q);
    const int diff = static_cast<int>(gatePercent(g)) - static_cast<int>(gatePercent(tc.defGate));
    if (!kit && (diff > 10 || diff < -10) && slot < kFxSlots) ns.fx[slot++] = FxSlot{Fx::GAT, g};
    if (m.keepMicrotiming && slot < kFxSlots) {
      const int64_t stepAbs = static_cast<int64_t>(pl.pattern) * len + pl.step;
      int64_t ndg = roundDiv((rel * kPpqn - stepAbs * c.q) * 100, c.q);
      if (ndg > 50) ndg = 50;
      if (ndg < -50) ndg = -50;
      if (ndg >= 3 || ndg <= -3) ns.fx[slot++] = FxSlot{Fx::NDG, static_cast<uint8_t>(static_cast<int8_t>(ndg))};
    }
    s = ns;
  }
  return r;
}

}  // namespace mt
