#include "flash_bank.h"
#include <Arduino.h>
#include <string.h>

// cores/teensy4/eeprom.c (also declared by FlasherX's FlashTxx.h).
extern "C" {
void eepromemu_flash_write(void* addr, const void* data, uint32_t len);
void eepromemu_flash_erase_sector(void* addr);
extern unsigned long _flashimagelen;
}

namespace flashmap {

uint32_t firmwareBytes() { return reinterpret_cast<uint32_t>(&_flashimagelen); }
bool firmwareFits() { return firmwareBytes() <= kFwMax; }

}  // namespace flashmap

namespace {

constexpr uint32_t kPage = 256;  // program page: one write never crosses it

bool inRange(uint32_t off, uint32_t n) { return off <= flashmap::kBankSize && n <= flashmap::kBankSize - off; }

}  // namespace

bool FlashBank::read(uint32_t off, void* d, uint32_t n) {
  if (!inRange(off, n)) return false;
  memcpy(d, reinterpret_cast<const void*>(flashmap::kBankBase + off), n);
  return true;
}

bool FlashBank::erase(uint32_t off, uint32_t n) {
  if (off % mt::kBankAlign || n % mt::kBankAlign || !inRange(off, n)) return false;
  for (uint32_t at = 0; at < n; at += mt::kBankAlign)
    eepromemu_flash_erase_sector(reinterpret_cast<void*>(flashmap::kBankBase + off + at));
  return true;
}

bool FlashBank::write(uint32_t off, const void* d, uint32_t n) {
  if (!inRange(off, n)) return false;
  // The source must not be flash itself (FlexSPI is busy programming): pages go through RAM.
  static uint8_t page[kPage];
  const uint8_t* s = static_cast<const uint8_t*>(d);
  while (n > 0) {
    uint32_t len = kPage - (off % kPage);
    if (len > n) len = n;
    memcpy(page, s, len);
    void* at = reinterpret_cast<void*>(flashmap::kBankBase + off);
    eepromemu_flash_write(at, page, len);
    if (memcmp(at, page, len) != 0) return false;  // worn / not erased
    off += len;
    s += len;
    n -= len;
  }
  return true;
}
