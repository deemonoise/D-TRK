#pragma once
#include <stdint.h>

namespace storage {

enum class SpaceState : uint8_t { Idle, Pending, Ready, Failed };
struct SpaceResult {
  SpaceState state;
  uint32_t freeMb;
};

// UI task, nonblocking. One query survives closing/reopening its window. A successful result
// is reused for 10 s on the same synth boot; errors can be retried by opening the window again.
void beginSpaceQuery();
SpaceResult spaceQuery();

}  // namespace storage
