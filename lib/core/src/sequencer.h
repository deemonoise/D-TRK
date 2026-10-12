#pragma once
#include <stdint.h>
#include "event_heap.h"
#include "model.h"
#include "rng.h"
#include "step_expand.h"
#include "voices.h"

namespace mt {

class MidiSink {
 public:
  virtual ~MidiSink() = default;
  virtual void send(const uint8_t* b, uint8_t len) = 0;
  // Message for the internal synth (INT tracks), see Synth::event. t: when it should sound, us
  // (the scheduled time, at or a little before the call; the time passed to the call otherwise).
  virtual void synth(uint64_t t, uint8_t track, const uint8_t* b, uint8_t len) {
    (void)t;
    (void)track;
    (void)b;
    (void)len;
  }
};

constexpr uint64_t kNever = UINT64_MAX;

// What changed in Project.song / songMute / songLen (see Sequencer::songEdited).
enum class SongOp : uint8_t { Edit, Insert, Delete };
constexpr uint32_t kTieOverlapUs = 1000;  // tied note ends this long after the next note starts

// Hardware-free sequencer. The caller passes the current time in us and calls
// process() again no later than the time it returns.
//
// SONG or LOOP is a runtime flag (setSong), not part of the project. SONG (with songLen > 0) plays
// the slots of Project.song: start() begins at the slot given to setSong and every pattern end
// advances to the next slot, after the last back to slot 0. The next slot is decided when the last
// step of a pass is scheduled, like a queued pattern, so rewind() undoes it together with that step.
// queuePattern() is ignored while the song plays (stopped it works); selectPattern() still switches
// at once and the song goes on from its position. setSong while playing takes effect at the pass
// end: on goes to the given slot, off keeps the heard pattern looping. With no slots the current
// pattern loops. Song edits take effect at the next pattern end: slots are clamped to valid
// patterns, a position beyond a shrunk song wraps to slot 0. After every song write the caller
// calls songEdited() (under the same lock): it re-decides unheard advances and keeps the position
// on the same slot when slots are inserted or deleted before it. A decision whose last step has
// been heard stays. Stopped, no slot is shown (heardSongPos() == -1).
//
// A slot that plays the same pattern as the one before goes on like a loop (ties and CND continue).
// When a slot starts, its songMute mask is written into every TrackCfg::mute (newly muted tracks
// go silent); a live mute change holds until the next slot start. The first slot start saves the
// mutes, which come back when the song stops being heard: stop(), or a pass end outside the song (LOOP,
// no slots). Pause keeps them. Pattern::trackLen makes a track
// loop on its own length inside the pass; the pass counter follows the pattern length.
//
// ARS (step arp): a note step with ARS plays its first arp note, every following step of the track
// without a note plays the next one (every `div` steps) until a note step that plays, OFF (note or
// fx), a muted note step or a pattern change; it goes on over a loop of the same pattern, like TIE.
//
// Live: setFill() is the held fill (CND FIL / NFL), perfOn() a punch-in effect on a track until
// perfOff() (or stop()). Both apply to steps planned from then on (at most the lookahead late).
//
// Pattern::trackSpeed: an xN track plays N of its steps per pattern step, squeezed evenly into the
// slot up to the next (swung / grooved) pattern step; the groove accent of the pattern step applies to
// all of them. A /D track plays one step every D pattern steps, counted from loop_ through pass ends
// (a new pattern starts it at step 0); between them it is not touched (TIE, ARS go on). GAT, RAT, NDG,
// TIE, ARS, VSL count from the track's own step; CND counts pattern passes. Limits: NDG on a slow track
// is planned at most kLookTicks ahead, and the synth's step length byte caps at 127 ticks. Editing the
// pattern length while it plays shifts a /D track's phase (the count is pass * length + pos).
class Sequencer {
 public:
  explicit Sequencer(Project& p) : p_(p), bpm_(clampBpm(p.bpm)) {}

  void start(uint64_t now, MidiSink& out);
  void pause(uint64_t now, MidiSink& out);
  void resume(uint64_t now, MidiSink& out);
  void stop(uint64_t now, MidiSink& out);
  void togglePlay(uint64_t now, MidiSink& out);

  void queuePattern(uint8_t idx);   // switch at the end of the current pass; ignored while the song plays
  void selectPattern(uint8_t idx);  // switch from the next step
  void setBpm(uint16_t bpm);
  // Program change of one track, sent now (nothing for kNoProgram).
  void sendProgram(uint64_t now, int track, MidiSink& out);
  // Replans the unheard steps from the (edited) pattern, then ends every held TIE now
  // (not before onT + kMinGateUs), e.g. after its pattern was cleared.
  void releaseTies(uint64_t now, MidiSink& out);
  // After a song edit: Insert = a slot inserted at `at`, Delete = slot `at` removed,
  // Edit = slots, masks or songLen changed in place.
  void songEdited(uint64_t now, MidiSink& out, int at, SongOp op);
  // SONG (on, with songLen > 0) or LOOP. Stopped: start() plays slot `slot` (clamped). Playing: takes
  // effect at the end of the pass (unheard steps are replanned): on = slot `slot` follows (already in
  // the song: it goes on), off = the heard pattern keeps looping. Paused: as playing, on resume.
  void setSong(uint64_t now, MidiSink& out, bool on, int slot);
  bool song() const { return song_; }
  // The mutes from before the song (bit = track), held while the song plays; false when none.
  bool savedMutes(uint16_t& bits) const {
    bits = savedMute_;
    return muteHeld_;
  }
  // After TrackCfg::out of a track changed: ends its sounding notes on both outputs.
  void trackOutChanged(uint64_t now, int track, MidiSink& out);

  uint64_t process(uint64_t now, MidiSink& out);

  bool playing() const { return state_ == State::Playing; }
  bool paused() const { return state_ == State::Paused; }
  uint8_t pattern() const { return cur_; }
  int queued() const { return queued_; }
  uint8_t playPos() const { return heardPos_; }
  uint8_t heardPattern() const { return heardPat_; }
  // The switch the UI should show as pending: scheduled but not heard yet counts.
  int pendingPattern() const { return queued_ >= 0 ? queued_ : (heardPat_ != cur_ ? cur_ : -1); }
  uint32_t loopCount() const { return loop_; }  // planned, ahead of what is heard
  uint32_t heardLoop() const { return heardLoop_; }  // passes of the heard pattern (trackStepAt)
  // Song slot of the heard pattern; -1 outside the song (and stopped).
  int heardSongPos() const { return heardSong_; }
  void seed(uint32_t s) { rng_ = Rng(s); }
  // Tracks that may sound (bit = track), on top of mute / solo. Offline render; default all.
  void setTrackMask(uint16_t m) { mask_ = m; }
  // Steps at or after this tick (counted from start()) are not planned. Offline render; default never.
  void setEndTick(uint64_t t) { endTick_ = t; }
  // Fill held: CND FIL steps play, NFL steps do not.
  void setFill(bool on) { fill_ = on; }
  bool fill() const { return fill_; }
  void perfOn(int track, PerfFx fx);
  void perfOff(int track);
  PerfFx perf(int track) const { return track >= 0 && track < kTracks ? perf_[track] : PerfFx::None; }
  // The fx a punch-in effect adds to a step (None for Mute, which silences the track).
  static FxSlot perfSlot(PerfFx fx);
  // Position inside the heard step at now, 0 at its start .. 255 (live recording).
  uint8_t phase256(uint64_t now) const;
  uint64_t heardStepTime() const { return heardStepT_; }
  uint32_t stepDuration() const { return stepUs(); }

 private:
  enum class State : uint8_t { Stopped, Playing, Paused };
  struct Tie {
    bool on = false;
    uint8_t ch = 0, note = 0;
    uint32_t id = 0;
    uint64_t onT = 0;  // the held NoteOn's time: a release must not come before it
  };

  struct Sched {  // a scheduled step and the state just before it, to undo it
    uint64_t t;      // step time incl. swing
    uint64_t first;  // earliest event it pushed (t if none)
    uint64_t tick;
    uint32_t serial;
    uint32_t loop;
    uint8_t pos, pat;  // what played
    uint32_t posLoop;  // loop_ when pos played (loop is the one before the step)
    uint8_t prevPos, prevPat;
    int8_t prevQueued;
    int8_t song, prevSong;  // songPos_ when played / before
    Tie ties[kTracks];
    StepArp arps[kTracks];
    uint16_t perfMuted;   // perfMuted_ before: its first-muted-step release is undone with the step
    uint16_t condBits;    // condBits_ before
    uint16_t mutes;       // TrackCfg::mute bits before
    bool muteSet;         // the step's pass end started a slot or left the song (mutes written)
    bool held;            // muteHeld_ before
    uint16_t saved;       // savedMute_ before
  };
  static constexpr int kHist = 16;           // covers every scheduled but unheard step
  static constexpr uint16_t kLookTicks = 48;  // the largest nudge: -50 % of a quarter
  static constexpr uint64_t kTickNs = 625000000ull;  // one 96 PPQN tick is kTickNs / bpm ns

  static uint16_t clampBpm(uint16_t b) { return b < 20 ? 20 : (b > 300 ? 300 : b); }
  int songLen() const { return p_.songLen > kSongMax ? kSongMax : p_.songLen; }
  bool songActive() const { return song_ && songLen() > 0; }
  uint8_t songAt(int i) const { return p_.song[i] < kPatterns ? p_.song[i] : kPatterns - 1; }
  // Slot the song enters at (start, loop -> song): setSong's slot, clamped to the last.
  int songEntry() const { return songFrom_ < songLen() ? songFrom_ : songLen() - 1; }
  // Unheard steps replanned (a song advance among them decided again); their tie releases kept.
  void replan(uint64_t now, MidiSink& out);
  uint16_t ticks() const { return ticksPerStep(p_.patterns[cur_].res); }
  uint32_t stepUs() const { return 625000u * ticks() / bpm_; }
  uint32_t lookUs() const { return 625000u * kLookTicks / bpm_; }
  // Time of tick relative to the anchor, in ns*bpm (exact).
  int64_t span(uint64_t tick) const {
    return tick >= anchorTick_ ? static_cast<int64_t>((tick - anchorTick_) * kTickNs)
                               : -static_cast<int64_t>((anchorTick_ - tick) * kTickNs);
  }
  uint64_t tickTime(uint64_t tick) const;
  void reanchor(uint64_t tick);
  void anchorAt(uint64_t now);
  void applyBpm(uint64_t now);
  bool rewind(uint64_t now);
  void scheduleStep(uint64_t now);
  void skipStep(uint64_t now);
  void emitDue(uint64_t now, MidiSink& out);
  void endOfPass();
  void showStopped();
  void releaseTie(int track, uint64_t t);
  void pushOff(int track, uint64_t t);
  void releaseAllTies(uint64_t t);
  // ARS: the next arp note of the track on a step without a note (sounds only if aud); su: the track's step.
  void arpStep(int track, uint64_t t, int64_t earliest, bool aud, uint32_t su);
  void stopArps() {
    for (StepArp& a : arps_) a.n = 0;
  }
  void silence(uint64_t now, MidiSink& out);
  // Transport message (0xFA start, 0xFC pause, 0xFB resume) to the synth, on the first INT track; none
  // without INT tracks. TEMPO LFOs with Retrig OFF follow it.
  void synthTransport(uint64_t now, MidiSink& out, uint8_t msg);
  bool internal(int track) const { return track >= 0 && track < kTracks && p_.trackInternal(track); }
  bool expand(const Step& s, int track, const ExpandCtx& ctx, ExpandOut& ex);
  // A track's sub-steps per pattern step (mul) and its own step: su us, tps ticks (TrackSpeed).
  struct TrackTiming {
    int mul;
    uint32_t su;
    uint16_t tps;
  };
  TrackTiming timing(const Pattern& pt, int tr, uint32_t su) const;
  int64_t stepShift(const Pattern& pt, int pos, uint32_t su) const;
  // su, tps: the track's own step (TrackSpeed).
  ExpandCtx ctx(uint32_t su, uint16_t tps) const {
    ExpandCtx c{su, loop_, p_.scaleRoot, static_cast<ScaleType>(p_.scaleType), tps};
    c.fill = fill_;
    return c;
  }
  // One track step of tr at t (a pattern step, or a sub-step of a fast track); tps: its ticks.
  void playTrackStep(int tr, const Step& raw, uint64_t t, const ExpandCtx& c, uint16_t tps, int64_t earliest);
  // skipStep for one track step at t: the controls go out at now.
  void skipTrackStep(int tr, const Step& raw, uint64_t t, uint64_t now, const ExpandCtx& c, uint16_t tps);
  // The step as it plays: the track's punch-in fx.
  void adjustStep(Step& s, int track) const;
  // The slot's mask into every track's mute; tracks it newly mutes go silent at t.
  void applyMute(int slot, uint64_t t);
  // Mutes from bits; tracks it newly mutes go silent at t (none when t is kNever).
  void writeMutes(uint16_t bits, uint64_t t);
  // Back to the mutes saved at the song start, if held.
  void restoreMutes(uint64_t t);
  // Plays this step: not muted / solo-excluded, no perf Mute (whose first muted step ends its notes).
  bool audible(int track, uint64_t t);
  void pushStepStart(const ExpandOut& ex, uint64_t t, int64_t earliest, uint8_t track, bool note, uint16_t tps,
                     bool nudge = true);
  int pushControls(const ExpandOut& ex, uint64_t t, int64_t earliest, uint8_t track, bool nudge = true);
  bool push(uint64_t t, uint8_t s, uint8_t d1, uint8_t d2, uint32_t id, bool cont = false, uint8_t len = 3,
            uint8_t track = kNoTrack);
  void dispatch(const SchedEvent& e, MidiSink& out);
  uint32_t newId();

  Project& p_;
  EventHeap heap_;
  Voices voices_;
  VoicesN<kTracks> intVoices_;  // INT tracks: channel = track number
  Rng rng_;
  State state_ = State::Stopped;
  uint8_t cur_ = 0;
  int queued_ = -1;
  int songPos_ = -1;  // song slot of cur_, -1 outside the song
  bool song_ = false;      // setSong
  uint8_t songFrom_ = 0;   // setSong's slot
  bool fill_ = false;
  PerfFx perf_[kTracks] = {};
  uint16_t perfMuted_ = 0;  // perf Mute already ended the track's notes
  bool muteSet_ = false;    // applyMute wrote the mutes while the current step was planned
  bool muteHeld_ = false;   // savedMute_ holds the mutes from before the song
  uint16_t savedMute_ = 0;
  uint16_t condBits_ = 0;   // per track: the last CND / PRB result (CND PRE / NEI)
  uint16_t mask_ = 0xFFFF;  // setTrackMask
  uint64_t endTick_ = kNever;  // setEndTick
  uint64_t heardStepT_ = 0;  // start of the heard step
  uint8_t pos_ = 0;  // next step to schedule
  uint32_t loop_ = 0;
  uint16_t bpm_;
  uint64_t anchorUs_ = 0;   // time of anchorTick_ is anchorUs_ + anchorRem_ / (1000 * bpm_) us
  uint64_t anchorRem_ = 0;  // < 1000 * bpm_
  uint64_t anchorTick_ = 0;
  uint64_t stepTick_ = 0;   // tick of the next step to schedule
  uint64_t clockTick_ = 0;  // tick of the next MIDI clock
  uint64_t playStartT_ = 0;
  uint32_t stepSerial_ = 1;  // serial of the next step to schedule
  uint32_t selSerial_ = 0;   // stepSerial_ at the last selectPattern
  uint32_t tag_ = 0;         // tag for pushed events
  uint64_t minT_ = 0;        // earliest event of the step being scheduled
  Sched hist_[kHist] = {};   // newest first
  uint8_t histN_ = 0;
  uint8_t heardPos_ = 0;
  uint32_t heardLoop_ = 0;
  uint8_t heardPat_ = 0;
  int heardSong_ = -1;
  uint32_t nextId_ = 1;
  Tie ties_[kTracks];
  StepArp arps_[kTracks];  // running ARS per track (n = 0: none)
  ExpandOut ex_;  // scratch for scheduleStep: too big for the engine task stack
};

}  // namespace mt
