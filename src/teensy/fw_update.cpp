#include "fw_update.h"
#include <Arduino.h>
#include <SD.h>
#include <string.h>
#include "audio_out.h"
#include "bank.h"
#include "flash_bank.h"
#include "fs_server.h"
#include "ihex.h"
#include "link_server.h"
#include "preview_stream.h"
#include "render_server.h"
extern "C" {
#include "FlashTxx.h"
}

using namespace mt::link;
using flashmap::kBase;
using flashmap::kOtaBase;
using flashmap::kOtaSize;

// The target id FlasherX looks for in an image: every firmware carries it (referenced in begin()).
extern "C" const char kFwId[] = FLASH_ID;

namespace fw {

namespace {

constexpr uint32_t kSector = 4096;
constexpr uint32_t kPage = 256;
constexpr uint32_t kProgressBytes = 4096;

SynthStream* out = nullptr;

uint8_t seq = 0;
bool haveLast = false;
uint8_t lastSeq = 0;
FwRep lastRep;
bool movePending = false;
uint32_t moveSize = 0;

// Staging: the page being filled (RAM: the flash write takes its data from RAM) and the OTA sectors
// erased so far (from kOtaBase).
alignas(4) uint8_t page[kPage];
uint32_t pageAddr = 0;
uint32_t erasedEnd = kOtaBase;

bool sectorErased(uint32_t a) {
  const uint32_t* p = reinterpret_cast<const uint32_t*>(a & ~(kSector - 1));
  for (uint32_t i = 0; i < kSector / 4; ++i)
    if (p[i] != 0xFFFFFFFFu) return false;
  return true;
}

bool eraseSector(uint32_t a) {
  if (sectorErased(a)) return true;
  eepromemu_flash_erase_sector(reinterpret_cast<void*>(a));
  return sectorErased(a);
}

bool flushPage() {
  if (!pageAddr) return true;
  eepromemu_flash_write(reinterpret_cast<void*>(pageAddr), page, kPage);
  const bool ok = memcmp(reinterpret_cast<const void*>(pageAddr), page, kPage) == 0;
  pageAddr = 0;
  return ok;
}

// Bytes for OTA address dst: sectors erased ahead of the first write into them.
bool stage(uint32_t dst, const uint8_t* d, int n) {
  for (int i = 0; i < n; ++i, ++dst) {
    const uint32_t pa = dst & ~(kPage - 1);
    if (pa != pageAddr) {
      if (!flushPage()) return false;
      while (erasedEnd <= pa) {
        if (!eraseSector(erasedEnd)) return false;
        erasedEnd += kSector;
      }
      memcpy(page, reinterpret_cast<const void*>(pa), kPage);
      pageAddr = pa;
    }
    page[dst & (kPage - 1)] = d[i];
  }
  return true;
}

void progress(uint32_t done, uint32_t total) {
  Progress pr;
  pr.op = static_cast<uint8_t>(Msg::FwFromFile);
  pr.done = done;
  pr.total = total;
  link::reply(Msg::Progress, seq, pr);
  link::statusIfDue();
}

// The OTA area back to erased (what was staged of a rejected image).
void dropStaged() {
  pageAddr = 0;
  for (uint32_t a = kOtaBase; a < erasedEnd; a += kSector) {
    eraseSector(a);
    if ((a & 0xFFFF) == 0) progress(a - kOtaBase, erasedEnd - kOtaBase);
  }
  erasedEnd = kOtaBase;
}

FwResult stageFile(const char* path, uint32_t& size) {
  size = 0;
  if (!card::ready()) return FwResult::NoSd;
  FsFile f = SD.sdfs.open(path, O_RDONLY);
  if (!f || f.isDir()) return FwResult::OpenFail;
  const uint32_t total = static_cast<uint32_t>(f.fileSize());
  mt::IhexReader hex;
  static char line[600];
  static uint8_t buf[512];
  int lineN = 0, bufN = 0, bufAt = 0;
  uint32_t done = 0, nextProgress = kProgressBytes;
  uint32_t top = kBase;  // end of the image
  bool lineTooLong = false;
  for (;;) {
    if (bufAt == bufN) {
      bufN = f.read(buf, sizeof buf);
      bufAt = 0;
      if (bufN < 0) return FwResult::ReadFail;
      if (bufN == 0) break;
      done += bufN;
      if (done >= nextProgress) {
        nextProgress = done + kProgressBytes;
        progress(done, total);
      }
    }
    const char c = static_cast<char>(buf[bufAt++]);
    if (c != '\n') {
      if (lineN < static_cast<int>(sizeof line)) line[lineN++] = c;
      else lineTooLong = true;
      continue;
    }
    if (lineTooLong) return FwResult::BadHex;
    const mt::IhexReader::Line l = hex.feed(line, lineN);
    lineN = 0;
    if (l == mt::IhexReader::Line::Bad) return FwResult::BadHex;
    if (l != mt::IhexReader::Line::Data) continue;
    const uint32_t a = hex.address();
    if (a < kBase) return FwResult::BadImage;
    if (a + hex.size() > kBase + kOtaSize) return FwResult::TooBig;
    if (!stage(kOtaBase + (a - kBase), hex.data(), hex.size())) return FwResult::FlashFail;
    if (a + hex.size() > top) top = a + hex.size();
  }
  if (lineN > 0 && !lineTooLong && hex.feed(line, lineN) == mt::IhexReader::Line::Bad) return FwResult::BadHex;
  if (!hex.eof()) return FwResult::BadHex;  // cut short
  if (!flushPage()) return FwResult::FlashFail;
  size = top - kBase;
  if (!mt::teensyImageOk(reinterpret_cast<const uint8_t*>(kOtaBase), size, kBase, FLASH_ID)) return FwResult::BadImage;
  return FwResult::Ok;
}

FwRep update(Reader& r) {
  FwRep m;
  FwFromFileReq q;
  if (!decode(r, q)) {
    m.result = static_cast<uint8_t>(FwResult::OpenFail);
    return m;
  }
  if (render::active() || bank::writable() == mt::BankResult::Busy) {
    m.result = static_cast<uint8_t>(FwResult::Busy);
    return m;
  }
  preview::stop();
  out->park(true);  // flash erases hold the interrupts off for tens of ms: no glitching sound
  erasedEnd = kOtaBase;
  pageAddr = 0;
  uint32_t size = 0;
  const FwResult res = stageFile(q.path, size);
  m.result = static_cast<uint8_t>(res);
  m.bytes = size;
  if (res == FwResult::Ok) {
    link::log("fw: %s staged, %lu bytes", q.path, static_cast<unsigned long>(size));
    movePending = true;
    moveSize = size;
  } else {
    link::log("fw: %s rejected (%u)", q.path, static_cast<unsigned>(res));
    dropStaged();
    out->park(false);
  }
  return m;
}

// From RAM with every maskable interrupt held off (BASEPRI: the flash routines toggle PRIMASK):
// the staged image over the firmware, the old firmware's tail erased up to the OTA area, reboot.
FASTRUN __attribute__((noinline)) void moveAndReboot(uint32_t size) {
  static uint8_t buf[kPage];
  __asm__ volatile("msr basepri, %0" ::"r"(16u) : "memory");
  for (uint32_t off = 0; off < size; off += kPage) {
    const uint32_t dst = kBase + off;
    if ((dst & (kSector - 1)) == 0) {
      const uint32_t* p = reinterpret_cast<const uint32_t*>(dst);
      for (uint32_t i = 0; i < kSector / 4; ++i)
        if (p[i] != 0xFFFFFFFFu) {
          eepromemu_flash_erase_sector(reinterpret_cast<void*>(dst));
          break;
        }
    }
    const uint8_t* src = reinterpret_cast<const uint8_t*>(kOtaBase + off);
    for (uint32_t i = 0; i < kPage; ++i) buf[i] = src[i];
    eepromemu_flash_write(reinterpret_cast<void*>(dst), buf, kPage);
  }
  for (uint32_t a = kBase + (size + kSector - 1) / kSector * kSector; a < kOtaBase; a += kSector) {
    const uint32_t* p = reinterpret_cast<const uint32_t*>(a);
    for (uint32_t i = 0; i < kSector / 4; ++i)
      if (p[i] != 0xFFFFFFFFu) {
        eepromemu_flash_erase_sector(reinterpret_cast<void*>(a));
        break;
      }
  }
  SCB_AIRCR = 0x05FA0004;
  for (;;) {
  }
}

}  // namespace

void begin(SynthStream& o) {
  out = &o;
  volatile char keep = kFwId[0];  // the id stays in the image
  (void)keep;
}

bool isRequest(Msg t) { return t == Msg::FwFromFile; }

int handle(Msg t, uint8_t s, const uint8_t* p, int n, uint8_t* o) {
  Writer w(o, kMaxPayload);
  if (haveLast && lastSeq == s) {  // a retry: its reply got lost
    encode(lastRep, w);
    return w.ok() ? w.size() : 0;
  }
  if (movePending) return 0;
  seq = s;
  Reader r(p, n);
  lastRep = update(r);
  lastSeq = s;
  haveLast = true;
  encode(lastRep, w);
  return w.ok() ? w.size() : 0;
}

void step() {
  if (!movePending) return;
  link::flushOut();
  delay(50);  // the UART's last bytes on the wire
  moveAndReboot(moveSize);
}

}  // namespace fw
