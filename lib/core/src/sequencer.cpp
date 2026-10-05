#include "sequencer.h"
#include <string.h>

namespace mt {

namespace {
int64_t floorDiv(int64_t a, int64_t b) {
  int64_t q = a / b;
  if (a % b != 0 && a < 0) --q;
  return q;
}
}  // namespace

uint32_t Sequencer::newId() {
  const uint32_t id = nextId_++;
  if (nextId_ == 0) nextId_ = 1;
  return id;
}

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
  heardPat_ = cur_;
  heardSong_ = songPos_;
}

void Sequencer::start(uint64_t now, MidiSink& out) {
  if (state_ == State::Playing) silence(now, out);
  heap_.clear();
  if (songActive()) {
    songPos_ = 0;
    cur_ = chainAt(0);
  } else {
    songPos_ = -1;
    if (queued_ >= 0) cur_ = queued_;
  }
  queued_ = -1;
  pos_ = 0;
  loop_ = 0;
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
  const uint8_t s = 0xFA;
  out.send(&s, 1);
  anchorTick_ = stepTick_ = clockTick_ = 0;
  anchorAt(now);
  state_ = State::Playing;
}

void Sequencer::pause(uint64_t now, MidiSink& out) {
  if (state_ != State::Playing) return;
  emitDue(now, out);  // what was due before the command was heard
  rewind(now);  // steps scheduled ahead but not yet heard are replayed on resume
  silence(now, out);
  const uint8_t s = 0xFC;
  out.send(&s, 1);
  heardPos_ = pos_;
  heardPat_ = cur_;
  heardSong_ = songPos_;
  state_ = State::Paused;
}

void Sequencer::resume(uint64_t now, MidiSink& out) {
  if (state_ != State::Paused) return;
  // Song Position Pointer counts sixteenths (24 ticks at 96 PPQN). It is the position
  // within the current pattern, not since start(): a slave sees every pass (and every
  // chain entry) restart at 0, which is what a pattern-based slave expects.
  const uint16_t spp = static_cast<uint16_t>(static_cast<uint32_t>(pos_) * ticks() / 24);
  const uint8_t m[3] = {0xF2, static_cast<uint8_t>(spp & 0x7F), static_cast<uint8_t>((spp >> 7) & 0x7F)};
  out.send(m, 3);
  const uint8_t c = 0xFB;
  out.send(&c, 1);
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
  state_ = State::Stopped;
  pos_ = 0;
  heardPos_ = 0;
  loop_ = 0;
  showStopped();
}

// Stopped: start() plays chain[0] in song mode, so that is what is shown.
void Sequencer::showStopped() {
  songPos_ = -1;
  if (songActive()) {
    cur_ = chainAt(0);
    heardSong_ = 0;
  } else {
    heardSong_ = -1;
  }
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
  if (idx >= kPatterns || songActive()) return;
  if (state_ == State::Playing) {
    queued_ = idx;
  } else {
    cur_ = idx;
    queued_ = -1;
    pos_ = 0;
    heardPos_ = 0;
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

void Sequencer::chainEdited(uint64_t now, MidiSink& out, int at, ChainOp op) {
  if (state_ == State::Stopped) {
    showStopped();
    return;
  }
  if (state_ == State::Playing) {
    emitDue(now, out);
    // Unheard steps are replanned: an advance among them is decided again on the new chain.
    if (rewind(now)) {
      tag_ = stepSerial_ - 1;
      releaseAllTies(tickTime(stepTick_));
    }
  }
  if (op == ChainOp::Edit) return;
  auto remap = [&](int v) {
    if (v < 0) return v;
    if (op == ChainOp::Insert) return v >= at ? v + 1 : v;
    return v > at ? v - 1 : (v == at ? at - 1 : v);  // a deleted entry goes on with the next one
  };
  songPos_ = remap(songPos_);
  heardSong_ = remap(heardSong_);
  for (int i = 0; i < histN_; ++i) {
    hist_[i].song = static_cast<int8_t>(remap(hist_[i].song));
    hist_[i].prevSong = static_cast<int8_t>(remap(hist_[i].prevSong));
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
    voices_.releaseChannel(ch, [&](uint8_t note) {
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
  while (k + 1 < histN_ && hist_[k + 1].first > now) ++k;
  if (k < 0) return false;
  const Sched h = hist_[k];
  heap_.removeIf([&](const SchedEvent& e) { return static_cast<int32_t>(e.tag - h.serial) >= 0; });
  memcpy(ties_, h.ties, sizeof(ties_));
  songPos_ = h.prevSong;  // a chain advance among the undone steps is decided again
  stepTick_ = h.tick;
  stepSerial_ = h.serial;
  for (int i = k + 1; i < histN_; ++i) hist_[i - k - 1] = hist_[i];
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

  // Plan far enough ahead for the largest negative nudge at any resolution.
  // After a stall, steps already followed by another due step are skipped
  // rather than played together.
  const uint32_t look = lookUs();
  while (tickTime(stepTick_) <= now + look) {
    if (tickTime(stepTick_ + ticks()) <= now) {
      skipStep(now);
    } else {
      scheduleStep(now);
    }
  }

  emitDue(now, out);

  for (int i = 0; i < histN_; ++i) {
    if (hist_[i].t <= now) {
      heardPos_ = hist_[i].pos;
      heardPat_ = hist_[i].pat;
      heardSong_ = hist_[i].song;
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

void Sequencer::scheduleStep(uint64_t now) {
  for (int i = kHist - 1; i > 0; --i) hist_[i] = hist_[i - 1];
  if (histN_ < kHist) ++histN_;
  Sched& h = hist_[0];
  h.tick = stepTick_;
  h.serial = stepSerial_;
  h.loop = loop_;
  h.prevPos = pos_;
  h.prevPat = cur_;
  h.prevQueued = static_cast<int8_t>(queued_);
  h.prevSong = static_cast<int8_t>(songPos_);
  memcpy(h.ties, ties_, sizeof(ties_));
  tag_ = stepSerial_;
  minT_ = kNever;

  if (pos_ >= p_.patterns[cur_].length) endOfPass();  // length shrank mid-pass
  Pattern& pat = p_.patterns[cur_];
  const uint32_t su = stepUs();
  uint64_t t = tickTime(stepTick_);
  if (pos_ & 1) {
    const uint8_t sw = pat.swing < 50 ? 50 : (pat.swing > 75 ? 75 : pat.swing);
    t += static_cast<uint64_t>(su) * (sw - 50) / 50;
  }
  // Nothing may land in the past; a shifted note keeps its gate.
  const int64_t earliest = static_cast<int64_t>(now > playStartT_ ? now : playStartT_);

  const ExpandCtx ctx{su, loop_, p_.scaleRoot, static_cast<ScaleType>(p_.scaleType)};
  ExpandOut& ex = ex_;
  for (int tr = 0; tr < kTracks; ++tr) {
    const Step& s = pat.steps[tr][pos_];
    const bool hasFx = s.fx[0].cmd != Fx::None || s.fx[1].cmd != Fx::None;
    if (!s.hasNote()) {
      // OFF ends a tie; controls on OFF or on a step without a note still go out.
      if (s.note == kNoteOff) releaseTie(tr, t);
      if (hasFx && p_.trackAudible(tr) && expand(s, tr, ctx, ex)) {
        pushStepStart(ex, t, earliest, static_cast<uint8_t>(tr), false);
        pushControls(ex, t, earliest, static_cast<uint8_t>(tr));
      }
      continue;
    }
    if (!p_.trackAudible(tr) || !expand(s, tr, ctx, ex)) {
      releaseTie(tr, t + kTieOverlapUs);
      continue;
    }
    pushStepStart(ex, t, earliest, static_cast<uint8_t>(tr), true);
    int i = pushControls(ex, t, earliest, static_cast<uint8_t>(tr));  // index of the first NoteOn
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
    const StepEvent& first = ex.ev[i];
    if (tie.on && tie.ch == first.ch && tie.note == first.note) {
      // Same pitch: the held note goes on; this step's gate (or next tie) ends it.
      // If another track took the voice meanwhile, the continuation sounds it again.
      const uint64_t on = at(first);
      push(on > tie.onT ? on : tie.onT, 0x90 | tie.ch, tie.note, first.vel, tie.id, true, 3, tr);
      if (i + 1 == ex.count) {  // tied again: the next release must follow the continuation
        if (on > tie.onT) tie.onT = on;
        continue;
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
  }

  h.t = t;
  h.pos = pos_;
  h.pat = cur_;
  h.song = static_cast<int8_t>(songPos_);
  ++stepSerial_;
  stepTick_ += ticks();
  if (++pos_ >= pat.length) endOfPass();
  h.first = minT_ == kNever ? t : minT_;
}

// INT tracks: tells the synth a step starts (see Synth: 0xF5 0xF0), with the controls' time.
// Sent before every step with a note (it ends the previous step's fx) and before synth fx.
void Sequencer::pushStepStart(const ExpandOut& ex, uint64_t t, int64_t earliest, uint8_t track, bool note,
                              bool nudge) {
  if (!internal(track) || ex.count == 0) return;
  bool fx = false;
  for (int i = 0; i < ex.count && !fx; ++i) fx = ex.ev[i].kind == EvKind::SynthFx;
  if (!note && !fx) return;
  int64_t et = static_cast<int64_t>(t) + (nudge ? ex.ev[0].offsetUs : 0);
  if (et < earliest) et = earliest;
  const uint8_t tps = static_cast<uint8_t>(ticks() > 127 ? 127 : ticks());
  push(static_cast<uint64_t>(et), 0xF5, kSynthStep, static_cast<uint8_t>(tps | (note ? 0x80 : 0)), 0, false, 3,
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
  const ExpandCtx ctx{stepUs(), loop_, p_.scaleRoot, static_cast<ScaleType>(p_.scaleType)};
  for (int tr = 0; tr < kTracks; ++tr) {
    const Step& s = pat.steps[tr][pos_];
    if (s.hasNote() || s.note == kNoteOff) releaseTie(tr, t);
    const bool hasFx = s.fx[0].cmd != Fx::None || s.fx[1].cmd != Fx::None;
    if (hasFx && p_.trackAudible(tr) && expand(s, tr, ctx, ex_)) {
      pushStepStart(ex_, now, static_cast<int64_t>(now), static_cast<uint8_t>(tr), false, false);
      pushControls(ex_, now, static_cast<int64_t>(now), static_cast<uint8_t>(tr), false);  // all at now, no nudge
    }
  }
  stepTick_ += ticks();
  if (++pos_ >= pat.length) endOfPass();
}

void Sequencer::endOfPass() {
  pos_ = 0;
  ++loop_;
  int next = queued_;
  if (songActive()) {
    songPos_ = songPos_ + 1 < chainLen() ? songPos_ + 1 : 0;
    next = chainAt(songPos_);
    if (next == cur_) next = -1;  // a repeated entry goes on like a loop (ties, CND)
  } else {
    songPos_ = -1;
  }
  queued_ = -1;
  if (next >= 0) {
    releaseAllTies(tickTime(stepTick_));
    cur_ = static_cast<uint8_t>(next);
    loop_ = 0;
  }
  reanchor(stepTick_);  // keeps tick distances small
}

void Sequencer::releaseTie(int track, uint64_t t) {
  Tie& tie = ties_[track];
  if (!tie.on) return;
  const uint64_t minT = tie.onT + kMinGateUs;
  push(t > minT ? t : minT, 0x80 | tie.ch, tie.note, 0, tie.id, false, 3, static_cast<uint8_t>(track));
  tie.on = false;
}

void Sequencer::releaseAllTies(uint64_t t) {
  for (int tr = 0; tr < kTracks; ++tr) releaseTie(tr, t);
}

void Sequencer::silence(uint64_t now, MidiSink& out) {
  heap_.clear();
  voices_.releaseAll([&](uint8_t ch, uint8_t note) {
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
}

// expandStep with the track's routing: on an INT track every event's channel is the track
// number (the key of intVoices_ and of ties), CHN does not apply.
bool Sequencer::expand(const Step& s, int track, const ExpandCtx& ctx, ExpandOut& ex) {
  if (!expandStep(s, p_.tracks[track], ctx, rng_, ex)) return false;
  if (internal(track))
    for (int i = 0; i < ex.count; ++i) ex.ev[i].ch = static_cast<uint8_t>(track);
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
  if (e.b[0] == 0xF5 && !toSynth) return;  // synth fx never reach MIDI
  if (kind == 0xB0 && toSynth) return;     // CC is MIDI only
  // INT voices are keyed by track (== the channel of INT events; an event planned before the
  // track became INT still has its MIDI channel).
  const uint8_t key = toSynth ? e.track : ch;
  auto noteOn = [&](uint8_t n, uint32_t id) {
    return toSynth ? intVoices_.noteOn(key, n, id) : voices_.noteOn(key, n, id);
  };
  auto noteOff = [&](uint8_t n, uint32_t id) {
    return toSynth ? intVoices_.noteOff(key, n, id) : voices_.noteOff(key, n, id);
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
    if (e.track < kTracks) activity_ |= static_cast<uint8_t>(1u << e.track);
  } else if (kind == 0x80) {
    if (!noteOff(e.b[1], e.id)) return;
  }
  send(e.b, e.len);
}

}  // namespace mt
