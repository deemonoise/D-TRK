#include "oneshot_lib.h"
#include <stdio.h>
#include <string.h>
#include "synth_drum_machines.h"

namespace mt {

namespace {

// Short folder of a DRUM group, by drumGroupName.
const char* drumShort(int g) {
  static const struct { const char* group; const char* dir; } kShort[] = {
      {"KICK", "BD"}, {"SNARE", "SD"}, {"TOM", "TOM"}, {"CLAP", "CP"},
      {"RIM", "RS"},  {"HAT", "HH"},   {"CYMBL", "CY"},
  };
  const char* name = drumGroupName(g);
  for (const auto& s : kShort)
    if (strcmp(s.group, name) == 0) return s.dir;
  return name;  // PERC and later groups as they are
}

void sanitize(char* s) {
  for (; *s; ++s) {
    const char c = *s;
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok) *s = '_';
  }
}

}  // namespace

void oneshotDir(const Instrument& in, char out[kOneshotDirMax]) {
  switch (in.type) {
    case InstrType::Drum: {
      const int g = drumGroupOf(in.machine);
      snprintf(out, kOneshotDirMax, "%s/%s", drumModelName(g, drumModelOf(in.machine)), drumShort(g));
      break;
    }
    case InstrType::Fm: snprintf(out, kOneshotDirMax, "FM/%s", fmMachineName(in.machine)); break;
    case InstrType::Synth: {
      static const char* const kOsc[] = {"SAW", "SQUARE", "TRI", "WT"};
      static_assert(sizeof(kOsc) / sizeof(kOsc[0]) == static_cast<int>(SynOsc::Count), "SYNTH osc names");
      const uint8_t o = in.synOsc[0];
      snprintf(out, kOneshotDirMax, "SYNTH/%s", kOsc[o < static_cast<uint8_t>(SynOsc::Count) ? o : 0]);
      break;
    }
    case InstrType::Chip: {
      char w[16];
      chipWaveName(in.wave, w, sizeof w);
      sanitize(w);
      snprintf(out, kOneshotDirMax, "CHIP/%s", w);
      break;
    }
    case InstrType::String: snprintf(out, kOneshotDirMax, "STRING/%s", stringModelName(in.machine)); break;
    case InstrType::Lofi: snprintf(out, kOneshotDirMax, "LOFI/%s", lofiMachineName(in.machine)); break;
    case InstrType::Sample: snprintf(out, kOneshotDirMax, "SAMPLE"); break;
    case InstrType::Kit: snprintf(out, kOneshotDirMax, "KIT"); break;
    default: snprintf(out, kOneshotDirMax, "OTHER"); break;
  }
}

bool oneshotFilePath(const char* dir, const char* name, bool (*exists)(const char* path, void* ctx),
                     void* ctx, char* out, int n) {
  for (int k = 0; k <= 999; ++k) {
    const int len = k == 0 ? snprintf(out, n, "/samples/oneshots/%s/%s.wav", dir, name)
                           : snprintf(out, n, "/samples/oneshots/%s/%s_%d.wav", dir, name, k);
    if (len < 0 || len >= n) return false;
    if (!exists(out, ctx)) return true;
  }
  return false;
}

int kitBakeLanes(const Project& p, int kit, uint8_t lanes[kKitLanes]) {
  if (kit < 0 || kit >= kInstruments || p.instruments[kit].type != InstrType::Kit) return 0;
  const KitLane* k = p.instruments[kit].kit;
  int count = 0;
  for (int l = 0; l < kKitLanes; ++l) {
    const uint8_t i = k[l].instr;
    if (i >= kInstruments) continue;
    const InstrType t = p.instruments[i].type;
    if (t == InstrType::Kit || t == InstrType::Sample) continue;
    bool shadowed = false;
    for (int e = 0; e < l && !shadowed; ++e) shadowed = k[e].note == k[l].note;
    if (!shadowed) lanes[count++] = static_cast<uint8_t>(l);
  }
  return count;
}

void kitLaneToSample(Instrument& kit, int lane, const char* name) {
  if (lane < 0 || lane >= kKitLanes) return;
  KitLane& l = kit.kit[lane];
  l.instr = kNoInstr;
  strncpy(l.sample, name, kSampleNameMax);
  l.sample[kSampleNameMax] = 0;
  l.pitch = 0;
  l.vol = kLaneUnityVol;
  l.decay = 0;
}

}  // namespace mt
