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
      static_assert(static_cast<int>(DrumMachine::Count) - kDrum2First == 8 && kDrum2First == 16,
                    "voiceCost: a case per machine");
      switch (static_cast<DrumMachine>(m.machine)) {
        case DrumMachine::Bda:
        case DrumMachine::Unused22: c = kCostDrum2[0]; break;  // was BDF: plays as BDA
        case DrumMachine::Sda: c = kCostDrum2[1]; break;
        case DrumMachine::Bds: c = kCostDrum2[2]; break;
        case DrumMachine::Sds: c = kCostDrum2[3]; break;
        case DrumMachine::Cpx: c = kCostDrum2[4]; break;
        case DrumMachine::Rsx: c = kCostDrum2[5]; break;
        case DrumMachine::Hhr: c = kCostDrum2[7]; break;
        case DrumMachine::Bd8: c = kCostDrum[0]; break;
        case DrumMachine::Sd8: c = kCostDrum[1]; break;
        case DrumMachine::Tom8: c = kCostDrum[2]; break;
        case DrumMachine::Cp8: c = kCostDrum[3]; break;
        case DrumMachine::Rs8: c = kCostDrum[4]; break;
        case DrumMachine::Cl8: c = kCostDrum[5]; break;
        case DrumMachine::Cb8: c = kCostDrum[6]; break;
        case DrumMachine::Hh8: c = kCostDrum[7]; break;
        case DrumMachine::Cy8: c = kCostDrum[8]; break;
        case DrumMachine::Bd9: c = kCostDrum[9]; break;
        case DrumMachine::Sd9: c = kCostDrum[10]; break;
        case DrumMachine::Tom9: c = kCostDrum[11]; break;
        case DrumMachine::Cp9: c = kCostDrum[12]; break;
        case DrumMachine::Rs9: c = kCostDrum[13]; break;
        case DrumMachine::Hh9: c = kCostDrum[14]; break;
        default: c = kCostDrum[15]; break;  // CY9 (and out of range: the last DrumVoice one)
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
    case InstrType::Chip: {  // the wave's loop (see renderChip); STEP runs every wave in the generic one
      const uint8_t w = m.wave;
      if (w >= static_cast<uint8_t>(Wave::SidTs) && w <= static_cast<uint8_t>(Wave::SidPts)) c += kCostChipSid;
      else if (w == static_cast<uint8_t>(Wave::AyToneNoise)) c += kCostChipAyTn;
      else if (m.chipStep || w > static_cast<uint8_t>(Wave::Saw)) c += kCostChipGeneric;
      if (m.chipStep) c += kCostChipStep;
      break;
    }
    default: break;                                  // KIT
  }
  switch (static_cast<FltMode>(m.fltMode)) {
    case FltMode::Ladder: c += kCostLadder; break;
    case FltMode::Ms20: c += kCostMs20; break;
    case FltMode::Comb: c += m.type == InstrType::String && !sample ? 0 : kCostComb; break;  // STRING: Off
    case FltMode::Formant: c += kCostFormant; break;
    case FltMode::Sid: c += kCostSid; break;
    default: break;
  }
  return c;
}

}  // namespace mt
