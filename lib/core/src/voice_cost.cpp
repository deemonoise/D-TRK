#include "voice_cost.h"

namespace mt {

int voiceCost(const Instrument& m, bool sample) {
  // A sample voice runs m's filter too (controlFilter): the add-ons below apply (a KIT sampler
  // lane builds fltMode Off: the base).
  int c = kCostChip;
  switch (sample ? InstrType::Sample : m.type) {
    case InstrType::Sample: c = kCostSample; break;
    case InstrType::Fm: c = kCostFm; break;
    case InstrType::Drum:  // constant indices, no .rodata table (RAM1): see STRING below
      static_assert(static_cast<int>(DrumMachine::Count) - kDrum2First == 8, "voiceCost: a case per machine");
      switch (static_cast<DrumMachine>(m.machine)) {
        case DrumMachine::Bda:
        case DrumMachine::Unused22: c = kCostDrum2[0]; break;  // was BDF: plays as BDA
        case DrumMachine::Sda: c = kCostDrum2[1]; break;
        case DrumMachine::Bds: c = kCostDrum2[2]; break;
        case DrumMachine::Sds: c = kCostDrum2[3]; break;
        case DrumMachine::Cpx: c = kCostDrum2[4]; break;
        case DrumMachine::Rsx: c = kCostDrum2[5]; break;
        case DrumMachine::Hhr: c = kCostDrum2[7]; break;
        default: c = kCostDrum; break;
      }
      break;
    case InstrType::Synth: {
      const uint8_t wt = static_cast<uint8_t>(SynOsc::Wt);
      c = m.synOsc[0] == wt || m.synOsc[1] == wt ? kCostSynWt : kCostSynBl;
      if (m.synFold) c += kCostFold;
      if (m.synRing) c += kCostRing;
      break;
    }
    case InstrType::String:  // constant indices: a table in .rodata would cost RAM1 (DTCM alignment)
      static_assert(static_cast<int>(StringModel::Count) == 2, "voiceCost: a branch per STRING model");
      c = m.machine == static_cast<uint8_t>(StringModel::String) ? kCostString[0] : kCostString[1];
      break;
    case InstrType::Lofi:
      c = m.machine == static_cast<uint8_t>(LofiMachine::Byte) ? kCostLofiByte : kCostLofiApc;
      break;
    default: break;                                  // CHIP, KIT
  }
  switch (static_cast<FltMode>(m.fltMode)) {
    case FltMode::Ladder: c += kCostLadder; break;
    case FltMode::Ms20: c += kCostMs20; break;
    case FltMode::Comb: c += m.type == InstrType::String && !sample ? 0 : kCostComb; break;  // STRING: Off
    case FltMode::Formant: c += kCostFormant; break;
    default: break;
  }
  return c;
}

}  // namespace mt
