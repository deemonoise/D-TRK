#pragma once
#include <stdint.h>
#include "synth_model.h"

namespace mt::link {

// ESP side: which chunks differ from what the Teensy acknowledged.
class StateMirror {
 public:
  StateMirror() { invalidate(); }
  void invalidate();  // Teensy rebooted / new project: everything is dirty
  // Next dirty chunk of `live` not in flight (send order), -1 if none. Marks it in flight.
  int nextDirty(const SynthModel& live);
  // The chunk's bytes as they were sent (not live: an edit made meanwhile stays dirty).
  void acked(uint16_t chunk, const uint8_t* sent);
  void failed(uint16_t chunk);  // not in flight any more, stays dirty
  bool synced(const SynthModel& live) const;  // nothing dirty, nothing in flight
  bool inFlight() const;

 private:
  bool dirty(int i, const SynthModel& live) const;

  SynthModel shadow_;
  bool valid_[kChunks];
  bool inFlight_[kChunks];
};

}  // namespace mt::link
