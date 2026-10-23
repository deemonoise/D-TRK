#include "synth.h"
#include "bytebeat_lib.h"
#include "fastmath.h"
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

MT_INLINE float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

constexpr int kLfoDests = static_cast<int>(LfoDest::Count);
constexpr int ld(LfoDest d) { return static_cast<int>(d); }
// 0..127 parameter + LFO offset, rounded and clamped.
MT_HOT inline uint8_t u7(float x) { return static_cast<uint8_t>(clampf(x, 0.f, 127.f) + 0.5f); }

MT_HOT int lfoDepth(int8_t d) { return d < -64 ? -64 : (d > 63 ? 63 : d); }

// A voice still sounds: its note, or once the envelope has ended its filter ringing out (renderCore
// frees it after that: cutting a resonant filter mid-ring clicks).
MT_INLINE bool sounding(const Voice& v) { return v.on && !(v.env.idle() && !(v.fltOn && v.flt.ringing())); }

// Bit i: LFO i has Retrig OFF (the instrument's shared phase).
MT_HOT uint8_t sharedLfos(const Instrument& m) {
  uint8_t r = lfoFree(m.lfoSync) ? 1 : 0;
  for (int i = 1; i < kLfos; ++i)
    if (lfoFree(m.lfo[i - 1].sync)) r |= static_cast<uint8_t>(1u << i);
  return r;
}

// LFO -> LFO mods of an instrument's LFOs cs (with a note's LFO fx locks, if any). Sources: LFOs in srcMask with depth != 0 and a RateJ / DepthJ / RtrgJ
// target; out = their last outputs, wraps = bit i if source i wrapped. RATE sums 4 octaves per unit,
// DEPTH sums into 1 + l (clamped 0..2), RTRG on itself is ignored.
MT_HOT LfoMods lfoMods(const LfoCfg* cs, const float* out, uint8_t wraps, uint8_t srcMask) {
  constexpr int kRate = ld(LfoDest::Rate1), kDepth = ld(LfoDest::Depth1), kRtrg = ld(LfoDest::Rtrg1);
  LfoMods r;
  for (int i = 0; i < kLfos; ++i) {
    if (!(srcMask & (1u << i))) continue;
    const LfoCfg& c = cs[i];
    if (!c.depth || c.dest < kRate) continue;
    const int d = c.dest;
    if (d < kRate + kLfos) r.oct[d - kRate] += 4.f * out[i];
    else if (d >= kDepth && d < kDepth + kLfos) r.depth[d - kDepth] += out[i];
    else if (d >= kRtrg && d < kRtrg + kLfos && d - kRtrg != i && (wraps & (1u << i)))
      r.rtrg |= static_cast<uint8_t>(1u << (d - kRtrg));
  }
  for (float& x : r.depth) x = clampf(x, 0.f, 2.f);
  return r;
}

// Releases a full queue must not drop: note-offs, track start / all off, PERF release / release all
// (a lost one leaves the effect held), stop (a lost one leaves the trance gates running).
bool isOff(const uint8_t* b, uint8_t len) {
  const uint8_t k = b[0] & 0xF0;
  return k == 0x80 || (k == 0x90 && len >= 3 && b[2] == 0) || b[0] == 0xFF || b[0] == 0xFE || b[0] == 0xFD ||
         (b[0] == 0xF6 && len >= 2 && (b[1] == 1 || b[1] == 3));
}

// Soft clip: 1.5x - 0.5x^3 inside [-1, 1], flat outside.
float softClip(float x) {
  if (x >= 1.f) return 1.f;
  if (x <= -1.f) return -1.f;
  return x * (1.5f - 0.5f * x * x);
}

// Pan gains per position 0..127, constant power x sqrt 2 (centre 1 / 1, hard left sqrt 2 / 0).
// Filled once by the first Synth (initPan), outside the audio path.
float panL[128], panR[128];
bool panReady = false;

void initPan() {
  if (panReady) return;
  for (int p = 0; p < 128; ++p) {
    const float th = 0.78539816f * (1.f + (p - 64) / (p < 64 ? 64.f : 63.f));
    panL[p] = 1.41421356f * cosf(th);
    panR[p] = 1.41421356f * sinf(th);
  }
  panL[64] = panR[64] = 1.f;  // exact: centred voices are bit-exact to the mono mix
  panR[0] = 0;
  panL[127] = 0;
  panReady = true;
}

}  // namespace

int eventOffset(uint64_t t, uint64_t blockT) {
  if (t <= blockT) return 0;
  const uint64_t off = ((t - blockT) * kSynthRate + 500000) / 1000000;
  return off < static_cast<uint64_t>(Synth::kBlock) ? static_cast<int>(off) : -1;
}

Synth::Synth(const SynthModel& p) : p_(p) {
  Drive::init();  // the tanh table, outside the audio path
  byteFormulaCount();  // compiles the LOFI formula table, outside the audio path
  initPan();
  for (int8_t& s : instSlot_) s = -1;
  for (int m = 0; m < kInstruments; ++m) cacheLfos(m);
  tuneSeen_.count = 0xFF;  // never a valid table: the first syncTuning builds
  syncTuning();
  reset();
  for (auto& r : instLfoRnd_)
    for (float& x : r) x = rnd();
}

void Synth::reset() {
  for (auto& v : voices_) v = Voice();
  age_ = 0;
  nEv_ = 0;
  load_ = 0;
  cap_ = kVoices;
  capUp_ = 0;
  over_ = 0;
  delay_.clear();
  reverb_.clear();
  comp_.reset();
  perf_.reset();
  perfDs_ = perfRs_ = perfMs_ = 0;
  perfDry_ = 1;
  perfDRet_ = perfRRet_ = perfMRet_ = 0;
  rvbRan_ = false;
  modfx_.reset();
  eq_.reset();
  shimmer_.clear();
  shimRan_ = false;
  for (auto& x : strips_) x = TrackStrip();
  stripDirty_ = 0;
  xfSnap_ = true;  // the crossfader lands where the model has it
  for (float& x : srcPk_) x = 0;
  for (int t = 0; t < kTracks; ++t) {
    folEnv_[t] = 0;
    folMod_[t] = 0;
    folLa_[t] = -1;
    folKey_[t] = 0xFFFF;
  }
  folSrcMask_ = folVoice_ = folVolMask_ = 0;
  for (int t = 0; t < kSynthTracks; ++t) startTrack(static_cast<uint8_t>(t));
}

MT_HOT void Synth::setLoad(float load) {
  load_ += (load - load_) * 0.25f;
  over_ = load > kLoadPanic ? over_ + 1 : 0;
  const bool high = load_ > kLoadHigh;
  if (!high && over_ < kPanicBlocks && load <= kLoadHard) {
    if (load_ < kLoadLow && cap_ < kVoices && ++capUp_ >= kCapUpBlocks) {
      ++cap_;
      capUp_ = 0;
    }
    return;
  }
  capUp_ = 0;
  // Shed one voice per block while over: releasing ones first, the oldest of each. A note that
  // has ended (a filter tail) costs only the filter and cannot be faded without a click.
  int live = 0, victim = -1;
  bool victimRel = false;
  for (int i = 0; i < kVoices; ++i) {
    const Voice& x = voices_[i];
    if (!x.on || x.stolen || x.env.idle()) continue;
    ++live;
    const bool rel = x.env.stage() == Env::Stage::Release;
    if (victim < 0 || (rel && !victimRel) || (rel == victimRel && x.age < voices_[victim].age)) {
      victim = i;
      victimRel = rel;
    }
  }
  if (live <= kMinVoices || victim < 0) return;
  voices_[victim].env.fade(kStealMs);
  voices_[victim].stolen = true;
  if (high) cap_ = live - 1;
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
  r.lfoSel = 0;
  r.lfoLockMask = 0;
  r.lfoRst = 0;
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

MT_HOT uint8_t Synth::morphTrk(uint8_t t, int k, uint8_t base) const {
  const Scene &a = p_.sceneLive[0], &b = p_.sceneLive[1];
  return sceneMix(base, (a.trkMask[t] >> k) & 1u, a.trk[t][k], (b.trkMask[t] >> k) & 1u, b.trk[t][k], xf_,
                  sceneTrkDiscrete(k));
}

MT_HOT uint8_t Synth::morphMst(int k, uint8_t base) const {
  const Scene &a = p_.sceneLive[0], &b = p_.sceneLive[1];
  return sceneMix(base, (a.mstMask >> k) & 1u, a.mst[k], (b.mstMask >> k) & 1u, b.mst[k], xf_, sceneMstDiscrete(k));
}

MT_HOT uint8_t Synth::trackVol(uint8_t track) const {
  const uint8_t v = track < kTracks ? morphT(track, kScnVol, p_.tracks[track].vol) : kPreviewVol;
  return v > 127 ? 127 : v;
}

MT_HOT uint8_t Synth::trackPan(uint8_t track) const {
  const uint8_t v = track < kTracks ? morphT(track, kScnPan, p_.tracks[track].pan) : 64;  // the preview track: centre
  return v > 127 ? 127 : v;
}

static_assert(static_cast<int>(DelayMode::Count) == static_cast<int>(DlyMode::Count) &&
                  static_cast<int>(DelayMode::PingPong) == static_cast<int>(DlyMode::PingPong) &&
                  static_cast<int>(DelayMode::Tape) == static_cast<int>(DlyMode::Tape),
              "DlyMode follows DelayMode");
static_assert(static_cast<int>(ModFxType::Count) == static_cast<int>(ModType::Count) &&
                  static_cast<int>(ModFxType::Chorus) == static_cast<int>(ModType::Chorus) &&
                  static_cast<int>(ModFxType::Phaser) == static_cast<int>(ModType::Phaser),
              "ModType follows ModFxType");
static_assert(PitchShift::kIntervals == static_cast<int>(ShimInt::Count) && PitchShift::Fifth == 1 &&
                  PitchShift::TwoOct == 2,
              "ShimInt follows PitchShift::Interval");
static_assert(ModelDrive::kModels == static_cast<int>(DriveModel::Count) &&
                  ModelDrive::kFold == static_cast<int>(DriveModel::Fold) &&
                  ModelDrive::kAsym == static_cast<int>(DriveModel::Asym),
              "ModelDrive follows DriveModel");

MT_HOT const TrackCfg* Synth::insertCfg(uint8_t track) const {
  if (track < kTracks) return &p_.tracks[track];
  return previewIns_ < kTracks ? &p_.tracks[previewIns_] : nullptr;
}

// The trance gate's slopes from ATK / REL (ad = gateAtk | gateRel << 8), per change only.
void Synth::gateSlopes(TrackStrip::Gate& g, uint16_t ad) {
  constexpr float kPerMs = kSynthRate / 1000.f;
  g.ad = ad;
  g.up = 1.f / (gateMs(static_cast<uint8_t>(ad & 0xFF)) * kPerMs);
  g.down = 1.f / (gateMs(static_cast<uint8_t>(ad >> 8)) * kPerMs);
}

// A strip whose track starts sounding: the gains / sends at their targets, crush, drive and gate
// restart.
void Synth::snapStrip(TrackStrip& s) {
  s.gl = s.tgl;
  s.gr = s.tgr;
  s.dgl = s.dgr = 0;
  s.send = s.tsend;
  s.rsend = s.trsend;
  s.msend = s.tmsend;
  s.dsend = s.drsend = s.dmsend = 0;
  s.panSet = true;
  s.crush.reset();
  s.drive.reset();
  s.drvFade = 1;
  s.gate.g = gateTarget(s.gate, static_cast<float>(gatePos_) / s.gate.steps);
}

// The gains (pan x fader) ramp linearly to the target, landing after len samples (renderCore snaps
// them there at the control boundary). Equal target: steps exactly 0, a centred track at full
// fader keeps 1.0. A strip without panSet (the track was silent) snaps and restarts its crush.
MT_HOT void Synth::refreshStrip(uint8_t track, int nv, int len) {
  TrackStrip& s = strips_[track];
  if (nv < 0) {
    s.panSet = false;
    return;
  }
  const Voice& v = voices_[nv];
  float f = trackVol(track) * (1.f / 127.f);
  if (folVolMask_ & (1u << track)) f *= clampf(1.f + folMod_[track], 0.f, 2.f);  // follower on Vol
  s.fader = f;
  s.tsend = v.send * f;
  s.trsend = v.rsend * f;
  s.tmsend = v.msend * f;
  if (s.panSet && s.drive.differs(v.drive, v.dmodel)) {  // older voices sound on: crossfade
    s.drvOld = s.drive;
    s.drvFade = 0;
  }
  s.drive.set(v.drive, v.dmodel);
  // Crush steps: its output is a quantiser's staircase already (a bits / rate move changes it by
  // at most one step of the coarser grid); a crossfade would double its cost for nothing audible.
  s.crush.set(v.bits, v.srr);
  // Trance gate: depth from the voice (GTE lock), the rest from the track's insert settings.
  TrackStrip::Gate& g = s.gate;
  const TrackCfg* ic = insertCfg(track);
  g.on = ic && v.gdepth > 0;
  if (g.on) {
    g.floor = static_cast<float>(127 - v.gdepth) * (1.f / 127.f);  // depth 127: exactly 0
    g.mask = ic->gateMask;
    g.steps = gateSixteenths(ic->gateRate);
    g.len = (ic->gateLen < 1 ? 1 : (ic->gateLen > 100 ? 100 : ic->gateLen)) * 0.01f;
    const uint16_t ad = static_cast<uint16_t>(ic->gateAtk | ic->gateRel << 8);
    if (ad != g.ad) gateSlopes(g, ad);  // only when ATK / REL change (flash)
  }
  const float l = panL[v.pan & 127] * f, r = panR[v.pan & 127] * f;
  s.tgl = l;
  s.tgr = r;
  if (!s.panSet) {
    snapStrip(s);  // the track starts sounding (flash)
    return;
  }
  const float k = len == 32 ? 1.f / 32 : 1.f / static_cast<float>(len < 1 ? 1 : len);  // kControl: no __divsf3
  s.dgl = (l - s.gl) * k;
  s.dgr = (r - s.gr) * k;
  s.dsend = (s.tsend - s.send) * k;
  s.drsend = (s.trsend - s.rsend) * k;
  s.dmsend = (s.tmsend - s.msend) * k;
}

void Synth::apply(const Ev& e) {
  const uint8_t k = e.b[0] & 0xF0;
  stripDirty_ |= 1u << e.track;  // locks, pan, a new newest voice: renderCore refreshes the strip
  if (e.b[0] == 0xFE) startTrack(e.track);
  else if (e.b[0] == 0xF7) {
    if (e.len >= 2 && e.track == kPreviewTrack) previewIns_ = e.b[1];
  }
  else if (e.b[0] == 0xFA) transportStart();
  else if (e.b[0] == 0xFD) running_ = false;
  else if (e.b[0] == 0xFC) paused_ = true;
  else if (e.b[0] == 0xFB) paused_ = false;
  else if (e.b[0] == 0xFF) releaseTrack(e.track);
  else if (e.b[0] == 0xF5) {
    if (e.len >= 3) fx(e.track, e.b[1], e.b[2]);
  }
  else if (e.b[0] == 0xF6) {
    if (e.len >= 3 && e.b[1] == 4) perf_.stepAt(e.b[2], e.off);  // step clock, at its sample
    else if (e.len >= 3 && e.b[1] == 5) gatePos_ = e.b[2] & 63;      // song clock (segments split here)
    else if (e.len >= 3) perfMsg(e.b[1], e.b[2]);
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

MT_HOT float Synth::tickSamples() const {
  const uint16_t b = p_.bpm;
  const int bpm = b < 20 ? 20 : (b > 300 ? 300 : b);
  return kSynthRate * 0.625f / bpm;  // 625000 / bpm us
}

// Project::dlyTime sixteenths (24 ticks each); Delay clamps it to its line.
MT_HOT uint32_t Synth::delaySamples() const {
  const uint8_t dt = morphM(kScnDTime, p_.dlyTime);
  const uint8_t t = dt < 1 ? 1 : (dt > kDlyTimeMax ? kDlyTimeMax : dt);
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
    r.lfoSel = 0;
    r.lfoLockMask = 0;
    r.lfoRst = 0;
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
    case Fx::SRR:
    case Fx::PAN:
    case Fx::DTY:
    case Fx::GTE:
    case Fx::MOD: {
      // Lock: this step's note-ons, or the track's sounding voices. Macros: FM / DRUM / SYNTH voices only.
      const int k = cmd == static_cast<uint8_t>(Fx::DLY)   ? kLockDly
                    : cmd == static_cast<uint8_t>(Fx::DRV) ? kLockDrv
                    : cmd == static_cast<uint8_t>(Fx::RVB) ? kLockRvb
                    : cmd == static_cast<uint8_t>(Fx::BIT) ? kLockBit
                    : cmd == static_cast<uint8_t>(Fx::SRR) ? kLockSrr
                    : cmd == static_cast<uint8_t>(Fx::PAN) ? kLockPan
                    : cmd == static_cast<uint8_t>(Fx::DTY) ? kLockDty
                    : cmd == static_cast<uint8_t>(Fx::GTE) ? kLockGte
                    : cmd == static_cast<uint8_t>(Fx::MOD) ? kLockMod
                                                           : cmd - static_cast<uint8_t>(Fx::DCY);
      const bool macro = k < kFmMacros;
      const uint8_t lv = val > 127 ? 127 : val;
      if (now) {
        for (auto& x : voices_)
          if (x.on && x.track == track && (!macro || x.fm || x.drum || x.syn || x.lofi)) {
            x.lock[k] = lv;
            x.lockMask |= static_cast<uint16_t>(1u << k);
            // PAN ramps from here to the control boundary (apply() marks the strip for a refresh;
            // the others wait for the next update).
            if (k == kLockPan) control(x, 0);
          }
      } else {
        r.lock[k] = lv;
        r.lockMask |= static_cast<uint16_t>(1u << k);
      }
      break;
    }
    case Fx::LFO:
    case Fx::LFD:
    case Fx::LFS:
    case Fx::LFW:
    case Fx::LFT:
    case Fx::LFR: lfoFx(track, static_cast<Fx>(cmd), val, now); break;
    default: break;
  }
}

// LFO fx (see Fx::LFO): the selection, locks of the selected LFO, its phase restart.
void Synth::lfoFx(uint8_t track, Fx f, uint8_t val, bool now) {
  TrackRt& r = rt_[track];
  if (f == Fx::LFO) {
    r.lfoSel = static_cast<uint8_t>(val >= 1 && val <= kLfos ? val - 1 : 0);
    return;
  }
  const int i = r.lfoSel;
  if (f == Fx::LFR) {
    const float ph = val * (1.f / 256.f);
    const uint8_t mi = trackInstr(track);
    if (mi < kInstruments && lfoFree(lfoAt(inst(mi), i).sync)) {  // Retrig OFF: the instrument's, at once
      instLfoPhase_[mi][i] = ph;
      instLfoRnd_[mi][i] = rnd();
    }
    if (now) {
      for (auto& x : voices_)
        if (x.on && x.track == track) {
          x.lfoPhase[i] = ph;
          x.lfoRnd[i] = rnd();
        }
    } else {
      r.lfoRst |= static_cast<uint8_t>(1u << i);
      r.lfoRstPh[i] = val;
    }
    return;
  }
  const int k = static_cast<int>(f) - static_cast<int>(Fx::LFD);  // LfoLock
  auto set = [k, val](LfoCfg& c) {
    if (k == kLfoLkDepth) {
      const int d = fxSigned(val);
      c.depth = static_cast<int8_t>(d < -64 ? -64 : (d > 63 ? 63 : d));
    } else if (k == kLfoLkRate) {
      c.rate = val > 127 ? 127 : val;
    } else if (k == kLfoLkWave) {
      c.wave = val < static_cast<uint8_t>(LfoWave::Count) ? val : 0;
    } else {
      c.dest = val < static_cast<uint8_t>(LfoDest::Count) ? val : 0;
    }
  };
  const uint16_t bit = static_cast<uint16_t>(1u << (i * kLfoLocks + k));
  if (now) {
    for (auto& x : voices_)
      if (x.on && x.track == track) {
        set(x.lfoLock[i]);
        x.lfoLockMask |= bit;
      }
  } else {
    set(r.lfoLock[i]);
    r.lfoLockMask |= bit;
  }
}

uint8_t Synth::machineOf(const Instrument& m) {
  const uint8_t n = m.type == InstrType::Drum ? static_cast<uint8_t>(DrumMachine::Count)
                    : m.type == InstrType::Lofi ? static_cast<uint8_t>(LofiMachine::Count)
                                               : static_cast<uint8_t>(FmMachine::Count);
  return m.machine < n ? m.machine : 0;
}

// One-shot drum voice (DRUM, FM drum machines): ignores note-offs, always retriggers (choke).
bool Synth::oneShot(const Voice& v) { return v.drum || (v.fm && !fmGated(v.machine)); }

// -1..1, LCG.
MT_HOT float Synth::rnd() {
  rng_ = rng_ * 1664525u + 1013904223u;
  return static_cast<int32_t>(rng_) * (1.f / 2147483648.f);
}

// KIT sampler lane release: envTimeMs(32) ~ 10 ms.
static constexpr uint8_t kLaneRelease = 32;

// KIT sampler lane: the lane's sample at its own pitch (root = lane note) + pitch, one-shot or
// decay. out holds Instrument() defaults in every other field: start 0, end 0xFFFF, loop Off, no
// slices, forward.
MT_HOT static void buildLane(Instrument& out, const Instrument& k, const KitLane& ln) {
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
  out.lfoDepth = 0;  // = lfoRef(out, i).depth = 0 for every LFO, without the flash call (per control)
  for (auto& l : out.lfo) l.depth = 0;
  out.send = k.send;
  out.rsend = k.rsend;
}

MT_HOT void Synth::cacheInstrument(uint8_t i) {
  if (i >= kInstruments) return;
  int s = instSlot_[i];
  if (s < 0) {
    if (nInstSlots_ == kInstSlots) return;
    s = nInstSlots_++;
    instSlot_[i] = static_cast<int8_t>(s);
    slotInst_[s] = i;
  }
  instCopy_[s] = p_.instruments[i];
  cacheLfos(i);
}

MT_HOT void Synth::cacheLfos(int m) {
  const Instrument& x = inst(static_cast<uint8_t>(m));
  for (int i = 0; i < kLfos; ++i) instLfoCfg_[m][i] = lfoAt(x, i);
  instShared_[m] = sharedLfos(x);
}

MT_HOT void Synth::cacheInstruments() {
  for (int s = 0; s < nInstSlots_; ++s) instSlot_[slotInst_[s]] = -1;
  nInstSlots_ = 0;
  for (const auto& v : voices_)
    if (v.on && instSlot_[v.instr < kInstruments ? v.instr : 0] < 0) cacheInstrument(v.instr);
  mark(kProfCache);
  for (int k = 0; k < kLfoScan; ++k) {
    cacheLfos(lfoScan_);
    lfoScan_ = static_cast<uint8_t>((lfoScan_ + 1) % kInstruments);
  }
}

const Instrument& Synth::laneOf(const Voice& v) const {
  const Instrument& m = inst(v.instr);
  buildLane(laneScratch_, m, m.kit[v.laneIdx]);
  return laneScratch_;
}

void Synth::noteOn(uint8_t track, uint8_t note, uint8_t vel) {
  uint8_t ii = trackInstr(track);
  bool kitLane = false;  // a KIT lane: mono per lane (choke on the same note), poly across lanes
  int laneIdx = 0;
  Instrument scratch;    // sampler lane: built from the lane (instrOf rebuilds it while the voice plays)
  cacheInstrument(ii);   // fresh: the setup below and the voice's control updates read the same
  const Instrument* mp = &inst(ii);
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
      cacheInstrument(ln.instr);
      if (inst(ln.instr).type == InstrType::Kit) return;  // no kit in a kit
      ii = ln.instr;
      mp = &inst(ii);
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
  const bool lofiT = m.type == InstrType::Lofi;
  const uint8_t machine = machineOf(m);
  const bool heavy = fm || drumT || synWtT || (lofiT && machine == static_cast<uint8_t>(LofiMachine::Byte));
  const bool drum = drumT || (fm && !fmGated(machine));  // one-shot, choke
  const bool tone = fm && machine == static_cast<uint8_t>(FmMachine::Tone);
  // FM: only TONE may be poly; drums and CHORD take the track's voice. DRUM is always mono.
  // A KIT lane is mono through its own note (below), so the track is poly across lanes.
  const bool mono = kitLane ? false : fm ? (tone ? m.mono : true) : (drumT ? true : m.mono);
  // Tuning: melodic voices only (not drums, FM drum machines, KIT lanes, NOTE slices). Their pitches are
  // keys without fine: control() maps them and adds fine in cents.
  const bool tuned = tune_.on() && !kitLane && !drum && !slicePitch;
  const float pitch = tuned ? static_cast<float>(note + static_cast<int>(clampf(m.transpose, -24, 24)))
                            : (slicePitch ? m.root : note) + clampf(m.transpose, -24, 24) + clampf(m.fine, -50, 50) * 0.01f;
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
    vi = allocVoice(voices_, track, mono, age_, legato, heavy, kitLane ? kKitLanes : kPolyPerTrack, cap_);
  }
  Voice& v = voices_[vi];
  v.stolen = false;  // SLD may take a fading voice back
  const bool keepFm = legato && v.fm;  // a sounding FM voice: choke without a click
  const bool prevDrum = legato && oneShot(v);  // its one-shot sound and gate env end here
  const bool wasDrum = legato && v.drum;       // a sounding DRUM voice: choke without a click
  const bool wasLofi = legato && v.lofi;       // a sounding LOFI voice: BYTE continues its output
  // A releasing (or finished, not yet freed) voice is reused, but that is no legato: the note
  // restarts the sample and gets no glide. SLD still slides from it.
  const bool wasReleasing = v.env.stage() == Env::Stage::Release || v.env.idle();
  // A drum always retriggers (choke), and so does a note following one.
  // A KIT lane re-hit restarts its sound.
  const bool overlap = legato && !wasReleasing && !drum && !prevDrum && !kitLane;
  // Legato on the same sample keeps playing from where it is.
  const bool restartSmp = sample && (!overlap || !v.sample || v.smp != smp || v.slice != slice);
  v.note = note;
  v.tuned = tuned;
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
  v.lofi = lofiT;
  v.machine = machine;
  // Filter locks go to every type, macro locks to FM / DRUM / SYNTH only.
  constexpr uint8_t kMacroBits = (1 << kFmMacros) - 1;
  v.lockMask = (heavy || synT || lofiT) ? r.lockMask : (r.lockMask & ~kMacroBits);
  for (int k = 0; k < kLocks; ++k) v.lock[k] = r.lock[k];
  // A fresh note on a track with no other gated voice starts where it is panned (the strip snaps);
  // otherwise the strip ramps to a new pan (no click for the voices still sounding).
  if (!overlap) {
    bool other = false;
    for (int x = 0; x < kVoices && !other; ++x)
      other = x != vi && voices_[x].track == track && sounding(voices_[x]);
    if (!other) strips_[track].panSet = false;
  }
  v.lfoLockMask = r.lfoLockMask;
  for (int i = 0; i < kLfos; ++i) v.lfoLock[i] = r.lfoLock[i];
  if (fm || drumT || synT || lofiT)
    v.useEngine(fm ? EngineKind::Fm : drumT ? EngineKind::Drum : synT ? EngineKind::Syn : EngineKind::Lofi);
  if (fm) {
    v.fpValid = false;
    if (!overlap || !keepFm) {  // legato from a CHIP / SAMPLE voice starts the FM voice afresh
      // A held machine after a drum starts afresh too: its level is 1 at once, ramping the
      // drum's operators into it would jump from the drum's decayed level.
      v.fmv().trigger(keepFm && (drum || !prevDrum));
      resetLfos(v);
    }
  }
  for (int i = 0; i < kLfos; ++i)  // LFR: this step's note-ons start there, after the restart above
    if (r.lfoRst & (1u << i)) {
      v.lfoPhase[i] = r.lfoRstPh[i] * (1.f / 256.f);
      v.lfoRnd[i] = rnd();
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
  // Legato keeps t / the oscillator running; from a CHIP / SAMPLE voice it starts afresh. age_: drift seed
  if (lofiT && (!overlap || !wasLofi)) v.lv().trigger(age_, wasLofi);
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
  if (!legato) v.flt.reset();
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
  // A tuned voice: keys (slide, ARP) map through the tuning, then fine, bend, VIB, LFO in semitones.
  float pitch = v.tuned ? v.pitch : v.pitch + r.bend;
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
  if (v.tuned) pitch = tune_.map(pitch) + clampf(m.fine, -50, 50) * 0.01f + r.bend;
  const uint8_t vx = r.vib >> 4, vy = r.vib & 15;
  if (vx && vy) {
    // Sine LFO, x * 0.5 Hz, depth y / 15 * 2 semitones.
    v.vibPhase += vx * 0.5f * dt * (1.f / kSynthRate);
    v.vibPhase -= static_cast<int>(v.vibPhase);
    pitch += vy * (2.f / 15.f) * phaseSine(v.vibPhase);
  } else {
    v.vibPhase = 0;
  }
  // LFOs 1..4 summed per target (la: -1..1 x depth / 64 each; VOL multiplies). Macro targets go to
  // controlFm / controlDrum / controlSyn (lm: macro offsets), SUB / NOISE / SEMI2 to controlSyn.
  // Targets invalid for the type are summed and never read, so are L1..L4 RATE / DEPTH / RTRG: they
  // act through mods, LFO -> LFO from the last update's outputs and wraps (shared LFOs' wraps: advanceInstLfos). A
  // dt = 0 update (note-on, fx) neither restarts nor consumes the wraps.
  const uint8_t shared = sharedLfos(m);
  const uint8_t wraps = dt ? static_cast<uint8_t>((v.lfoWrap & ~shared) | (instLfoWrap_[v.instr] & shared)) : 0;
  LfoCfg cs[kLfos];  // the instrument's LFOs with this note's LFO fx locks
  for (int i = 0; i < kLfos; ++i) cs[i] = lfoCfgOf(&v, m, i);
  LfoMods mods = lfoMods(cs, v.lfoOut, wraps, 0xF);
  // Envelope follower of the voice's track (TrackCfg::fol*): L1..L4 RATE / DEPTH into the mods as an
  // LFO -> LFO source, the other targets summed with the LFOs below (Vol: the strip's).
  int fla = -1;
  if (folVoice_ & (1u << v.track)) {
    const int d = folLa_[v.track];
    const float f = folMod_[v.track];
    if (d >= ld(LfoDest::Rate1) && d < ld(LfoDest::Rate1) + kLfos) mods.oct[d - ld(LfoDest::Rate1)] += 4.f * f;
    else if (d >= ld(LfoDest::Depth1) && d < ld(LfoDest::Depth1) + kLfos)
      mods.depth[d - ld(LfoDest::Depth1)] = clampf(mods.depth[d - ld(LfoDest::Depth1)] + f, 0.f, 2.f);
    else fla = d;
  }
  if (dt) v.lfoWrap = 0;  // collected anew by this update's advances
  float la[kLfoDests] = {};
  float lfoVol = 1;
  for (int i = 0; i < kLfos; ++i) {
    const LfoCfg& c = cs[i];
    const float l = lfo(v, c, i, dt, mods);
    if (l == 0) continue;
    const int dest = c.dest < kLfoDests ? c.dest : 0;
    if (dest == ld(LfoDest::Vol)) lfoVol *= clampf(1.f + l, 0.f, 2.f);
    else la[dest] += l;
  }
  if (fla >= 0) la[fla] += folMod_[v.track];
  pitch += 12.f * la[ld(LfoDest::Pitch)] + la[ld(LfoDest::Fine)];
  float lm[kFmMacros];
  for (int k = 0; k < kFmMacros; ++k) lm[k] = 64.f * la[ld(LfoDest::Dec) + k];
  // The strip's values as this voice asks for them (see Voice::send .. srr). Sends: the track's
  // instrument; a KIT's lanes all take the KIT's. Bases through the scene morph (morphT), the locks above.
  const Instrument& ti = inst(trackInstr(v.track));
  const Instrument& sm = ti.type == InstrType::Kit ? ti : m;
  const uint8_t tr = v.track;
  const uint8_t snd = (v.lockMask & (1u << kLockDly)) ? v.lock[kLockDly] : morphT(tr, kScnDly, sm.send);
  v.send = u7(snd + 64.f * la[ld(LfoDest::Dly)]) * (1.f / 127.f);
  const uint8_t rs = (v.lockMask & (1u << kLockRvb)) ? v.lock[kLockRvb] : morphT(tr, kScnRvb, sm.rsend);
  v.rsend = u7(rs + 64.f * la[ld(LfoDest::Rvb)]) * (1.f / 127.f);
  const uint8_t pb = (v.lockMask & (1u << kLockPan)) ? v.lock[kLockPan] : trackPan(v.track);
  v.pan = u7(pb + 64.f * la[ld(LfoDest::Pan)]);
  const TrackCfg* ic = insertCfg(v.track);
  const uint8_t drv = (v.lockMask & (1u << kLockDrv)) ? v.lock[kLockDrv] : (ic ? morphT(tr, kScnDrv, ic->drive) : 0);
  v.drive = u7(drv + 64.f * la[ld(LfoDest::Drive)]);
  const uint8_t bits = (v.lockMask & (1u << kLockBit)) ? v.lock[kLockBit] : (ic ? morphT(tr, kScnBit, ic->crushBits) : 0);
  const uint8_t srr = (v.lockMask & (1u << kLockSrr)) ? v.lock[kLockSrr] : (ic ? morphT(tr, kScnSrr, ic->crushRate) : 0);
  v.bits = u7(bits + 64.f * la[ld(LfoDest::Bit)]);
  v.srr = u7(srr + 64.f * la[ld(LfoDest::Srr)]);
  v.dmodel = (v.lockMask & (1u << kLockDty)) ? v.lock[kLockDty] : (ic ? morphT(tr, kScnDty, ic->driveModel) : 0);
  const uint8_t gd = (v.lockMask & (1u << kLockGte)) ? v.lock[kLockGte] : (ic ? morphT(tr, kScnGte, ic->gateDepth) : 0);
  v.gdepth = gd > 127 ? 127 : gd;
  const uint8_t ms = (v.lockMask & (1u << kLockMod)) ? v.lock[kLockMod] : (ic ? morphT(tr, kScnMod, ic->modSend) : 0);
  v.msend = u7(ms + 64.f * la[ld(LfoDest::ModSend)]) * (1.f / 127.f);
  if (dt && clock_) {  // the profile: render's updates only (note-on / fx updates stay in events)
    ++profN_[kProfCtlMod];
    mark(kProfCtlMod);
  }
  controlFilter(v, m, pitch, la);
  if (dt) mark(kProfCtlFlt);
  if (v.fm) {
    controlFm(v, m, pitch, dt, lm, lfoVol);  // sets v.amp too
    if (dt) mark(kProfCtlEng);
    return;
  }
  if (v.drum) {
    controlDrum(v, m, pitch, dt, lm, lfoVol);  // sets v.amp too
    if (dt) mark(kProfCtlEng);
    return;
  }
  if (v.syn) {
    controlSyn(v, m, pitch, dt, lm, lfoVol, la);  // sets v.amp too
    if (dt) mark(kProfCtlEng);
    return;
  }
  if (v.lofi) {
    controlLofi(v, m, pitch, dt, lm, lfoVol);  // sets v.amp too
    if (dt) mark(kProfCtlEng);
    return;
  }
  if (v.sample) {
    // Frames per output sample; the root note plays at the sample's own rate.
    const uint8_t root = m.root > 127 ? 127 : m.root;
    const float semis = pitch - root;
    if (semis != v.smpKey) {
      v.smpKey = semis;
      v.smpVal = fastExp2(semis * (1.f / 12.f));
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
  if (la[ld(LfoDest::Duty)] != 0) duty = clampf(duty + 0.49f * la[ld(LfoDest::Duty)], 0.01f, 0.99f);
  v.duty = duty;
  const uint8_t iv = m.vol > 127 ? 127 : m.vol;
  v.amp = v.gain * iv * lfoVol * (1.f / 127.f);
  if (dt) mark(kProfCtlEng);
}

// Filter at control rate: off costs nothing per sample (renderVoice skips it).
MT_HOT void Synth::controlFilter(Voice& v, const Instrument& m, float pitch, const float* la) {
  const uint8_t mode = m.fltMode < static_cast<uint8_t>(FltMode::Count) ? m.fltMode : 0;
  const bool was = v.fltOn;
  v.fltOn = mode != static_cast<uint8_t>(FltMode::Off);
  if (!v.fltOn) return;
  if (!was) v.flt.reset();  // turned on mid-note (UI, legato from an unfiltered instrument): no stale state
  const uint8_t mc = morphT(v.track, kScnCut, m.cutoff), mr = morphT(v.track, kScnRes, m.reso);
  const float cut = (v.lockMask & (1u << kLockFlt)) ? v.lock[kLockFlt] : (mc > 127 ? 127 : mc);
  float res = (v.lockMask & (1u << kLockRes)) ? v.lock[kLockRes] : (mr > 127 ? 127 : mr);
  if (la[ld(LfoDest::Reso)] != 0) res = clampf(res + 64.f * la[ld(LfoDest::Reso)], 0.f, 127.f);
  if (res != v.fltRes) {
    v.fltRes = res;
    v.fltQ = resoQ(res);
  }
  // Octaves above 20 Hz (as cutoffHz): cutoff, envelope, key tracking (from C4).
  float oct = clampf(cut + 64.f * la[ld(LfoDest::Cutoff)], 0.f, 127.f) * (9.451211f / 127.f);  // log2(700)
  const float fenv = clampf(m.fenv + 64.f * la[ld(LfoDest::Fenv)], -64.f, 63.f);
  if (fenv != 0 && !v.fenvDone) {
    const float e = filterEnv(v.fenvT, m.fAtk, m.fDec);
    // The -60 dB tail moves the cutoff < 0.01 octave: drop it (not the start of a slow attack).
    if (e < 1e-3f && v.fenvT > envTimeMs(m.fAtk) * (kSynthRate / 1000.f)) v.fenvDone = true;
    oct += fenv * (6.f / 64.f) * e;
  }
  oct += (m.keytrack > 127 ? 127 : m.keytrack) * (1.f / 127.f) * (pitch - 60.f) * (1.f / 12.f);
  // Velocity: +-6 octaves at full depth from velocity 64 (no change) to 0 / 127.
  if (m.velCut) oct += clampf(m.velCut, -64, 63) * (1.f / 64.f) * ((static_cast<int>(v.vel) - 64) * (1.f / 64.f)) * 6.f;
  if (oct != v.octKey) {
    v.octKey = oct;
    v.octHz = 20.f * fastExp2(oct);
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
    v.lfoOut[i] = 0;
  }
  v.lfoWrap = 0;
}

// LFO rate: free (lfoHz) or a tempo division (lfoSyncHz at the project's BPM).
MT_HOT float Synth::lfoRateHz(const LfoCfg& c) const {
  return lfoTempo(c.sync) ? lfoSyncHz(c.rate, p_.bpm) : lfoHz(c.rate);
}

namespace {
// LFO wave at phase ph (0..1), -1..1; rnd: the Random wave's value of this cycle.
MT_HOT float lfoShape(uint8_t wave, float ph, float rnd) {
  switch (static_cast<LfoWave>(wave)) {
    case LfoWave::Tri: return 4.f * (ph < 0.5f ? 0.5f - ph : ph - 0.5f) - 1.f;
    case LfoWave::Saw: return 2.f * ph - 1.f;
    case LfoWave::Square: return ph < 0.5f ? 1.f : -1.f;
    case LfoWave::Random: return rnd;
    default: return phaseSine(ph);
  }
}
}  // namespace

// LFO i of a voice, restarted at note-on; with Retrig OFF the instrument's shared phase
// (advanceInstLfos) instead. mods: LFO -> LFO (RATE / RTRG of a per-voice LFO, DEPTH of either).
// -1..1 x depth / 64, kept in v.lfoOut as a source for the next update.
MT_HOT float Synth::lfo(Voice& v, const LfoCfg& c, int i, int dt, const LfoMods& mods) {
  if (!c.depth) return v.lfoOut[i] = 0;
  float w;
  if (lfoFree(c.sync)) {
    w = lfoShape(c.wave, instLfoPhase_[v.instr][i], instLfoRnd_[v.instr][i]);
  } else {
    float& ph = v.lfoPhase[i];
    if (mods.rtrg & (1u << i)) {
      ph = 0;
      v.lfoRnd[i] = rnd();
    }
    float hz = lfoRateHz(c);
    if (mods.oct[i] != 0) hz *= fastExp2(mods.oct[i]);
    ph += hz * dt * (1.f / kSynthRate);
    if (ph >= 1.f) {
      ph -= static_cast<int>(ph);
      v.lfoRnd[i] = rnd();
      v.lfoWrap |= static_cast<uint8_t>(1u << i);
    }
    w = lfoShape(c.wave, ph, v.lfoRnd[i]);
  }
  return v.lfoOut[i] = w * lfoDepth(c.depth) * (1.f / 64.f) * mods.depth[i];
}

float Synth::lfoPhase(int v, int i) const {
  const Voice& x = voices_[v];
  return lfoFree(lfoAt(instrOf(x), i).sync) ? instLfoPhase_[x.instr][i] : x.lfoPhase[i];
}

// Retrig OFF LFOs of every instrument, playing or not (FREE keeps running, TEMPO stays on the
// transport's grid and holds while it is paused), once per control update. LFO -> LFO: RATE / RTRG /
// DEPTH only from the instrument's shared LFOs, from their last outputs and wraps (instLfoOut_ /
// instLfoWrap_, the sources of the voices' targets too).
MT_HOT void Synth::advanceInstLfos() {
  constexpr float kPerHz = static_cast<float>(kControl) / kSynthRate;
  for (int m = 0; m < kInstruments; ++m) {
    const uint8_t shared = instShared_[m];
    if (!shared) {
      instLfoWrap_[m] = 0;
      for (float& o : instLfoOut_[m]) o = 0;  // no stale source when one is set to Retrig OFF again
      continue;
    }
    const LfoCfg* cs = instLfoCfg_[m];
    const LfoMods mods = lfoMods(cs, instLfoOut_[m], instLfoWrap_[m], shared);
    uint8_t wrap = 0;
    for (int i = 0; i < kLfos; ++i) {
      if (!(shared & (1u << i))) {
        instLfoOut_[m][i] = 0;
        continue;
      }
      const LfoCfg& c = cs[i];
      float& ph = instLfoPhase_[m][i];
      if (!(paused_ && lfoTempo(c.sync))) {  // a paused TEMPO one holds (its output too)
        if (mods.rtrg & (1u << i)) {
          ph = 0;
          instLfoRnd_[m][i] = rnd();
        }
        float hz = lfoRateHz(c);
        if (mods.oct[i] != 0) hz *= fastExp2(mods.oct[i]);
        ph += hz * kPerHz;
        if (ph >= 1.f) {
          ph -= static_cast<int>(ph);
          instLfoRnd_[m][i] = rnd();
          wrap |= static_cast<uint8_t>(1u << i);
        }
      }
      // Read only as an LFO -> LFO source: the wave (sinf) only for an L1..L4 target.
      instLfoOut_[m][i] = c.depth && c.dest >= ld(LfoDest::Rate1)
                              ? lfoShape(c.wave, ph, instLfoRnd_[m][i]) * lfoDepth(c.depth) * (1.f / 64.f) * mods.depth[i]
                              : 0;
    }
    instLfoWrap_[m] = wrap;
  }
}

// LFO i of an instrument with v's LFO fx locks over it (v: nullptr = none).
MT_HOT LfoCfg Synth::lfoCfgOf(const Voice* v, const Instrument& m, int i) {
  LfoCfg c = lfoAt(m, i);
  const unsigned b = v ? (v->lfoLockMask >> (i * kLfoLocks)) & ((1u << kLfoLocks) - 1) : 0;
  if (!b) return c;
  const LfoCfg& l = v->lfoLock[i];
  if (b & (1u << kLfoLkDepth)) c.depth = l.depth;
  if (b & (1u << kLfoLkRate)) c.rate = l.rate;
  if (b & (1u << kLfoLkWave)) c.wave = l.wave;
  if (b & (1u << kLfoLkDest)) c.dest = l.dest;
  if (lfoTempo(c.sync) && c.rate >= kLfoSyncSteps) c.rate = kLfoSyncSteps - 1;
  return c;
}

// 0xFA: TEMPO LFOs with Retrig OFF restart with the transport (FREE ones keep running).
void Synth::transportStart() {
  paused_ = false;
  running_ = true;
  gatePos_ = 0;
  perf_.transportStart();
  for (int m = 0; m < kInstruments; ++m)
    for (int i = 0; i < kLfos; ++i)
      if (const uint8_t sync = lfoAt(p_.instruments[m], i).sync; lfoTempo(sync) && lfoFree(sync)) {
        instLfoPhase_[m][i] = 0;
        instLfoRnd_[m][i] = rnd();
        instLfoOut_[m][i] = 0;  // no LFO -> LFO from the old run (a restart is not a wrap)
      }
}

// 0xF6 op val: PERF on the master (Synth's header). An effect out of range is ignored.
void Synth::perfMsg(uint8_t op, uint8_t val) {
  const PerfFx f = static_cast<PerfFx>(val);
  const bool ok = f != PerfFx::None && f < PerfFx::Count;
  if (op == 0 && ok) perf_.press(f);
  else if (op == 1 && ok) perf_.release(f);
  else if (op == 2) perf_.turn(static_cast<int8_t>(val));
  else if (op == 3) perf_.releaseAll();
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
  v.amp = v.gain * iv * vol * (1.f / 127.f);
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
  v.amp = v.gain * iv * vol * (1.f / 127.f);
}

// SYNTH: macros SHP1, SHP2, MIX, DET, SENV (env -> SHAPE depth, bipolar around 64); la: control()'s LFO
// sums (SUB, NOISE, SEMI2 here).
MT_HOT void Synth::controlSyn(Voice& v, const Instrument& m, float pitch, int dt, const float* lm, float vol,
                              const float* la) {
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
  sp.hz[1] = cachedHz(v, 1, pitch + clampf(m.synSemi, -24, 24) + 24.f * la[ld(LfoDest::Semi2)] + det);
  sp.mix = mac[kMacMix] * (1.f / 127.f);
  sp.sync = m.synSync;
  sp.sub = u7(m.synSub + 64.f * la[ld(LfoDest::Sub)]) * (1.f / 127.f);
  sp.subOct = m.synSubOct ? 2 : 1;
  sp.noise = u7(m.synNoise + 64.f * la[ld(LfoDest::Noise)]) * (1.f / 127.f);
  v.sv().control(sp, dt ? dt : ctlLeft_);  // same ramp contract as the FM voice
  const uint8_t iv = m.vol > 127 ? 127 : m.vol;
  v.amp = v.gain * iv * vol * (1.f / 127.f);
}

// LOFI: macros as DRUM (A B C TONE LOOP / WIDTH TONE DRIFT SMOOTH), no params cache (all cheap). The
// formula's bytecode is looked up again only when the instrument's formula index changes.
MT_HOT void Synth::controlLofi(Voice& v, const Instrument& m, float pitch, int dt, const float* lm, float vol) {
  float mac[kFmMacros];
  macros(v, m, lm, mac);
  LofiParams& lp = v.lp();
  lp.machine = v.machine;
  if (lp.formula != m.lofiFormula || !lp.code) {
    lp.formula = m.lofiFormula;
    lp.code = &byteCode(m.lofiFormula);
  }
  if (v.machine == static_cast<uint8_t>(LofiMachine::Byte) && m.lofiSync) {
    // SYNC: 1024 t per 1/16 step (24 ticks) x 2^octave of the note from C-4; semitones, bend and
    // glide ignored. Recomputed every control period, so tempo changes follow.
    const int n = v.note + static_cast<int>(clampf(m.transpose, -24, 24)) - 60;
    const int oct = n >= 0 ? n / 12 : -((11 - n) / 12);
    const float rate = 1024.f * kSynthRate / (24.f * tickSamples());  // t per second at C-4
    lp.hz = rate * (oct >= 0 ? static_cast<float>(1 << oct) : 1.f / static_cast<float>(1 << -oct)) / kByteRatePerHz;
  } else {
    lp.hz = cachedHz(v, 0, pitch);
  }
  for (int k = 0; k < kFmMacros; ++k) lp.mac[k] = mac[k] * (1.f / 127.f);
  v.lv().control(lp, dt);
  const uint8_t iv = m.vol > 127 ? 127 : m.vol;
  v.amp = v.gain * iv * vol * (1.f / 127.f);
}

MT_HOT void Synth::renderVoice(Voice& v, float* out, int n) {
  // Segments end at control boundaries: n <= kControl. A filtered voice renders into flt first,
  // an unfiltered one straight into out.
  if (v.env.idle()) {
    // The note has ended: only the filter rings out (render() frees the voice when it is quiet).
    if (v.fltOn) v.flt.processAdd(nullptr, out, n);
    return;
  }
  // Filtered: the engine renders into flt, then the filter adds it to out. Else straight into out
  // (bit-exact to the plain path). Drive and crush are the track's (runStrip).
  float flt[kControl];
  float* __restrict dst = out;
  if (v.fltOn) {
    for (int i = 0; i < n; ++i) flt[i] = 0;
    dst = flt;
  }
  // Env and oscillator state are copied to locals for the loops: through v every dst store would
  // force a reload (dst may alias the voice as far as the compiler knows).
  if (v.fm || v.drum || v.syn || v.lofi) {
    float tmp[kControl] = {0};
    if (v.fm) v.fmv().render(tmp, n, v.amp);
    else if (v.drum) v.drv().render(tmp, n, v.amp);  // the gate env scales a choke tail too
    else if (v.syn) v.sv().render(tmp, n, v.amp);
    else v.lv().render(tmp, n, v.amp);
    Env env = v.env;
    for (int i = 0; i < n; ++i) dst[i] += tmp[i] * env.next();
    v.env = env;
    if ((v.fm && v.fmv().done()) || (v.drum && v.drv().done())) v.env.kill();
  } else if (v.sample) {
    renderSample(v, dst, n);
  } else {
    renderChip(v, dst, n);
  }
  if (v.fltOn) v.flt.processAdd(flt, out, n);
}

// CHIP: one loop per wave (no per-sample switch), state in locals.
template <Wave W>
MT_HOT static void chipLoop(ChipOsc& o, Env& e, float* __restrict dst, int n, float inc, float duty, float amp) {
  ChipOsc osc = o;
  Env env = e;
  for (int i = 0; i < n; ++i) dst[i] += osc.next(W, inc, duty) * env.next() * amp;
  o = osc;
  e = env;
}

MT_HOT void Synth::renderChip(Voice& v, float* out, int n) {
  const float inc = v.inc, duty = v.duty, amp = v.amp;
  switch (static_cast<Wave>(v.wave)) {
    case Wave::Pulse: chipLoop<Wave::Pulse>(v.osc, v.env, out, n, inc, duty, amp); break;
    case Wave::Triangle: chipLoop<Wave::Triangle>(v.osc, v.env, out, n, inc, duty, amp); break;
    case Wave::Saw: chipLoop<Wave::Saw>(v.osc, v.env, out, n, inc, duty, amp); break;
    default: {
      const Wave w = static_cast<Wave>(v.wave);  // noise, metal, wavetables: the generic loop
      ChipOsc osc = v.osc;
      Env env = v.env;
      for (int i = 0; i < n; ++i) out[i] += osc.next(w, inc, duty) * env.next() * amp;
      v.osc = osc;
      v.env = env;
    }
  }
}

// Sample playback: 32.32 fixed point position, linear interpolation between frames. The loop
// region [loopLo, loopHi) bounds the play direction's edge (for Off it is the whole region).
MT_HOT void Synth::renderSample(Voice& v, float* out, int n) {
  const int16_t* d = v.smp;
  const uint32_t last = v.smpLen - 1;
  const int64_t lo = static_cast<int64_t>(v.loopLo) << 32, hi = static_cast<int64_t>(v.loopHi) << 32;
  const int64_t one = int64_t(1) << 32;
  const float amp = v.amp * (kSampleGain / 32768.f);
  const uint8_t mode = v.loopMode;
  int64_t pos = v.pos;
  int64_t step = v.dir > 0 ? v.step : -v.step;
  Env env = v.env;  // in registers: an out store would force a reload through v
  for (int i = 0; i < n; ++i) {
    const uint32_t idx = static_cast<uint32_t>(pos >> 32);
    const int32_t a = d[idx];
    const int32_t b = d[idx < last ? idx + 1 : last];
    const float s = a + (b - a) * (static_cast<uint32_t>(pos) * 2.3283064e-10f);
    out[i] += s * env.next() * amp;
    pos += step;
    if (step >= 0 ? pos < hi : pos >= lo) continue;
    // Crossed the edge in the play direction (rare).
    if (mode == static_cast<uint8_t>(LoopMode::Forward)) {
      const int64_t span = hi - lo;
      // One span back / on first: the usual case, no 64-bit '%' (__moddi3, ROM) per loop pass.
      if (step >= 0) {
        if (pos >= hi) pos -= span;
        if (pos >= hi) pos = lo + (pos - lo) % span;  // one step may cross a short loop many times
      } else {
        if (pos < lo) pos += span;
        if (pos < lo) pos = hi - 1 - (hi - 1 - pos) % span;
      }
    } else if (mode == static_cast<uint8_t>(LoopMode::PingPong)) {
      // Reflect around the edge frame: ..., 98, 99, 98, ... (the end frame is not repeated).
      pos = step >= 0 ? 2 * (hi - one) - pos : 2 * lo - pos;
      step = -step;
      if (pos < lo) pos = lo;
      if (pos >= hi) pos = hi - 1;
    } else {
      env.kill();
      break;
    }
  }
  v.env = env;
  v.pos = pos;
  v.dir = step >= 0 ? 1 : -1;
}

// x x (gl, gr) added into l / r. Ramping (a step non-zero): each gain steps before its sample, gl /
// gr return the gains reached. Steady: no increments (centred: x x 1 adds x bit-exactly).
MT_INLINE static void panAdd(const float* __restrict x, float* __restrict l, float* __restrict r, int n,
                             float& gl, float& gr, float dgl, float dgr) {
  float a = gl, b = gr;
  if (dgl == 0 && dgr == 0) {
    for (int i = 0; i < n; ++i) {
      l[i] += x[i] * a;
      r[i] += x[i] * b;
    }
    return;
  }
  for (int i = 0; i < n; ++i) {
    a += dgl;
    b += dgr;
    l[i] += x[i] * a;
    r[i] += x[i] * b;
  }
  gl = a;
  gr = b;
}

// Insert, then fader x pan into the mix: the strip's sends (mono, pre-pan) and the peak for the
// track's level meter (MIX) on the way.
MT_HOT void Synth::runStrip(uint8_t track, int pos, int n, int scTrack, bool keyAround) {
  TrackStrip& s = strips_[track];
  float* __restrict x = trk_;
  if (s.drvFade < 1.f) {  // a drive change: the old settings fade out over kControl samples
    float y[kControl];
    for (int i = 0; i < n; ++i) y[i] = x[i];
    s.drvOld.processBlock(y, n);  // bypass (off): unchanged
    s.drive.processBlock(x, n);
    float w = s.drvFade;
    constexpr float kStep = 1.f / kControl;
    for (int i = 0; i < n; ++i) {
      w = minf(w + kStep, 1.f);
      x[i] = y[i] + (x[i] - y[i]) * w;
    }
    s.drvFade = w;
  } else if (s.drive.on()) {
    s.drive.processBlock(x, n);
  }
  if (s.crush.on()) s.crush.processBlock(x, n);
  if (s.gate.on || s.gate.g != 1.f) runGate(s.gate, x, n);  // off and settled: bit-exact bypass
  // One peak scan for the follower (a source: after the insert, pre-fader) and the meter.
  const bool folSrc = folSrcMask_ & (1u << track);
  float xPk = 0;
  if (folSrc || meters_) {
    for (int i = 0; i < n; ++i) xPk = maxf(xPk, fabsf(x[i]));
    if (folSrc) srcPk_[track] = maxf(srcPk_[track], xPk);
  }
  if (s.dsend != 0 || s.drsend != 0) {  // ramping (landing at the boundary)
    float vs = s.send, vr = s.rsend;
    const float ds = s.dsend, dr = s.drsend;
    for (int i = 0; i < n; ++i) {
      vs += ds;
      vr += dr;
      send_[pos + i] += x[i] * vs;
      rsend_[pos + i] += x[i] * vr;
    }
    s.send = vs;
    s.rsend = vr;
  } else if (s.send > 0 || s.rsend > 0) {
    const float vs = s.send, vr = s.rsend;
    for (int i = 0; i < n; ++i) {
      send_[pos + i] += x[i] * vs;
      rsend_[pos + i] += x[i] * vr;
    }
  }
  if (s.dmsend != 0) {  // the MOD send, the same way
    float vm = s.msend;
    const float dm = s.dmsend;
    for (int i = 0; i < n; ++i) {
      vm += dm;
      msend_[pos + i] += x[i] * vm;
    }
    s.msend = vm;
  } else if (s.msend > 0) {
    const float vm = s.msend;
    for (int i = 0; i < n; ++i) msend_[pos + i] += x[i] * vm;
  }
  if (meters_) {
    const float pk = xPk * s.fader;
    if (pk > trackPeak_[track]) trackPeak_[track] = pk;
  }
  // Sidechain ducking: the key track goes around the compressor (into sc only, added back after
  // it); without the compressor the key also stays in the mix.
  float gl = s.gl, gr = s.gr;
  const bool sc = track == scTrack;
  if (sc && !keyAround) {
    float a = gl, b = gr;
    panAdd(x, scL_ + pos, scR_ + pos, n, a, b, s.dgl, s.dgr);
  }
  const bool key = sc && keyAround;
  panAdd(x, (key ? scL_ : mixL_) + pos, (key ? scR_ : mixR_) + pos, n, gl, gr, s.dgl, s.dgr);
  s.gl = gl;
  s.gr = gr;
}

// One segment of latency, whatever the track order: the envelopes move from the last segment's
// peaks before any voice reads them. A follower is on with a source 1..kTracks and both tracks INT;
// amount 0 still runs the envelope (the TRACK page's bar) but moves nothing.
void Synth::updateFollowers() {  // per control update with a follower set (flash)
  uint32_t srcs = 0, voice = 0, vol = 0, have = 0;
  float lvl[kTracks];
  for (int t = 0; t < kTracks; ++t) {
    const TrackCfg& c = p_.tracks[t];
    const int src = c.folSrc - 1;
    if (src < 0 || src >= kTracks || c.out != TrackOut::Int || p_.tracks[src].out != TrackOut::Int) {
      folEnv_[t] = 0;
      folMod_[t] = 0;
      continue;
    }
    srcs |= 1u << src;
    if (!(have & (1u << src))) {  // one log per source
      lvl[src] = folLevel(srcPk_[src]);
      have |= 1u << src;
    }
    const uint16_t key = static_cast<uint16_t>(c.folAtk | c.folRel << 8);
    if (key != folKey_[t]) {
      folKey_[t] = key;
      folK_[t][0] = folCoef(c.folAtk);
      folK_[t][1] = folCoef(c.folRel);
    }
    const float l = lvl[src];
    float e = folEnv_[t];
    e += (l - e) * folK_[t][l > e ? 0 : 1];
    if (e < 1e-6f) e = 0;  // a silent source settles at exactly 0: the targets bit-exact to off
    folEnv_[t] = e;
    const int8_t a = c.folAmt < -64 ? -64 : (c.folAmt > 63 ? 63 : c.folAmt);
    folMod_[t] = a * (1.f / 64.f) * e;
    if (a == 0) continue;
    const uint8_t d = c.folDest < kLfoDests ? c.folDest : ld(LfoDest::Cutoff);
    if (d == ld(LfoDest::Vol)) {
      vol |= 1u << t;
    } else {
      voice |= 1u << t;
      folLa_[t] = static_cast<int8_t>(d);
    }
  }
  for (float& x : srcPk_) x = 0;
  folSrcMask_ = srcs;
  folVoice_ = voice;
  folVolMask_ = vol;
  folIdle_ = srcs == 0;
}

// Open (1) on a mask step within its first len, else floor; 1 while stopped or off.
float Synth::gateTarget(const TrackStrip::Gate& g, float p) const {
  if (!g.on || !running_) return 1.f;
  const int k = static_cast<int>(p);
  return ((g.mask >> (k & 15)) & 1) && p - static_cast<float>(k) < g.len ? 1.f : g.floor;
}

// The gate over the segment from the song clock's position (gatePos_ moves after the strips).
// The gain ramps linearly: never more than up / down per sample, so an edge never clicks.
// Settled (the gain at a target that holds over the whole segment): open = untouched (bit-exact),
// else one constant gain; only a segment with an edge or a ramp runs the per-sample loop.
MT_HOT void Synth::runGate(TrackStrip::Gate& g, float* __restrict x, int n) {
  float p = static_cast<float>(gatePos_) / g.steps;
  const float inc = running_ && !paused_ ? 1.f / (sp16_ * g.steps) : 0.f;
  float a = g.g;
  const bool open = g.on && running_;
  const uint16_t mask = g.mask;
  const float len = g.len, floor = g.floor, up = g.up, down = g.down;
  // Target at p: open on a mask step within its first len, else floor; 1 while stopped or off.
  auto target = [&](float q) {
    const int k = static_cast<int>(q);
    return !open ? 1.f : (((mask >> (k & 15)) & 1) && q - static_cast<float>(k) < len ? 1.f : floor);
  };
  const float t0 = target(p);
  if (a == t0) {
    // The target over the segment: the same step and side of len at both ends (a margin of a few
    // increments past the last sample covers the per-sample accumulation's rounding).
    bool hold = !open || inc == 0.f;
    if (!hold) {
      const float pe = p + inc * static_cast<float>(n + 4);
      const int k0 = static_cast<int>(p), ke = static_cast<int>(pe);
      hold = k0 == ke && (p - static_cast<float>(k0) < len) == (pe - static_cast<float>(ke) < len);
    }
    if (hold) {
      if (a != 1.f)
        for (int i = 0; i < n; ++i) x[i] *= a;
      return;
    }
  }
  for (int i = 0; i < n; ++i) {
    const float t = target(p);
    a = a < t ? minf(a + up, t) : maxf(a - down, t);
    x[i] *= a;
    p += inc;
  }
  g.g = a;
}

// The profile stage of a voice's render.
MT_INLINE static int voiceStage(const Voice& v) {
  return v.fm ? Synth::kProfFm : v.drum ? Synth::kProfDrum : v.syn ? Synth::kProfSyn
       : v.lofi ? Synth::kProfLofi : v.sample ? Synth::kProfSample : Synth::kProfChip;
}

bool Synth::applyProfiler() {
  profSeen_ = profReq_.load(std::memory_order_acquire);
  clock_ = clockReq_.load(std::memory_order_relaxed);
  if (!clock_) return false;
  for (int i = 0; i < kProfStages; ++i) prof_[i] = profN_[i] = 0;
  profBlocks_ = 0;
  profT_ = clock_();
  return true;
}

// A changed table (the link's Tuning chunk, the UI) or scale root (AUTO root): rebuild the key map. A
// memcmp of ~280 bytes per block; nothing per sample.
MT_HOT void Synth::syncTuning() {
  if (p_.scaleRoot == tuneRootSeen_ && memcmp(&p_.tuning, &tuneSeen_, sizeof tuneSeen_) == 0) return;
  memcpy(&tuneSeen_, &p_.tuning, sizeof tuneSeen_);
  tuneRootSeen_ = p_.scaleRoot;
  tune_.build(tuneSeen_, tuneRootSeen_);
}

MT_HOT void Synth::renderCore() {
  // Profiling: mark(stage) books the time since the last mark. The clock only changes here.
  syncProfiler();
  const bool clk = clock_ != nullptr;
  if (clk) {
    for (const auto& v : voices_)
      if (v.on) ++profN_[voiceStage(v)];
    profT_ = clock_();
  }
  cacheInstruments();
  syncTuning();
  const float xft = p_.xfade > 127 ? 127.f : static_cast<float>(p_.xfade);
  if (xfSnap_) {  // a new synth / reset: the position as it is, no glide
    xf_ = xft;
    xfSnap_ = false;
  }
  mark(kProfInstLfo);
  for (auto& x : mixL_) x = 0;
  for (auto& x : mixR_) x = 0;
  for (auto& x : send_) x = 0;
  for (auto& x : rsend_) x = 0;
  for (auto& x : msend_) x = 0;
  for (auto& x : scL_) x = 0;
  for (auto& x : scR_) x = 0;
  const int scTrack = p_.scTrack >= 1 && p_.scTrack <= kTracks ? p_.scTrack - 1 : -1;
  // Sidechain ducking: the key track goes around the compressor (into sc only, added back after it),
  // so it ducks the rest of the mix without squashing itself.
  const bool keyAround = scTrack >= 0 && p_.compAmt > 0 && p_.scDepth > 0;
  sp16_ = (kPpqn / 4) * tickSamples();
  int e = 0;
  for (int pos = 0; pos < kBlock;) {
    mark(kProfMix);
    if (pos % kControl == 0) {
      if (xf_ != xft) xf_ = xf_ < xft ? minf(xf_ + kXfStep, xft) : maxf(xf_ - kXfStep, xft);  // crossfader glide
      // Followers: none set and none running (the common case) costs a scan of the tracks.
      bool fol = !folIdle_;
      for (int t = 0; t < kTracks && !fol; ++t) fol = p_.tracks[t].folSrc != 0;
      if (fol) updateFollowers();
      advanceInstLfos();
      mark(kProfInstLfo);
      for (auto& v : voices_)
        if (v.on && !v.env.idle()) control(v, kControl);  // a ringing filter tail keeps its cutoff
      mark(kProfCtlEng);
      // Every strip from its newest voice (the event refreshes below then cover only this segment's
      // events), ramping over the whole control period.
      int nv[kSynthTracks];
      for (int& x : nv) x = -1;
      for (int i = 0; i < kVoices; ++i) {
        const Voice& v = voices_[i];
        const int t = v.track < kSynthTracks ? v.track : 0;
        if (v.on && (nv[t] < 0 || v.age > voices_[nv[t]].age)) nv[t] = i;
      }
      for (int t = 0; t < kSynthTracks; ++t) refreshStrip(static_cast<uint8_t>(t), nv[t], kControl);
      stripDirty_ = 0;
      mark(kProfStrip);
    }
    ctlLeft_ = kControl - pos % kControl;
    while (e < nEv_ && ev_[e].off <= pos) apply(ev_[e++]);
    mark(kProfEvents);
    // Tracks the events touched: their strips ramp over what is left of the control period.
    for (uint32_t d = stripDirty_; d;) {
      const int t = __builtin_ctz(d);
      d &= d - 1;
      refreshStrip(static_cast<uint8_t>(t), trackVoice(static_cast<uint8_t>(t)), ctlLeft_);
    }
    stripDirty_ = 0;
    int end = (pos / kControl + 1) * kControl;
    if (e < nEv_ && ev_[e].off < end) end = ev_[e].off;
    const bool boundary = end % kControl == 0;
    const int n = end - pos;
    // Track by track (ascending, as the mix adds them): the track's voices (in voice order) into
    // one mono sum, trk_ (the first copies, the others add), then the track's strip. One sum
    // buffer for all tracks.
    int8_t head[kSynthTracks], next[kVoices];
    for (auto& h : head) h = -1;
    for (int i = kVoices - 1; i >= 0; --i) {
      const Voice& v = voices_[i];
      if (!v.on) continue;
      const int t = v.track < kSynthTracks ? v.track : 0;
      next[i] = head[t];
      head[t] = static_cast<int8_t>(i);
    }
    for (int t = 0; t < kSynthTracks; ++t) {
      if (head[t] < 0) continue;
      float* __restrict x = trk_;
      for (int i = head[t]; i >= 0; i = next[i]) {
        Voice& v = voices_[i];
        if (i != head[t]) {
          float tmp[kControl] = {0};
          renderVoice(v, tmp, n);
          for (int j = 0; j < n; ++j) x[j] += tmp[j];
        } else {
          for (int j = 0; j < n; ++j) x[j] = 0;
          renderVoice(v, x, n);
        }
        if (clk) mark(voiceStage(v));
        // Freed once the note has ended and its filter has rung out: cutting a resonant filter
        // mid-ring clicks.
        if (!sounding(v)) v.on = false;
      }
      runStrip(static_cast<uint8_t>(t), pos, n, scTrack, keyAround);
      if (clk) mark(kProfStrip);
    }
    if (boundary)  // the ramps have landed: exact targets, no drift
      for (auto& x : strips_) {
        x.gl = x.tgl;
        x.gr = x.tgr;
        x.dgl = x.dgr = 0;
        x.send = x.tsend;
        x.rsend = x.trsend;
        x.msend = x.tmsend;
        x.dsend = x.drsend = x.dmsend = 0;
      }
    if (running_ && !paused_) {  // the song clock free-runs between 0xF6 5 messages
      // A full control segment: n / sp16_ cached per tempo (a __divdf3 in ROM, ~4 per block otherwise).
      if (n == kControl && sp16_ != gateSp16_) {
        gateSp16_ = sp16_;
        gateCtlInc_ = kControl / static_cast<double>(sp16_);
      }
      gatePos_ += n == kControl ? gateCtlInc_ : n / static_cast<double>(sp16_);
      if (gatePos_ >= 64.0) gatePos_ -= 64.0;
    }
    mark(kProfStrip);
    pos = end;
  }
  gateOut_ = running_ ? static_cast<float>(gatePos_) : -1.f;
  nEv_ = 0;
  ctlLeft_ = kControl;
  mark(kProfMix);
  // PERF throws: the whole mix (mono, before the MOD bus / delay / reverb) into the sends while held.
  // The send gains ramp (kPerfRamp per sample): a cut mid-note would step the lines' input. The
  // sidechain key track (keyAround) joins the mix after the comp: it is neither thrown nor ducked by a
  // freeze.
  if (const float ds = perf_.delaySend(), rs = perf_.reverbSend(), ms = perf_.modSend();
      ds > 0.f || rs > 0.f || ms > 0.f || perfDs_ > 0.f || perfRs_ > 0.f || perfMs_ > 0.f)
    for (int i = 0; i < kBlock; ++i) {
      perfDs_ = perfDs_ < ds ? minf(perfDs_ + kPerfRamp, ds) : maxf(perfDs_ - kPerfRamp, ds);
      perfRs_ = perfRs_ < rs ? minf(perfRs_ + kPerfRamp, rs) : maxf(perfRs_ - kPerfRamp, rs);
      perfMs_ = perfMs_ < ms ? minf(perfMs_ + kPerfRamp, ms) : maxf(perfMs_ - kPerfRamp, ms);
      const float m = (mixL_[i] + mixR_[i]) * 0.5f;
      send_[i] += m * perfDs_;
      rsend_[i] += m * perfRs_;
      msend_[i] += m * perfMs_;
    }
  // A throw / freeze (or its tail): its return at least kPerfRet, so a throw sounds with the
  // project's level at 0. Delay: DELAY THROW; reverb: REVERB THROW or FREEZE; MOD: MOD THROW (with
  // the MOD bus OFF it throws into a CHORUS). The floors move per block (fast up, slow down): no step
  // in the returns.
  const bool frozen = perf_.freeze();
  auto floor = [](uint8_t& f, bool up) {
    if (up) f = f + kPerfRetUp < kPerfRet ? f + kPerfRetUp : kPerfRet;
    else if (f) --f;
  };
  floor(perfDRet_, perf_.delayTail() || perfDs_ > 0.f);
  floor(perfRRet_, perf_.reverbTail() || frozen || perfRs_ > 0.f);
  floor(perfMRet_, perf_.modTail() || perfMs_ > 0.f);
  // Scene morph of the master values (morphM: the project's when no scene end holds them).
  const uint8_t dlv = morphM(kScnDLvl, p_.dlyLevel), rlv = morphM(kScnRLvl, p_.rvbLevel);
  const uint8_t rsz = morphM(kScnRSize, p_.rvbSize);
  const uint8_t dl = dlv < perfDRet_ ? perfDRet_ : dlv;
  const uint8_t rl = rlv < perfRRet_ ? perfRRet_ : rlv;
  // MOD bus: its stereo return into the mix (OFF, level 0 or idle: untouched).
  {
    uint8_t mt = p_.modType;
    if (mt == static_cast<uint8_t>(ModType::Off) && perfMRet_ > 0) mt = static_cast<uint8_t>(ModType::Chorus);
    const uint8_t mlv = morphM(kScnMLvl, p_.modLevel);
    const uint8_t ml = mlv < perfMRet_ ? perfMRet_ : mlv;
    modfx_.process(msend_, mixL_, mixR_, kBlock, mt, morphM(kScnMRate, p_.modRate), morphM(kScnMDepth, p_.modDepth),
                   p_.modFb, ml);
  }
  mark(kProfMod);
  // Delay mode; TAPE WOBBLE (PERF) forces TAPE at its wear (at least the project's) while held. In
  // PING-PONG it does nothing: a switch would wipe the line (the delay mutes for ~0.25 s).
  {
    uint8_t mode = p_.dlyMode, chr = morphM(kScnDChar, p_.dlyChar);
    if (const int w = perf_.tapeWobble(); w >= 0 && mode != static_cast<uint8_t>(DlyMode::PingPong)) {
      if (mode != static_cast<uint8_t>(DlyMode::Tape)) chr = 0;
      mode = static_cast<uint8_t>(DlyMode::Tape);
      if (w > chr) chr = static_cast<uint8_t>(w);
    }
    delay_.process(send_, mixL_, mixR_, kBlock, delaySamples(), morphM(kScnDFb, p_.dlyFb),
                   morphM(kScnDTone, p_.dlyTone), dl, mode, chr);
  }
  mark(kProfDelay);
  // FREEZE: the live mix (and the delay) ducks under the frozen tail, ramped in and out.
  if (const float g = perf_.freezeDry(); g != 1.f || perfDry_ != 1.f)
    for (int i = 0; i < kBlock; ++i) {
      perfDry_ = perfDry_ < g ? minf(perfDry_ + kPerfRamp, g) : maxf(perfDry_ - kPerfRamp, g);
      mixL_[i] *= perfDry_;
      mixR_[i] *= perfDry_;
    }
  // The delay runs at level 0 too (its line stays live); the reverb rests there, so once its return
  // closes the lines are cleared: a later throw must not replay a stale tail.
  if (rvbRan_ && rl == 0) {
    reverb_.clear();
    shimmer_.clear();
  }
  rvbRan_ = rl > 0;
  // SHIMMER: the shifted last block of the reverb (shimWet_) into its input. Leaving SHIMMER, the
  // loop gain ramps down over a block before it stops; amount 0 adds nothing (bit-exact).
  const bool shim = shimOn_ && (p_.rvbMode == static_cast<uint8_t>(RvbMode::Shimmer) || shimmer_.active());
  if (shim) {
    if (!shimRan_)
      for (int i = 0; i < kBlock; ++i) shimWet_[i] = 0;
    shimmer_.setInterval(p_.shimInt);
    const uint8_t amt = p_.rvbMode == static_cast<uint8_t>(RvbMode::Shimmer) ? p_.rvbShim : 0;
    shimmer_.process(rsend_, shimWet_, rsend_, kBlock, amt, rsz, frozen);
    for (int i = 0; i < kBlock; ++i) shimWet_[i] = 0;  // the reverb writes this block's (or nothing: silent)
  }
  shimRan_ = shim;
  mark(kProfShim);
  reverb_.process(rsend_, mixL_, mixR_, kBlock, rsz, morphM(kScnRDamp, p_.rvbDamp), rl, frozen, shim ? shimWet_ : nullptr);
  mark(kProfReverb);
  comp_.process(mixL_, mixR_, scTrack >= 0 ? scL_ : nullptr, scTrack >= 0 ? scR_ : nullptr, kBlock, p_.compAmt,
                p_.compRel, p_.scDepth);
  mark(kProfComp);
  if (keyAround)
    for (int i = 0; i < kBlock; ++i) {
      mixL_[i] += scL_[i];
      mixR_[i] += scR_[i];
    }
  // Master EQ: after the comp (it does not react to the boost) and the key track, before PERF (its
  // FILTER stays the last colour). Flat: skipped.
  eq_.process(mixL_, mixR_, kBlock, morphM(kScnEqLo, p_.eqLow), morphM(kScnEqHi, p_.eqHigh));
  mark(kProfEq);
  // After the key track is back: PERF works on the whole mix. A 1/16 step = kPpqn / 4 ticks.
  perf_.process(mixL_, mixR_, kBlock, (kPpqn / 4) * tickSamples(), !paused_);
  mark(kProfPerf);
  const uint8_t mvs = morphM(kScnMain, p_.masterVol);
  const uint8_t mv = mvs > kMasterVolMax ? kMasterVolMax : mvs;
  const float g = mv * (0.25f / 100.f);  // 100 %: headroom for 16 voices; up to 200 % leans on the soft clip
  for (int i = 0; i < kBlock; ++i) mixL_[i] = softClip(mixL_[i] * g) * 32767.f;
  for (int i = 0; i < kBlock; ++i) mixR_[i] = softClip(mixR_[i] * g) * 32767.f;
  mark(kProfMaster);
  if (clk) ++profBlocks_;
}

MT_HOT void Synth::renderStereo(int16_t* lr) {
  renderCore();
  for (int i = 0; i < kBlock; ++i) {
    lr[2 * i] = static_cast<int16_t>(mixL_[i]);
    lr[2 * i + 1] = static_cast<int16_t>(mixR_[i]);
  }
}

// The downmix of the two int16 channels: equal sides give that side exactly.
MT_HOT void Synth::render(int16_t* out) {
  renderCore();
  for (int i = 0; i < kBlock; ++i) {
    const int l = static_cast<int16_t>(mixL_[i]), r = static_cast<int16_t>(mixR_[i]);
    out[i] = static_cast<int16_t>((l + r) >> 1);
  }
}

void Synth::takeTrackPeaks(float out[kTracks]) {
  const float g = 0.25f;  // the master gain at MAIN 100 % (render())
  for (int t = 0; t < kTracks; ++t) {
    out[t] = trackPeak_[t] * g;
    trackPeak_[t] = 0;
  }
}

const char* Synth::profName(int stage) {
  static const char* const kNames[kProfStages] = {"queue", "events", "cache",  "instLfo", "ctlMod", "ctlFlt",
                                                  "ctlEng", "chip",  "sample", "fm",      "drum",   "synth",
                                                  "mix",   "delay", "reverb", "comp",    "perf",   "master", "strip",
                                                  "mod",   "shim",  "eq",     "lofi"};
  return stage >= 0 && stage < kProfStages ? kNames[stage] : "";
}

void Synth::takeProfile(uint32_t out[kProfStages], uint32_t count[kProfStages], uint32_t& blocks) {
  for (int i = 0; i < kProfStages; ++i) {
    out[i] = prof_[i];
    prof_[i] = 0;
    count[i] = profN_[i];
    profN_[i] = 0;
  }
  blocks = profBlocks_;
  profBlocks_ = 0;
}

int Synth::activeVoices() const {
  int n = 0;
  for (const auto& v : voices_) n += v.on;
  return n;
}

MT_HOT int Synth::trackVoice(uint8_t track) const {
  int best = -1;
  for (int i = 0; i < kVoices; ++i)
    if (voices_[i].on && voices_[i].track == track && (best < 0 || voices_[i].age > voices_[best].age)) best = i;
  return best;
}

}  // namespace mt
