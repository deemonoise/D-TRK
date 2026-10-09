#pragma once
#include <stdint.h>
#include "render.h"
#include "storage.h"

namespace storage {

// Offline renders of the internal synth, on the synth board (RenderStart / RenderBlocks / RenderEnd):
// this side runs the sequence (mt::OfflineSequence) and sends each block's events; the board renders
// them into its card or its sample bank. Playback must be stopped (the caller checks:
// Result::EngineBusy otherwise). The project is locked for the whole render; cb reports progress in
// blocks and returns false to cancel. Result::NoSynth when the synth board does not answer.
using RenderProgress = bool (*)(uint32_t done, uint32_t total, void* ctx);
struct RenderStats {
  uint32_t frames;
  int16_t peak;
  uint32_t clips;
};

constexpr const char* kRenderDir = "/samples/render";  // FILE -> SAMPLES lists it; files import as samples
constexpr uint32_t kResampleMaxFrames = 60 * mt::kSynthRate;  // 60 s

// path of the WAV of spec for project p: /samples/render/<project>_P01.wav or <project>_SONG.wav.
// stem >= 0: one track's file, "_T03" appended (track 3).
void renderPath(const mt::Project& p, const mt::RenderSpec& spec, char* out, int n, int stem = -1);
// Renders spec into path (stereo 16-bit 44.1 kHz WAV, "mtcr" = the mono mix's crc, written via
// path.tmp on the synth board's card).
Result renderWav(mt::Project& p, const mt::RenderSpec& spec, const char* path, RenderStats& st, RenderProgress cb,
                 void* ctx);
// Renders spec into the sample bank (via a temporary file on the card): the mono mix, cut after the
// last block above -60 dBFS and normalized to -1 dBFS, added to p's sample list as RSn (name out). At
// most kResampleMaxFrames: Result::Capped (added, but cut).
Result resample(mt::Project& p, const mt::RenderSpec& spec, char name[mt::kSampleNameMax + 1], RenderStats& st,
                RenderProgress cb, void* ctx);

}  // namespace storage
