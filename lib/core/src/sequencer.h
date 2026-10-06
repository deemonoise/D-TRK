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

// What changed in Project.chain / songMode (see Sequencer::chainEdited).
enum class ChainOp : uint8_t { Edit, Insert, Delete };
constexpr uint32_t kTieOverlapUs = 1000;  // tied note ends this long after the next note starts

// Hardware-free sequencer. The caller passes the current time in us and calls
// process() again no later than the time it returns.
//
// Song mode (Project.songMode with chainLen > 0) plays the chain: start() begins at
// chain[0] and every pattern end advances to the next entry, after the last back to
// chain[0]. The next entry is decided when the last step of a pass is scheduled, like
// a queued pattern, so rewind() undoes it together with that step. queuePattern() is
// ignored while the song is active; selectPattern() still switches at once and the
// chain goes on from its position. Enabling the song while playing starts chain[0] at
// the next pattern end, disabling it keeps the current pattern looping. With an empty
// chain the current pattern plays as without song mode. Chain edits take effect at
// the next pattern end: entries are clamped to valid patterns, a position beyond a
// shrunk chain wraps to chain[0]. After every chain / songMode write the caller calls
// chainEdited() (under the same lock): it re-decides unheard chain advances and keeps
// the position on the same entry when rows are inserted or deleted before it. A
// decision whose last step has been heard stays. Stopped in song mode, the sequencer
// shows chain[0] (pattern() == chain[0], heardSongPos() == 0).
//
// A chain item plays chainRep passes before the next one (a repeat goes on like a loop: ties and
// CND continue), transposes the notes of melodic tracks by chainTr while it plays (drum tracks, OFF
// and empty steps untouched) and, when it starts, recalls its mute scene into TrackCfg::mute (an
// empty scene is skipped). Pattern::trackLen makes a track loop on its own length inside the
// pass; the pass counter follows the pattern length.
//
// Live: setFill() is the held fill (CND FIL / NFL), perfOn() a punch-in effect on a track until
// perfOff() (or stop()). Both apply to steps planned from then on (at most the lookahead late).
class Sequencer {
 public:
  explicit Sequencer(Project& p) : p_(p), bpm_(clampBpm(p.bpm)) {}

  void start(uint64_t now, MidiSink& out);
  void pause(uint64_t now, MidiSink& out);
  void resume(uint64_t now, MidiSink& out);
  void stop(uint64_t now, MidiSink& out);
  void togglePlay(uint64_t now, MidiSink& out);

  void queuePattern(uint8_t idx);   // switch at the end of the current pass; ignored in song mode
  void selectPattern(uint8_t idx);  // switch from the next step
  void setBpm(uint16_t bpm);
  // Program change of one track, sent now (nothing for kNoProgram).
  void sendProgram(uint64_t now, int track, MidiSink& out);
  // Replans the unheard steps from the (edited) pattern, then ends every held TIE now
  // (not before onT + kMinGateUs), e.g. after its pattern was cleared.
  void releaseTies(uint64_t now, MidiSink& out);
  // After a chain edit: Insert = a row inserted at `at`, Delete = row `at` removed,
  // Edit = entries or songMode changed in place.
  void chainEdited(uint64_t now, MidiSink& out, int at, ChainOp op);
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
  uint32_t loopCount() const { return loop_; }
  // Chain index of the heard pattern; -1 outside song mode.
  int heardSongPos() const { return heardSong_; }
  void seed(uint32_t s) { rng_ = Rng(s); }
  // Tracks that may sound (bit = track), on top of mute / solo. Offline render; default all.
  void setTrackMask(uint16_t m) { mask_ = m; }
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
  // Tracks whose NoteOns went out since the last call (bit = track).
  uint16_t takeActivity() {
    const uint16_t a = activity_;
    activity_ = 0;
    return a;
  }

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
    uint8_t prevPos, prevPat;
    int8_t prevQueued;
    int8_t song, prevSong;  // songPos_ when played / before
    uint8_t prevRep;        // rep_ before
    Tie ties[kTracks];
  };
  static constexpr int kHist = 16;           // covers every scheduled but unheard step
  static constexpr uint16_t kLookTicks = 48;  // the largest nudge: -50 % of a quarter
  static constexpr uint64_t kTickNs = 625000000ull;  // one 96 PPQN tick is kTickNs / bpm ns

  static uint16_t clampBpm(uint16_t b) { return b < 20 ? 20 : (b > 300 ? 300 : b); }
  int chainLen() const { return p_.chainLen > kChainMax ? kChainMax : p_.chainLen; }
  bool songActive() const { return p_.songMode && chainLen() > 0; }
  uint8_t chainAt(int i) const { return p_.chain[i] < kPatterns ? p_.chain[i] : kPatterns - 1; }
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
  void silence(uint64_t now, MidiSink& out);
  bool internal(int track) const { return track >= 0 && track < kTracks && p_.trackInternal(track); }
  bool expand(const Step& s, int track, const ExpandCtx& ctx, ExpandOut& ex);
  ExpandCtx ctx(uint32_t su) const {
    ExpandCtx c{su, loop_, p_.scaleRoot, static_cast<ScaleType>(p_.scaleType), ticks()};
    c.fill = fill_;
    return c;
  }
  // The step of `track` at pass position pos: the track loops on its own length (polymeter).
  static int stepIndex(const Pattern& pt, int track, int pos) {
    const uint8_t n = pt.trackLen[track];
    return n && n < pt.length ? pos % n : pos;
  }
  // The step as it plays: the chain transpose on a melodic track, the track's punch-in fx.
  void adjustStep(Step& s, int track) const;
  int chainTranspose() const { return songPos_ >= 0 && songPos_ < chainLen() ? p_.chainTr[songPos_] : 0; }
  int chainRep(int i) const {
    const uint8_t r = p_.chainRep[i];
    return r < 1 ? 1 : (r > kChainRepMax ? kChainRepMax : r);
  }
  void applyScene(int songPos, uint64_t t);
  // Plays this step: not muted / solo-excluded, no perf Mute (whose first muted step ends its notes).
  bool audible(int track, uint64_t t);
  void pushStepStart(const ExpandOut& ex, uint64_t t, int64_t earliest, uint8_t track, bool note,
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
  int songPos_ = -1;  // chain index of cur_, -1 outside song mode
  uint8_t rep_ = 0;   // passes of the chain item done (song mode)
  bool fill_ = false;
  PerfFx perf_[kTracks] = {};
  uint16_t perfMuted_ = 0;  // perf Mute already ended the track's notes
  uint16_t mask_ = 0xFFFF;  // setTrackMask
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
  uint8_t heardPat_ = 0;
  int heardSong_ = -1;
  uint32_t nextId_ = 1;
  uint16_t activity_ = 0;
  Tie ties_[kTracks];
  ExpandOut ex_;  // scratch for scheduleStep: too big for the engine task stack
};

}  // namespace mt
