#pragma once
#include <stdint.h>

namespace mt {

enum class WtErr : uint8_t { Ok, BadLength, Silent };
struct WtFormat {
  int frameLen = 0;  // points per source frame
  int frames = 0;    // source frames
};
constexpr uint32_t kWtMaxSrcSamples = 256 * 2048;  // import limit (Serum max)

// Frame layout of a mono wavetable of n samples; clm = WavInfo::clmFrame (0 = none).
WtErr wtDetect(uint32_t n, uint16_t clm, WtFormat& out);
// Mono samples (any wtDetect layout) -> mip-mapped table (kWtTableSamples, wt_mip.h).
WtErr wtImport(const int16_t* mono, uint32_t n, uint16_t clm, int16_t* table);

}  // namespace mt
