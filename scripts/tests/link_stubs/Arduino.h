#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
inline uint32_t testNow = 0;
inline uint32_t millis() { return testNow; }
inline void delay(uint32_t n) { testNow += n; }
using boolean = bool;
inline size_t strlcpy(char *d, const char *s, size_t n) {
  size_t len = strlen(s);
  if (n) {
    size_t k = std::min(n - 1, len);
    memcpy(d, s, k);
    d[k] = 0;
  }
  return len;
}
struct SerialStub {
  template <class... T> void printf(const char *, T...) {}
  void println(const char *) {}
};
inline SerialStub Serial;
class String {
  std::string s;

public:
  String() {}
  String(const char *p) : s(p) {}
  bool isEmpty() const { return s.empty(); }
  const char *c_str() const { return s.c_str(); }
  int lastIndexOf(char c) const {
    auto p = s.rfind(c);
    return p == std::string::npos ? -1 : int(p);
  }
  String substring(int n) const { return String(s.substr(n).c_str()); }
};
