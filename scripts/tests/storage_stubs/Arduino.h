#pragma once
#include <cstdint>
#include <cstring>
#include <algorithm>
inline size_t strlcpy(char* dst, const char* src, size_t cap) {
 const size_t n = strlen(src); if(cap) {const size_t k = std::min(n,cap-1); memcpy(dst,src,k);dst[k]=0;} return n;
}
inline uint32_t millis() {static uint32_t t=0;return ++t;}
#define pdMS_TO_TICKS(x) (x)
inline void vTaskDelay(uint32_t) {}
struct SerialStub {template<class... T> void printf(const char*,T...) {}};
inline SerialStub Serial;
