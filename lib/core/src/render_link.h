#pragma once
#include <stdint.h>
#include "link_msg.h"
#include "render.h"

namespace mt {

// The ESP's end of a render on the synth board: the events of seq's blocks (up to maxBlocks) packed
// into RenderBlocks frames, up to kMaxBlocks blocks each; a block with more events than fit one frame
// goes on in the next (kCont).
class RenderPacker {
 public:
  explicit RenderPacker(OfflineSequence& seq, uint32_t maxBlocks = 0xFFFFFFFFu) : seq_(seq), max_(maxBlocks) {}
  // The next frame; false when every block is out (m.n = 0).
  bool next(link::RenderBlocks& m);
  uint32_t blocksPacked() const { return packed_; }  // whole blocks

 private:
  OfflineSequence& seq_;
  uint32_t max_, packed_ = 0;
  SeqEv ev_[OfflineSequence::kMaxEvents];
  int n_ = 0, pos_ = 0;
  bool have_ = false;
};

// The synth board's end: RenderBlocks into the synth (as OfflineRender does), each completed block
// rendered and handed to out. The caller resets the synth with reset().
class RenderFeeder {
 public:
  // l, r: n frames of one block (kBlock, fewer in the block where a skip ends); false stops the render
  // (the write failed).
  using Out = bool (*)(const int16_t* l, const int16_t* r, int n, void* ctx);
  explicit RenderFeeder(Synth& s) : synth_(s) {}
  void reset();
  // Tap mode (one-shot bounce): out() and the stats get tap k of t (x kTapScale, the preview tap
  // x kPrevTapScale; saturated) instead of the master; t = nullptr: the master. The caller sets t as the synth's taps (Synth::setTaps).
  void setTap(const RenderTaps* t, int k) {
    tap_ = t;
    tapK_ = k;
  }
  // The first frames of the render (Resample's wrap pass, RenderStartReq::skipFrames) are rendered but
  // neither go out nor count in the stats (peak, clips, loud blocks). reset() clears it.
  void setSkip(uint32_t frames) { skip_ = frames; }
  // Ok; BadOrder when m does not start at the block being filled (nothing applied); WriteFail when
  // out said stop.
  link::RenderResult apply(const link::RenderBlocks& m, Out out, void* ctx);
  uint32_t blocks() const { return blocks_; }  // rendered
  int16_t peak() const { return stats_.peak; }
  uint32_t clips() const { return stats_.clips; }
  int16_t monoPeak() const { return monoPeak_; }  // of (L + R) / 2
  // Blocks up to the last one with a sample above kSilence (0 = all silent): stereo / the mono mix.
  uint32_t loudBlocks() const { return loud_; }
  uint32_t loudBlocksMono() const { return loudMono_; }

 private:
  Synth& synth_;
  BlockRender stats_;
  uint32_t blocks_ = 0, loud_ = 0, loudMono_ = 0, skip_ = 0;
  int16_t monoPeak_ = 0;
  const RenderTaps* tap_ = nullptr;
  int tapK_ = 0;
};

}  // namespace mt
