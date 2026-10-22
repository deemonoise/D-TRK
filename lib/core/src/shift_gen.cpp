#include "shift_gen.h"
#include <stdio.h>

namespace mt {

uint32_t genHash(uint32_t a, uint32_t b, uint32_t c) {
  uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ c * 0xC2B2AE3Du;
  h ^= h >> 16;  // murmur3 finalizer
  h *= 0x85EBCA6Bu;
  h ^= h >> 13;
  h *= 0xC2B2AE35u;
  h ^= h >> 16;
  return h;
}

void genReset(GenState& s, const GenCfg& c, int track) {
  const uint16_t m = genMask(c.len);
  s.reg = static_cast<uint16_t>(genHash(c.seed, static_cast<uint32_t>(track), 0xFFFFFFFFu) & m);
  if (!s.reg) s.reg = 1;
  s.n = 0;
  s.flipped = 0;
}

void genShift(GenState& s, const GenCfg& c, int track, uint8_t flip) {
  const uint16_t m = genMask(c.len);
  const int len = __builtin_popcount(m);
  const unsigned out = (s.reg >> (len - 1)) & 1u;
  const bool f = genHash(c.seed, static_cast<uint32_t>(track), s.n) % 100u < flip;
  s.reg = static_cast<uint16_t>(((s.reg << 1) | (out ^ (f ? 1u : 0u))) & m);
  if (!s.reg && !flip) s.reg = 1;  // len lowered while playing can mask every bit away: LOCK keeps 0 forever
  s.flipped = f ? 1 : 0;
  ++s.n;
}

uint8_t genNote(const GenState& s, const GenCfg& c, uint8_t root, ScaleType t) {
  const uint16_t m = genMask(c.len);
  const int w = __builtin_popcount(m) < 8 ? __builtin_popcount(m) : 8;
  const uint32_t top = (1u << w) - 1;
  const uint32_t win = s.reg & top;
  const int oct = c.oct < 1 ? 1 : (c.oct > kGenOctMax ? kGenOctMax : c.oct);
  const int steps = scaleDegrees(t) * oct;  // N - 1
  const int idx = static_cast<int>((win * static_cast<uint32_t>(steps) + top / 2) / top);
  int lo = c.low > 127 ? 127 : c.low;
  while (lo < 127 && !inScale(lo, root, t)) ++lo;
  while (lo > 0 && !inScale(lo, root, t)) --lo;  // nothing in the scale above: the nearest below
  while (lo + 12 * oct > 127) lo -= 12;
  return static_cast<uint8_t>(moveDegrees(lo, idx, root, t));
}

bool genGate(const GenState& s, const GenCfg& c) {
  uint32_t b = s.reg & genMask(c.len) & 0xFFu, r = 0;
  for (int i = 0; i < 8; ++i) r |= ((b >> i) & 1u) << (7 - i);
  return r * 100u < static_cast<uint32_t>(c.density > 100 ? 100 : c.density) * 256u;
}

void genFlipText(uint8_t flip, char out[8]) {
  if (flip == 0) snprintf(out, 8, "LOCK");
  else if (flip >= 100) snprintf(out, 8, "x2 INV");
  else snprintf(out, 8, "%u%%", flip);
}

void genClamp(GenCfg& c) {
  if (c.mode >= static_cast<uint8_t>(GenMode::Count)) c.mode = 0;
  c.len = c.len < kGenLenMin ? kGenLenMin : (c.len > kGenLenMax ? kGenLenMax : c.len);
  if (c.flip > 100) c.flip = 100;
  if (c.density > 100) c.density = 100;
  c.oct = c.oct < 1 ? 1 : (c.oct > kGenOctMax ? kGenOctMax : c.oct);
  if (c.low > 127) c.low = 127;
}

}  // namespace mt
