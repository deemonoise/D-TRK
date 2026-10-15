#pragma once
#include <stdint.h>
#include "hot.h"

namespace mt {

// Per-voice delay lines: the COMB filter's (and STRING's later), one per Synth voice, indexed by the
// voice's slot (no allocation). On the Teensy in RAM2 (MT_RAM2): not zeroed at boot, so a user clears
// its line before the first read (VoiceFilter does on reset).
// One global pool, not a Synth member: only one Synth may render COMB voices per process (the ESP
// and the Teensy run one; native tests that keep two Synths alive must not play COMB on both).
// renderVoice indexes it by &v - voices_, in range by construction (voices_ is Voice[kVoices]).
constexpr int kLineLen = 512;  // a power of two (index masks); COMB's lowest pitch 44100 / 511 = 86 Hz
static_assert((kLineLen & (kLineLen - 1)) == 0, "kLineLen must be a power of two");

extern int16_t gVoiceLines[][kLineLen];  // [kVoices], synth_lines.cpp
MT_INLINE int16_t* voiceLine(int voice) { return gVoiceLines[voice]; }

}  // namespace mt
