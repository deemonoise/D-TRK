#pragma once
#include <unistd.h>

#include <cstdlib>
constexpr int MALLOC_CAP_8BIT = 1, MALLOC_CAP_INTERNAL = 2, MALLOC_CAP_SPIRAM = 4;
inline void* heap_caps_malloc(size_t n, int) { return std::malloc(n); }
inline void* heap_caps_calloc(size_t n, size_t sz, int) { return std::calloc(n, sz); }
inline void heap_caps_free(void* p) { std::free(p); }
inline size_t heap_caps_get_free_size(int) {
#ifdef _SC_AVPHYS_PAGES
  long pages = sysconf(_SC_AVPHYS_PAGES);
  return pages > 0 ? size_t(pages) * sysconf(_SC_PAGESIZE) : 0;
#else
  return 0;
#endif
}
inline size_t heap_caps_get_largest_free_block(int) { return 0; }
inline size_t heap_caps_get_minimum_free_size(int) { return 0; }
