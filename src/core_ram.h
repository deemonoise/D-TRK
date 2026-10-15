#pragma once

// The synth and the sequencer share one block of internal RAM (~75 KB), taken first thing at boot.
// Wi-Fi needs that RAM: the Wi-Fi dialog moves both into PSRAM and frees the block; on close both
// come back into one new block. One block, not two: the hole it leaves closes up again once Wi-Fi
// is off, while two separate blocks could split it so the second no longer fits.
namespace core_ram {

void reserve();   // before anything else allocates; hands the parts to audio and engine
bool internal();  // both live in the internal block
// Transport stopped. False: not moved (no PSRAM, or the audio task did not park).
bool toPsram();
// False: no internal block that big is free; both stay in PSRAM (the audio costs more).
bool toInternal();

}  // namespace core_ram
