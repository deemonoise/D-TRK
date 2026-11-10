#pragma once

// Reserve the live Synth state in internal RAM before other allocations. The sequencer lives
// in PSRAM for its lifetime, leaving internal RAM for the per-sample reverb lines. Wi-Fi moves
// only the Synth to PSRAM; audio moves its reverb lines separately. No interior pointers are freed.
namespace core_ram {
void reserve();   // once at boot, before audio::begin and engine::begin
bool internal();  // the managed Synth storage is internal (Seq residency: engine::seqInternal)
// Transport stopped. False: already external, no destination memory, or audio did not park.
bool toPsram();
// May retry after a boot PSRAM fallback or a previous failed restore. False: not moved.
bool toInternal();
}  // namespace core_ram
