#include "euclid.h"
#include "rng.h"

namespace mt {

namespace {
constexpr uint8_t kNil = 0xFF;

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

// Bjorklund by group pairing: start with `hits` groups [1] and `steps-hits` groups [0];
// repeatedly append one remainder group to each front group until at most one remainder
// group is left. Groups are linked lists over element indexes (no heap, ~640 B stack).
void euclid(int hits, int steps, int rotation, bool* out) {
  steps = clampInt(steps, 0, kMaxSteps);
  if (steps == 0) return;
  hits = clampInt(hits, 0, steps);

  bool val[kMaxSteps];
  uint8_t next[kMaxSteps], head[kMaxSteps], tail[kMaxSteps];
  for (int i = 0; i < steps; ++i) {
    val[i] = i < hits;
    next[i] = kNil;
    head[i] = tail[i] = static_cast<uint8_t>(i);
  }
  int n = steps;   // group count
  int f = hits;    // front groups: [0, f), remainder groups: [f, n)
  while (f > 0 && n - f > 1) {
    const int r = n - f;
    const int k = f < r ? f : r;
    for (int i = 0; i < k; ++i) {
      next[tail[i]] = head[f + i];
      tail[i] = tail[f + i];
    }
    // Leftover groups become the new remainder, right after the k merged ones.
    int m = k;
    const int from = f > r ? k : f + k;
    const int to = f > r ? f : n;
    for (int i = from; i < to; ++i, ++m) {
      head[m] = head[i];
      tail[m] = tail[i];
    }
    n = m;
    f = k;
  }

  int rot = rotation % steps;
  if (rot < 0) rot += steps;
  int pos = 0;
  for (int g = 0; g < n; ++g)
    for (uint8_t e = head[g]; e != kNil; e = next[e]) out[(pos++ + rot) % steps] = val[e];
}

void applyEuclid(Pattern& p, int track, const EuclidParams& e, uint8_t root, ScaleType scale) {
  if (track < 0 || track >= kTracks) return;
  const int plen = clampInt(p.length, 1, kMaxSteps);
  const int len = clampInt(e.length, 1, kMaxSteps);
  bool hit[kMaxSteps];
  euclid(e.hits, len, e.rotation, hit);

  if (e.lane >= 0 && e.lane < kKitLanes) {
    const uint8_t bit = static_cast<uint8_t>(1u << e.lane);
    Step* tr = p.steps[track];
    int k = 0;  // hit index
    for (int s = 0; s < plen; ++s) {
      Step& st = tr[s];
      if (!e.merge && st.hasNote() && (st.vel & bit)) {
        st.vel &= static_cast<uint8_t>(~bit);
        // Nothing left but a default-velocity step: it goes.
        if (!st.vel && st.note == 0 && !st.hasFx()) st = Step();
      }
      if (!hit[s % len]) continue;
      const int hitIdx = k++;
      if (!st.hasNote()) {
        st.note = 0;
        st.vel = 0;
      }
      st.vel |= bit;
      if (e.accentEvery > 0 && hitIdx % e.accentEvery == 0 && st.note == 0) st.note = e.accentVel;
    }
    return;
  }

  // Base note snapped to the nearest scale note at or below it (above if there is none below).
  const int want = e.baseNote > 127 ? 127 : e.baseNote;
  int base = want;
  while (base >= 0 && !inScale(base, root, scale)) --base;
  if (base < 0) {
    base = want;
    while (base <= 127 && !inScale(base, root, scale)) ++base;
    if (base > 127) base = want;
  }
  const int octaves = clampInt(e.range, 1, 4);
  // Ranged fills: drop the base by octaves so the top of the range fits under 128 (the
  // highest degree used is one scale step below base + 12 * octaves).
  if (e.fill != EuclidFill::Root && e.fill != EuclidFill::Down)
    while (base + 12 * octaves > 128 && base >= 12) base -= 12;

  const uint16_t mask = scaleMask(scale);
  int perOct = 0;
  for (int i = 0; i < 12; ++i) perOct += (mask >> i) & 1;
  if (perOct == 0) perOct = 12;
  const int count = perOct * octaves;  // degrees in range
  Rng rng(e.seed);

  Step* tr = p.steps[track];
  int k = 0;  // hit index
  for (int s = 0; s < plen; ++s) {
    if (!hit[s % len]) {
      if (!e.merge) tr[s] = Step();
      continue;
    }
    const int hitIdx = k++;
    if (e.merge && !tr[s].isEmpty()) continue;

    int deg = 0;
    switch (e.fill) {
      case EuclidFill::Up: deg = hitIdx % count; break;
      case EuclidFill::Down: deg = -(hitIdx % count); break;
      case EuclidFill::UpDown: {
        const int period = count > 1 ? 2 * (count - 1) : 1;
        const int ph = hitIdx % period;
        deg = ph < count ? ph : period - ph;
        break;
      }
      case EuclidFill::Random: deg = static_cast<int>(rng.below(static_cast<uint32_t>(count))); break;
      default: deg = 0; break;
    }
    Step st;
    st.note = static_cast<uint8_t>(deg ? moveDegrees(base, deg, root, scale) : base);
    const bool accent = e.accentEvery > 0 && hitIdx % e.accentEvery == 0;
    st.vel = accent ? e.accentVel : e.vel;
    tr[s] = st;
  }
}

}  // namespace mt
