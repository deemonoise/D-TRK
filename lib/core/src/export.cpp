#include "export.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "follow.h"
#include "wav.h"

namespace mt {

namespace {

bool isSong(const ExportSpec& s) { return s.mode == RenderSpec::Mode::Song; }
uint32_t loopsOf(const ExportSpec& s) { return isSong(s) || s.loops < 1 ? 1 : s.loops; }
uint64_t passOf(const Project& p, const ExportSpec& s) { return isSong(s) ? songUs(p, s.maxSlots) : passUs(p, s.pattern); }
uint32_t tailSec(ExportTail t) { return t == ExportTail::S2 ? 2 : (t == ExportTail::S4 ? 4 : (t == ExportTail::S8 ? 8 : 0)); }
bool wrap(const ExportSpec& s) { return s.tail == ExportTail::Wrap && !isSong(s); }
uint32_t bodyBlocks(const Project& p, const ExportSpec& s) {
  return (exportSkipFrames(p, s) + exportBodyFrames(p, s) + Synth::kBlock - 1) / Synth::kBlock;
}

// The patterns the source plays.
void usedPatterns(const Project& p, const ExportSpec& s, bool used[kPatterns]) {
  for (int i = 0; i < kPatterns; ++i) used[i] = false;
  if (isSong(s)) {
    followWalk(p, followRunSeed(p.seed, 0), s.maxSlots,  // the slots on the render's path
               [&](int i) { used[p.song[i] < kPatterns ? p.song[i] : 0] = true; });
  } else {
    used[s.pattern < kPatterns ? s.pattern : 0] = true;
  }
}

bool hasNotes(const Project& p, const bool used[kPatterns], int t) {
  for (int pi = 0; pi < kPatterns; ++pi) {
    if (!used[pi]) continue;
    const Pattern& pt = p.patterns[pi];
    for (int st = 0; st < pt.length && st < kMaxSteps; ++st)
      if (pt.steps[t][st].hasNote()) return true;
  }
  return false;
}

// A-Z a-z 0-9 _ - kept, the rest '_'.
void sanitize(const char* in, char* out, int n) {
  int k = 0;
  for (; in[k] && k < n - 1; ++k) {
    const char c = in[k];
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    out[k] = ok ? c : '_';
  }
  out[k] = 0;
}

uint32_t xorshift(uint32_t& x) {
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return x;
}

}  // namespace

uint32_t usToFrames(uint64_t us) {
  return static_cast<uint32_t>((us * Synth::kBlock + kRenderBlockUs / 2) / kRenderBlockUs);
}

RenderSpec exportRenderSpec(const Project& p, const ExportSpec& s) {
  (void)p;
  RenderSpec r;
  r.mode = s.mode;
  r.pattern = s.pattern;
  r.tracksMask = s.tracksMask;
  r.maxSlots = s.maxSlots;
  r.loops = static_cast<uint8_t>(loopsOf(s) + (wrap(s) ? 1 : 0));
  if (wrap(s)) r.tailBlocks = 1;  // the cut's rounding never runs past the render
  else if (s.tail == ExportTail::Auto) r.tailBlocks = kExportAutoMaxBlocks;
  else r.tailBlocks = (tailSec(s.tail) * kSynthRate + Synth::kBlock - 1) / Synth::kBlock + (tailSec(s.tail) ? 1 : 0);
  return r;
}

uint32_t exportBodyFrames(const Project& p, const ExportSpec& s) { return usToFrames(passOf(p, s) * loopsOf(s)); }

uint32_t exportSkipFrames(const Project& p, const ExportSpec& s) { return wrap(s) ? usToFrames(passOf(p, s)) : 0; }

uint32_t exportCutFrames(const Project& p, const ExportSpec& s, uint32_t rendered, uint32_t quiet) {
  const uint32_t body = exportBodyFrames(p, s);
  uint32_t want = body;
  if (s.tail == ExportTail::Auto) {
    const uint32_t loud = quiet < rendered ? rendered - quiet : 0;
    const uint64_t f = static_cast<uint64_t>(loud + kExportAutoPadBlocks) * Synth::kBlock;
    want = f > body ? static_cast<uint32_t>(f) : body;
  } else if (!wrap(s)) {
    want = body + tailSec(s.tail) * kSynthRate;
  }
  const uint64_t avail = static_cast<uint64_t>(rendered) * Synth::kBlock;
  const uint32_t skip = exportSkipFrames(p, s);
  const uint64_t room = avail > skip ? avail - skip : 0;
  return want < room ? want : static_cast<uint32_t>(room);
}

uint32_t exportEstFrames(const Project& p, const ExportSpec& s) {
  const uint32_t body = exportBodyFrames(p, s);
  if (s.tail == ExportTail::Auto) return body + 2 * kSynthRate;
  return wrap(s) ? body : body + tailSec(s.tail) * kSynthRate;
}

bool exportAutoDone(const Project& p, const ExportSpec& s, uint32_t rendered, uint32_t quiet) {
  const uint32_t body = bodyBlocks(p, s);
  if (rendered < body) return false;
  return quiet >= kExportQuietBlocks || rendered >= body + kExportAutoMaxBlocks;
}

uint16_t exportStemTracks(const Project& p, const ExportSpec& s) {
  bool used[kPatterns];
  usedPatterns(p, s, used);
  uint16_t m = 0;
  for (int t = 0; t < kTracks; ++t)
    if (p.trackInternal(t) && p.trackAudible(t) && (s.tracksMask >> t & 1) && hasNotes(p, used, t))
      m |= static_cast<uint16_t>(1u << t);
  return m;
}

int exportMidiTracks(const Project& p, const ExportSpec& s) {
  bool used[kPatterns];
  usedPatterns(p, s, used);
  int n = 0;
  for (int t = 0; t < kTracks; ++t)
    if (!p.trackInternal(t) && hasNotes(p, used, t)) ++n;
  return n;
}

void exportSrcTag(const ExportSpec& s, char* out, int n) {
  if (isSong(s)) snprintf(out, n, "SONG");
  else if (loopsOf(s) > 1) snprintf(out, n, "P%02ux%u", s.pattern + 1u, static_cast<unsigned>(loopsOf(s)));
  else snprintf(out, n, "P%02u", s.pattern + 1u);
}

void exportDir(const Project& p, const ExportSpec& s, char* out, int n) {
  char src[12], name[sizeof p.name];
  exportSrcTag(s, src, sizeof src);
  sanitize(p.name[0] ? p.name : "PROJECT", name, sizeof name);
  snprintf(out, n, "/export/%s/%s", name, src);
}

int exportOutputs(const Project& p, const ExportSpec& s, ExportOut* out) {
  char src[12], proj[sizeof p.name];
  exportSrcTag(s, src, sizeof src);
  sanitize(p.name[0] ? p.name : "PROJECT", proj, sizeof proj);
  int n = 0;
  out[n].tap = kExportTapMaster;
  snprintf(out[n++].name, kExportNameMax, "%s_%s_MASTER.wav", proj, src);
  if (s.stems == ExportStems::Off) return n;
  const uint16_t m = exportStemTracks(p, s);
  for (int t = 0; t < kTracks; ++t) {
    if (!(m >> t & 1)) continue;
    char tn[sizeof p.tracks[t].name];
    sanitize(p.tracks[t].name, tn, sizeof tn);
    out[n].tap = static_cast<uint8_t>(1 + t);
    if (tn[0]) snprintf(out[n++].name, kExportNameMax, "%s_%s_%02d_%s.wav", proj, src, t + 1, tn);
    else snprintf(out[n++].name, kExportNameMax, "%s_%s_%02d.wav", proj, src, t + 1);
  }
  if (s.stems != ExportStems::TracksFx) return n;
  const bool on[3] = {p.modType != static_cast<uint8_t>(ModType::Off) && p.modLevel > 0, p.dlyLevel > 0,
                      p.rvbLevel > 0};
  static const char* const kFx[3] = {"MOD", "DLY", "RVB"};
  for (int k = 0; k < 3; ++k) {
    if (!on[k]) continue;
    out[n].tap = static_cast<uint8_t>(1 + RenderTaps::kMod + k);
    snprintf(out[n++].name, kExportNameMax, "%s_%s_FX_%s.wav", proj, src, kFx[k]);
  }
  return n;
}

int exportPasses(int n, int outMax) { return outMax < 1 ? n : (n + outMax - 1) / outMax; }

uint64_t exportBytes(int n, uint32_t frames, uint8_t bits) {
  return static_cast<uint64_t>(n) * (kWavPcmHeaderBytes + static_cast<uint64_t>(frames) * (bits / 8u) * 2u);
}

uint32_t ExportPcm::put(const float* l, const float* r, int n, float g, uint8_t* out) {
  uint8_t* o = out;
  const bool b24 = bits == 24;
  const float lim = b24 ? 8388607.f : 32767.f;
  for (int i = 0; i < n; ++i)
    for (int c = 0; c < 2; ++c) {
      const float v = (c ? r[i] : l[i]) * g;  // int16 scale
      const float a = fabsf(v);
      if (a > peak) peak = static_cast<uint16_t>(a >= 32767.f ? 32767 : a);
      float x;
      if (b24) {
        x = v * 256.f;
      } else {  // TPDF: two uniform values in [0, 1), their difference spans +-1 LSB
        const float u1 = (xorshift(rng) >> 8) * (1.f / 16777216.f);
        const float u2 = (xorshift(rng) >> 8) * (1.f / 16777216.f);
        x = v + u1 - u2;
      }
      int32_t q;
      if (x > lim) {
        q = static_cast<int32_t>(lim);
        ++clips;
      } else if (x < -lim) {
        q = -static_cast<int32_t>(lim);
        ++clips;
      } else {
        q = static_cast<int32_t>(lrintf(x));
      }
      *o++ = static_cast<uint8_t>(q);
      *o++ = static_cast<uint8_t>(q >> 8);
      if (b24) *o++ = static_cast<uint8_t>(q >> 16);
    }
  return static_cast<uint32_t>(o - out);
}

void ExportWriter::begin(const char* dir, uint8_t bits, uint32_t skipFrames, uint8_t* mem, uint32_t cap,
                         float* tapMem, int maxOut) {
  abort();
  snprintf(dir_, sizeof dir_, "%s", dir);
  bits_ = bits == 16 ? 16 : 24;
  skip_ = skipFrames;
  cap_ = cap;
  tapMem_ = tapMem;
  max_ = maxOut < 0 ? 0 : (maxOut > kExportMaxOut ? kExportMaxOut : maxOut);
  for (int i = 0; i < max_; ++i) buf_[i] = mem + static_cast<size_t>(i) * cap;
  taps_ = RenderTaps();
  n_ = 0;
  open_ = true;
}

void ExportWriter::path(int i, bool tmp, char* out, int n) const {
  snprintf(out, n, "%s/%s%s", dir_, name_[i], tmp ? ".tmp" : "");
}

bool ExportWriter::add(int idx, uint8_t tap, const char* name, uint64_t prealloc) {
  if (!open_ || idx != n_ || idx >= max_ || tap > RenderTaps::kCount || !name || !name[0]) return false;
  snprintf(name_[idx], kExportNameMax, "%s", name);
  char p[sizeof dir_ + kExportNameMax + 8];
  path(idx, true, p, sizeof p);
  if (!io_.open(idx, p, prealloc)) return false;
  const uint8_t room[kWavPcmHeaderBytes] = {};  // the header's place, written at the end
  if (!io_.write(idx, room, sizeof room)) {
    io_.close(idx);
    io_.remove(p);
    return false;
  }
  tap_[idx] = tap;
  pcm_[idx] = ExportPcm();
  pcm_[idx].bits = bits_;
  pcm_[idx].seed(0x9E3779B9u ^ (tap * 0x85EBCA6Bu + 1));  // per output: passes give the same dither
  fill_[idx] = 0;
  done_[idx] = 0;
  if (tap != kExportTapMaster) {
    const int k = tap - 1;
    taps_.l[k] = tapMem_ + static_cast<size_t>(idx) * 2 * Synth::kBlock;
    taps_.r[k] = taps_.l[k] + Synth::kBlock;
    taps_.mask |= 1u << k;
  }
  ++n_;
  return true;
}

bool ExportWriter::write(int i) {
  const uint32_t n = fill_[i];
  if (!n) return true;
  fill_[i] = 0;
  done_[i] += n;
  return io_.write(i, buf_[i], n);
}

bool ExportWriter::block(const Synth& s) {
  int from = 0;
  if (skip_) {
    const uint32_t k = skip_ < static_cast<uint32_t>(Synth::kBlock) ? skip_ : Synth::kBlock;
    skip_ -= k;
    from = static_cast<int>(k);
    if (from == Synth::kBlock) return true;
  }
  const int n = Synth::kBlock - from;
  for (int i = 0; i < n_; ++i) {
    const uint32_t need = static_cast<uint32_t>(n) * pcm_[i].frameBytes();
    if (fill_[i] + need > cap_ && !write(i)) return false;  // never with flush() per RenderBlocks
    const float *l, *r;
    float g;
    if (tap_[i] == kExportTapMaster) {
      l = s.masterL();
      r = s.masterR();
      g = 1.f;
    } else {
      l = taps_.l[tap_[i] - 1];
      r = taps_.r[tap_[i] - 1];
      g = taps_.gain * 32767.f;
    }
    fill_[i] += pcm_[i].put(l + from, r + from, n, g, buf_[i] + fill_[i]);
  }
  return true;
}

bool ExportWriter::flush() {
  for (int i = 0; i < n_; ++i)
    if (fill_[i] >= threshold() && !write(i)) return false;
  return true;
}

bool ExportWriter::end(uint32_t frames) {
  if (!open_) return false;
  bool ok = true;
  for (int i = 0; i < n_ && ok; ++i) ok = write(i);
  const uint32_t fb = bits_ / 8u * 2u;
  for (int i = 0; i < n_ && ok; ++i) {
    uint64_t data = done_[i];
    if (frames && static_cast<uint64_t>(frames) * fb < data) data = static_cast<uint64_t>(frames) * fb;
    uint8_t hdr[kWavPcmHeaderBytes];
    wavHeaderPcm(hdr, static_cast<uint32_t>(data / fb), kSynthRate, 2, bits_);
    ok = io_.seal(i, kWavPcmHeaderBytes + data, hdr, sizeof hdr);
    if (!ok) io_.close(i);
  }
  for (int i = 0; i < n_ && ok; ++i) {
    char a[sizeof dir_ + kExportNameMax + 8], b[sizeof a];
    path(i, true, a, sizeof a);
    path(i, false, b, sizeof b);
    ok = io_.rename(a, b);
  }
  if (!ok) {
    abort();
    return false;
  }
  n_ = 0;
  open_ = false;
  return true;
}

void ExportWriter::abort() {
  for (int i = 0; i < n_; ++i) {
    io_.close(i);
    char a[sizeof dir_ + kExportNameMax + 8];
    path(i, true, a, sizeof a);
    io_.remove(a);
  }
  n_ = 0;
  open_ = false;
}

}  // namespace mt
