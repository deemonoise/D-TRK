#pragma once

#include "hw/ble_midi.h"

#ifdef __linux__
#include <sys/types.h>

namespace hw::ble::pipewire {
// ALSA endpoints have type 0. Store a distinct type with the real BLE address.
inline constexpr uint8_t kDeviceType = 2;
int scan(Found* out, int max);
pid_t start(int client, const char* in, int inPort, const char* out, int outPort);
bool running(pid_t& child);
void stop(pid_t& child);
}  // namespace hw::ble::pipewire
#endif
