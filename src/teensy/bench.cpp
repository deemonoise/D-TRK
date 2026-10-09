// Voice pool bench (env teensy41-bench, -DAUDIO_BENCH_POOL; port of the ESP's audio.cpp bench):
// first 16 CHIP voices alone (their cost per voice), then all kVoices CHIP voices, then 8, 12 and
// kFmVoiceMax heavy voices (DRUM and FM HAT on alternate tracks, retriggered every 16th at 120 BPM),
// then the same with wavetable SYNTH heavy voices (both oscillators on built-in tables, held notes),
// the rest CHIP saw voices through a resonant LP filter with an envelope, as many as the tracks
// left hold (kPolyPerTrack each, plus the preview track); drive and reverb on all, reverb 100,
// compressor on. The CPU guard (Synth::setLoad) sheds what does not fit.
// Each step lasts 10 s; the last 8 s are measured (with the CPU profile) and printed with
// link::log (Log frames + USB Serial). Then the bench stops (all notes off).
// It overwrites the top 18 instruments and every track's out / instrument; the ESP's state mirror
// is not applied in a bench build (link_server.cpp), so the bench keeps its sounds.
#ifdef AUDIO_BENCH_POOL
#include "bench.h"
#include <Arduino.h>
#include "audio_out.h"
#include "link_server.h"
#include "model.h"
#include "synth.h"

namespace bench {

namespace {

static_assert(mt::kFmVoiceMax >= 8 && mt::kFmVoiceMax <= mt::kTracks, "pool bench: heavy tracks");
constexpr int kHeavyInstr = mt::kInstruments - 1 - mt::kFmVoiceMax;  // .. kChipInstr - 1: heavy
constexpr int kChipInstr = mt::kInstruments - 1;
constexpr int kWtInstr = kHeavyInstr - 1;
static_assert(kWtInstr >= 0, "pool bench layout");

constexpr uint32_t blocksIn(float ms) { return static_cast<uint32_t>(ms * 1000.f / SynthStream::kBlockUs + 0.5f); }
constexpr uint32_t kHitEvery = blocksIn(125);     // a 16th at 120 BPM
constexpr uint32_t kStepBlocks = blocksIn(10000);  // 10 s
constexpr uint32_t kSkipBlocks = blocksIn(2000);   // settling, not measured

// Light voices with h heavy tracks: the rest of the pool, as far as the tracks left hold them.
constexpr int lightFor(int h) {
  return mt::kVoices - h < (mt::kTracks - h + 1) * mt::kPolyPerTrack ? mt::kVoices - h
                                                                     : (mt::kTracks - h + 1) * mt::kPolyPerTrack;
}
constexpr int kMid = mt::kFmVoiceMax > 12 ? 12 : mt::kFmVoiceMax;
struct PoolStep {
  int heavy, light;
  bool wt;  // heavy voices: wavetable SYNTH, else DRUM / FM
};
constexpr PoolStep kSteps[] = {{0, 16, false},
                               {0, lightFor(0), false},
                               {8, lightFor(8), false},
                               {kMid, lightFor(kMid), false},
                               {mt::kFmVoiceMax, lightFor(mt::kFmVoiceMax), false},
                               {mt::kFmVoiceMax, 0, true},
                               {8, lightFor(8), true},
                               {kMid, lightFor(kMid), true},
                               {mt::kFmVoiceMax, lightFor(mt::kFmVoiceMax), true}};
constexpr uint32_t kStepCount = sizeof(kSteps) / sizeof(kSteps[0]);

mt::SynthModel* model = nullptr;
mt::Synth* synth = nullptr;

// Audio interrupt state.
uint32_t stepsDone;
int heavy = kSteps[0].heavy, lightN = kSteps[0].light;
bool wt = kSteps[0].wt;
uint32_t blocks, n, peak, voiceSum, capMin;
uint64_t sum;

// Results, handed to loop() under seq.
struct Result {
  int heavy, light;
  bool wt;
  uint32_t avgCycles, peakCycles, voices, cap;
};
volatile uint32_t resSeq = 0, measureSeq = 0;
Result res;

// loop(): the profile sums (the synth's 32-bit ones wrap within seconds).
uint32_t seenRes = 0, seenMeasure = 0;
bool profOn = false;
uint64_t prof[mt::Synth::kProfStages];
uint32_t profBlocks;

uint32_t cycleClock() { return ARM_DWT_CYCCNT; }

void note(int track, uint8_t status, uint8_t key) {
  const uint8_t b[3] = {status, key, 100};
  synth->event(0, static_cast<uint8_t>(track), b, 3);
}

void hits() {
  if (wt) return;  // held from startStep
  for (int t = 0; t < heavy; ++t) note(t, 0x90, 60);
}

void wtNotes(uint8_t status) {
  if (!wt) return;
  for (int t = 0; t < heavy; ++t) note(t, status, static_cast<uint8_t>(48 + t * 2));
}

// Light notes of the step: kPolyPerTrack per track from heavy up, then the preview track.
template <typename F>
void light(F f) {
  int left = lightN;
  for (int t = heavy; t <= mt::kTracks && left > 0; ++t) {
    const int track = t < mt::kTracks ? t : mt::kPreviewTrack;
    for (int k = 0; k < mt::kPolyPerTrack && left > 0; ++k, --left)
      f(track, static_cast<uint8_t>(48 + (t % 4) * 3 + k * 5));
  }
}

void startStep() {
  for (int t = 0; t < mt::kTracks; ++t) {
    model->tracks[t].out = mt::TrackOut::Int;
    model->tracks[t].instr = static_cast<uint8_t>(t >= heavy ? kChipInstr : wt ? kWtInstr : kHeavyInstr + t);
  }
  light([](int track, uint8_t key) { note(track, 0x90, key); });
  wtNotes(0x90);
  hits();
}

// Audio interrupt, before the block: last = the previous block's render cycles.
void tick(uint32_t last) {
  if (stepsDone >= kStepCount) return;
  ++blocks;
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
  res = {heavy, lightN, wt, n ? static_cast<uint32_t>(sum / n) : 0, peak, n ? (voiceSum + n / 2) / n : 0, capMin};
  resSeq = resSeq + 1;
  blocks = n = peak = voiceSum = 0;
  sum = 0;
  light([](int track, uint8_t key) { note(track, 0x80, key); });
  wtNotes(0x80);
  if (++stepsDone >= kStepCount) return;  // quiet: the drums decay on their own
  heavy = kSteps[stepsDone].heavy;
  lightN = kSteps[stepsDone].light;
  wt = kSteps[stepsDone].wt;
  startStep();
}

void foldProfile() {
  uint32_t c[mt::Synth::kProfStages], b;
  __disable_irq();
  synth->takeProfile(c, b);
  __enable_irq();
  for (int i = 0; i < mt::Synth::kProfStages; ++i) prof[i] += c[i];
  profBlocks += b;
}

void report(const Result& r) {
  const float block = static_cast<float>(SynthStream::blockCycles());
  link::log("pool %d %s+%d of %d (heavy max %d): avg %.1f %% peak %.1f %%, voices %lu, cap min %lu", r.heavy, r.wt ? "WT" : "DRUM/FM", r.light,
            mt::kVoices, mt::kFmVoiceMax, 100.f * r.avgCycles / block, 100.f * r.peakCycles / block,
            static_cast<unsigned long>(r.voices), static_cast<unsigned long>(r.cap));
  if (!profBlocks) return;
  char line[200];
  int k = 0;
  for (int i = 0; i < mt::Synth::kProfStages && k < static_cast<int>(sizeof line) - 24; ++i)
    k += snprintf(line + k, sizeof line - k, " %s %.1f", mt::Synth::profName(i), 100.f * prof[i] / profBlocks / block);
  link::log("  profile %%:%s", line);
}

}  // namespace

void begin(mt::SynthModel& m, mt::Synth& s, SynthStream& out) {
  model = &m;
  synth = &s;
  using mt::DrumMachine;
  static const DrumMachine kMachines[] = {DrumMachine::Bd8, DrumMachine::Sd8, DrumMachine::Hh8, DrumMachine::Cy9,
                                          DrumMachine::Cp8, DrumMachine::Tom9, DrumMachine::Bd9, DrumMachine::Rs8};
  for (int i = 0; i < mt::kFmVoiceMax; ++i) {
    mt::Instrument& in = m.instruments[kHeavyInstr + i];
    in = mt::Instrument();
    if (i % 2 == 0) {
      mt::instrSetType(in, mt::InstrType::Drum);
      mt::drumSetMachine(in, static_cast<uint8_t>(kMachines[(i / 2) % 8]));
      in.macro[mt::kMacDec] = 110;  // still ringing at the choke
    } else {
      mt::instrSetType(in, mt::InstrType::Fm);
      mt::fmSetMachine(in, static_cast<uint8_t>(mt::FmMachine::Hat));
      in.macro[mt::kMacDec] = 127;
      in.macro[mt::kMacCon] = 127;
      in.macro[mt::kMacShp] = 64;
    }
  }
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
  for (int i = kWtInstr; i <= kChipInstr; ++i) {
    m.instruments[i].drive = 100;
    m.instruments[i].rsend = 100;
  }
  m.rvbLevel = 100;
  m.compAmt = 100;
  m.scTrack = 1;
  link::log("pool bench: %lu steps of 10 s, results below", static_cast<unsigned long>(kStepCount));
  __disable_irq();
  const uint8_t pgm[2] = {0xC0, kChipInstr};
  s.event(0, mt::kPreviewTrack, pgm, 2);
  startStep();
  __enable_irq();
  out.setBlockHook(tick);
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
    uint32_t c[mt::Synth::kProfStages], b;
    __disable_irq();
    synth->takeProfile(c, b);  // dropped: the settling part
    __enable_irq();
  } else if (profOn) {
    foldProfile();
  }
  if (resSeq == seenRes) return;
  seenRes = resSeq;
  __disable_irq();
  const Result r = res;
  __enable_irq();
  report(r);
  if (seenRes >= kStepCount) {
    synth->setProfiler(nullptr);
    profOn = false;
    link::log("pool bench done");
  }
}

}  // namespace bench
#endif
