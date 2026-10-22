#include "follow.h"
#include "shift_gen.h"

namespace mt {

uint32_t followRunSeed(uint16_t seed, uint32_t salt) { return genHash(seed, salt, 0x464F4Cu); }  // "FOL"

int followNext(const Project& p, int slot, uint32_t runSeed, uint32_t n) {
  const int len = p.songLen > kSongMax ? kSongMax : p.songLen;
  if (slot < 0 || slot >= len) return kFollowEnd;
  const int next = slot + 1 < len ? slot + 1 : kFollowEnd;
  const SongFollow& f = p.songFol[slot];
  const FollowAct a = f.act < static_cast<uint8_t>(FollowAct::Count) ? static_cast<FollowAct>(f.act) : FollowAct::Next;
  if (a == FollowAct::Next) return next;
  const uint32_t h = genHash(runSeed, n, static_cast<uint32_t>(slot));
  if (h % 100u >= (f.chance > 100 ? 100u : f.chance)) return next;
  switch (a) {
    case FollowAct::Prev: return slot > 0 ? slot - 1 : len - 1;
    case FollowAct::Stay: return slot;
    case FollowAct::Jump: return f.target < len ? f.target : len - 1;
    case FollowAct::Random: {
      if (len < 2) return slot;
      const int k = static_cast<int>((h / 100u) % static_cast<uint32_t>(len - 1));
      return k >= slot ? k + 1 : k;
    }
    default: return next;
  }
}

const char* followName(uint8_t act) {
  static const char* const kNames[] = {"NEXT", "PREV", "STAY", "JUMP", "RAND"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<int>(FollowAct::Count), "follow names");
  return act < static_cast<uint8_t>(FollowAct::Count) ? kNames[act] : kNames[0];
}

}  // namespace mt
