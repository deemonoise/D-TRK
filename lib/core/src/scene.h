#pragma once
#include <stdint.h>
#include "hot.h"
#include "model.h"

// Scenes and the crossfader (docs/plans/2026-10-15-scenes-morph-design.md). Types in model.h (Scene,
// ScnTrk, ScnMst). A scene holds only the parameters set in it; the crossfader xfade 0..127 morphs from
// scene A (Project::sceneA, live copy SynthModel::sceneLive[0]) to scene B (sceneLive[1]). Per parameter
// with the project's value `base`: a = A set ? A : base, b = B set ? B : base; continuous: round(a +
// (b - a) * xf / 127), discrete (DTY, DLY time): xf < 64 ? a : b. Unset at both ends: base, bit-exact.
// p-locks replace the morphed value, LFO / follower add on top of it (Synth).

namespace mt {

// The morph of one parameter, xf 0..127 (the synth's smoothed position, or xfade).
MT_INLINE uint8_t sceneMix(uint8_t base, bool setA, uint8_t a, bool setB, uint8_t b, float xf, bool discrete) {
  const int va = setA ? a : base, vb = setB ? b : base;
  if (discrete) return static_cast<uint8_t>(xf >= 64.f ? vb : va);
  if (va == vb) return static_cast<uint8_t>(va);
  return static_cast<uint8_t>(static_cast<int>(static_cast<float>(va) + (vb - va) * xf * (1.f / 127.f) + 0.5f));
}

constexpr bool sceneTrkDiscrete(int p) { return p == kScnDty; }
constexpr bool sceneMstDiscrete(int p) { return p == kScnDTime; }
constexpr uint16_t kScnTrkBits = (1u << kScnTrk) - 1, kScnMstBits = (1u << kScnMst) - 1;

const char* sceneTrkName(int p);  // "VOL" .. "RES", "?" out of range
const char* sceneMstName(int p);  // "MAIN" .. "EQHI"
int sceneTrkMin(int p);
int sceneTrkMax(int p);  // DTY: the last DriveModel, else 127
int sceneMstMin(int p);  // DLY time: 1
int sceneMstMax(int p);  // MAIN: kMasterVolMax, DLY time: kDlyTimeMax
uint8_t sceneClampTrk(int p, int v);  // DTY out of range -> TNH (0), else into min..max
uint8_t sceneClampMst(int p, int v);

// The project's value of a parameter (track t's instrument for DLY / RVB / CUT / RES).
uint8_t sceneTrkBase(const SynthModel& m, int t, int p);
uint8_t sceneMstBase(const SynthModel& m, int p);
// What sounds now (sceneLive and xfade, unsmoothed): the MIX view.
uint8_t sceneTrkEff(const SynthModel& m, int t, int p);
uint8_t sceneMstEff(const SynthModel& m, int p);
bool sceneTrkLive(const SynthModel& m, int t, int p);  // scene A or B holds it
bool sceneMstLive(const SynthModel& m, int p);

inline bool sceneTrkIsSet(const Scene& s, int t, int p) { return (s.trkMask[t] >> p) & 1u; }
inline bool sceneMstIsSet(const Scene& s, int p) { return (s.mstMask >> p) & 1u; }
void sceneSetTrk(Scene& s, int t, int p, int v);  // clamped
void sceneUnsetTrk(Scene& s, int t, int p);
void sceneSetMst(Scene& s, int p, int v);
void sceneUnsetMst(Scene& s, int p);
bool sceneEmpty(const Scene& s);  // no known parameter set

// Copies scenes[sceneA / sceneB] into sceneLive (indices clamped). After every scene edit, A / B change
// and project load.
void syncSceneLive(Project& p);
void sceneClear(Project& p, int s);       // + sync
void sceneCaptureMix(Project& p, int s);  // VOL + PAN of every INT track from the base, + sync

}  // namespace mt
