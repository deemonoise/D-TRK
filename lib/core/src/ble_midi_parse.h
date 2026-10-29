#pragma once
#include <stdint.h>

namespace mt {

// A channel message (status 0x80..0xEF) or a real-time byte. d2 = 0 for one-data-byte messages
// (program change, channel pressure); d1 = d2 = 0 for real-time.
struct MidiMsg {
  uint8_t status, d1, d2;
};

// Data bytes after a channel status: 1 for program change / channel pressure, else 2.
int midiDataLen(uint8_t status);

// BLE-MIDI packets (header, timestamp bytes, running status) -> messages. SysEx is skipped, also
// across packets. One parser per connection: running status carries over packets, a message
// cut at a packet end is dropped. Runs in the NimBLE notify callback, so no allocation.
class BleMidiParser {
 public:
  using Sink = void (*)(const MidiMsg& m, void* ctx);
  void feed(const uint8_t* b, int n, Sink out, void* ctx);
  void reset() {
    cancelRunning();
    sysex_ = false;
  }

 private:
  void cancelRunning() {
    status_ = 0;
    have_ = 0;
  }

  uint8_t status_ = 0;  // running status, 0 = none
  uint8_t data_[2] = {};
  uint8_t have_ = 0;  // data bytes taken for status_
  bool sysex_ = false;
};

// A message to send: a channel message (len = 1 + midiDataLen) or a real-time byte (len 1) at a
// time in ms (any epoch).
struct TimedMsg {
  uint32_t ms;
  uint8_t b[3];
  uint8_t len;
};

// Messages -> one BLE-MIDI packet of at most cap bytes (the connection's MTU - 3). The header
// carries bits 12..7 of the first written message's time; every message gets a timestamp byte
// (bits 6..0), which may wrap inside the packet (the spec allows it). A packet ends early when a
// message is more than 127 ms after the previous one or earlier than it. Running status: a
// channel message with the same status as the channel message before it is sent as timestamp +
// data (status omitted); the data-only form is never used, for compatibility. Real-time is
// timestamp + byte, and the next channel message writes its status again (simpler receivers).
// Messages are never split. Bad messages (len 0 or > 3, length not matching the status, SysEx /
// system common, data >= 0x80) are skipped and counted in `taken`. Returns the bytes written (0
// when n == 0, cap < 3 or the first good message does not fit: the caller needs a bigger cap);
// `taken` = messages consumed. A return of 0 with taken > 0 (only bad messages): consume them,
// send no packet.
int blePack(const TimedMsg* in, int n, uint8_t* out, int cap, int& taken);

}  // namespace mt
