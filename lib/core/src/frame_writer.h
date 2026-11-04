#pragma once
#include <stddef.h>
#include <stdint.h>
#include "link_frame.h"

namespace mt::link {

// One loop() writer owns the port. Capacity means bytes write() can accept without waiting;
// write() must return at most size, as the USB SDK does.
struct FramePort {
  virtual bool connected() = 0;
  virtual int capacity() = 0;
  virtual size_t write(const uint8_t* bytes, size_t size) = 0;
};

struct FrameTxStats {
  uint32_t telemetryDropped = 0, closedDropped = 0, overflow = 0, shortWrites = 0, invalid = 0;
  uint16_t pending = 0;
};

// Desktop USB only: bounded whole-frame admission and ordered recovery from unexpected short writes.
// A disconnected session is abandoned; the next one gets a delimiter before any new frame.
class FrameWriter {
 public:
  static constexpr int kQueue = 8;
  bool submit(FramePort& port, const uint8_t* bytes, int size, bool critical);
  // At most one nonblocking port write. No telemetry can interrupt a partially sent frame.
  bool step(FramePort& port);
  bool roomForCritical() const { return count_ < kQueue; }
  FrameTxStats stats() const;

 private:
  void observe(bool connected);
  struct Pending {
    uint8_t bytes[kMaxEncoded];
    uint16_t size, sent;
  } queue_[kQueue];
  uint8_t head_ = 0, count_ = 0;
  bool connected_ = false, delimiter_ = false;
  FrameTxStats stats_;
};

}  // namespace mt::link
