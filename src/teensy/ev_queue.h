#pragma once
#include <stdint.h>
#include <atomic>

// Synth events from the link (loop()) to the audio interrupt: single producer, single consumer.
struct QEv {
  uint64_t localUs;  // when it plays, Teensy micros() timeline
  uint8_t track, len, b[3];
};

template <int N>
class EvQueue {
  static_assert((N & (N - 1)) == 0, "power of two");

 public:
  // Producer. False when full (the event is lost).
  bool push(const QEv& e) {
    const uint32_t h = head_.load(std::memory_order_relaxed);
    if (h - tail_.load(std::memory_order_acquire) >= N) return false;
    buf_[h & (N - 1)] = e;
    head_.store(h + 1, std::memory_order_release);
    return true;
  }
  // Consumer: the oldest event or nullptr; pop() after using it.
  const QEv* peek() const {
    const uint32_t t = tail_.load(std::memory_order_relaxed);
    return t == head_.load(std::memory_order_acquire) ? nullptr : &buf_[t & (N - 1)];
  }
  void pop() { tail_.store(tail_.load(std::memory_order_relaxed) + 1, std::memory_order_release); }
  void clear() { tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release); }

 private:
  QEv buf_[N];
  std::atomic<uint32_t> head_{0}, tail_{0};
};
