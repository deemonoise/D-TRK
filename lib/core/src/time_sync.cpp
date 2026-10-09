#include "time_sync.h"

namespace mt::link {

void TimeSync::sample(uint64_t espUs, uint64_t localUs) {
  while (n_ > 0 && localUs - ring_[head_].localUs > kWindowUs) {
    head_ = (head_ + 1) % kSlots;
    --n_;
  }
  if (n_ == kSlots) {
    head_ = (head_ + 1) % kSlots;
    --n_;
  }
  ring_[(head_ + n_) % kSlots] = {localUs, static_cast<int64_t>(localUs - espUs)};
  ++n_;
  int64_t m = ring_[head_].delta;
  for (int i = 1; i < n_; ++i) {
    int64_t d = ring_[(head_ + i) % kSlots].delta;
    if (d < m) m = d;
  }
  offset_ = m;
}

}  // namespace mt::link
