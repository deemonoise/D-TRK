#pragma once
#include "sample_extract.h"
#include "tempo_detect.h"

namespace audio {
enum class SampleToolResult : uint8_t {
  Ok, NoSd, NoBank, BadSource, BadName, NameTaken, Full, Busy, Cancelled,
  ReadFail, WriteFail, NoMemory, BadRatio
};
using SampleToolProgress = bool (*)(uint32_t done, uint32_t total, void* ctx);
const char* sampleToolResultText(SampleToolResult result);

// UI task, offline. The new project WAV and binding appear only after processing,
// verification and cache import succeed. The original file and binding survive every error.
SampleToolResult extractSample(mt::Project& project, const mt::SampleExtractTarget& target,
                               const char* name, bool normalize,
                               SampleToolProgress progress = nullptr, void* ctx = nullptr);
SampleToolResult stretchSample(mt::Project& project, const mt::SampleExtractTarget& target,
                               const char* name, SampleToolProgress progress = nullptr, void* ctx = nullptr);
SampleToolResult detectSampleTempo(const mt::Project& project, const mt::SampleExtractTarget& target,
                                   mt::TempoEstimate& estimate,
                                   SampleToolProgress progress = nullptr, void* ctx = nullptr);
}
