#pragma once
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "freertos/FreeRTOS.h"

inline uint32_t millis() {
  using namespace std::chrono;
  static const auto start = steady_clock::now();
  return duration_cast<milliseconds>(steady_clock::now() - start).count();
}
inline uint32_t micros() {
  using namespace std::chrono;
  return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}
inline void delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
inline void vTaskDelay(uint32_t ms) { delay(ms); }
class String : public std::string {
 public:
  using std::string::string;
  String(const std::string& s) : std::string(s) {}
  template <class T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
  explicit String(T n) : std::string(std::to_string(n)) {}
  bool isEmpty() const { return empty(); }
  int lastIndexOf(char c) const {
    auto p = rfind(c);
    return p == npos ? -1 : static_cast<int>(p);
  }
  String substring(size_t p) const { return substr(p); }
};
struct Console {
  template <class... Args>
  void printf(const char* fmt, Args... args) {
    std::fprintf(stderr, fmt, args...);
  }
  void println(const char* text) { std::fprintf(stderr, "%s\n", text); }
};
inline Console Serial;
