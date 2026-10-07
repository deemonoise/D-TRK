#include "synth.h"
#include "hot.h"
#include "slices.h"
#include "synth_drum_machines.h"
#include "synth_fm_machines.h"
#include "scale.h"
#include <math.h>
#include <string.h>

namespace mt {
namespace {

// +12 dB: chip waves sit at full scale, a peak-normalised sample averages ~12-18 dB lower.
constexpr float kSampleGain = 4.f;

constexpr float kNoiseClock = 93.f;    // LFSR steps per note period
constexpr float kNoiseMaxSteps = 8.f;  // per output sample (high notes)

constexpr uint16_t kDrumReleaseMs = 3;  // one-shot gate env: fade on all-off instead of a click
constexpr uint16_t kChokeMs = 3;        // a SAMPLE note fading the track's older samples

// FM params cache tolerance (Synth::controlFm).
constexpr float kFmCacheMacro = 0.5f;   // macro units (0..127)
constexpr float kFmCacheCents = 0.01f;  // semitones

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool isOff(const uint8_t* b, uint8_t len) {
  const uint8_t k = b[0] & 0xF0;
  return k == 0x80 || (k == 0x90 && len >= 3 && b[2] == 0) || b[0] == 0xFF || b[0] == 0xFE;
}

// Soft clip: 1.5x - 0.5x^3 inside [-1, 1], flat outside.
float softClip(float x) {
  if (x >= 1.f) return 1.f;
  if (x <= -1.f) return -1.f;
  return x * (1.5f - 0.5f * x * x);
}

}  // namespace

int eventOffset(uint64_t t, uint64_t blockT) {
  if (t <= blockT) return 0;
  const uint64_t off = ((t - blockT) * kSynthRate + 500000) / 1000000;
  return off < static_cast<uint64_t>(Synth::kBlock) ? static_cast<int>(off) : -1;
}

Synth::Synth(const Project& p) : p_(p) {
  Drive::init();  // the tanh table, outside the audio path
  reset();
}

void Synth::reset() {
  for (auto& v : voices_) v = Voice();
  age_ = 0;
  nEv_ = 0;
  delay_.clear();
  reverb_.clear();
  comp_.reset();
  for (int t = 0; t < kSynthTracks; ++t) startTrack(static_cast<uint8_t>(t));
}

void Synth::startTrack(uint8_t track) {
  if (track >= kSynthTracks) return;
  TrackRt& r = rt_[track];
  r.instr = kNoInstr;
  r.bend = 0;
  r.tps = 24;
  r.noteStep = true;
  r.gen = 0;
  r.vib = r.arp = r.cut = r.sld = r.ofs = r.slc = 0;
  resetArp(r);
  r.vslSet = r.ofsSet = r.slcSet = false;
  r.vsl = 0;
  r.lastPitch = -1;
  r.lockMask = 0;
  for (auto& x : r.lock) x = 0;
  for (auto& v : voices_)
    if (v.on && v.track == track) control(v, 0);
}

bool Synth::event(int offset, uint8_t track, const uint8_t* b, uint8_t len) {
  if (track >= kSynthTracks || len == 0 || len > 3) return false;
  if (nEv_ == kMaxEvents) {
    if (!isOff(b, len)) return false;
    int victim = -1;
    for (int i = nEv_ - 1; i >= 0 && victim < 0; --i)
      if (!isOff(ev_[i].b, ev_[i].len)) victim = i;
    if (victim < 0) return false;
    memmove(&ev_[victim], &ev_[victim + 1], sizeof(Ev) * (nEv_ - victim - 1));
    --nEv_;
  }
  const uint8_t off = static_cast<uint8_t>(offset < 0 ? 0 : (offset >= kBlock ? kBlock - 1 : offset));
  int at = nEv_;
  while (at > 0 && ev_[at - 1].off > off) --at;  // stable: after equal offsets
  memmove(&ev_[at + 1], &ev_[at], sizeof(Ev) * (nEv_ - at));
  Ev& e = ev_[at];
  e.off = off;
  e.track = track;
  e.len = len;
  for (int i = 0; i < 3; ++i) e.b[i] = i < len ? b[i] : 0;
  ++nEv_;
  return true;
}

uint8_t Synth::trackInstr(uint8_t track) const {
  const uint8_t i = rt_[track].instr != kNoInstr ? rt_[track].instr
                                                 : (track < kTracks ? p_.tracks[track].instr : 0);
  return i < kInstruments ? i : 0;
}

MT_HOT uint8_t Synth::trackVol(uint8_t track) const {
  const uint8_t v = track < kTracks ? p_.tracks[track].vol : kPreviewVol;
  return v > 127 ? 127 : v;
}

void Synth::apply(const Ev& e) {
  const uint8_t k = e.b[0] & 0xF0;
  if (e.b[0] == 0xFE) startTrack(e.track);
  else if (e.b[0] == 0xFF) releaseTrack(e.track);
  else if (e.b[0] == 0xF5) {
    if (e.len >= 3) fx(e.track, e.b[1], e.b[2]);
  }
  else if (k == 0x90 && e.len >= 3) e.b[2] ? noteOn(e.track, e.b[1] & 127, e.b[2]) : noteOff(e.track, e.b[1] & 127);
  else if (k == 0x80 && e.len >= 2) noteOff(e.track, e.b[1] & 127);
  else if (k == 0xC0 && e.len >= 2) rt_[e.track].instr = e.b[1] < kInstruments ? e.b[1] : kInstruments - 1;
  else if (k == 0xE0 && e.len >= 3) {
    const int v = ((e.b[1] & 127) | ((e.b[2] & 127) << 7)) - 8192;
    rt_[e.track].bend = v * (2.f / 8192.f);
    for (auto& x : voices_)
      if (x.on && x.track == e.track) control(x, 0);
  }
}

float Synth::tickSamples() const {
  const uint16_t b = p_.bpm;
  const int bpm = b < 20 ? 20 : (b > 300 ? 300 : b);
  return kSynthRate * 0.625f / bpm;  // 625000 / bpm us
}

// Project::dlyTime sixteenths (24 ticks each); Delay clamps it to its line.
uint32_t Synth::delaySamples() const {
  const uint8_t t = p_.dlyTime < 1 ? 1 : (p_.dlyTime > kDlyTimeMax ? kDlyTimeMax : p_.dlyTime);
  return static_cast<uint32_t>(t * 24 * tickSamples() + 0.5f);
}

void Synth::startVsl(Voice& v, int8_t val) const {
  const float len = stepSamples(v.track);
  v.vslLeft = static_cast<int32_t>(len + 0.5f);
  v.vslStep = v.vslLeft > 0 ? val / 64.f / len : 0;
}

void Synth::startCut(Voice& v, uint8_t ticks) const {
  v.cutLeft = static_cast<int32_t>(ticks * tickSamples() + 0.5f);
}

void Synth::slideTo(Voice& v, float target, uint8_t sld) const {
  v.target = target;
  v.slideStep = (target - v.pitch) / (sld * 4.f * (kSynthRate / 1000.f));
}

void Synth::resetArp(TrackRt& r) const {
  r.arm = kArmDefault;
  r.arpChord = kNoArpChord;
  r.arpN = 0;
}

void Synth::fx(uint8_t track, uint8_t cmd, uint8_t val) {
  TrackRt& r = rt_[track];
  if (cmd == kSynthStep) {
    r.tps = (val & 0x7F) ? (val & 0x7F) : 24;
    r.noteStep = (val & 0x80) != 0;
    if (r.noteStep) {
      r.vib = r.arp = 0;
      resetArp(r);
      ++r.gen;
    }
    r.cut = r.sld = 0;
    r.vslSet = r.ofsSet = r.slcSet = false;
    r.lockMask = 0;
    return;
  }
  if (cmd == kSynthArpChord) {  // the step's note-on arpeggiates this chord (intervals from its note)
    r.arpChord = val;
    return;
  }
  // On a step without a note, fx act on the track's sounding voices now.
  const bool now = !r.noteStep;
  switch (static_cast<Fx>(cmd)) {
    case Fx::SLD:
      if (!val) break;
      if (now) {
        const int v = trackVoice(track);
        if (v >= 0 && r.lastPitch >= 0) slideTo(voices_[v], r.lastPitch, val);
      } else {
        r.sld = val;
      }
      break;
    case Fx::VIB: r.vib = val; break;
    case Fx::ARM: r.arm = val; break;
    case Fx::ARP:
      r.arp = val;
      if (now)
        for (auto& v : voices_)
          if (v.on && v.track == track) v.arpT = 0;
      break;
    case Fx::VSL:
      r.vslSet = true;
      r.vsl = fxSigned(val);
      if (now)
        for (auto& v : voices_)
          if (v.on && v.track == track) startVsl(v, r.vsl);
      break;
    case Fx::OFS:
      r.ofsSet = true;
      r.ofs = val;
      break;
    case Fx::SLC:
      r.slcSet = true;
      r.slc = val;
      break;
    case Fx::CUT:
      r.cut = val;
      if (now && val)
        for (auto& v : voices_)
          if (v.on && v.track == track) startCut(v, val);
      break;
    case Fx::DCY:
    case Fx::COL:
    case Fx::SHP:
    case Fx::SWP:
    case Fx::CON:
    case Fx::FLT:
    case Fx::RES:
    case Fx::DLY:
    case Fx::DRV:
    case Fx::RVB:
    case Fx::BIT:
    case Fx::SRR: {
      // Lock: this step's note-ons, or the track's sounding voices. Macros: FM / DRUM / SYNTH voices only.
      const int k = cmd == static_cast<uint8_t>(Fx::DLY)   ? kLockDly
                    : cmd == static_cast<uint8_t>(Fx::DRV) ? kLockDrv
                    : cmd == static_cast<uint8_t>(Fx::RVB) ? kLockRvb
                    : cmd == static_cast<uint8_t>(Fx::BIT) ? kLockBit
                    : cmd == static_cast<uint8_t>(Fx::SRR) ? kLockSrr
                                                           : cmd - static_cast<uint8_t>(Fx::DCY);
      const bool macro = k < kFmMacros;
      const uint8_t lv = val > 127 ? 127 : val;
      if (now) {
        for (auto& x : voices_)
          if (x.on && x.track == track && (!macro || x.fm || x.drum || x.syn)) {
            x.lock[k] = lv;
            x.lockMask |= static_cast<uint16_t>(1u << k);
          }
      } else {
        r.lock[k] = lv;
        r.lockMask |= static_cast<uint16_t>(1u << k);
      }
      break;
    }
    default: break;
  }
}

uint8_t Synth::machineOf(const Instrument& m) {
  const uint8_t n = m.type == InstrType::Drum ? static_cast<uint8_t>(DrumMachine::Count)
                                               : static_cast<uint8_t>(FmMachine::Count);
  return m.machine < n ? m.machine : 0;
}

// One-shot drum voice (DRUM, FM drum machines): ignores note-offs, always retriggers (choke).
bool Synth::oneShot(const Voice& v) { return v.drum || (v.fm && !fmGated(v.machine)); }

// -1..1, LCG.
float Synth::rnd() {
  rng_ = rng_ * 1664525u + 1013904223u;
  return static_cast<int32_t>(rng_) * (1.f / 2147483648.f);
}

// KIT sampler lane release: envTimeMs(32) ~ 10 ms.
static constexpr uint8_t kLaneRelease = 32;

// KIT sampler lane: the lane's sample at its own pitch (root = lane note) + pitch, one-shot or
// decay. out holds Instrument() defaults in every other field: start 0, end 0xFFFF, loop Off, no
// slices, forward.
static void buildLane(Instrument& out, const Instrument& k, const KitLane& ln) {
  out.type = InstrType::Sample;
  for (int i = 0; i < kSampleNameMax; ++i) out.sample[i] = ln.sample[i];  // the UI may be editing it
  out.sample[kSampleNameMax] = 0;
  out.root = ln.note;
  out.transpose = ln.pitch;
  out.vol = ln.vol;
  out.attack = 0;
  out.decay = ln.decay;
  out.sustain = ln.decay ? 0 : 127;
  out.release = kLaneRelease;
  out.mono = true;
  out.fltMode = 0;
  for (int i = 0; i < kLfos; ++i) lfoRef(out, i).depth = 0;
  out.send = k.send;
  out.rsend = k.rsend;
}

const Instrument& Synth::instrOf(const Voice& v) const {
  const Instrument& m = p_.instruments[v.instr];
  if (!v.lane) return m;
  buildLane(laneScratch_, m, m.kit[v.laneIdx]);
  return laneScratch_;
}

void Synth::noteOn(uint8_t track, uint8_t note, uint8_t vel) {
  uint8_t ii = trackInstr(track);
  bool kitLane = false;  // a KIT lane: mono per lane (choke on the same note), poly across lanes
  int laneIdx = 0;
  Instrument scratch;    // sampler lane: built from the lane (instrOf rebuilds it while the voice plays)
  const Instrument* mp = &p_.instruments[ii];
  if (mp->type == InstrType::Kit) {
    const Instrument& k = *mp;
    int lane = -1;
    for (int l = 0; l < kKitLanes && lane < 0; ++l)
      if (k.kit[l].note == note) lane = l;
    if (lane < 0) return;
    const KitLane& ln = k.kit[lane];
    kitLane = true;
    laneIdx = lane;
    if (ln.instr < kInstruments) {
      if (p_.instruments[ln.instr].type == InstrType::Kit) return;  // no kit in a kit
      ii = ln.instr;
      mp = &p_.instruments[ii];
    } else {
      buildLane(scratch, k, ln);  // mini sampler
      mp = &scratch;
    }
  }
  const Instrument& m = *mp;
  const bool sample = m.type == InstrType::Sample;
  const int16_t* smp = nullptr;
  uint32_t frames = 0, rate = 0;
  if (sample) {
    // The UI may be editing the name: copy it first.
    char name[kSampleNameMax + 1];
    for (int i = 0; i < kSampleNameMax; ++i) name[i] = m.sample[i];
    name[kSampleNameMax] = 0;
    if (bank_) smp = bank_->find(name, frames, rate);
    if (!smp || frames == 0 || rate == 0) return;
  }
  TrackRt& r = rt_[track];
  // Slices: NOTE maps note - root to a slice (none = silent), FX takes SLC (none = whole region).
  // The UI may be editing them: the count is read once.
  int slice = -1;
  bool slicePitch = false;  // NOTE: the slice plays at the root
  if (sample) {
    int cnt = m.sliceCount;
    if (cnt > kMaxSlices) cnt = kMaxSlices;
    if (cnt) {
      if (track == kPreviewTrack && r.slcSet) {  // slice audition (SampleEditor)
        if (r.slc >= cnt) {
          r.slcSet = false;
          return;
        }
        slice = r.slc;
        slicePitch = true;
      } else if (m.sliceMode == static_cast<uint8_t>(SliceMode::Note)) {
        slice = note - m.root;
        if (slice < 0 || slice >= cnt) return;
        slicePitch = true;
      } else if (m.sliceMode == static_cast<uint8_t>(SliceMode::Fx) && r.slcSet && r.slc < cnt) {
        slice = r.slc;
      }
    }
    r.slcSet = false;
  }
  const bool fm = m.type == InstrType::Fm;
  const bool drumT = m.type == InstrType::Drum;
  const bool synT = m.type == InstrType::Synth;
  // A wavetable oscillator costs about a DRUM voice (bench); BL-only SYNTH is light.
  const bool synWtT = synT && (m.synOsc[0] == static_cast<uint8_t>(SynOsc::Wt) ||
                               m.synOsc[1] == static_cast<uint8_t>(SynOsc::Wt));
  const bool heavy = fm || drumT || synWtT;
  const uint8_t machine = machineOf(m);
  const bool drum = drumT || (fm && !fmGated(machine));  // one-shot, choke
  const bool tone = fm && machine == static_cast<uint8_t>(FmMachine::Tone);
  // FM: only TONE may be poly; drums and CHORD take the track's voice. DRUM is always mono.
  // A KIT lane is mono through its own note (below), so the track is poly across lanes.
  const bool mono = kitLane ? false : fm ? (tone ? m.mono : true) : (drumT ? true : m.mono);
  const float pitch = (slicePitch ? m.root : note) + clampf(m.transpose, -24, 24) + clampf(m.fine, -50, 50) * 0.01f;
  bool legato;
  // SLD: the track's sounding voice glides to the note (like legato), else a new one from the last note.
  // A KIT lane never takes another lane's voice: only its own (same note) glides or chokes.
  int held = r.sld && !kitLane ? trackVoice(track) : -1;
  if (kitLane)  // the lane's sounding voice: choke
    for (int x = 0; x < kVoices; ++x)
      if (voices_[x].on && voices_[x].track == track && voices_[x].note == note) held = x;
  // A heavy (FM / DRUM) note gliding from a CHIP / SAMPLE voice adds a heavy voice: past the
  // limit allocVoice picks one (SLD then starts from the last note).
  if (held >= 0 && heavy && !heavyLoad(voices_[held])) {
    int n = 0;
    for (const auto& x : voices_) n += heavyLoad(x);
    if (n >= kFmVoiceMax) held = -1;
  }
  // A SAMPLE note chokes the track's samples of earlier steps and its own retrigger (a chord
  // in one step stays). SLD instead continues the held voice.
  if (sample && !kitLane && held < 0 && track < kTracks)
    for (auto& x : voices_)
      if (x.on && x.track == track && x.sample && x.env.stage() != Env::Stage::Release &&
          (x.gen != r.gen || x.note == note))
        x.env.fade(kChokeMs);
  int vi;
  if (held >= 0) {
    vi = held;
    legato = true;
    voices_[vi].age = ++age_;
  } else {
    vi = allocVoice(voices_, track, mono, age_, legato, heavy, kitLane ? kKitLanes : kPolyPerTrack);
  }
  Voice& v = voices_[vi];
  v.stolen = false;  // SLD may take a fading voice back
  const bool keepFm = legato && v.fm;  // a sounding FM voice: choke without a click
  const bool prevDrum = legato && oneShot(v);  // its one-shot sound and gate env end here
  const bool wasDrum = legato && v.drum;       // a sounding DRUM voice: choke without a click
  // A releasing (or finished, not yet freed) voice is reused, but that is no legato: the note
  // restarts the sample and gets no glide. SLD still slides from it.
  const bool wasReleasing = v.env.stage() == Env::Stage::Release || v.env.idle();
  // A drum always retriggers (choke), and so does a note following one.
  // A KIT lane re-hit restarts its sound.
  const bool overlap = legato && !wasReleasing && !drum && !prevDrum && !kitLane;
  // Legato on the same sample keeps playing from where it is.
  const bool restartSmp = sample && (!overlap || !v.sample || v.smp != smp || v.slice != slice);
  v.note = note;
  v.instr = ii;
  v.lane = kitLane && mp == &scratch;
  v.laneIdx = static_cast<uint8_t>(laneIdx);
  v.sample = sample;
  v.gen = r.gen;
  v.smp = smp;
  v.slice = static_cast<int8_t>(slice);
  v.smpLen = frames;
  v.smpRate = rate;
  v.gain = vel * (1.f / 127.f);
  v.vel = vel;
  v.arpK = UINT32_MAX;
  if (r.arpChord != kNoArpChord) {  // ARP + CHD: the chord's notes above this one
    uint8_t notes[4];
    const int n = chordNotes(note, r.arpChord, p_.scaleRoot, static_cast<ScaleType>(p_.scaleType), notes);
    r.arpN = static_cast<uint8_t>(n < 0 ? 0 : (n > 4 ? 4 : n));
    for (int i = 0; i < r.arpN; ++i) r.arpNotes[i] = static_cast<int8_t>(notes[i] - note);
    r.arpChord = kNoArpChord;
  }
  v.target = pitch;
  if (r.sld) {
    if (!legato) v.pitch = r.lastPitch >= 0 ? r.lastPitch : pitch;
    slideTo(v, pitch, r.sld);
    r.sld = 0;  // the first note-on of the step takes it
  } else if (overlap && m.glide && (!fm || tone)) {
    slideTo(v, pitch, m.glide);
  } else {
    v.pitch = pitch;
    v.slideStep = 0;
  }
  if (!legato) {
    v.osc.phase = 0;
    v.pwmPhase = 0;
    v.vibPhase = 0;
  }
  if (!fm && !overlap) resetLfos(v);  // FM restarts its LFOs below; DRUM never overlaps
  r.lastPitch = pitch;
  v.arpT = 0;
  v.vslLeft = 0;
  if (r.vslSet) startVsl(v, r.vsl);
  v.cutLeft = -1;
  if (r.cut) startCut(v, r.cut);
  v.ofs = 0;
  if (sample && r.ofsSet) {
    v.ofs = r.ofs;
    r.ofsSet = false;
  }
  v.fm = fm;
  v.drum = drumT;
  v.syn = synT;
  v.synHeavy = synWtT;
  v.machine = machine;
  // Filter locks go to every type, macro locks to FM / DRUM / SYNTH only.
  constexpr uint8_t kMacroBits = (1 << kFmMacros) - 1;
  v.lockMask = (heavy || synT) ? r.lockMask : (r.lockMask & ~kMacroBits);
  for (int k = 0; k < kLocks; ++k) v.lock[k] = r.lock[k];
  if (fm || drumT || synT) v.useEngine(fm ? EngineKind::Fm : drumT ? EngineKind::Drum : EngineKind::Syn);
  if (fm) {
    v.fpValid = false;
    if (!overlap || !keepFm) {  // legato from a CHIP / SAMPLE voice starts the FM voice afresh
      // A held machine after a drum starts afresh too: its level is 1 at once, ramping the
      // drum's operators into it would jump from the drum's decayed level.
      v.fmv().trigger(keepFm && (drum || !prevDrum));
      resetLfos(v);
    }
  }
  if (drumT) {
    v.fpValid = false;
    v.drv().trigger(wasDrum);
  }
  if (synT) {
    // The UI may be editing the names: copy them first. Only WT oscillators look a table up.
    for (int k = 0; k < 2; ++k) {
      char name[kSampleNameMax + 1];
      for (int i = 0; i < kSampleNameMax; ++i) name[i] = m.synWt[k][i];
      name[kSampleNameMax] = 0;
      const bool wtMode = m.synOsc[k] == static_cast<uint8_t>(SynOsc::Wt);
      v.synWt[k] = (wt_ && wtMode && name[0]) ? wt_->findWt(name) : nullptr;
    }
    if (!overlap) {  // legato keeps the phases and the env -> SHAPE running, as the filter env
      v.sv().trigger();
      v.senvT = 0;
    }
  }
  if (restartSmp) startSample(v, m);
  if (!overlap) {
    const uint8_t sus = m.sustain > 127 ? 127 : m.sustain;
    if (drum) {
      // The FM / DRUM voice shapes itself; the env only gates. Note-offs are ignored: the short
      // release only fades an all-off (stop, output change).
      v.env.set(0, 0, 1.f, kDrumReleaseMs);
    } else {
      const uint8_t dec = velDecay(v, m, (v.lockMask & (1u << kMacDec)) ? v.lock[kMacDec] : m.macro[kMacDec]);
      v.env.set(envTimeMs(m.attack), fm ? fmDecayMs(dec) : envTimeMs(m.decay), sus * (1.f / 127.f),
                envTimeMs(m.release));
    }
    v.env.gate(true);
  }
  if (!overlap) {  // legato keeps the filter envelope running (303 style)
    v.fenvT = 0;
    v.fenvDone = false;
  }
  if (!legato) {
    v.flt.reset();
    v.crush.reset();
  }
  control(v, 0);
}

void Synth::startSample(Voice& v, const Instrument& m) const {
  const uint32_t len = v.smpLen;
  uint32_t from, to, lf;
  // A slice plays [from, to) of its own once: the loop is ignored.
  const bool sliced = v.slice >= 0 && sliceRegion(m, v.slice, len, from, to);
  if (!sliced) {
    from = static_cast<uint32_t>(static_cast<uint64_t>(m.start) * len / 0xFFFF);
    to = static_cast<uint32_t>(static_cast<uint64_t>(m.end) * len / 0xFFFF);
    if (from >= len) from = len - 1;
    if (to > len) to = len;
    if (to <= from) to = from + 1;
  }
  const uint32_t span = to - from;
  if (sliced) {
    lf = from;
  } else {
    lf = from + static_cast<uint32_t>(static_cast<uint64_t>(m.loopStart) * span / 0xFFFF);
    if (lf >= to) lf = to - 1;
  }
  v.from = from;
  v.to = to;
  v.loopMode = sliced ? static_cast<uint8_t>(LoopMode::Off)
                      : (m.loop < static_cast<uint8_t>(LoopMode::Count) ? m.loop : 0);
  const uint32_t ofs = static_cast<uint32_t>(static_cast<uint64_t>(v.ofs) * span / 256);
  if (m.reverse) {
    // Reverse plays the region mirrored: the loop is [from, to - (lf - from)) walked downwards.
    v.dir = -1;
    v.loopLo = from;
    v.loopHi = to - (lf - from);
    v.pos = static_cast<int64_t>(to - 1 - ofs) << 32;
  } else {
    v.dir = 1;
    v.loopLo = lf;
    v.loopHi = to;
    v.pos = static_cast<int64_t>(from + ofs) << 32;
  }
  if (v.loopMode == static_cast<uint8_t>(LoopMode::Off)) {
    v.loopLo = from;
    v.loopHi = to;
  }
}

// Samples on the pattern tracks play until OFF / CUT / a choke (not on the preview track).
void Synth::noteOff(uint8_t track, uint8_t note) {
  for (auto& v : voices_)
    if (v.on && v.track == track && v.note == note && v.env.stage() != Env::Stage::Release && !oneShot(v) &&
        !(v.sample && track < kTracks))
      v.env.gate(false);
}

void Synth::releaseTrack(uint8_t track) {
  for (auto& v : voices_)
    if (v.on && v.track == track) v.env.gate(false);
}

MT_HOT void Synth::control(Voice& v, int dt) {
  const Instrument& m = instrOf(v);
  const TrackRt& r = rt_[v.track];
  v.fenvT += static_cast<uint32_t>(dt);
  const uint8_t snd = (v.lockMask & (1u << kLockDly)) ? v.lock[kLockDly] : m.send;
  v.send = (snd > 127 ? 127 : snd) * (1.f / 127.f);
  const uint8_t rs = (v.lockMask & (1u << kLockRvb)) ? v.lock[kLockRvb] : m.rsend;
  v.rsend = (rs > 127 ? 127 : rs) * (1.f / 127.f);
  if (v.slideStep != 0) {
    v.pitch += v.slideStep * dt;
    if ((v.slideStep > 0 && v.pitch >= v.target) || (v.slideStep < 0 && v.pitch <= v.target)) {
      v.pitch = v.target;
      v.slideStep = 0;
    }
  }
  if (v.vslLeft > 0) {
    const int n = dt < v.vslLeft ? dt : v.vslLeft;
    v.gain = clampf(v.gain + v.vslStep * n, 0.f, 1.f);
    v.vslLeft -= n;
  }
  if (v.cutLeft >= 0) {
    v.cutLeft -= dt;
    if (v.cutLeft <= 0) {
      v.env.kill();  // no release
      v.cutLeft = -1;
    }
  }
  float pitch = v.pitch + r.bend;
  v.arpOff = 0;
  if (r.arp) {
    // Notes 0, +x, +y (or the ARP + CHD chord), `rate` switches per step in the ARM order; the
    // default ARM (UP, 3) is the plain ARP: a third of a step each.
    int8_t offs[4] = {0, static_cast<int8_t>(r.arp >> 4), static_cast<int8_t>(r.arp & 15), 0};
    int n = 3;
    if (r.arpN > 1) {
      n = r.arpN;
      for (int i = 0; i < n; ++i) offs[i] = r.arpNotes[i];
    }
    const int rate = (r.arm & 15) < 1 ? 1 : ((r.arm & 15) > kArmRateMax ? kArmRateMax : (r.arm & 15));
    v.arpT += static_cast<uint32_t>(dt);
    const uint32_t slot = static_cast<uint32_t>(stepSamples(v.track) / static_cast<float>(rate));
    const uint32_t k = slot ? v.arpT / slot : 0;
    switch ((r.arm >> 4) & 3) {
      case 1: v.arpIdx = static_cast<uint8_t>(n - 1 - static_cast<int>(k % n)); break;  // DOWN
      case 2: {  // UPDOWN, the ends once: 0 1 2 1 0 1 ...
        const int cyc = n > 1 ? 2 * n - 2 : 1;
        const int q = static_cast<int>(k % cyc);
        v.arpIdx = static_cast<uint8_t>(q < n ? q : cyc - q);
        break;
      }
      case 3:  // RANDOM: one pick per slot
        if (k != v.arpK) {
          const int i = static_cast<int>((rnd() * 0.5f + 0.5f) * n);
          v.arpIdx = static_cast<uint8_t>(i < 0 ? 0 : (i >= n ? n - 1 : i));
        }
        break;
      default: v.arpIdx = static_cast<uint8_t>(k % n); break;  // UP
    }
    v.arpK = k;
    if (v.arpIdx >= n) v.arpIdx = 0;
    v.arpOff = offs[v.arpIdx];
    pitch += v.arpOff;
  }
  const uint8_t vx = r.vib >> 4, vy = r.vib & 15;
  if (vx && vy) {
    // Sine LFO, x * 0.5 Hz, depth y / 15 * 2 semitones.
    v.vibPhase += vx * 0.5f * dt * (1.f / kSynthRate);
    v.vibPhase -= static_cast<int>(v.vibPhase);
    pitch += vy * (2.f / 15.f) * sinf(6.2831853f * v.vibPhase);
  } else {
    v.vibPhase = 0;
  }
  // LFOs 1..4: PITCH, VOL, CUTOFF, DRIVE on every type, summed; macro targets go to controlFm /
  // controlDrum / controlSyn (lm: macro offsets).
  float lfoVol = 1, lfoCut = 0, lfoDrv = 0;
  float lm[kFmMacros] = {};
  for (int i = 0; i < kLfos; ++i) {
    const LfoCfg c = lfoAt(m, i);
    const float l = lfo(v, c, i, dt);
    if (l == 0) continue;
    const uint8_t dest = c.dest < static_cast<uint8_t>(LfoDest::Count) ? c.dest : 0;
    if (dest == static_cast<uint8_t>(LfoDest::Pitch)) pitch += 12.f * l;
    else if (dest == static_cast<uint8_t>(LfoDest::Vol)) lfoVol *= clampf(1.f + l, 0.f, 2.f);
    else if (dest == static_cast<uint8_t>(LfoDest::Cutoff)) lfoCut += 64.f * l;
    else if (dest == static_cast<uint8_t>(LfoDest::Drive)) lfoDrv += 64.f * l;
    else if (dest >= static_cast<uint8_t>(LfoDest::Dec) && dest <= static_cast<uint8_t>(LfoDest::Con))
      lm[dest - 1] += 64.f * l;
  }
  const uint8_t drv = (v.lockMask & (1u << kLockDrv)) ? v.lock[kLockDrv] : (m.drive > 127 ? 127 : m.drive);
  v.drive.set(static_cast<uint8_t>(clampf(drv + lfoDrv, 0.f, 127.f) + 0.5f));
  v.crush.set((v.lockMask & (1u << kLockBit)) ? v.lock[kLockBit] : m.crushBits,
              (v.lockMask & (1u << kLockSrr)) ? v.lock[kLockSrr] : m.crushRate);
  controlFilter(v, m, pitch, lfoCut);
  if (v.fm) {
    controlFm(v, m, pitch, dt, lm, lfoVol);  // sets v.amp too
    return;
  }
  if (v.drum) {
    controlDrum(v, m, pitch, dt, lm, lfoVol);  // sets v.amp too
    return;
  }
  if (v.syn) {
    controlSyn(v, m, pitch, dt, lm, lfoVol);  // sets v.amp too
    return;
  }
  if (v.sample) {
    // Frames per output sample; the root note plays at the sample's own rate.
    const uint8_t root = m.root > 127 ? 127 : m.root;
    const float semis = pitch - root;
    if (semis != v.smpKey) {
      v.smpKey = semis;
      v.smpVal = exp2f(semis * (1.f / 12.f));
    }
    float inc = v.smpRate * (1.f / kSynthRate) * v.smpVal;
    inc = clampf(inc, 0.f, 256.f);
    v.inc = inc;
    v.step = static_cast<int64_t>(inc * 4294967296.f);
  } else {
    v.inc = cachedHz(v, 0, pitch) * (1.f / kSynthRate);
    // Noise: the LFSR steps 93x the note frequency (NES-like rates, kHz), so METAL's 93-step
    // period sounds at the played pitch and NOISE is a hiss that darkens with lower notes.
    const uint8_t w = m.wave < kWaveCount ? m.wave : 0;
    if (w == static_cast<uint8_t>(Wave::Noise) || w == static_cast<uint8_t>(Wave::Metal))
      v.inc = clampf(v.inc * kNoiseClock, 0.f, kNoiseMaxSteps);
  }
  v.wave = m.wave < kWaveCount ? m.wave : 0;
  float duty = clampf(m.duty, 1, 99) * 0.01f;
  if (m.pwmRate && m.pwmDepth) {
    // Triangle sweep, pwmRate / 10 Hz.
    v.pwmPhase += m.pwmRate * 0.1f * dt * (1.f / kSynthRate);
    v.pwmPhase -= static_cast<int>(v.pwmPhase);
    const float tri = 4.f * (v.pwmPhase < 0.5f ? 0.5f - v.pwmPhase : v.pwmPhase - 0.5f) - 1.f;
    duty = clampf(duty + tri * m.pwmDepth * 0.01f, 0.01f, 0.99f);
  }
  v.duty = duty;
  const uint8_t iv = m.vol > 127 ? 127 : m.vol;
  v.amp = v.gain * iv * trackVol(v.track) * lfoVol * (1.f / (127.f * 127.f));
}

// Filter at control rate: off costs nothing per sample (renderVoice skips it).
MT_HOT void Synth::controlFilter(Voice& v, const Instrument& m, float pitch, float lfoCut) {
  const uint8_t mode = m.fltMode < static_cast<uint8_t>(FltMode::Count) ? m.fltMode : 0;
  const bool was = v.fltOn;
  v.fltOn = mode != static_cast<uint8_t>(FltMode::Off);
  if (!v.fltOn) return;
  if (!was) v.flt.reset();  // turned on mid-note (UI, legato from an unfiltered instrument): no stale state
  const float cut = (v.lockMask & (1u << kLockFlt)) ? v.lock[kLockFlt] : (m.cutoff > 127 ? 127 : m.cutoff);
  const float res = (v.lockMask & (1u << kLockRes)) ? v.lock[kLockRes] : (m.reso > 127 ? 127 : m.reso);
  if (res != v.fltRes) {
    v.fltRes = res;
    v.fltQ = resoQ(res);
  }
  // Octaves above 20 Hz (as cutoffHz): cutoff, envelope, key tracking (from C4).
  float oct = clampf(cut + lfoCut, 0.f, 127.f) * (9.451211f / 127.f);  // log2(700)
  if (m.fenv && !v.fenvDone) {
    const float e = filterEnv(v.fenvT, m.fAtk, m.fDec);
    // The -60 dB tail moves the cutoff < 0.01 octave: drop it (not the start of a slow attack).
    if (e < 1e-3f && v.fenvT > envTimeMs(m.fAtk) * (kSynthRate / 1000.f)) v.fenvDone = true;
    oct += clampf(m.fenv, -64, 63) * (6.f / 64.f) * e;
  }
  oct += (m.keytrack > 127 ? 127 : m.keytrack) * (1.f / 127.f) * (pitch - 60.f) * (1.f / 12.f);
  // Velocity: +-6 octaves at full depth from velocity 64 (no change) to 0 / 127.
  if (m.velCut) oct += clampf(m.velCut, -64, 63) * (1.f / 64.f) * ((static_cast<int>(v.vel) - 64) * (1.f / 64.f)) * 6.f;
  if (oct != v.octKey) {
    v.octKey = oct;
    v.octHz = 20.f * exp2f(oct);
  }
  v.flt.set(static_cast<Svf::Mode>(mode - 1), v.octHz, v.fltQ);
}

MT_HOT float Synth::cachedHz(Voice& v, int k, float note) {
  if (note != v.hzKey[k]) {
    v.hzKey[k] = note;
    v.hzVal[k] = noteHz(note);
  }
  return v.hzVal[k];
}

void Synth::resetLfos(Voice& v) {
  for (int i = 0; i < kLfos; ++i) {
    v.lfoPhase[i] = 0;
    v.lfoRnd[i] = rnd();
  }
}

// LFO i of a voice: free (lfoHz) or a tempo division (lfoSyncHz at the project's BPM), restarted at
// note-on; -1..1 x depth / 64.
MT_HOT float Synth::lfo(Voice& v, const LfoCfg& c, int i, int dt) {
  if (!c.depth) return 0;
  const float hz = c.sync ? lfoSyncHz(c.rate, p_.bpm) : lfoHz(c.rate);
  float& ph = v.lfoPhase[i];
  ph += hz * dt * (1.f / kSynthRate);
  if (ph >= 1.f) {
    ph -= static_cast<int>(ph);
    v.lfoRnd[i] = rnd();
  }
  float w;
  switch (static_cast<LfoWave>(c.wave)) {
    case LfoWave::Tri: w = 4.f * (ph < 0.5f ? 0.5f - ph : ph - 0.5f) - 1.f; break;
    case LfoWave::Saw: w = 2.f * ph - 1.f; break;
    case LfoWave::Square: w = ph < 0.5f ? 1.f : -1.f; break;
    case LfoWave::Random: w = v.lfoRnd[i]; break;
    default: w = sinf(6.2831853f * ph); break;
  }
  const int d = c.depth < -64 ? -64 : (c.depth > 63 ? 63 : c.depth);
  return w * d * (1.f / 64.f);
}

// Macros of an FM / DRUM / SYNTH voice: locks over the instrument's, then the LFO's macro target (l:
// control()'s LFO value).
// Velocity -> DECAY macro (macro 0: SHP1 on SYNTH): velMac x (vel - 64) / 64 macro units.
MT_HOT uint8_t Synth::velDecay(const Voice& v, const Instrument& m, uint8_t dec) {
  if (!m.velMac) return dec;
  const int d = dec + clampf(m.velMac, -64, 63) * (static_cast<int>(v.vel) - 64) / 64;
  return static_cast<uint8_t>(d < 0 ? 0 : (d > 127 ? 127 : d));
}

MT_HOT void Synth::macros(const Voice& v, const Instrument& m, const float* lm, float (&mac)[kFmMacros]) {
  for (int k = 0; k < kFmMacros; ++k) mac[k] = (v.lockMask & (1u << k)) ? v.lock[k] : m.macro[k];
  mac[kMacDec] = velDecay(v, m, static_cast<uint8_t>(mac[kMacDec] > 127 ? 127 : mac[kMacDec]));
  for (int k = 0; k < kFmMacros; ++k)
    if (lm[k] != 0) mac[k] = clampf(mac[k] + lm[k], 0.f, 127.f);
}

// pitch includes the LFO's PITCH target, vol its VOL target; l moves the macro targets here.
MT_HOT void Synth::controlFm(Voice& v, const Instrument& m, float pitch, int dt, const float* lm, float vol) {
  float mac[kFmMacros];
  macros(v, m, lm, mac);
  // fmMachine is pure and costly (tens of us): reuse the last params while the inputs stay
  // within half a macro step and a cent of the ones they were made from.
  bool stale = !v.fpValid || !fmCache_ || fabsf(pitch - v.fpPitch) >= kFmCacheCents;
  for (int k = 0; k < kFmMacros && !stale; ++k) stale = fabsf(mac[k] - v.fpMac[k]) >= kFmCacheMacro;
  if (stale) {
    fmMachine(v.machine, mac, pitch, v.fp());
    ++fmCalls_;
    v.fpValid = true;
    v.fpPitch = pitch;
    for (int k = 0; k < kFmMacros; ++k) v.fpMac[k] = mac[k];
  }
  // Ramp to the next control update: a full period from render(), the rest of it mid-segment
  // (note-on, bend), so a choke ramp ends exactly where the next update starts.
  v.fmv().control(v.fp(), dt ? dt : ctlLeft_);
  const uint8_t iv = m.vol > 127 ? 127 : m.vol;
  v.amp = v.gain * iv * trackVol(v.track) * vol * (1.f / (127.f * 127.f));
}

// As controlFm, with the same cache: drumMachine's powf calls cost microseconds each on the ESP32.
MT_HOT void Synth::controlDrum(Voice& v, const Instrument& m, float pitch, int dt, const float* lm, float vol) {
  float mac[kFmMacros];
  macros(v, m, lm, mac);
  bool stale = !v.fpValid || !fmCache_ || fabsf(pitch - v.fpPitch) >= kFmCacheCents;
  for (int k = 0; k < kFmMacros && !stale; ++k) stale = fabsf(mac[k] - v.fpMac[k]) >= kFmCacheMacro;
  if (stale) {
    drumMachine(v.machine, mac, pitch, v.dp());
    ++drumCalls_;
    v.fpValid = true;
    v.fpPitch = pitch;
    for (int k = 0; k < kFmMacros; ++k) v.fpMac[k] = mac[k];
  }
  v.drv().control(v.dp(), dt ? dt : ctlLeft_);  // same ramp contract as the FM voice
  const uint8_t iv = m.vol > 127 ? 127 : m.vol;
  v.amp = v.gain * iv * trackVol(v.track) * vol * (1.f / (127.f * 127.f));
}

// SYNTH: macros SHP1, SHP2, MIX, DET, SENV (env -> SHAPE depth, bipolar around 64).
MT_HOT void Synth::controlSyn(Voice& v, const Instrument& m, float pitch, int dt, const float* lm, float vol) {
  float mac[kFmMacros];
  macros(v, m, lm, mac);
  v.senvT += static_cast<uint32_t>(dt);
  // env -> SHAPE: nothing to compute at the neutral 64.
  const float senvAmt = (mac[kMacSenv] - 64.f) * (1.f / 64.f);
  const float senv = senvAmt != 0 ? senvAmt * filterEnv(v.senvT, m.synEAtk, m.synEDec) : 0.f;  // AD, decay 0 = hold
  SynParams sp;
  for (int k = 0; k < 2; ++k) {
    sp.mode[k] = m.synOsc[k] < static_cast<uint8_t>(SynOsc::Count) ? m.synOsc[k] : 0;
    sp.wt[k] = v.synWt[k];
    sp.shape[k] = clampf(mac[kMacShp1 + k] * (1.f / 127.f) + senv, 0.f, 1.f);
  }
  const float det = (mac[kMacDet] - 64.f) * (50.f / 64.f) * 0.01f;  // +-50 cents, in semitones
  sp.hz[0] = cachedHz(v, 0, pitch);
  sp.hz[1] = cachedHz(v, 1, pitch + clampf(m.synSemi, -24, 24) + det);
  sp.mix = mac[kMacMix] * (1.f / 127.f);
  sp.sync = m.synSync;
  sp.sub = (m.synSub > 127 ? 127 : m.synSub) * (1.f / 127.f);
  sp.subOct = m.synSubOct ? 2 : 1;
  sp.noise = (m.synNoise > 127 ? 127 : m.synNoise) * (1.f / 127.f);
  v.sv().control(sp, dt ? dt : ctlLeft_);  // same ramp contract as the FM voice
  const uint8_t iv = m.vol > 127 ? 127 : m.vol;
  v.amp = v.gain * iv * trackVol(v.track) * vol * (1.f / (127.f * 127.f));
}

MT_HOT void Synth::renderVoice(Voice& v, float* out, int n) {
  // Segments end at control boundaries: n <= kControl. A filtered voice renders into flt first,
  // an unfiltered one straight into out.
  if (v.env.idle()) {
    // The note has ended: only the filter rings out (render() frees the voice when it is quiet).
    if (v.fltOn)
      for (int i = 0; i < n; ++i) out[i] += v.flt.process(0.f);
    return;
  }
  // Driven or filtered: the engine renders into flt, then drive -> filter -> out. Neither: straight
  // into out (bit-exact to the plain path).
  float flt[kControl];
  float* dst = out;
  const bool crushed = v.crush.on();
  const bool driven = v.drive.on() || crushed;  // the pre-filter stage: drive, then crush
  if (v.fltOn || driven) {
    for (int i = 0; i < n; ++i) flt[i] = 0;
    dst = flt;
  }
  if (v.fm) {
    float tmp[kControl] = {0};
    v.fmv().render(tmp, n, v.amp);
    for (int i = 0; i < n; ++i) dst[i] += tmp[i] * v.env.next();
    if (v.fmv().done()) v.env.kill();
  } else if (v.drum) {
    // The gate env scales the choke tail too: it is 1 through a choke, and an all-off fades both.
    float tmp[kControl] = {0};
    v.drv().render(tmp, n, v.amp);
    for (int i = 0; i < n; ++i) dst[i] += tmp[i] * v.env.next();
    if (v.drv().done()) v.env.kill();
  } else if (v.syn) {
    float tmp[kControl] = {0};
    v.sv().render(tmp, n, v.amp);
    for (int i = 0; i < n; ++i) dst[i] += tmp[i] * v.env.next();
  } else if (v.sample) {
    renderSample(v, dst, n);
  } else {
    const Wave w = static_cast<Wave>(v.wave);
    const float inc = v.inc, duty = v.duty, amp = v.amp;
    for (int i = 0; i < n; ++i) dst[i] += v.osc.next(w, inc, duty) * v.env.next() * amp;
  }
  if (v.drive.on())
    for (int i = 0; i < n; ++i) flt[i] = v.drive.process(flt[i]);
  if (crushed)
    for (int i = 0; i < n; ++i) flt[i] = v.crush.process(flt[i]);
  if (v.fltOn)
    for (int i = 0; i < n; ++i) out[i] += v.flt.process(flt[i]);
  else if (driven)
    for (int i = 0; i < n; ++i) out[i] += flt[i];
}

// Sample playback: 32.32 fixed point position, linear interpolation between frames. The loop
// region [loopLo, loopHi) bounds the play direction's edge (for Off it is the whole region).
// Master DJ filter (Project::djFilter): low-pass 20 kHz .. 100 Hz to the left, high-pass 20 Hz ..
// 8 kHz to the right, a little resonance; the state is cleared while it is off.
void Synth::djFilter(float* x, int n) {
  const int v = p_.djFilter;
  if (v == 0) {
    dj_.reset();
    return;
  }
  const float a = v < 0 ? -v / 64.f : v / 63.f;
  const float hz = v < 0 ? 20000.f * powf(100.f / 20000.f, a) : 20.f * powf(8000.f / 20.f, a);
  dj_.set(v < 0 ? Svf::Mode::Lp : Svf::Mode::Hp, hz, 0.9f);
  for (int i = 0; i < n; ++i) x[i] = dj_.process(x[i]);
}

MT_HOT void Synth::renderSample(Voice& v, float* out, int n) {
  const int16_t* d = v.smp;
  const uint32_t last = v.smpLen - 1;
  const int64_t lo = static_cast<int64_t>(v.loopLo) << 32, hi = static_cast<int64_t>(v.loopHi) << 32;
  const int64_t one = int64_t(1) << 32;
  const float amp = v.amp * (kSampleGain / 32768.f);
  const uint8_t mode = v.loopMode;
  int64_t pos = v.pos;
  int64_t step = v.dir > 0 ? v.step : -v.step;
  for (int i = 0; i < n; ++i) {
    const uint32_t idx = static_cast<uint32_t>(pos >> 32);
    const int32_t a = d[idx];
    const int32_t b = d[idx < last ? idx + 1 : last];
    const float s = a + (b - a) * (static_cast<uint32_t>(pos) * 2.3283064e-10f);
    out[i] += s * v.env.next() * amp;
    pos += step;
    if (step >= 0 ? pos < hi : pos >= lo) continue;
    // Crossed the edge in the play direction (rare).
    if (mode == static_cast<uint8_t>(LoopMode::Forward)) {
      const int64_t span = hi - lo;
      if (step >= 0) {
        if (pos >= hi) pos = lo + (pos - lo) % span;  // one step may cross a short loop many times
      } else {
        if (pos < lo) pos = hi - 1 - (hi - 1 - pos) % span;
      }
    } else if (mode == static_cast<uint8_t>(LoopMode::PingPong)) {
      // Reflect around the edge frame: ..., 98, 99, 98, ... (the end frame is not repeated).
      pos = step >= 0 ? 2 * (hi - one) - pos : 2 * lo - pos;
      step = -step;
      if (pos < lo) pos = lo;
      if (pos >= hi) pos = hi - 1;
    } else {
      v.env.kill();
      break;
    }
  }
  v.pos = pos;
  v.dir = step >= 0 ? 1 : -1;
}

MT_HOT void Synth::render(int16_t* out) {
  // Profiling: t = the cycle counter at the last mark; mark(stage) books the time since.
  uint32_t (*const clk)() = clock_;
  uint32_t t = clk ? clk() : 0;
  auto mark = [&](int stage) {
    if (!clk) return;
    const uint32_t now = clk();
    prof_[stage] += now - t;
    t = now;
  };
  for (auto& x : mix_) x = 0;
  for (auto& x : send_) x = 0;
  for (auto& x : rsend_) x = 0;
  for (auto& x : sc_) x = 0;
  const int scTrack = p_.scTrack >= 1 && p_.scTrack <= kTracks ? p_.scTrack - 1 : -1;
  int e = 0;
  for (int pos = 0; pos < kBlock;) {
    mark(kProfMaster);
    if (pos % kControl == 0)
      for (auto& v : voices_)
        if (v.on && !v.env.idle()) control(v, kControl);  // a ringing filter tail keeps its cutoff
    mark(kProfControl);
    ctlLeft_ = kControl - pos % kControl;
    while (e < nEv_ && ev_[e].off <= pos) apply(ev_[e++]);
    mark(kProfEvents);
    int end = (pos / kControl + 1) * kControl;
    if (e < nEv_ && ev_[e].off < end) end = ev_[e].off;
    for (auto& v : voices_) {
      if (!v.on) continue;
      const bool sc = v.track == scTrack;
      const int n = end - pos;
      const bool sends = v.send > 0 || v.rsend > 0 || sc;
      if (!sends && !meters_) {
        renderVoice(v, mix_ + pos, n);
        if (v.env.idle() && !(v.fltOn && v.flt.ringing())) v.on = false;
        if (clk) mark(v.fm ? kProfFm : v.drum ? kProfDrum : v.syn ? kProfSyn : v.sample ? kProfSample : kProfChip);
        continue;
      }
      // Through tmp: the sends, and the peak for the track's level meter (MIX). A voice adds into the
      // mix once, so mix + (0 + x) is bit-exact to rendering straight into the mix.
      float tmp[kControl] = {0};
      renderVoice(v, tmp, n);
      float pk = trackPeak_[v.track < kSynthTracks ? v.track : 0];
      if (sends) {
        for (int i = 0; i < n; ++i) {
          mix_[pos + i] += tmp[i];
          send_[pos + i] += tmp[i] * v.send;
          rsend_[pos + i] += tmp[i] * v.rsend;
          if (sc) sc_[pos + i] += tmp[i];
          const float a = fabsf(tmp[i]);
          if (a > pk) pk = a;
        }
      } else {
        for (int i = 0; i < n; ++i) {
          mix_[pos + i] += tmp[i];
          const float a = fabsf(tmp[i]);
          if (a > pk) pk = a;
        }
      }
      trackPeak_[v.track < kSynthTracks ? v.track : 0] = pk;
      if (clk) mark(v.fm ? kProfFm : v.drum ? kProfDrum : v.syn ? kProfSyn : v.sample ? kProfSample : kProfChip);
      // Freed once the note has ended and its filter has rung out: cutting a resonant filter
      // mid-ring clicks.
      if (v.env.idle() && !(v.fltOn && v.flt.ringing())) v.on = false;
    }
    pos = end;
  }
  nEv_ = 0;
  ctlLeft_ = kControl;
  mark(kProfMaster);
  delay_.process(send_, mix_, kBlock, delaySamples(), p_.dlyFb, p_.dlyTone, p_.dlyLevel);
  mark(kProfDelay);
  reverb_.process(rsend_, mix_, kBlock, p_.rvbSize, p_.rvbDamp, p_.rvbLevel);
  mark(kProfReverb);
  djFilter(mix_, kBlock);
  comp_.process(mix_, scTrack >= 0 ? sc_ : nullptr, kBlock, p_.compAmt, p_.compRel, p_.scDepth);
  const uint8_t mv = p_.masterVol > kMasterVolMax ? kMasterVolMax : p_.masterVol;
  const float g = mv * (0.25f / 100.f);  // 100 %: headroom for 16 voices; up to 200 % leans on the soft clip
  for (int i = 0; i < kBlock; ++i) out[i] = static_cast<int16_t>(softClip(mix_[i] * g) * 32767.f);
  mark(kProfMaster);
  if (clk) ++profBlocks_;
}

void Synth::takeTrackPeaks(float out[kTracks]) {
  const float g = 0.25f;  // the master gain at MAIN 100 % (render())
  for (int t = 0; t < kTracks; ++t) {
    out[t] = trackPeak_[t] * g;
    trackPeak_[t] = 0;
  }
}

const char* Synth::profName(int stage) {
  static const char* const kNames[kProfStages] = {"queue",  "events", "control", "chip",   "sample", "fm",
                                                  "drum",   "synth",  "delay",   "reverb", "master"};
  return stage >= 0 && stage < kProfStages ? kNames[stage] : "";
}

void Synth::takeProfile(uint32_t out[kProfStages], uint32_t& blocks) {
  for (int i = 0; i < kProfStages; ++i) {
    out[i] = prof_[i];
    prof_[i] = 0;
  }
  blocks = profBlocks_;
  profBlocks_ = 0;
}

int Synth::activeVoices() const {
  int n = 0;
  for (const auto& v : voices_) n += v.on;
  return n;
}

int Synth::trackVoice(uint8_t track) const {
  int best = -1;
  for (int i = 0; i < kVoices; ++i)
    if (voices_[i].on && voices_[i].track == track && (best < 0 || voices_[i].age > voices_[best].age)) best = i;
  return best;
}

}  // namespace mt
