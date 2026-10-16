#include "sequencer.h"
#include <string.h>
#include <new>
#include "fx_info.h"
#include "groove.h"
#include "record.h"

namespace mt {

namespace {
int64_t floorDiv(int64_t a, int64_t b) {
  int64_t q = a / b;
  if (a % b != 0 && a < 0) --q;
  return q;
}
// t, but not before earliest.
uint64_t atLeast(int64_t t, int64_t earliest) { return static_cast<uint64_t>(t < earliest ? earliest : t); }
}  // namespace

uint32_t Sequencer::newId() {
  const uint32_t id = nextId_++;
  if (nextId_ == 0) nextId_ = 1;
  return id;
}

Sequencer::Sequencer(Project& p, void* cold)
    : p_(p), cold_(static_cast<Cold*>(cold)), ownCold_(cold == nullptr), bpm_(clampBpm(p.bpm)) {
  if (ownCold_) cold_ = new Cold();
  else new (cold_) Cold();  // zeroed
}

Sequencer::~Sequencer() {
  if (ownCold_) delete cold_;
}

size_t Sequencer::coldBytes() { return sizeof(Cold); }

uint64_t Sequencer::tickTime(uint64_t tick) const {
  const int64_t x = static_cast<int64_t>(anchorRem_) + span(tick);
  return static_cast<uint64_t>(static_cast<int64_t>(anchorUs_) + floorDiv(x, 1000 * bpm_));
}

void Sequencer::reanchor(uint64_t tick) {
  const int64_t u = 1000 * bpm_;
  const int64_t x = static_cast<int64_t>(anchorRem_) + span(tick);
  const int64_t q = floorDiv(x, u);
  anchorUs_ = static_cast<uint64_t>(static_cast<int64_t>(anchorUs_) + q);
  anchorRem_ = static_cast<uint64_t>(x - q * u);
  anchorTick_ = tick;
}

void Sequencer::anchorAt(uint64_t now) {
  bpm_ = clampBpm(p_.bpm);
  p_.bpm = bpm_;
  anchorUs_ = now;
  anchorRem_ = 0;
  playStartT_ = now;
  histN_ = 0;
  heardPos_ = pos_;
  heardLoop_ = loop_;
  heardPat_ = cur_;
  heardSong_ = songPos_;
  heardStepT_ = now;
}

void Sequencer::start(uint64_t now, MidiSink& out) {
  if (state_ == State::Playing) silence(now, out);
  heap_.clear();
  if (songActive()) {
    songPos_ = songEntry();
    cur_ = songAt(songPos_);
  } else {
    songPos_ = -1;
    if (queued_ >= 0) cur_ = queued_;
  }
  queued_ = -1;
  pos_ = 0;
  loop_ = 0;
  condBits_ = 0;
  restoreMutes(kNever);  // restart: saved again from the user's mutes
  if (songPos_ >= 0) applyMute(songPos_, now);
  for (int i = 0; i < kTracks; ++i) {
    const TrackCfg& t = p_.tracks[i];
    if (internal(i)) {
      const uint8_t m = 0xFE;  // synth: reset the track's runtime state
      out.synth(now, static_cast<uint8_t>(i), &m, 1);
    } else if (t.program != kNoProgram) {
      const uint8_t m[2] = {static_cast<uint8_t>(0xC0 | (t.channel & 15)), static_cast<uint8_t>(t.program & 127)};
      out.send(m, 2);
    }
  }
  synthTransport(now, out, 0xFA);
  const uint8_t s = 0xFA;
  out.send(&s, 1);
  anchorTick_ = stepTick_ = clockTick_ = 0;
  gateTick_ = 0;
  genResetAll();
  anchorAt(now);
  state_ = State::Playing;
}

void Sequencer::genResetAll() {
  for (int i = 0; i < kTracks; ++i) genReset(gen_[i], p_.gen[i], i);
}

void Sequencer::genReseed(uint64_t now, MidiSink& out, int tr) {
  if (tr < 0 || tr >= kTracks) return;
  replan(now, out);
  if (state_ != State::Stopped) genReset(gen_[tr], p_.gen[tr], tr);
}

GenState Sequencer::genNext(int tr) const {
  GenState g;
  if (tr < 0 || tr >= kTracks) return g;
  if (state_ != State::Stopped) return gen_[tr];
  genReset(g, p_.gen[tr], tr);
  return g;
}

int Sequencer::genFromStep(int tr, int pat) const {
  if (state_ == State::Stopped || pat != cur_ || tr < 0 || tr >= kTracks) return 0;
  const Pattern& pt = p_.patterns[cur_];
  int pos = pos_;
  uint32_t loop = loop_;
  for (int k = 0; k < 4 * kMaxSteps; ++k) {  // a slow track resting: its next step
    if (pos >= pt.length) {
      pos = 0;
      ++loop;
    }
    const int idx = trackStepAt(pt, tr, pos, loop, 0);
    if (idx >= 0) return idx;
    ++pos;
  }
  return 0;
}

GenState Sequencer::heardGen(int tr) const {
  if (tr < 0 || tr >= kTracks || state_ == State::Stopped) return genNext(tr);
  for (int i = 0; i < histN_; ++i)
    if (hist(i).t <= heardStepT_) return i > 0 ? hist(i - 1).gens[tr] : gen_[tr];
  return gen_[tr];
}

const Step& Sequencer::genStep(int tr, const Step& s, Step& tmp) {
  if (!p_.genOn(tr)) return s;
  const GenCfg& g = p_.gen[tr];
  const bool gate = g.mode == static_cast<uint8_t>(GenMode::Gate);
  if (!gate && !s.hasNote()) return s;  // PITCH: clocked by the steps with a note
  const FxSlot* f = s.find(Fx::FLP);
  genShift(gen_[tr], g, tr, f ? (f->val > 100 ? 100 : f->val) : g.flip);
  if (gate && s.note == kNoteOff) return s;
  tmp = s;
  if (!gate || genGate(gen_[tr], g))
    tmp.note = genNote(gen_[tr], g, p_.scaleRoot, static_cast<ScaleType>(p_.scaleType));
  else
    tmp.note = kNoteEmpty;
  return tmp;
}

void Sequencer::synthTransport(uint64_t now, MidiSink& out, uint8_t msg) {
  for (int i = 0; i < kTracks; ++i)
    if (internal(i)) {
      out.synth(now, static_cast<uint8_t>(i), &msg, 1);
      return;
    }
}

void Sequencer::pause(uint64_t now, MidiSink& out) {
  if (state_ != State::Playing) return;
  emitDue(now, out);  // what was due before the command was heard
  rewind(now);  // steps scheduled ahead but not yet heard are replayed on resume
  silence(now, out);
  const uint8_t s = 0xFC;
  out.send(&s, 1);
  synthTransport(now, out, 0xFC);
  heardPos_ = pos_;
  heardLoop_ = loop_;
  heardPat_ = cur_;
  heardSong_ = songPos_;
  state_ = State::Paused;
}

void Sequencer::resume(uint64_t now, MidiSink& out) {
  if (state_ != State::Paused) return;
  // Song Position Pointer counts sixteenths (24 ticks at 96 PPQN). It is the position
  // within the current pattern, not since start(): a slave sees every pass (and every
  // song slot) restart at 0, which is what a pattern-based slave expects.
  const uint16_t spp = static_cast<uint16_t>(static_cast<uint32_t>(pos_) * ticks() / 24);
  const uint8_t m[3] = {0xF2, static_cast<uint8_t>(spp & 0x7F), static_cast<uint8_t>((spp >> 7) & 0x7F)};
  out.send(m, 3);
  const uint8_t c = 0xFB;
  out.send(&c, 1);
  synthTransport(now, out, 0xFB);
  // The step sits at its exact tick after the clock origin, so a slave following
  // SPP + clock stays aligned even when the position is not on a sixteenth.
  anchorTick_ = clockTick_ = static_cast<uint64_t>(spp) * 24;
  stepTick_ = static_cast<uint64_t>(pos_) * ticks();
  anchorAt(now);
  state_ = State::Playing;
}

void Sequencer::stop(uint64_t now, MidiSink& out) {
  if (state_ == State::Playing) silence(now, out);
  const uint8_t s = 0xFC;
  out.send(&s, 1);
  synthTransport(now, out, 0xFD);  // the trance gates bypass while stopped
  state_ = State::Stopped;
  pos_ = 0;
  heardPos_ = 0;
  loop_ = 0;
  heardLoop_ = 0;
  restoreMutes(kNever);
  showStopped();
}

// Stopped: no slot is shown, the pattern stays.
void Sequencer::showStopped() {
  songPos_ = -1;
  heardSong_ = -1;
  heardPat_ = cur_;
}

void Sequencer::togglePlay(uint64_t now, MidiSink& out) {
  switch (state_) {
    case State::Stopped: start(now, out); break;
    case State::Playing: pause(now, out); break;
    case State::Paused: resume(now, out); break;
  }
}

void Sequencer::queuePattern(uint8_t idx) {
  if (idx >= kPatterns || (state_ != State::Stopped && songActive())) return;
  if (state_ == State::Playing) {
    queued_ = idx;
  } else {
    cur_ = idx;
    queued_ = -1;
    pos_ = 0;
    loop_ = 0;  // a new pattern counts its passes from 0 (CND, slow tracks)
    heardPos_ = 0;
    heardLoop_ = 0;
    heardPat_ = cur_;
  }
}

void Sequencer::selectPattern(uint8_t idx) {
  if (idx >= kPatterns) return;
  if (state_ != State::Playing) {
    queuePattern(idx);
    return;
  }
  selSerial_ = stepSerial_;
  tag_ = stepSerial_ - 1;  // belongs to the steps already scheduled
  releaseAllTies(tickTime(stepTick_));
  cur_ = idx;
  queued_ = -1;
  loop_ = 0;
  const uint8_t len = p_.patterns[idx].length;
  if (len) pos_ %= len;
}

void Sequencer::setBpm(uint16_t bpm) {
  p_.bpm = clampBpm(bpm);
  if (state_ != State::Playing) bpm_ = p_.bpm;  // while playing, process() applies it
}

void Sequencer::sendProgram(uint64_t now, int track, MidiSink& out) {
  if (track < 0 || track >= kTracks) return;
  const TrackCfg& t = p_.tracks[track];
  if (internal(track)) {
    const uint8_t m[2] = {0xC0, static_cast<uint8_t>(t.instr < kInstruments ? t.instr : 0)};
    out.synth(now, static_cast<uint8_t>(track), m, 2);
    return;
  }
  if (t.program == kNoProgram) return;
  const uint8_t m[2] = {static_cast<uint8_t>(0xC0 | (t.channel & 15)), static_cast<uint8_t>(t.program & 127)};
  out.send(m, 2);
}

void Sequencer::releaseTies(uint64_t now, MidiSink& out) {
  if (state_ != State::Playing) return;  // pause / stop already silenced them
  emitDue(now, out);
  // Unheard steps are replanned from the edited pattern, so ties_ holds only what was sent.
  rewind(now);
  tag_ = stepSerial_ - 1;  // belongs to the heard steps: a later rewind keeps the releases
  releaseAllTies(now);
  emitDue(now, out);
}

void Sequencer::replan(uint64_t now, MidiSink& out) {
  if (state_ != State::Playing) return;  // paused: already rewound
  emitDue(now, out);
  if (rewind(now)) {
    tag_ = stepSerial_ - 1;
    releaseAllTies(tickTime(stepTick_));
  }
}

void Sequencer::setSong(uint64_t now, MidiSink& out, bool on, int slot) {
  replan(now, out);  // an advance among the unheard steps is decided again
  song_ = on;
  songFrom_ = static_cast<uint8_t>(slot < 0 ? 0 : (slot >= kSongMax ? kSongMax - 1 : slot));
}

void Sequencer::songEdited(uint64_t now, MidiSink& out, int at, SongOp op) {
  if (state_ == State::Stopped) {
    showStopped();
    return;
  }
  replan(now, out);  // an advance among the unheard steps is decided again on the new song
  if (op == SongOp::Edit) return;
  auto remap = [&](int v) {
    if (v < 0) return v;
    if (op == SongOp::Insert) return v >= at ? v + 1 : v;
    if (v != at) return v > at ? v - 1 : v;
    // A deleted slot goes on with the next one: slot 0 via the last, so the pass end wraps to the new slot 0.
    return at > 0 ? at - 1 : songLen() - 1;
  };
  songPos_ = remap(songPos_);
  heardSong_ = remap(heardSong_);
  for (int i = 0; i < histN_; ++i) {
    hist(i).song = static_cast<int8_t>(remap(hist(i).song));
    hist(i).prevSong = static_cast<int8_t>(remap(hist(i).prevSong));
  }
}

// Notes still sounding on the old output would never get their NoteOff (dispatch routes by
// the current output), so they end now. A MIDI channel may be shared with other tracks:
// their notes on it end too.
void Sequencer::trackOutChanged(uint64_t now, int track, MidiSink& out) {
  if (track < 0 || track >= kTracks) return;
  const uint8_t tr = static_cast<uint8_t>(track);
  if (internal(track)) {
    const uint8_t ch = p_.tracks[track].channel & 15;
    cold_->voices.releaseChannel(ch, [&](uint8_t note) {
      const uint8_t m[3] = {static_cast<uint8_t>(0x80 | ch), note, 0};
      out.send(m, 3);
    });
  } else {
    intVoices_.releaseChannel(tr, [&](uint8_t note) {
      const uint8_t m[3] = {static_cast<uint8_t>(0x80 | tr), note, 0};
      out.synth(now, tr, m, 3);
    });
    const uint8_t m = 0xFF;
    out.synth(now, tr, &m, 1);
  }
  ties_[track].on = false;
  arps_[track].n = 0;
}

void Sequencer::applyBpm(uint64_t now) {
  const uint16_t bpm = clampBpm(p_.bpm);
  p_.bpm = bpm;
  if (bpm == bpm_) return;
  // Unheard steps are rescheduled at the new tempo; clocks and steps continue
  // on one grid from the next clock.
  const bool selected = rewind(now);
  reanchor(clockTick_);
  anchorRem_ = anchorRem_ * bpm / bpm_;
  bpm_ = bpm;
  if (selected) {
    tag_ = stepSerial_ - 1;
    releaseAllTies(tickTime(stepTick_));
  }
}

// Undoes every scheduled step none of whose events has gone out yet. Returns
// true if a selectPattern came after the first undone step: it stays in force
// and its tie releases are the caller's job.
bool Sequencer::rewind(uint64_t now) {
  int k = -1;
  while (k + 1 < histN_ && hist(k + 1).first > now) ++k;
  if (k < 0) return false;
  const Sched h = hist(k);
  heap_.removeIf([&](const SchedEvent& e) { return static_cast<int32_t>(e.tag - h.serial) >= 0; });
  memcpy(ties_, h.ties, sizeof(ties_));
  memcpy(arps_, h.arps, sizeof(arps_));
  memcpy(gen_, h.gens, sizeof(gen_));
  condBits_ = h.condBits;
  muteHeld_ = h.held;
  savedMute_ = h.saved;
  for (int i = 0; i <= k; ++i)
    if (hist(i).muteSet) {
      for (int tr = 0; tr < kTracks; ++tr) p_.tracks[tr].mute = (h.mutes >> tr) & 1;
      break;
    }
  songPos_ = h.prevSong;  // a song advance among the undone steps is decided again
  stepTick_ = h.tick;
  gateTick_ = h.gateTick;
  stepSerial_ = h.serial;
  histHead_ = static_cast<uint8_t>((histHead_ + k + 1) & (kHist - 1));  // drop the k + 1 newest
  histN_ -= k + 1;
  if (static_cast<int32_t>(selSerial_ - h.serial) > 0) {
    loop_ = 0;
    const uint8_t len = p_.patterns[cur_].length;
    pos_ = len ? h.prevPos % len : 0;
    selSerial_ = h.serial;
    return true;
  }
  cur_ = h.prevPat;
  loop_ = h.loop;
  pos_ = h.prevPos;
  if (queued_ < 0) queued_ = h.prevQueued;  // a newer queued pattern wins
  return false;
}

// Missed clocks still go out (in a burst) so a slave keeps its song position;
// notes missed by more than the lookahead are dropped, their NoteOffs are no-ops.
void Sequencer::emitDue(uint64_t now, MidiSink& out) {
  while (tickTime(clockTick_) <= now) {
    const uint8_t c = 0xF8;
    out.send(&c, 1);
    clockTick_ += kPpqn / 24;
  }
  const uint32_t stale = lookUs();
  while (!heap_.empty() && heap_.top().t <= now) {
    const SchedEvent e = heap_.top();
    heap_.pop();
    if ((e.b[0] & 0xF0) == 0x90 && now - e.t > stale) continue;
    dispatch(e, out);
  }
}

uint64_t Sequencer::process(uint64_t now, MidiSink& out) {
  if (state_ != State::Playing) return kNever;
  if (p_.bpm != bpm_) applyBpm(now);

  // What is due goes out before planning: a step takes up to ~2 ms to plan on the device, and the
  // synth renders an event's block ~2 ms after the event's time (audio.cpp, kLagUs).
  emitDue(now, out);

  // Plan far enough ahead for the largest negative nudge at any resolution.
  // After a stall, steps already followed by another due step are skipped
  // rather than played together.
  const uint32_t look = lookUs();
  while (stepTick_ < endTick_ && tickTime(stepTick_) <= now + look) {
    if (tickTime(stepTick_ + ticks()) <= now) {
      skipStep(now);
    } else {
      scheduleStep(now);
    }
  }

  emitDue(now, out);

  for (int i = 0; i < histN_; ++i) {
    if (hist(i).t <= now) {
      heardPos_ = hist(i).pos;
      heardLoop_ = hist(i).posLoop;
      heardPat_ = hist(i).pat;
      heardSong_ = hist(i).song;
      heardStepT_ = hist(i).t;
      break;
    }
  }

  uint64_t next = tickTime(clockTick_);
  const uint64_t grid = tickTime(stepTick_);
  const uint64_t planAt = grid > look ? grid - look : 0;
  if (planAt < next) next = planAt;
  if (!heap_.empty() && heap_.top().t < next) next = heap_.top().t;
  return next;
}

// Swing (odd steps) or the groove template's shift of pattern step pos, in us.
int64_t Sequencer::stepShift(const Pattern& pt, int pos, uint32_t su) const {
  if (pt.groove) return static_cast<int64_t>(su) * grooveAt(pt.groove).shift[pos % 16] / 100;
  if (!(pos & 1)) return 0;
  const uint8_t sw = pt.swing < 50 ? 50 : (pt.swing > 75 ? 75 : pt.swing);
  return static_cast<int64_t>(static_cast<uint64_t>(su) * (sw - 50) / 50);
}

Sequencer::TrackTiming Sequencer::timing(const Pattern& pt, int tr, uint32_t su) const {
  const TrackSpeed sp = toSpeed(pt.trackSpeed[tr]);
  const int mul = speedMul(sp), div = speedDiv(sp);
  return {mul, su / mul * div, static_cast<uint16_t>(ticks() / mul * div)};
}

void Sequencer::scheduleStep(uint64_t now) {
  histHead_ = static_cast<uint8_t>((histHead_ - 1) & (kHist - 1));  // the oldest drops off a full ring
  if (histN_ < kHist) ++histN_;
  Sched& h = hist(0);
  h.tick = stepTick_;
  h.gateTick = gateTick_;
  h.serial = stepSerial_;
  h.loop = loop_;
  h.prevPos = pos_;
  h.prevPat = cur_;
  h.prevQueued = static_cast<int8_t>(queued_);
  h.prevSong = static_cast<int8_t>(songPos_);
  memcpy(h.ties, ties_, sizeof(ties_));
  memcpy(h.arps, arps_, sizeof(arps_));
  memcpy(h.gens, gen_, sizeof(gen_));
  h.condBits = condBits_;
  h.held = muteHeld_;
  h.saved = savedMute_;
  h.mutes = 0;
  for (int i = 0; i < kTracks; ++i)
    if (p_.tracks[i].mute) h.mutes |= static_cast<uint16_t>(1u << i);
  muteSet_ = false;
  tag_ = stepSerial_;
  minT_ = kNever;

  if (pos_ >= p_.patterns[cur_].length) endOfPass();  // length shrank mid-pass
  Pattern& pat = p_.patterns[cur_];
  const uint32_t su = stepUs();
  const uint64_t t = static_cast<uint64_t>(static_cast<int64_t>(tickTime(stepTick_)) + stepShift(pat, pos_, su));
  // The groove template replaces the swing: shift and accent per step of 16.
  const uint8_t velPct = pat.groove ? grooveAt(pat.groove).vel[pos_ % 16] : 100;
  // Sub-steps of a fast track share the slot up to the next (shifted) step evenly.
  const int nextPos = pos_ + 1 < pat.length ? pos_ + 1 : 0;
  const int64_t nextT = static_cast<int64_t>(tickTime(stepTick_ + ticks())) + stepShift(pat, nextPos, su);
  const uint64_t slot = nextT > static_cast<int64_t>(t) ? static_cast<uint64_t>(nextT) - t : 0;
  // Nothing may land in the past; a shifted note keeps its gate.
  const int64_t earliest = static_cast<int64_t>(now > playStartT_ ? now : playStartT_);
  pushPerfStep(earliest);

  for (int tr = 0; tr < kTracks; ++tr) {
    const TrackTiming tm = timing(pat, tr, su);
    ExpandCtx tc = ctx(tm.su, tm.tps);
    tc.velPct = velPct;
    for (int k = 0; k < tm.mul; ++k) {
      const int idx = trackStepAt(pat, tr, pos_, loop_, k);
      if (idx < 0) break;  // a slow track rests on this pattern step
      const uint64_t tk = t + slot * k / tm.mul;
      if (tm.mul > 1 && t + slot * (k + 1) / tm.mul <= now) {
        skipTrackStep(tr, pat.steps[tr][idx], tk, now, tc, tm.tps);  // after a stall: as a missed step
      } else {
        playTrackStep(tr, pat.steps[tr][idx], tk, tc, tm.tps, earliest);
      }
    }
  }

  h.t = t;
  h.pos = pos_;
  h.posLoop = loop_;
  h.pat = cur_;
  h.song = static_cast<int8_t>(songPos_);
  ++stepSerial_;
  stepTick_ += ticks();
  gateTick_ += ticks();
  if (++pos_ >= pat.length) endOfPass();
  h.first = minT_ == kNever ? t : minT_;
  h.muteSet = muteSet_;
}

void Sequencer::playTrackStep(int tr, const Step& ps, uint64_t t, const ExpandCtx& c, uint16_t tps,
                              int64_t earliest) {
  Step gs;
  const Step& s = genStep(tr, ps, gs);
  ExpandOut& ex = ex_;
  const bool aud = audible(tr);
  const bool hasFx = s.hasFx();
  if (!s.hasNote()) {
    // OFF ends a tie (and every INT voice); controls on OFF or on a step without a note still go out.
    // An ARS note due here counts as the step's note for the synth (its fx lock it).
    StepArp& a = arps_[tr];
    bool stop = s.note == kNoteOff;
    if (stop) pushOff(tr, t);
    const bool arpDue = a.n && !stop && a.wait + 1 >= a.div;
    if (hasFx && aud && expand(s, tr, c, ex)) {
      pushStepStart(ex, t, earliest, static_cast<uint8_t>(tr), arpDue && ex.offUs < 0, tps);
      pushControls(ex, t, earliest, static_cast<uint8_t>(tr));
      if (ex.offUs >= 0) {
        pushOff(tr, atLeast(static_cast<int64_t>(t) + ex.offUs, earliest));
        stop = true;
      }
    }
    if (stop) a.n = 0;
    else arpStep(tr, t, earliest, aud, c.stepUs);
    return;
  }
  if (!aud) arps_[tr].n = 0;
  if (!aud || !expand(s, tr, c, ex)) {  // a step failing CND / PRB leaves a running arp alone
    releaseTie(tr, t + kTieOverlapUs);
    return;
  }
  arps_[tr] = ex.arp;
  if (ex.offUs >= 0) arps_[tr].n = 0;
  pushStepStart(ex, t, earliest, static_cast<uint8_t>(tr), true, tps);
  int i = pushControls(ex, t, earliest, static_cast<uint8_t>(tr));  // index of the first NoteOn
  pushHint(s, ex, i, t, earliest, static_cast<uint8_t>(tr), c.stepUs);
  uint64_t shift = 0;
  auto at = [&](const StepEvent& e) {
    const int64_t et = static_cast<int64_t>(t) + e.offsetUs;
    if (e.kind == EvKind::NoteOn) shift = et < earliest ? static_cast<uint64_t>(earliest - et) : 0;
    return static_cast<uint64_t>(et) + shift;
  };
  Tie& tie = ties_[tr];
  uint32_t id = 0;
  uint64_t onT = 0;
  bool keep = false;  // the latest NoteOn made it into the heap
  const StepEvent& first = ex.ev[i];  // stale when the step has no NoteOn (empty KIT mask)
  if (tie.on && i < ex.count && tie.ch == first.ch && tie.note == first.note) {
    // Same pitch: the held note goes on; this step's gate (or next tie) ends it.
    // If another track took the voice meanwhile, the continuation sounds it again.
    const uint64_t on = at(first);
    push(on > tie.onT ? on : tie.onT, 0x90 | tie.ch, tie.note, first.vel, tie.id, true, 3, tr);
    if (i + 1 == ex.count) {  // tied again: the next release must follow the continuation
      if (on > tie.onT) tie.onT = on;
      return;
    }
    const uint64_t off = at(ex.ev[i + 1]);
    const uint64_t minOff = tie.onT + kMinGateUs;
    push(off > minOff ? off : minOff, 0x80 | tie.ch, tie.note, 0, tie.id, false, 3, tr);
    keep = true;
    tie.on = false;
    i += 2;
  } else {
    releaseTie(tr, t + kTieOverlapUs);
  }
  for (; i < ex.count; ++i) {
    const StepEvent& e = ex.ev[i];
    const uint64_t et = at(e);
    if (e.kind == EvKind::NoteOn) {
      id = newId();
      onT = et;
      keep = push(et, 0x90 | e.ch, e.note, e.vel, id, false, 3, tr);
    } else if (keep) {
      push(et, 0x80 | e.ch, e.note, 0, id, false, 3, tr);
    }
  }
  if (ex.tie && keep) {
    const StepEvent& last = ex.ev[ex.count - 1];
    tie = {true, last.ch, last.note, id, onT};
  }
  if (ex.offUs >= 0) pushOff(tr, atLeast(static_cast<int64_t>(t) + ex.offUs, earliest) + shift);
}

// PERF step clock (Synth 0xF6 4): on a 1/16 boundary of the pass (pos_ x ticks: every pass starts
// on one, whatever its length), its index mod 64 at the unshifted time (the grid ignores swing),
// to the first INT track. Song clock (0xF6 5): the same on a 1/16 boundary since start()
// (gateTick_: continuous across passes and song slots). A boundary already past is skipped: a late
// anchor would misplace the grid. Not counted in the step's first event (rewind keys on the
// heard ones; a swung step's clock goes out before its notes).
void Sequencer::pushPerfStep(int64_t earliest) {
  constexpr uint32_t k16 = kPpqn / 4;
  const uint32_t pt = static_cast<uint32_t>(pos_) * ticks();
  const bool perf = pt % k16 == 0, song = gateTick_ % k16 == 0;
  if (!perf && !song) return;
  const uint64_t t = tickTime(stepTick_);
  if (static_cast<int64_t>(t) < earliest) return;
  for (int i = 0; i < kTracks; ++i)
    if (internal(i)) {
      const uint64_t keep = minT_;
      const uint8_t tr = static_cast<uint8_t>(i);
      if (perf) push(t, 0xF6, 4, static_cast<uint8_t>((pt / k16) & 63), 0, false, 3, tr);
      if (song) push(t, 0xF6, 5, static_cast<uint8_t>((gateTick_ / k16) & 63), 0, false, 3, tr);
      minT_ = keep;
      return;
    }
}

// INT track with a SAMPLE instrument (its configured one: a PGM on the step is not followed): tells
// the synth where the note will start, one step ahead (see kSynthHint). Ahead of the step's events,
// so not counted in its first event (as pushPerfStep).
void Sequencer::pushHint(const Step& s, const ExpandOut& ex, int first, uint64_t t, int64_t earliest,
                         uint8_t track, uint32_t stepUs) {
  if (!internal(track) || first >= ex.count || ex.ev[first].kind != EvKind::NoteOn) return;
  const uint8_t ii = p_.tracks[track].instr;
  if (ii >= kInstruments || p_.instruments[ii].type != InstrType::Sample) return;
  const FxSlot* o = s.find(Fx::OFS);
  const uint64_t keep = minT_;
  push(hintTime(t, stepUs, earliest), kSynthHint, ex.ev[first].note, o ? o->val : 0, 0, false, 3, track);
  minT_ = keep;
}

// INT tracks: tells the synth a step starts (see Synth: 0xF5 0xF0), with the controls' time.
// Sent before every step with a note (it ends the previous step's fx) and before synth fx.
void Sequencer::pushStepStart(const ExpandOut& ex, uint64_t t, int64_t earliest, uint8_t track, bool note,
                              uint16_t tps, bool nudge) {
  if (!internal(track) || ex.count == 0) return;
  bool fx = false;
  for (int i = 0; i < ex.count && !fx; ++i) fx = ex.ev[i].kind == EvKind::SynthFx;
  if (!note && !fx) return;
  int64_t et = static_cast<int64_t>(t) + (nudge ? ex.ev[0].offsetUs : 0);
  if (et < earliest) et = earliest;
  const uint8_t tp = static_cast<uint8_t>(tps > 127 ? 127 : tps);
  push(static_cast<uint64_t>(et), 0xF5, kSynthStep, static_cast<uint8_t>(tp | (note ? 0x80 : 0)), 0, false, 3,
       track);
}

// Pushes the leading control events of an expanded step (never in the past) and
// returns the index of the first note event.
int Sequencer::pushControls(const ExpandOut& ex, uint64_t t, int64_t earliest, uint8_t track, bool nudge) {
  int i = 0;
  for (; i < ex.count; ++i) {
    const StepEvent& e = ex.ev[i];
    uint8_t status, len = 3;
    switch (e.kind) {
      case EvKind::Cc: status = 0xB0; break;
      case EvKind::PitchBend: status = 0xE0; break;
      case EvKind::Program: status = 0xC0; len = 2; break;
      case EvKind::SynthFx: status = 0xF5; break;  // no channel nibble
      default: return i;
    }
    int64_t et = static_cast<int64_t>(t) + (nudge ? e.offsetUs : 0);
    if (et < earliest) et = earliest;
    if (e.kind != EvKind::SynthFx) status |= e.ch;
    push(static_cast<uint64_t>(et), status, e.note, len == 3 ? e.vel : 0, 0, false, len, track);
  }
  return i;
}

// A missed step's notes are dropped, but its controls (CND and PRB still apply)
// go out now: a later step relies on the program, CC or bend they set.
void Sequencer::skipStep(uint64_t now) {
  tag_ = stepSerial_ - 1;  // not undoable: the step is in the past
  if (pos_ >= p_.patterns[cur_].length) endOfPass();
  const Pattern& pat = p_.patterns[cur_];
  const uint64_t t = tickTime(stepTick_);
  const uint64_t slot = tickTime(stepTick_ + ticks()) - t;
  const uint32_t su = stepUs();
  for (int tr = 0; tr < kTracks; ++tr) {
    const TrackTiming tm = timing(pat, tr, su);
    const ExpandCtx c = ctx(tm.su, tm.tps);
    for (int k = 0; k < tm.mul; ++k) {
      const int idx = trackStepAt(pat, tr, pos_, loop_, k);
      if (idx < 0) break;  // a slow track rests on this pattern step
      skipTrackStep(tr, pat.steps[tr][idx], t + slot * k / tm.mul, now, c, tm.tps);
    }
  }
  stepTick_ += ticks();
  gateTick_ += ticks();
  if (++pos_ >= pat.length) endOfPass();
}

void Sequencer::skipTrackStep(int tr, const Step& ps, uint64_t t, uint64_t now, const ExpandCtx& c, uint16_t tps) {
  Step gs;
  const Step& s = genStep(tr, ps, gs);
  if (s.note == kNoteOff) pushOff(tr, t);
  else if (s.hasNote()) releaseTie(tr, t);
  const bool aud = audible(tr);
  // The arp follows the step as scheduleStep would have played it (its notes are dropped).
  StepArp& a = arps_[tr];
  bool stopArp = s.note == kNoteOff || (s.hasNote() && !aud);
  if ((s.hasFx() || s.hasNote()) && aud && expand(s, tr, c, ex_)) {
    pushStepStart(ex_, now, static_cast<int64_t>(now), static_cast<uint8_t>(tr), false, tps, false);
    pushControls(ex_, now, static_cast<int64_t>(now), static_cast<uint8_t>(tr), false);  // all at now, no nudge
    if (s.hasNote()) a = ex_.arp;
    if (ex_.offUs >= 0) {
      pushOff(tr, now);
      stopArp = true;
    }
  } else if (s.hasNote() && aud) {
    return;  // CND / PRB failed: a running arp goes on
  }
  if (stopArp) a.n = 0;
  else if (!s.hasNote()) arpStep(tr, t, static_cast<int64_t>(now), false, c.stepUs);  // counted, not played
}

void Sequencer::endOfPass() {
  pos_ = 0;
  ++loop_;
  int next = queued_;
  if (songActive()) {
    songPos_ = songPos_ < 0 ? songEntry() : (songPos_ + 1 < songLen() ? songPos_ + 1 : 0);
    next = songAt(songPos_);
    applyMute(songPos_, tickTime(stepTick_));
    if (next == cur_) next = -1;  // the same pattern again goes on like a loop (ties, CND)
  } else {
    songPos_ = -1;
    restoreMutes(tickTime(stepTick_));
  }
  queued_ = -1;
  if (next >= 0) {
    releaseAllTies(tickTime(stepTick_));
    cur_ = static_cast<uint8_t>(next);
    loop_ = 0;
  }
  reanchor(stepTick_);  // keeps tick distances small
}

bool Sequencer::audible(int track) const {
  return p_.trackAudible(track) && (mask_ & static_cast<uint16_t>(1u << track));
}

void Sequencer::applyMute(int slot, uint64_t t) {
  if (slot < 0 || slot >= songLen()) return;
  if (!muteHeld_) {
    savedMute_ = 0;
    for (int tr = 0; tr < kTracks; ++tr)
      if (p_.tracks[tr].mute) savedMute_ |= static_cast<uint16_t>(1u << tr);
    muteHeld_ = true;
  }
  writeMutes(p_.songMute[slot], t);
}

void Sequencer::restoreMutes(uint64_t t) {
  if (!muteHeld_) return;
  muteHeld_ = false;
  writeMutes(savedMute_, t);
}

void Sequencer::writeMutes(uint16_t bits, uint64_t t) {
  for (int tr = 0; tr < kTracks; ++tr) {
    const bool mute = (bits >> tr) & 1;
    if (mute && !p_.tracks[tr].mute && t != kNever) pushOff(tr, t);
    p_.tracks[tr].mute = mute;
  }
  muteSet_ = true;
}

uint8_t Sequencer::phase256(uint64_t now) const { return stepPhase256(now, heardStepT_, stepUs()); }

void Sequencer::releaseTie(int track, uint64_t t) {
  Tie& tie = ties_[track];
  if (!tie.on) return;
  const uint64_t minT = tie.onT + kMinGateUs;
  push(t > minT ? t : minT, 0x80 | tie.ch, tie.note, 0, tie.id, false, 3, static_cast<uint8_t>(track));
  tie.on = false;
}

// OFF: ends the track's tie and, on an INT track, releases all its voices (samples ignore note-offs).
void Sequencer::pushOff(int track, uint64_t t) {
  releaseTie(track, t);
  if (internal(track)) push(t, 0xFF, 0, 0, 0, false, 1, static_cast<uint8_t>(track));
}

// Pattern change or cleared pattern: ties end and arps stop.
void Sequencer::releaseAllTies(uint64_t t) {
  for (int tr = 0; tr < kTracks; ++tr) releaseTie(tr, t);
  stopArps();
}

void Sequencer::arpStep(int track, uint64_t t, int64_t earliest, bool aud, uint32_t su) {
  StepArp& a = arps_[track];
  if (!a.n || ++a.wait < a.div) return;
  a.wait = 0;
  const int all = a.count();
  const int cycle = a.mode == 2 && all > 1 ? 2 * all - 2 : all;  // UPDOWN: up and back
  a.k = static_cast<uint8_t>((a.k + 1) % cycle);  // never wraps mid-cycle
  const int idx = a.mode == 3 ? static_cast<int>(rng_.below(all)) : arpIndex(a.mode, a.k, all);
  if (!aud) return;
  // Gate: a share of the arp's step span, ending before the next arp note.
  const uint32_t span = su * a.div;
  uint32_t gate = static_cast<uint32_t>(static_cast<uint64_t>(span) * a.gate / 100);
  if (gate + kMinGateUs > span) gate = span > 2 * kMinGateUs ? span - kMinGateUs : kMinGateUs;
  if (gate < kMinGateUs) gate = kMinGateUs;
  const uint64_t on = atLeast(static_cast<int64_t>(t), earliest);
  const uint8_t note = a.note(idx);
  const uint32_t id = newId();
  if (push(on, 0x90 | a.ch, note, a.vel, id, false, 3, static_cast<uint8_t>(track)))
    push(on + gate, 0x80 | a.ch, note, 0, id, false, 3, static_cast<uint8_t>(track));
}

void Sequencer::silence(uint64_t now, MidiSink& out) {
  heap_.clear();
  cold_->voices.releaseAll([&](uint8_t ch, uint8_t note) {
    const uint8_t m[3] = {static_cast<uint8_t>(0x80 | ch), note, 0};
    out.send(m, 3);
  });
  intVoices_.releaseAll([&](uint8_t ch, uint8_t note) {
    const uint8_t m[3] = {static_cast<uint8_t>(0x80 | ch), note, 0};
    out.synth(now, ch, m, 3);
  });
  for (int t = 0; t < kTracks; ++t) {
    if (!internal(t)) continue;
    const uint8_t m = 0xFF;
    out.synth(now, static_cast<uint8_t>(t), &m, 1);
  }
  for (auto& t : ties_) t.on = false;
  stopArps();
}

// expandStep with the track's instrument (a KIT makes it a drum track) and routing: on an INT
// track every event's channel is the track number (the key of intVoices_ and of ties), CHN does
// not apply.
bool Sequencer::expand(const Step& s, int track, const ExpandCtx& ctx, ExpandOut& ex) {
  ExpandCtx c = ctx;
  c.kit = p_.kitOf(track);
  c.pre = (condBits_ >> track) & 1;
  c.nei = track > 0 && ((condBits_ >> (track - 1)) & 1);
  const bool played = expandStep(s, p_.tracks[track], c, rng_, ex);
  if (ex.cond >= 0) {  // the track's last condition result, for PRE / NEI
    const uint16_t bit = static_cast<uint16_t>(1u << track);
    condBits_ = ex.cond ? static_cast<uint16_t>(condBits_ | bit) : static_cast<uint16_t>(condBits_ & ~bit);
  }
  if (!played) return false;
  if (internal(track)) {
    for (int i = 0; i < ex.count; ++i) ex.ev[i].ch = static_cast<uint8_t>(track);
    ex.arp.ch = static_cast<uint8_t>(track);
  }
  return true;
}

bool Sequencer::push(uint64_t t, uint8_t s, uint8_t d1, uint8_t d2, uint32_t id, bool cont, uint8_t len,
                     uint8_t track) {
  SchedEvent e{};
  e.t = t;
  e.id = id;
  e.tag = tag_;
  e.b[0] = s;
  e.b[1] = d1;
  e.b[2] = d2;
  e.len = len;
  e.cont = cont;
  e.track = track;
  if (t < minT_) minT_ = t;
  // A full heap drops NoteOns and controls; NoteOffs have a reserve.
  return heap_.push(e);
}

// Routed by the track's current output: INT tracks go to out.synth() with their own
// voice table, so an INT and a MIDI note on the same channel and pitch both sound.
void Sequencer::dispatch(const SchedEvent& e, MidiSink& out) {
  const uint8_t kind = e.b[0] & 0xF0;
  const uint8_t ch = e.b[0] & 0x0F;
  const bool toSynth = internal(e.track);
  // synth fx / release / PERF step clock never reach MIDI
  if ((e.b[0] == 0xF5 || e.b[0] == 0xF6 || e.b[0] == 0xFF || e.b[0] == kSynthHint) && !toSynth) return;
  if (kind == 0xB0 && toSynth) return;     // CC is MIDI only
  // INT voices are keyed by track (== the channel of INT events; an event planned before the
  // track became INT still has its MIDI channel).
  const uint8_t key = toSynth ? e.track : ch;
  auto noteOn = [&](uint8_t n, uint32_t id) {
    return toSynth ? intVoices_.noteOn(key, n, id) : cold_->voices.noteOn(key, n, id);
  };
  auto noteOff = [&](uint8_t n, uint32_t id) {
    return toSynth ? intVoices_.noteOff(key, n, id) : cold_->voices.noteOff(key, n, id);
  };
  auto send = [&](const uint8_t* b, uint8_t len) {
    if (toSynth) {
      out.synth(e.t, e.track, b, len);  // the synth plays it at its time
    } else {
      out.send(b, len);
    }
  };
  if (kind == 0x90) {
    // A continuation stays silent while the held note still owns its voice.
    if (e.cont && noteOff(e.b[1], e.id)) {
      noteOn(e.b[1], e.id);
      return;
    }
    if (noteOn(e.b[1], e.id)) {
      const uint8_t off[3] = {static_cast<uint8_t>(0x80 | ch), e.b[1], 0};
      send(off, 3);
    }
  } else if (kind == 0x80) {
    if (!noteOff(e.b[1], e.id)) return;
  }
  send(e.b, e.len);
}

}  // namespace mt
