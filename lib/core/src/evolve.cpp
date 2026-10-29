#include "evolve.h"
#include <string.h>
#include "shift_gen.h"

namespace mt {

namespace {
bool swapAt(const EvoTrack& s, int i, int len) { return i >= 0 && i + 1 < len && (s.step[i].flags & kEvoSwapNext); }
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
int toward0(int v, int by) { return v > 0 ? (v > by ? v - by : 0) : (v < -by ? v + by : 0); }
// evoRand kinds.
enum : uint32_t {
  kSaltHomeDeg = 1, kSaltHomeVol, kSaltHomeFlag, kSaltHomeRot, kSaltNote, kSaltRhythm, kSaltRtrg, kSaltVol,
  kSaltShift
};
}  // namespace

int evoLen(const Pattern& pt, int tr) {
  if (tr < 0 || tr >= kTracks) return 0;
  const uint8_t n = pt.trackLen[tr];
  const int len = n && n < pt.length ? n : pt.length;
  return len > kMaxSteps ? kMaxSteps : len;
}

void evoReset(EvoTrack& s) {
  s.n = 0;
  evoClearDeltas(s);
}

void evoClearDeltas(EvoTrack& s) {
  s.rot = 0;
  memset(s.step, 0, sizeof(s.step));
}

bool evoIdle(const EvoTrack& s) { return s.rot == 0 && evoChanged(s, kMaxSteps) == 0; }

int evoChanged(const EvoTrack& s, int len) {
  if (len > kMaxSteps) len = kMaxSteps;
  int k = 0;
  for (int i = 0; i < len; ++i) {
    const EvoStep& d = s.step[i];
    if (d.deg || d.vol || d.flags) ++k;
  }
  return k;
}

uint32_t evoRand(uint32_t seed, int track, uint32_t n, int i, uint32_t salt) {
  return genHash(seed ^ (salt * 0x9E3779B1u), (static_cast<uint32_t>(track) & 0xFFu) | (static_cast<uint32_t>(i) << 8),
                 n);
}

int evoSource(const EvoTrack& s, int i, int len, uint8_t mask) {
  if (!(mask & kEvoShift) || len < 2 || i < 0 || i >= len) return i;
  int j = i;
  if (swapAt(s, i, len)) j = i + 1;
  else if (swapAt(s, i - 1, len)) j = i - 1;
  const int r = s.rot % len;
  return ((j - r) % len + len) % len;
}

void evoMakeView(const Pattern& pt, int tr, const EvoTrack& s, uint8_t mask, bool noteOk, ScaleType t,
                 EvoView& v) {
  const int len = evoLen(pt, tr);
  v.len = static_cast<uint8_t>(len);
  v.degs = static_cast<uint8_t>(scaleDegrees(t));
  v.noteOk = noteOk;
  for (int i = 0; i < len; ++i) {
    const Step& st = pt.steps[tr][evoSource(s, i, len, mask)];
    v.kind[i] = st.hasNote() ? kEvoKNote : (st.note == kNoteEmpty ? kEvoKEmpty : kEvoKOther);
  }
}

bool evoRhythmOk(const Project& p, int tr) {
  return !(p.genOn(tr) && genGateMode(p.gen[tr].mode));
}

EvoApply evoApplyOf(const Project& p, int tr) {
  EvoApply a;
  if (tr < 0 || tr >= kTracks) return a;
  a.mask = static_cast<uint8_t>(p.evo[tr].mask & kEvoMaskAll);
  if (!evoRhythmOk(p, tr)) a.mask = static_cast<uint8_t>(a.mask & ~kEvoRhythm);
  a.drum = p.trackIsDrum(tr);
  a.noteOk = !a.drum && !p.genOn(tr);
  a.defVel = p.tracks[tr].defVel;
  a.root = p.scaleRoot;
  a.scale = static_cast<ScaleType>(p.scaleType);
  return a;
}

const Step& evoStep(const Pattern& pt, int tr, int idx, const EvoTrack& s, const EvoApply& a, Step& tmp) {
  const int len = evoLen(pt, tr);
  if (!(a.mask & kEvoMaskAll) || idx < 0 || idx >= len) return pt.steps[tr][idx < 0 ? 0 : idx];
  const int src = evoSource(s, idx, len, a.mask);
  const Step& base = pt.steps[tr][src];
  const EvoStep& d = s.step[idx];
  if (src == idx && !d.deg && !d.vol && !(d.flags & ~kEvoSwapNext)) return base;
  tmp = base;
  if (a.mask & kEvoRhythm) {
    if ((d.flags & kEvoDrop) && tmp.hasNote()) {
      clearNote(tmp);
    } else if ((d.flags & kEvoGhost) && tmp.note == kNoteEmpty && len > 1) {
      const int j = (d.flags & kEvoGhostNext) ? (idx + 1) % len : (idx + len - 1) % len;
      const Step& nb = pt.steps[tr][evoSource(s, j, len, a.mask)];
      if (nb.hasNote()) {
        const uint8_t gv = static_cast<uint8_t>(clampi(a.defVel * kEvoGhostPct / 100, 1, 127));
        if (a.drum) {
          tmp.note = gv;
          setLaneMask(tmp, laneMask(nb));
        } else {
          tmp.note = nb.note;
          tmp.vel = gv;
        }
      }
    }
  }
  if ((a.mask & kEvoNote) && a.noteOk && !a.drum && d.deg && tmp.hasNote())
    tmp.note = static_cast<uint8_t>(moveDegrees(tmp.note, d.deg, a.root, a.scale));
  if ((a.mask & kEvoRtrgVol) && tmp.hasNote()) {
    if (d.vol) {
      uint8_t& f = a.drum ? tmp.note : tmp.vel;
      f = static_cast<uint8_t>(clampi((f ? f : a.defVel) + d.vol, 1, 127));
    }
    const int x = (d.flags & kEvoRtrgBits) >> kEvoRtrgShift;
    if (x && !tmp.find(Fx::RAT))
      for (FxSlot& f : tmp.fx)
        if (f.cmd == Fx::None) {
          f = {Fx::RAT, static_cast<uint8_t>(x + 1)};
          break;
        }
  }
  return tmp;
}

void evoMutate(EvoTrack& s, const EvoCfg& cfg, const EvoView& v, uint32_t seed, int track) {
  EvoCfg c = cfg;
  evoClamp(c);
  const uint32_t n = ++s.n;
  const int len = v.len > kMaxSteps ? kMaxSteps : v.len;
  const uint32_t home = c.home, pm = c.amount * 5u / 2u;  // per mille per step and kind: 250 at 100 %
  auto roll = [&](int i, uint32_t salt) { return evoRand(seed, track, n, i, salt); };
  for (int i = 0; i < len; ++i) {
    EvoStep& d = s.step[i];
    if (d.deg && roll(i, kSaltHomeDeg) % 100u < home) d.deg = static_cast<int8_t>(toward0(d.deg, 1));
    if (d.vol && roll(i, kSaltHomeVol) % 100u < home) d.vol = static_cast<int8_t>(toward0(d.vol, kEvoVolStep));
    if (d.flags && roll(i, kSaltHomeFlag) % 100u < home) d.flags = 0;
  }
  if (s.rot && roll(kMaxSteps, kSaltHomeRot) % 100u < home) s.rot = static_cast<int8_t>(toward0(s.rot, 1));
  if (!pm) return;
  for (int i = 0; i < len; ++i) {
    EvoStep& d = s.step[i];
    const uint8_t k = v.kind[i];
    if ((c.mask & kEvoNote) && v.noteOk && k == kEvoKNote) {
      const uint32_t h = roll(i, kSaltNote);
      if (h % 1000u < pm) {
        const uint32_t b = h / 1000u;
        int dd = b % 8u == 0 ? v.degs : (b / 8u % 2u ? 2 : 1);  // an octave one time in 8
        if (b / 16u % 2u) dd = -dd;
        d.deg = static_cast<int8_t>(clampi(d.deg + dd, -kEvoDegMax, kEvoDegMax));
      }
    }
    if (c.mask & kEvoRhythm) {
      const uint32_t h = roll(i, kSaltRhythm);
      if (h % 1000u < pm / 2) {
        if (k == kEvoKNote) {
          d.flags = static_cast<uint8_t>((d.flags & ~(kEvoGhost | kEvoGhostNext)) | kEvoDrop);
        } else if (k == kEvoKEmpty && len > 1) {
          const bool prev = v.kind[(i + len - 1) % len] == kEvoKNote;
          const bool next = v.kind[(i + 1) % len] == kEvoKNote;
          if (prev || next) {
            const bool useNext = next && (!prev || (h / 1000u) % 2u);
            d.flags = static_cast<uint8_t>((d.flags & ~(kEvoDrop | kEvoGhostNext)) | kEvoGhost |
                                           (useNext ? kEvoGhostNext : 0));
          }
        }
      }
    }
    if ((c.mask & kEvoRtrgVol) && k == kEvoKNote) {
      const uint32_t r = roll(i, kSaltRtrg);
      if (r % 1000u < pm / 2) {
        const uint8_t x = (r / 1000u) % 3u == 0 ? 2 : 1;  // x3 one time in three, else x2
        d.flags = static_cast<uint8_t>((d.flags & ~kEvoRtrgBits) | (x << kEvoRtrgShift));
      }
      const uint32_t w = roll(i, kSaltVol);
      if (w % 1000u < pm) {
        int dv = kEvoVolStep * (1 + static_cast<int>((w / 1000u) % 3u));
        if ((w / 3000u) % 2u) dv = -dv;
        d.vol = static_cast<int8_t>(clampi(d.vol + dv, -kEvoVolMax, kEvoVolMax));
      }
    }
  }
  if ((c.mask & kEvoShift) && len >= 2) {
    const uint32_t h = roll(kMaxSteps, kSaltShift);
    if (h % 1000u < pm * 2) {
      const uint32_t b = h / 1000u;
      if (b % 2u) {
        const int r = s.rot + (b / 2u % 2u ? 1 : -1);
        if (r >= -kEvoRotMax && r <= kEvoRotMax && r > -len && r < len) s.rot = static_cast<int8_t>(r);
      } else {
        const int i = static_cast<int>((b / 2u) % static_cast<uint32_t>(len - 1));
        if (!swapAt(s, i - 1, len) && !swapAt(s, i, len) && !swapAt(s, i + 1, len))
          s.step[i].flags = static_cast<uint8_t>(s.step[i].flags | kEvoSwapNext);
      }
    }
  }
}

void evoClamp(EvoCfg& c) {
  if (c.amount > 100) c.amount = 100;
  if (c.home > 100) c.home = 100;
  if (c.every < 1) c.every = 1;
  if (c.every > kEvoEveryMax) c.every = kEvoEveryMax;
  c.mask &= kEvoMaskAll;
}

}  // namespace mt
