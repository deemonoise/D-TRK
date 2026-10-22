#include "scene.h"

#include <atomic>

namespace mt {

namespace {

const char* const kTrkNames[kScnTrk] = {"VOL", "PAN", "DRV", "DTY", "BIT", "SRR",
                                        "GTE", "DLY", "RVB", "MOD", "CUT", "RES"};
const char* const kMstNames[kScnMst] = {"MAIN", "DTIM", "DFB", "DTON", "DCHR", "DLVL", "RSIZ",
                                        "RDMP", "RLVL", "MRAT", "MDEP", "MLVL", "EQLO", "EQHI"};

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

const Scene* live(const SynthModel& m, int end) { return &m.sceneLive[end]; }

}  // namespace

const char* sceneTrkName(int p) { return p >= 0 && p < kScnTrk ? kTrkNames[p] : "?"; }
const char* sceneMstName(int p) { return p >= 0 && p < kScnMst ? kMstNames[p] : "?"; }
int sceneTrkMin(int) { return 0; }
int sceneTrkMax(int p) { return p == kScnDty ? static_cast<int>(DriveModel::Count) - 1 : 127; }
int sceneMstMin(int p) { return p == kScnDTime ? 1 : 0; }
int sceneMstMax(int p) { return p == kScnMain ? kMasterVolMax : (p == kScnDTime ? kDlyTimeMax : 127); }

uint8_t sceneClampTrk(int p, int v) {
  if (p == kScnDty) return static_cast<uint8_t>(v >= 0 && v < static_cast<int>(DriveModel::Count) ? v : 0);
  return static_cast<uint8_t>(clampi(v, sceneTrkMin(p), sceneTrkMax(p)));
}

uint8_t sceneClampMst(int p, int v) { return static_cast<uint8_t>(clampi(v, sceneMstMin(p), sceneMstMax(p))); }

uint8_t sceneTrkBase(const SynthModel& m, int t, int p) {
  const TrackCfg& c = m.tracks[t];
  const Instrument& in = m.instruments[c.instr % kInstruments];
  switch (p) {
    case kScnVol: return c.vol;
    case kScnPan: return c.pan;
    case kScnDrv: return c.drive;
    case kScnDty: return c.driveModel;
    case kScnBit: return c.crushBits;
    case kScnSrr: return c.crushRate;
    case kScnGte: return c.gateDepth;
    case kScnDly: return in.send;
    case kScnRvb: return in.rsend;
    case kScnMod: return c.modSend;
    case kScnCut: return in.cutoff;
    case kScnRes: return in.reso;
    default: return 0;
  }
}

uint8_t sceneMstBase(const SynthModel& m, int p) {
  switch (p) {
    case kScnMain: return m.masterVol;
    case kScnDTime: return m.dlyTime;
    case kScnDFb: return m.dlyFb;
    case kScnDTone: return m.dlyTone;
    case kScnDChar: return m.dlyChar;
    case kScnDLvl: return m.dlyLevel;
    case kScnRSize: return m.rvbSize;
    case kScnRDamp: return m.rvbDamp;
    case kScnRLvl: return m.rvbLevel;
    case kScnMRate: return m.modRate;
    case kScnMDepth: return m.modDepth;
    case kScnMLvl: return m.modLevel;
    case kScnEqLo: return m.eqLow;
    case kScnEqHi: return m.eqHigh;
    default: return 0;
  }
}

uint8_t sceneTrkEff(const SynthModel& m, int t, int p) {
  const Scene &a = *live(m, 0), &b = *live(m, 1);
  return sceneMix(sceneTrkBase(m, t, p), sceneTrkIsSet(a, t, p), a.trk[t][p], sceneTrkIsSet(b, t, p), b.trk[t][p],
                  m.xfade, sceneTrkDiscrete(p));
}

uint8_t sceneMstEff(const SynthModel& m, int p) {
  const Scene &a = *live(m, 0), &b = *live(m, 1);
  return sceneMix(sceneMstBase(m, p), sceneMstIsSet(a, p), a.mst[p], sceneMstIsSet(b, p), b.mst[p], m.xfade,
                  sceneMstDiscrete(p));
}

bool sceneTrkLive(const SynthModel& m, int t, int p) {
  return p >= 0 && p < kScnTrk && (sceneTrkIsSet(*live(m, 0), t, p) || sceneTrkIsSet(*live(m, 1), t, p));
}

bool sceneMstLive(const SynthModel& m, int p) {
  return p >= 0 && p < kScnMst && (sceneMstIsSet(*live(m, 0), p) || sceneMstIsSet(*live(m, 1), p));
}

void sceneSetTrk(Scene& s, int t, int p, int v) {
  if (t < 0 || t >= kTracks || p < 0 || p >= kScnTrk) return;
  s.trk[t][p] = sceneClampTrk(p, p == kScnDty ? clampi(v, 0, sceneTrkMax(p)) : v);
  s.trkMask[t] |= static_cast<uint16_t>(1u << p);
}

void sceneUnsetTrk(Scene& s, int t, int p) {
  if (t < 0 || t >= kTracks || p < 0 || p >= kScnTrk) return;
  s.trkMask[t] &= static_cast<uint16_t>(~(1u << p));
  s.trk[t][p] = 0;
}

void sceneSetMst(Scene& s, int p, int v) {
  if (p < 0 || p >= kScnMst) return;
  s.mst[p] = sceneClampMst(p, v);
  s.mstMask |= static_cast<uint16_t>(1u << p);
}

void sceneUnsetMst(Scene& s, int p) {
  if (p < 0 || p >= kScnMst) return;
  s.mstMask &= static_cast<uint16_t>(~(1u << p));
  s.mst[p] = 0;
}

bool sceneEmpty(const Scene& s) {
  if (s.mstMask & kScnMstBits) return false;
  for (uint16_t mk : s.trkMask)
    if (mk & kScnTrkBits) return false;
  return true;
}

void syncSceneLive(Project& p) {
  if (p.sceneA >= kScenes) p.sceneA = 0;
  if (p.sceneB >= kScenes) p.sceneB = 1;
  // The audio task reads sceneLive without the lock: a set bit must never meet an old value. Drop the
  // bits that change first, publish the values, then raise the new bits.
  const Scene* src[2] = {&p.scenes[p.sceneA], &p.scenes[p.sceneB]};
  for (int e = 0; e < 2; ++e) {
    Scene& d = p.sceneLive[e];
    for (int t = 0; t < kTracks; ++t) d.trkMask[t] &= src[e]->trkMask[t];
    d.mstMask &= src[e]->mstMask;
  }
  std::atomic_thread_fence(std::memory_order_release);
  for (int e = 0; e < 2; ++e) {
    for (int t = 0; t < kTracks; ++t)
      for (int k = 0; k < kScnTrk; ++k) p.sceneLive[e].trk[t][k] = src[e]->trk[t][k];
    for (int k = 0; k < kScnMst; ++k) p.sceneLive[e].mst[k] = src[e]->mst[k];
  }
  std::atomic_thread_fence(std::memory_order_release);
  for (int e = 0; e < 2; ++e) {
    for (int t = 0; t < kTracks; ++t) p.sceneLive[e].trkMask[t] = src[e]->trkMask[t];
    p.sceneLive[e].mstMask = src[e]->mstMask;
  }
}

void sceneClear(Project& p, int s) {
  if (s < 0 || s >= kScenes) return;
  p.scenes[s] = Scene();
  syncSceneLive(p);
}

void sceneCaptureMix(Project& p, int s) {
  if (s < 0 || s >= kScenes) return;
  for (int t = 0; t < kTracks; ++t) {
    if (!p.trackInternal(t)) continue;
    sceneSetTrk(p.scenes[s], t, kScnVol, p.tracks[t].vol);
    sceneSetTrk(p.scenes[s], t, kScnPan, p.tracks[t].pan);
  }
  syncSceneLive(p);
}

}  // namespace mt
