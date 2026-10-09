#pragma once
#include <stdint.h>
#include "link_msg.h"

// The synth board's card (built-in SD slot, FAT32 / exFAT) served to the ESP over the link
// (lib/core link_fs). loop() only.
namespace card {

// Mounts the card when one is in and creates the standard folders; a card put in later is mounted
// by the next request.
void begin();
// The card is in and mounted (remounts one put back in).
bool ready();
// Files were written outside the Fs requests (bank jobs): the cached free space is stale.
void spaceChanged();
// An Fs* request (frame seq) -> its reply payload in out (kMaxPayload); -1 when t is not one.
int handle(mt::link::Msg t, uint8_t seq, const uint8_t* p, int n, uint8_t* out);
// The ESP (re)started: its files are closed.
void reset();

}  // namespace card
