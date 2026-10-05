#include "wav.h"
#include <string.h>

namespace mt {
namespace {

uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) { return rd16(p) | (static_cast<uint32_t>(rd16(p + 2)) << 16); }

constexpr uint16_t kFmtPcm = 1;
constexpr uint16_t kFmtExtensible = 0xFFFE;

// ByteSource with a position counter.
struct Reader {
  ByteSource& src;
  uint32_t pos = 0;
  bool read(void* d, uint32_t n) {
    if (!src.read(d, n)) return false;
    pos += n;
    return true;
  }
  bool skip(uint32_t n) {
    if (n && !src.skip(n)) return false;
    pos += n;
    return true;
  }
};

WavErr parseFmt(Reader& r, uint32_t size, WavInfo& w) {
  uint8_t b[40] = {0};
  const uint32_t n = size < sizeof(b) ? size : sizeof(b);
  if (size < 16) return WavErr::Unsupported;
  if (!r.read(b, n) || !r.skip(size - n + (size & 1))) return WavErr::Truncated;
  uint16_t format = rd16(b);
  if (format == kFmtExtensible) format = size >= 26 ? rd16(b + 24) : 0;
  w.channels = rd16(b + 2);
  w.rate = rd32(b + 4);
  w.bits = rd16(b + 14);
  if (format != kFmtPcm || w.channels == 0 || w.rate == 0) return WavErr::Unsupported;
  if (w.bits != 8 && w.bits != 16 && w.bits != 24) return WavErr::Unsupported;
  return WavErr::Ok;
}

void parseSmpl(Reader& r, uint32_t size, WavInfo& w) {
  uint8_t b[16];
  if (size < sizeof(b)) {
    r.skip(size + (size & 1));  // too short for a root note: skip the body
    return;
  }
  if (!r.read(b, sizeof(b))) return;
  const uint32_t note = rd32(b + 12);
  if (note <= 127) {
    w.hasRoot = true;
    w.root = static_cast<uint8_t>(note);
  }
  r.skip(size - sizeof(b) + (size & 1));
}

}  // namespace

WavErr wavParse(ByteSource& src, WavInfo& out) {
  out = WavInfo{};
  Reader r{src};
  uint8_t h[12];
  if (!r.read(h, 12) || memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) return WavErr::NotWav;
  bool fmt = false, data = false;
  for (;;) {
    uint8_t c[8];
    if (!r.read(c, 8)) return data ? WavErr::Ok : WavErr::Truncated;
    const uint32_t size = rd32(c + 4);
    if (memcmp(c, "fmt ", 4) == 0 && !fmt) {
      const WavErr e = parseFmt(r, size, out);
      if (e != WavErr::Ok) return e;
      fmt = true;
    } else if (memcmp(c, "data", 4) == 0 && !data) {
      if (!fmt) return WavErr::Unsupported;
      out.dataOffset = r.pos;
      out.dataBytes = size - size % out.frameBytes();
      if (!r.skip(size)) return WavErr::Truncated;
      data = true;
      // The pad byte and anything after the data are optional (only "smpl" is of interest).
      if ((size & 1) && !r.skip(1)) return WavErr::Ok;
    } else if (memcmp(c, "smpl", 4) == 0) {
      parseSmpl(r, size, out);
      if (data) return WavErr::Ok;
    } else if (!r.skip(size + (size & 1))) {
      return data ? WavErr::Ok : WavErr::Truncated;
    }
  }
}

void wavToMono(const uint8_t* raw, uint32_t frames, const WavInfo& w, int16_t* out) {
  const int ch = w.channels;
  const int bps = w.bits / 8;
  for (uint32_t f = 0; f < frames; ++f) {
    int32_t sum = 0;
    for (int c = 0; c < ch; ++c, raw += bps) {
      switch (bps) {
        case 1: sum += (static_cast<int32_t>(raw[0]) - 128) * 256; break;
        case 2: sum += static_cast<int16_t>(rd16(raw)); break;
        default: sum += static_cast<int16_t>(rd16(raw + 1)); break;
      }
    }
    out[f] = static_cast<int16_t>(sum / ch);
  }
}

int Downsampler::push(const int16_t* in, int n, int16_t* out) {
  if (n <= 0) return 0;
  if (rate_ <= kWavMaxRate) {
    memcpy(out, in, sizeof(int16_t) * n);
    return n;
  }
  if (inBase_ == 0) prevX_ = prevY_ = in[0];
  // Filtered input y[j] = (x[j] + x[j-1]) / 2 for j in [inBase_ - 1, inBase_ + n).
  auto y = [&](uint64_t j) -> int {
    if (j + 1 == inBase_) return prevY_;
    const int i = static_cast<int>(j - inBase_);
    return (in[i] + (i ? in[i - 1] : prevX_)) / 2;
  };
  const uint64_t end = inBase_ + static_cast<uint64_t>(n);
  int produced = 0;
  for (;;) {
    const uint64_t num = k_ * rate_;
    const uint64_t i = num / kWavMaxRate;
    if (i + 1 >= end) break;
    const int frac = static_cast<int>(num % kWavMaxRate);
    const int a = y(i), b = y(i + 1);
    out[produced++] = static_cast<int16_t>(a + static_cast<int64_t>(b - a) * frac / static_cast<int>(kWavMaxRate));
    ++k_;
  }
  prevY_ = y(end - 1);
  prevX_ = in[n - 1];
  inBase_ = end;
  return produced;
}

}  // namespace mt
