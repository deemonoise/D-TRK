#include "voice_cost.h"
#include "bytebeat_lib.h"
#include "synth_lofi.h"
#include "synth_osc.h"

namespace mt {

namespace {
MT_CONTROL int byteCost(const Instrument& m, float hz) {
  // Upper buckets of 1/16 evaluation/sample: small bends do not jitter the price.
  // BYTE evaluates at most once per output sample even when its counter skips values.
  const float scaled = hz * (kByteRatePerHz * (16.f / kSynthRate));
  unsigned bucket = 16;
  if (scaled > 0.f && scaled < 16.f) {
    bucket = static_cast<unsigned>(scaled);
    if (static_cast<float>(bucket) < scaled) ++bucket;
  }
  const unsigned work = byteWorkScore(m.lofiFormula) * kCostByteWork * bucket;
  return kCostByteBase + static_cast<int>((work + 15) / 16);
}

MT_CONTROL int ayToneNoiseCost(float hz) {
  if (!(hz > 0.f)) return kCostChipAyTnExtreme;
  const float clocks = hz * (8.f / kSynthRate);
  if (clocks <= 1.f) return kCostChipAyTn;
  if (clocks <= 16.f) return kCostChipAyTnHigh;
  return kCostChipAyTnExtreme;
}

MT_CONTROL uint8_t configuredPotential(const Instrument& m) {
  uint8_t flags = 0;
  if (m.synEnvDest == static_cast<uint8_t>(SynEnvDest::Fold)) flags |= kCostPotentialFold;
  if (m.synEnvDest == static_cast<uint8_t>(SynEnvDest::Ring)) flags |= kCostPotentialRing;
  for (int i = 0; i < kLfos; ++i) {
    const uint8_t dest = lfoAt(m, i).dest;
    if (dest == static_cast<uint8_t>(LfoDest::Fold)) flags |= kCostPotentialFold;
    if (dest == static_cast<uint8_t>(LfoDest::Ring)) flags |= kCostPotentialRing;
    if (dest == static_cast<uint8_t>(LfoDest::Sat)) flags |= kCostPotentialSat;
  }
  return flags;
}
}

MT_CONTROL int voiceCost(const Instrument& m, bool sample, uint8_t potential, float effectiveHz,
                         InstrType actualType, uint8_t actualMachine) {
  const InstrType type = sample ? InstrType::Sample : actualType == InstrType::Count ? m.type : actualType;
  const uint8_t machine = actualMachine == 255 ? m.machine : actualMachine;
  if (type == InstrType::Synth || type == InstrType::Fm)
    potential |= configuredPotential(m);
  // A sample voice runs m's filter too (controlFilter): the add-ons below apply (a KIT sampler
  // lane builds fltMode Off: the base).
  int c = kCostChip;
  switch (type) {
    case InstrType::Sample: c = kCostSample; break;
    case InstrType::Fm: c = kCostFm + ((m.fmSat || (potential & kCostPotentialSat)) ? kCostFmSat : 0); break;
    case InstrType::Drum:  // constant indices, no .rodata table (RAM1): see STRING below
      static_assert(static_cast<int>(DrumMachine::Count) - kDrum2First == 8 && kDrum2First == 16,
                    "voiceCost: a case per machine");
      switch (static_cast<DrumMachine>(machine)) {
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
      if (m.synFold || (potential & kCostPotentialFold)) c += kCostFold;
      if (m.synRing || (potential & kCostPotentialRing)) c += kCostRing;
      break;
    }
    case InstrType::String:  // constant indices: a table in .rodata would cost RAM1 (DTCM alignment)
      static_assert(static_cast<int>(StringModel::Count) == 2, "voiceCost: a branch per STRING model");
      c = machine == static_cast<uint8_t>(StringModel::String) ? kCostString[0] : kCostString[1];
      break;
    case InstrType::Lofi:
      c = machine == static_cast<uint8_t>(LofiMachine::Byte) ? byteCost(m, effectiveHz) : kCostLofiApc;
      break;
    case InstrType::Chip: {  // the wave's loop (see renderChip); STEP runs every wave in the generic one
      const uint8_t w = m.wave;
      if (w >= static_cast<uint8_t>(Wave::SidTs) && w <= static_cast<uint8_t>(Wave::SidPts)) c += kCostChipSid;
      else if (w == static_cast<uint8_t>(Wave::AyToneNoise)) c += ayToneNoiseCost(effectiveHz);
      else if (m.chipStep || w > static_cast<uint8_t>(Wave::Saw)) c += kCostChipGeneric;
      if (m.chipStep) c += kCostChipStep;
      break;
    }
    default: break;                                  // KIT
  }
  switch (static_cast<FltMode>(m.fltMode)) {
    case FltMode::Ladder: c += kCostLadder; break;
    case FltMode::Ms20: c += kCostMs20; break;
    case FltMode::Comb: c += type == InstrType::String ? 0 : kCostComb; break;  // STRING: Off
    case FltMode::Formant: c += kCostFormant; break;
    case FltMode::Sid: c += kCostSid; break;
    default: break;
  }
  return c;
}

}  // namespace mt
