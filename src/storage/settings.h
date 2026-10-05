#pragma once
#include <stdint.h>

namespace storage {

// Device settings in NVS (survive power-off, independent of projects).
// Master volume 0..mt::kMasterVolMax; fallback when nothing is stored yet.
uint8_t loadVolume(uint8_t fallback);
void saveVolume(uint8_t v);

}  // namespace storage
