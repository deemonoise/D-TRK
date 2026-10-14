#pragma once
#include <stdint.h>

namespace mt {
struct SynthModel;
class Synth;
}  // namespace mt
class SynthStream;

// Stream bench (desk build): the 16 longest files of /mt-store become SAMPLE instruments
// (kBenchInstr ..), looped from their start and held on tracks 0..15 at the root + semis. Every second
// a log line with the stream stats and the render CPU, at the end a summary. kBenchHeavy: 16 stream
// voices on tracks 0..7 (two notes each) and kFmVoiceMax heavy voices (DRUM / FM TONE, kBenchWt: wavetable
// SYNTH) on 8..15; kBenchFx: drive / delay / reverb on every voice, comp. It overwrites the model's samples, those
// instruments and the tracks: on the desk there is no ESP mirror to keep.
namespace sbench {

void begin(mt::SynthModel& m, mt::Synth& s, SynthStream& out);
void start(uint8_t semis, uint8_t flags, uint16_t secs);  // flags: mt::link::kBench*
void stop();
void step();  // loop()

}  // namespace sbench
