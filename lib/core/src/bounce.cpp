#include "bounce.h"
#include <stdio.h>
#include <string.h>
#include "sample_set.h"
#include "synth_osc.h"

namespace mt {

using link::RenderBlocks;

bool BounceSeq::next(RenderBlocks& f, uint32_t quiet) {
  f.first = sent_;
  f.n = 0;
  if (sent_ >= s_.maxBlocks) {
    max_ = true;
    return false;
  }
  if (sent_ > s_.holdBlocks && quiet >= s_.quietStop) return false;  // the note-off rendered, silent
  int used = 0;
  auto add = [&](uint8_t a, uint8_t b, uint8_t c, uint8_t len) {
    link::RenderEv& e = f.ev[used++];
    e.off = 0;
    e.track = kPreviewTrack;
    e.len = len;
    e.b[0] = a;
    e.b[1] = b;
    e.b[2] = c;
  };
  while (f.n < RenderBlocks::kMaxBlocks && sent_ < s_.maxBlocks) {
    const int before = used;
    if (sent_ == 0) {
      add(0xF7, s_.insTrack, 0, 2);
      add(0xC0, s_.instr, 0, 2);
      add(0x90, s_.note, s_.vel, 3);
    }
    if (sent_ == s_.holdBlocks) add(0x80, s_.note, 0, 3);
    f.evN[f.n++] = static_cast<uint8_t>(used - before);
    ++sent_;
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
