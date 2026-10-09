#pragma once
#include <stdint.h>
#include "bank_ops.h"
#include "link_msg.h"

namespace mt {
struct SynthModel;
class Synth;
}
class SynthStream;

// The sample bank in program flash (flash_bank.h) and its jobs on the card (lib/core bank_ops):
// the synth's samples and wavetables, BankSync / AssetsSave / BankImport / SampleInfo / BankIndex /
// BankClear / WtFrame requests from the ESP. loop() only.
namespace bank {

// Mounts the bank and adds the built-in wavetables it lacks (first boot: several seconds), then
// hands the synth its sources. Before the delay line takes the RAM2 heap (needs a 96 KB table).
void begin(mt::SynthModel& model, mt::Synth& synth, SynthStream& out);
// One line about the bank for the link log (after link::begin).
void logState();

bool isRequest(mt::link::Msg t);
// Request t (frame seq) -> its reply payload in out (kMaxPayload), or -1 when the reply comes later
// (a job started: step() sends it, with Progress frames meanwhile). The same request again (the ESP
// retried) gets a Progress while it runs, its reply again after it ended; another bank job request
// while one runs gets Busy.
int handle(mt::link::Msg t, uint8_t seq, const uint8_t* p, int n, uint8_t* out);
// The bank data of project sample index for the editor tools; nullptr while not cached, without a
// bank or while a job changes the bank. gen: the bank generation (the data may differ when it does).
const int16_t* sampleData(int index, uint32_t& frames, uint32_t& rate, uint32_t& gen);

// For a render into the cache (render_server, the output parked by it): Ok, NoBank or Busy (a job
// runs).
mt::BankResult writable();
// mt::bankWriteFrames on the bank; keep gets the long parts' progress (the caller's Progress).
mt::BankResult writeFrames(uint32_t frames, uint32_t rate, mt::FrameFill fill, void* ctx, mt::ImportOut& out,
                           mt::BankProgress keep, void* keepCtx);

// The ESP (re)started: its seqs start over, so the last reply is no longer one to repeat.
void reset();

// The running job's next piece (about kStepBytes of card I/O), then its reply when it ends.
void step();

}  // namespace bank
