#pragma once
#include <cstdint>
#include <mutex>
namespace mt {
struct Project;
}
namespace brick {
using ProjectMutex = std::recursive_timed_mutex;
ProjectMutex& projectMutex();
void advance(uint64_t until);
void beginBank(mt::Project& p);
void mixPreview(int16_t* l, int16_t* r, int frames);
void resetAudio();
void parkForRender(bool parked);
}  // namespace brick
