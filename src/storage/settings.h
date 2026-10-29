#pragma once
#include <stdint.h>

namespace mt {
class MidiDevices;
}

namespace storage {

// Device settings in NVS (survive power-off, independent of projects).
// Any small device setting by NVS key (up to 15 characters); fallback when nothing is stored yet.
uint8_t loadSetting(const char* key, uint8_t fallback);
void saveSetting(const char* key, uint8_t v);

// Colour theme index (ui::themeAt); fallback when nothing is stored yet.
uint8_t loadTheme(uint8_t fallback);
void saveTheme(uint8_t v);

// Remembered BLE MIDI devices with their learned pads (NVS "midi" / "devs": MidiDevices blob).
// No "devs" yet: built from the old keys (in / in_t / out / out_t addresses, pads), saved as "devs",
// the old keys erased. False when nothing valid is stored (out empty then).
bool loadDevices(mt::MidiDevices& out);
void saveDevices(const mt::MidiDevices& d);

// BLE OUT clock ticks on / off (NVS "midi" / "clock"); true when nothing is stored.
bool loadBleClock();
void saveBleClock(bool on);

}  // namespace storage
