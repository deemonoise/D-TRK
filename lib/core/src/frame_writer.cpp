#include "frame_writer.h"
#include <string.h>
#include "hot.h"

namespace mt::link {

MT_CONTROL void FrameWriter::observe(bool connected) {
  if (connected_ && !connected) {
    stats_.closedDropped += count_;
    head_ = count_ = 0;
    // USB may already hold a prefix from the old host. Never join it to a new session's reply.
    delimiter_ = true;
  }
  connected_ = connected;
}

MT_CONTROL bool FrameWriter::submit(FramePort& port, const uint8_t* bytes, int size, bool critical) {
  if (!bytes || size <= 0 || size > kMaxEncoded || bytes[size - 1] != 0) {
    ++stats_.invalid;
    return false;
  }
  observe(port.connected());
  if (!connected_) {
    ++stats_.closedDropped;
    return false;
  }
  if (!critical && (count_ || delimiter_ || port.capacity() < size)) {
    ++stats_.telemetryDropped;
    return false;
  }
  if (!roomForCritical()) {
    ++stats_.overflow;
    return false;
  }
  Pending& pending = queue_[(head_ + count_) % kQueue];
  memcpy(pending.bytes, bytes, size);
  pending.size = static_cast<uint16_t>(size);
  pending.sent = 0;
  ++count_;
  return true;
}

MT_CONTROL bool FrameWriter::step(FramePort& port) {
  observe(port.connected());
  if (!connected_) return false;
  if (delimiter_) {
    if (port.capacity() < 1) return false;
    const uint8_t zero = 0;
    if (port.write(&zero, 1) != 1) {
      ++stats_.shortWrites;
      return false;
    }
    delimiter_ = false;
    return true;
  }
  if (!count_) return false;
  Pending& pending = queue_[head_];
  const size_t left = pending.size - pending.sent;
  if (port.capacity() < static_cast<int>(left)) return false;
  const size_t sent = port.write(pending.bytes + pending.sent, left);
  if (sent < left) ++stats_.shortWrites;
  pending.sent += static_cast<uint16_t>(sent);
  if (pending.sent == pending.size) {
    head_ = (head_ + 1) % kQueue;
    --count_;
  }
  return sent != 0;
}

MT_CONTROL FrameTxStats FrameWriter::stats() const {
  FrameTxStats result = stats_;
  result.pending = count_;
  return result;
}

}  // namespace mt::link
