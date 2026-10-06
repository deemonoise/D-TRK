#pragma once
#include <stdint.h>
#include "render.h"
#include "storage.h"

namespace storage {

// Offline renders of the internal synth (see mt::OfflineRender). Playback must be stopped (the
// caller checks: Result::EngineBusy otherwise). The audio task is parked and the project locked for
// the whole render; cb reports progress in blocks and returns false to cancel.
using RenderProgress = bool (*)(uint32_t done, uint32_t total, void* ctx);
struct RenderStats {
  uint32_t frames;
  int16_t peak;
  uint32_t clips;
};

constexpr const char* kRenderDir = "/samples/render";  // the Wi-Fi page lists it; files import as samples
constexpr uint32_t kResampleMaxFrames = 60 * 32000;    // 60 s

// path of the WAV of spec for project p: /samples/render/<project>_P01.wav or <project>_SONG.wav.
void renderPath(const mt::Project& p, const mt::RenderSpec& spec, char* out, int n);
// Renders spec into path (mono 16-bit 32 kHz WAV with "mtcr" crc, written via path.tmp).
Result renderWav(mt::Project& p, const mt::RenderSpec& spec, const char* path, RenderStats& st, RenderProgress cb,
                 void* ctx);
// Renders spec twice: measures (peak, the last block above -60 dBFS), then writes it normalized to
// -1 dBFS into the sample bank and p's sample list as RSn (name out). At most kResampleMaxFrames:
// Result::Capped (added, but cut).
Result resample(mt::Project& p, const mt::RenderSpec& spec, char name[mt::kSampleNameMax + 1], RenderStats& st,
                RenderProgress cb, void* ctx);

}  // namespace storage
