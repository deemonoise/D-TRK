#include "link_frame.h"

namespace mt::link {

uint16_t crc16(const uint8_t* d, int n) {
  uint16_t c = 0xFFFF;
  for (int i = 0; i < n; ++i) {
    c ^= static_cast<uint16_t>(d[i]) << 8;
    for (int k = 0; k < 8; ++k) c = (c & 0x8000) ? static_cast<uint16_t>((c << 1) ^ 0x1021) : static_cast<uint16_t>(c << 1);
  }
  return c;
}

int encode(uint8_t type, uint8_t seq, const uint8_t* payload, int n, uint8_t* out) {
  if (n < 0 || n > kMaxPayload) return 0;
  uint8_t frame[kMaxFrame];
  frame[0] = type;
  frame[1] = seq;
  for (int i = 0; i < n; ++i) frame[2 + i] = payload[i];
  uint16_t c = crc16(frame, n + 2);
  frame[n + 2] = static_cast<uint8_t>(c);
  frame[n + 3] = static_cast<uint8_t>(c >> 8);
  int len = n + 4;

  int o = 1, codePos = 0;
  uint8_t code = 1;
  for (int i = 0; i < len; ++i) {
    if (frame[i] == 0) {
      out[codePos] = code;
      codePos = o++;
      code = 1;
    } else {
      out[o++] = frame[i];
      if (++code == 0xFF) {
        out[codePos] = code;
        codePos = o++;
        code = 1;
      }
    }
  }
  out[codePos] = code;
  out[o++] = 0;
  return o;
}

bool Decoder::feed(uint8_t b) {
  if (b != 0) {
    if (rawLen_ < kMaxEncoded) raw_[rawLen_++] = b;
    else overflow_ = true;
    return false;
  }
  bool ok = false;
  if (overflow_) ++errors_;
  else if (rawLen_ > 0) ok = finish();
  rawLen_ = 0;
  overflow_ = false;
  return ok;
}

bool Decoder::finish() {
  int o = 0, i = 0;
  while (i < rawLen_) {
    uint8_t code = raw_[i++];
    for (int k = 1; k < code; ++k) {
      if (i >= rawLen_ || o >= kMaxFrame) {
        ++errors_;
        return false;
      }
      frame_[o++] = raw_[i++];
    }
    if (code != 0xFF && i < rawLen_) {
      if (o >= kMaxFrame) {
        ++errors_;
        return false;
      }
      frame_[o++] = 0;
    }
  }
  if (o < 4) {
    ++errors_;
    return false;
  }
  uint16_t c = static_cast<uint16_t>(frame_[o - 2] | (frame_[o - 1] << 8));
  if (crc16(frame_, o - 2) != c) {
    ++errors_;
    return false;
  }
  size_ = o - 4;
  return true;
}

}  // namespace mt::link
