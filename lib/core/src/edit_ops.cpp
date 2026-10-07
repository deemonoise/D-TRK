#include "edit_ops.h"
#include <string.h>

namespace mt {

namespace {
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

Sel makeSel(int trackA, int stepA, int trackB, int stepB) {
  int ta = clampi(trackA, 0, kTracks - 1), tb = clampi(trackB, 0, kTracks - 1);
  int sa = clampi(stepA, 0, kMaxSteps - 1), sb = clampi(stepB, 0, kMaxSteps - 1);
  Sel s;
  s.t0 = static_cast<uint8_t>(ta < tb ? ta : tb);
  s.t1 = static_cast<uint8_t>(ta < tb ? tb : ta);
  s.s0 = static_cast<uint8_t>(sa < sb ? sa : sb);
  s.s1 = static_cast<uint8_t>(sa < sb ? sb : sa);
  return s;
}

void copySel(const Pattern& p, const Sel& s, Clipboard& cb) {
  cb.tracks = static_cast<uint8_t>(s.t1 - s.t0 + 1);
  cb.steps = static_cast<uint8_t>(s.s1 - s.s0 + 1);
  for (int t = 0; t < cb.tracks; ++t)
    for (int i = 0; i < cb.steps; ++i) cb.data[t][i] = p.steps[s.t0 + t][s.s0 + i];
}

void pasteAt(Pattern& p, const Clipboard& cb, int track, int step) {
  if (track < 0 || step < 0) return;
  int len = p.length < kMaxSteps ? p.length : kMaxSteps;
  for (int t = 0; t < cb.tracks && track + t < kTracks; ++t)
    for (int i = 0; i < cb.steps && step + i < len; ++i) p.steps[track + t][step + i] = cb.data[t][i];
}

void clearSel(Pattern& p, const Sel& s) {
  for (int t = s.t0; t <= s.t1; ++t)
    for (int i = s.s0; i <= s.s1; ++i) p.steps[t][i] = Step();
}

void transposeSel(Pattern& p, const Sel& s, int amount, bool degrees, uint8_t root, ScaleType type,
                  const bool* drumTracks) {
  for (int t = s.t0; t <= s.t1; ++t) {
    if (drumTracks && drumTracks[t]) continue;
    for (int i = s.s0; i <= s.s1; ++i) {
      Step& st = p.steps[t][i];
      if (!st.hasNote()) continue;
      int n = degrees ? moveDegrees(st.note, amount, root, type) : clampi(st.note + amount, 0, 127);
      st.note = static_cast<uint8_t>(n);
    }
  }
}

bool chainInsert(Project& p, int at, uint8_t pat) {
  const int n = p.chainLen;
  if (n >= kChainMax) return false;
  at = clampi(at, 0, n);
  const size_t k = static_cast<size_t>(n - at);
  memmove(p.chain + at + 1, p.chain + at, k);
  memmove(p.chainTr + at + 1, p.chainTr + at, k);
  memmove(p.chainRep + at + 1, p.chainRep + at, k);
  memmove(p.chainScene + at + 1, p.chainScene + at, k);
  p.chain[at] = pat < kPatterns ? pat : kPatterns - 1;
  p.chainTr[at] = 0;
  p.chainRep[at] = 1;
  p.chainScene[at] = 0;
  p.chainLen = static_cast<uint8_t>(n + 1);
  return true;
}

void chainDelete(Project& p, int at) {
  const int n = p.chainLen;
  if (at < 0 || at >= n) return;
  const size_t k = static_cast<size_t>(n - at - 1);
  memmove(p.chain + at, p.chain + at + 1, k);
  memmove(p.chainTr + at, p.chainTr + at + 1, k);
  memmove(p.chainRep + at, p.chainRep + at + 1, k);
  memmove(p.chainScene + at, p.chainScene + at + 1, k);
  p.chain[n - 1] = 0;
  p.chainTr[n - 1] = 0;
  p.chainRep[n - 1] = 1;
  p.chainScene[n - 1] = 0;
  p.chainLen = static_cast<uint8_t>(n - 1);
}

}  // namespace mt
