#pragma once
#include <stdint.h>
#include "link_msg.h"

namespace mt { struct BgWork; }

// The synth board's card (built-in SD slot, FAT32 / exFAT) served to the ESP over the link
// (lib/core link_fs). loop() only.
namespace card {

// Mounts the card when one is in and creates the standard folders; a card put in later is mounted
// by the next request.
void begin();
// Whether queued events, active voices and gate playback are idle. Checked with slot ownership under
// AudioNoInterrupts before long filesystem work; an unset callback conservatively blocks it.
// The loop must be the only queue producer while the filesystem operation runs.
void setAudioIdle(bool (*check)());
// The card is in and mounted (remounts one put back in).
bool ready();
// Raw sectors through the same protected, cache-coherent bounce path as filesystem reads.
// Call ready() before this. A bounce chunk is copied only after a successful transfer.
bool readSectors(uint32_t sector, uint32_t count, uint8_t* out);
// Cached state / SDK error registers only; no card transaction (safe for Hello diagnostics).
void logState();
// Files were written outside the Fs requests (bank jobs): the cached free space is stale.
void spaceChanged();
// An Fs* request (frame seq): copies it for background execution. -1 when queued / pending retry
// or t is not an Fs request; >=0 is an immediate reply in out (cached result / rejected overlap).
int handle(mt::link::Msg t, uint8_t seq, const uint8_t* p, int n, uint8_t* out);
// Register once with stream::addBackground after stream::begin. The scheduler requires >50 ms reserve;
// monolithic scans / copy / metadata writes additionally require idle streams.
mt::BgWork* background();
// Every loop pass, even when the scheduler cannot admit background work: expires a request
// after its bounded queue wait with an explicit error reply and no card access.
void step();
// The ESP (re)started: cancel queued requests now; close old handles in an admitted idle
// background portion before executing any new filesystem request. No card I/O here.
void reset();

}  // namespace card
