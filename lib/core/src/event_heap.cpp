#include "event_heap.h"
#include <utility>

namespace mt {

bool EventHeap::push(const SchedEvent& e) {
  const int limit = e.isNoteOff() ? kCap : kCap - kOffReserve;
  if (n_ >= limit) return false;
  int i = n_++;
  buf_[i] = e;
  buf_[i].seq = seq_++;
  while (i > 0) {
    const int p = (i - 1) / 2;
    if (!before(buf_[i], buf_[p])) break;
    std::swap(buf_[i], buf_[p]);
    i = p;
  }
  return true;
}

void EventHeap::pop() {
  if (n_ == 0) return;
  buf_[0] = buf_[--n_];
  siftDown(0);
}

void EventHeap::siftDown(int i) {
  for (;;) {
    const int l = 2 * i + 1, r = l + 1;
    int m = i;
    if (l < n_ && before(buf_[l], buf_[m])) m = l;
    if (r < n_ && before(buf_[r], buf_[m])) m = r;
    if (m == i) break;
    std::swap(buf_[i], buf_[m]);
    i = m;
  }
}

}  // namespace mt
