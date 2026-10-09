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

// Source frames on demand (a file read frame by frame instead of whole): frame k of fmt's layout as
// fmt.frameLen mono points, valid until the next call.
struct WtFrameReader {
  virtual const int16_t* frame(int k, const WtFormat& fmt) = 0;
};
// wtImport over a reader; reads each frame it uses twice (wtBuild makes two passes).
WtErr wtImport(WtFrameReader& rd, uint32_t n, uint16_t clm, int16_t* table);

}  // namespace mt
