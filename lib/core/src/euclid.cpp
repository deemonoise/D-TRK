#include "euclid.h"

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

}  // namespace mt
