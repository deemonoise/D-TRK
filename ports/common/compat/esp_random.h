#pragma once
#include <random>
inline unsigned esp_random() {
  static thread_local std::mt19937 gen(std::random_device{}());
  return gen();
}
