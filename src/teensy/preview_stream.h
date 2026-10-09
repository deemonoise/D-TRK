#pragma once
#include <stdint.h>
#include "link_msg.h"

namespace mt {
struct SynthModel;
}
class SynthStream;

// PreviewFile: a WAV on the card streamed into the output (FILE import list). loop() reads the file
// into a 16 KB ring at the output rate (mono mix, linear resampling); the audio interrupt adds it at
// the level of a sample instrument at full volume (MAIN). Running dry: silence, counted
// in the link log when it ends.
namespace preview {

void begin(mt::SynthModel& model, SynthStream& out);
bool isRequest(mt::link::Msg t);  // PreviewFile
// PreviewFile -> its reply payload in out (kMaxPayload), its size.
int handle(mt::link::Msg t, const uint8_t* p, int n, uint8_t* out);
void stop();  // PreviewStop
// loop(): tops the ring up from the file.
void step();

}  // namespace preview
