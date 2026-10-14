#pragma once
#include <stdint.h>

namespace mt {
struct SynthModel;
class Synth;
}  // namespace mt

// The optional PSRAM (an 8 MB chip on the CS0 pad, found and mapped by startup.c): boot self-test, a watchdog every
// 100 ms, the PERF ring buffer (reserved at the arena's start) and the whole-sample cache of the
// project (PsramFill, background work of stream::step).
namespace psram {

enum class State : uint8_t { Off, On, Failed };  // Off: no chip on CS0 / self-test failed; Failed: watchdog

// The PERF buffer held in PSRAM: 6 s stereo (6 x 44,100 x 2 x 2 = 1,058,400 B).
constexpr uint8_t kPerfSec = 6;

// setup(), after stream::begin and before allocPerf / the audio; reserves the PERF buffer, then
// starts the fill for m. s: the PERF buffer is detached from it if the watchdog fails.
void begin(const mt::SynthModel& m, mt::Synth& s);
// The reserved PERF buffer (frames stereo frames, its contents undefined), nullptr when PSRAM is off.
int16_t* perfBuffer(uint32_t& frames);
// loop(): the watchdog. A failure clears pool.psramOk for good (voices go to SD at the next block)
// and silences PERF until a reboot.
void step();
// After stream::rescan (synth parked): the copies of the new model, shortest first.
void restart(const mt::SynthModel& m);
State state();
uint8_t fillPct();     // of the planned bytes; 100 when nothing is planned
// Copies of the current plan: verified, failed the CRC check, planned (fillPct reaches 100 with the
// last copy, its CRC check still to come).
void fillCounts(int& ready, int& bad, int& planned);
void injectFault();    // desk: the next watchdog check reads back a flipped bit
void hold(bool on);    // desk stream bench: no copies, every sample streams

}  // namespace psram
