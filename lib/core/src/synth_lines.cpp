#include "synth_lines.h"
#include "synth_voice.h"

namespace mt {

// 1 KB per voice: 32 KB on the Teensy (RAM2), 24 KB on the ESP and native. Data only (no code for the
// linker script's flash list).
#if defined(__IMXRT1062__)
// Not read before fmMachine initializes the entire record on a fresh note.
MT_RAM2 FmParams gFmVoiceParams[kVoices] __attribute__((aligned(32)));
static_assert(sizeof(FmEngine) <= sizeof(DrumEngine), "FM must not grow the RAM1 voice union");
#endif
MT_RAM2 int16_t gVoiceLines[kVoices][kLineLen] __attribute__((aligned(32)));

}  // namespace mt
