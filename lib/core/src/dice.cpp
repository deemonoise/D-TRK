#include "dice.h"
#include <stdio.h>
#include <string.h>
#include "fill.h"
#include "gen_util.h"
#include "preset_io.h"
#include "presets_factory.h"
#include "rng.h"

namespace mt {
namespace {

static_assert(kInstruments <= 32 && kTracks <= 16, "dice masks");

constexpr DiceGenre kGenres[] = {
    {"HOUSE", 118, 126, {ScaleType::Minor, ScaleType::Dorian, ScaleType::Major}, {"909 KIT", "HYBRID"}},
    {"TECHNO", 124, 134, {ScaleType::Minor, ScaleType::Phrygian, ScaleType::Dorian}, {"909 HARD", "909 KIT"}},
    {"TRANCE", 132, 140, {ScaleType::Minor, ScaleType::HarmonicMinor, ScaleType::Minor}, {"909 KIT", "909 HARD"}},
    {"DNB", 168, 176, {ScaleType::Minor, ScaleType::Dorian, ScaleType::Phrygian}, {"HARD KIT", "LOFI KIT"}},
    {"HIPHOP", 84, 96, {ScaleType::Dorian, ScaleType::Minor, ScaleType::Mixolydian}, {"808 DEEP", "LOFI KIT"}},
    {"SYNTHPOP", 100, 118, {ScaleType::Major, ScaleType::Mixolydian, ScaleType::Lydian}, {"808 KIT", "FM KIT"}},
    {"DOWNTEMPO", 70, 90, {ScaleType::Dorian, ScaleType::Minor, ScaleType::Major}, {"DUB KIT", "LOFI KIT"}},
};
constexpr int kGenreN = sizeof(kGenres) / sizeof(kGenres[0]);
constexpr const char* kRoleNames[kDiceRoles] = {"DRUMS", "BASS", "CHORDS", "LEAD"};

int8_t rollTempo(DiceParts& d, Rng& r) {
  const int g = static_cast<int>(r.below(kGenreN));
  const DiceGenre& k = kGenres[g];
  d.bpm = static_cast<uint16_t>(k.bpmLo + r.below(static_cast<uint32_t>(k.bpmHi - k.bpmLo + 1)));
  d.scaleType = static_cast<uint8_t>(k.scales[r.below(kDiceScales)]);
  d.scaleRoot = static_cast<uint8_t>(r.below(12));
  return static_cast<int8_t>(g);
}

// Everything a roll may write, bit for bit (memcpy: padding too, so an undo compares equal).
void load(const Project& p, int pattern, DiceParts& d) {
  d.bpm = p.bpm;
  d.scaleRoot = p.scaleRoot;
  d.scaleType = p.scaleType;
  memcpy(d.tracks, p.tracks, sizeof(d.tracks));
  memcpy(d.instruments, p.instruments, sizeof(d.instruments));
  memcpy(d.gen, p.gen, sizeof(d.gen));
  memcpy(d.var, p.var, sizeof(d.var));
  memcpy(&d.pat, &p.patterns[pattern], sizeof(Pattern));
}

bool isRoleTrack(int t) {
  for (int k = 0; k < kDiceRoles; ++k)
    if (kDiceTrack[k] == t) return true;
  return false;
}

// Marks track t's instrument (an INT track plays it; lanesOnly: not that one) and, for a KIT, the
// instruments of its lanes.
void markPlayed(const TrackCfg* tracks, const Instrument* instruments, int t, bool* used, bool lanesOnly = false) {
  if (tracks[t].out != TrackOut::Int) return;
  const int i = tracks[t].instr % kInstruments;
  if (!lanesOnly) used[i] = true;
  if (instruments[i].type != InstrType::Kit) return;
  for (const KitLane& l : instruments[i].kit)
    if (l.instr < kInstruments) used[l.instr] = true;
}

// diceSlots on a copy of the track settings and instruments.
void slotsOf(const TrackCfg* tracks, const Instrument* instruments, uint8_t out[kDiceRoles]) {
  bool used[kInstruments] = {};  // played by a track the roll leaves alone
  bool own[kInstruments] = {};   // a role track's instrument: kept for that role
  for (int t = 0; t < kTracks; ++t)
    if (!isRoleTrack(t)) markPlayed(tracks, instruments, t, used);
  for (int k = 0; k < kDiceRoles; ++k) {
    own[tracks[kDiceTrack[k]].instr % kInstruments] = true;
    if (k > 0) markPlayed(tracks, instruments, kDiceTrack[k], used, true);  // a KIT on a melodic role
  }
  // DRUMS: the kit and its whole block, none of them played elsewhere.
  const int kit = tracks[kDiceTrack[0]].instr % kInstruments;
  const int block = factoryKitBlock(kit);
  bool free = !used[kit];
  for (int l = 0; l < kKitLanes; ++l) free &= !used[block + l];
  if (free) {
    out[0] = static_cast<uint8_t>(kit);
    used[kit] = true;
    for (int l = 0; l < kKitLanes; ++l) used[block + l] = true;
  } else {
    out[0] = kNoInstr;
    markPlayed(tracks, instruments, kDiceTrack[0], used);  // the kit stays, so do its lanes
  }
  for (int k = 1; k < kDiceRoles; ++k) {
    int s = tracks[kDiceTrack[k]].instr % kInstruments;
    if (used[s]) {
      s = 0;
      while (s < kInstruments && (used[s] || own[s])) ++s;
    }
    if (s >= kInstruments) {
      out[k] = kNoInstr;  // no free slot: the role is skipped
      continue;
    }
    out[k] = static_cast<uint8_t>(s);
    used[s] = true;
  }
}

bool roleCategory(DiceRole role, const char* c) {
  switch (role) {
    case DiceRole::Bass: return strcmp(c, "BASS") == 0;
    case DiceRole::Chords: return strcmp(c, "PAD") == 0 || strcmp(c, "KEYS") == 0;
    case DiceRole::Lead: return strcmp(c, "LEAD") == 0 || strcmp(c, "PLUCK") == 0;
    default: return false;
  }
}

// The role plays the notes as written: FM only as TONE (the other machines are one-shot drums or a
// chord of their own), CHORDS needs polyphony.
bool roleFits(DiceRole role, const Instrument& m) {
  if (m.type == InstrType::Fm && m.machine != static_cast<uint8_t>(FmMachine::Tone)) return false;
  return !(role == DiceRole::Chords && m.mono);
}

// A random factory CHIP / FM / SYNTH preset of the role into out (tmp: scratch). Two passes: count,
// then build the drawn one. False when none fits (out unchanged).
bool pickSound(DiceRole role, Rng& r, Instrument& tmp, Instrument& out) {
  static constexpr InstrType kTypes[] = {InstrType::Chip, InstrType::Fm, InstrType::Synth};
  int n = 0;
  for (InstrType t : kTypes)
    for (int i = 0; i < factoryCount(t); ++i) {
      if (!roleCategory(role, factoryPreset(t, i).category)) continue;
      factoryBuild(t, i, tmp);
      n += roleFits(role, tmp);
    }
  if (n == 0) return false;
  int k = static_cast<int>(r.below(static_cast<uint32_t>(n)));
  for (InstrType t : kTypes)
    for (int i = 0; i < factoryCount(t); ++i) {
      if (!roleCategory(role, factoryPreset(t, i).category)) continue;
      factoryBuild(t, i, tmp);
      if (!roleFits(role, tmp) || k-- > 0) continue;
      memcpy(&out, &tmp, sizeof(Instrument));
      return true;
    }
  return false;
}

// Factory KIT index: kDiceKitPct % one of the genre's kits (genre >= 0), else any.
int pickKit(int genre, Rng& r) {
  const int n = factoryCount(InstrType::Kit);
  if (genre >= 0 && static_cast<int>(r.below(100)) < kDiceKitPct) {
    const char* name = kGenres[genre].kits[r.below(kDiceKits)];
    for (int i = 0; i < n; ++i)
      if (strcmp(factoryPreset(InstrType::Kit, i).name, name) == 0) return i;
  }
  return static_cast<int>(r.below(static_cast<uint32_t>(n)));
}

void rollSounds(DiceParts& d, int genre, Rng& r, DiceSnap& s) {
  uint8_t slot[kDiceRoles];
  slotsOf(d.tracks, d.instruments, slot);
  for (int k = 0; k < kDiceRoles; ++k) {
    const DiceRole role = static_cast<DiceRole>(k);
    const int t = kDiceTrack[k];
    if (slot[k] == kNoInstr) continue;
    if (role == DiceRole::Drums) {
      const int i = pickKit(genre, r);
      factoryBuildKit(i, d.instruments, slot[k]);
      const int block = factoryKitBlock(slot[k]);
      const int n = factoryPreset(InstrType::Kit, i).drumCount;
      for (int l = 0; l < n && l < kKitLanes; ++l) s.instrMask |= 1u << (block + l);
    } else if (!pickSound(role, r, s.work.tmp, d.instruments[slot[k]])) {
      continue;
    }
    s.instrMask |= 1u << slot[k];
    TrackCfg& c = d.tracks[t];
    c.out = TrackOut::Int;
    c.instr = slot[k];
    snprintf(c.name, sizeof(c.name), "%s", kRoleNames[k]);
    takePresetInsert(d.instruments[slot[k]], c);  // as the preset browser: the track takes the recommended insert
    s.cfgMask |= static_cast<uint16_t>(1u << t);
  }
}

// Clears track t of the pattern for a role (every step, polymeter, speed, its GEN).
void clearTrack(DiceParts& d, int t, DiceSnap& s) {
  for (Step& st : d.pat.steps[t]) st = Step();
  d.pat.trackLen[t] = 0;
  d.pat.trackSpeed[t] = 0;
  d.gen[t].mode = static_cast<uint8_t>(GenMode::Off);
  d.var[t].mode = static_cast<uint8_t>(VarMode::Off);
  s.patMask |= static_cast<uint16_t>(1u << t);
}

bool lanePlays(const Instrument& kit, int l) {
  return kit.kit[l].instr < kInstruments || kit.kit[l].sample[0];
}

// Euclid hits on KIT lanes (kick, snare, hats, clap, an extra), 16-step cycles over the pattern.
void rollDrums(Pattern& pt, int t, int len, const Instrument& kit, Rng& r, const bool* drum) {
  const Sel sel{static_cast<uint8_t>(t), static_cast<uint8_t>(t), 0, static_cast<uint8_t>(len - 1)};
  auto lane = [&](int l, int hits, int rot) {
    if (l < 0 || l >= kKitLanes || hits <= 0 || !lanePlays(kit, l)) return;
    FillSpec f;
    f.where = FillWhere::Euclid;
    f.hits = static_cast<uint8_t>(hits);
    f.length = 16;
    f.rotation = static_cast<int8_t>(rot);
    f.target = FillTarget::Note;
    f.mode = FillMode::Overwrite;
    f.lane = static_cast<uint8_t>(l);
    applyFill(pt, sel, f, 0, ScaleType::Chromatic, drum);  // a drum Note sets the lane bit
  };
  static constexpr uint8_t kKick[] = {4, 4, 3, 5};
  lane(0, kKick[r.below(4)], 0);                                            // kick, from step 1
  if (r.below(100) < 70) lane(1, 2, 4);                                     // backbeat: 5 and 13
  else lane(1, 3, 2 + static_cast<int>(r.below(3)));                        // broken snare
  lane(2, 4 + static_cast<int>(r.below(9)), static_cast<int>(r.below(4)));  // closed hat 4..12
  if (r.below(100) < 40) lane(3, 2 + static_cast<int>(r.below(3)), 2);      // open hat, off-beat
  if (r.below(100) < 40) lane(4, 1 + static_cast<int>(r.below(2)), 4);      // clap
  if (r.below(100) < 50)
    lane(5 + static_cast<int>(r.below(3)), 3 + static_cast<int>(r.below(3)), static_cast<int>(r.below(16)));
}

// Out-of-scale notes (the bass' approach notes) go down to the nearest scale note.
void snapToScale(Pattern& pt, int t, uint8_t root, ScaleType sc) {
  for (Step& st : pt.steps[t]) {
    if (!st.hasNote()) continue;
    int n = st.note;
    for (int k = 0; k < 12 && n > 0 && !inScale(n, root, sc); ++k) --n;
    st.note = static_cast<uint8_t>(n);
  }
}

void rollPattern(DiceParts& d, Rng& r, DiceSnap& s) {
  Pattern& pt = d.pat;
  const int len = genClamp(pt.length, kMinSteps, kMaxSteps);
  bool drum[kTracks];
  for (int t = 0; t < kTracks; ++t) drum[t] = d.instruments[d.tracks[t].instr % kInstruments].type == InstrType::Kit;
  const int td = kDiceTrack[static_cast<int>(DiceRole::Drums)];
  if (drum[td]) {
    clearTrack(d, td, s);
    rollDrums(pt, td, len, d.instruments[d.tracks[td].instr % kInstruments], r, drum);
  }

  const uint8_t root = static_cast<uint8_t>(d.scaleRoot % 12);
  const ScaleType sc = d.scaleType < static_cast<uint8_t>(ScaleType::Count) ? static_cast<ScaleType>(d.scaleType)
                                                                             : ScaleType::Chromatic;
  const int tb = kDiceTrack[static_cast<int>(DiceRole::Bass)];
  const int tc = kDiceTrack[static_cast<int>(DiceRole::Chords)];
  const int tl = kDiceTrack[static_cast<int>(DiceRole::Lead)];
  auto range = [len](int t) {
    return Sel{static_cast<uint8_t>(t), static_cast<uint8_t>(t), 0, static_cast<uint8_t>(len - 1)};
  };
  DiceWork& w = s.work;

  // CHORDS: a random progression (randomProgression) in a random factory rhythm.
  static constexpr ChordFlavor kFlavors[] = {ChordFlavor::Triad, ChordFlavor::Seventh, ChordFlavor::Sus};
  ChordSpec cs;
  cs.random = true;
  cs.chords = len >= 16 ? 4 : 2;
  cs.octave = 4;
  cs.flavor = kFlavors[r.below(3)];
  cs.voicing = ChordVoicing::Close;
  cs.length = 0;  // AUTO: the chords share the pattern
  cs.dest = static_cast<uint8_t>(tc);
  cs.rhythm = static_cast<uint8_t>(r.below(static_cast<uint32_t>(chordRhythmCount())));
  cs.seed = r.next();
  const Progression prog{};  // empty: cs.random draws it
  parseArpPattern(chordRhythmText(cs.rhythm), w.rhythm);
  bool chords = false;
  if (!drum[tc]) {
    clearTrack(d, tc, s);
    chords = applyChords(pt, range(tc), cs, prog, w.rhythm, root, sc, drum);
    snapToScale(pt, tc, root, sc);
  }

  // BASS: a random factory pattern against the same progression (one seed, as FILL's PROGRESSION).
  BassSpec bs;
  bs.source = BassSource::Progression;
  bs.octave = 2;
  bs.dest = static_cast<uint8_t>(tb);
  bs.pattern = static_cast<uint8_t>(r.below(static_cast<uint32_t>(bassFactoryCount())));
  bs.seed = cs.seed;
  parseBassPattern(bassFactoryText(bs.pattern), w.bass);
  if (!drum[tb]) {
    const int segN = layoutChords(prog, cs, len, root, sc, w.segs);
    clearTrack(d, tb, s);
    applyBass(pt, pt, range(tb), bs, w.bass, root, sc, w.segs, segN, drum);
    snapToScale(pt, tb, root, sc);
  }

  // LEAD: a random factory arp over the CHORDS track (else the tonic triad).
  static constexpr ArpMode kModes[] = {ArpMode::Up,     ArpMode::Down,   ArpMode::UpDown,
                                       ArpMode::DownUp, ArpMode::Random, ArpMode::Pedal};
  ArpSpec as;
  as.source = chords ? ArpSource::Selection : ArpSource::Chord;
  as.root = static_cast<uint8_t>(60 + root);
  as.chord = kChordTriad;
  as.dest = static_cast<uint8_t>(tl);
  as.mode = kModes[r.below(6)];
  as.octaves = static_cast<uint8_t>(1 + r.below(2));
  as.pattern = static_cast<uint8_t>(r.below(static_cast<uint32_t>(arpFactoryCount())));
  as.seed = r.next();
  parseArpPattern(arpFactoryText(as.pattern), w.arp);
  if (!drum[tl]) {
    clearTrack(d, tl, s);
    // SELECTION reads the CHORDS track (sel.t0..t1) of the same pattern and writes only Dest.
    applyArp(pt, pt, range(chords ? tc : tl), as, w.arp, root, sc, drum);
    snapToScale(pt, tl, root, sc);
  }
}

// Byte swap without a temporary (a track's steps are kilobytes; the UI task's stack is small).
void swapBytes(void* a, void* b, size_t n) {
  uint8_t* x = static_cast<uint8_t*>(a);
  uint8_t* y = static_cast<uint8_t*>(b);
  for (size_t i = 0; i < n; ++i) {
    const uint8_t t = x[i];
    x[i] = y[i];
    y[i] = t;
  }
}

// Swaps the parts the roll wrote (s.layers, the masks) between p and s.parts.
void swapWritten(Project& p, DiceSnap& s) {
  DiceParts& d = s.parts;
  if (s.layers & kDiceTempo) {
    swapBytes(&p.bpm, &d.bpm, sizeof(p.bpm));
    swapBytes(&p.scaleRoot, &d.scaleRoot, 1);
    swapBytes(&p.scaleType, &d.scaleType, 1);
  }
  Pattern& pt = p.patterns[s.pattern];
  for (int t = 0; t < kTracks; ++t) {
    if (s.cfgMask & (1u << t)) {
      swapBytes(&p.tracks[t], &d.tracks[t], sizeof(TrackCfg));
      // Mute / solo stay live (the sequencer writes them too: song mutes, rewind): swapped back.
      swapBytes(&p.tracks[t].mute, &d.tracks[t].mute, sizeof(bool));
      swapBytes(&p.tracks[t].solo, &d.tracks[t].solo, sizeof(bool));
    }
    if (s.patMask & (1u << t)) {
      swapBytes(pt.steps[t], d.pat.steps[t], sizeof(pt.steps[t]));
      swapBytes(&pt.trackLen[t], &d.pat.trackLen[t], sizeof(pt.trackLen[t]));
      swapBytes(&pt.trackSpeed[t], &d.pat.trackSpeed[t], sizeof(pt.trackSpeed[t]));
      swapBytes(&p.gen[t], &d.gen[t], sizeof(GenCfg));
      swapBytes(&p.var[t], &d.var[t], sizeof(VarCfg));
    }
  }
  for (int i = 0; i < kInstruments; ++i)
    if (s.instrMask & (1u << i)) swapBytes(&p.instruments[i], &d.instruments[i], sizeof(Instrument));
}

}  // namespace

const char* diceRoleName(DiceRole r) {
  const int i = static_cast<int>(r);
  return kRoleNames[i >= 0 && i < kDiceRoles ? i : 0];
}

int diceGenreCount() { return kGenreN; }

const DiceGenre& diceGenre(int i) { return kGenres[i >= 0 && i < kGenreN ? i : 0]; }

int diceGenreOfBpm(uint16_t bpm) {
  for (int i = 0; i < kGenreN; ++i)
    if (bpm >= kGenres[i].bpmLo && bpm <= kGenres[i].bpmHi) return i;
  return -1;
}

void diceSlots(const Project& p, uint8_t out[kDiceRoles]) { slotsOf(p.tracks, p.instruments, out); }

DiceResult diceDraft(const Project& p, uint8_t layers, int pattern, uint32_t seed, DiceSnap& s) {
  DiceResult res;
  layers &= kDiceAll;
  if (pattern < 0 || pattern >= kPatterns) pattern = 0;
  s.layers = 0;  // parts are about to be overwritten: the previous undo is gone
  s.drafted = layers;
  s.pattern = static_cast<uint8_t>(pattern);
  s.cfgMask = 0;
  s.instrMask = 0;
  s.patMask = 0;
  load(p, pattern, s.parts);
  Rng r(seed);
  if (layers & kDiceTempo) res.genre = rollTempo(s.parts, r);
  if (layers & kDiceSounds) rollSounds(s.parts, res.genre >= 0 ? res.genre : diceGenreOfBpm(s.parts.bpm), r, s);
  if (layers & kDicePattern) rollPattern(s.parts, r, s);
  res.written = static_cast<uint16_t>(s.cfgMask | s.patMask);
  return res;
}

uint8_t diceApply(Project& p, DiceSnap& s) {
  const uint8_t l = s.drafted;
  if (!l) return 0;  // nothing drafted: the applied roll's undo stays
  s.drafted = 0;
  s.layers = l;
  swapWritten(p, s);
  return l;
}

DiceResult diceRoll(Project& p, uint8_t layers, int pattern, uint32_t seed, DiceSnap& s) {
  const DiceResult res = diceDraft(p, layers, pattern, seed, s);
  diceApply(p, s);
  return res;
}

uint8_t diceUndo(Project& p, DiceSnap& s) {
  const uint8_t l = s.layers;
  if (!l) return 0;
  swapWritten(p, s);
  s.layers = 0;
  s.cfgMask = 0;
  s.instrMask = 0;
  s.patMask = 0;
  return l;
}

}  // namespace mt
