#pragma once
#include <stdint.h>

namespace mt {

// Immutable logical-file to physical-card runs, built before a sample is published.
struct SectorExtent {
  uint32_t fileSector, cardSector, count;
};
bool appendSectorExtent(SectorExtent* runs, int& count, int capacity, uint32_t logical,
                        uint32_t physical, uint32_t length);

struct SectorReader {
  virtual bool read(uint32_t sector, uint32_t count, uint8_t* out) = 0;
};

struct SectorMap {
  uint32_t first = 0, sectors = 0;
  const SectorExtent* extents = nullptr;  // null: the existing contiguous-file fast path
  uint16_t count = 0;
  // Complete reads only; the last partial stream block is padded with zeros past file EOF.
  // A failed read must not be committed to the audio cache.
  bool read(uint32_t at, uint32_t length, uint8_t* out, SectorReader& card) const;
};

}  // namespace mt
