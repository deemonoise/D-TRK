#include "perf_look.h"
#include <math.h>

namespace mt {

namespace {

constexpr float kTwoPi = 6.2831853f;

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
float frac(float x) { return x - floorf(x); }
int wrap(int x, int w) {
  x %= w;
  return x < 0 ? x + w : x;
}

struct Rgb {
  int r, g, b;  // 0..255
};
Rgb unpack(uint16_t px) {
  const uint16_t p = static_cast<uint16_t>(px >> 8 | px << 8);
  const int r = p >> 11, g = (p >> 5) & 63, b = p & 31;
  return {r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2};
}
uint16_t pack(const Rgb& c) {
  const uint16_t p = static_cast<uint16_t>((c.r >> 3) << 11 | (c.g >> 2) << 5 | (c.b >> 3));
  return static_cast<uint16_t>(p >> 8 | p << 8);
}

int colour(int c, const PerfLook& v) {
  c = c * v.mul >> 8;
  c += (255 - c) * v.wash >> 8;
  if (v.contrast != 256) c = clampi(128 + ((c - 128) * v.contrast >> 8), 0, 255);
  return c;
}

}  // namespace

PerfLook perfLook(const PerfVisIn* in, int n, float steps) {
  PerfLook v;
  for (int i = 0; i < n; ++i) {
    const PerfFx f = in[i].fx;
    const PerfRange r = perfRange(f);
    const int d = clampi(in[i].depth, r.lo, r.hi);
    switch (f) {
      case PerfFx::Repeat: v.tile = clampi(static_cast<int>(perfRepeatSteps(d) * 20), 10, kPerfLookH / 2); break;
      case PerfFx::Roll:
        v.tile = kPerfLookH >> (d + 1);
        if (frac(steps) < 0.25f && v.wash < 96) v.wash = 96;  // a flash on every step
        break;
      case PerfFx::Reverse: v.mirror = true; break;
      case PerfFx::Scratch:
        v.shift += static_cast<int>(lroundf(sinf(kTwoPi * steps / perfScratchSteps(d)) * 60));
        break;
      case PerfFx::TapeStop: {
        float p = in[i].heldSteps / perfTapeSteps(d);
        p = p > 1 ? 1 : (p < 0 ? 0 : p);
        v.slide = static_cast<int>(p * p * kPerfLookH);
        v.mul = v.mul * (256 - static_cast<int>(p * 200)) / 256;
        break;
      }
      case PerfFx::Gate: v.invert = frac(steps / perfGateSteps(d)) >= 0.5f; break;
      case PerfFx::Filter:
        if (d < 0) v.mul = v.mul * (256 + d * 3) / 256;
        else if (d * 3 > v.wash) v.wash = d * 3;
        break;
      case PerfFx::Crush:
        v.bits = clampi(d / 2, 1, 5);
        v.idle = d <= 3;
        break;
      case PerfFx::Downsample: {
        static const int kBlock[] = {2, 3, 4, 8, 16, 32};
        v.block = kBlock[d - r.lo];
        break;
      }
      case PerfFx::Drive: v.contrast = 256 + d * 5; break;
      case PerfFx::DelayThrow: v.ghost = d * 128 / 127; break;
      case PerfFx::ReverbThrow: v.blur = d ? 1 + d / 43 : 0; break;
      case PerfFx::Freeze: v.freeze = true; break;
      case PerfFx::ModThrow:
        v.waveAmp = d * 24 / 127;
        v.wavePhase = frac(steps / 4);  // a cycle per beat
        break;
      case PerfFx::TapeWobble:
        v.wobAmp = d * 8 / 127;
        v.wobPhase = frac(steps / 16);  // a cycle per bar
        break;
      default: break;
    }
  }
  return v;
}

bool perfLookPlainColour(const PerfLook& v) {
  return v.mul == 256 && v.wash == 0 && v.contrast == 256 && v.bits >= 5 && v.ghost == 0 && v.blur == 0;
}

int perfLookSrcRow(const PerfLook& v, int y) {
  int sy = y - v.slide;
  if (sy < 0) return -1;
  if (v.tile > 0) sy %= v.tile;
  if (v.block > 1) sy -= sy % v.block;
  return sy;
}

int perfLookShift(const PerfLook& v, int y) {
  float s = static_cast<float>(v.shift);
  if (v.waveAmp) s += v.waveAmp * sinf(kTwoPi * (static_cast<float>(y) / v.waveLen + v.wavePhase));
  if (v.wobAmp) s += v.wobAmp * sinf(kTwoPi * (static_cast<float>(y) / v.wobLen + v.wobPhase));
  return static_cast<int>(lroundf(s));
}

void perfLookRow(const PerfLook& v, const uint16_t* src, uint16_t* dst, int w, int shift) {
  // Geometry: mirror, shift, pixelate (all as a source index per pixel).
  const bool plainGeo = !v.mirror && shift % w == 0 && v.block <= 1;
  const bool plainCol = perfLookPlainColour(v);
  if (plainGeo && plainCol) {
    for (int x = 0; x < w; ++x) dst[x] = src[x];
    return;
  }
  const int mask = v.bits < 5 ? (0xFF << (8 - v.bits)) & 0xFF : 0xFF;
  for (int x = 0; x < w; ++x) {
    int sx = wrap((v.mirror ? w - 1 - x : x) - shift, w);
    if (v.block > 1) sx -= sx % v.block;
    if (plainCol) {
      dst[x] = src[sx];
      continue;
    }
    Rgb c = unpack(src[sx]);
    if (v.ghost) {
      const Rgb g = unpack(src[wrap(sx + kPerfGhostDx, w)]);
      c.r += (g.r - c.r) * v.ghost >> 8;
      c.g += (g.g - c.g) * v.ghost >> 8;
      c.b += (g.b - c.b) * v.ghost >> 8;
    }
    c.r = colour(c.r, v) & mask;
    c.g = colour(c.g, v) & mask;
    c.b = colour(c.b, v) & mask;
    dst[x] = pack(c);
  }
  if (v.blur <= 0) return;
  // Box blur over dst, edges clamped; the originals are kept in a ring (dst is overwritten).
  const int r = v.blur > 3 ? 3 : v.blur;
  Rgb ring[8];
  Rgb sum{0, 0, 0};
  auto at = [&](int i) { return unpack(dst[clampi(i, 0, w - 1)]); };
  for (int i = -r; i <= r; ++i) {
    const Rgb c = at(i);
    sum.r += c.r;
    sum.g += c.g;
    sum.b += c.b;
  }
  for (int i = -r; i < 0; ++i) ring[(i + 8) & 7] = at(i);
  const int n = 2 * r + 1;
  for (int x = 0; x < w; ++x) {
    ring[x & 7] = unpack(dst[x]);
    dst[x] = pack({sum.r / n, sum.g / n, sum.b / n});
    const Rgb out = ring[(x - r) & 7];  // its original (left of 0: the prefilled edge)
    const Rgb in = at(x + r + 1);
    sum.r += in.r - out.r;
    sum.g += in.g - out.g;
    sum.b += in.b - out.b;
  }
}

}  // namespace mt
