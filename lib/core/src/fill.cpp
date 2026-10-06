#include "fill.h"
#include "euclid.h"
#include "fx_info.h"
#include "rng.h"

namespace mt {

namespace {

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Fx values as encoder positions: index 0 = the lowest value fxStep reaches.
constexpr int kFxIndexMax = 255;
uint8_t fxAt(Fx f, int i) { return fxStep(f, fxStep(f, 0, -100000), i); }
int fxIndex(Fx f, uint8_t v) {
  for (int i = 0; i <= kFxIndexMax; ++i) {
    const uint8_t a = fxAt(f, i);
    if (a == v) return i;
    if (i > 0 && a == fxAt(f, i - 1)) break;  // past the top
  }
  return 0;
}

// Linear from a to b at hit k of m, rounded.
int lerp(int a, int b, int k, int m) {
  if (m <= 1) return a;
  const int num = (b - a) * k;
  const int den = m - 1;
  return a + (num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den));
}

// Value of hit k of m in from..to (as ints: notes, velocities, fx indexes).
int pick(FillValue v, int from, int to, int k, int m, Rng& rng) {
  switch (v) {
    case FillValue::Ramp: return lerp(from, to, k, m);
    case FillValue::Random: {
      const int lo = from < to ? from : to, hi = from < to ? to : from;
      return lo + static_cast<int>(rng.below(static_cast<uint32_t>(hi - lo + 1)));
    }
    default: return from;
  }
}

}  // namespace

void fillHits(const FillSpec& f, int n, bool* out) {
  n = clampInt(n, 0, kMaxSteps);
  switch (f.where) {
    case FillWhere::Euclid: {
      const int len = clampInt(f.length, 1, kMaxSteps);
      bool h[kMaxSteps];
      euclid(f.hits, len, f.rotation, h);
      for (int i = 0; i < n; ++i) out[i] = h[i % len];
      break;
    }
    case FillWhere::Random: {
      Rng rng(f.seed);
      for (int i = 0; i < n; ++i) out[i] = static_cast<int>(rng.below(100)) < f.density;
      break;
    }
    default: {
      const int every = f.every < 1 ? 1 : f.every;
      for (int i = 0; i < n; ++i) out[i] = i >= f.offset && (i - f.offset) % every == 0;
      break;
    }
  }
}

void applyFill(Pattern& p, const Sel& sel, const FillSpec& f, uint8_t root, ScaleType scale,
               const bool* drumTracks) {
  const int plen = clampInt(p.length, 1, kMaxSteps);
  const int s0 = clampInt(sel.s0, 0, plen - 1);
  const int s1 = clampInt(sel.s1, s0, plen - 1);
  const int n = s1 - s0 + 1;
  bool hit[kMaxSteps];
  fillHits(f, n, hit);
  int m = 0;
  for (int i = 0; i < n; ++i) m += hit[i];

  // Ramp / random notes run over the scale notes between from and to.
  uint8_t notes[128];
  int noteCount = 0;
  const bool scaleNotes = f.target == FillTarget::Note && f.value != FillValue::Const;
  if (scaleNotes) {
    const int lo = f.from < f.to ? f.from : f.to, hi = f.from < f.to ? f.to : f.from;
    for (int nt = lo; nt <= hi && nt < 128; ++nt)
      if (inScale(nt, root, scale)) notes[noteCount++] = static_cast<uint8_t>(nt);
  }
  const bool fxTarget = f.target == FillTarget::Fx && f.slot < kFxSlots;
  const int fxFrom = fxTarget && f.cmd != Fx::None ? fxIndex(f.cmd, f.from) : 0;
  const int fxTo = fxTarget && f.cmd != Fx::None ? fxIndex(f.cmd, f.to) : 0;
  Rng rng(f.seed ^ 0x9E3779B9u);

  for (int t = sel.t0; t <= sel.t1 && t < kTracks; ++t) {
    const bool drum = drumTracks && drumTracks[t];
    Step* tr = p.steps[t];
    int k = 0;
    for (int i = 0; i < n; ++i) {
      if (!hit[i]) continue;
      const int hitIdx = k++;
      Step& st = tr[s0 + i];
      switch (f.target) {
        case FillTarget::Note: {
          if (f.mode == FillMode::Empty && st.note != kNoteEmpty) break;
          if (f.mode == FillMode::Notes && !st.hasNote()) break;
          if (drum) {
            if (!st.hasNote()) {
              st.note = 0;
              st.vel = 0;
            }
            st.vel |= static_cast<uint8_t>(1u << (f.lane % kKitLanes));
            break;
          }
          int note;
          if (!scaleNotes) {
            note = f.from;
          } else if (noteCount == 0) {
            note = f.from;
          } else {
            // Ramp: index into the scale notes, descending when from > to.
            int idx = pick(f.value, 0, noteCount - 1, hitIdx, m, rng);
            if (f.value == FillValue::Ramp && f.from > f.to) idx = noteCount - 1 - idx;
            note = notes[idx];
          }
          st.note = static_cast<uint8_t>(clampInt(note, 0, 127));
          break;
        }
        case FillTarget::Vel: {
          if (!st.hasNote()) break;
          uint8_t& field = drum ? st.note : st.vel;
          if (f.mode == FillMode::Empty && field != kVelDefault) break;
          field = static_cast<uint8_t>(clampInt(pick(f.value, f.from, f.to, hitIdx, m, rng), 1, 127));
          break;
        }
        case FillTarget::Fx: {
          if (!fxTarget) break;
          FxSlot& sl = st.fx[f.slot];
          if (f.mode == FillMode::Empty && sl.cmd != Fx::None) break;
          if (f.mode == FillMode::Notes && !st.hasNote()) break;
          if (f.cmd == Fx::None) {
            sl = FxSlot{};
            break;
          }
          sl.cmd = f.cmd;
          sl.val = fxAt(f.cmd, pick(f.value, fxFrom, fxTo, hitIdx, m, rng));
          break;
        }
        default: break;
      }
    }
  }
}

}  // namespace mt
