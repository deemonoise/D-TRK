#pragma once
#include <stdint.h>
#include "model.h"

namespace engine {

// SendProgram: arg = track. ReleaseTies: after the UI changed heard pattern data under a held TIE.
// ChainEdit: arg = (row << 8) | mt::ChainOp, after a write to Project.chain / chainLen / songMode.
// Post it while still holding lockProject(): the engine handles it before planning on the new chain.
enum class Cmd : uint8_t {
  StartStop, TogglePlay, Stop, QueuePattern, SelectPattern, SetBpm, SendProgram, ReleaseTies, ChainEdit
};

struct Command {
  Cmd cmd;
  uint16_t arg;
};

struct Status {
  bool playing;
  bool paused;
  uint8_t pattern;
  int8_t queued;
  uint8_t pos;
  uint32_t loop;
  int8_t songPos;  // chain index of the heard pattern, -1 outside song mode
  bool operator==(const Status& o) const {
    return playing == o.playing && paused == o.paused && pattern == o.pattern && queued == o.queued &&
           pos == o.pos && loop == o.loop && songPos == o.songPos;
  }
};

void begin(mt::Project* p);
// False when the command queue was full (the command is lost).
bool post(Cmd c, uint16_t arg = 0);
Status status();

// Hold while writing project data from the UI. Keep it short: the engine waits on it.
void lockProject();
void unlockProject();

}  // namespace engine
