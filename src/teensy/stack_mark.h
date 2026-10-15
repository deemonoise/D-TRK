#pragma once
#include <stdint.h>

// Stack low-water mark. The main loop and every interrupt share one stack in RAM1 (DTCM), from
// _estack down to _ebss ("free for local variables" in the build): an overflow runs into the MPU
// guard at _ebss. paint() fills the unused part with a pattern; minFree() counts the bytes above the
// guard that nothing has written since.
namespace stackmark {

// setup(), first: paints from the guard up to just below the caller's frame.
void paint();
// Bytes never touched since paint(), 0xFFFFFFFF (link Status::kStackUnknown) before it; 0 = the stack
// reached the guard. Only writes count: locals a frame allocates but does not (yet) write keep the
// pattern, so the depth can be under-reported (the free figure too high), never over-reported.
uint32_t minFree();

}  // namespace stackmark
