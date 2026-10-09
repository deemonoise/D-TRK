#pragma once
#include <FS.h>
#include <stdint.h>

// The synth board's card as an Arduino fs::FS, over the link (lib/core link_fs). Reads come in
// 4 KB look-ahead runs, writes go out in 4 KB runs, folders are listed a frame at a time. UI task
// only; every call blocks until the synth board answers (no answer: the call fails).
namespace storage {

fs::FS& remoteFs();
// The last operation's result: mt::link::kErrOk, kErrNoCard (no card in the synth board), kErrLink
// (no answer), ...
int16_t remoteLastError();
// The card's size and free space in MB (FsStat with the space flag), asked at most every 10 s.
// False when there is no card / no answer.
bool remoteSpace(uint32_t& totalMb, uint32_t& freeMb);

}  // namespace storage
