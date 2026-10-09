#pragma once
#include <stdint.h>
#include "link_msg.h"

class SynthStream;

// FwFromFile (link_msg.h): a firmware .hex from the card staged in the flash's OTA area
// (flashmap::kOtaBase), checked, then moved over the running firmware (FlasherX's way) and the board
// rebooted. loop() only.
namespace fw {

void begin(SynthStream& out);
bool isRequest(mt::link::Msg t);
// FwFromFile (frame seq) -> its FwRep payload in out (kMaxPayload), its size; Progress frames while
// the file is read (tens of seconds). After an Ok reply step() replaces the firmware.
int handle(mt::link::Msg t, uint8_t seq, const uint8_t* p, int n, uint8_t* out);
// The ESP (re)started: its seqs start over, so the last reply is no longer one to repeat.
void reset();
// loop(): an image staged and answered -> the reply flushed, the image moved, reboot (no return).
void step();

}  // namespace fw
