#pragma once
#include <stdint.h>
#include "bounce.h"
#include "export.h"
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

constexpr uint32_t kResampleMaxFrames = 60 * mt::kSynthRate;  // 60 s

// Export (FILE -> PROJECTS -> Export...): spec's master and stems into /export/<project>/<src>/ on the
// synth board's card (mt::exportOutputs names them), all files one length. Passes: as many files per
// pass as the board takes (RenderStart's outMax); every pass renders the same blocks (deterministic), the
// first one decides the length (AUTO tail). Result::DiskFull when the estimate does not fit the card.
// Cancel / error: the files of this run removed. Success: the folder's other .wav files (an older set)
// removed. cb: pass (0-based) of passes, blocks done / total of the pass; false cancels.
struct ExportStats {
  uint32_t frames = 0;
  int files = 0, passes = 0;
  uint16_t peak = 0;
  uint32_t clips = 0;
  int clipFile = -1;  // the first file with clipped samples (index into mt::exportOutputs)
};
using ExportProgress = bool (*)(int pass, int passes, uint32_t done, uint32_t total, void* ctx);
Result exportSet(mt::Project& p, const mt::ExportSpec& spec, ExportStats& st, ExportProgress cb, void* ctx);
// Renders spec into the sample bank (via a temporary file on the card): the mono mix, cut after the
// last block above -60 dBFS and normalized to -1 dBFS, added to p's sample list as RSn (name out). At
// most kResampleMaxFrames: Result::Capped (added, but cut).
Result resample(mt::Project& p, const mt::RenderSpec& spec, char name[mt::kSampleNameMax + 1], RenderStats& st,
                RenderProgress cb, void* ctx);
// One-shot (Save one-shot...): a note of spec.instr into the sample bank. The synth board renders the
// preview track's tap (spec.insTrack's insert or dry, fader, pan; no sends, no master) until silence
// after the note-off or 10 s (mt::BounceSeq), trimmed, normalized to -0.3 dBFS; added to p's sample
// list as name. The transport must be stopped (EngineBusy). Errors: BankFull (the list or the bank),
// NoSd, AudioBusy, NoSynth, Silent (nothing above -60 dBFS: nothing added), Cancelled (cb).
Result bounce(mt::Project& p, const mt::BounceSpec& spec, const char* name, RenderStats& st, RenderProgress cb,
              void* ctx);

}  // namespace storage
