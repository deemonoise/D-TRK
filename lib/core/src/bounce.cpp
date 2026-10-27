#include "bounce.h"
#include <stdio.h>
#include <string.h>
#include "render.h"
#include "sample_set.h"
#include "synth_osc.h"

namespace mt {

int BounceNotes::events(uint32_t block, BounceEv out[kMaxEvents]) const {
  int n = 0;
  auto add = [&](uint8_t a, uint8_t b, uint8_t c, uint8_t len) {
    BounceEv& e = out[n++];
    e.track = kPreviewTrack;
    e.len = len;
    e.b[0] = a;
    e.b[1] = b;
    e.b[2] = c;
  };
  if (block == 0) {
    add(0xF7, s_.insTrack, 0, 2);
    add(0xC0, s_.instr, 0, 2);
    add(0x90, s_.note, s_.vel, 3);
  }
  if (block == s_.holdBlocks) add(0x80, s_.note, 0, 3);
  return n;
}

bool BounceNotes::done(uint32_t block, uint32_t quietBlocks) {
  if (block >= s_.maxBlocks) {
    max_ = true;
    return true;
  }
  return block > s_.holdBlocks && quietBlocks >= s_.quietStop;  // the note-off rendered, silent
}

BounceRender::BounceRender(Synth& s, const BounceSpec& spec) : s_(s), notes_(spec) {
  s_.reset();
  s_.reseed();
  s_.setPreviewTap(l_, r_);
}

BounceRender::~BounceRender() { s_.setPreviewTap(nullptr, nullptr); }

namespace {
int16_t sat16(float x) {
  return static_cast<int16_t>(x > 32767.f ? 32767 : (x < -32768.f ? -32768 : static_cast<int>(x)));
}
}  // namespace

bool BounceRender::next(int16_t mono[Synth::kBlock]) {
  if (failed_ || notes_.done(n_, n_ - loudLR_)) return false;
  BounceEv ev[BounceNotes::kMaxEvents];
  const int ne = notes_.events(n_, ev);
  for (int i = 0; i < ne; ++i)
    if (!s_.event(0, ev[i].track, ev[i].b, ev[i].len)) failed_ = true;  // never after reset: the queue is empty
  if (failed_) return false;
  s_.render(mono);  // the mix, unused: the tap is the source
  ++n_;
  for (int k = 0; k < Synth::kBlock; ++k) {
    const int l = sat16(l_[k] * kPrevTapScale), r = sat16(r_[k] * kPrevTapScale);
    if (l > kSilence || l < -kSilence || r > kSilence || r < -kSilence) loudLR_ = n_;
    const int m = (l + r) / 2;
    const int a = m < 0 ? (m < -32767 ? 32767 : -m) : m;
    if (a > peak_) peak_ = static_cast<int16_t>(a);
    if (a > kSilence) loud_ = n_;
    mono[k] = static_cast<int16_t>(m);
  }
  return true;
}

uint32_t bounceHoldBlocks(float seconds) {
  const float s = seconds < 0.05f ? 0.05f : (seconds > 5.f ? 5.f : seconds);
  return static_cast<uint32_t>(s * kSynthRate / Synth::kBlock + 0.5f);
}

void nextBounceName(const Project& p, const char* base, char out[kSampleNameMax + 1]) {
  // A sample name is [A-Za-z0-9_-] only (projectBaseValid): anything else in the instrument's name
  // becomes '_'.
  char b[kSampleNameMax + 1];
  int bl = 0;
  for (const char* c = base ? base : ""; *c && bl < kSampleNameMax; ++c) {
    const char ch = *c;
    const bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-';
    b[bl++] = ok ? ch : '_';
  }
  while (bl > 0 && b[bl - 1] == '_') --bl;
  if (bl == 0) bl = snprintf(b, sizeof b, "SHOT");
  b[bl] = 0;
  for (int n = 1;; ++n) {
    char num[12];
    const int nl = snprintf(num, sizeof num, "_%d", n);
    int k = bl + nl > kSampleNameMax ? kSampleNameMax - nl : bl;
    while (k > 1 && b[k - 1] == '_') --k;  // no "AB__1" from a cut at a '_'
    snprintf(out, kSampleNameMax + 1, "%.*s%s", k, b, num);
    if (projSampleFind(p, out) < 0) return;
  }
}

}  // namespace mt
