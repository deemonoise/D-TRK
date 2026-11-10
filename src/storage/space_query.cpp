#include "space_query.h"
#include <Arduino.h>
#include <atomic>
#include "freertos/task.h"
#include "link/link.h"
#include "link_fs.h"

namespace storage {
namespace {

std::atomic<SpaceState> state{SpaceState::Idle};
uint32_t queryBoot = 0, completedAt = 0, freeMb = 0;
constexpr uint32_t kCacheMs = 10000;

bool transfer(void*, mt::link::Msg type, const uint8_t* data, int size, uint8_t* reply, int& len) {
  // A cold FAT scan can take several seconds. The link keeps the peer alive during this wait.
  return slink::synthUp() && slink::request(type, data, size, type, reply, len, 3000);
}

void query(void*) {
  // The synchronous filesystem client belongs to the UI; this task must own its own buffers.
  mt::link::FsClientCore client(transfer, nullptr);
  uint32_t total = 0, available = 0;
  const bool ok = client.space(total, available) == mt::link::kErrOk && slink::bootId() == queryBoot;
  freeMb = available;
  completedAt = millis();
  state.store(ok ? SpaceState::Ready : SpaceState::Failed, std::memory_order_release);
  vTaskDelete(nullptr);
}

}  // namespace

void beginSpaceQuery() {
  const SpaceState s = state.load(std::memory_order_acquire);
  if (s == SpaceState::Pending) return;
  if (s == SpaceState::Ready && queryBoot == slink::bootId() && millis() - completedAt < kCacheMs) return;
  queryBoot = slink::bootId();
  state.store(SpaceState::Pending, std::memory_order_release);
  if (!slink::synthUp() || xTaskCreatePinnedToCore(query, "sd-space", 4096, nullptr, 1, nullptr, 1) != pdTRUE)
    state.store(SpaceState::Failed, std::memory_order_release);
}

SpaceResult spaceQuery() {
  const SpaceState s = state.load(std::memory_order_acquire);
  if (s == SpaceState::Ready) {
    if (queryBoot != slink::bootId()) return {SpaceState::Failed, 0};
    return {s, freeMb};
  }
  return {s, 0};
}

}  // namespace storage
