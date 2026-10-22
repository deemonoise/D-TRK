#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

// Follow actions (Project::songFol): after a slot's pass the song goes on with the slot its action
// picks. NEXT (and a failed chance) is the next slot; from the last slot it leaves the song
// (kFollowEnd: live play wraps to slot 0, a render ends). PREV from slot 0 is the last slot; STAY plays
// the slot again (one more pass, then a new decision); JUMP goes to target (past the song: the last
// slot); RANDOM to any other slot (itself in a one-slot song). Unknown actions are NEXT.
// A decision is a hash of (run seed, decision count, slot), not a shared generator (as GEN, shift_gen.h):
// one run seed always walks the same path, whatever else happens (PRB, replanning, Render).
constexpr int kFollowEnd = -1;

// The run seed: Project::seed mixed with salt (0 = the render's path; live play: a fresh salt per start).
uint32_t followRunSeed(uint16_t seed, uint32_t salt);
// The slot after a pass of `slot`, decision n (0 = the first since the start); kFollowEnd past the last
// slot, also for a slot outside the song.
int followNext(const Project& p, int slot, uint32_t runSeed, uint32_t n);
// The path a song render plays from slot 0: visit(slot) per pass, at most maxSlots passes, until the song
// leaves past its last slot. Returns the passes (0 for an empty song).
template <class F>
int followWalk(const Project& p, uint32_t runSeed, int maxSlots, F&& visit) {
  if (p.songLen == 0) return 0;
  int slot = 0, n = 0;
  while (n < maxSlots) {
    visit(slot);
    const int nx = followNext(p, slot, runSeed, static_cast<uint32_t>(n));
    ++n;
    if (nx < 0) break;
    slot = nx;
  }
  return n;
}
// UI name, 4 chars: NEXT PREV STAY JUMP RAND (unknown: NEXT).
const char* followName(uint8_t act);

}  // namespace mt
