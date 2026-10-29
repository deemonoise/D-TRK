// Factory preset API over the per-type tables (presets_factory_<type>.cpp).
#include "presets_factory.h"
#include <stdio.h>
#include <string.h>
#include "presets_factory_fill.h"

namespace mt {
namespace {

const FactoryTable* tableOf(InstrType t) {
  switch (t) {
    case InstrType::Kit: return &kFactoryKit;
    case InstrType::Chip: return &kFactoryChip;
    case InstrType::Fm: return &kFactoryFm;
    case InstrType::Drum: return &kFactoryDrum;
    case InstrType::Synth: return &kFactorySynth;
    case InstrType::String: return &kFactoryString;
    case InstrType::Lofi: return &kFactoryLofi;
    default: return nullptr;  // SAMPLE, Unused7: no factory presets
  }
}

}  // namespace

int factoryCount(InstrType t) {
  const FactoryTable* tb = tableOf(t);
  return tb ? tb->count : 0;
}

const FactoryPreset& factoryPreset(InstrType t, int i) {
  const FactoryTable* tb = tableOf(t);
  if (tb && i >= 0 && i < tb->count) return tb->presets[i];
  return kFactoryKit.presets[0];
}

void factoryBuild(InstrType t, int i, Instrument& out) {
  const FactoryPreset& p = factoryPreset(t, i);
  out = Instrument();
  out.type = p.type;
  snprintf(out.name, sizeof(out.name), "%s", p.name);
  if (p.type == InstrType::Kit) kitSetDefaults(out);
  for (int k = 0; k < p.drumCount && k < kKitLanes; ++k) out.kit[k].vol = p.drums[k].vol;
  p.fill(out);
}

int factoryKitBlock(int kitSlot) {
  return kitSlot >= kInstruments - kKitBlock ? kInstruments - 2 * kKitBlock : kInstruments - kKitBlock;
}

void factoryBuildKit(int i, Project& p, int kitSlot) { factoryBuildKit(i, p.instruments, kitSlot); }

void factoryBuildKit(int i, Instrument* instruments, int kitSlot) {
  Instrument& kit = instruments[kitSlot];
  factoryBuild(InstrType::Kit, i, kit);
  const FactoryPreset& f = factoryPreset(InstrType::Kit, i);
  const int first = factoryKitBlock(kitSlot);
  for (int k = 0; k < f.drumCount && k < kKitLanes; ++k) {
    Instrument& d = instruments[first + k];
    const FactoryDrum& dr = f.drums[k];
    for (int j = 0; j < factoryCount(dr.type); ++j)
      if (strcmp(factoryPreset(dr.type, j).name, dr.name) == 0) {
        factoryBuild(dr.type, j, d);
        clearInsert(d);  // behind a lane: no track insert takes the drum's recommendation
        d.transpose = static_cast<int8_t>(d.transpose - k);
        // A lane that plays an instrument sounds at the instrument's vol (KitLane::vol drives only
        // sampler lanes): the lane level is the drum's level. This also drops a driven preset's
        // lowered vol together with the drive clearInsert took away.
        d.vol = dr.vol;
        kit.kit[k].instr = static_cast<uint8_t>(first + k);
        break;
      }
  }
}

}  // namespace mt
