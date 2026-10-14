#include "stream_info.h"
#include <stdio.h>

namespace mt {

void streamLine(char* out, int n, uint8_t psram, uint8_t fill, uint32_t underruns) {
  const unsigned long u = underruns;
  if (psram == 1 && fill < 100) snprintf(out, n, "SD+PSRAM %u%%, UNDERRUNS %lu", fill, u);
  else if (psram == 1) snprintf(out, n, "SD+PSRAM, UNDERRUNS %lu", u);
  else if (psram == 2) snprintf(out, n, "SD, PSRAM FAILED, UNDERRUNS %lu", u);
  else snprintf(out, n, "SD, UNDERRUNS %lu", u);
}

}  // namespace mt
