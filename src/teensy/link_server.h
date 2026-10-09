#pragma once
#include <stdint.h>
#include "link_msg.h"

namespace mt {
struct SynthModel;
class Synth;
}
class SynthStream;

// The synth board's end of the link: Serial1 to the ESP (USB Serial under LINK_DESK). loop() only.
namespace link {

// Events reach the synth this long after their ESP time (then ~2 blocks to the DAC): one block
// period (2.9 ms) plus the link jitter.
constexpr uint32_t kPlayLatencyUs = 4000;
constexpr uint32_t kStatusMs = 40;

void begin(mt::SynthModel& model, mt::Synth& synth, SynthStream& out);
void poll();
// A reply (or Progress) frame of type t carrying a request's seq.
void reply(mt::link::Msg t, uint8_t seq, const uint8_t* p, int n);
template <class M>
void reply(mt::link::Msg t, uint8_t seq, const M& m) {
  uint8_t p[mt::link::kMaxPayload];
  mt::link::Writer w(p, sizeof p);
  encode(m, w);
  if (w.ok()) reply(t, seq, p, w.size());
}
// Waits until every frame queued for the ESP is out (before a reboot).
void flushOut();
// From work that keeps loop() busy (bank jobs): a Status when one is due, so the ESP sees the board
// alive.
void statusIfDue();
// printf into a Log frame (up to 200 chars), and to USB Serial when that is not the link.
void log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

}  // namespace link
