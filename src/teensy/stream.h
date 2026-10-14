#pragma once
#include <stdint.h>
#include "sample_store.h"
#include "stream_pool.h"
#include "stream_sched.h"

namespace mt {
class Synth;
}

// Sample streaming on the synth board: the slot pool (RAM2), the store table, card reads of raw sectors
// and the loop() scheduler (voice / preview loads first, background work when every reserve allows).
namespace stream {

void begin(mt::Synth& s);
// One loop() pass: schedulePass over the slots and the preview, then one background portion.
void step();
// Background work, run round robin when every consumer has > 30 ms (bank jobs, tools, PSRAM fill).
void addBackground(mt::BgWork* w);
// Re-reads the store entries of the model's samples (contiguous files only). The synth is parked.
// Returns the samples found; logs "store: N of M samples" when log (not before link::begin).
int rescan(const mt::SynthModel& m, bool log = true);
// Registers one store file now (an import just wrote it). The synth may be running.
bool add(uint32_t crc);
// A prefetch hint from the link (kSynthHint note ofs): a Prefetch slot for the track's next note-on.
// loop() (link::poll); counts in Stats::hints.
void hint(uint8_t track, uint8_t note, uint8_t ofs);

mt::StreamPool& pool();
mt::StoreTable& table();
mt::BlockReader& reader();  // raw card reads by key (also the offline render's sync reader)

struct Stats {
  uint8_t voices = 0;          // slots owned by voices now
  uint16_t underruns = 0;      // since the last take
  uint16_t minReserveMs = 0;   // lowest voice reserve seen since the last take (0xFFFF = none)
  uint16_t hints = 0, hintSlots = 0;  // since the last take: hints, slots they took
};
constexpr int kStatReaders = 2;  // 0: Status, 1: the desk bench
// Reads and resets the counters of one reader (who < kStatReaders); the readers are independent.
Stats takeStats(int who = 0);

}  // namespace stream
