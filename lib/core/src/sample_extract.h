#pragma once
#include "bank_ops.h"

namespace mt {

// Immutable source identity captured before the off-line extraction RPC.
struct SampleExtractTarget {
  int instrument = -1, index = -1;
  ProjSample source;
  uint32_t from = 0, to = 0;
  uint32_t ratioQ16 = 65536;  // Exact copy by default; Stretch 0.5..2.
};

// Zero for invalid/overflowing duration. Shared wire/UI rounding.
uint32_t sampleEditFrames(uint32_t frames, uint32_t ratioQ16);
bool sampleStretchRatio(int sourceBpm10, int targetBpm10, uint32_t& ratioQ16);

enum class SampleExtractResult : uint8_t { Ok, BadSource, BadName, NameTaken, Full, Cancelled, JobFailed };

// slice < 0: always Start..End. A slice is intersected with Start..End;
// unlike playback's last-slice fallback, an empty intersection is refused.
bool sampleExtractRegion(const Instrument& in, uint32_t frames, int slice, uint32_t& from, uint32_t& to);
// A free project name, case-insensitive, using _1, _2, ... with a bounded base.
bool sampleExtractName(const SynthModel& p, const char* base, char out[kSampleNameMax + 1]);
SampleExtractResult sampleExtractCheck(const SynthModel& p, const SampleExtractTarget& target, const char* name);
// Caller holds the project lock. No mutation until every check and the whole job succeed.
SampleExtractResult sampleExtractCommit(Project& p, const SampleExtractTarget& target, const char* name,
                                        BankResult result, const ImportOut& out, bool cancelled = false);

}  // namespace mt
