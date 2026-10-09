#pragma once
#include <stdint.h>

// Firmware updates of the synth board from a .hex on its card (FwFromFile). Blocking, the UI task.
namespace synthfw {

constexpr const char* kHexPath = "/firmware/teensy.hex";
constexpr const char* kHexTmp = "/firmware/teensy.hex.tmp";  // a Wi-Fi upload in progress
constexpr uint32_t kRebootWaitMs = 60000;

// Progress: bytes of the file read so far / its size.
using ProgressFn = void (*)(uint32_t done, uint32_t total, void* ctx);
enum class Result : uint8_t {
  Ok,        // staged: the board replaces its firmware and reboots
  NoSynth,   // no answer
  Rejected,  // the board refused it (rejectText)
};
// result: mt::link::FwResult of a Rejected one; bytes: the image size.
Result update(const char* path, uint8_t& fwResult, uint32_t& bytes, ProgressFn cb = nullptr, void* ctx = nullptr);
const char* rejectText(uint8_t fwResult);  // short, upper case
// After Ok: the board is back with another boot id than oldBoot (its Hello seen).
bool rebooted(uint32_t oldBoot);

}  // namespace synthfw
