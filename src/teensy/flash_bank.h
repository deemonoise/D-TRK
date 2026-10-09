#pragma once
#include <stdint.h>
#include "sample_bank.h"

// Teensy 4.1 program flash (8 MB at 0x60000000, 4 KB sectors) as the sample bank's partition.
//   0x60000000  firmware, up to kFwMax
//   0x60100000  OTA staging (kOtaSize), FwFromFile
//   0x60200000  sample bank, up to the linker's FLASH end (0x607C0000): above it the core's EEPROM
//               emulation (63 sectors) and the restore area.
namespace flashmap {

constexpr uint32_t kBase = 0x60000000;
constexpr uint32_t kFwMax = 0x100000;
constexpr uint32_t kOtaBase = kBase + kFwMax;
constexpr uint32_t kOtaSize = 0x100000;
constexpr uint32_t kBankBase = kOtaBase + kOtaSize;
constexpr uint32_t kFlashEnd = kBase + 7936 * 1024;  // imxrt1062_t41.ld: FLASH LENGTH = 7936K
constexpr uint32_t kBankSize = kFlashEnd - kBankBase;
static_assert(kBankBase % mt::kBankAlign == 0 && kBankSize % mt::kBankAlign == 0, "sector aligned");

// The running firmware (linker symbol _flashimagelen) fits below kOtaBase. Checked at boot: a
// larger image would be overwritten by the OTA staging area.
bool firmwareFits();
uint32_t firmwareBytes();

}  // namespace flashmap

// Erase / program go through the core's FlexSPI routines (eeprom.c): interrupts are off while
// each runs (a 4 KB erase: ~30-50 ms, a 256-byte page: ~0.5 ms), so the audio interrupt is late and
// the UART may drop bytes. The bank only changes inside bank jobs, with the synth parked.
class FlashBank final : public mt::BankFlash {
 public:
  uint32_t size() const override { return flashmap::kBankSize; }
  bool read(uint32_t off, void* d, uint32_t n) override;
  bool erase(uint32_t off, uint32_t n) override;
  bool write(uint32_t off, const void* d, uint32_t n) override;
  const uint8_t* mapped() const override { return reinterpret_cast<const uint8_t*>(flashmap::kBankBase); }
};
