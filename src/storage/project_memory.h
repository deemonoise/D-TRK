#pragma once
#include <stddef.h>
namespace storage {
// One UI-task workspace shared by project I/O and a temporary Dice draft.
// Reserve before display/network caches fragment PSRAM; leases never overlap.
bool reserveProjectMemory();
void* allocateProjectMemory(size_t bytes);
void releaseProjectMemory(void* memory);
}
