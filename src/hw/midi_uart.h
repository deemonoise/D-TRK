#pragma once
#include "sequencer.h"

namespace hw {

// UART1 TX only on GPIO10, 31250 baud. RX is never routed, so no pin conflicts.
class MidiUart : public mt::MidiSink {
 public:
  void begin();
  void send(const uint8_t* b, uint8_t len) override;
};

}  // namespace hw
