#pragma once
#include <stdint.h>
#include "stream_pool.h"

namespace mt {

constexpr int kAhead = 4;  // legacy blocks wanted past the current one
constexpr int kWantMax = kSlotBlocks;  // guarded consumers can use every existing cache way

// Blocks needed by the current FIR window first, then the cursor's future path. The current
// block stays first, guards follow region/loop/reflection rules. The result is bounded to
// kWantMax entries; no guard can cause an out-of-bounds cache plan. Without guards this is the
// legacy current block plus kAhead future blocks. A window that cannot fit has zero reserve.
int streamWant(const PlaySnap& p, uint32_t want[kWantMax]);
// Conservative reserve for slot s: frames in memory (blocks, heads) ahead along the path / step,
// in us at 44.1 kHz. A full prefetch horizon still has finite reserve; UINT32_MAX only for a free
// slot, a stationary cursor, or data cached through the end / a whole loop.
uint32_t streamReserveUs(const StreamPool& pool, int s);
// One-snapshot query for scheduling: the same reserve as streamReserveUs and the same load
// availability as streamPlan, without assembling a batch or selecting its destination ways.
bool streamNeeds(const StreamPool& pool, int s, uint32_t& reserveUs);
// The next load for slot s: the first wanted block that is missing and up to kLoadMax - 1 more
// missing ones right after it, into ways no wanted block uses. False if nothing is missing.
bool streamPlan(const StreamPool& pool, int s, StreamLoad& out);
// streamPlan + read + commit. False: nothing to load, or the read failed.
bool streamLoad(StreamPool& pool, int s, BlockReader& r);

}  // namespace mt
