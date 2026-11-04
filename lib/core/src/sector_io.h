#pragma once
#include <stdint.h>

namespace mt {

// One raw block device. Cache hooks receive only the exclusive, 32-byte-aligned bounce area.
// recover() must ensure the old transfer has stopped before another one may reuse that area.
struct SectorIoDevice {
  virtual bool read(uint32_t sector, uint32_t count, uint8_t* out) = 0;
  virtual bool write(uint32_t sector, uint32_t count, const uint8_t* in) = 0;
  virtual bool recover() = 0;
  virtual void prepareRead(uint8_t*, uint32_t) {}
  virtual void finishRead(uint8_t*, uint32_t) {}
  virtual void prepareWrite(uint8_t*, uint32_t) {}
  // After the final successful write chunk, before reporting success. DMA transfer completion
  // need not mean card programming is finished. False is final: do not retry an already-written
  // request. The adapter must latch a failed device before another operation can use it.
  virtual bool finishWrite() { return true; }
};

class SectorIo {
 public:
  static constexpr int kTries = 3;
  SectorIo(SectorIoDevice& device, uint8_t* bounce, uint32_t capacitySectors)
      : device_(device), bounce_(bounce), capacity_(capacitySectors) {}
  // Caller addresses may be unaligned. No read chunk is copied out before success. An error
  // after earlier successful chunks may leave those chunks in out; callers must not commit a
  // failed request. A single-sector filesystem cache therefore never receives failed-read data.
  bool read(uint32_t sector, uint32_t count, uint8_t* out);
  bool write(uint32_t sector, uint32_t count, const uint8_t* in);

 private:
  bool valid(uint32_t sector, uint32_t count, const void* data) const;
  bool readChunk(uint32_t sector, uint32_t count);
  bool writeChunk(uint32_t sector, uint32_t count);
  SectorIoDevice& device_;
  uint8_t* bounce_;
  uint32_t capacity_;
};

}  // namespace mt
