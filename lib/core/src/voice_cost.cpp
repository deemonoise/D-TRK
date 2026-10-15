#include "voice_cost.h"

namespace mt {

int voiceCost(const Instrument& m, bool sample) {
  // A sample voice runs m's filter too (controlFilter): the add-ons below apply (a KIT sampler
  // lane builds fltMode Off: the base).
  int c = kCostChip;
  switch (sample ? InstrType::Sample : m.type) {
    case InstrType::Sample: c = kCostSample; break;
    case InstrType::Fm: c = kCostFm; break;
    case InstrType::Drum: c = kCostDrum; break;
    case InstrType::Synth: {
      const uint8_t wt = static_cast<uint8_t>(SynOsc::Wt);
      c = m.synOsc[0] == wt || m.synOsc[1] == wt ? kCostSynWt : kCostSynBl;
      if (m.synFold) c += kCostFold;
      if (m.synRing) c += kCostRing;
      break;
    }
    case InstrType::Macro: {
      const int n = static_cast<int>(MacroModel::Count);
      c = kCostMacro[m.machine < n ? m.machine : n - 1];
      break;
    }
    case InstrType::String:  // constant indices: a table in .rodata would cost RAM1 (DTCM alignment)
      static_assert(static_cast<int>(StringModel::Count) == 2, "voiceCost: a branch per STRING model");
      c = m.machine == static_cast<uint8_t>(StringModel::String) ? kCostString[0] : kCostString[1];
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
