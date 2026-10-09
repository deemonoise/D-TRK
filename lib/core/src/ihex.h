#pragma once
#include <stdint.h>

namespace mt {

// Intel HEX, a line at a time (firmware updates of the synth board): every record's checksum is
// checked; extended segment / linear address records move the base, start address records are
// ignored.
class IhexReader {
 public:
  enum class Line : uint8_t {
    Data,    // data() / size() at address()
    Other,   // an address record, an empty line
    Eof,     // the end record
    Bad,     // malformed, a wrong checksum, an unknown record type
    AfterEof,
  };
  // One line without its line end (a trailing '\r' is ignored).
  Line feed(const char* s, int n);
  uint32_t address() const { return addr_; }
  const uint8_t* data() const { return data_; }
  int size() const { return n_; }
  bool eof() const { return eof_; }
  int lines() const { return lines_; }

 private:
  uint32_t base_ = 0, addr_ = 0;
  uint8_t data_[255];
  int n_ = 0, lines_ = 0;
  bool eof_ = false;
};

// A Teensy 4.x image as it lands at flash base `base` (the first `size` bytes): the FlexSPI config
// block's tag at 0, the image vector table at 0x1000 (header 0xD1, its own address, the entry point
// inside the image) and the target id string somewhere in it.
bool teensyImageOk(const uint8_t* img, uint32_t size, uint32_t base, const char* id);

}  // namespace mt
