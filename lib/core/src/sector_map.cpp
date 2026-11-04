#include "sector_map.h"
#include <string.h>

namespace mt {

bool appendSectorExtent(SectorExtent* runs, int& count, int capacity, uint32_t logical,
                        uint32_t physical, uint32_t length) {
  if (!runs || count < 0 || count > capacity || !length || !physical) return false;
  if (uint64_t(logical) + length > UINT32_MAX || uint64_t(physical) + length - 1 > UINT32_MAX)
    return false;
  const uint64_t next = count ? uint64_t(runs[count-1].fileSector) + runs[count-1].count : 0;
  if (next != logical) return false;
  if (count && uint64_t(runs[count-1].cardSector) + runs[count-1].count == physical) {
    runs[count-1].count += length;
    return true;
  }
  if (count == capacity) return false;
  runs[count++] = {logical, physical, length};
  return true;
}

namespace {
const SectorExtent* findExtent(const SectorExtent* runs, int count, uint32_t at) {
  int lo = 0, hi = count;
  while (lo < hi) {
    const int mid = lo + (hi - lo) / 2;
    if (runs[mid].fileSector <= at) lo = mid + 1;
    else hi = mid;
  }
  if (!lo) return nullptr;
  const SectorExtent& run = runs[lo-1];
  return at - run.fileSector < run.count ? &run : nullptr;
}
}  // namespace

bool SectorMap::read(uint32_t at, uint32_t length, uint8_t* out, SectorReader& card) const {
  if (!length) return true;
  if (!out || length > UINT32_MAX / 512) return false;
  if (extents ? !count : (count || !first)) return false;
  uint32_t left = at < sectors ? sectors - at : 0;
  if (left > length) left = length;
  const uint32_t padding = length - left;
  while (left) {
    uint32_t take = left;
    uint64_t physical = uint64_t(first) + at;
    if (extents) {
      const SectorExtent* run = findExtent(extents, count, at);
      if (!run || !run->cardSector) return false;
      const uint32_t offset = at - run->fileSector;
      physical = uint64_t(run->cardSector) + offset;
      if (take > run->count - offset) take = run->count - offset;
    }
    if (physical + take - 1 > UINT32_MAX) return false;
    if (!card.read(static_cast<uint32_t>(physical), take, out)) return false;
    left -= take;
    at += take;
    out += take * 512;
  }
  if (padding) memset(out, 0, padding * 512);
  return true;
}

}  // namespace mt
