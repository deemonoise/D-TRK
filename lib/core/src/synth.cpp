#include "synth.h"
#include "synth_fm_machines.h"
#include <math.h>
#include <string.h>

namespace mt {
namespace {

// +12 dB: chip waves sit at full scale, a peak-normalised sample averages ~12-18 dB lower.
constexpr float kSampleGain = 4.f;

constexpr float kNoiseClock = 93.f;    // LFSR steps per note period
constexpr float kNoiseMaxSteps = 8.f;  // per output sample (high notes)

constexpr uint16_t kDrumReleaseMs = 3;  // FM drum gate env: fade on all-off instead of a click

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

Synth::Synth(const Project& p) : p_(p) { reset(); }

void Synth::reset() {
  for (auto& v : voices_) v = Voice();
  age_ = 0;
  nEv_ = 0;
  for (int t = 0; t < kSynthTracks; ++t) startTrack(static_cast<uint8_t>(t));
}

void Synth::startTrack(uint8_t track) {
  if (track >= kSynthTracks) return;
  TrackRt& r = rt_[track];
  r.instr = kNoInstr;
  r.bend = 0;
  r.tps = 24;
  r.noteStep = true;
  r.vib = r.arp = r.cut = r.sld = r.ofs = 0;
  r.vslSet = r.ofsSet = false;
  r.vsl = 0;
  r.lastPitch = -1;
  r.lockMask = 0;
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

uint8_t Synth::trackVol(uint8_t track) const {
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

void Synth::fx(uint8_t track, uint8_t cmd, uint8_t val) {
  TrackRt& r = rt_[track];
  if (cmd == kSynthStep) {
    r.tps = (val & 0x7F) ? (val & 0x7F) : 24;
    r.noteStep = (val & 0x80) != 0;
    if (r.noteStep) r.vib = r.arp = 0;
    r.cut = r.sld = 0;
    r.vslSet = r.ofsSet = false;
    r.lockMask = 0;
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
    case Fx::CON: {
      // FM macro lock: this step's note-ons, or the track's sounding FM voices.
      const int k = cmd - static_cast<uint8_t>(Fx::DCY);
      const uint8_t lv = val > 127 ? 127 : val;
      if (now) {
        for (auto& x : voices_)
          if (x.on && x.track == track && x.fm) {
            x.lock[k] = lv;
            x.lockMask |= 1 << k;
          }
      } else {
        r.lock[k] = lv;
        r.lockMask |= 1 << k;
      }
      break;
    }
    default: break;
  }
}

uint8_t Synth::fmMachineOf(const Instrument& m) {
  return m.machine < static_cast<uint8_t>(FmMachine::Count) ? m.machine : 0;
}

bool Synth::fmDrum(const Voice& v) { return v.fm && !fmGated(v.machine); }

// -1..1, LCG.
float Synth::rnd() {
  rng_ = rng_ * 1664525u + 1013904223u;
  return static_cast<int32_t>(rng_) * (1.f / 2147483648.f);
}

void Synth::noteOn(uint8_t track, uint8_t note, uint8_t vel) {
  const uint8_t ii = trackInstr(track);
  const Instrument& m = p_.instruments[ii];
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
  const bool fm = m.type == InstrType::Fm;
  const uint8_t machine = fmMachineOf(m);
  const bool drum = fm && !fmGated(machine);
  const bool tone = fm && machine == static_cast<uint8_t>(FmMachine::Tone);
  // FM: only TONE may be poly; drums and CHORD take the track's voice.
  const bool mono = fm ? (tone ? m.mono : true) : m.mono;
  const float pitch = note + clampf(m.transpose, -24, 24) + clampf(m.fine, -50, 50) * 0.01f;
  bool legato;
  // SLD: the track's sounding voice glides to the note (like legato), else a new one from the last note.
  int held = r.sld ? trackVoice(track) : -1;
  // An FM note gliding from a CHIP / SAMPLE voice adds an FM voice: past the limit allocVoice
  // picks one (SLD then starts from the last note).
  if (held >= 0 && fm && !voices_[held].fm) {
    int n = 0;
    for (const auto& x : voices_) n += x.on && x.fm;
    if (n >= kFmVoiceMax) held = -1;
  }
  int vi;
  if (held >= 0) {
    vi = held;
    legato = true;
    voices_[vi].age = ++age_;
  } else {
    vi = allocVoice(voices_, track, mono, age_, legato, fm);
  }
  Voice& v = voices_[vi];
  const bool keepFm = legato && v.fm;  // a sounding FM voice: choke without a click
  const bool prevDrum = legato && fmDrum(v);  // its one-shot sound and gate env end here
  // A releasing (or finished, not yet freed) voice is reused, but that is no legato: the note
  // restarts the sample and gets no glide. SLD still slides from it.
  const bool wasReleasing = v.env.stage() == Env::Stage::Release || v.env.idle();
  // An FM drum always retriggers (choke), and so does a note following one.
  const bool overlap = legato && !wasReleasing && !drum && !prevDrum;
  // Legato on the same sample keeps playing from where it is.
  const bool restartSmp = sample && (!overlap || !v.sample || v.smp != smp);
  v.note = note;
  v.instr = ii;
  v.sample = sample;
  v.smp = smp;
  v.smpLen = frames;
  v.smpRate = rate;
  v.gain = vel * (1.f / 127.f);
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
  v.machine = machine;
  v.lockMask = fm ? r.lockMask : 0;
  if (fm) {
    v.fpValid = false;
    for (int k = 0; k < kFmMacros; ++k) v.lock[k] = r.lock[k];
    if (!overlap || !keepFm) {  // legato from a CHIP / SAMPLE voice starts the FM voice afresh
      // A held machine after a drum starts afresh too: its level is 1 at once, ramping the
      // drum's operators into it would jump from the drum's decayed level.
      v.fmv.trigger(keepFm && (drum || !prevDrum));
      v.lfoPhase = 0;
      v.lfoRnd = rnd();
    }
  }
  if (restartSmp) startSample(v, m);
  if (!overlap) {
    const uint8_t sus = m.sustain > 127 ? 127 : m.sustain;
    if (drum) {
      // The FM voice shapes itself; the env only gates. Note-offs are ignored: the short
      // release only fades an all-off (stop, output change).
      v.env.set(0, 0, 1.f, kDrumReleaseMs);
    } else {
      const uint8_t dec = (v.lockMask & (1 << kMacDec)) ? v.lock[kMacDec] : m.macro[kMacDec];
      v.env.set(envTimeMs(m.attack), fm ? fmDecayMs(dec) : envTimeMs(m.decay), sus * (1.f / 127.f),
                envTimeMs(m.release));
    }
    v.env.gate(true);
  }
  control(v, 0);
}

void Synth::startSample(Voice& v, const Instrument& m) const {
  const uint32_t len = v.smpLen;
  uint32_t from = static_cast<uint32_t>(static_cast<uint64_t>(m.start) * len / 0xFFFF);
  uint32_t to = static_cast<uint32_t>(static_cast<uint64_t>(m.end) * len / 0xFFFF);
  if (from >= len) from = len - 1;
  if (to > len) to = len;
  if (to <= from) to = from + 1;
  const uint32_t span = to - from;
  uint32_t lf = from + static_cast<uint32_t>(static_cast<uint64_t>(m.loopStart) * span / 0xFFFF);
  if (lf >= to) lf = to - 1;
  v.from = from;
  v.to = to;
  v.loopMode = m.loop < static_cast<uint8_t>(LoopMode::Count) ? m.loop : 0;
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

void Synth::noteOff(uint8_t track, uint8_t note) {
  for (auto& v : voices_)
    if (v.on && v.track == track && v.note == note && v.env.stage() != Env::Stage::Release && !fmDrum(v))
      v.env.gate(false);
}

void Synth::releaseTrack(uint8_t track) {
  for (auto& v : voices_)
    if (v.on && v.track == track) v.env.gate(false);
}

void Synth::control(Voice& v, int dt) {
  const Instrument& m = p_.instruments[v.instr];
  const TrackRt& r = rt_[v.track];
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
  if (r.arp) {
    // note, +x, +y: three switches per step.
    v.arpT += static_cast<uint32_t>(dt);
    const uint32_t third = static_cast<uint32_t>(stepSamples(v.track) / 3.f);
    const uint32_t k = third ? (v.arpT / third) % 3 : 0;
    pitch += k == 0 ? 0 : (k == 1 ? (r.arp >> 4) : (r.arp & 15));
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
  if (v.fm) {
    controlFm(v, m, pitch, dt);  // sets v.amp too
    return;
  }
  if (v.sample) {
    // Frames per output sample; the root note plays at the sample's own rate.
    const uint8_t root = m.root > 127 ? 127 : m.root;
    float inc = v.smpRate * (1.f / kSynthRate) * exp2f((pitch - root) * (1.f / 12.f));
    inc = clampf(inc, 0.f, 256.f);
    v.inc = inc;
    v.step = static_cast<int64_t>(inc * 4294967296.f);
  } else {
    v.inc = noteHz(pitch) * (1.f / kSynthRate);
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
  v.amp = v.gain * iv * trackVol(v.track) * (1.f / (127.f * 127.f));
}

float Synth::lfo(Voice& v, const Instrument& m, int dt) {
  if (!m.lfoDepth) return 0;
  v.lfoPhase += lfoHz(m.lfoRate) * dt * (1.f / kSynthRate);
  if (v.lfoPhase >= 1.f) {
    v.lfoPhase -= static_cast<int>(v.lfoPhase);
    v.lfoRnd = rnd();
  }
  const float ph = v.lfoPhase;
  float w;
  switch (static_cast<LfoWave>(m.lfoWave)) {
    case LfoWave::Tri: w = 4.f * (ph < 0.5f ? 0.5f - ph : ph - 0.5f) - 1.f; break;
    case LfoWave::Saw: w = 2.f * ph - 1.f; break;
    case LfoWave::Square: w = ph < 0.5f ? 1.f : -1.f; break;
    case LfoWave::Random: w = v.lfoRnd; break;
    default: w = sinf(6.2831853f * ph); break;
  }
  const int d = m.lfoDepth < -64 ? -64 : (m.lfoDepth > 63 ? 63 : m.lfoDepth);
  return w * d * (1.f / 64.f);
}

void Synth::controlFm(Voice& v, const Instrument& m, float pitch, int dt) {
  float mac[kFmMacros];
  for (int k = 0; k < kFmMacros; ++k) mac[k] = (v.lockMask & (1 << k)) ? v.lock[k] : m.macro[k];
  const float l = lfo(v, m, dt);
  float vol = 1;
  if (l != 0) {
    const uint8_t dest = m.lfoDest < static_cast<uint8_t>(LfoDest::Count) ? m.lfoDest : 0;
    if (dest == static_cast<uint8_t>(LfoDest::Pitch)) pitch += 12.f * l;
    else if (dest == static_cast<uint8_t>(LfoDest::Vol)) vol = clampf(1.f + l, 0.f, 2.f);
    else mac[dest - 1] = clampf(mac[dest - 1] + 64.f * l, 0.f, 127.f);
  }
  // fmMachine is pure and costly (tens of us): reuse the last params while the inputs stay
  // within half a macro step and a cent of the ones they were made from.
  bool stale = !v.fpValid || !fmCache_ || fabsf(pitch - v.fpPitch) >= kFmCacheCents;
  for (int k = 0; k < kFmMacros && !stale; ++k) stale = fabsf(mac[k] - v.fpMac[k]) >= kFmCacheMacro;
  if (stale) {
    fmMachine(v.machine, mac, pitch, v.fp);
    ++fmCalls_;
    v.fpValid = true;
    v.fpPitch = pitch;
    for (int k = 0; k < kFmMacros; ++k) v.fpMac[k] = mac[k];
  }
  // Ramp to the next control update: a full period from render(), the rest of it mid-segment
  // (note-on, bend), so a choke ramp ends exactly where the next update starts.
  v.fmv.control(v.fp, dt ? dt : ctlLeft_);
  const uint8_t iv = m.vol > 127 ? 127 : m.vol;
  v.amp = v.gain * iv * trackVol(v.track) * vol * (1.f / (127.f * 127.f));
}

void Synth::renderVoice(Voice& v, float* out, int n) {
  if (v.fm) {
    // Segments end at control boundaries: n <= kControl.
    float tmp[kControl] = {0};
    v.fmv.render(tmp, n, v.amp);
    for (int i = 0; i < n; ++i) out[i] += tmp[i] * v.env.next();
    if (v.fmv.done()) v.env.kill();
    return;
  }
  if (v.sample) {
    renderSample(v, out, n);
    return;
  }
  const Wave w = static_cast<Wave>(v.wave);
  const float inc = v.inc, duty = v.duty, amp = v.amp;
  for (int i = 0; i < n; ++i) out[i] += v.osc.next(w, inc, duty) * v.env.next() * amp;
}

// Sample playback: 32.32 fixed point position, linear interpolation between frames. The loop
// region [loopLo, loopHi) bounds the play direction's edge (for Off it is the whole region).
void Synth::renderSample(Voice& v, float* out, int n) {
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
        while (pos >= hi) pos -= span;
      } else {
        while (pos < lo) pos += span;
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

void Synth::render(int16_t* out) {
  for (auto& x : mix_) x = 0;
  int e = 0;
  for (int pos = 0; pos < kBlock;) {
    if (pos % kControl == 0)
      for (auto& v : voices_)
        if (v.on) control(v, kControl);
    ctlLeft_ = kControl - pos % kControl;
    while (e < nEv_ && ev_[e].off <= pos) apply(ev_[e++]);
    int end = (pos / kControl + 1) * kControl;
    if (e < nEv_ && ev_[e].off < end) end = ev_[e].off;
    for (auto& v : voices_) {
      if (!v.on) continue;
      renderVoice(v, mix_ + pos, end - pos);
      if (v.env.idle()) v.on = false;
    }
    pos = end;
  }
  nEv_ = 0;
  ctlLeft_ = kControl;
  const uint8_t mv = p_.masterVol > kMasterVolMax ? kMasterVolMax : p_.masterVol;
  const float g = mv * (0.25f / 100.f);  // 100 %: headroom for 16 voices; up to 200 % leans on the soft clip
  for (int i = 0; i < kBlock; ++i) out[i] = static_cast<int16_t>(softClip(mix_[i] * g) * 32767.f);
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
