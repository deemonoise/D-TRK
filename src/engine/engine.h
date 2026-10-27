#pragma once
#include <stdint.h>
#include "evolve.h"
#include "model.h"
#include "mut.h"
#include "shift_gen.h"
#include "var_gen.h"

namespace engine {

// SendProgram: arg = track. ReleaseTies: after the UI changed heard pattern data under a held TIE.
// SongEdit: arg = (slot << 8) | mt::SongOp, after a write to Project.song / songMute / songLen.
// Post it while still holding lockProject(): the engine handles it before planning on the new song.
// SetSong: arg = on << 15 | slot: SONG from that slot / LOOP (Sequencer::setSong).
// TrackOut: arg = track, after TrackCfg::out changed (post ReleaseTies first): ends its sounding notes.
// Fill: arg 1 = fill held, 0 = released.
// GenEdit: after a Project::gen edit while playing: the unheard steps are planned again with it.
// GenReseed: arg = track, after its GenCfg::seed changed: replanned, its register reset from the next step.
// MutEdit: after a Project::evo / var (MUT) edit: the unheard steps are planned again with it; a track
// whose MUT mode changed starts over (no EVO changes, VAR generation 0). Sequencer::mutEdited.
enum class Cmd : uint8_t {
  StartStop, TogglePlay, Stop, QueuePattern, SelectPattern, SetBpm, SendProgram, ReleaseTies, SongEdit, TrackOut,
  Fill, SetSong, GenEdit, GenReseed, MutEdit, SetLiveBpm
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

// First thing at boot (core_ram): the internal RAM the sequencer is built in (null: allocated in begin).
void reserve(void* mem);
void begin(mt::Project* p);
bool seqInternal();  // the sequencer ended up in internal RAM
// Moves the sequencer (~34 KB) into `dst` (a byte copy under the project mutex). Returns the memory
// it left (the caller frees it). core_ram uses it for Wi-Fi (transport stopped).
void* moveSeq(void* dst);
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
// While a song slot's BPM holds the live tempo (p.bpm): the project BPM, see Sequencer::savedBpm. Call
// holding lockProject().
bool savedBpm(uint16_t& bpm);
// The project BPM (PROJ shows and edits it): p.bpm unless a song slot's BPM holds the live tempo. Takes
// lockProject() itself: do not call holding it.
uint16_t projectBpm(const mt::Project& p);
// Track generators, holding lockProject(): the register after the heard step (the reset one while
// stopped), and the register the next planned step starts from with that step of `track` in pattern pat
// (Capture; step 0 while stopped or another pattern plays; stopped: the reset register).
mt::GenState heardGen(int track);
mt::GenState nextGen(int track, int pat, int& fromStep);
// Track evolve, holding lockProject(): the state after the heard step of `track` (copied; the reset one
// while stopped) and the passes counted towards its next mutation.
void heardEvo(int track, mt::EvoTrack& out, uint8_t& passes);
// The same without the copy (UI header): mutations, changed steps of the first len, passes counted.
struct EvoInfo {
  uint32_t n;
  int changed;
  uint8_t passes;
};
EvoInfo heardEvoInfo(int track, int len);
// Track VAR, holding lockProject(): the state after the heard step of `track` (copied; the reset one while
// stopped) and the passes counted towards its next generation.
void heardVar(int track, mt::VarTrack& out, uint8_t& passes);
// The same without the copy (UI header redraws).
struct VarInfo {
  uint32_t n;
  uint8_t sinceReset;
  uint8_t passes;
};
VarInfo heardVarInfo(int track);
// Clears the track's MUT state (no EVO changes, VAR generation 0; a rewritten track: Dice), holding
// lockProject(): the engine does it before it plans again (Sequencer::mutClear). A flag, not a command:
// it cannot be lost to a full queue.
void mutClear(int track);
// Capture live (Sequencer::captureLive), holding lockProject(): writes what `track` plays now (MUT, then
// GEN) into pattern pat, GEN off, MUT state cleared; the engine plans the unheard steps again from it.
// scratch: kMaxSteps steps (App::undoScratch). Push the undo before, post Cmd::ReleaseTies after.
mt::LiveCapture captureLive(int track, int pat, mt::Step* scratch);

}  // namespace engine
