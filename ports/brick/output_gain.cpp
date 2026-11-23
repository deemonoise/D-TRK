#include "output_gain.h"

#include <algorithm>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace brick {
namespace {
constexpr uint32_t unity = 1u << 16;
int16_t scale(int16_t sample, uint32_t gain) {
  const int64_t value = static_cast<int64_t>(sample) * gain / unity;
  return static_cast<int16_t>(std::clamp<int64_t>(value, -32768, 32767));
}
}  // namespace

OutputGain::~OutputGain() {
  if (gains_) munmap(const_cast<uint64_t*>(gains_), sizeof(*gains_));
}
bool OutputGain::open(const char* path) {
  if (!path) return true;  // PipeWire owns volume on the system route.
  const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return false;
  struct stat st {};
  void* mapping = MAP_FAILED;
  if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == sizeof(uint64_t))
    mapping = mmap(nullptr, sizeof(uint64_t), PROT_READ, MAP_SHARED, fd, 0);
  close(fd);
  if (mapping == MAP_FAILED) return false;
  gains_ = static_cast<const uint64_t*>(mapping);
  return true;
}
void OutputGain::apply(int16_t* samples, size_t frames) const {
  if (!gains_) return;
  static_assert(__atomic_always_lock_free(sizeof(uint64_t), nullptr), "64-bit audio control must be lock-free");
  const uint64_t gains = __atomic_load_n(gains_, __ATOMIC_RELAXED);
  const uint32_t left = static_cast<uint32_t>(gains), right = static_cast<uint32_t>(gains >> 32);
  if (left == unity && right == unity) return;
  for (size_t i = 0; i < frames; ++i) {
    samples[2 * i] = scale(samples[2 * i], left);
    samples[2 * i + 1] = scale(samples[2 * i + 1], right);
  }
}
}  // namespace brick
