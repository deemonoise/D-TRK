#pragma once
#include <stdint.h>
#include "event_heap.h"
#include "evolve.h"
#include "follow.h"
#include "mut.h"
#include "model.h"
#include "rng.h"
#include "shift_gen.h"
#include "step_expand.h"
#include "var_gen.h"
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
// The prefetch hint (kSynthHint) of a SAMPLE step goes one step ahead of it, at most kHintLeadUs
// (the board's slot holds ~70 ms; an earlier hint only waits), never before earliest.
constexpr uint32_t kHintLeadUs = 150000;
inline uint64_t hintTime(uint64_t t, uint32_t stepUs, int64_t earliest) {
  const uint64_t lead = stepUs < kHintLeadUs ? stepUs : kHintLeadUs;
  const int64_t h = t > lead ? static_cast<int64_t>(t - lead) : 0;
  return static_cast<uint64_t>(h < earliest ? earliest : h);
}

// Hardware-free sequencer. The caller passes the current time in us and calls
// process() again no later than the time it returns.
//
// SONG or LOOP is a runtime flag (setSong), not part of the project. SONG (with songLen > 0) plays
// the slots of Project.song: start() begins at the slot given to setSong and every pattern end
// advances as the slot's follow action says (follow.h: NEXT = the next slot, after the last back to
// slot 0). The decisions hash the run seed (Project::seed mixed with setFollowSalt's salt at start())
// with a decision count that rewinds with the steps, so a replan takes the same decision again (on
// the edited song). The next slot is decided when the last step of a pass is scheduled, like a queued pattern, so rewind() undoes it together with that step.
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
// Live: setFill() is the held fill (CND FIL / NFL); it applies to steps planned from then on (at most
// the lookahead late).
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
  // cold: coldBytes() of memory for the step history and the MIDI-out note table (touched once per
  // step / note: PSRAM is fine), owned by the caller and outliving the Sequencer; nullptr = its own
  // (heap). The object itself holds the per-event state (event heap, INT note table).
  explicit Sequencer(Project& p, void* cold = nullptr);
  ~Sequencer();
  Sequencer(const Sequencer&) = delete;
  Sequencer& operator=(const Sequencer&) = delete;
  static size_t coldBytes();

  void start(uint64_t now, MidiSink& out);
  void pause(uint64_t now, MidiSink& out);
  void resume(uint64_t now, MidiSink& out);
  void stop(uint64_t now, MidiSink& out);
  void togglePlay(uint64_t now, MidiSink& out);

  void queuePattern(uint8_t idx);   // switch at the end of the current pass; ignored while the song plays
  void selectPattern(uint8_t idx);  // switch from the next step
  void setBpm(uint16_t bpm);
  // The header BPM: while a song slot's BPM holds the live tempo, the live tempo only (until the next slot
  // with its own BPM, leaving the song or Stop; the project BPM stays); else setBpm.
  void setLiveBpm(uint16_t bpm);
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
  // The project BPM while a song slot's BPM holds the live tempo (p.bpm); false when none holds (p.bpm is
  // the project's). A slot with its BPM sets the live tempo from its start ("-" keeps it); a BPM edit
  // (setBpm, a write to p.bpm) sets both; Stop and leaving the song go back to the project BPM.
  bool savedBpm(uint16_t& bpm) const {
    bpm = projBpm_;
    return bpmHeld_;
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
  // Mixed with Project::seed into the follow run seed at start(): live play passes a fresh value per
  // start, 0 (default) is the render's path (followWalk with salt 0).
  void setFollowSalt(uint32_t s) { followSalt_ = s; }
  // Tracks that may sound (bit = track), on top of mute / solo. Offline render; default all.
  void setTrackMask(uint16_t m) { mask_ = m; }
  // Steps at or after this tick (counted from start()) are not planned. Offline render; default never.
  void setEndTick(uint64_t t) { endTick_ = t; }
  // Fill held: CND FIL steps play, NFL steps do not.
  void setFill(bool on) { fill_ = on; }
  bool fill() const { return fill_; }
  // Position inside the heard step at now, 0 at its start .. 255 (live recording).
  uint8_t phase256(uint64_t now) const;
  uint64_t heardStepTime() const { return heardStepT_; }

  // Track generators (Project::gen, shift_gen.h). The register of a track shifts on each of its steps with a
  // note (PITCH) or each of its steps (GATE), muted or skipped too, before CND / PRB; start() resets every
  // register, nothing else does (pattern / song changes, pause go on). The step's note is replaced before
  // expand; everything else of the step applies. Rewound with the unheard steps.
  // After a GenCfg edit while playing: the unheard steps are planned again with it.
  void genEdited(uint64_t now, MidiSink& out) { replan(now, out); }
  // After a new seed of track tr: replanned, its register reset from the first unheard step.
  void genReseed(uint64_t now, MidiSink& out, int tr);
  // The register as the next planned step of tr finds it (Capture), and the step of tr it starts from in
  // pattern pat (0 when another pattern plays or stopped: the state is the reset one then).
  GenState genNext(int tr) const;
  int genFromStep(int tr, int pat) const;
  // The register after the heard step of tr (UI); the reset one while stopped.
  GenState heardGen(int tr) const;
  uint32_t stepDuration() const { return stepUs(); }

  // MUT (mut.h): one mutator per track, Project::var[t].mode: EVO (Project::evo, evolve.h) or a VAR mode
  // (var_gen.h). A pass = a pattern pass (endOfPass: the pattern's end or a song slot's), counted per
  // track from start() across pattern / slot changes; only the active mode counts and, every `every`
  // passes, advances its state from the one before (the follow run seed, not rng_): EVO mutates, VAR takes
  // its next generation (another pattern next: VAR back to generation 0, the pass count from 0). A track
  // step plays pattern -> MUT (mutStep) -> GEN; the pattern is never written. start() / stop() reset every
  // state; rewound with the unheard steps.
  // After a Project::evo / var edit (engine Cmd::MutEdit): the unheard steps are planned again with it; a
  // track whose stored mode (VarCfg::mode) changed restarts from no EVO changes and VAR generation 0. An
  // instrument type change (CA2D <-> CA1D in effect) needs no command: the VAR state carries its mode.
  void mutEdited(uint64_t now, MidiSink& out);
  // Replanned, then track tr's MUT state cleared: EVO changes and rotation, VAR generation 0 (n and the
  // pass counts go on). A rewritten track (Dice).
  void mutClear(uint64_t now, MidiSink& out, int tr);
  // Capture live of track tr into pattern pat (holding the project lock, any task): the unheard steps
  // are undone (no events sent: the engine plans them again from the result), then mt::captureLive from
  // the state the next step plays (MUT, the GEN register, its track step), GEN off, the MUT state
  // cleared as mutClear. Off: nothing plays on the track; OtherPattern: playing / paused and pat is not
  // the heard pattern. scratch: kMaxSteps steps of the caller's.
  LiveCapture captureLive(uint64_t now, int tr, int pat, Step* scratch);
  // The state after the heard step of tr (UI); the reset one while stopped. Valid until the next call
  // on the sequencer (hold the project lock).
  const EvoTrack& heardEvo(int tr) const;
  const VarTrack& heardVar(int tr) const;
  // Passes counted towards the next EVO mutation / VAR generation of tr (0..every-1).
  uint8_t evoPasses(int tr) const { return tr >= 0 && tr < kTracks ? evoCnt_[tr] : 0; }
  uint8_t varPasses(int tr) const { return tr >= 0 && tr < kTracks ? varCnt_[tr] : 0; }
  // Tests only: the state the next planned step of tr plays.
  EvoTrack& evoDebugState(int tr) { return evoAt(tr, evoN_[tr]); }

 private:
  enum class State : uint8_t { Stopped, Playing, Paused };
  struct Tie {
    bool on = false;
    uint8_t ch = 0, note = 0;
    uint32_t id = 0;
    uint64_t onT = 0;  // the held NoteOn's time: a release must not come before it
  };

  // A tick clock: the time of tick is us + rem / (1000 * bpm) us; the ticks after it run at bpm.
  struct Anchor {
    uint64_t us, rem, tick;
    uint16_t bpm;
  };
  // Anchors before the current one (tempo changes whose clocks or steps are still to come), oldest first.
  static constexpr int kOldAnchors = 4;

  struct Sched {  // a scheduled step and the state just before it, to undo it
    uint64_t t;      // step time incl. swing
    uint64_t first;  // earliest event it pushed (t if none)
    uint64_t tick;
    uint64_t gateTick;  // gateTick_ before
    uint32_t serial;
    uint32_t loop;
    uint8_t pos, pat;  // what played
    uint32_t posLoop;  // loop_ when pos played (loop is the one before the step)
    uint8_t prevPos, prevPat;
    int8_t prevQueued;
    int8_t song, prevSong;  // songPos_ when played / before
    uint32_t prevSongN;     // songN_ before
    Tie ties[kTracks];
    StepArp arps[kTracks];
    GenState gens[kTracks];
    uint32_t evoN[kTracks];   // evoN_ before
    uint8_t evoCnt[kTracks];  // evoCnt_ before
    uint32_t varN[kTracks];   // varN_ before
    uint8_t varCnt[kTracks];  // varCnt_ before
    uint16_t condBits;    // condBits_ before
    uint16_t mutes;       // TrackCfg::mute bits before
    bool muteSet;         // the step's pass end started a slot or left the song (mutes written)
    bool held;            // muteHeld_ before
    uint16_t saved;       // savedMute_ before
    Anchor anchor;                  // the tick clock before (a tempo change among the undone steps is undone)
    Anchor old[kOldAnchors];        // old_ before
    uint8_t oldN;
    uint16_t tempo;        // the step's tempo (p.bpm once it is heard)
    bool bpmHeld;          // bpmHeld_ before
  };
  static constexpr int kHist = 16;           // covers every scheduled but unheard step
  static_assert((kHist & (kHist - 1)) == 0, "hist ring");
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
  // Time of tick relative to tick base, in ns*bpm (exact).
  static int64_t span(uint64_t tick, uint64_t base) {
    return tick >= base ? static_cast<int64_t>((tick - base) * kTickNs) : -static_cast<int64_t>((base - tick) * kTickNs);
  }
  // The clock of tick: the current anchor, before it the older anchor it follows (the oldest for earlier
  // ticks). idx: its old_ index, -1 = the current one.
  Anchor anchorOf(uint64_t tick, int& idx) const;
  uint64_t tickTime(uint64_t tick) const;
  // Moves the anchor to tick, keeping every tick's time.
  void reanchor(uint64_t tick);
  void pushOld();       // the current anchor into old_ (full: the oldest drops)
  void pruneAnchors();  // drops old anchors only ticks before the next clock and step use
  // A tempo change at tick (reanchored there): the ticks before it keep their time and tempo.
  void tempoAt(uint64_t tick, uint16_t bpm);
  void anchorAt(uint64_t now);
  void applyBpm(uint64_t now);
  uint16_t slotBpm(int slot) const { return slot >= 0 && slot < songLen() ? songBpmClamp(p_.songBpm[slot]) : 0; }
  bool rewind(uint64_t now);
  void scheduleStep(uint64_t now);
  void skipStep(uint64_t now);
  void emitDue(uint64_t now, MidiSink& out);
  void endOfPass();
  void showStopped();
  void releaseTie(int track, uint64_t t);
  void pushOff(int track, uint64_t t);
  void releaseAllTies(uint64_t t);
  // Gen: shifts tr's register for this track step, returns the step to play (s, or tmp with the generated note).
  const Step& genStep(int tr, const Step& s, Step& tmp);
  void genResetAll();
  // Evolve: state n of tr in the ring; the state a track step plays (the pattern's step, or tmp).
  EvoTrack& evoAt(int tr, uint32_t n) { return cold_->evo[tr][n % kEvoRing]; }
  const EvoTrack& evoAt(int tr, uint32_t n) const { return cold_->evo[tr][n % kEvoRing]; }
  void evoResetAll();
  // The step a track step plays before GEN: the pattern's through the active MUT mode (pt's step or tmp).
  const Step& evoPick(const Pattern& pt, int tr, int idx, Step& tmp) const;
  // Track tr's MUT state cleared (no replan): EVO changes, VAR generation 0, older ring states dropped.
  void mutReset(int tr);
  // Unheard steps undone without sending (captureLive: any task); their tie releases kept.
  void replanQuiet(uint64_t now);
  void evoPass();  // endOfPass: counts the pass, mutates the tracks due
  // VAR: generation n of tr in the ring (as Evolve's).
  VarTrack& varAt(int tr, uint32_t n) { return cold_->var[tr][n % kEvoRing]; }
  const VarTrack& varAt(int tr, uint32_t n) const { return cold_->var[tr][n % kEvoRing]; }
  void varResetAll();
  // endOfPass, before evoPass: counts the pass, the tracks due take a generation; newPat: another pattern
  // plays next, every VAR track back to generation 0.
  void varPass(bool newPat);
  // ARS: the next arp note of the track on a step without a note (sounds only if aud); su: the track's step.
  void arpStep(int track, uint64_t t, int64_t earliest, bool aud, uint32_t su);
  void stopArps() {
    for (StepArp& a : arps_) a.n = 0;
  }
  void silence(uint64_t now, MidiSink& out);
  // Transport message (0xFA start, 0xFC pause, 0xFB resume, 0xFD stop) to the synth, on the first INT track; none
  // without INT tracks. TEMPO LFOs with Retrig OFF follow it.
  void synthTransport(uint64_t now, MidiSink& out, uint8_t msg);
  bool internal(int track) const { return track >= 0 && track < kTracks && p_.trackInternal(track); }
  bool expand(const Step& s, int track, const ExpandCtx& ctx, ExpandOut& ex);
  // TRN of the planned step's song slot for track (0 outside the song and with its Song trn bit off).
  int songTrn(int track) const {
    if (songPos_ < 0 || songPos_ >= songLen() || !((p_.songTrnMask >> track) & 1)) return 0;
    return songTrnClamp(p_.songTrn[songPos_]);
  }
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
  void playTrackStep(int tr, const Step& s, uint64_t t, const ExpandCtx& c, uint16_t tps, int64_t earliest);
  // skipStep for one track step at t: the controls go out at now.
  void skipTrackStep(int tr, const Step& s, uint64_t t, uint64_t now, const ExpandCtx& c, uint16_t tps);
  // The slot's mask into every track's mute; tracks it newly mutes go silent at t.
  void applyMute(int slot, uint64_t t);
  // Mutes from bits; tracks it newly mutes go silent at t (none when t is kNever).
  void writeMutes(uint16_t bits, uint64_t t);
  // Back to the mutes saved at the song start, if held.
  void restoreMutes(uint64_t t);
  // Plays this step: not muted / solo-excluded, in the track mask.
  bool audible(int track) const;
  void pushPerfStep(int64_t earliest);
  void pushHint(const Step& s, const ExpandOut& ex, int first, uint64_t t, int64_t earliest, uint8_t track,
                uint32_t stepUs);
  void pushStepStart(const ExpandOut& ex, uint64_t t, int64_t earliest, uint8_t track, bool note, uint16_t tps,
                     bool nudge = true);
  int pushControls(const ExpandOut& ex, uint64_t t, int64_t earliest, uint8_t track, bool nudge = true);
  bool push(uint64_t t, uint8_t s, uint8_t d1, uint8_t d2, uint32_t id, bool cont = false, uint8_t len = 3,
            uint8_t track = kNoTrack);
  void dispatch(const SchedEvent& e, MidiSink& out);
  uint32_t newId();

  // Rarely touched, kept out of the object (see the constructor).
  struct Cold {
    Sched hist[kHist];  // a ring: hist(0) newest
    Voices voices;      // MIDI-out tracks' sounding notes
    EvoTrack evo[kTracks][kEvoRing];  // state n of track t at [t][n % kEvoRing] (rewind needs the older ones)
    VarTrack var[kTracks][kEvoRing];  // VAR generation n of track t, as evo (n = kEvoNone: none)
  };
  Sched& hist(int i) { return cold_->hist[(histHead_ + i) & (kHist - 1)]; }
  const Sched& hist(int i) const { return cold_->hist[(histHead_ + i) & (kHist - 1)]; }

  Project& p_;
  Cold* cold_;
  bool ownCold_;
  EventHeap heap_;
  VoicesN<kTracks> intVoices_;  // INT tracks: channel = track number
  Rng rng_;
  State state_ = State::Stopped;
  uint8_t cur_ = 0;
  int queued_ = -1;
  int songPos_ = -1;  // song slot of cur_, -1 outside the song
  bool song_ = false;      // setSong
  uint8_t songFrom_ = 0;   // setSong's slot
  uint32_t followSalt_ = 0;  // setFollowSalt
  uint32_t runSeed_ = 0;     // followRunSeed at start()
  uint32_t songN_ = 0;       // follow decisions since start(): the n of followNext
  bool fill_ = false;
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
  Anchor old_[kOldAnchors];  // see kOldAnchors
  uint8_t oldN_ = 0;
  uint16_t projBpm_;    // the project BPM (savedBpm)
  uint16_t bpmSeen_;    // p.bpm as the sequencer wrote or took it: another value is a BPM edit
  uint32_t heardSerial_ = 0;  // serial of the heard step: a newly heard one publishes its tempo
  bool bpmHeld_ = false;  // a slot's BPM holds the live tempo (savedBpm)
  bool liveBpm_ = false;  // the pending p.bpm write is setLiveBpm's: the project BPM stays
  uint64_t stepTick_ = 0;   // tick of the next step to schedule
  // Ticks since start() to the next step (not pass-relative, not reset by resume): the song clock
  // of the trance gates (Synth 0xF6 5), rewound with the steps.
  uint64_t gateTick_ = 0;
  uint64_t clockTick_ = 0;  // tick of the next MIDI clock
  uint64_t playStartT_ = 0;
  uint32_t stepSerial_ = 1;  // serial of the next step to schedule
  uint32_t selSerial_ = 0;   // stepSerial_ at the last selectPattern
  uint32_t tag_ = 0;         // tag for pushed events
  uint64_t minT_ = 0;        // earliest event of the step being scheduled
  uint8_t histHead_ = 0;  // hist(i) = cold_->hist[(histHead_ + i) % kHist], newest first
  uint8_t histN_ = 0;
  uint8_t heardPos_ = 0;
  uint32_t heardLoop_ = 0;
  uint8_t heardPat_ = 0;
  int heardSong_ = -1;
  uint32_t nextId_ = 1;
  Tie ties_[kTracks];
  StepArp arps_[kTracks];  // running ARS per track (n = 0: none)
  GenState gen_[kTracks];  // generator registers, as the next step to plan finds them
  uint32_t evoN_[kTracks] = {};   // each track's state number, as the next step to plan finds it
  uint8_t evoCnt_[kTracks] = {};  // passes since the last mutation
  EvoView evoView_;               // scratch for evoPass
  uint32_t varN_[kTracks] = {};   // VAR: each track's generation, as the next step to plan finds it
  uint8_t varCnt_[kTracks] = {};  // passes since the last generation
  uint8_t mutMode_[kTracks] = {};  // the stored MUT mode (mutKey) each track's state is for: mutEdited
  uint8_t mutKey(int tr) const;
  ExpandOut ex_;  // scratch for scheduleStep: too big for the engine task stack
};

}  // namespace mt
