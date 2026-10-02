#pragma once
#include <stdint.h>

namespace mt {

// xorshift32: small, deterministic with a fixed seed (tests), never yields 0.
struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed = 0x12345678u) : s(seed ? seed : 0x12345678u) {}
  uint32_t next() {
    uint32_t x = s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return s = x;
  }
  uint32_t below(uint32_t n) { return n ? next() % n : 0; }
};

}  // namespace mt
