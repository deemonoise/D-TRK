#pragma once
#include <stdint.h>
#include "model.h"
#include "synth.h"

// One-shot bounce: a note of an instrument rendered into a project sample. The note on kPreviewTrack,
// rendered locally with the preview tap (Synth::setPreviewTap) as the source.
namespace mt {

constexpr uint32_t kBounceBlocksPerSec = kSynthRate / Synth::kBlock;  // 250

struct BounceSpec {
  uint8_t instr = 0, note = 60, vel = 127;
  uint8_t insTrack = kTracks;                       // the insert of this track, kTracks = dry
  uint32_t holdBlocks = kBounceBlocksPerSec / 2;    // note-off after: 0.5 s (bounceHoldBlocks)
  uint32_t maxBlocks = kBounceBlocksPerSec * 10;    // 10 s
  uint32_t quietStop = (kBounceBlocksPerSec * 50 + 999) / 1000;  // ~50 ms below -60 dBFS after the note-off ends the render
  bool normalize = true;  // the peak to -0.3 dBFS (kNormPeakHot); false: the level as played (KIT bake)
};

// One message for Synth::event(0, track, b, len).
struct BounceEv {
  uint8_t track, len, b[3];
};

// The note's events and stop rule, block by block. Block 0: 0xF7 insTrack, 0xC0 instr, 0x90 note vel;
// block holdBlocks: 0x80 note 0 (with holdBlocks 0 after the note-on); the other blocks have none.
// After the note-off block has been rendered, quietBlocks >= quietStop ends the render; maxBlocks
// always does.
class BounceNotes {
 public:
  static constexpr int kMaxEvents = 4;
  explicit BounceNotes(const BounceSpec& s) : s_(s) {}
  // Events to send (offset 0) before rendering block `block`, into out; their count.
  int events(uint32_t block, BounceEv out[kMaxEvents]) const;
  // Before rendering block `block` (= blocks rendered so far): true when the render ends.
  // quietBlocks: the rendered blocks since the last one with a sample above kSilence.
  bool done(uint32_t block, uint32_t quietBlocks);
  bool hitMax() const { return max_; }
  const BounceSpec& spec() const { return s_; }

 private:
  BounceSpec s_;
  bool max_ = false;
};

// The render of a BounceSpec through the preview tap, block by block: storage::bounce runs it twice
// (measure, then write the kept frames), both passes sample-identical. The constructor resets the
// synth (reset + reseed) and sets the tap to its own buffers, the destructor takes the tap off; the
// caller keeps everything else off the synth (audio task parked) and the project still.
class BounceRender {
 public:
  BounceRender(Synth& s, const BounceSpec& spec);
  ~BounceRender();
  BounceRender(const BounceRender&) = delete;
  BounceRender& operator=(const BounceRender&) = delete;
  // Renders the next block into mono: (L + R) / 2 of the tap x kPrevTapScale, each side saturated to
  // int16 (the level as played: a sampler at vol 127 replays it). False when the render has ended
  // (BounceNotes::done: silence after the note-off, or maxBlocks) or the synth refused an event
  // (failed()), nothing rendered.
  bool next(int16_t mono[Synth::kBlock]);
  bool failed() const { return failed_; }  // Synth::event dropped one of the note's messages
  uint32_t blocks() const { return n_; }   // rendered so far
  int16_t peak() const { return peak_; }   // |mono| max
  bool silent() const { return loudLR_ == 0; }  // no L / R sample above kSilence
  bool hitMax() const { return notes_.hitMax(); }
  // Frames to keep: through the last block with a mono sample above kSilence, one block at least.
  uint32_t keepFrames() const { return (loud_ ? loud_ : 1) * static_cast<uint32_t>(Synth::kBlock); }

 private:
  Synth& s_;
  BounceNotes notes_;
  float l_[Synth::kBlock], r_[Synth::kBlock];
  uint32_t n_ = 0, loudLR_ = 0, loud_ = 0;  // blocks; the last loud ones (1-based, 0 = none)
  int16_t peak_ = 0;
  bool failed_ = false;
};

// HOLD seconds (clamped 0.05..5) in synth blocks.
uint32_t bounceHoldBlocks(float seconds);
// "<base>_N" (a sample name has no spaces: projectBaseValid): the first N >= 1 not in p's sample list
// (ignoring case); base's other characters -> '_', cut so the name fits kSampleNameMax; no base: "SHOT_N".
void nextBounceName(const Project& p, const char* base, char out[kSampleNameMax + 1]);

}  // namespace mt
