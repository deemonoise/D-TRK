#pragma once
#include <cstdlib>
#include <limits>
#include <map>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_8BIT 4
namespace host_heap {
// Only explicit PSRAM allocations consume this test budget. Ordinary host allocations
// keep the live model and the fake card independent of the firmware's staging heap.
inline size_t psramBudget = std::numeric_limits<size_t>::max();
inline size_t psramUsed = 0;
inline size_t rejected = 0;
inline std::map<void*, size_t> psramBlocks;
}
inline void* heap_caps_malloc(size_t n, int caps) {
 if ((caps & MALLOC_CAP_SPIRAM) &&
     (host_heap::psramUsed > host_heap::psramBudget || n > host_heap::psramBudget-host_heap::psramUsed)) {
   ++host_heap::rejected;
   return nullptr;
 }
 void* p = malloc(n);
 if (p && (caps & MALLOC_CAP_SPIRAM)) {
   host_heap::psramBlocks[p] = n;
   host_heap::psramUsed += n;
 }
 return p;
}
inline void heap_caps_free(void* p) {
 const auto block = host_heap::psramBlocks.find(p);
 if (block != host_heap::psramBlocks.end()) {
   host_heap::psramUsed -= block->second;
   host_heap::psramBlocks.erase(block);
 }
 free(p);
}
