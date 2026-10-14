#include "stream_sched.h"

namespace mt {

int schedulePass(StreamConsumer* const* c, int n, BgWork* const* bg, int nBg, int& bgNext) {
  int loads = 0;
  uint32_t minRes = UINT32_MAX;
  for (; loads < kPassLoads; ++loads) {
    int best = -1;
    uint32_t bestRes = UINT32_MAX;
    minRes = UINT32_MAX;
    for (int i = 0; i < n; ++i) {
      uint32_t r = UINT32_MAX;
      const bool need = c[i]->needs(r);
      if (r < minRes) minRes = r;
      if (need && (best < 0 || r < bestRes)) {
        best = i;
        bestRes = r;
      }
    }
    if (best < 0) break;
    c[best]->load();
  }
  if (loads == kPassLoads) {  // reserves after the last load
    minRes = UINT32_MAX;
    for (int i = 0; i < n; ++i) {
      uint32_t r = UINT32_MAX;
      c[i]->needs(r);
      if (r < minRes) minRes = r;
    }
  }
  if (minRes <= kBgReserveUs || nBg <= 0) return loads;
  for (int k = 0; k < nBg; ++k) {
    const int i = (bgNext + k) % nBg;
    if (bg[i]->step()) {
      bgNext = (i + 1) % nBg;
      break;
    }
  }
  return loads;
}

}  // namespace mt
