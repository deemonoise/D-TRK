#include "file_rules.h"
#include <string.h>
#include <strings.h>

namespace mt {

bool projectBaseValid(const char* base) {
  const size_t n = strlen(base);
  if (n == 0 || n > 16) return false;
  for (const char* p = base; *p; ++p) {
    const char c = *p;
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
      return false;
  }
  return true;
}

bool webFirmwareName(const char* name) {
  const size_t n = name ? strlen(name) : 0;
  return n > 4 && strcasecmp(name + n - 4, ".bin") == 0;
}

bool webSynthFirmwareName(const char* name) {
  const size_t n = name ? strlen(name) : 0;
  return n > 4 && strcasecmp(name + n - 4, ".hex") == 0;
}

bool webExportPath(const char* path) {
  if (!path || strncmp(path, "/export/", 8) != 0 || !path[8]) return false;
  for (const char* seg = path + 8;;) {
    const char* end = strchr(seg, '/');
    const size_t n = end ? static_cast<size_t>(end - seg) : strlen(seg);
    if (n == 0 || (n == 1 && seg[0] == '.') || (n == 2 && seg[0] == '.' && seg[1] == '.')) return false;
    for (size_t i = 0; i < n; ++i)
      if (seg[i] == '\\') return false;
    if (!end) return true;
    seg = end + 1;
  }
}

bool parseRangeStart(const char* range, uint32_t& at) {
  if (!range || strncmp(range, "bytes=", 6) != 0) return false;
  const char* p = range + 6;
  uint64_t v = 0;
  int digits = 0;
  for (; *p >= '0' && *p <= '9'; ++p, ++digits) {
    v = v * 10 + static_cast<uint64_t>(*p - '0');
    if (v > 0xFFFFFFFFull) return false;
  }
  if (!digits || *p != '-') return false;
  at = static_cast<uint32_t>(v);
  return true;
}

}  // namespace mt
