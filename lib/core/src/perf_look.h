#pragma once
#include <stdint.h>
#include "perf_fx.h"

namespace mt {

// PERF visuals: what the held effects do to the whole screen. Pure: ui::PerfVis pushes the frame
// row by row through perfLookRow. Pixels are RGB565 in panel byte order (big-endian), as LovyanGFX
// keeps them in a 16-bit sprite.
constexpr int kPerfLookW = 480, kPerfLookH = 320;
constexpr int kPerfGhostDx = 24;  // DelayThrow: the echo's offset, px

struct PerfVisIn {
  PerfFx fx;
  int depth;
  float heldSteps;  // sixteenths since the press (TapeStop)
};

struct PerfLook {
  // Colour, in this order.
  int mul = 256;       // brightness, 256 = as is (Filter LP, TapeStop)
  int wash = 0;        // towards white, 0..256 (Filter HP, Roll's flash)
  int contrast = 256;  // around mid grey, 256 = as is (Drive)
  int bits = 5;        // bits per colour channel, 5 = as is (Crush)
  // The panel's own modes (ui::PerfVis sends them).
  bool idle = false;    // 8 colours (Crush at the bottom)
  bool invert = false;  // Gate, closed phase
  // Geometry.
  int block = 1;        // pixelate, px (Downsample)
  int tile = 0;         // rows repeat every `tile`, 0 = off (Repeat, Roll)
  int slide = 0;        // the image moved down, rows (TapeStop)
  bool mirror = false;  // Reverse
  int shift = 0;        // whole-frame x offset (Scratch)
  int waveAmp = 0, waveLen = 64;  // per-row sine x offset (ModThrow)
  int wobAmp = 0, wobLen = kPerfLookH;  // a slow wide one (TapeWobble)
  float wavePhase = 0, wobPhase = 0;   // 0..1
  int ghost = 0;        // blend of the row kPerfGhostDx to the right, 0..128 (DelayThrow)
  int blur = 0;         // horizontal box blur radius, px (ReverbThrow)
  bool freeze = false;  // the frame stays (Freeze)
};

// The held effects (any order: they compose) at play position `steps` (sixteenths).
PerfLook perfLook(const PerfVisIn* in, int n, float steps);
bool perfLookPlainColour(const PerfLook& v);  // no colour op: rows are copied
int perfLookSrcRow(const PerfLook& v, int y);  // the sprite row shown at y, -1 = black
int perfLookShift(const PerfLook& v, int y);   // row y's x offset (right = positive)
// One row: src (the sprite row) -> dst, w pixels, moved right by shift (wrapped).
void perfLookRow(const PerfLook& v, const uint16_t* src, uint16_t* dst, int w, int shift);

}  // namespace mt
