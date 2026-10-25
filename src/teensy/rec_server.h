#pragma once
#include <stdint.h>
#include "link_msg.h"

class SynthStream;

// Recording on the synth board (RecStart / RecSet / RecLevel / RecStop / RecCancel, link_msg.h): the
// audio interrupt feeds an mt::RecCapture (ring in PSRAM), loop() drains it into
// /samples/rec/RECn.wav.tmp; STOP seals the file (header, rename) and imports it into the store.
// loop() only; the code lives in flash (imxrt1062_t41_mt.ld).
namespace rec {

void begin(SynthStream& out);
bool isRequest(mt::link::Msg t);
// A request (frame seq) -> its reply payload in out (kMaxPayload), its size; -1 for RecStop: step()
// sends Progress frames, then the RecStopRep.
int handle(mt::link::Msg t, uint8_t seq, const uint8_t* p, int n, uint8_t* out);
// The ESP (re)started: a take is sealed into its file without the store, the meter and MON stop.
void reset();
// The ring into the file (about 2 ms a pass), STOP's sealing and import, the keepalive.
void step();
// A take owns the card (armed, recording, sealing or importing): bank jobs and renders wait.
bool busy();

// Without a RecLevel (or RecSet / RecStart) for this long: a take is sealed without the store, an
// open window's meter and MON stop.
constexpr uint32_t kKeepaliveMs = 2000;

}  // namespace rec
