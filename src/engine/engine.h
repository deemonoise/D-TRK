#pragma once
#include <stdint.h>
#include "model.h"

namespace engine {

// SendProgram: arg = track. ReleaseTies: after the UI changed heard pattern data under a held TIE.
// SongEdit: arg = (slot << 8) | mt::SongOp, after a write to Project.song / songMute / songLen.
// Post it while still holding lockProject(): the engine handles it before planning on the new song.
// SetSong: arg = on << 15 | slot: SONG from that slot / LOOP (Sequencer::setSong).
// TrackOut: arg = track, after TrackCfg::out changed (post ReleaseTies first): ends its sounding notes.
// Fill: arg 1 = fill held, 0 = released.
enum class Cmd : uint8_t {
  StartStop, TogglePlay, Stop, QueuePattern, SelectPattern, SetBpm, SendProgram, ReleaseTies, SongEdit, TrackOut,
  Fill, SetSong
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
  int8_t songPos;  // song slot of the heard pattern, -1 outside the song
  bool fill;       // fill held
  bool song;       // SONG (else LOOP)
  // The heard step: start (nowUs() time) and length, for stepPhase() (not compared: no redraws).
  uint64_t stepT;
  uint32_t stepUs;
  bool operator==(const Status& o) const {
    return playing == o.playing && paused == o.paused && pattern == o.pattern && queued == o.queued &&
           pos == o.pos && loop == o.loop && songPos == o.songPos && fill == o.fill && song == o.song;
  }
};

// First thing at boot: takes the sequencer's internal RAM before anything else can fragment it.
void reserve();
void begin(mt::Project* p);
bool seqInternal();  // the sequencer ended up in internal RAM
// Wi-Fi needs the internal RAM: the sequencer (~33 KB) waits in PSRAM meanwhile (transport stopped).
// seqToInternal() brings it back when the room is there again, else it stays in PSRAM (slower).
void seqToPsram();
void seqToInternal();
// False when the command queue was full (the command is lost).
bool post(Cmd c, uint16_t arg = 0);
// As post, waiting up to 50 ms for room: for commands that must not be lost (fill / perf release).
bool postWait(Cmd c, uint16_t arg = 0);
Status status();
// Engine timer time, us (time stamps of synth events).
uint64_t nowUs();
// Position inside the heard step of s now, 0..255 (live recording).
uint8_t stepPhase(const Status& s);

// Hold while writing project data from the UI. Keep it short: the engine waits on it.
void lockProject();
void unlockProject();
// While the song plays: the mutes from before it (bit = track), see Sequencer::savedMutes.
// Call holding lockProject().
bool savedMutes(uint16_t& bits);

}  // namespace engine
