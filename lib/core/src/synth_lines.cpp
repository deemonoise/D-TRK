#include "synth_lines.h"
#include "synth_voice.h"

namespace mt {

// 1 KB per voice: 32 KB on the Teensy (RAM2), 24 KB on the ESP and native. Data only (no code for the
// linker script's flash list).
MT_RAM2 int16_t gVoiceLines[kVoices][kLineLen] __attribute__((aligned(32)));

}  // namespace mt
