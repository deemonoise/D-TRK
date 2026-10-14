#pragma once

namespace mt {
struct SynthModel;
class Synth;
}
class SynthStream;

// Voice pool bench (teensy41-bench, -DAUDIO_BENCH_POOL): see bench.cpp; the voice ceiling bench
// (teensy41-bench-max, also -DAUDIO_BENCH_MAX): see bench_max.cpp. begin() from setup() after
// link::begin, step() from loop().
namespace bench {

void begin(mt::SynthModel& model, mt::Synth& synth, SynthStream& out);
void step();

}  // namespace bench
