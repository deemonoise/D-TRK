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

}  // namespace mt
