#pragma once
#include <stdint.h>

// ESP <-> synth board link: COBS frames, 0x00 delimiter. Inside: type, seq, payload, crc16 (LE).
namespace mt::link {

constexpr uint32_t kBaud = 3000000;  // both firmwares; raise after the echo test
constexpr int kMaxPayload = 512;
constexpr int kMaxFrame = kMaxPayload + 4;                    // type, seq, crc16
constexpr int kMaxEncoded = kMaxFrame + kMaxFrame / 254 + 2;  // COBS overhead + 0x00

uint16_t crc16(const uint8_t* d, int n);  // CCITT-FALSE, init 0xFFFF
// type, seq, payload -> COBS bytes ending in 0x00. Returns the encoded length, 0 if n is too big.
int encode(uint8_t type, uint8_t seq, const uint8_t* payload, int n, uint8_t* out);

// Byte-at-a-time decoder: feed() returns true when a whole valid frame is ready (type(), seq(),
// payload(), size()); bad CRC / overlong / bad COBS drop the frame and bump errors(), so joining a
// stream mid-frame costs one error and resyncs on the next 0x00.
class Decoder {
 public:
  bool feed(uint8_t b);
  uint8_t type() const { return frame_[0]; }
  uint8_t seq() const { return frame_[1]; }
  const uint8_t* payload() const { return frame_ + 2; }
  int size() const { return size_; }
  uint32_t errors() const { return errors_; }

 private:
  bool finish();

  uint8_t raw_[kMaxEncoded];
  uint8_t frame_[kMaxFrame];
  int rawLen_ = 0;
  int size_ = 0;
  uint32_t errors_ = 0;
  bool overflow_ = false;
};

}  // namespace mt::link
