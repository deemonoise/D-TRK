#include "ihex.h"
#include <string.h>

namespace mt {

namespace {

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

int hexByte(const char* s) {
  const int h = hexDigit(s[0]), l = hexDigit(s[1]);
  return h < 0 || l < 0 ? -1 : h * 16 + l;
}

uint32_t rd32(const uint8_t* p) {
  return p[0] | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

IhexReader::Line IhexReader::feed(const char* s, int n) {
  n_ = 0;
  while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == ' ')) --n;
  if (n == 0) return Line::Other;
  if (eof_) return Line::AfterEof;
  if (s[0] != ':' || n < 11 || (n - 1) % 2) return Line::Bad;
  const int bytes = (n - 1) / 2;
  uint8_t b[260];
  if (bytes > static_cast<int>(sizeof b)) return Line::Bad;
  uint8_t sum = 0;
  for (int i = 0; i < bytes; ++i) {
    const int v = hexByte(s + 1 + 2 * i);
    if (v < 0) return Line::Bad;
    b[i] = static_cast<uint8_t>(v);
    sum = static_cast<uint8_t>(sum + v);
  }
  const int len = b[0];
  if (bytes != len + 5 || sum != 0) return Line::Bad;
  const uint16_t off = static_cast<uint16_t>(b[1] << 8 | b[2]);
  const uint8_t* d = b + 4;
  ++lines_;
  switch (b[3]) {
    case 0:
      addr_ = base_ + off;
      memcpy(data_, d, len);
      n_ = len;
      return Line::Data;
    case 1:
      eof_ = true;
      return Line::Eof;
    case 2:
      if (len != 2) return Line::Bad;
      base_ = static_cast<uint32_t>(d[0] << 8 | d[1]) << 4;
      return Line::Other;
    case 4:
      if (len != 2) return Line::Bad;
      base_ = static_cast<uint32_t>(d[0] << 8 | d[1]) << 16;
      return Line::Other;
    case 3:
    case 5: return Line::Other;
    default: return Line::Bad;
  }
}

bool teensyImageOk(const uint8_t* img, uint32_t size, uint32_t base, const char* id) {
  constexpr uint32_t kIvt = 0x1000;
  if (size < kIvt + 32) return false;
  if (rd32(img) != 0x42464346u) return false;  // "FCFB"
  const uint8_t* ivt = img + kIvt;
  const uint32_t entry = rd32(ivt + 4);
  if ((rd32(ivt) & 0xFF) != 0xD1 || rd32(ivt + 20) != base + kIvt) return false;
  if (entry < base || entry >= base + size) return false;
  const size_t idLen = strlen(id);
  for (uint32_t i = 0; i + idLen <= size; ++i)
    if (memcmp(img + i, id, idLen) == 0) return true;
  return false;
}

}  // namespace mt
