#pragma once
#include <stdint.h>
#include "stream_pool.h"

namespace mt {

constexpr int kAhead = 4;  // blocks wanted past the current one

// Blocks a consumer at p needs, in play order: the current one and kAhead more along its path
// (loops wrap, ping-pong turns, LoopMode::Off stops at the edge). Returns the count (1..kAhead + 1).
int streamWant(const PlaySnap& p, uint32_t want[kAhead + 1]);
// Time until slot s runs out of data: frames in memory (blocks, heads) ahead along the path / step,
// in us at 44.1 kHz. UINT32_MAX for a free slot or a slot with nothing left to load.
uint32_t streamReserveUs(const StreamPool& pool, int s);
// The next load for slot s: the first wanted block that is missing and up to kLoadMax - 1 more
// missing ones right after it, into ways no wanted block uses. False if nothing is missing.
bool streamPlan(const StreamPool& pool, int s, StreamLoad& out);
// streamPlan + read + commit. False: nothing to load, or the read failed.
bool streamLoad(StreamPool& pool, int s, BlockReader& r);

}  // namespace mt
