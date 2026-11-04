#include "synth_fm_machines.h"
#include <math.h>
#include "hot.h"
namespace mt {
MT_CONTROL float fmMixGain(uint8_t machine, float spacingRaw) {
  // Full-Synth starting-note audit against CHIP/SYNTH at identical faders.
  // Keep loud rolling CP/CY/Vintage unchanged; no signal-dependent normalization.
  switch (static_cast<FmMachine>(machine)) {
    case FmMachine::BdModern: return 1.5f;
    case FmMachine::SdBasic:
    case FmMachine::PcCarbon:
    case FmMachine::Chord: return 2.f;
    case FmMachine::Toy:
    case FmMachine::Tone:
    case FmMachine::HhBasic: return 4.f;
    case FmMachine::CpVintage: {
      // The compact centre has less energy than a rolling train. Smoothly trim
      // that region only; boosting the whole machine would overload the rolls.
      const float spread=fabsf(spacingRaw-64.f);
      return !isfinite(spread) || spread>=16.f ? 1.f : 2.f-spread*(1.f/16.f);
    }
    case FmMachine::SdVintage:
    case FmMachine::CyAlloy: return 1.f;
    default: return 4.f;  // same fallback as fmMachine
  }
}
MT_CONTROL void fmMachine(uint8_t machine, const float raw[kFmParams], float pitch, FmParams& out) {
  float clean[kFmParams];
  for (int i=0;i<kFmParams;++i) {
    const float x=raw[i];
    clean[i]=!isfinite(x)?64.f:x<0?0.f:x>127?127.f:x;
  }
  if (!isfinite(pitch)) pitch=60;
  if (pitch < -48) pitch=-48;
  if (pitch > 156) pitch=156;
  out=FmParams();
  if (!fmTonalMachine(machine,clean,pitch,out) && !fmPercussionMachine(machine,clean,pitch,out))
    fmTonalMachine(static_cast<uint8_t>(FmMachine::Tone),clean,pitch,out);
}
}
