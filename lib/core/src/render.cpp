#include "render.h"
#include <stdio.h>
#include <string.h>
#include "sample_set.h"

namespace mt {

uint64_t passUs(const Project& p, int idx) {
  const Pattern& pt = p.patterns[idx >= 0 && idx < kPatterns ? idx : 0];
  const uint64_t len = pt.length < kMinSteps ? kMinSteps : (pt.length > kMaxSteps ? kMaxSteps : pt.length);
  const uint16_t bpm = p.bpm < 20 ? 20 : (p.bpm > 300 ? 300 : p.bpm);  // as the sequencer clamps
  return len * ticksPerStep(pt.res) * 625000ull / bpm;
}

uint64_t songUs(const Project& p) {
  uint64_t us = 0;
  const int n = p.chainLen > kChainMax ? kChainMax : p.chainLen;
  for (int i = 0; i < n; ++i) {
    const int rep = p.chainRep[i] < 1 ? 1 : (p.chainRep[i] > kChainRepMax ? kChainRepMax : p.chainRep[i]);
    us += passUs(p, p.chain[i] < kPatterns ? p.chain[i] : kPatterns - 1) * rep;
  }
  return us;
}

void normalizePeak(int16_t* b, uint32_t n, int16_t peak) {
  if (peak <= 0) return;
  const float g = kNormPeak / peak;
  for (uint32_t i = 0; i < n; ++i) {
    const float v = b[i] * g;
    b[i] = static_cast<int16_t>(v > 32767.f ? 32767 : (v < -32768.f ? -32768 : (v < 0 ? v - 0.5f : v + 0.5f)));
  }
}

uint32_t trimTail(const int16_t* b, uint32_t n, uint32_t block) {
  uint32_t last = 0;
  for (uint32_t i = 0; i < n; ++i)
    if (b[i] > kSilence || b[i] < -kSilence) last = i + 1;
  const uint32_t keep = (last + block - 1) / block * block;
  return keep < block ? block : (keep > n ? n : keep);
}

void nextResampleName(const Project& p, char out[kSampleNameMax + 1]) {
  for (int n = 1;; ++n) {
    snprintf(out, kSampleNameMax + 1, "RS%d", n);
    if (projSampleFind(p, out) < 0) return;
  }
}

OfflineRender::Guard::Guard(Project& pr, const RenderSpec& s) : p(pr), songMode(pr.songMode) {
  for (int t = 0; t < kTracks; ++t) mute[t] = p.tracks[t].mute;
  p.songMode = s.mode == RenderSpec::Mode::Song;
}

OfflineRender::Guard::~Guard() {
  p.songMode = songMode;
  for (int t = 0; t < kTracks; ++t) p.tracks[t].mute = mute[t];
}

OfflineRender::OfflineRender(Project& p, Synth& synth, const RenderSpec& spec)
    : synth_(synth), spec_(spec), seq_(p) {
  seq_.setTrackMask(spec.tracksMask);
  seq_.seed(0x5EED);
  if (spec.mode == RenderSpec::Mode::Pattern) seq_.queuePattern(spec.pattern < kPatterns ? spec.pattern : 0);
  bodyUs_ = spec.mode == RenderSpec::Mode::Song ? songUs(p) : passUs(p, spec.pattern);
  bodyBlocks_ = static_cast<uint32_t>((bodyUs_ + kRenderBlockUs - 1) / kRenderBlockUs);
  seq_.start(0, *this);
}

// Note-offs and releases: what ends sounding notes. Everything else of the next pass is dropped.
bool OfflineRender::passesAfterEnd(const uint8_t* b, uint8_t len) {
  const uint8_t k = b[0] & 0xF0;
  return k == 0x80 || (k == 0x90 && len >= 3 && b[2] == 0) || b[0] == 0xFF;
}

void OfflineRender::synth(uint64_t t, uint8_t track, const uint8_t* b, uint8_t len) {
  if (len == 0 || len > 3) return;
  if (t >= bodyUs_ && !passesAfterEnd(b, len)) return;
  if (fifoN_ == kFifo) return;  // never with one block of lookahead; the synth's own queue is smaller
  Ev& e = fifo_[fifoN_++];
  e.t = t;
  e.track = track;
  e.len = len;
  memcpy(e.b, b, len);
}

// As audio.cpp's drain: events of this block (late ones at its start) go in, later ones wait.
void OfflineRender::drain() {
  int kept = 0;
  for (int i = 0; i < fifoN_; ++i) {
    const int off = eventOffset(fifo_[i].t, blockT_);
    if (off < 0) {
      fifo_[kept++] = fifo_[i];
      continue;
    }
    synth_.event(off, fifo_[i].track, fifo_[i].b, fifo_[i].len);
  }
  fifoN_ = kept;
}

bool OfflineRender::renderBlock(int16_t out[Synth::kBlock]) {
  if (done_ >= blocksTotal()) return false;
  if (done_ < bodyBlocks_) seq_.process(blockT_ + kRenderBlockUs, *this);  // everything of this block
  else if (done_ == bodyBlocks_) seq_.stop(blockT_, *this);                // the end: release all
  drain();
  synth_.render(out);
  for (int i = 0; i < Synth::kBlock; ++i) {
    const int a = out[i] < 0 ? -out[i] : out[i];
    if (a > peak_) peak_ = static_cast<int16_t>(a > 32767 ? 32767 : a);
    if (out[i] >= 32767 || out[i] <= -32767) ++clips_;
  }
  blockT_ += kRenderBlockUs;
  ++done_;
  return true;
}

}  // namespace mt
