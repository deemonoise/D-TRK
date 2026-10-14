#pragma once
#include <stdint.h>
#include "link_msg.h"
#include "stream_sched.h"

namespace mt {
struct SynthModel;
}
class SynthStream;

// PreviewFile: a WAV on the card streamed into the output (FILE import list). loop() reads the file
// into a ring on a stream pool slot (3072 frames, about 70 ms) at the output rate (mono mix, linear resampling); the audio interrupt adds it at
// the level of a sample instrument at full volume (MAIN). Running dry: silence, counted
// in the link log when it ends.
namespace preview {

void begin(mt::SynthModel& model, SynthStream& out);
bool isRequest(mt::link::Msg t);  // PreviewFile
// PreviewFile -> its reply payload in out (kMaxPayload), its size.
int handle(mt::link::Msg t, const uint8_t* p, int n, uint8_t* out);
void stop();  // PreviewStop
// loop(): watches for the preview's end (the stream scheduler tops the ring up via consumer()).
void step();
mt::StreamConsumer* consumer();

}  // namespace preview
