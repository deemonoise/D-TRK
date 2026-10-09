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
  // l, r: one block; false stops the render (the write failed).
  using Out = bool (*)(const int16_t* l, const int16_t* r, void* ctx);
  explicit RenderFeeder(Synth& s) : synth_(s) {}
  void reset();
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
  uint32_t blocks_ = 0, loud_ = 0, loudMono_ = 0;
  int16_t monoPeak_ = 0;
};

}  // namespace mt
