#pragma once
#include <stddef.h>
#include <utility>
namespace mt {
template<class Frame, size_t N> class MenuHistory {
 public:
  bool push(const Frame& frame) {
    if (size_ == N) return false;
    frames_[size_++] = frame;
    return true;
  }
  bool pop(Frame& frame) {
    if (!size_) return false;
    frame = std::move(frames_[--size_]);
    frames_[size_] = Frame{};
    return true;
  }
  void clear() { while (size_) frames_[--size_] = Frame{}; }
  size_t size() const { return size_; }
 private:
  Frame frames_[N]{};
  size_t size_ = 0;
};
}
