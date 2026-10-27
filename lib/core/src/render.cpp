#include "render.h"
#include <stdio.h>
#include <string.h>
#include "follow.h"
#include "sample_set.h"

namespace mt {

uint64_t ticksToUs(const Project& p, uint64_t ticks) {
  const uint16_t bpm = p.bpm < 20 ? 20 : (p.bpm > 300 ? 300 : p.bpm);  // as the sequencer clamps
  return ticks * 625000ull / bpm;
}

uint64_t passTicks(const Project& p, int idx) {
  const Pattern& pt = p.patterns[idx >= 0 && idx < kPatterns ? idx : 0];
  const uint64_t len = pt.length < kMinSteps ? kMinSteps : (pt.length > kMaxSteps ? kMaxSteps : pt.length);
  return len * ticksPerStep(pt.res);
}

uint64_t songTicks(const Project& p, int maxSlots) {
  uint64_t ticks = 0;
  followWalk(p, followRunSeed(p.seed, 0), maxSlots,
             [&](int slot) { ticks += passTicks(p, p.song[slot] < kPatterns ? p.song[slot] : kPatterns - 1); });
  return ticks;
}

uint64_t passUs(const Project& p, int idx) { return ticksToUs(p, passTicks(p, idx)); }

void renderDoneText(const char* name, uint32_t frames, uint32_t clips, char* out, int n) {
  if (n <= 0) return;
  int k = snprintf(out, n, "%s %lu.%lus", name ? name : "", static_cast<unsigned long>(frames / kSynthRate),
                   static_cast<unsigned long>(frames % kSynthRate / (kSynthRate / 10)));
  if (clips && k >= 0 && k < n)
    snprintf(out + k, n - k, ", %lu CLIP%s", static_cast<unsigned long>(clips), clips == 1 ? "" : "S");
}

uint64_t trackLoopTicks(const Pattern& pt, int track) {
  const uint64_t len = pt.length < kMinSteps ? kMinSteps : (pt.length > kMaxSteps ? kMaxSteps : pt.length);
  const uint64_t pass = len * ticksPerStep(pt.res);
  if (track < 0 || track >= kTracks) return pass;
  const TrackSpeed s = toSpeed(pt.trackSpeed[track]);
  if (speedDiv(s) > 1) return pass;
  const uint8_t n = pt.trackLen[track];
  const uint64_t steps = n && n < len ? n : len;
  const uint64_t ticks = steps * ticksPerStep(pt.res) / static_cast<uint64_t>(speedMul(s));
  return ticks && ticks < pass ? ticks : pass;
}

// A slot's BPM is the tempo from its start ("-" keeps it), as the sequencer plays it.
uint64_t songUs(const Project& p, int maxSlots) {
  uint64_t us = 0, ticks = 0;  // ticks: since the last tempo change
  uint16_t bpm = p.bpm < 20 ? 20 : (p.bpm > 300 ? 300 : p.bpm);
  followWalk(p, followRunSeed(p.seed, 0), maxSlots, [&](int slot) {
    const uint16_t b = songBpmClamp(p.songBpm[slot]);
    if (b && b != bpm) {
      us += ticks * 625000ull / bpm;
      ticks = 0;
      bpm = b;
    }
    ticks += passTicks(p, p.song[slot] < kPatterns ? p.song[slot] : kPatterns - 1);
  });
  return us + ticks * 625000ull / bpm;
}

void normalizePeak(int16_t* b, uint32_t frames, int16_t peak, float target) {
  normalizeSamples(b, 2 * frames, peak, target);
}

void normalizeSamples(int16_t* b, uint32_t n, int16_t peak, float target) {
  if (peak <= 0) return;
  const float g = target / peak;
  for (uint32_t i = 0; i < n; ++i) {
    const float v = b[i] * g;
    b[i] = static_cast<int16_t>(v > 32767.f ? 32767 : (v < -32768.f ? -32768 : (v < 0 ? v - 0.5f : v + 0.5f)));
  }
}

uint32_t trimTail(const int16_t* b, uint32_t n, uint32_t block) {
  uint32_t last = 0;
  for (uint32_t i = 0; i < 2 * n; ++i)
    if (b[i] > kSilence || b[i] < -kSilence) last = i / 2 + 1;
  const uint32_t keep = (last + block - 1) / block * block;
  return keep < block ? block : (keep > n ? n : keep);
}

void nextResampleName(const Project& p, char out[kSampleNameMax + 1]) {
  for (int n = 1;; ++n) {
    snprintf(out, kSampleNameMax + 1, "RS%d", n);
    if (projSampleFind(p, out) < 0) return;
  }
}

OfflineSequence::Guard::Guard(Project& pr) : p(pr), bpm(pr.bpm) {
  for (int t = 0; t < kTracks; ++t) mute[t] = p.tracks[t].mute;
}

OfflineSequence::Guard::~Guard() {
  for (int t = 0; t < kTracks; ++t) p.tracks[t].mute = mute[t];
  p.bpm = bpm;  // a slot's BPM
}

OfflineSequence::OfflineSequence(Project& p, const RenderSpec& spec) : spec_(spec), seq_(p) {
  const int slots = spec.maxSlots < 1 ? 1 : (spec.maxSlots > kRenderSlotsMax ? kRenderSlotsMax : spec.maxSlots);
  seq_.setTrackMask(spec.tracksMask);
  seq_.seed(0x5EED);
  seq_.setFollowSalt(0);  // the walk's path
  seq_.setSong(0, *this, spec.mode == RenderSpec::Mode::Song, 0);
  if (spec.mode == RenderSpec::Mode::Pattern) seq_.queuePattern(spec.pattern < kPatterns ? spec.pattern : 0);
  const uint64_t loops = spec.mode == RenderSpec::Mode::Song || spec.loops < 1 ? 1 : spec.loops;
  const uint64_t pass = passTicks(p, spec.pattern);
  const uint64_t cut = spec.mode == RenderSpec::Mode::Pattern && spec.cutTicks && spec.cutTicks < pass ? spec.cutTicks : 0;
  bodyUs_ = spec.mode == RenderSpec::Mode::Song ? songUs(p, slots)
            : cut ? passUs(p, spec.pattern) * (loops - 1) + ticksToUs(p, cut)
                  : passUs(p, spec.pattern) * loops;
  bodyBlocks_ = static_cast<uint32_t>((bodyUs_ + kRenderBlockUs - 1) / kRenderBlockUs);
  // Steps of the next pass are never planned (a negative NDG would land inside the body).
  seq_.setEndTick(spec.mode == RenderSpec::Mode::Song ? songTicks(p, slots)
                  : cut ? pass * (loops - 1) + cut : pass * loops);
  seq_.start(0, *this);
  seq_.process(0, *this);  // step 0 planned at 0, not clamped to the first block's end
}

// Note-offs and releases: what ends sounding notes. Everything else of the next pass is dropped.
bool OfflineSequence::passesAfterEnd(const uint8_t* b, uint8_t len) {
  const uint8_t k = b[0] & 0xF0;
  return k == 0x80 || (k == 0x90 && len >= 3 && b[2] == 0) || b[0] == 0xFF;
}

void OfflineSequence::synth(uint64_t t, uint8_t track, const uint8_t* b, uint8_t len) {
  if (len == 0 || len > 3) return;
  if (t >= bodyUs_ && !passesAfterEnd(b, len)) return;
  if (fifoN_ == kFifo) return;  // never with one block of lookahead; the synth's own queue is smaller
  Ev& e = fifo_[fifoN_++];
  e.t = t;
  e.track = track;
  e.len = len;
  memcpy(e.b, b, len);
}

// As the audio task's drain: events of this block (late ones at its start) go out, later ones wait.
bool OfflineSequence::nextBlock(SeqEv* out, int& n) {
  n = 0;
  if (done_ >= blocksTotal()) return false;
  if (done_ < bodyBlocks_) seq_.process(blockT_ + kRenderBlockUs, *this);  // everything of this block
  else if (done_ == bodyBlocks_) seq_.stop(blockT_, *this);                // the end: release all
  int kept = 0;
  for (int i = 0; i < fifoN_; ++i) {
    const int off = eventOffset(fifo_[i].t, blockT_);
    if (off < 0) {
      fifo_[kept++] = fifo_[i];
      continue;
    }
    SeqEv& e = out[n++];
    e.off = static_cast<uint8_t>(off);
    e.track = fifo_[i].track;
    e.len = fifo_[i].len;
    memcpy(e.b, fifo_[i].b, sizeof e.b);
  }
  fifoN_ = kept;
  blockT_ += kRenderBlockUs;
  ++done_;
  return true;
}

void BlockRender::render(Synth& synth, const SeqEv* ev, int n, int16_t l[Synth::kBlock],
                         int16_t r[Synth::kBlock]) {
  for (int i = 0; i < n; ++i) synth.event(ev[i].off, ev[i].track, ev[i].b, ev[i].len);
  synth.render(l, r);
  measure(l, r);
}

void BlockRender::measure(const int16_t* l, const int16_t* r, int n) {
  for (int c = 0; c < 2; ++c) {
    const int16_t* const out = c ? r : l;
    for (int i = 0; i < n; ++i) {
      const int a = out[i] < 0 ? -out[i] : out[i];
      if (a > peak) peak = static_cast<int16_t>(a > 32767 ? 32767 : a);
      if (out[i] >= 32767 || out[i] <= -32767) ++clips;
    }
  }
}

bool OfflineRender::renderBlock(int16_t l[Synth::kBlock], int16_t r[Synth::kBlock]) {
  int n = 0;
  if (!seq_.nextBlock(ev_, n)) return false;
  out_.render(synth_, ev_, n, l, r);
  return true;
}

}  // namespace mt
