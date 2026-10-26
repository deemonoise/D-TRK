#pragma once
#include <stdint.h>
#include "link_msg.h"

// Sample editor requests on a project sample (bank::sampleAccess): WavePeaks, Onsets, WtRender. In memory
// (PSRAM, legacy flash bank) they are answered at once; from the store file on the card they run as
// the stream scheduler's background work and answer later (Progress meanwhile). loop() only.
namespace tools {

void begin();  // registers the background work (after stream::begin)
// The ESP (re)started: no reply to repeat, no task to finish.
void reset();
bool isRequest(mt::link::Msg t);
// Request t (frame seq) -> its reply payload in out (kMaxPayload), or -1 when the reply comes later.
int handle(mt::link::Msg t, uint8_t seq, const uint8_t* p, int n, uint8_t* out);
// A task runs (it may hold a PSRAM copy or a store entry of the sample).
bool busy();

}  // namespace tools
