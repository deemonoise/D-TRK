#pragma once

#include <cstdlib>
#include <cstring>

namespace rg351v {
inline bool selected() {
  const char* device = std::getenv("DTRK_DEVICE");
  return device && std::strcmp(device, "rg351v") == 0;
}
}  // namespace rg351v
