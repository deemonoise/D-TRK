#include "render_link.h"
#include <string.h>

namespace mt {

using link::RenderBlocks;
using link::RenderResult;

namespace {
int16_t sat(float v) {
  return static_cast<int16_t>(v > 32767.f ? 32767 : (v < -32768.f ? -32768 : (v < 0 ? v - 0.5f : v + 0.5f)));
}
}  // namespace

bool RenderPacker::next(RenderBlocks& m) {
  m.first = packed_;
  m.n = 0;
  int used = 0;
  while (m.n < RenderBlocks::kMaxBlocks && used < RenderBlocks::kMaxEvents) {
    if (!have_) {
      if (packed_ >= max_ || !seq_.nextBlock(ev_, n_)) break;
      pos_ = 0;
      have_ = true;
    }
    const int left = n_ - pos_;
    const int room = RenderBlocks::kMaxEvents - used;
    const int take = left < room ? left : room;
    for (int i = 0; i < take; ++i) {
      const SeqEv& e = ev_[pos_ + i];
      link::RenderEv& o = m.ev[used + i];
      o.off = e.off;
      o.track = e.track;
      o.len = e.len;
      memcpy(o.b, e.b, sizeof o.b);
    }
    used += take;
    pos_ += take;
    m.evN[m.n++] = static_cast<uint8_t>(take | (pos_ < n_ ? RenderBlocks::kCont : 0));
    if (pos_ < n_) break;  // goes on in the next frame
    have_ = false;
    ++packed_;
  }
  return m.n > 0;
}

void RenderFeeder::reset() {
  stats_ = BlockRender();
  blocks_ = loud_ = loudMono_ = skip_ = 0;
  monoPeak_ = 0;
}

RenderResult RenderFeeder::apply(const RenderBlocks& m, Out out, void* ctx) {
  if (m.first != blocks_) return RenderResult::BadOrder;
  const link::RenderEv* ev = m.ev;
  for (int i = 0; i < m.n; ++i) {
    const int n = m.evN[i] & ~RenderBlocks::kCont;
    for (int k = 0; k < n; ++k, ++ev) synth_.event(ev->off, ev->track, ev->b, ev->len);
    if (m.evN[i] & RenderBlocks::kCont) break;
    int16_t l[Synth::kBlock], r[Synth::kBlock];
    synth_.render(l, r);
    if (tap_) {
      const float g = tapK_ == RenderTaps::kPrev ? kPrevTapScale : kTapScale;
      for (int k = 0; k < Synth::kBlock; ++k) {
        l[k] = sat(tap_->l[tapK_][k] * g);
        r[k] = sat(tap_->r[tapK_][k] * g);
      }
    }
    const uint64_t at = static_cast<uint64_t>(blocks_) * Synth::kBlock;
    const int from = skip_ <= at ? 0 : (skip_ - at >= Synth::kBlock ? Synth::kBlock : static_cast<int>(skip_ - at));
    ++blocks_;
    if (from == Synth::kBlock) continue;  // skipped
    stats_.measure(l + from, r + from, Synth::kBlock - from);
    for (int k = from; k < Synth::kBlock; ++k) {
      if (l[k] > kSilence || l[k] < -kSilence || r[k] > kSilence || r[k] < -kSilence) loud_ = blocks_;
      const int mono = (l[k] + r[k]) / 2;
      const int a = mono < 0 ? -mono : mono;
      if (a > monoPeak_) monoPeak_ = static_cast<int16_t>(a);
      if (a > kSilence) loudMono_ = blocks_;
    }
    if (!out(l + from, r + from, Synth::kBlock - from, ctx)) return RenderResult::WriteFail;
  }
  return RenderResult::Ok;
}

}  // namespace mt
