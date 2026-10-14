// Voice ceiling bench (env teensy41-bench-max, -DAUDIO_BENCH_MAX with a raised MT_VOICES / MT_HEAVY_MAX):
// how many voices of each kind fit before the CPU guard (Synth::setLoad, kLoadHigh) starts shedding.
// Every voice sends to the delay and the reverb at 100 with drive 100; delay feedback 100, reverb and
// delay level 100, compressor on. Step 0: no voices (the effects' own cost). Then ladders of 4 more
// voices per step: CHIP saw through a resonant LP with an envelope, FM TONE and wavetable SYNTH chords
// (held, kPolyPerTrack per track), DRUM retriggered every 16th at 120 BPM (one voice per track), and
// 16 wavetable SYNTH voices plus a growing CHIP pool. A ladder stops at its first step over budget:
// average over kLoadHigh, a block over 100 %, the guard shrinking the pool below the voices played
// or fewer voices sounding than played; its limit is the step before.
// Each step lasts 4 s, the last 3 s measured (with the CPU profile); 1 s of silence between ladders.
// The first step starts 15 s after boot. Results go out with link::log, a summary at the end; on the
// desk build (the env) read them with `link_desk.py status 300`.
#ifdef AUDIO_BENCH_MAX
#include "bench.h"
#include <Arduino.h>
#include "audio_out.h"
#include "link_server.h"
#include "model.h"
#include "synth.h"

namespace bench {

namespace {

constexpr int kChipInstr = mt::kInstruments - 1;
constexpr int kWtInstr = kChipInstr - 1;
constexpr int kFmInstr = kWtInstr - 1;
constexpr int kDrumKinds = 8;
constexpr int kDrumInstr = kFmInstr - kDrumKinds;  // .. kFmInstr - 1
static_assert(kDrumInstr >= 0, "max bench layout");

constexpr uint32_t blocksIn(float ms) { return static_cast<uint32_t>(ms * 1000.f / SynthStream::kBlockUs + 0.5f); }
constexpr uint32_t kHitEvery = blocksIn(125);     // a 16th at 120 BPM
constexpr uint32_t kStepBlocks = blocksIn(4000);
constexpr uint32_t kSkipBlocks = blocksIn(1000);  // settling, not measured
constexpr uint32_t kRestBlocks = blocksIn(1000);  // between ladders: tails die out
constexpr uint32_t kBootBlocks = blocksIn(15000); // from boot: time to start link_desk.py

enum class Kind : uint8_t { None, Chip, Fm, Wt, Drum, WtChip };
constexpr int kMixWt = 16;                                  // WtChip: fixed wavetable voices ...
constexpr int kMixWtTracks = kMixWt / mt::kPolyPerTrack;     // ... on the first tracks
constexpr int kAll = mt::kTracks * mt::kPolyPerTrack;
constexpr int kHeavyTop = mt::kFmVoiceMax < kAll ? mt::kFmVoiceMax : kAll;
constexpr int kChipTop = mt::kVoices < kAll ? mt::kVoices : kAll;
constexpr int kMixTop = (mt::kVoices < kAll ? mt::kVoices : kAll) - kMixWt;
static_assert(kMixWt % mt::kPolyPerTrack == 0 && kMixWt <= mt::kFmVoiceMax && kMixTop > 0, "max bench mix");

struct Ladder {
  const char* name;
  Kind kind;
  int top;  // voices (WtChip: CHIP voices on top of kMixWt)
};
constexpr Ladder kLadders[] = {{"fx only", Kind::None, 0},    {"CHIP", Kind::Chip, kChipTop},
                               {"FM TONE", Kind::Fm, kHeavyTop}, {"WT SYNTH", Kind::Wt, kHeavyTop},
                               {"DRUM", Kind::Drum, mt::kTracks}, {"16 WT + CHIP", Kind::WtChip, kMixTop}};
constexpr int kLadderCount = sizeof(kLadders) / sizeof(kLadders[0]);
constexpr int kStep = 4;

mt::SynthModel* model = nullptr;
mt::Synth* synth = nullptr;

// Audio interrupt state.
int ladder = 0, count = 0;  // count: voices of the step (WtChip: CHIP ones)
bool resting = true, done = false, pending = false;  // pending: the step's notes go out next block
uint32_t restFor = kBootBlocks;
uint32_t blocks, n, peak, voiceSum, capMin;
uint64_t sum;

struct Result {
  int ladder, count, played;
  uint32_t avgCycles, peakCycles, voices, cap;
  bool over;
};
struct Limit {
  int clean = -1;  // voices of the last clean step, -1 = none
  float avg = 0, peak = 0;
};
volatile uint32_t resSeq = 0, measureSeq = 0;
Result res;
Limit limits[kLadderCount];

// loop(): the profile sums (the synth's 32-bit ones wrap within seconds).
uint32_t seenRes = 0, seenMeasure = 0;
bool profOn = false;
uint64_t prof[mt::Synth::kProfStages];
uint32_t profBlocks;

uint32_t cycleClock() { return ARM_DWT_CYCCNT; }
float pct(uint32_t cycles) { return 100.f * cycles / SynthStream::blockCycles(); }

void note(int track, uint8_t status, uint8_t key) {
  const uint8_t b[3] = {status, key, 100};
  synth->event(0, static_cast<uint8_t>(track), b, 3);
}

// Held notes: kPolyPerTrack per track from track `first`.
void chord(int first, int voices, uint8_t status) {
  for (int i = 0; i < voices; ++i) {
    const int t = first + i / mt::kPolyPerTrack, k = i % mt::kPolyPerTrack;
    note(t, status, static_cast<uint8_t>(48 + (t % 4) * 3 + k * 5));
  }
}

int played() { return kLadders[ladder].kind == Kind::WtChip ? kMixWt + count : count; }

// The step's tracks: instruments, then the held notes (status 0x90) or their ends (0x80).
void notes(uint8_t status) {
  switch (kLadders[ladder].kind) {
    case Kind::None:
    case Kind::Drum: break;  // drums: hits()
    case Kind::Chip:
    case Kind::Fm:
    case Kind::Wt: chord(0, count, status); break;
    case Kind::WtChip:
      chord(0, kMixWt, status);
      chord(kMixWtTracks, count, status);
      break;
  }
}

void hits() {
  if (kLadders[ladder].kind != Kind::Drum) return;
  for (int t = 0; t < count; ++t) note(t, 0x90, 60);
}

void startStep() {
  const Kind k = kLadders[ladder].kind;
  for (int t = 0; t < mt::kTracks; ++t) {
    mt::TrackCfg& tr = model->tracks[t];
    tr.out = mt::TrackOut::Int;
    tr.instr = k == Kind::Fm                                ? kFmInstr
               : k == Kind::Wt                              ? kWtInstr
               : k == Kind::Drum                            ? kDrumInstr + t % kDrumKinds
               : k == Kind::WtChip && t < kMixWtTracks      ? kWtInstr
                                                            : kChipInstr;
  }
  notes(0x90);
  hits();
}

// Next step of the ladder, or a rest before the next ladder.
void advance(bool over) {
  notes(0x80);
  const Ladder& l = kLadders[ladder];
  if (!over && count < l.top) {  // its notes a block later: ends + starts in one block overflow kMaxEvents
    count = count + kStep < l.top ? count + kStep : l.top;
    pending = true;
    return;
  }
  if (++ladder >= kLadderCount) {
    done = true;
    return;
  }
  count = kStep;
  resting = true;
  restFor = kRestBlocks;
}

// Audio interrupt, before the block: last = the previous block's render cycles.
void tick(uint32_t last) {
  if (done) return;
  if (pending) {
    pending = false;
    startStep();
  }
  ++blocks;
  if (resting) {
    if (blocks < restFor) return;
    resting = false;
    blocks = 0;
    startStep();
    return;
  }
  if (blocks == kSkipBlocks) {
    measureSeq = measureSeq + 1;
    capMin = mt::kVoices;
  }
  if (blocks > kSkipBlocks) {
    sum += last;
    ++n;
    if (last > peak) peak = last;
    voiceSum += synth->activeVoices();
    if (static_cast<uint32_t>(synth->voiceCap()) < capMin) capMin = synth->voiceCap();
  }
  if (blocks % kHitEvery == 0) hits();
  if (blocks < kStepBlocks) return;
  const uint32_t avg = n ? static_cast<uint32_t>(sum / n) : 0;
  const uint32_t voices = n ? (voiceSum + n / 2) / n : 0;
  const int want = played();
  const bool over = pct(avg) >= 100.f * mt::Synth::kLoadHigh || peak >= SynthStream::blockCycles() ||
                    capMin < static_cast<uint32_t>(want) || static_cast<int>(voices) < want;
  res = {ladder, count, want, avg, peak, voices, capMin, over};
  if (!over) limits[ladder] = {want, pct(avg), pct(peak)};
  resSeq = resSeq + 1;
  blocks = n = peak = voiceSum = 0;
  sum = 0;
  advance(over);
}

void foldProfile() {
  uint32_t c[mt::Synth::kProfStages], cn[mt::Synth::kProfStages], b;
  __disable_irq();
  synth->takeProfile(c, cn, b);
  __enable_irq();
  for (int i = 0; i < mt::Synth::kProfStages; ++i) prof[i] += c[i];
  profBlocks += b;
}

void report(const Result& r) {
  link::log("max %-12s %2d voices: avg %5.1f %% peak %5.1f %%, sounding %2lu, cap min %2lu%s", kLadders[r.ladder].name,
            r.played, pct(r.avgCycles), pct(r.peakCycles), static_cast<unsigned long>(r.voices),
            static_cast<unsigned long>(r.cap), r.over ? "  <- over" : "");
  if (!profBlocks) return;
  char line[220];
  int k = 0;
  const float block = static_cast<float>(SynthStream::blockCycles());
  for (int i = 0; i < mt::Synth::kProfStages && k < static_cast<int>(sizeof line) - 24; ++i) {
    const float p = 100.f * prof[i] / profBlocks / block;
    if (p >= 0.05f) k += snprintf(line + k, sizeof line - k, " %s %.1f", mt::Synth::profName(i), p);
  }
  link::log("  profile %%:%s", line);
}

void summary() {
  link::log("max bench done (voices %d, heavy %d, guard at %.0f %%):", mt::kVoices, mt::kFmVoiceMax,
            100.f * mt::Synth::kLoadHigh);
  for (int i = 0; i < kLadderCount; ++i) {
    const Limit& l = limits[i];
    if (l.clean < 0)
      link::log("  %-12s none clean", kLadders[i].name);
    else
      link::log("  %-12s %2d voices (avg %.1f %%, peak %.1f %%)%s", kLadders[i].name, l.clean, l.avg, l.peak,
                kLadders[i].kind != Kind::None && l.clean == (kLadders[i].kind == Kind::WtChip ? kMixWt : 0) +
                                                                kLadders[i].top
                    ? "  (ladder top, not the CPU)"
                    : "");
  }
}

void setFx(mt::Instrument& in) {
  in.drive = 100;
  in.rsend = 100;
  in.send = 100;
}

}  // namespace

void begin(mt::SynthModel& m, mt::Synth& s, SynthStream& out) {
  model = &m;
  synth = &s;
  using mt::DrumMachine;
  static const DrumMachine kMachines[kDrumKinds] = {DrumMachine::Bd8, DrumMachine::Sd8, DrumMachine::Hh8,
                                                    DrumMachine::Cy9, DrumMachine::Cp8, DrumMachine::Tom9,
                                                    DrumMachine::Bd9, DrumMachine::Rs8};
  for (int i = 0; i < kDrumKinds; ++i) {
    mt::Instrument& in = m.instruments[kDrumInstr + i];
    in = mt::Instrument();
    mt::instrSetType(in, mt::InstrType::Drum);
    mt::drumSetMachine(in, static_cast<uint8_t>(kMachines[i]));
    in.macro[mt::kMacDec] = 110;  // still ringing at the choke
    in.vol = 40;
    setFx(in);
  }
  mt::Instrument& fm = m.instruments[kFmInstr];
  fm = mt::Instrument();
  mt::instrSetType(fm, mt::InstrType::Fm);
  mt::fmSetMachine(fm, static_cast<uint8_t>(mt::FmMachine::Tone));
  fm.mono = false;
  fm.sustain = 127;
  fm.vol = 30;
  setFx(fm);
  mt::Instrument& w = m.instruments[kWtInstr];
  w = mt::Instrument();
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
  setFx(w);
  mt::Instrument& chip = m.instruments[kChipInstr];
  chip = mt::Instrument();
  chip.wave = static_cast<uint8_t>(mt::Wave::Saw);
  chip.sustain = 127;
  chip.mono = false;
  chip.fltMode = static_cast<uint8_t>(mt::FltMode::Lp);
  chip.cutoff = 60;
  chip.reso = 100;
  chip.fenv = 40;
  chip.fDec = 0;
  chip.vol = 30;
  setFx(chip);
  m.dlyFb = 100;
  m.dlyLevel = 100;
  m.rvbLevel = 100;
  m.compAmt = 100;
  m.scTrack = 1;
  link::log("max bench: voices %d, heavy %d; ladders of +%d voices, 4 s per step, from 15 s after boot",
            mt::kVoices, mt::kFmVoiceMax, kStep);
  out.setBlockHook(tick);  // the first step after kBootBlocks
}

void step() {
  if (measureSeq != seenMeasure) {  // a step's measured part began: profile from here
    seenMeasure = measureSeq;
    memset(prof, 0, sizeof prof);
    profBlocks = 0;
    if (!profOn) {
      synth->setProfiler(cycleClock);
      profOn = true;
    }
    uint32_t c[mt::Synth::kProfStages], cn[mt::Synth::kProfStages], b;
    __disable_irq();
    synth->takeProfile(c, cn, b);  // dropped: the settling part
    __enable_irq();
  } else if (profOn) {
    foldProfile();
  }
  if (resSeq == seenRes) return;
  seenRes = resSeq;
  __disable_irq();
  const Result r = res;
  const bool fin = done;
  __enable_irq();
  report(r);
  if (fin && seenRes == resSeq) {
    synth->setProfiler(nullptr);
    profOn = false;
    summary();
  }
}

}  // namespace bench
#endif
