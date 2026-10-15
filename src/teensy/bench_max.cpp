// Voice ceiling bench (env teensy41-bench-max, -DAUDIO_BENCH_MAX with a raised MT_VOICES / MT_HEAVY_MAX):
// how many voices of each kind fit before the CPU guard (Synth::setLoad, kLoadHigh) starts shedding.
// Every voice sends to the delay and the reverb at 100, every track's insert drives at 100 and sends
// to the MOD bus (ENSEMBLE); delay TAPE (feedback, wear, level 100), reverb SHIMMER (level 100),
// master EQ and compressor on. Step 0: no voices (the effects' own cost). Then ladders of 4 more
// voices per step: CHIP saw through a resonant LP (filter env held at its peak), FM TONE and wavetable SYNTH chords
// (held, kPolyPerTrack per track), DRUM retriggered every 16th at 120 BPM (one voice per track), and
// 16 wavetable SYNTH voices plus a growing CHIP pool. A ladder stops at its first step over budget:
// average over kLoadHigh, a block over 100 %, the guard shrinking the pool below the voices played
// or fewer voices sounding than played; its limit is the step before.
// Each step lasts 4 s, the last 3 s measured (with the CPU profile); 1 s of silence between ladders
// (longer until the guard has grown the pool back to kVoices).
// The first step starts 15 s after boot. Results go out with link::log, a summary at the end; on the
// desk build (the env) read them with `link_desk.py status 300`.
// Groups (-DBENCH_MAX_GROUP=g, 0 = every ladder once): 1 the ladders above; 2 the SYNTH fold / ring and
// the filter types (CHIP LADDER / MS20 / COMB / FORMANT, WT FOLD (fold 100), WT RING (ring 64), WT
// FOLD+RING (both), 16 WT FX + CHIP LADDER), all with an LFO on the cutoff, and fx only, CHIP LFO and WT LFO (CHIP / WT SYNTH with that LFO) as
// references; 3 the MACRO models (MACRO SAW+ .. MACRO FB FM: held chords, t 0.7, c 0.6, LP 90 / reso
// 40, no ENV>TMB) and 16 MACRO <worst> + CHIP (kMacroWorst: 16 held MACRO voices plus a growing CHIP
// pool), with fx only and WT SYNTH as references; 4 the STRING models (STRING HIT, MODAL HIT: chords
// re-struck every 1/8 at 120 BPM, DAMP 100 so they ring through, BRIGHT 0.7, STRUCT 0.6, POS 0.3, EXC 100
// (a 13 ms burst), LP 90 / reso 40; STRING LOW: the STRING chords on notes 28 .. 40, the half / quarter rate
// string) and 16 <worst> HIT + CHIP (kStringWorst: 16 re-struck voices plus a growing held CHIP pool),
// with fx only, WT SYNTH and DRUM as references; 5 the voice pool by cost
// (Synth::setVoiceBudget at kVoiceBudgetTeensy, the other groups run with the budget off: they price raw
// voices): POOL WT FX+CHIP LADDER (16 WT FX as in group 2 plus CHIP LADDER) and POOL MACRO <worst> + CHIP
// request 16 .. 32 voices by 4; the budget steals, so their reports add the live voices (on, not
// stolen), and they go over only on the CPU (average, a block over 100 %, the guard shrinking the pool).
// A ladder lists the groups it runs in; its instruments come from the table in begin().
#ifdef AUDIO_BENCH_MAX
#include "bench.h"
#include <Arduino.h>
#include "audio_out.h"
#include "link_server.h"
#include "model.h"
#include "stack_mark.h"
#include "synth.h"
#include "voice_cost.h"

namespace bench {

namespace {

// Instrument slots, from the top of the list down.
constexpr int kChipInstr = mt::kInstruments - 1;
constexpr int kWtInstr = kChipInstr - 1;
constexpr int kFmInstr = kWtInstr - 1;
constexpr int kDrumKinds = 8;
constexpr int kDrumInstr = kFmInstr - kDrumKinds;  // .. kFmInstr - 1
// Group 2: the CHIP of group 1 through the new filters, the WT SYNTH with fold and ring.
constexpr int kChipLadderInstr = kDrumInstr - 1;
constexpr int kChipMs20Instr = kChipLadderInstr - 1;
constexpr int kChipCombInstr = kChipMs20Instr - 1;
constexpr int kChipFormantInstr = kChipCombInstr - 1;
constexpr int kWtFoldInstr = kChipFormantInstr - 1;   // fold 100, ring 64, LP
constexpr int kWtFxInstr = kWtFoldInstr - 1;          // fold 100, ring 64, LADDER
// Group 2's references: CHIP and WT SYNTH with the same moving cutoff as the ladders they price.
constexpr int kChipLfoInstr = kWtFxInstr - 1;
constexpr int kWtLfoInstr = kChipLfoInstr - 1;
// Fold and ring priced apart (each over WT LFO).
constexpr int kWtFoldOnlyInstr = kWtLfoInstr - 1;  // fold 100, LP
constexpr int kWtRingInstr = kWtFoldOnlyInstr - 1;  // ring 64, LP
// Group 3: one MACRO slot per model (kMacroInstr + MacroModel).
constexpr int kMacroModels = static_cast<int>(mt::MacroModel::Count);
constexpr int kMacroInstr = kWtRingInstr - kMacroModels;  // .. kWtRingInstr - 1
// The model of 16 MACRO <worst> + CHIP: the most expensive one (MORPH by estimate; set it from a run).
// Its ladder name is BENCH_MACRO_WORST_NAME, checked against the model's own ladder below.
constexpr mt::MacroModel kMacroWorst = mt::MacroModel::Morph;
#define BENCH_MACRO_WORST_NAME "MORPH"
constexpr int kMacroWorstInstr = kMacroInstr + static_cast<int>(kMacroWorst);
static_assert(kMacroModels == 8, "kLadders / kInstrs list 8 MACRO models");
// Group 4: one STRING slot per model (kStringInstr + StringModel).
constexpr int kStringModels = static_cast<int>(mt::StringModel::Count);
constexpr int kStringInstr = kMacroInstr - kStringModels;  // .. kMacroInstr - 1
// The model of 16 <worst> HIT + CHIP: the most expensive one (MODAL by estimate; set it from a run).
// Its ladder name is BENCH_STRING_WORST_NAME, checked against the model's own HIT ladder below.
constexpr mt::StringModel kStringWorst = mt::StringModel::Modal;
#define BENCH_STRING_WORST_NAME "MODAL"
constexpr int kStringWorstInstr = kStringInstr + static_cast<int>(kStringWorst);
static_assert(kStringModels == 2, "kLadders / kInstrs list 2 STRING models");
static_assert(kStringInstr >= 0, "max bench layout");

constexpr uint32_t blocksIn(float ms) { return static_cast<uint32_t>(ms * 1000.f / SynthStream::kBlockUs + 0.5f); }
constexpr uint32_t kHitEvery = blocksIn(125);     // a 16th at 120 BPM
constexpr uint32_t kStrikeEvery = blocksIn(250);  // an 8th: strike ladders re-strike their chords
constexpr uint32_t kStepBlocks = blocksIn(4000);
constexpr uint32_t kSkipBlocks = blocksIn(1000);  // settling, not measured
constexpr uint32_t kRestBlocks = blocksIn(1000);  // between ladders: tails die out
constexpr uint32_t kBootBlocks = blocksIn(15000); // from boot: time to start link_desk.py

// How a ladder plays: nothing (the fx' own cost), held chords (kPolyPerTrack per track), drum hits
// every 16th (one voice per track, instr + t % kDrumKinds), or the mix: kMixWt held voices of `mix`
// on the first tracks plus held voices of instr. A strike ladder re-sends its chords' note-ons every 1/8
// (Mix: only the kMixWt voices): each takes its own voice back (the track is at kPolyPerTrack) and
// restarts it (exciter, full ring); low: the chords on notes 28 .. 40 instead of 48 .. 72.
enum class Kind : uint8_t { None, Held, Hits, Mix };
constexpr int kMixWt = 16;                                  // Mix: fixed voices ...
constexpr int kMixWtTracks = kMixWt / mt::kPolyPerTrack;     // ... on the first tracks
constexpr int kAll = mt::kTracks * mt::kPolyPerTrack;
constexpr int kHeavyTop = mt::kFmVoiceMax < kAll ? mt::kFmVoiceMax : kAll;
constexpr int kChipTop = mt::kVoices < kAll ? mt::kVoices : kAll;
constexpr int kMixTop = (mt::kVoices < kAll ? mt::kVoices : kAll) - kMixWt;
static_assert(kMixWt % mt::kPolyPerTrack == 0 && kMixWt <= mt::kFmVoiceMax && kMixTop > 0, "max bench mix");
constexpr int kPoolTop = 16;  // group 5: kMixWt + 0 .. 16 = the 16 .. 32 voices requested
static_assert(kPoolTop <= kMixTop, "max bench pool");

// Group bits: a ladder runs in every group it lists (BENCH_MAX_GROUP 0 runs each ladder once).
constexpr uint8_t kG1 = 1, kG2 = 2, kG3 = 4, kG4 = 8, kG5 = 16;
struct Ladder {
  char name[24];
  uint8_t groups;
  Kind kind;
  int top;    // voices (Mix: of instr, on top of kMixWt)
  int instr;  // the voices' slot (Hits: the first of kDrumKinds)
  int mix;    // Mix: the slot of the kMixWt fixed voices
  bool pool = false;  // the budget on (kVoiceBudgetTeensy), from 0 voices of instr (Mix: kMixWt played)
  bool strike = false;  // the held chords re-struck every kStrikeEvery
  bool low = false;     // the chords on notes 28 .. 40
};
// In flash (PROGMEM, read in place): RAM1 is short in this env.
constexpr Ladder kLadders[] PROGMEM = {
    {"fx only", kG1 | kG2 | kG3 | kG4 | kG5, Kind::None, 0, kChipInstr, -1},
    {"CHIP", kG1, Kind::Held, kChipTop, kChipInstr, -1},
    {"FM TONE", kG1, Kind::Held, kHeavyTop, kFmInstr, -1},
    {"WT SYNTH", kG1 | kG3 | kG4, Kind::Held, kHeavyTop, kWtInstr, -1},
    {"DRUM", kG1 | kG4, Kind::Hits, mt::kTracks, kDrumInstr, -1},
    {"16 WT + CHIP", kG1, Kind::Mix, kMixTop, kChipInstr, kWtInstr},
    {"CHIP LFO", kG2, Kind::Held, kChipTop, kChipLfoInstr, -1},
    {"WT LFO", kG2, Kind::Held, kHeavyTop, kWtLfoInstr, -1},
    {"CHIP LADDER", kG2, Kind::Held, kChipTop, kChipLadderInstr, -1},
    {"CHIP MS20", kG2, Kind::Held, kChipTop, kChipMs20Instr, -1},
    {"CHIP COMB", kG2, Kind::Held, kChipTop, kChipCombInstr, -1},
    {"CHIP FORMANT", kG2, Kind::Held, kChipTop, kChipFormantInstr, -1},
    {"WT FOLD", kG2, Kind::Held, kHeavyTop, kWtFoldOnlyInstr, -1},
    {"WT RING", kG2, Kind::Held, kHeavyTop, kWtRingInstr, -1},
    {"WT FOLD+RING", kG2, Kind::Held, kHeavyTop, kWtFoldInstr, -1},
    {"16 WT FX + CHIP LADDER", kG2, Kind::Mix, kMixTop, kChipLadderInstr, kWtFxInstr},
    {"MACRO SAW+", kG3, Kind::Held, kHeavyTop, kMacroInstr + 0, -1},
    {"MACRO MORPH", kG3, Kind::Held, kHeavyTop, kMacroInstr + 1, -1},
    {"MACRO SAW/SQR", kG3, Kind::Held, kHeavyTop, kMacroInstr + 2, -1},
    {"MACRO FOLD", kG3, Kind::Held, kHeavyTop, kMacroInstr + 3, -1},
    {"MACRO VOSIM", kG3, Kind::Held, kHeavyTop, kMacroInstr + 4, -1},
    {"MACRO VOWEL", kG3, Kind::Held, kHeavyTop, kMacroInstr + 5, -1},
    {"MACRO FM", kG3, Kind::Held, kHeavyTop, kMacroInstr + 6, -1},
    {"MACRO FB FM", kG3, Kind::Held, kHeavyTop, kMacroInstr + 7, -1},
    {"16 MACRO " BENCH_MACRO_WORST_NAME " + CHIP", kG3, Kind::Mix, kMixTop, kChipInstr, kMacroWorstInstr},
    {"STRING HIT", kG4, Kind::Held, kHeavyTop, kStringInstr + 0, -1, false, true},
    {"MODAL HIT", kG4, Kind::Held, kHeavyTop, kStringInstr + 1, -1, false, true},
    {"STRING LOW", kG4, Kind::Held, kHeavyTop, kStringInstr + 0, -1, false, true, true},
    {"16 " BENCH_STRING_WORST_NAME " HIT + CHIP", kG4, Kind::Mix, kMixTop, kChipInstr, kStringWorstInstr, false, true},
    {"POOL WT FX+CHIP LADDER", kG5, Kind::Mix, kPoolTop, kChipLadderInstr, kWtFxInstr, true},
    {"POOL MACRO " BENCH_MACRO_WORST_NAME " + CHIP", kG5, Kind::Mix, kPoolTop, kChipInstr, kMacroWorstInstr, true},
};
constexpr int kLadderCount = sizeof(kLadders) / sizeof(kLadders[0]);
constexpr bool sameStr(const char* a, const char* b) { return *a == *b && (!*a || sameStr(a + 1, b + 1)); }
// The held MACRO ladder of model kMacroWorst, kLadderCount if none.
constexpr int worstLadder(int i = 0) {
  return i >= kLadderCount || (kLadders[i].kind == Kind::Held && kLadders[i].instr == kMacroWorstInstr)
             ? i : worstLadder(i + 1);
}
static_assert(worstLadder() < kLadderCount && sameStr(kLadders[worstLadder()].name, "MACRO " BENCH_MACRO_WORST_NAME),
              "BENCH_MACRO_WORST_NAME must name kMacroWorst (its MACRO <model> ladder)");
// The struck STRING ladder (not LOW) of model kStringWorst, kLadderCount if none.
constexpr int stringWorstLadder(int i = 0) {
  return i >= kLadderCount || (kLadders[i].kind == Kind::Held && kLadders[i].strike && !kLadders[i].low &&
                               kLadders[i].instr == kStringWorstInstr)
             ? i : stringWorstLadder(i + 1);
}
static_assert(stringWorstLadder() < kLadderCount &&
                  sameStr(kLadders[stringWorstLadder()].name, BENCH_STRING_WORST_NAME " HIT"),
              "BENCH_STRING_WORST_NAME must name kStringWorst (its <model> HIT ladder)");
#ifndef BENCH_MAX_GROUP
#define BENCH_MAX_GROUP 0
#endif
static_assert(BENCH_MAX_GROUP >= 0 && BENCH_MAX_GROUP <= 5, "BENCH_MAX_GROUP: 0 (all), 1 .. 5");
// 0: all. The inner ternary only keeps the shift count >= 0 when BENCH_MAX_GROUP is 0 (both arms are
// compiled; GCC warns on a negative shift even in the unused one).
constexpr uint8_t kGroupMask = BENCH_MAX_GROUP ? 1 << (BENCH_MAX_GROUP > 0 ? BENCH_MAX_GROUP - 1 : 0) : 0xFF;
constexpr bool inGroup(int i) { return kLadders[i].groups & kGroupMask; }
// The first ladder of the group from i on, kLadderCount if none.
constexpr int nextLadder(int i) { return i < kLadderCount && !inGroup(i) ? nextLadder(i + 1) : i; }
constexpr int groupCount(int i = 0) { return i < kLadderCount ? inGroup(i) + groupCount(i + 1) : 0; }
static_assert(nextLadder(0) < kLadderCount, "BENCH_MAX_GROUP runs no ladder");
constexpr int kStep = 4;

mt::SynthModel* model = nullptr;
mt::Synth* synth = nullptr;

// Audio interrupt state.
// ladder: index into kLadders (one of the group); count: voices of the step (Mix: instr ones).
int ladder = nextLadder(0), count = 0;
bool resting = true, done = false, pending = false;  // pending: the step's notes go out next block
uint32_t restFor = kBootBlocks;
uint32_t blocks, n, peak, voiceSum, liveSum, capMin;
uint64_t sum;

struct Result {
  int ladder, count, played;
  uint32_t avgCycles, peakCycles, voices, live, cap;
  bool over;
};
struct Limit {
  int clean = -1;  // voices of the last clean step, -1 = none
  float avg = 0, peak = 0;
  uint32_t live = 0;  // pool ladders: the live voices of that step
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

const Ladder& cur() { return kLadders[ladder]; }

// Held notes: kPolyPerTrack per track from track `first` (48 .. 72; low: 28 .. 40).
void chord(int first, int voices, uint8_t status) {
  const bool low = cur().low;
  for (int i = 0; i < voices; ++i) {
    const int t = first + i / mt::kPolyPerTrack, k = i % mt::kPolyPerTrack;
    note(t, status, static_cast<uint8_t>(low ? 28 + t % 4 + k * 3 : 48 + (t % 4) * 3 + k * 5));
  }
}
int played() { return cur().kind == Kind::Mix ? kMixWt + count : count; }

// The step's tracks: instruments, then the held notes (status 0x90) or their ends (0x80).
void notes(uint8_t status) {
  switch (cur().kind) {
    case Kind::None:
    case Kind::Hits: break;  // hits()
    case Kind::Held: chord(0, count, status); break;
    case Kind::Mix:
      chord(0, kMixWt, status);
      chord(kMixWtTracks, count, status);
      break;
  }
}

void hits() {
  if (cur().kind != Kind::Hits) return;
  for (int t = 0; t < count; ++t) note(t, 0x90, 60);
}

// A strike ladder's chords again (note-ons only: each takes its track's oldest voice, its own).
void strike() {
  if (!cur().strike) return;
  if (cur().kind == Kind::Mix)
    chord(0, kMixWt, 0x90);
  else
    notes(0x90);
}

void startStep() {
  const Ladder& l = cur();
  synth->setVoiceBudget(l.pool ? mt::kVoiceBudgetTeensy : mt::kVoiceBudgetOff);
  for (int t = 0; t < mt::kTracks; ++t) {
    mt::TrackCfg& tr = model->tracks[t];
    tr.out = mt::TrackOut::Int;
    tr.instr = l.kind == Kind::Hits                     ? l.instr + t % kDrumKinds
               : l.kind == Kind::Mix && t < kMixWtTracks ? l.mix
                                                         : l.instr;
  }
  notes(0x90);
  hits();
}

// Next step of the ladder, or a rest before the next ladder.
void advance(bool over) {
  notes(0x80);
  const Ladder& l = cur();
  if (!over && count < l.top) {  // its notes a block later: ends + starts in one block overflow kMaxEvents
    count = count + kStep < l.top ? count + kStep : l.top;
    pending = true;
    return;
  }
  if ((ladder = nextLadder(ladder + 1)) >= kLadderCount) {
    done = true;
    return;
  }
  count = cur().pool ? 0 : kStep;
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
    // The guard shrank the pool in the last ladder's over step: rest on (fx only, under kLoadLow)
    // until it has grown back, or the next ladder starts with a short cap (a false over).
    if (blocks < restFor || synth->voiceCap() < mt::kVoices) return;
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
    for (int i = 0; i < mt::kVoices; ++i) liveSum += synth->voice(i).on && !synth->voice(i).stolen;
    if (static_cast<uint32_t>(synth->voiceCap()) < capMin) capMin = synth->voiceCap();
  }
  if (blocks % kHitEvery == 0) hits();
  // Not in the step's last block: its note-offs go out there (ons + offs would overflow kMaxEvents).
  if (blocks % kStrikeEvery == 0 && blocks < kStepBlocks) strike();
  if (blocks < kStepBlocks) return;
  const uint32_t avg = n ? static_cast<uint32_t>(sum / n) : 0;
  const uint32_t voices = n ? (voiceSum + n / 2) / n : 0;
  const uint32_t live = n ? (liveSum + n / 2) / n : 0;
  const int want = played();
  // A pool ladder plays fewer voices than requested by design: only the CPU and the guard count.
  const bool over = pct(avg) >= 100.f * mt::Synth::kLoadHigh || peak >= SynthStream::blockCycles() ||
                    (cur().pool ? capMin < static_cast<uint32_t>(mt::kVoices)
                                : capMin < static_cast<uint32_t>(want) || static_cast<int>(voices) < want);
  res = {ladder, count, want, avg, peak, voices, live, capMin, over};
  if (!over) limits[ladder] = {want, pct(avg), pct(peak), live};
  resSeq = resSeq + 1;
  blocks = n = peak = voiceSum = liveSum = 0;
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
  char live[24] = "";
  if (kLadders[r.ladder].pool) snprintf(live, sizeof live, PSTR(", live %2lu"), static_cast<unsigned long>(r.live));
  link::log(PSTR("max %-22s %2d voices: avg %5.1f %% peak %5.1f %%, sounding %2lu%s, cap min %2lu%s"), kLadders[r.ladder].name,
            r.played, pct(r.avgCycles), pct(r.peakCycles), static_cast<unsigned long>(r.voices), live,
            static_cast<unsigned long>(r.cap), r.over ? PSTR("  <- over") : "");
  if (!profBlocks) return;
  char line[220];
  int k = 0;
  const float block = static_cast<float>(SynthStream::blockCycles());
  for (int i = 0; i < mt::Synth::kProfStages && k < static_cast<int>(sizeof line) - 24; ++i) {
    const float p = 100.f * prof[i] / profBlocks / block;
    if (p >= 0.05f) k += snprintf(line + k, sizeof line - k, PSTR(" %s %.1f"), mt::Synth::profName(i), p);
  }
  link::log(PSTR("  profile %%:%s"), line);
}

void summary() {
  link::log(PSTR("max bench done (group %d, voices %d, heavy %d, guard at %.0f %%, stack free %lu B):"), BENCH_MAX_GROUP,
            mt::kVoices, mt::kFmVoiceMax, 100.f * mt::Synth::kLoadHigh, static_cast<unsigned long>(stackmark::minFree()));
  for (int i = nextLadder(0); i < kLadderCount; i = nextLadder(i + 1)) {
    const Limit& l = limits[i];
    if (l.clean < 0)
      link::log(PSTR("  %-22s none clean"), kLadders[i].name);
    else if (kLadders[i].pool)
      link::log(PSTR("  %-22s %2d voices requested, %2lu live (avg %.1f %%, peak %.1f %%, budget %d)"), kLadders[i].name,
                l.clean, static_cast<unsigned long>(l.live), l.avg, l.peak, mt::kVoiceBudgetTeensy);
    else
      link::log(PSTR("  %-22s %2d voices (avg %.1f %%, peak %.1f %%)%s"), kLadders[i].name, l.clean, l.avg, l.peak,
                kLadders[i].kind != Kind::None && l.clean == (kLadders[i].kind == Kind::Mix ? kMixWt : 0) +
                                                                kLadders[i].top
                    ? PSTR("  (ladder top, not the CPU)")
                    : "");
  }
}

void setFx(mt::Instrument& in) {
  in.rsend = 100;
  in.send = 100;
}

// The master / send fx all on (as AUDIO_BENCH_FX on main): drive 100 and a MOD send on every track's
// insert, delay TAPE (wear 100), reverb SHIMMER (+12), MOD bus ENSEMBLE, master EQ (low +6 dB,
// high -3 dB).
void masterFx(mt::SynthModel& m) {
  for (auto& c : m.tracks) {
    c.drive = 100;
    c.modSend = 90;
  }
  m.dlyMode = static_cast<uint8_t>(mt::DlyMode::Tape);
  m.dlyChar = 100;
  m.rvbMode = static_cast<uint8_t>(mt::RvbMode::Shimmer);
  m.rvbShim = 80;
  m.modType = static_cast<uint8_t>(mt::ModType::Ensemble);
  m.eqLow = 96;
  m.eqHigh = 48;
}

// The instruments: one maker per slot (arg: the drum kind, the filter mode).
void makeDrum(mt::Instrument& in, int kind) {
  using mt::DrumMachine;
  static const DrumMachine kMachines[kDrumKinds] PROGMEM = {DrumMachine::Bd8, DrumMachine::Sd8, DrumMachine::Hh8,
                                                    DrumMachine::Cy9, DrumMachine::Cp8, DrumMachine::Tom9,
                                                    DrumMachine::Bd9, DrumMachine::Rs8};
  mt::instrSetType(in, mt::InstrType::Drum);
  mt::drumSetMachine(in, static_cast<uint8_t>(kMachines[kind]));
  in.macro[mt::kMacDec] = 110;  // still ringing at the choke
  in.vol = 40;
}

void makeFm(mt::Instrument& fm, int) {
  mt::instrSetType(fm, mt::InstrType::Fm);
  mt::fmSetMachine(fm, static_cast<uint8_t>(mt::FmMachine::Tone));
  fm.mono = false;
  fm.sustain = 127;
  fm.vol = 30;
}

// Group 2: arg | kMove adds a moving cutoff. The group 1 instruments hold theirs (CHIP: the env held
// at its peak; WT: the env settles after its decay), so VoiceFilter::set returns early on unchanged
// hz / reso and their ladders never time the per-control coefficient work (FORMANT's setFormant,
// LADDER / MS20 / COMB set). LFO 1 triangle on Cutoff, +-24 steps (~+-1.8 octaves) at ~6 Hz, one
// phase for the instrument (Retrig OFF): the cutoff changes every control tick.
constexpr int kMove = 0x100;
void moveCutoff(mt::Instrument& in, int arg) {
  if (!(arg & kMove)) return;
  in.lfoWave = static_cast<uint8_t>(mt::LfoWave::Tri);
  in.lfoRate = 96;  // lfoHz: ~6.3 Hz
  in.lfoDepth = 24;
  in.lfoDest = static_cast<uint8_t>(mt::LfoDest::Cutoff);
  in.lfoSync = mt::kLfoFree;
}

// WT SYNTH through LP; group 2's fold variants on top of it.
void makeWt(mt::Instrument& w, int arg) {
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
  moveCutoff(w, arg);
}

// arg: the filter mode | kMove, | kNoRing / kNoFold to leave one of them at 0.
constexpr int kNoRing = 0x200, kNoFold = 0x400;
void makeWtFold(mt::Instrument& w, int arg) {
  makeWt(w, arg);
  w.synFold = arg & kNoFold ? 0 : 100;
  w.synRing = arg & kNoRing ? 0 : 64;
  w.fltMode = static_cast<uint8_t>(arg & 0xFF);
}

// CHIP saw through a resonant filter (arg: the mode | kMove), its envelope held at the peak (fDec 0).
void makeChip(mt::Instrument& chip, int arg) {
  chip.wave = static_cast<uint8_t>(mt::Wave::Saw);
  chip.sustain = 127;
  chip.mono = false;
  chip.fltMode = static_cast<uint8_t>(arg & 0xFF);
  chip.cutoff = 60;
  chip.reso = 100;
  chip.fenv = 40;
  chip.fDec = 0;
  chip.vol = 30;
  moveCutoff(chip, arg);
}

// Group 3: a MACRO model (arg) at TIMBRE 0.7 / COLOR 0.6 through LP, held, no ENV>TMB.
void makeMacro(mt::Instrument& in, int model) {
  mt::instrSetType(in, mt::InstrType::Macro);
  in.machine = static_cast<uint8_t>(model);
  in.macro[mt::kMacTimbre] = 89;  // 0.7
  in.macro[mt::kMacColor] = 76;   // 0.6
  in.macro[mt::kMacEnvTmb] = 64;  // off
  in.sustain = 127;
  in.mono = false;
  in.fltMode = static_cast<uint8_t>(mt::FltMode::Lp);
  in.cutoff = 90;
  in.reso = 40;
  in.vol = 30;
}

// Group 4: a STRING model (arg) plucked: DAMP 100 (rings through the 1/8), BRIGHT 0.7, STRUCT 0.6, POS
// 0.3, EXC 100 (a noise burst of ~13 ms), sustain held (the natural decay), through LP 90 / reso 40.
void makeString(mt::Instrument& in, int model) {
  mt::instrSetType(in, mt::InstrType::String);
  in.machine = static_cast<uint8_t>(model);
  in.macro[mt::kMacDamp] = 100;
  in.macro[mt::kMacBright] = 89;  // 0.7
  in.macro[mt::kMacStruct] = 76;  // 0.6
  in.macro[mt::kMacPos] = 38;     // 0.3
  in.macro[mt::kMacExc] = 100;    // 0.79: 0.5 + 20 e^2 = 12.9 ms
  in.sustain = 127;
  in.mono = false;
  in.fltMode = static_cast<uint8_t>(mt::FltMode::Lp);
  in.cutoff = 90;
  in.reso = 40;
  in.vol = 30;
}

struct InstrDef {
  int slot;
  void (*make)(mt::Instrument&, int);
  int arg;
};
constexpr int kLp = static_cast<int>(mt::FltMode::Lp);
constexpr InstrDef kInstrs[] PROGMEM = {
    {kDrumInstr + 0, makeDrum, 0},
    {kDrumInstr + 1, makeDrum, 1},
    {kDrumInstr + 2, makeDrum, 2},
    {kDrumInstr + 3, makeDrum, 3},
    {kDrumInstr + 4, makeDrum, 4},
    {kDrumInstr + 5, makeDrum, 5},
    {kDrumInstr + 6, makeDrum, 6},
    {kDrumInstr + 7, makeDrum, 7},
    {kFmInstr, makeFm, 0},
    {kWtInstr, makeWt, 0},
    {kChipInstr, makeChip, kLp},
    {kChipLfoInstr, makeChip, kLp | kMove},
    {kWtLfoInstr, makeWt, kMove},
    {kChipLadderInstr, makeChip, static_cast<int>(mt::FltMode::Ladder) | kMove},
    {kChipMs20Instr, makeChip, static_cast<int>(mt::FltMode::Ms20) | kMove},
    {kChipCombInstr, makeChip, static_cast<int>(mt::FltMode::Comb) | kMove},
    {kChipFormantInstr, makeChip, static_cast<int>(mt::FltMode::Formant) | kMove},
    {kWtFoldInstr, makeWtFold, kLp | kMove},
    {kWtFoldOnlyInstr, makeWtFold, kLp | kMove | kNoRing},
    {kWtRingInstr, makeWtFold, kLp | kMove | kNoFold},
    {kWtFxInstr, makeWtFold, static_cast<int>(mt::FltMode::Ladder) | kMove},
    {kMacroInstr + 0, makeMacro, 0},
    {kMacroInstr + 1, makeMacro, 1},
    {kMacroInstr + 2, makeMacro, 2},
    {kMacroInstr + 3, makeMacro, 3},
    {kMacroInstr + 4, makeMacro, 4},
    {kMacroInstr + 5, makeMacro, 5},
    {kMacroInstr + 6, makeMacro, 6},
    {kMacroInstr + 7, makeMacro, 7},
    {kStringInstr + 0, makeString, 0},
    {kStringInstr + 1, makeString, 1},
};
static_assert(kDrumKinds == 8, "kInstrs lists 8 drums");

}  // namespace

void begin(mt::SynthModel& m, mt::Synth& s, SynthStream& out) {
  model = &m;
  synth = &s;
  for (const InstrDef& d : kInstrs) {
    mt::Instrument& in = m.instruments[d.slot];
    in = mt::Instrument();
    d.make(in, d.arg);
    setFx(in);
  }
  m.dlyFb = 100;
  m.dlyLevel = 100;
  m.rvbLevel = 100;
  m.compAmt = 100;
  m.scTrack = 1;
  masterFx(m);
  link::log(PSTR("max bench: group %d (%d ladders), voices %d, heavy %d; ladders of +%d voices, 4 s per step, "
            "from 15 s after boot"), BENCH_MAX_GROUP, groupCount(), mt::kVoices, mt::kFmVoiceMax, kStep);
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
