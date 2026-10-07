#include "demos.h"
#include <string.h>
#include <initializer_list>
#include "presets_factory.h"
#include "scale.h"

namespace mt {
namespace {

// ---- Building blocks ----

// Instrument slot i from the factory preset (type, name); a missing preset leaves the slot as is.
Instrument& ins(Project& p, int i, InstrType t, const char* name) {
  for (int k = 0; k < factoryCount(t); ++k)
    if (strcmp(factoryPreset(t, k).name, name) == 0) {
      factoryBuild(t, k, p.instruments[i]);
      break;
    }
  return p.instruments[i];
}

void track(Project& p, int t, const char* name, int instr, uint8_t vol = 100, uint8_t gate = 50) {
  TrackCfg& c = p.tracks[t];
  strncpy(c.name, name, sizeof(c.name) - 1);
  c.name[sizeof(c.name) - 1] = 0;
  c.out = TrackOut::Int;
  c.instr = static_cast<uint8_t>(instr);
  c.vol = vol;
  c.defGate = gate;
}

struct Drum {
  InstrType type;
  const char* name;
};

// KIT in slot k named name, lane l playing the preset drums[l] from slot first + l. Lane l answers
// note 60 + l, so each drum is transposed down by l to sound at its own pitch.
void kit(Project& p, int k, const char* name, int first, const Drum* drums, int n) {
  Instrument& m = p.instruments[k];
  m = Instrument();
  instrSetType(m, InstrType::Kit);
  strncpy(m.name, name, sizeof(m.name) - 1);
  for (int l = 0; l < n && l < kKitLanes; ++l) {
    Instrument& d = ins(p, first + l, drums[l].type, drums[l].name);
    d.transpose = static_cast<int8_t>(d.transpose - l);
    m.kit[l].instr = static_cast<uint8_t>(first + l);
  }
}

Pattern& pat(Project& p, int i, int len, uint8_t swing = 50) {
  Pattern& pt = p.patterns[i];
  pt.clear();
  pt.length = static_cast<uint8_t>(len);
  pt.swing = swing;
  return pt;
}

int effVel(uint8_t v) { return v ? v : 100; }

// Drum lane hits, one char per step, repeated over [from, to) (to < 0: the pattern's end):
// 'x' hit (the track's velocity), 'X' accent, 'o' ghost, anything else none. Lanes of one step share
// its velocity: the louder one wins.
void hits(Pattern& pt, int t, int lane, const char* s, int from = 0, int to = -1) {
  const int n = static_cast<int>(strlen(s));
  if (to < 0) to = pt.length;
  for (int i = from; i < to; ++i) {
    const char c = s[(i - from) % n];
    if (c != 'x' && c != 'X' && c != 'o') continue;
    const uint8_t v = c == 'X' ? 127 : c == 'o' ? 45 : 0;
    Step& st = pt.steps[t][i];
    if (!st.hasNote() || effVel(v) > effVel(st.note)) st.note = v;
    st.vel = static_cast<uint8_t>(st.vel | (1u << lane));
  }
}

// "C4" = 60, "F#3", "Bb2"; 0xFF if not a note.
uint8_t noteOf(const char* s, int len) {
  static const int8_t kSemi[7] = {9, 11, 0, 2, 4, 5, 7};  // A..G
  if (len < 2 || s[0] < 'A' || s[0] > 'G') return 0xFF;
  int n = kSemi[s[0] - 'A'];
  int i = 1;
  if (s[i] == '#') ++n, ++i;
  else if (s[i] == 'b') --n, ++i;
  if (i >= len || s[i] < '0' || s[i] > '9') return 0xFF;
  n += (s[i] - '0' + 1) * 12;
  return n >= 0 && n <= 127 ? static_cast<uint8_t>(n) : 0xFF;
}

// Melodic steps from step `at`, one space-separated token per step: "." empty, "^" note off, a note
// ("A4", "C#5", "Bb2"), optionally followed by '!' (accent, velocity 127) or '\'' (soft, 70).
void notes(Pattern& pt, int t, int at, const char* s) {
  int step = at;
  while (*s && step < kMaxSteps) {
    while (*s == ' ') ++s;
    if (!*s) break;
    const char* b = s;
    while (*s && *s != ' ') ++s;
    int len = static_cast<int>(s - b);
    Step& st = pt.steps[t][step++];
    if (b[0] == '.') continue;
    if (b[0] == '^') {
      st.note = kNoteOff;
      continue;
    }
    uint8_t vel = 0;
    if (b[len - 1] == '!') vel = 127, --len;
    else if (b[len - 1] == '\'') vel = 70, --len;
    const uint8_t n = noteOf(b, len);
    if (n == 0xFF) continue;
    st.note = n;
    st.vel = vel;
  }
}

void note(Pattern& pt, int t, int step, uint8_t n, uint8_t vel = 0) {
  pt.steps[t][step].note = n;
  pt.steps[t][step].vel = vel;
}

// Puts fx f = v in the step's slot for f, else its first free slot.
void fx(Pattern& pt, int t, int step, Fx f, uint8_t v) {
  Step& st = pt.steps[t][step];
  for (FxSlot& s : st.fx)
    if (s.cmd == f) {
      s.val = v;
      return;
    }
  for (FxSlot& s : st.fx)
    if (s.cmd == Fx::None) {
      s.cmd = f;
      s.val = v;
      return;
    }
}

// A chord (CHD) on root with gate GAT g.
void chord(Pattern& pt, int t, int step, uint8_t root, uint8_t chd, uint8_t g) {
  note(pt, t, step, root);
  fx(pt, t, step, Fx::CHD, chd);
  fx(pt, t, step, Fx::GAT, g);
}

// A drum roll over [from, to): a hit on lane every `every` steps, velocity rising lo..hi.
void roll(Pattern& pt, int t, int lane, int from, int to, int every, uint8_t lo, uint8_t hi) {
  const int n = (to - from + every - 1) / every;
  for (int k = 0; k < n; ++k) {
    Step& st = pt.steps[t][from + k * every];
    st.note = static_cast<uint8_t>(lo + (hi - lo) * k / (n > 1 ? n - 1 : 1));
    st.vel = static_cast<uint8_t>(st.vel | (1u << lane));
  }
}

struct Item {
  uint8_t pattern, rep;
  int8_t tr;
  uint8_t scene;
};

void song(Project& p, const Item* items, int n) {
  p.chainLen = static_cast<uint8_t>(n);
  for (int i = 0; i < n; ++i) {
    p.chain[i] = items[i].pattern;
    p.chainRep[i] = items[i].rep;
    p.chainTr[i] = items[i].tr;
    p.chainScene[i] = items[i].scene;
  }
  p.songMode = true;
}

void name(Project& p, const char* n) {
  strncpy(p.name, n, sizeof(p.name) - 1);
  p.name[sizeof(p.name) - 1] = 0;
}

constexpr uint8_t kGat800 = 200;  // GAT 800 %
constexpr uint8_t gat(int pct) {  // GAT value for pct (100..800 in 7 % units above 100)
  return static_cast<uint8_t>(pct <= 100 ? pct : 100 + (pct - 100) / 7);
}
constexpr uint8_t kArs2Up = 0x41;  // ARS: 2 octaves, up, one step per note

// ---- TRANCE: 138 BPM, A minor, Am F C G. Sidechained pads and bass, supersaw hook, arp, riser ----

void buildTrance(Project& p) {
  name(p, "DEMO-TRANCE");
  p.masterVol = 160;  // the mix peaks around -3 dBFS
  p.bpm = 138;
  p.scaleRoot = 9;
  p.scaleType = static_cast<uint8_t>(ScaleType::Minor);
  enum { kKick, kDrums, kBass, kPad, kLead, kArp, kFx };
  static const Drum kKickKit[] = {{InstrType::Drum, "BD909"}};
  static const Drum kDrumKit[] = {{InstrType::Drum, "CH909"}, {InstrType::Drum, "OH909"}, {InstrType::Drum, "CP909"},
                                  {InstrType::Drum, "SD909"}, {InstrType::Drum, "CY909"}};
  enum { kCh, kOh, kCp, kSd, kCy };
  kit(p, 0, "KICK", 16, kKickKit, 1);
  kit(p, 1, "DRUMS", 17, kDrumKit, 5);
  ins(p, 2, InstrType::Synth, "BASS");
  ins(p, 3, InstrType::Synth, "WARM PAD").rsend = 90;
  ins(p, 4, InstrType::Synth, "SUPERSAW").send = 55;
  ins(p, 5, InstrType::Synth, "PING");
  ins(p, 6, InstrType::Synth, "RISER");
  track(p, kKick, "KICK", 0, 127);
  p.instruments[0].kit[0].vol = 127;
  track(p, kDrums, "DRUMS", 1, 100);
  track(p, kBass, "BASS", 2, 105);
  track(p, kPad, "PAD", 3, 72);
  track(p, kLead, "LEAD", 4, 90, 128);  // ~300 %: the hook's notes join
  track(p, kArp, "ARP", 5, 62);
  track(p, kFx, "RISER", 6, 85);
  p.dlyTime = 3;
  p.dlyFb = 55;
  p.rvbSize = 100;
  p.rvbLevel = 85;
  p.compAmt = 45;
  p.scTrack = kKick + 1;  // the kick pumps everything else
  p.scDepth = 85;

  static const uint8_t kPadRoot[4] = {57, 53, 60, 55};   // A3 F3 C4 G3: Am F C G in A minor
  static const uint8_t kBassRoot[4] = {45, 41, 48, 43};  // A2 F2 C3 G2
  static const uint8_t kHook[4][6] = {{69, 72, 76, 74, 72, 71},   // A4 C5 E5 D5 C5 B4
                                      {69, 72, 77, 76, 72, 69},   // A4 C5 F5 E5 C5 A4
                                      {67, 72, 76, 79, 76, 72},   // G4 C5 E5 G5 E5 C5
                                      {71, 74, 79, 77, 74, 71}};  // B4 D5 G5 F5 D5 B4
  static const uint8_t kHookAt[6] = {0, 3, 6, 8, 11, 14};          // 3 + 3 + 2, twice
  auto kick = [](Pattern& pt, int from, int to) { hits(pt, kKick, 0, "x...x...x...x...", from, to); };
  auto hats = [](Pattern& pt, int from, int to) {
    hits(pt, kDrums, kCh, "xx.xxx.xxx.xxx.x", from, to);
    hits(pt, kDrums, kOh, "..x...x...x...x.", from, to);
  };
  auto pad = [](Pattern& pt) {
    for (int b = 0; b < 4; ++b) {
      chord(pt, kPad, b * 16, kPadRoot[b], kChordTriad, kGat800);
      chord(pt, kPad, b * 16 + 8, kPadRoot[b], kChordTriad, kGat800);
    }
  };
  auto bass = [](Pattern& pt) {
    for (int b = 0; b < 4; ++b)
      for (int q = 0; q < 4; ++q)
        for (int k = 1; k < 4; ++k) note(pt, kBass, b * 16 + q * 4 + k, kBassRoot[b], k == 2 ? 115 : 0);
  };
  auto arp = [](Pattern& pt, int fromBar) {
    for (int b = fromBar; b < 4; ++b) {
      note(pt, kArp, b * 16, kPadRoot[b]);
      fx(pt, kArp, b * 16, Fx::CHD, kChordTriad);
      fx(pt, kArp, b * 16, Fx::ARS, kArs2Up);
    }
  };
  auto hook = [&](Pattern& pt) {
    for (int b = 0; b < 4; ++b)
      for (int k = 0; k < 6; ++k) note(pt, kLead, b * 16 + kHookAt[k], kHook[b][k], k == 0 ? 120 : 0);
  };

  Pattern& intro = pat(p, 0, 64);
  kick(intro, 0, 64);
  hits(intro, kDrums, kOh, "..x...x...x...x.");
  hits(intro, kDrums, kCh, "xx.xxx.xxx.xxx.x", 32, 64);
  pad(intro);
  arp(intro, 2);

  Pattern& build = pat(p, 1, 64);
  kick(build, 0, 64);
  hats(build, 0, 48);
  hits(build, kDrums, kCp, "....x.......x...", 0, 48);
  roll(build, kDrums, kSd, 48, 64, 2, 50, 110);
  fx(build, kDrums, 60, Fx::RAT, 0x12);  // the last beat in 32nds, rising
  fx(build, kDrums, 62, Fx::RAT, 0x14);
  pad(build);
  bass(build);
  arp(build, 0);

  Pattern& main = pat(p, 2, 64);
  kick(main, 0, 64);
  hats(main, 0, 64);
  hits(main, kDrums, kCp, "....x.......x...");
  hits(main, kDrums, kCy, "X", 0, 1);
  pad(main);
  bass(main);
  arp(main, 0);
  hook(main);

  Pattern& brk = pat(p, 3, 64);
  pad(brk);
  hook(brk);
  for (int b = 0; b < 4; ++b)  // the hook's filter opens up over the breakdown
    for (int k = 0; k < 6; ++k) {
      const int at = b * 16 + kHookAt[k];
      fx(brk, kLead, at, Fx::FLT, static_cast<uint8_t>(60 + at));
    }
  note(brk, kFx, 32, 57);
  fx(brk, kFx, 32, Fx::TIE, 0);  // the riser holds to the drop
  roll(brk, kDrums, kSd, 48, 64, 1, 30, 120);

  Pattern& outro = pat(p, 4, 64);
  kick(outro, 0, 48);
  hits(outro, kDrums, kOh, "..x...x...x...x.", 0, 48);
  pad(outro);
  arp(outro, 0);

  static const Item kSong[] = {{0, 2, 0, 0}, {1, 2, 0, 0}, {2, 4, 0, 0}, {3, 1, 0, 0}, {2, 4, 0, 0}, {4, 1, 0, 0}};
  song(p, kSong, 6);
}

// ---- CHIPTUNE: 150 BPM, C major, C Am F G; a bridge on F G Em Am; the last chorus a tone up ----

void buildChip(Project& p) {
  name(p, "DEMO-CHIPTUNE");
  p.masterVol = 60;  // the mix peaks around -3 dBFS
  p.bpm = 150;
  p.scaleType = static_cast<uint8_t>(ScaleType::Major);
  enum { kDrums, kLead, kArp, kBass, kBell };
  static const Drum kKit[] = {{InstrType::Chip, "CHIPKICK"}, {InstrType::Chip, "NOIS SN"}, {InstrType::Chip, "NOIS HH"},
                              {InstrType::Chip, "NOIS OH"}};
  enum { kBd, kSd, kHh, kOh };
  kit(p, 0, "CHIPKIT", 16, kKit, 4);
  ins(p, 1, InstrType::Chip, "SQ LEAD").send = 25;
  ins(p, 2, InstrType::Chip, "ARP PLK");
  ins(p, 3, InstrType::Chip, "TRI BASS");
  Instrument& bell = ins(p, 4, InstrType::Chip, "WT BELL");
  bell.send = 50;
  bell.rsend = 30;
  track(p, kDrums, "DRUMS", 0, 85);
  track(p, kLead, "LEAD", 1, 90, 90);
  track(p, kArp, "ARP", 2, 75, 100);
  track(p, kBass, "BASS", 3, 110, 80);
  track(p, kBell, "BELL", 4, 80, 100);
  p.dlyTime = 3;
  p.dlyFb = 40;
  p.rvbSize = 50;
  p.rvbLevel = 50;

  // Chords: ARP root, +x, +y (major 0x47, minor 0x37) once a beat; the bass bounces octaves.
  struct Bar {
    uint8_t root, arp;
  };
  static const Bar kVerse[4] = {{60, 0x47}, {57, 0x37}, {53, 0x47}, {55, 0x47}};   // C Am F G
  static const Bar kBridge[4] = {{53, 0x47}, {55, 0x47}, {52, 0x37}, {57, 0x37}};  // F G Em Am
  auto harmony = [](Pattern& pt, const Bar* bars, int fromBar) {
    for (int b = fromBar; b < 4; ++b)
      for (int q = 0; q < 4; ++q) {
        const int s = b * 16 + q * 4;
        note(pt, kArp, s, bars[b].root, q == 0 ? 110 : 0);
        fx(pt, kArp, s, Fx::ARP, bars[b].arp);
        // TRI BASS is an octave down: root-ish 8ths, octave up on the offbeats.
        note(pt, kBass, s, static_cast<uint8_t>(bars[b].root - 12));
        note(pt, kBass, s + 2, bars[b].root);
      }
  };
  auto drums = [](Pattern& pt, bool chorus) {
    hits(pt, kDrums, kBd, chorus ? "x...x...x...x..." : "x.......x.x.....");
    hits(pt, kDrums, kSd, "....x.......x...");
    hits(pt, kDrums, kHh, chorus ? "xxoxxxoxxxoxxxox" : "x.x.x.x.x.x.x.x.");
  };

  Pattern& intro = pat(p, 0, 64);
  harmony(intro, kVerse, 0);
  hits(intro, kDrums, kHh, "x.x.x.x.x.x.x.x.", 32, 64);
  roll(intro, kDrums, kSd, 56, 64, 1, 50, 110);

  Pattern& verse = pat(p, 1, 64);
  harmony(verse, kVerse, 0);
  drums(verse, false);
  notes(verse, kLead, 0,
        "E5 . G5 . C6 . G5 . E5 . D5 . C5 . D5 . "
        "E5 . . . A4 . C5 . E5 . . . D5 . C5 . "
        "F5 . A5 . F5 . C5 . A4 . C5 . F5 . E5 . "
        "D5 . . . B4 . G4 . B4 . D5 . G5 . . .");
  fx(verse, kLead, 60, Fx::VIB, 0x46);
  fx(verse, kLead, 60, Fx::GAT, gat(300));

  Pattern& chorus = pat(p, 2, 64);
  harmony(chorus, kVerse, 0);
  drums(chorus, true);
  hits(chorus, kDrums, kOh, "X", 0, 1);
  notes(chorus, kLead, 0,
        "G5 . G5 E5 . C5 . E5 G5 . A5 . G5 . E5 . "
        "A5 . A5 E5 . C5 . E5 A5 . C6 . B5 . A5 . "
        "F5 . A5 . C6 . A5 . F5 . A5 . C6 . D6 . "
        "B5 . . . G5 . D5 . G5 . B5 . D6 . . .");
  fx(chorus, kLead, 60, Fx::VIB, 0x46);
  fx(chorus, kLead, 60, Fx::GAT, gat(300));
  notes(chorus, kBell, 0,
        "C5 . . . . . . . E5 . . . . . . . C5 . . . . . . . E5 . . . . . . . "
        "A4 . . . . . . . C5 . . . . . . . B4 . . . . . . . D5 . . . . . . .");

  Pattern& bridge = pat(p, 3, 64);
  harmony(bridge, kBridge, 0);
  hits(bridge, kDrums, kBd, "x...............");
  hits(bridge, kDrums, kSd, "........x.......");
  hits(bridge, kDrums, kHh, "x...x...x...x...");
  roll(bridge, kDrums, kSd, 56, 64, 1, 50, 120);
  notes(bridge, kBell, 0,
        "A5 . . . G5 . F5 . . . C5 . F5 . . . "
        "G5 . . . F5 . D5 . . . B4 . D5 . . . "
        "E5 . . . D5 . B4 . . . G4 . B4 . . . "
        "C5 . . . . . . . E5 . . . A5 . . .");

  Pattern& end = pat(p, 4, 16);
  hits(end, kDrums, kBd, "x...............");
  hits(end, kDrums, kOh, "X...............");
  note(end, kArp, 0, 60, 110);
  fx(end, kArp, 0, Fx::ARP, 0x47);
  fx(end, kArp, 0, Fx::GAT, kGat800);
  note(end, kBass, 0, 48);
  note(end, kLead, 0, 84, 120);
  fx(end, kLead, 0, Fx::VIB, 0x46);
  fx(end, kLead, 0, Fx::GAT, kGat800);

  static const Item kSong[] = {{0, 1, 0, 0}, {1, 2, 0, 0}, {2, 2, 0, 0}, {3, 1, 0, 0},
                               {1, 1, 0, 0}, {2, 1, 0, 0}, {2, 2, 2, 0}, {4, 1, 2, 0}};
  song(p, kSong, 8);
}

// ---- ACID: 128 BPM, A minor. 909 drums, a 303-style line with slides, accents and a filter that
// opens over the song, a 12-step polymeter section, offbeat stabs ----

void buildAcid(Project& p) {
  name(p, "DEMO-ACID");
  p.masterVol = 90;  // the mix peaks around -3 dBFS
  p.bpm = 128;
  p.scaleRoot = 9;
  p.scaleType = static_cast<uint8_t>(ScaleType::Minor);
  enum { kDrums, kAcid, kStab };
  static const Drum kKit[] = {{InstrType::Drum, "BD909"}, {InstrType::Drum, "CP909"}, {InstrType::Drum, "CH909"},
                              {InstrType::Drum, "OH909"}, {InstrType::Drum, "RS909"}};
  enum { kBd, kCp, kCh, kOh, kRs };
  kit(p, 0, "909", 16, kKit, 5);
  Instrument& acid = ins(p, 1, InstrType::Synth, "ACID");
  acid.send = 30;
  acid.drive = 30;
  ins(p, 2, InstrType::Synth, "STAB").send = 70;
  track(p, kDrums, "909", 0, 125);
  track(p, kAcid, "ACID", 1, 78, 60);
  track(p, kStab, "STAB", 2, 70);
  p.dlyTime = 3;
  p.dlyFb = 60;
  p.rvbSize = 70;
  p.rvbLevel = 60;
  p.compAmt = 40;

  static const char* const kLine = "A2! A2 A3 . A2 C3 A2! . E3 A2 G3 A2 A2! . C3 D3";
  static const int kSlides[] = {2, 10, 15};
  // The line with its slides and a filter lock on every step: from lo, rising by step.
  auto line = [](Pattern& pt, int lo, int step) {
    for (int at = 0; at < pt.length; at += 16) {
      notes(pt, kAcid, at, kLine);
      for (int s : kSlides) fx(pt, kAcid, at + s, Fx::SLD, 15);
      for (int s = 0; s < 16; ++s) fx(pt, kAcid, at + s, Fx::FLT, static_cast<uint8_t>(lo + s * step));
    }
  };
  auto drums = [](Pattern& pt, bool full) {
    hits(pt, kDrums, kBd, "x...x...x...x...");
    hits(pt, kDrums, kOh, "..x...x...x...x.");
    hits(pt, kDrums, kCh, full ? "xxoxxxoxxxoxxxox" : "x.x.x.x.x.x.x.x.");
    if (full) hits(pt, kDrums, kCp, "....x.......x...");
  };

  Pattern& intro = pat(p, 0, 16);
  drums(intro, false);

  Pattern& low = pat(p, 1, 16);
  drums(low, true);
  line(low, 20, 2);

  Pattern& poly = pat(p, 2, 16);
  drums(poly, true);
  hits(poly, kDrums, kRs, "...x..x.......x.");
  line(poly, 45, 3);
  poly.trackLen[kAcid] = 12;  // the line runs 12 steps against the 16-step beat
  for (int s = 0; s < 16; s += 2) fx(poly, kDrums, s + 1, Fx::PRB, 60);  // the off-16th hats come and go

  Pattern& brk = pat(p, 3, 16);
  hits(brk, kDrums, kRs, "...x..x.......x.");
  hits(brk, kDrums, kCh, "x.x.x.x.x.x.x.x.");
  line(brk, 70, 3);
  for (int s = 0; s < 16; ++s) fx(brk, kAcid, s, Fx::RES, 120);
  for (int s = 0; s < 16; s += 4) fx(brk, kAcid, s, Fx::DLY, 120);

  Pattern& peak = pat(p, 4, 16);
  drums(peak, true);
  hits(peak, kDrums, kRs, "...x..x.......x.");
  fx(peak, kDrums, 15, Fx::RAT, 0x03);
  line(peak, 75, 3);
  chord(peak, kStab, 3, 57, kChordTriad, 50);  // Am offbeat stabs, C on the last
  chord(peak, kStab, 10, 57, kChordTriad, 50);
  chord(peak, kStab, 14, 60, kChordTriad, 50);

  static const Item kSong[] = {{0, 4, 0, 0}, {1, 8, 0, 0}, {2, 8, 0, 0}, {3, 4, 0, 0}, {4, 8, 0, 0}, {1, 4, 0, 0}};
  song(p, kSong, 6);
}

// ---- LOFI: 84 BPM, C major, Dm7 G7 Cmaj7 Am7. Swung dusty drums, crushed FM e-piano, vinyl
// crackle on probability, humanize, a low-passed master ----

void buildLofi(Project& p) {
  name(p, "DEMO-LOFI");
  p.masterVol = 75;  // the mix peaks around -3 dBFS
  p.bpm = 84;
  p.scaleType = static_cast<uint8_t>(ScaleType::Major);
  enum { kDrums, kKeys, kBass, kMelody, kVinyl };
  static const Drum kKit[] = {{InstrType::Drum, "BD DUSTY"}, {InstrType::Drum, "SD DUSTY"}, {InstrType::Drum, "HH DUSTY"},
                              {InstrType::Drum, "RIM BIT"}};
  enum { kBd, kSd, kHh, kRim };
  kit(p, 0, "DUSTY", 16, kKit, 4);
  Instrument& keys = ins(p, 1, InstrType::Fm, "EP SOFT");
  keys.crushBits = 45;
  keys.crushRate = 20;
  keys.rsend = 60;
  ins(p, 2, InstrType::Synth, "MOOGISH");
  ins(p, 3, InstrType::Chip, "MUSICBOX").send = 60;
  Instrument& vinyl = ins(p, 4, InstrType::Chip, "NOIS HH");
  vinyl.decay = 6;
  vinyl.release = 6;
  vinyl.cutoff = 112;
  track(p, kDrums, "DRUMS", 0, 125);
  track(p, kKeys, "KEYS", 1, 68);
  track(p, kBass, "BASS", 2, 110, gat(380));
  track(p, kMelody, "MELODY", 3, 75, 100);
  track(p, kVinyl, "VINYL", 4, 80);
  p.tracks[kDrums].humanize = 25;
  p.tracks[kKeys].humanize = 30;
  p.dlyTime = 6;  // 3/8
  p.dlyFb = 45;
  p.dlyTone = 60;
  p.rvbSize = 80;
  p.rvbDamp = 100;
  p.rvbLevel = 70;
  p.compAmt = 35;
  p.djFilter = -18;  // a gentle low-pass on the whole mix

  static const uint8_t kChord[4] = {50, 55, 60, 57};  // D3 G3 C4 A3: 7th chords in C major
  auto keys2 = [](Pattern& pt) {
    for (int b = 0; b < 4; ++b) {
      chord(pt, kKeys, b * 16, kChord[b], kChordSeventh, gat(660));
      chord(pt, kKeys, b * 16 + 10, kChord[b], kChordSeventh, gat(450));
      pt.steps[kKeys][b * 16 + 10].vel = 80;
    }
  };
  auto vinyl2 = [](Pattern& pt) {
    for (int s = 0; s < pt.length; ++s) {
      note(pt, kVinyl, s, 72, 45);
      fx(pt, kVinyl, s, Fx::PRB, 22);
      fx(pt, kVinyl, s, Fx::NRN, 7);   // a random pitch: the crackle never repeats
    }
  };
  auto drums = [](Pattern& pt) {
    hits(pt, kDrums, kBd, "x.....x...x.....x.........x..x..");
    hits(pt, kDrums, kSd, "....x.......x...");
    hits(pt, kDrums, kHh, "x.o.x.o.x.o.x.ox");
    hits(pt, kDrums, kRim, "..........x..x..", 48, 64);
  };
  auto bass = [](Pattern& pt) {  // MOOGISH is an octave down
    notes(pt, kBass, 0,
          "D3 . . . . . D3 . . . A2 . . . C3 . "
          "G2 . . . . . G2 . . . D3 . . . F2 . "
          "C3 . . . . . C3 . . . G2 . . . B2 . "
          "A2 . . . . . A2 . . . E3 . . . G2 .");
  };
  auto melody = [](Pattern& pt) {  // MUSICBOX is an octave up
    notes(pt, kMelody, 0,
          ". . A4 . . . G4 . . . F4 . E4 . . . "
          "D4 . . . . . . . B3 . . . D4 . . . "
          "E4 . . . G4 . . . . . E4 . D4 . C4 . "
          "A3 . . . . . . . . . . . . . . .");
  };

  Pattern& intro = pat(p, 0, 64, 62);
  keys2(intro);
  vinyl2(intro);

  Pattern& beat = pat(p, 1, 64, 62);
  drums(beat);
  keys2(beat);
  bass(beat);
  vinyl2(beat);

  Pattern& full = pat(p, 2, 64, 62);
  drums(full);
  keys2(full);
  bass(full);
  melody(full);
  vinyl2(full);

  Pattern& brk = pat(p, 3, 64, 62);
  keys2(brk);
  melody(brk);
  vinyl2(brk);
  hits(brk, kDrums, kRim, "....x.......x...");

  Pattern& outro = pat(p, 4, 64, 62);
  keys2(outro);
  vinyl2(outro);
  outro.steps[kKeys][58] = Step();  // the last chord rings out
  chord(outro, kKeys, 48, kChord[3], kChordSeventh, kGat800);

  static const Item kSong[] = {{0, 1, 0, 0}, {1, 2, 0, 0}, {2, 2, 0, 0}, {3, 1, 0, 0}, {2, 2, 0, 0}, {4, 1, 0, 0}};
  song(p, kSong, 6);
}

// ---- SYNTHWAVE: 108 BPM, E minor, Em C G D. Pumping 16th bass, gated-reverb clap, sync pluck
// arpeggio, a wavetable lead with vibrato, tom fills ----

void buildSynthwave(Project& p) {
  name(p, "DEMO-SYNTHWAVE");
  p.masterVol = 150;
  p.bpm = 108;
  p.scaleRoot = 4;
  p.scaleType = static_cast<uint8_t>(ScaleType::Minor);
  enum { kKick, kDrums, kBass, kPad, kArp, kLead };
  static const Drum kKickKit[] = {{InstrType::Drum, "BD909"}};
  static const Drum kDrumKit[] = {{InstrType::Drum, "CLAP VRB"}, {InstrType::Drum, "CH808"},
                                  {InstrType::Drum, "TOM808"}, {InstrType::Drum, "TOM808 L"}};
  enum { kClap, kCh, kTomH, kTomL };
  kit(p, 0, "KICK", 16, kKickKit, 1);
  kit(p, 1, "DRUMS", 17, kDrumKit, 4);
  ins(p, 2, InstrType::Synth, "MOOGISH");
  ins(p, 3, InstrType::Synth, "PWMSTR").rsend = 90;
  ins(p, 4, InstrType::Synth, "SYNCPLK").send = 60;
  Instrument& lead = ins(p, 5, InstrType::Synth, "WT LEAD");
  lead.send = 50;
  lead.rsend = 70;
  track(p, kKick, "KICK", 0, 127);
  p.instruments[0].kit[0].vol = 127;
  track(p, kDrums, "DRUMS", 1, 95);
  track(p, kBass, "BASS", 2, 100, 45);
  track(p, kPad, "PAD", 3, 36);
  track(p, kArp, "ARP", 4, 62);
  track(p, kLead, "LEAD", 5, 85, gat(300));
  p.dlyTime = 3;
  p.dlyFb = 50;
  p.rvbSize = 110;
  p.rvbLevel = 90;
  p.compAmt = 45;
  p.scTrack = kKick + 1;
  p.scDepth = 70;

  static const uint8_t kRoot[4] = {52, 48, 55, 50};  // E3 C3 G3 D3: Em C G D in E minor
  auto pad = [](Pattern& pt) {
    for (int b = 0; b < 4; ++b) {
      chord(pt, kPad, b * 16, kRoot[b], kChordTriad, kGat800);
      chord(pt, kPad, b * 16 + 8, kRoot[b], kChordTriad, kGat800);
    }
  };
  auto bass = [](Pattern& pt) {  // MOOGISH is an octave down: driving 16ths, accents on the 8ths
    for (int b = 0; b < 4; ++b)
      for (int k = 0; k < 16; ++k) note(pt, kBass, b * 16 + k, kRoot[b], k % 2 ? 72 : 110);
  };
  auto arp = [](Pattern& pt, int fromBar) {
    for (int b = fromBar; b < 4; ++b) {
      note(pt, kArp, b * 16, static_cast<uint8_t>(kRoot[b] + 12));
      fx(pt, kArp, b * 16, Fx::CHD, kChordTriad);
      fx(pt, kArp, b * 16, Fx::ARS, kArs2Up);
    }
  };
  auto drums = [](Pattern& pt) {
    hits(pt, kKick, 0, "x...x...x...x...");
    hits(pt, kDrums, kClap, "....x.......x...");
    hits(pt, kDrums, kCh, "x.x.x.x.x.x.x.x.", 0, 56);
  };
  auto fill = [](Pattern& pt) {  // toms down the last two beats
    roll(pt, kDrums, kTomH, 56, 60, 2, 90, 110);
    roll(pt, kDrums, kTomL, 60, 64, 2, 110, 127);
  };
  auto melody = [](Pattern& pt) {
    notes(pt, kLead, 0,
          "B4 . . . . . G4 . A4 . B4 . . . E5 . "
          "D5 . . . C5 . . . B4 . . . G4 . . . "
          "B4 . . . . . D5 . E5 . D5 . . . B4 . "
          "A4 . . . . . . . F#4 . . . A4 . . .");
    for (int b = 0; b < 4; ++b) fx(pt, kLead, b * 16, Fx::VIB, 0x35);
  };

  Pattern& intro = pat(p, 0, 64);
  pad(intro);
  arp(intro, 1);
  hits(intro, kKick, 0, "x...x...x...x...", 32, 64);
  fill(intro);

  Pattern& verse = pat(p, 1, 64);
  drums(verse);
  fill(verse);
  bass(verse);
  pad(verse);
  arp(verse, 0);

  Pattern& chorus = pat(p, 2, 64);
  drums(chorus);
  fill(chorus);
  bass(chorus);
  pad(chorus);
  arp(chorus, 0);
  melody(chorus);

  Pattern& brk = pat(p, 3, 64);
  pad(brk);
  arp(brk, 0);
  melody(brk);
  fill(brk);

  Pattern& outro = pat(p, 4, 64);
  pad(outro);
  arp(outro, 0);
  hits(outro, kKick, 0, "x...x...x...x...", 0, 32);

  static const Item kSong[] = {{0, 1, 0, 0}, {1, 2, 0, 0}, {2, 2, 0, 0}, {3, 1, 0, 0}, {2, 2, 0, 0}, {4, 1, 0, 0}};
  song(p, kSong, 6);
}

// ---- DUB TECHNO: 120 BPM, C minor. One FM minor-9 chord stab through a long dark delay and a big
// reverb, its filter swept by a 4-bar LFO and FLT locks, delay / reverb throws, a drone pad ----

void buildDubTechno(Project& p) {
  name(p, "DEMO-DUBTECHNO");
  p.masterVol = 110;
  p.bpm = 120;
  p.scaleType = static_cast<uint8_t>(ScaleType::Minor);
  enum { kDrums, kStab, kBass, kPad };
  static const Drum kKit[] = {{InstrType::Drum, "BD909 S"}, {InstrType::Drum, "RS909"}, {InstrType::Drum, "CH909"},
                              {InstrType::Drum, "OH909"}};
  enum { kBd, kRs, kCh, kOh };
  kit(p, 0, "DUB KIT", 16, kKit, 4);
  Instrument& stab = ins(p, 1, InstrType::Fm, "CHRD MI9");
  stab.send = 100;
  stab.rsend = 90;
  stab.fltMode = static_cast<uint8_t>(FltMode::Lp);
  stab.cutoff = 82;
  stab.reso = 35;
  stab.lfoWave = static_cast<uint8_t>(LfoWave::Sine);
  stab.lfoDest = static_cast<uint8_t>(LfoDest::Cutoff);
  stab.lfoSync = 1;
  stab.lfoRate = 10;  // 4 bars
  stab.lfoDepth = 24;
  ins(p, 2, InstrType::Synth, "SUBBASS");
  ins(p, 3, InstrType::Synth, "DARK PAD").rsend = 110;
  track(p, kDrums, "DRUMS", 0, 112);
  track(p, kStab, "STAB", 1, 95, 30);
  track(p, kBass, "BASS", 2, 105, gat(200));
  track(p, kPad, "DRONE", 3, 55);
  p.dlyTime = 3;
  p.dlyFb = 100;
  p.dlyTone = 45;
  p.dlyLevel = 110;
  p.rvbSize = 120;
  p.rvbDamp = 90;
  p.rvbLevel = 95;
  p.compAmt = 30;

  static const uint8_t kCut[4] = {70, 88, 62, 96};  // the stabs' brightness, bar by bar
  // Stabs on 3 and 10 of each bar: C (Cm9), or F (Fm9) in bars 3-4 of the B section.
  auto stabs = [](Pattern& pt, bool alt) {
    for (int b = 0; b < 4; ++b) {
      const uint8_t n = alt && b >= 2 ? 53 : 60;
      for (int at : {3, 10}) {
        note(pt, kStab, b * 16 + at, n);
        fx(pt, kStab, b * 16 + at, Fx::FLT, kCut[b]);
      }
    }
  };
  auto drums = [](Pattern& pt) {
    hits(pt, kDrums, kBd, "x...x...x...x...");
    hits(pt, kDrums, kCh, "..x...x...x...x.");
    hits(pt, kDrums, kOh, "......x.......x.", 32, 64);
    hits(pt, kDrums, kRs, "...x.....x....x.");
    for (int s = 2; s < 64; s += 8) fx(pt, kDrums, s, Fx::PRB, 75);
  };
  auto bass = [](Pattern& pt, bool alt) {
    for (int b = 0; b < 4; ++b) {
      const uint8_t n = alt && b >= 2 ? 41 : 36;  // F2 / C2
      note(pt, kBass, b * 16, n);
      note(pt, kBass, b * 16 + 10, n, 80);
    }
  };
  auto drone = [](Pattern& pt) {
    note(pt, kPad, 0, 60);
    fx(pt, kPad, 0, Fx::TIE, 0);
  };
  auto throws = [](Pattern& pt) {  // the last stab of the pattern into the delay and the reverb
    fx(pt, kStab, 58, Fx::DLY, 127);
    fx(pt, kStab, 58, Fx::RVB, 127);
  };

  Pattern& intro = pat(p, 0, 64);
  stabs(intro, false);
  drone(intro);
  hits(intro, kDrums, kCh, "..x...x...x...x.", 32, 64);

  Pattern& a = pat(p, 1, 64);
  drums(a);
  stabs(a, false);
  bass(a, false);
  drone(a);

  Pattern& b = pat(p, 2, 64);
  drums(b);
  stabs(b, true);
  bass(b, true);
  drone(b);
  throws(b);

  Pattern& brk = pat(p, 3, 64);
  stabs(brk, false);
  for (int s = 0; s < 64; ++s)
    if (brk.steps[kStab][s].hasNote()) fx(brk, kStab, s, Fx::DLY, 127);
  drone(brk);
  hits(brk, kDrums, kRs, "...x.....x....x.");

  static const Item kSong[] = {{0, 2, 0, 0}, {1, 3, 0, 0}, {2, 4, 0, 0}, {3, 2, 0, 0}, {1, 3, 0, 0}, {0, 1, 0, 0}};
  song(p, kSong, 6);
}

// ---- IDM: 110 BPM, D dorian. Every part on its own track length (16, 7, 5, 13, 9, 11, 6 steps)
// inside 64-step patterns, so the parts drift against each other; conditions (CND A:B, PRE, NEI),
// probability and random notes (NRN) keep it from repeating ----

void buildIdm(Project& p) {
  name(p, "DEMO-IDM");
  p.masterVol = 100;
  p.bpm = 110;
  p.scaleRoot = 2;
  p.scaleType = static_cast<uint8_t>(ScaleType::Dorian);
  enum { kKick, kClick, kHat, kMetal, kBass, kBell, kPad, kGlitch };
  static const Drum kKick1[] = {{InstrType::Drum, "BD808 S"}};
  static const Drum kClick1[] = {{InstrType::Drum, "RS808"}};
  static const Drum kHat1[] = {{InstrType::Drum, "CH808"}};
  static const Drum kMetal1[] = {{InstrType::Drum, "METAL HT"}};
  kit(p, 0, "KICK", 16, kKick1, 1);
  kit(p, 1, "CLICK", 17, kClick1, 1);
  kit(p, 2, "HAT", 18, kHat1, 1);
  kit(p, 3, "METAL", 19, kMetal1, 1);
  ins(p, 4, InstrType::Synth, "PLUCKBAS");
  ins(p, 5, InstrType::Fm, "DX BELL").send = 60;
  ins(p, 6, InstrType::Synth, "WARM PAD").rsend = 100;
  ins(p, 7, InstrType::Chip, "BLIP").send = 50;
  track(p, kKick, "KICK", 0, 120);
  track(p, kClick, "CLICK", 1, 90);
  track(p, kHat, "HAT", 2, 115);
  track(p, kMetal, "METAL", 3, 110);
  track(p, kBass, "BASS", 4, 100, 60);
  track(p, kBell, "BELL", 5, 58);
  track(p, kPad, "PAD", 6, 62);
  track(p, kGlitch, "GLITCH", 7, 55);
  p.tracks[kHat].humanize = 20;
  p.dlyTime = 5;  // 5/16 against the 4/4
  p.dlyFb = 55;
  p.rvbSize = 100;
  p.rvbLevel = 80;
  p.compAmt = 35;

  static const uint8_t kLen[8] = {0, 7, 5, 13, 9, 11, 0, 6};
  auto lens = [](Pattern& pt) {
    for (int t = 0; t < 8; ++t) pt.trackLen[t] = kLen[t];
  };
  auto drums = [](Pattern& pt) {
    hits(pt, kKick, 0, "x......x..x.....");
    hits(pt, kClick, 0, "x..x...", 0, 7);
    hits(pt, kHat, 0, "x.xx.", 0, 5);
    fx(pt, kHat, 2, Fx::CND, 0x12);         // 1:2
    fx(pt, kHat, 3, Fx::CND, kCndPre);      // with the step before
    hits(pt, kMetal, 0, "x.......x....", 0, 13);
    fx(pt, kMetal, 8, Fx::CND, kCndNei);    // when the hats' condition passed
  };
  auto bass = [](Pattern& pt) {  // PLUCKBAS is an octave down
    notes(pt, kBass, 0, "D3 . . A2 . D3 . F3 .");
    fx(pt, kBass, 7, Fx::PRB, 60);
  };
  auto bell = [](Pattern& pt) {
    notes(pt, kBell, 0, "A5 . . E5 . D5 . . C5 . .");
    fx(pt, kBell, 3, Fx::NRN, 2);
    fx(pt, kBell, 8, Fx::NRN, 3);
    fx(pt, kBell, 5, Fx::PRB, 70);
  };
  auto pad = [](Pattern& pt, bool alt) {
    chord(pt, kPad, 0, 62, kChordAdd9, kGat800);
    chord(pt, kPad, 16, 62, kChordAdd9, kGat800);
    chord(pt, kPad, 32, alt ? 60 : 57, kChordAdd9, kGat800);
    chord(pt, kPad, 48, alt ? 60 : 57, kChordAdd9, kGat800);
  };
  auto glitch = [](Pattern& pt) {
    notes(pt, kGlitch, 0, "C6 . . G5 . .");
    fx(pt, kGlitch, 0, Fx::RAT, 0x23);  // 3 hits falling
    fx(pt, kGlitch, 3, Fx::NRN, 4);
    fx(pt, kGlitch, 3, Fx::CND, 0x13);  // 1:3
  };

  Pattern& intro = pat(p, 0, 64);
  lens(intro);
  pad(intro, false);
  bell(intro);
  hits(intro, kHat, 0, "x.xx.", 0, 5);

  Pattern& a = pat(p, 1, 64);
  lens(a);
  drums(a);
  bass(a);
  bell(a);
  pad(a, false);

  Pattern& b = pat(p, 2, 64);
  lens(b);
  drums(b);
  bass(b);
  bell(b);
  pad(b, true);
  glitch(b);

  Pattern& brk = pat(p, 3, 64);
  lens(brk);
  pad(brk, true);
  bell(brk);
  glitch(brk);
  hits(brk, kClick, 0, "x..x...", 0, 7);

  static const Item kSong[] = {{0, 1, 0, 0}, {1, 3, 0, 0}, {2, 3, 0, 0}, {3, 1, 0, 0}, {1, 2, 0, 0}, {0, 1, 0, 0}};
  song(p, kSong, 6);
}

// ---- HOUSE: 124 BPM, F minor, Fm7 Bbm7. Swung 909 groove, an offbeat FM bass, chord stabs,
// congas, a choir breakdown with a clap roll; the PERF buttons set up for playing it live ----

void buildHouse(Project& p) {
  name(p, "DEMO-HOUSE");
  p.masterVol = 100;
  p.bpm = 124;
  p.scaleRoot = 5;
  p.scaleType = static_cast<uint8_t>(ScaleType::Minor);
  enum { kDrums, kBass, kStab, kPad, kPerc };
  static const Drum kKit[] = {{InstrType::Drum, "BD909"}, {InstrType::Drum, "CP909"}, {InstrType::Drum, "CH909"},
                              {InstrType::Drum, "OH909"}, {InstrType::Drum, "RS909"}};
  enum { kBd, kCp, kCh, kOh, kRs };
  kit(p, 0, "909", 16, kKit, 5);
  ins(p, 1, InstrType::Fm, "DX BASS");
  ins(p, 2, InstrType::Synth, "STAB").send = 45;
  ins(p, 3, InstrType::Synth, "CHOIR").rsend = 100;
  ins(p, 4, InstrType::Fm, "CONGA");
  track(p, kDrums, "909", 0, 118);
  track(p, kBass, "BASS", 1, 100, 40);
  track(p, kStab, "STAB", 2, 78, 35);
  track(p, kPad, "CHOIR", 3, 70);
  track(p, kPerc, "CONGA", 4, 72);
  p.tracks[kDrums].humanize = 15;
  p.tracks[kPerc].humanize = 25;
  p.dlyTime = 3;
  p.dlyFb = 45;
  p.rvbSize = 85;
  p.rvbLevel = 75;
  p.compAmt = 40;
  // PERF: filter down / up, delay and reverb throws, rolls, short decays, mute.
  static const PerfFx kPerf[kPerfButtons] = {PerfFx::FltLow, PerfFx::FltHigh, PerfFx::DlyMax,  PerfFx::RvbMax,
                                             PerfFx::Rat4,   PerfFx::RatUp,   PerfFx::DecShort, PerfFx::Mute};
  for (int i = 0; i < kPerfButtons; ++i) p.perfMap[i] = static_cast<uint8_t>(kPerf[i]);

  static const uint8_t kRoot[4] = {53, 53, 58, 58};  // F3 F3 Bb3 Bb3: Fm7, Bbm7
  auto drums = [](Pattern& pt, bool clap) {
    hits(pt, kDrums, kBd, "x...x...x...x...");
    hits(pt, kDrums, kOh, "..x...x...x...x.");
    hits(pt, kDrums, kCh, "xx.xxx.xxx.xxx.x");
    if (clap) hits(pt, kDrums, kCp, "....x.......x...");
  };
  auto bass = [](Pattern& pt) {  // DX BASS is an octave down; offbeats, the fifth on the last
    for (int b = 0; b < 4; ++b)
      for (int q = 0; q < 4; ++q)
        note(pt, kBass, b * 16 + q * 4 + 2, static_cast<uint8_t>(kRoot[b] + (q == 3 ? 7 : 0)), q == 0 ? 115 : 0);
  };
  auto stabs = [](Pattern& pt) {
    for (int b = 0; b < 4; ++b)
      for (int at : {3, 6, 11}) chord(pt, kStab, b * 16 + at, static_cast<uint8_t>(kRoot[b] + 12), kChordSeventh, 35);
  };
  auto congas = [](Pattern& pt, int at) {
    notes(pt, kPerc, at, ". . . G4 . . . C4 . . G4 . . C4 . G4");
    fx(pt, kPerc, at + 13, Fx::PRB, 70);
    fx(pt, kPerc, at + 15, Fx::PRB, 50);
  };

  Pattern& intro = pat(p, 0, 16, 56);
  drums(intro, false);

  Pattern& groove = pat(p, 1, 64, 56);
  drums(groove, true);
  hits(groove, kDrums, kRs, "......x.......x.");
  bass(groove);
  for (int at = 0; at < 64; at += 16) congas(groove, at);

  Pattern& full = pat(p, 2, 64, 56);
  drums(full, true);
  hits(full, kDrums, kRs, "......x.......x.");
  bass(full);
  stabs(full);

  Pattern& brk = pat(p, 3, 64, 56);
  for (int b = 0; b < 4; b += 2) chord(brk, kPad, b * 16, kRoot[b], kChordSeventh, kGat800);
  chord(brk, kPad, 8, kRoot[0], kChordSeventh, kGat800);
  chord(brk, kPad, 40, kRoot[2], kChordSeventh, kGat800);
  stabs(brk);
  for (int s = 0; s < 64; ++s)
    if (brk.steps[kStab][s].hasNote()) fx(brk, kStab, s, Fx::FLT, static_cast<uint8_t>(40 + s));
  roll(brk, kDrums, kCp, 48, 64, 1, 30, 120);

  static const Item kSong[] = {{0, 4, 0, 0}, {1, 2, 0, 0}, {2, 4, 0, 0}, {3, 1, 0, 0}, {2, 4, 0, 0}, {1, 1, 0, 0}};
  song(p, kSong, 6);
}

struct Entry {
  const char* name;
  void (*build)(Project&);
};
constexpr Entry kDemos[] = {
    {"DEMO-TRANCE", buildTrance}, {"DEMO-CHIPTUNE", buildChip},
    {"DEMO-ACID", buildAcid},     {"DEMO-LOFI", buildLofi},  {"DEMO-SYNTHWAVE", buildSynthwave},
    {"DEMO-DUBTECHNO", buildDubTechno}, {"DEMO-IDM", buildIdm}, {"DEMO-HOUSE", buildHouse},
};
constexpr int kCount = sizeof(kDemos) / sizeof(kDemos[0]);

}  // namespace

int demoCount() { return kCount; }

const char* demoName(int i) { return kDemos[i >= 0 && i < kCount ? i : 0].name; }

void demoBuild(int i, Project& p) {
  p.reset();
  kDemos[i >= 0 && i < kCount ? i : 0].build(p);
}

}  // namespace mt
