#include "voice_cost.h"
#include "hot.h"

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
}  // namespace

MT_HOT int voiceCost(const Instrument& m, bool sample) {
  // A sample voice runs m's filter too (controlFilter): the add-on applies (a KIT sampler lane
  // builds fltMode Off).
  int c = kCostChip;  // KIT (a lane is priced by its own instrument)
  switch (sample ? InstrType::Sample : m.type) {
    case InstrType::Sample: c = kCostSample; break;
    case InstrType::Fm: c = kFmTab[m.machine < kFmN ? m.machine : 0]; break;  // out of range plays KICK
    case InstrType::Drum: c = kDrumTab[m.machine < kDrumN ? m.machine : 0]; break;
    case InstrType::Synth: {
      const uint8_t wt = static_cast<uint8_t>(SynOsc::Wt);
      c = m.synOsc[0] == wt || m.synOsc[1] == wt ? kCostSynWt : kCostSynBl;
      break;
    }
    case InstrType::Lofi:
      c = m.machine == static_cast<uint8_t>(LofiMachine::Byte) ? kCostLofiByte : kCostLofiApc;
      break;
    case InstrType::Chip:  // the wave's loop (see renderChip)
      if (m.wave > static_cast<uint8_t>(Wave::Saw)) c += kCostChipGeneric;
      break;
    default: break;
  }
  if (m.fltMode != static_cast<uint8_t>(FltMode::Off) && m.fltMode < static_cast<uint8_t>(FltMode::Count))
    c += kCostFilter;
  return c;
}

}  // namespace mt
