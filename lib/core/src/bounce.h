#pragma once
#include <stdint.h>
#include "link_msg.h"
#include "model.h"
#include "synth.h"

// One-shot bounce: a note of an instrument rendered into a project sample. The ESP's side of the
// render: the event sequence (RenderBlocks frames) of the note on kPreviewTrack. The synth board
// renders it with the preview track's tap as the source (RenderStartReq::kSrcPreview).
namespace mt {

struct BounceSpec {
  uint8_t instr = 0, note = 60, vel = 127;
  uint8_t insTrack = kTracks;  // the insert of this track, kTracks = dry
  uint32_t holdBlocks = 172;   // note-off after: 0.5 s (bounceHoldBlocks)
  uint32_t maxBlocks = 3446;   // 10 s
  uint32_t quietStop = 18;     // ~50 ms below -60 dBFS after the note-off ends the render
};

// Block 0 (offset 0, kPreviewTrack): 0xF7 insTrack, 0xC0 instr, 0x90 note vel; block holdBlocks:
// 0x80 note 0; the other blocks have no events, kMaxBlocks to a frame. After the note-off has been
// rendered, a reply's quiet >= quietStop (RenderRep::quiet) ends the render; maxBlocks always does.
class BounceSeq {
 public:
  explicit BounceSeq(const BounceSpec& s) : s_(s) {}
  // The next frame; quiet: RenderRep::quiet of the last reply (0 before the first). false: done.
  bool next(link::RenderBlocks& f, uint32_t quiet);
  uint32_t blocksSent() const { return sent_; }
  bool hitMax() const { return max_; }

 private:
  BounceSpec s_;
  uint32_t sent_ = 0;
  bool max_ = false;
};

// HOLD seconds (clamped 0.05..5) in synth blocks.
uint32_t bounceHoldBlocks(float seconds);
// "<base>_N" (a sample name has no spaces: projectBaseValid): the first N >= 1 not in p's sample list
// (ignoring case); base's other characters -> '_', cut so the name fits kSampleNameMax; no base: "SHOT_N".
void nextBounceName(const Project& p, const char* base, char out[kSampleNameMax + 1]);

}  // namespace mt
