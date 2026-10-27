#pragma once
#include <stdint.h>
#include "bounce.h"
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
  bool copied;  // bounce: the copy into /samples/oneshots made
};

constexpr const char* kRenderDir = "/samples/render";  // the Wi-Fi page lists it; files import as samples
constexpr uint32_t kResampleMaxFrames = 60 * 32000;    // 60 s

// path of the WAV of spec for project p: /samples/render/<project>_P01.wav or <project>_SONG.wav.
// stem >= 0: one track's file, "_T03" appended (track 3).
void renderPath(const mt::Project& p, const mt::RenderSpec& spec, char* out, int n, int stem = -1);
// Renders spec into path (stereo 16-bit 32 kHz WAV with "mtcr" crc, written via path.tmp).
Result renderWav(mt::Project& p, const mt::RenderSpec& spec, const char* path, RenderStats& st, RenderProgress cb,
                 void* ctx);
// Renders spec twice: measures (peak, the last block above -60 dBFS), then writes it normalized to
// -1 dBFS into the sample bank and p's sample list as RSn (name out). At most kResampleMaxFrames:
// Result::Capped (added, but cut).
Result resample(mt::Project& p, const mt::RenderSpec& spec, char name[mt::kSampleNameMax + 1], RenderStats& st,
                RenderProgress cb, void* ctx);
// One-shot (Save one-shot...): a note of spec.instr into the sample bank, rendered locally from the
// preview track's tap (spec.insTrack's insert or dry, fader, pan; no sends, no master) until silence
// after the note-off or spec.maxBlocks (mt::BounceRender). Two passes: the first measures (the peak,
// the last block above -60 dBFS), the second writes the kept frames, normalized to -0.3 dBFS
// (spec.normalize; off: the level as played, the preview fader undone - mt::kPrevTapScale, clipped).
// Added to p's sample list as name (an existing entry replaced). Then, when libCopy, written to
// /samples/oneshots/<mt::oneshotDir of dirOf (nullptr: spec.instr)>/<name>.wav (_1, _2.. when
// taken): st.copied; a failed copy is not an error. The transport must be stopped (EngineBusy, also
// checked under the project lock). Capped: still sounding at spec.maxBlocks (10 s), added (and
// copied) cut there - a success. Errors: NoBank, BankFull (the list or the bank), AudioBusy (audio
// not parked, or pass 2 did not repeat pass 1: nothing added), NoMemory, Silent (nothing above
// -60 dBFS: nothing added), Cancelled (cb).
Result bounce(mt::Project& p, const mt::BounceSpec& spec, const char* name, RenderStats& st, RenderProgress cb,
              void* ctx, const mt::Instrument* dirOf = nullptr, bool libCopy = true);

}  // namespace storage
