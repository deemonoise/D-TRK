#pragma once
#include <stdint.h>
#include "link_msg.h"

// Sample editor requests on the bank data (bank::sampleData): WavePeaks, Onsets. loop() only.
namespace tools {

bool isRequest(mt::link::Msg t);
// Request t -> its reply payload in out (kMaxPayload), its size.
int handle(mt::link::Msg t, const uint8_t* p, int n, uint8_t* out);

}  // namespace tools
