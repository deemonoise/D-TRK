#pragma once
// The firmware uses BSD bounded string operations, present in glibc since 2.38.
#ifdef __linux__
#include <features.h>
#if defined(__GLIBC__) && !__GLIBC_PREREQ(2, 38)
#include <cstring>
inline size_t strlcpy(char* dest, const char* src, size_t cap) {
  const size_t length = std::strlen(src);
  if (cap) {
    const size_t copy = length < cap ? length : cap - 1;
    std::memcpy(dest, src, copy);
    dest[copy] = '\0';
  }
  return length;
}
inline size_t strlcat(char* dest, const char* src, size_t cap) {
  const size_t used = strnlen(dest, cap);
  return used == cap ? cap + std::strlen(src) : used + strlcpy(dest + used, src, cap - used);
}
#endif
#endif
