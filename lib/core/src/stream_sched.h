#pragma once
#include <stdint.h>

namespace mt {

constexpr int kPassLoads = 4;             // card loads per loop() pass
constexpr uint32_t kBgReserveUs = 30000;  // background work only when every consumer has more

// Something that must get card data on time (a voice slot, the preview ring).
struct StreamConsumer {
  // Time until it runs dry (us); false if it has nothing to load (r still says its reserve).
  virtual bool needs(uint32_t& reserveUs) = 0;
  virtual void load() = 0;  // one load (<= kLoadMax blocks, <= 4 KB)
};
// Work that may wait: one bounded portion per step(); false if it had nothing to do.
struct BgWork {
  virtual bool step() = 0;
};

// One loop() pass: up to kPassLoads loads, each for the consumer with the least reserve; then, if
// every reserve is above kBgReserveUs, one portion of background work, round robin from bgNext
// (idle ones are skipped). Returns the loads done.
int schedulePass(StreamConsumer* const* c, int n, BgWork* const* bg, int nBg, int& bgNext);

}  // namespace mt
