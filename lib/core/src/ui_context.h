#pragma once
#include "model.h"
#include <stddef.h>
#include <stdio.h>
namespace mt {
struct InstrumentUse { uint16_t tracks = 0; bool current = false, currentViaKit = false; int count = 0; };
// A KIT can consume an instrument through several lanes; count each INT track only once.
inline InstrumentUse instrumentUse(const Project& p, int instrument, int currentTrack) {
  InstrumentUse use;
  if(instrument<0 || instrument>=kInstruments)return use;
  for(int t=0;t<kTracks;++t){
    if(!p.trackInternal(t))continue;
    const int slot=p.tracks[t].instr%kInstruments;
    bool consumes=slot==instrument;
    if(p.instruments[slot].type==InstrType::Kit)
      for(const auto& lane:p.instruments[slot].kit)consumes|=lane.instr==instrument;
    if(!consumes)continue;
    use.tracks|=static_cast<uint16_t>(1u<<t);++use.count;
    use.current|=t==currentTrack;
    use.currentViaKit|=t==currentTrack && slot!=instrument;
  }
  return use;
}
inline void markerPosition(uint16_t fraction, uint32_t frames, uint32_t rate, char* out, size_t size) {
  const unsigned permille=(static_cast<uint32_t>(fraction)*1000+32767)/65535;
  if(!rate){snprintf(out,size,"%u.%u%%",permille/10,permille%10);return;}
  const uint64_t frame=(static_cast<uint64_t>(fraction)*frames+32767)/65535;
  const uint64_t ms=(frame*1000+rate/2)/rate;
  snprintf(out,size,"%u.%u%% %lu.%03lus",permille/10,permille%10,
           static_cast<unsigned long>(ms/1000),static_cast<unsigned long>(ms%1000));
}
}
