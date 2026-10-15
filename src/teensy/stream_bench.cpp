#if defined(LINK_DESK) && !defined(AUDIO_BENCH_MAX)
#include "stream_bench.h"
#include <Arduino.h>
#include <Audio.h>
#include <SD.h>
#include <stdio.h>
#include <string.h>
#include "audio_out.h"
#include "fs_server.h"
#include "link_msg.h"
#include "link_server.h"
#include "model.h"
#include "psram.h"
#include "sample_store.h"
#include "stream.h"
#include "synth.h"


namespace sbench {

namespace {

constexpr int kN = 16;
constexpr int kBenchInstr = mt::kInstruments - 1 - kN;  // the top instrument stays as it is
constexpr uint32_t kSettleMs = 2000;                     // first notes start on heads / cold slots
constexpr uint32_t kNotesAfterMs = 20;  // the unpark resets the synth in the next audio block: notes after it
// A voice takes its PSRAM copy only at note-on and the bench holds its notes: they wait for the fill.
constexpr uint32_t kFillWaitMs = 20000;
static_assert(kBenchInstr >= 0 && kN <= mt::kTracks, "stream bench layout");

// Heavy (kBenchHeavy): the 8 longest files on tracks 0..7, two notes each (root + semis, one semitone
// above) = 16 stream voices; tracks 8..15 carry kHeavyN heavy voices: DRUM on the even ones
// (retriggered every 16th at 120 BPM, as the pool bench), held FM TONE chords on the odd ones.
constexpr int kSmpTracks = 8;
constexpr int kSmpNotes = 2;
constexpr int kDrumTracks = (mt::kTracks - kSmpTracks) / 2;
constexpr int kHeavyN = mt::kFmVoiceMax < 16 ? mt::kFmVoiceMax : 16;  // teensy41-bench-max raises the cap
constexpr int kToneNotes = (kHeavyN - kDrumTracks) / kDrumTracks;    // per FM TONE track
constexpr int kHeavyInstr = kBenchInstr - kDrumTracks - 1;                 // drums, then FM TONE
static_assert(kSmpTracks * kSmpNotes <= kN && kSmpNotes <= mt::kPolyPerTrack, "heavy bench: stream voices");
static_assert(kDrumTracks * (1 + kToneNotes) == kHeavyN && kToneNotes <= mt::kPolyPerTrack,
              "heavy bench: kHeavyN heavy voices on 8 tracks");
constexpr int kWtInstr = kHeavyInstr - 1;  // kBenchWt: two held notes per track on 8..15
constexpr int kWtNotes = kHeavyN / (mt::kTracks - kSmpTracks);
static_assert(kWtInstr >= 0 && kWtNotes <= mt::kPolyPerTrack, "heavy bench layout");

constexpr uint32_t blocksIn(float ms) { return static_cast<uint32_t>(ms * 1000.f / SynthStream::kBlockUs + 0.5f); }
constexpr uint32_t kHitEvery = blocksIn(125);  // a 16th at 120 BPM

mt::SynthModel* model = nullptr;
mt::Synth* synth = nullptr;
SynthStream* out = nullptr;

bool running = false;
bool notesOn = false;
uint8_t note = 60;
uint32_t t0 = 0, lastLog = 0, endMs = 0;
uint32_t underruns = 0;
uint16_t minRes = 0xFFFF;
bool settled = false;
bool heavy = false;
bool wt = false;  // kBenchWt
uint64_t cpuSum = 0;  // measured part: render cycles, blocks, slowest block, lowest voice cap
uint32_t cpuBlocks = 0, cpuPeak = 0, capLow = mt::kVoices;

// Audio interrupt state (the block hook).
volatile bool drumsOn = false, measuring = false;
uint32_t hookBlocks = 0, hookSum = 0, hookN = 0, hookPeak = 0, hookCap = mt::kVoices;

struct Found {
  uint32_t crc = 0, frames = 0;
};

// "<8 hex>.wav" (a store file, not its .pk) -> its crc (no sscanf: libc's scanf would land in ITCM).
FLASHMEM bool hexName(const char* name, uint32_t& crc) {
  if (strlen(name) != 12 || strcasecmp(name + 8, ".wav") != 0) return false;
  crc = 0;
  for (int i = 0; i < 8; ++i) {
    const char c = name[i];
    const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (d < 0) return false;
    crc = crc << 4 | static_cast<uint32_t>(d);
  }
  return true;
}

// The kN longest store files (by size), longest first. Returns how many.
FLASHMEM int scanStore(Found* f) {
  int n = 0;
  FsFile dir = SD.sdfs.open(mt::kStoreDir);
  if (!dir) return 0;
  FsFile e;
  char name[16];
  while (e.openNext(&dir, O_RDONLY)) {
    e.getName(name, sizeof name);
    uint32_t crc = 0;
    if (hexName(name, crc) && e.fileSize() > mt::kStoreDataOffset) {
      Found x;
      x.crc = crc;
      x.frames = static_cast<uint32_t>((e.fileSize() - mt::kStoreDataOffset) / 2);
      int at = n < kN ? n++ : kN;  // insertion, longest first
      for (; at > 0 && f[at - 1].frames < x.frames; --at)
        if (at < kN) f[at] = f[at - 1];
      if (at < kN) f[at] = x;
    }
    e.close();
  }
  dir.close();
  return n;
}

void noteOn(int track, uint8_t key) {
  const uint8_t m[3] = {static_cast<uint8_t>(0x90 | track), key, 100};
  synth->event(0, static_cast<uint8_t>(track), m, 3);
}

bool drumTrack(int t) { return !wt && t >= kSmpTracks && (t - kSmpTracks) % 2 == 0; }

// Audio interrupt, before the block: last = the previous block's render cycles.
void tick(uint32_t last) {
  if (measuring) {
    hookSum += last;
    ++hookN;
    if (last > hookPeak) hookPeak = last;
    const uint32_t cap = static_cast<uint32_t>(synth->voiceCap());
    if (cap < hookCap) hookCap = cap;
  }
  if (!drumsOn || ++hookBlocks % kHitEvery) return;
  for (int t = kSmpTracks; t < mt::kTracks; ++t)
    if (drumTrack(t)) noteOn(t, 60);
}

struct Cpu {
  uint32_t sum, n, peak, cap;
};
Cpu takeCpu() {
  __disable_irq();
  const Cpu c = {hookSum, hookN, hookPeak, hookCap};
  hookSum = hookN = hookPeak = 0;
  hookCap = mt::kVoices;
  __enable_irq();
  return c;
}

FLASHMEM void notes(bool on) {
  AudioNoInterrupts();
  // Off: 0xFF releases the track (SAMPLE voices on tracks ignore note-offs, as in the tracker).
  if (!on) drumsOn = false;
  for (int t = 0; t < (heavy ? mt::kTracks : kN); ++t) {
    if (!on) {
      const uint8_t m[1] = {0xFF};
      synth->event(0, static_cast<uint8_t>(t), m, 1);
    } else if (!heavy) {
      noteOn(t, note);
    } else if (t < kSmpTracks) {  // one step (no kSynthStep between): the second note keeps the first
      for (int k = 0; k < kSmpNotes; ++k) noteOn(t, static_cast<uint8_t>(note + k));
    } else if (wt) {
      for (int k = 0; k < kWtNotes; ++k) noteOn(t, static_cast<uint8_t>(48 + (t % 4) * 3 + k * 7));
    } else if (!drumTrack(t)) {
      static const uint8_t kChord[4] = {48, 55, 60, 63};
      for (int k = 0; k < kToneNotes; ++k) noteOn(t, kChord[k]);
    }
  }
  if (on && heavy && !wt) {
    hookBlocks = kHitEvery - 1;  // the first hits in the next block
    drumsOn = true;
  }
  AudioInterrupts();
  notesOn = on;
}

FLASHMEM void heavyInstruments() {
  using mt::DrumMachine;
  static const DrumMachine kMachines[] = {DrumMachine::Bd8, DrumMachine::Sd8, DrumMachine::Hh8, DrumMachine::Cy9};
  for (int i = 0; i < kDrumTracks; ++i) {
    mt::Instrument& in = model->instruments[kHeavyInstr + i];
    in = mt::Instrument{};
    mt::instrSetType(in, mt::InstrType::Drum);
    mt::drumSetMachine(in, static_cast<uint8_t>(kMachines[i % 4]));
    in.macro[mt::kMacDec] = 110;  // still ringing at the choke
    in.vol = 40;
  }
  mt::Instrument& tone = model->instruments[kHeavyInstr + kDrumTracks];
  tone = mt::Instrument{};
  mt::instrSetType(tone, mt::InstrType::Fm);
  mt::fmSetMachine(tone, static_cast<uint8_t>(mt::FmMachine::Tone));
  tone.mono = false;
  tone.sustain = 127;
  tone.vol = 30;
  // As the max bench's (bench_max.cpp).
  mt::Instrument& w = model->instruments[kWtInstr];
  w = mt::Instrument{};
  mt::instrSetType(w, mt::InstrType::Synth);
  w.synOsc[0] = w.synOsc[1] = static_cast<uint8_t>(mt::SynOsc::Wt);
  snprintf(w.synWt[0], sizeof w.synWt[0], "*SAWSQR");
  snprintf(w.synWt[1], sizeof w.synWt[1], "*FORMANT");
  w.macro[mt::kMacMix] = 64;
  w.macro[mt::kMacDet] = 80;
  w.macro[mt::kMacShp1] = 70;
  w.macro[mt::kMacShp2] = 40;
  w.sustain = 127;
  w.mono = false;
  w.fltMode = static_cast<uint8_t>(mt::FltMode::Lp);
  w.cutoff = 70;
  w.reso = 80;
  w.fenv = 40;
  w.vol = 30;
}

// kBenchFx: every bench instrument into delay and reverb at 100, every track's insert through drive
// 100 and a MOD send (ENSEMBLE), delay TAPE (feedback, wear, level 100), reverb SHIMMER, master EQ,
// compressor on (as bench_max.cpp).
FLASHMEM void fxOn() {
  for (int i = kWtInstr; i < kBenchInstr + kN; ++i) {
    mt::Instrument& in = model->instruments[i];
    in.send = 100;
    in.rsend = 100;
  }
  for (auto& c : model->tracks) {
    c.drive = 100;
    c.modSend = 90;
  }
  model->dlyMode = static_cast<uint8_t>(mt::DlyMode::Tape);
  model->dlyChar = 100;
  model->rvbMode = static_cast<uint8_t>(mt::RvbMode::Shimmer);
  model->rvbShim = 80;
  model->modType = static_cast<uint8_t>(mt::ModType::Ensemble);
  model->eqLow = 96;
  model->eqHigh = 48;
  model->dlyFb = 100;
  model->dlyLevel = 100;
  model->rvbLevel = 100;
  model->compAmt = 100;
  model->scTrack = 1;
}

// Without kBenchFx: the track inserts and the master fx modes off again (an earlier fx run set them).
FLASHMEM void fxOff() {
  for (auto& c : model->tracks) c.drive = c.modSend = 0;
  model->dlyMode = static_cast<uint8_t>(mt::DlyMode::Digital);
  model->rvbMode = static_cast<uint8_t>(mt::RvbMode::Plain);
  model->modType = static_cast<uint8_t>(mt::ModType::Off);
  model->eqLow = model->eqHigh = 64;
}

}  // namespace

FLASHMEM void begin(mt::SynthModel& m, mt::Synth& s, SynthStream& o) {
  model = &m;
  synth = &s;
  out = &o;
  o.setBlockHook(tick);
}

FLASHMEM void start(uint8_t semis, uint8_t flags, uint16_t secs) {
  if (!model) return;
  if (running) stop();
  if (!card::ready()) {
    link::log("sbench: no card");
    return;
  }
  Found f[kN];
  const int n = scanStore(f);
  if (n < kN) link::log("sbench: only %d store files (want %d long ones: LD bank <wav>)", n, kN);
  if (!n) return;
  out->park(true);  // no audio block touches the synth from here on; the unpark resets it once more
  synth->reset();
  model->sampleCount = static_cast<uint8_t>(n);
  for (int i = 0; i < n; ++i) {
    mt::ProjSample& ps = model->samples[i];
    snprintf(ps.name, sizeof ps.name, "SB%02d", i);
    ps.crc = f[i].crc;
    ps.frames = f[i].frames;
    mt::Instrument& in = model->instruments[kBenchInstr + i];
    in = mt::Instrument{};
    mt::instrSetType(in, mt::InstrType::Sample);
    snprintf(in.sample, sizeof in.sample, "%s", ps.name);
    in.root = 60;
    in.vol = 40;  // 16 full-scale loops would clip the output
    in.loop = static_cast<uint8_t>(mt::LoopMode::Forward);
    in.loopStart = 0;
  }
  heavy = flags & mt::link::kBenchHeavy;
  wt = heavy && (flags & mt::link::kBenchWt);
  if (heavy) heavyInstruments();
  if (flags & mt::link::kBenchFx) fxOn();
  else fxOff();
  for (int t = 0; t < (heavy ? mt::kTracks : kN); ++t) {
    model->tracks[t].out = mt::TrackOut::Int;
    if (heavy && t >= kSmpTracks)
      model->tracks[t].instr = static_cast<uint8_t>(wt ? kWtInstr : drumTrack(t) ? kHeavyInstr + (t - kSmpTracks) / 2 : kHeavyInstr + kDrumTracks);
    else
      model->tracks[t].instr = static_cast<uint8_t>(kBenchInstr + (t < n ? t : t % n));
    model->tracks[t].vol = 100;
    model->tracks[t].mute = false;
    model->tracks[t].solo = false;
  }
  psram::hold(flags & mt::link::kBenchSdOnly);
  stream::rescan(*model);  // store entries, heads, and the PSRAM plan (none if held)
  out->park(false);
  link::log("sbench: %d samples, %lu..%lu frames, %s%s%s", n, static_cast<unsigned long>(f[n - 1].frames),
            static_cast<unsigned long>(f[0].frames), (flags & mt::link::kBenchSdOnly) ? "SD only" : "PSRAM allowed",
            !heavy ? "" : wt ? ", heavy: 16 stream voices on tracks 0..7 + WT SYNTH on 8..15" : ", heavy: 16 stream voices on tracks 0..7 + DRUM / FM TONE on 8..15",
            (flags & mt::link::kBenchFx) ? ", fx: drive / MOD / tape delay / shimmer reverb / EQ, comp" : "");
  if (flags & mt::link::kBenchSetupOnly) {
    psram::hold(false);  // only the plan of this rescan was held
    return;
  }
  note = static_cast<uint8_t>(60 + (semis > 24 ? 24 : semis));
  running = true;
  settled = false;
  notesOn = false;
  t0 = lastLog = millis();
  endMs = t0 + kSettleMs + secs * 1000u;
  underruns = 0;
  minRes = 0xFFFF;
  cpuSum = 0;
  cpuBlocks = cpuPeak = 0;
  capLow = mt::kVoices;
}

FLASHMEM void stop() {
  if (!running) return;
  measuring = false;
  if (notesOn) notes(false);
  running = false;
  const uint32_t ran = millis() - t0;
  const float block = static_cast<float>(SynthStream::blockCycles());
  link::log("sbench: done, %lu underruns in %lu s, min reserve %u ms, cpu avg %.1f %% peak %.1f %%, cap min %lu",
            static_cast<unsigned long>(underruns), static_cast<unsigned long>(ran > kSettleMs ? (ran - kSettleMs) / 1000 : 0),
            minRes, cpuBlocks ? 100.f * cpuSum / cpuBlocks / block : 0.f, 100.f * cpuPeak / block,
            static_cast<unsigned long>(capLow));
  psram::hold(false);
}

FLASHMEM void step() {
  if (!running) return;
  const uint32_t now = millis();
  if (!notesOn) {
    if (now - t0 < kNotesAfterMs) return;
    int ready = 0, bad = 0, planned = 0;
    psram::fillCounts(ready, bad, planned);
    const bool filling = psram::state() == psram::State::On && ready + bad < planned;
    if (filling && now - t0 < kFillWaitMs) return;
    if (now - t0 > kNotesAfterMs + 100) {
      link::log("sbench: notes after %lu ms of PSRAM fill (%u%%, %d copies, %d bad)",
                static_cast<unsigned long>(now - t0), psram::fillPct(), ready, bad);
    }
    endMs += now - t0;  // the measurement starts with the notes
    t0 = lastLog = now;
    notes(true);
    return;
  }
  if (!settled) {
    if (now - t0 < kSettleMs) return;
    stream::takeStats(1);  // the cold start does not count
    takeCpu();
    measuring = true;
    settled = true;
    lastLog = now;
    return;
  }
  if (now - lastLog >= 1000) {
    lastLog = now;
    const stream::Stats s = stream::takeStats(1);
    underruns += s.underruns;
    if (s.minReserveMs < minRes) minRes = s.minReserveMs;
    const Cpu c = takeCpu();
    cpuSum += c.sum;
    cpuBlocks += c.n;
    if (c.peak > cpuPeak) cpuPeak = c.peak;
    if (c.cap < capLow) capLow = c.cap;
    const float block = static_cast<float>(SynthStream::blockCycles());
    link::log("sbench: %3lu s  voices %2u/%2d  underruns %u (%lu)  min reserve %u ms  cpu %.1f %% peak %.1f %%  cap %lu",
              static_cast<unsigned long>((now - t0 - kSettleMs) / 1000), s.voices, synth->activeVoices(), s.underruns,
              static_cast<unsigned long>(underruns), s.minReserveMs, c.n ? 100.f * c.sum / c.n / block : 0.f,
              100.f * c.peak / block, static_cast<unsigned long>(c.cap));
  }
  if (static_cast<int32_t>(now - endMs) >= 0) stop();
}

}  // namespace sbench
#endif  // LINK_DESK, not AUDIO_BENCH_MAX
