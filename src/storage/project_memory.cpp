#include "project_memory.h"
#include "esp_heap_caps.h"
#include "model.h"

namespace storage {
namespace {
void* workspace = nullptr;
bool leased = false;
}

bool reserveProjectMemory() {
  if (!workspace) workspace = heap_caps_malloc(sizeof(mt::Project), MALLOC_CAP_SPIRAM);
  return workspace != nullptr;
}

void* allocateProjectMemory(size_t bytes) {
  if (!bytes || bytes > sizeof(mt::Project) || leased || !reserveProjectMemory()) return nullptr;
  leased = true;
  return workspace;
}

void releaseProjectMemory(void* memory) {
  if (memory && memory == workspace) leased = false;
}
}  // namespace storage
