#pragma once
#include <stdint.h>
#include "bank_ops.h"
#include "link_msg.h"

namespace mt {
struct SynthModel;
class Synth;
}
class SynthStream;

// The sample store on the card (lib/core sample_store: the synth's samples, streamed) and the bank in
// program flash (flash_bank.h: wavetables, heads, legacy samples until migrated) with their jobs:
// BankSync / AssetsSave / BankImport / SampleInfo / BankIndex / BankClear / WtFrame requests from the
// ESP. loop() only.
namespace bank {

// Mounts the bank and adds the built-in wavetables it lacks (first boot: several seconds), then
// hands the synth its sources. Before the delay line takes the RAM2 heap (needs a 96 KB table).
// Samples still in the bank (from before the store) start moving to the store in the background.
void begin(mt::SynthModel& model, mt::Synth& synth, SynthStream& out);
// One line about the bank for the link log (after link::begin).
void logState();

bool isRequest(mt::link::Msg t);
// Request t (frame seq) -> its reply payload in out (kMaxPayload), or -1 when the reply comes later
// (a job started: step() sends it, with Progress frames meanwhile). The same request again (the ESP
// retried) gets a Progress while it runs, its reply again after it ended; another bank job request
// while one runs gets Busy.
int handle(mt::link::Msg t, uint8_t seq, const uint8_t* p, int n, uint8_t* out);
// Project sample index for the editor tools: its store crc, frames and rate, and whole = all its
// frames in memory (the PSRAM copy, or a legacy flash bank entry), nullptr when they are only on the
// card (/mt-store/<crc>.wav). False when the sample is in neither (or a job changes the bank).
bool sampleAccess(int index, uint32_t& crc, uint32_t& frames, uint32_t& rate, const int16_t*& whole);
// The flash bank (heads "h" + crc, wavetables, legacy samples); changes only with the synth parked.
const mt::SampleBank& flash();

// For a render into the store (render_server, the output parked by it): Ok, NoSd or Busy (a job
// uses the card).
mt::BankResult writable();
// mt::storeWriteFrames on the card, the new file registered for streaming; keep gets the progress
// (the caller's Progress).
mt::BankResult writeFrames(uint32_t frames, uint32_t rate, mt::FrameFill fill, void* ctx, mt::ImportOut& out,
                           mt::BankProgress keep, void* keepCtx);

// A take's file (rec_server) into the store while the synth plays, as BankImport kind 0 but with no
// reply over the link: Running (importPoll follows it), NoSd, Busy or the start's error. Bank requests
// get Busy until it ends.
mt::BankResult importFile(const char* path);
// Running while it goes on (done / total of the store job for the take's Progress), then its result
// and the import (crc, frames in the store) until the next importFile.
mt::BankResult importPoll(mt::ImportOut& o, uint32_t& done, uint32_t& total);

// The ESP (re)started: its seqs start over, so the last reply is no longer one to repeat.
void reset();

// The running job's next piece (about kStepBytes of card I/O), then its reply when it ends. False: no
// job runs. The stream scheduler calls it as background work (registered by begin()).
bool bgStep();

// loop(): a sample the model gained on the fly (a render, a take, an import the ESP added after it)
// has no heads and no PSRAM copy until a BankSync; once the set holds and nothing sounds or uses the
// card, the heads and the fill are redone as a BankSync does (the synth parked a moment).
void refreshIfDue();

}  // namespace bank
