#include "sector_io.h"
#include <string.h>
#include "hot.h"

namespace mt {

MT_CONTROL bool SectorIo::valid(uint32_t sector, uint32_t count, const void* data) const {
  return data && bounce_ && !(reinterpret_cast<uintptr_t>(bounce_) & 31) && capacity_ &&
         capacity_ <= UINT32_MAX / 512 && count <= UINT32_MAX / 512 &&
         uint64_t(sector) + count - 1 <= UINT32_MAX;
}

MT_CONTROL bool SectorIo::readChunk(uint32_t sector, uint32_t count) {
  const uint32_t bytes = count * 512;
  for (int attempt = 0; attempt < kTries; ++attempt) {
    device_.prepareRead(bounce_, bytes);
    const bool good = device_.read(sector, count, bounce_);
    device_.finishRead(bounce_, bytes);
    if (good) return true;
    if (attempt + 1 == kTries || !device_.recover()) return false;
  }
  return false;
}

MT_CONTROL bool SectorIo::writeChunk(uint32_t sector, uint32_t count) {
  for (int attempt = 0; attempt < kTries; ++attempt) {
    device_.prepareWrite(bounce_, count * 512);
    if (device_.write(sector, count, bounce_)) return true;
    if (attempt + 1 == kTries || !device_.recover()) return false;
  }
  return false;
}

MT_CONTROL bool SectorIo::read(uint32_t sector, uint32_t count, uint8_t* out) {
  if (!count) return true;
  if (!valid(sector, count, out)) return false;
  while (count) {
    const uint32_t take = count < capacity_ ? count : capacity_;
    if (!readChunk(sector, take)) return false;
    memcpy(out, bounce_, take * 512);
    out += take * 512;
    sector += take;
    count -= take;
  }
  return true;
}

MT_CONTROL bool SectorIo::write(uint32_t sector, uint32_t count, const uint8_t* in) {
  if (!count) return true;
  if (!valid(sector, count, in)) return false;
  while (count) {
    const uint32_t take = count < capacity_ ? count : capacity_;
    memcpy(bounce_, in, take * 512);
    if (!writeChunk(sector, take)) return false;
    in += take * 512;
    sector += take;
    count -= take;
  }
  return device_.finishWrite();
}

}  // namespace mt
