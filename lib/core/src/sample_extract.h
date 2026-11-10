#pragma once
#include "model.h"

namespace mt {

// Immutable source identity captured before the off-line extraction job.
struct SampleExtractOutput { uint32_t crc=0, frames=0, rate=0; uint8_t root=60; };

struct SampleExtractTarget {
  int instrument = -1, index = -1;
  ProjSample source;
  uint32_t from = 0, to = 0;
  uint32_t ratioQ16 = 65536;  // Exact copy by default; Stretch 0.5..2.
};

// Raw mono16 utility used by the extraction job and hardware benchmarks.
constexpr int32_t kSampleNormalizedPeak = 29204;  // -1 dBFS
int32_t sampleExtractPeak(const int16_t* data, uint32_t frames, int32_t previous = 0);
void sampleExtractNormalize(int16_t* data, uint32_t frames, int32_t peak);

// Zero for invalid/overflowing duration. Shared wire/UI rounding.
uint32_t sampleEditFrames(uint32_t frames, uint32_t ratioQ16);
bool sampleStretchRatio(int sourceBpm10, int targetBpm10, uint32_t& ratioQ16);

enum class SampleExtractResult : uint8_t { Ok, BadSource, BadName, NameTaken, Full, Cancelled, JobFailed };

// slice < 0: always Start..End. A slice is intersected with Start..End;
// unlike playback's last-slice fallback, an empty intersection is refused.
bool sampleExtractRegion(const Instrument& in, uint32_t frames, int slice, uint32_t& from, uint32_t& to);
// A free project name, case-insensitive, using _1, _2, ... with a bounded base.
bool sampleExtractName(const Project& p, const char* base, char out[kSampleNameMax + 1]);
SampleExtractResult sampleExtractCheck(const Project& p, const SampleExtractTarget& target, const char* name);
// Caller holds the project lock. No mutation until every check and the whole job succeed.
SampleExtractResult sampleExtractCommit(Project& p, const SampleExtractTarget& target, const char* name,
                                        SampleExtractResult result, const SampleExtractOutput& out, bool cancelled = false);

}  // namespace mt
