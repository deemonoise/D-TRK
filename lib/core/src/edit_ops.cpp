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

namespace {
bool isJump(const SongFollow& f) { return f.act == static_cast<uint8_t>(FollowAct::Jump); }
}  // namespace

bool songInsert(Project& p, int at, uint8_t pat, uint16_t mute, SongFollow fol, int8_t trn, uint16_t bpm) {
  const int n = p.songLen;
  if (n >= kSongMax) return false;
  at = clampi(at, 0, n);
  for (int i = 0; i < n; ++i)
    if (isJump(p.songFol[i]) && p.songFol[i].target >= at && p.songFol[i].target < kSongMax - 1) ++p.songFol[i].target;
  if (isJump(fol) && fol.target >= at && fol.target < kSongMax - 1) ++fol.target;
  const size_t k = static_cast<size_t>(n - at);
  memmove(p.song + at + 1, p.song + at, k);
  memmove(p.songMute + at + 1, p.songMute + at, k * sizeof(p.songMute[0]));
  memmove(p.songFol + at + 1, p.songFol + at, k * sizeof(p.songFol[0]));
  memmove(p.songTrn + at + 1, p.songTrn + at, k * sizeof(p.songTrn[0]));
  memmove(p.songBpm + at + 1, p.songBpm + at, k * sizeof(p.songBpm[0]));
  p.song[at] = pat < kPatterns ? pat : kPatterns - 1;
  p.songMute[at] = mute;
  p.songFol[at] = fol;
  p.songTrn[at] = songTrnClamp(trn);
  p.songBpm[at] = songBpmClamp(bpm);
  p.songLen = static_cast<uint8_t>(n + 1);
  return true;
}

void songDelete(Project& p, int at) {
  const int n = p.songLen;
  if (at < 0 || at >= n) return;
  for (int i = 0; i < n; ++i) {
    SongFollow& f = p.songFol[i];
    if (!isJump(f) || f.target < at) continue;
    if (f.target > at) --f.target;
    else if (at == n - 1) f.target = static_cast<uint8_t>(n >= 2 ? n - 2 : 0);
    // == at, not the last: the slot that moves into its place keeps the index
  }
  const size_t k = static_cast<size_t>(n - at - 1);
  memmove(p.song + at, p.song + at + 1, k);
  memmove(p.songMute + at, p.songMute + at + 1, k * sizeof(p.songMute[0]));
  memmove(p.songFol + at, p.songFol + at + 1, k * sizeof(p.songFol[0]));
  memmove(p.songTrn + at, p.songTrn + at + 1, k * sizeof(p.songTrn[0]));
  memmove(p.songBpm + at, p.songBpm + at + 1, k * sizeof(p.songBpm[0]));
  p.song[n - 1] = 0;
  p.songMute[n - 1] = 0;
  p.songFol[n - 1] = SongFollow();
  p.songTrn[n - 1] = 0;
  p.songBpm[n - 1] = 0;
  p.songLen = static_cast<uint8_t>(n - 1);
}

uint16_t patternTrackMask(const Project& p, int pat) {
  const Pattern& pt = p.patterns[pat];
  uint16_t m = 0;
  for (int t = 0; t < kTracks; ++t)
    for (int s = 0; s < pt.length; ++s)
      if (!pt.steps[t][s].isEmpty()) {
        m |= 1 << t;
        break;
      }
  return m;
}

void captureGen(Pattern& p, int tr, const GenCfg& c, const GenState& st, int fromStep, uint8_t root, ScaleType t) {
  if (tr < 0 || tr >= kTracks) return;
  const uint8_t n = p.trackLen[tr];
  const int len = n && n < p.length ? n : p.length;
  if (len <= 0) return;
  const bool gate = genGateMode(c.mode);
  GenState g = st;
  const int from = fromStep < 0 ? 0 : fromStep % len;
  for (int k = 0; k < len; ++k) {
    Step& s = p.steps[tr][(from + k) % len];
    if (!gate && !s.hasNote()) continue;
    genShift(g, c, tr, 0);
    if (gate && s.note == kNoteOff) continue;
    s.note = !gate || genGate(g, c) ? genNote(g, c, root, t) : kNoteEmpty;
  }
}

}  // namespace mt
