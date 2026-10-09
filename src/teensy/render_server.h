#pragma once
#include <stdint.h>
#include "link_msg.h"

namespace mt {
class Synth;
}
class SynthStream;

// Renders on the synth board (RenderStart / RenderBlocks / RenderEnd, link_msg.h): the live output
// parked, the synth driven from loop() by the ESP's block events, the result written to the card
// (stereo WAV) or into the sample bank (mono mix). loop() only.
namespace render {

void begin(mt::Synth& synth, SynthStream& out);
bool isRequest(mt::link::Msg t);
// A render request (frame seq) -> its RenderRep payload in out (kMaxPayload), its size. RenderEnd may
// take seconds (normalizing, the bank write): Progress frames meanwhile.
int handle(mt::link::Msg t, uint8_t seq, const uint8_t* p, int n, uint8_t* out);
// The ESP (re)started: a render in progress is dropped.
void reset();
bool active();
// loop(): a render the ESP stopped feeding is dropped after kIdleMs.
void step();

constexpr uint32_t kIdleMs = 10000;

}  // namespace render
