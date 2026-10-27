#pragma once
#include <stdint.h>
#include "bounce.h"
#include "export.h"
#include "render.h"
#include "storage.h"

namespace storage {

// Offline renders of the internal synth, on the synth board (RenderStart / RenderBlocks / RenderEnd):
// this side runs the sequence (mt::OfflineSequence) and sends each block's events; the board renders
// them into its card or its sample bank. One driver for all (render_io.cpp: drive / precheck). Playback
// must be stopped (the UI stops it, ui::renderReady: Result::EngineBusy otherwise); the card ready
// (NoSd), room on the synth board's card (DiskFull), a free slot in the sample list for the bank
// (BankFull). The project is locked for the whole render; cb reports progress in blocks (one-shot: in
// frames, total 0 - the length is known at the end) and returns false to cancel. Result::NoSynth when
// the synth board does not answer; AudioBusy when it is busy (its bank, a REC take).
using RenderProgress = bool (*)(uint32_t done, uint32_t total, void* ctx);
struct RenderStats {
  uint32_t frames;
  int16_t peak;
  uint32_t clips;
  bool copied;  // bounce: the copy into /samples/oneshots made
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
// Renders plan (mt::resamplePlan: one loop, the tail wrapped into its start) into the sample bank (via a
// temporary file on the card): the mono mix as played (no normalizing, no trim), added to p's sample
// list as RSn (name out). Silent: nothing above -60 dBFS, nothing added. plan.capped (over
// kResampleMaxFrames): Result::Capped (added, but cut).
Result resample(mt::Project& p, const mt::ResamplePlan& plan, char name[mt::kSampleNameMax + 1], RenderStats& st,
                RenderProgress cb, void* ctx);
// One-shot (Save one-shot...): a note of spec.instr into the sample bank. The synth board renders the
// preview track's tap (spec.insTrack's insert or dry, fader, pan; no sends, no master) until silence
// after the note-off or 10 s (mt::BounceSeq), trimmed, normalized to -0.3 dBFS (spec.normalize; off: the
// level as played, the preview fader undone - mt::kPrevTapScale); added to p's sample
// list as name. Then copied on the synth board's card to /samples/oneshots/<mt::oneshotDir of dirOf
// (nullptr: spec.instr)>/<name>.wav (_1, _2.. when taken): st.copied; a failed copy is not an error.
// The transport must be stopped (EngineBusy). The board is waited for up to 2 s (busy with a copy
// before). Progress: frames rendered, total 0. Errors: BankFull (the list or the bank), NoSd, AudioBusy, NoSynth, Silent (nothing above
// -60 dBFS: nothing added), Cancelled (cb).
Result bounce(mt::Project& p, const mt::BounceSpec& spec, const char* name, RenderStats& st, RenderProgress cb,
              void* ctx, const mt::Instrument* dirOf = nullptr);

}  // namespace storage
