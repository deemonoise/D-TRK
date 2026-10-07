#include "templates.h"
#include <string.h>
#include "presets_factory.h"

namespace mt {
namespace {

// Factory preset by name (any type); false when the name is not in the factory set.
bool preset(const char* name, Instrument& out) {
  for (int t = 0; t < static_cast<int>(InstrType::Count); ++t) {
    const InstrType type = static_cast<InstrType>(t);
    for (int i = 0; i < factoryCount(type); ++i)
      if (strcmp(factoryPreset(type, i).name, name) == 0) {
        factoryBuild(type, i, out);
        return true;
      }
  }
  return false;
}

void track(Project& p, int t, const char* name, int instr) {
  TrackCfg& c = p.tracks[t];
  strncpy(c.name, name, sizeof(c.name) - 1);
  c.name[sizeof(c.name) - 1] = 0;
  c.out = TrackOut::Int;
  c.instr = static_cast<uint8_t>(instr);
}

// INS1 = a KIT whose lanes play INS9.. (the drum presets); lane k answers note 60 + k, so each drum
// instrument is transposed down by k to sound at its own pitch.
void kit(Project& p, const char* name, const char* const* drums, int n) {
  Instrument& k = p.instruments[0];
  k = Instrument();
  strncpy(k.name, name, sizeof(k.name) - 1);
  instrSetType(k, InstrType::Kit);
  for (int l = 0; l < n && l < kKitLanes; ++l) {
    Instrument& d = p.instruments[8 + l];
    if (!preset(drums[l], d)) continue;
    d.transpose = static_cast<int8_t>(-l);
    k.kit[l].instr = static_cast<uint8_t>(8 + l);
  }
  track(p, 0, "DRUMS", 0);
}

// Melodic presets into INS2.. on tracks 2.. (INS1 / track 1 stay for the kit).
void melodic(Project& p, const char* const* names, const char* const* tracks, int n) {
  for (int i = 0; i < n; ++i) {
    if (!preset(names[i], p.instruments[1 + i])) continue;
    track(p, 1 + i, tracks[i], 1 + i);
  }
}

void build808(Project& p) {
  static const char* const kDrums[] = {"BD808", "SD808", "CH808", "OH808", "CP808", "CB808"};
  kit(p, "808 KIT", kDrums, 6);
  static const char* const kInst[] = {"ACID", "LEAD", "PAD"}, *const kTr[] = {"BASS", "LEAD", "PAD"};
  melodic(p, kInst, kTr, 3);
}

void build909(Project& p) {
  static const char* const kDrums[] = {"BD909", "SD909", "CH909", "OH909", "CP909"};
  kit(p, "909 KIT", kDrums, 5);
  static const char* const kInst[] = {"SUBBASS", "SYNCLD", "PWMSTR", "PLUCK"};
  static const char* const kTr[] = {"BASS", "LEAD", "PAD", "PLUCK"};
  melodic(p, kInst, kTr, 4);
}

void buildFm(Project& p) {
  static const char* const kDrums[] = {"KICK", "SNARE", "HAT C", "HAT O", "CLAP", "WOODBLK"};
  kit(p, "FM KIT", kDrums, 6);
  static const char* const kInst[] = {"FM BASS", "E.PIANO", "BELL", "CHORD M7"};
  static const char* const kTr[] = {"BASS", "EPIANO", "BELL", "CHORDS"};
  melodic(p, kInst, kTr, 4);
}

void buildChip(Project& p) {
  static const char* const kInst[] = {"SQ LEAD", "ARP PLK", "TRI BASS", "NOIS HH", "NOIS SN", "WT BELL"};
  static const char* const kTr[] = {"LEAD", "ARP", "BASS", "HAT", "SNARE", "BELL"};
  for (int i = 0; i < 6; ++i)
    if (preset(kInst[i], p.instruments[i])) track(p, i, kTr[i], i);
}

void buildMidi(Project& p) {
  for (int t = 0; t < 8; ++t) {
    TrackCfg& c = p.tracks[t];
    c.out = TrackOut::Midi;
    c.channel = static_cast<uint8_t>(t);
    char nm[9];
    nm[0] = 'C', nm[1] = 'H', nm[2] = static_cast<char>('1' + t), nm[3] = 0;
    memcpy(c.name, nm, 4);
  }
}

struct Entry {
  const char* name;
  void (*build)(Project&);
};
constexpr Entry kTemplates[] = {
    {"EMPTY", nullptr}, {"808 SET", build808}, {"909 SET", build909},
    {"FM SET", buildFm}, {"CHIPTUNE", buildChip}, {"MIDI 8", buildMidi},
};
constexpr int kCount = sizeof(kTemplates) / sizeof(kTemplates[0]);

}  // namespace

int templateCount() { return kCount; }

const char* templateName(int i) { return kTemplates[i >= 0 && i < kCount ? i : 0].name; }

void templateBuild(int i, Project& p) {
  p.reset();
  if (i > 0 && i < kCount && kTemplates[i].build) kTemplates[i].build(p);
}

void templateStrip(Project& p) {
  for (Pattern& pt : p.patterns) pt = Pattern();
  p.chainLen = 0;
  for (int i = 0; i < kChainMax; ++i) {
    p.chain[i] = 0;
    p.chainTr[i] = 0;
    p.chainRep[i] = 1;
    p.chainScene[i] = 0;
  }
  for (uint16_t& s : p.scenes) s = kSceneEmpty;
  p.songMode = false;
  strncpy(p.name, "untitled", sizeof(p.name) - 1);
  p.name[sizeof(p.name) - 1] = 0;
}

}  // namespace mt
