#pragma once

// The sequencer's block of internal RAM (~34 KB), taken first thing at boot (the synth is on the
// synth board). Wi-Fi needs that RAM: the Wi-Fi dialog moves the sequencer into PSRAM and frees the
// block; on close it comes back into a new block.
namespace core_ram {

void reserve();   // before anything else allocates; hands the block to the engine
bool internal();  // the sequencer lives in the internal block
// Transport stopped. False: not moved (no PSRAM).
bool toPsram();
// False: no internal block that big is free; the sequencer stays in PSRAM (slower).
bool toInternal();

}  // namespace core_ram
