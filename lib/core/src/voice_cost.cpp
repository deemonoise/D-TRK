#include "voice_cost.h"
#include "hot.h"
#include "mod_matrix.h"

namespace mt {

namespace {
// Internal RAM copies: read on every note-on (a constexpr table indexed at run time sits in flash).
MT_DATA const int16_t kFmTab[] = {kCostFm[0], kCostFm[1], kCostFm[2], kCostFm[3],
                                  kCostFm[4], kCostFm[5], kCostFm[6], kCostFm[7]};
MT_DATA const int16_t kDrumTab[] = {kCostDrum[0],  kCostDrum[1],  kCostDrum[2],  kCostDrum[3],
                                    kCostDrum[4],  kCostDrum[5],  kCostDrum[6],  kCostDrum[7],
                                    kCostDrum[8],  kCostDrum[9],  kCostDrum[10], kCostDrum[11],
                                    kCostDrum[12], kCostDrum[13], kCostDrum[14], kCostDrum[15]};
static_assert(sizeof(kFmTab) == sizeof(kCostFm) / 2 && sizeof(kDrumTab) == sizeof(kCostDrum) / 2,
              "voiceCost tables mirror kCostFm / kCostDrum");
constexpr int kFmN = static_cast<int>(FmMachine::Count), kDrumN = static_cast<int>(DrumMachine::Count);

MT_HOT int matrixCost(const Instrument& m) {
  int routes = 0;
  uint16_t sources = 0;
  bool dynamic = false;
  for (const auto& r : m.mod) {
    // Match modSourceMask/modEvaluate: source work still runs when a sounding engine
    // does not consume this destination (for example after a live type change).
    if (!r.source || r.source >= kModSources || !modDestValid(r.dest)) continue;
    // Zero depth can become nonzero through M01..M08, so reserve it too.
    ++routes;
    sources |= 1u << r.source;
    if (r.via && r.via < kModSources) sources |= 1u << r.via;
    const auto d = static_cast<LfoDest>(r.dest);
    dynamic |= d == LfoDest::Pitch || d == LfoDest::Fine || d == LfoDest::Cutoff || d == LfoDest::Reso ||
               d == LfoDest::Drive || d == LfoDest::Sat || (d >= LfoDest::Dec && d <= LfoDest::Con);
  }
  if (!routes) return 0;
  int cost = kCostMatrixBase + routes * kCostMatrixRoute + (dynamic ? kCostMatrixDynamic : 0);
  for (int i = int(ModSource::Lfo1); i <= int(ModSource::Lfo4); ++i)
    if (sources & (1u << i)) cost += kCostMatrixLfo;
  constexpr uint16_t extra = (1u << int(ModSource::FilterEnv)) | (1u << int(ModSource::Random)) | (1u << int(ModSource::Follower));
  for (int i = int(ModSource::FilterEnv); i < kModSources; ++i)
    if (sources & extra & (1u << i)) cost += kCostMatrixSource;
  return cost;
}
}  // namespace

MT_HOT int voiceCost(const Instrument& m, bool sample, int typeOverride, int machineOverride) {
  const uint8_t machine = machineOverride >= 0 ? static_cast<uint8_t>(machineOverride) : m.machine;
  const InstrType type = typeOverride >= 0 ? static_cast<InstrType>(typeOverride) : m.type;
  // A sample voice runs m's filter too (controlFilter): the add-on applies (a KIT sampler lane
  // builds fltMode Off).
  int c = kCostChip;  // KIT (a lane is priced by its own instrument)
  switch (sample ? InstrType::Sample : type) {
    case InstrType::Sample: c = kCostSample; break;
    case InstrType::Fm: c = kFmTab[machine < kFmN ? machine : 0]; break;  // out of range plays KICK
    case InstrType::Drum: c = kDrumTab[machine < kDrumN ? machine : 0]; break;
    case InstrType::Synth: {
      const uint8_t wt = static_cast<uint8_t>(SynOsc::Wt);
      c = m.synOsc[0] == wt || m.synOsc[1] == wt ? kCostSynWt : kCostSynBl;
      break;
    }
    case InstrType::Lofi:
      c = machine == static_cast<uint8_t>(LofiMachine::Byte) ? kCostLofiByte : kCostLofiApc;
      break;
    case InstrType::Chip:  // the wave's loop (see renderChip)
      if (m.wave > static_cast<uint8_t>(Wave::Saw)) c += kCostChipGeneric;
      break;
    default: break;
  }
  if (m.fltMode != static_cast<uint8_t>(FltMode::Off) && m.fltMode < static_cast<uint8_t>(FltMode::Count))
    c += kCostFilter;
  return c + matrixCost(m);
}

}  // namespace mt
