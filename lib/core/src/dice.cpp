#include "dice.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include "gen_util.h"
#include "dice_kit.h"
#include "dice_sounds.h"
#include "dice_phrase.h"
#include "dice_key.h"
#include "dice_hybrid.h"
#include "rng.h"

namespace mt {
namespace {
int clamp(int value, int lo, int hi) { return std::max(lo, std::min(hi, value)); }
uint8_t bit(int track) { return static_cast<uint8_t>(1u << track); }

int pitch(const DiceVoice& voice, const DiceVariant& v, const DiceParts& d, const Instrument* instrument) {
  int base=60+voice.pitch;
  if(static_cast<DiceVoiceKind>(voice.kind)==DiceVoiceKind::Bass) base-=12;
  if(!dicePercussion(voice)) base+=static_cast<int>(d.scaleRoot)-v.root;
  return base-(instrument ? instrument->transpose : 0);
}
void eventFx(Step& step, const DiceEvent& e, const DiceVoice& voice, bool melodic) {
  const int chance=e.probability*voice.chance/100;
  if(chance<100) putFx(step,Fx::PRB,chance);
  if(e.condition) putFx(step,Fx::CND,e.condition);
  if(e.repeats>1) putFx(step,Fx::RAT,e.repeats);
  if(melodic && static_cast<DiceVoiceKind>(voice.kind)==DiceVoiceKind::Pad)
    putFx(step,Fx::GAT,200);  // 800%: allow the recipe pad envelope to develop
  if(melodic && diceChord(voice)!=255) putFx(step,Fx::CHD,diceChord(voice));
  if(e.cutoff!=255) putFx(step,Fx::FLT,e.cutoff);
  if(e.resonance!=255) putFx(step,Fx::RES,e.resonance);
  if(e.delay!=255) putFx(step,Fx::DLY,e.delay);
  if(e.reverb!=255) putFx(step,Fx::RVB,e.reverb);
}
void writeVoice(DicePat& dst, int t, const DiceVoice& voice, const DiceRecipe& recipe,
                const DiceVariant& v, const DiceParts& d, const Instrument* m,
                bool preserveTiming, int source, const dice_detail::PhrasePlan& plan, DiceDensity density, int feel, Rng& rng, bool kitGrid=false,const Step* prior=nullptr) {
  const TrackSpeed sourceSpeed=toSpeed(voice.speed);
  const TrackSpeed destSpeed=(preserveTiming || kitGrid) ? toSpeed(dst.trackSpeed[t]) : sourceSpeed;
  const int sourceTicks=ticksPerStep(static_cast<Resolution>(recipe.resolution))*speedDiv(sourceSpeed)/speedMul(sourceSpeed);
  const int destTicks=ticksPerStep(dst.res)*speedDiv(destSpeed)/speedMul(destSpeed);
  const int period=(voice.length ? voice.length : recipe.length)*sourceTicks;
  const int base=pitch(voice,v,d,m);
  Step* row=dst.steps[t];
  for(int cycle=0;cycle*period<dst.length*destTicks;++cycle)
    for(int i=0;i<diceEventCount(voice);++i) {
      const DiceEvent& e=diceEvent(voice,i);
      if(e.at>=recipe.length || (voice.length && e.at>=voice.length)) continue;
      const int tick=cycle*period+e.at*sourceTicks;
      int at=(tick+destTicks/2)/destTicks;
      if(at>=dst.length || row[at].hasNote()) continue;
      // Preserve the first source attack and strong anchors; SPARSE only thins softer hits.
      if(density==DiceDensity::Sparse && i && e.velocity<110 && rng.below(3)==0) continue;
      Step& step=row[at];
      step.note=clamp(base+e.note,0,127);
      if(!dicePercussion(voice) && d.scaleType!=v.scale) {
        const int transpose=m ? m->transpose : 0;
        step.note=clamp(moveDegrees(clamp(step.note+transpose,0,127),0,d.scaleRoot,static_cast<ScaleType>(d.scaleType))-transpose,0,127);
      }
      step.vel=e.velocity;
      eventFx(step,e,voice,!dicePercussion(voice));
      const int displacement=tick-at*destTicks;
      const int human=feel && e.at%4 ? (int(rng.below(9))-4)*feel/100 : 0;
      const int nudge=clamp(displacement*100/destTicks+human,-50,50);
      if(nudge) putFx(step,Fx::NDG,static_cast<uint8_t>(static_cast<int8_t>(nudge)));
      if(density==DiceDensity::Busy && e.velocity<110 && e.repeats==1 && i%5==4)
        putFx(step,Fx::RAT,2);
    }
  if(!preserveTiming && !kitGrid) {
    dst.trackLen[t]=voice.length ? std::min<int>(dst.length,std::max(1,voice.length*sourceTicks/destTicks)) : 0;
    dst.trackSpeed[t]=voice.speed;
  }
  const int played=dst.trackLen[t] ? std::min<int>(dst.length,dst.trackLen[t]) : dst.length;
  if(!kitGrid) dice_detail::developVoice(row,played,destTicks,source,recipe,v,d.scaleRoot,d.scaleType,m ? m->transpose : 0,plan,nullptr,prior);
}
void repairTies(const Pattern& prior, DicePat& dst, int t) {
  for(int i=0;i<dst.length;++i) {
    Step& step=dst.steps[t][i];
    if(!step.hasNote() || !step.find(Fx::TIE)) continue;
    int distance=1;
    while(distance<dst.length && !prior.steps[t][(i+distance)%dst.length].hasNote()) ++distance;
    if(dst.steps[t][(i+distance)%dst.length].hasNote()) continue;
    for(auto& fx:step.fx) if(fx.cmd==Fx::TIE) fx=FxSlot();
    putFx(step,Fx::GAT,200);  // a removed terminator must not leave an indefinite hold
  }
}
void blend(const Pattern& prior, DicePat& dst, int t, DiceChange change, bool kit, Rng& rng) {
  if(change==DiceChange::Wild) return;
  const bool subtle=change==DiceChange::Subtle;
  int attacks=0, count=0, first=-1;
  uint8_t candidates[kMaxSteps]; bool selected[kMaxSteps]={};
  for(int i=0;i<dst.length;++i) {
    const Step& old=prior.steps[t][i];
    if(old.hasNote()) { ++attacks; if(first<0) first=i; }
    if(subtle ? old.hasNote() : memcmp(&old,&dst.steps[t][i],sizeof(Step))!=0) candidates[count++]=i;
  }
  if(!attacks) return;
  const int budget=std::min(count,std::max(1,attacks/(subtle ? 4 : 2)));
  for(int j=0;j<budget;++j) {
    const int pick=j+rng.below(count-j); std::swap(candidates[j],candidates[pick]);
    selected[candidates[j]]=true;
  }
  for(int i=0;i<kMaxSteps;++i) {
    Step& step=dst.steps[t][i];
    if(!selected[i]) { step=prior.steps[t][i]; continue; }
    if(subtle) {
      const int velocity=kit ? step.note : step.vel;
      step=prior.steps[t][i];
      const int old=kit ? step.note : step.vel ? step.vel : 100;
      const int next=clamp(velocity ? velocity : old+5,std::max(1,old-8),std::min(127,old+8));
      if(kit) step.note=next; else step.vel=next;
    }
  }
  bool any=false;
  for(int i=0;i<dst.length;++i) any|=dst.steps[t][i].hasNote();
  if(!any) dst.steps[t][first]=prior.steps[t][first];
  repairTies(prior,dst,t);
}
// Section reductions act on the guide theme, never synthesize a missing role or pitch.
void section(DicePat& dst, int t, const DiceVoice& voice, DiceForm form, int index) {
  bool thin=false, withdraw=false;
  const bool variation=(form==DiceForm::Classic && index==1) ||
    (form==DiceForm::Develop && index==2) || (form==DiceForm::Arrange && index==3);
  const auto kind=static_cast<DiceVoiceKind>(voice.kind);
  const bool low=kind==DiceVoiceKind::Kick || kind==DiceVoiceKind::Bass;
  if(form==DiceForm::Classic) { thin=index==2 && low; withdraw=index==2; }
  if(form==DiceForm::Develop) { thin=(index==0 && low); withdraw=index==3; }
  if(form==DiceForm::Arrange) { thin=(index==0 || index==2 || index==3) && low; withdraw=index==5; }
  int seen=0;
  for(int i=0;i<dst.length;++i) if(dst.steps[t][i].hasNote()) {
    if((thin && seen++%2) || (withdraw && i>=dst.length*3/4)) dst.steps[t][i]=Step();
    else if(variation && i>=dst.length*3/4) {
      Step& step=dst.steps[t][i];
      if(dicePercussion(voice)) putFx(step,Fx::RAT,2);
      else step.vel=clamp(step.vel+6,1,127);
    }
  }
  if(withdraw && !dicePercussion(voice)) dst.steps[t][dst.length*3/4].note=kNoteOff;
}
void song(DiceSong& out, DiceForm form) {
  out=DiceSong();
  static const uint8_t chains[][12]={{0,0,1,0,2,3,1},{0,1,1,2,1,2,3},{0,0,1,1,2,3,4,4,1,3,4,5}};
  out.songLen=form==DiceForm::Arrange ? 12 : 7;
  for(int i=0;i<out.songLen;++i) out.song[i]=chains[static_cast<int>(form)][i];
}

void swapBytes(void* a, void* b, size_t n) {
  uint8_t* x = static_cast<uint8_t*>(a);
  uint8_t* y = static_cast<uint8_t*>(b);
  for (size_t i = 0; i < n; ++i) {
    const uint8_t t = x[i];
    x[i] = y[i];
    y[i] = t;
  }
}
#define SWAP(a, b) swapBytes(&(a), &(b), sizeof(a))

// Swaps the parts the roll wrote (s.layers, the masks) between p and s.parts. bpm: the project BPM (p.bpm
// may be a song slot's live tempo); it goes into s.parts, not p.bpm.
void swapWritten(Project& p, DiceSnap& s, uint16_t bpm) {
  DiceParts& d = s.parts;
  if ((s.layers & kDicePattern) && s.namePatterns)
    for (int i = 0; i < s.patCount; ++i) SWAP(p.patterns[s.first + i].name, d.pats[i].name);
  if (s.layers & kDiceTempo) {
    p.bpm = d.bpm;
    d.bpm = bpm;
    SWAP(p.scaleRoot, d.scaleRoot);
    SWAP(p.scaleType, d.scaleType);
  }
  for (int k = 0; k < kDiceRoles; ++k) {
    const int t = kDiceTrack[k];
    if (s.cfgMask & (1u << k)) {
      SWAP(p.tracks[t], d.tracks[k]);
      // Mute / solo stay live (the sequencer writes them too: song mutes, rewind): swapped back.
      SWAP(p.tracks[t].mute, d.tracks[k].mute);
      SWAP(p.tracks[t].solo, d.tracks[k].solo);
    }
    if (s.muteMask & (1u << k)) SWAP(p.tracks[t].mute, d.tracks[k].mute);
    if (s.patMask & (1u << k)) {
      for (int i = 0; i < s.patCount; ++i) {
        Pattern& pt = p.patterns[s.first + i];
        DicePat& dp = d.pats[i];
        SWAP(pt.steps[t], dp.steps[k]);
        SWAP(pt.trackLen[t], dp.trackLen[k]);
        SWAP(pt.trackSpeed[t], dp.trackSpeed[k]);
      }
      SWAP(p.gen[t], d.gen[k]);
      SWAP(p.var[t], d.var[k]);
    }
  }
  if ((s.layers & kDicePattern) && !s.preserveHeaders)
    for (int i = 0; i < s.patCount; ++i) {
      Pattern& pt = p.patterns[s.first + i];
      DicePat& dp = d.pats[i];
      SWAP(pt.length, dp.length);
      SWAP(pt.res, dp.res);
      SWAP(pt.swing, dp.swing);
      SWAP(pt.groove, dp.groove);
    }
  for (int i = 0; i < kInstruments; ++i)
    if (s.instrMask & (1u << i)) SWAP(p.instruments[i], d.instruments[i]);
  if (s.layers & kDiceSong) {
    DiceSong& g = d.song;
    SWAP(p.song, g.song);
    SWAP(p.songMute, g.songMute);
    SWAP(p.songLen, g.songLen);
    SWAP(p.songFol, g.songFol);
    SWAP(p.songTrn, g.songTrn);
    SWAP(p.songBpm, g.songBpm);
  }
}

}  // namespace
int dicePatternCount(const DiceOpts& o) {
  return o.form==DiceForm::Arrange && o.change==DiceChange::Wild &&
    (o.targetMask&63)==63 && (o.layers&kDicePattern) ? kDiceMaxPats : kDicePats;
}
int diceFirstPattern(int, int) { return 0; }
uint16_t diceOverwrites(const Project& p, int, int count) {
  uint16_t result=0;
  for(int i=0;i<clamp(count,1,kDiceMaxPats);++i) if(!p.patterns[i].isEmpty()) result|=1u<<i;
  return result;
}

DiceResult diceDraft(const Project& p, const DiceOpts& input, int, uint32_t seed, DiceSnap& s, uint16_t bpm) {
  DiceOpts o=input;
  seed=o.seed ? o.seed : seed;
  if(!diceRecipe(o.genre,o.style)) o.genre=o.style=0;
  if(o.variant>=kDiceVariants) {
    const int skip=!o.seed && o.avoidVariant<kDiceVariants ? o.avoidVariant : kDiceVariants;
    Rng choice(seed ^ 0x85EBCA6Bu);o.variant=choice.below(skip<kDiceVariants ? kDiceVariants-1 : kDiceVariants);
    if(skip<kDiceVariants && o.variant>=skip) ++o.variant;
  }
  if(o.form>DiceForm::Arrange) o.form=DiceForm::Develop;
  if(o.change>DiceChange::Wild) o.change=DiceChange::Wild;
  if(!(o.layers&kDicePattern)) o.change=DiceChange::Wild;
  if((o.layers&kDiceAll)==kDiceTempo) { o.lockMask=0; o.targetMask=63; }
  const uint8_t active=o.targetMask & ~o.lockMask & 63;
  const bool full=(o.targetMask&63)==63 && !(o.lockMask&63) && o.change==DiceChange::Wild;
  uint8_t layers=o.layers & (kDiceAll|kDiceSong);
  if(!full) layers &= ~(kDiceTempo|kDiceSong);
  if(!(layers&kDicePattern)) layers &= ~kDiceSong;
  if((o.layers&kDicePattern) && o.change!=DiceChange::Wild) layers &= ~kDiceSounds;
  DiceResult result; result.seed=seed; result.genre=o.genre; result.style=o.style; result.variant=o.variant;
  s.drafted=0;
  if(!active || !layers) return result;
  const auto& recipe=*diceRecipe(o.genre,o.style);
  const auto& v=*diceVariant(o.genre,o.style,o.variant);
  s.layers=0; s.first=0; s.patCount=dicePatternCount(o);
  s.cfgMask=s.patMask=s.muteMask=0; s.instrMask=0;
  s.preserveHeaders=!full; s.namePatterns=full && (layers&kDicePattern);
  DiceParts& d=s.parts;
  d.bpm=(layers&kDiceTempo) ? v.bpm : bpm ? bpm : p.bpm;
  const auto key=(layers&kDiceTempo) ? dice_detail::rollKey(p,v,o,seed) : dice_detail::RollKey{p.scaleRoot,p.scaleType};
  d.scaleRoot=key.root;d.scaleType=key.scale;
  std::copy(p.instruments,p.instruments+kInstruments,d.instruments);
  const DiceLayout layout=diceLayout(v);
  auto plan=dice_detail::phrasePlan(v,seed,clamp(o.variety,0,100));
  if(s.preserveHeaders) {
    plan.harmonic[1]=plan.harmonic[2]=0;
    std::fill(plan.progression,plan.progression+4,0);
  }
  const uint8_t protectedMask=dice_detail::protectedFormats(p,layout,active,(layers&kDicePattern) ? s.patCount : 0,layers&kDiceSounds);
  const uint8_t writable=active & ~protectedMask;
  result.missingMask=protectedMask;
  const auto allocated=dice_detail::allocate(p,layout,(layers&kDiceSounds) ? writable : 0);
  for(int t=0;t<kDiceRoles;++t) {
    d.tracks[t]=p.tracks[t];d.gen[t]=p.gen[t];d.var[t]=p.var[t];
    if(!(writable&bit(t))) continue;
    const int source=layout.source[t];
    const int slot=allocated.track[t];
    if(layers&kDiceSounds) {
      if(source==kDiceUnusedTrack) {
        snprintf(d.tracks[t].name,sizeof(d.tracks[t].name),"-");s.cfgMask|=bit(t);
      } else if(slot==kNoInstr) result.missingMask|=bit(t);
      else if(source>=0) {
        Rng rng(seed ^ (0xC2B2AE35u*(source+1)));
        dice_detail::buildVoice(diceVoice(v,source),d.instruments[slot],d.tracks[t],o.clean,clamp(o.variety,0,100),rng);
        d.tracks[t].instr=slot;s.instrMask|=1u<<slot;s.cfgMask|=bit(t);
      } else if(diceSupplement(source)) {
        dice_detail::buildCompanion(dicePartKind(source),v,d.instruments[slot],d.tracks[t],o.clean,
          clamp(o.variety,0,100),seed ^ (0xC2B2AE35u*(t+1)));
        d.tracks[t].instr=slot;s.instrMask|=1u<<slot;s.cfgMask|=bit(t);
      } else {
        Instrument& kit=d.instruments[slot];kit=Instrument();kit.type=InstrType::Kit;kitSetDefaults(kit);
        snprintf(kit.name,sizeof(kit.name),"DICE KIT");
        if(o.clean) d.tracks[t]=TrackCfg();
        snprintf(d.tracks[t].name,sizeof(d.tracks[t].name),"DRUMS");
        d.tracks[t].instr=slot;d.tracks[t].out=TrackOut::Int;d.tracks[t].vol=100;
        int delay=0,reverb=0,drive=0,voices=0;
        for(int voice=0;voice<kDiceRoles;++voice) if(layout.drums&bit(voice)) {
          const int instrument=allocated.drum[voice],lane=layout.lane[voice];
          TrackCfg level;Rng rng(seed ^ (0xC2B2AE35u*(voice+1)));
          const DiceVoice& spec=diceVoice(v,voice);
          delay+=spec.delay;reverb+=spec.reverb;++voices;
          dice_detail::buildVoice(spec,d.instruments[instrument],level,true,clamp(o.variety,0,100),rng);
          auto& drum=d.instruments[instrument];drive+=level.drive;drum.vol=drum.vol*level.vol/100;drum.transpose=clamp(spec.pitch-lane,-24,24);
          kit.kit[lane].instr=instrument;kit.kit[lane].vol=level.vol;
          s.instrMask|=1u<<instrument;
        }
        kit.send=delay/voices;kit.rsend=reverb/voices;d.tracks[t].drive=drive/voices;  // KIT shares its send buses
        s.instrMask|=1u<<slot;s.cfgMask|=bit(t);
      }
    }
    if((layers&kDiceSounds) && source==kDiceKitTrack && slot==kNoInstr) continue;
    if(!(layers&kDicePattern)) continue;
    const bool kit=d.tracks[t].out==TrackOut::Int && d.tracks[t].instr<kInstruments && d.instruments[d.tracks[t].instr].type==InstrType::Kit;
    if(source!=kDiceUnusedTrack && kit!=(source==kDiceKitTrack)) {result.missingMask|=bit(t);continue;}
    const auto& cfg=d.tracks[t];
    const Instrument* instrument=cfg.out==TrackOut::Int && cfg.instr<kInstruments ? &d.instruments[cfg.instr] : nullptr;
    if(diceSupplement(source) && !dice_detail::companionPlayable(cfg,instrument,dicePartKind(source))) {result.missingMask|=bit(t);continue;}
    s.patMask|=bit(t);s.muteMask|=bit(t);d.gen[t].mode=0;d.var[t].mode=0;
    bool hasEvents=diceSupplement(source);
    for(int voice=0;voice<kDiceRoles;++voice)
      if(voice==source || (source==kDiceKitTrack && (layout.drums&bit(voice)))) hasEvents|=diceEventCount(diceVoice(v,voice))>0;
    d.tracks[t].mute=o.change==DiceChange::Wild ? !hasEvents : p.tracks[t].mute;
  }
  static const char* names[][6]={{"MAIN","FILL","BREAK","DROP"},{"INTRO","MAIN","VAR","OUTRO"},
    {"INTRO","MAIN","BREAK","BUILD","DROP","OUTRO"}};
  for(int i=0;i<s.patCount;++i) {
    auto& dst=d.pats[i];const auto& prior=p.patterns[i];
    snprintf(dst.name,sizeof(dst.name),"%s",s.namePatterns ? names[static_cast<int>(o.form)][i] : prior.name);
    dst.length=s.preserveHeaders ? prior.length : (o.len==16 || o.len==32 || o.len==64 || o.len==128) ? o.len : recipe.length;
    dst.res=s.preserveHeaders ? prior.res : static_cast<Resolution>(recipe.resolution);
    dst.swing=s.preserveHeaders ? prior.swing : v.swing;dst.groove=s.preserveHeaders ? prior.groove : 0;
    for(int t=0;t<kDiceRoles;++t) {
      dst.trackLen[t]=prior.trackLen[t];dst.trackSpeed[t]=prior.trackSpeed[t];
      if(!(s.patMask&bit(t))) continue;
      std::fill(dst.steps[t],dst.steps[t]+kMaxSteps,Step());
      const int source=layout.source[t];
      if(source==kDiceUnusedTrack) {
        if(o.change!=DiceChange::Wild) std::copy(prior.steps[t],prior.steps[t]+kMaxSteps,dst.steps[t]);
        continue;
      }
      const auto& cfg=d.tracks[t];
      const Instrument* m=cfg.out==TrackOut::Int && cfg.instr<kInstruments ? &d.instruments[cfg.instr] : nullptr;
      const bool sections=(o.targetMask&63)==63 && o.change==DiceChange::Wild;
      if(source>=0) {
        const auto& voice=diceVoice(v,source);Rng rng(seed ^ (0x9E3779B9u*(source+1)));
        writeVoice(dst,t,voice,recipe,v,d,m,s.preserveHeaders,source,plan,
          o.change==DiceChange::Wild ? o.density : DiceDensity::Normal,clamp(o.groove,0,100),rng,false,(!o.seed && o.change==DiceChange::Wild) ? prior.steps[t] : nullptr);
        if(sections) section(dst,t,voice,o.form,i);
      } else if(diceSupplement(source)) {
        dice_detail::writeCompanion(dst,t,dicePartKind(source),recipe,v,d,m,plan,s.hybrid,
          s.preserveHeaders,o.form,sections ? i : -1,o.change==DiceChange::Wild ? o.density : DiceDensity::Normal);
      } else {
        if(!s.preserveHeaders) {dst.trackLen[t]=0;dst.trackSpeed[t]=0;}
        auto& rows=s.voices;rows.length=dst.length;rows.res=dst.res;
        for(auto& row:rows.steps) std::fill(row,row+kMaxSteps,Step());
        for(int voice=0;voice<kDiceRoles;++voice) if(layout.drums&bit(voice)) {
          rows.trackSpeed[voice]=dst.trackSpeed[t];rows.trackLen[voice]=dst.trackLen[t];
          Rng rng(seed ^ (0x9E3779B9u*(voice+1)));const auto& spec=diceVoice(v,voice);
          writeVoice(rows,voice,spec,recipe,v,d,nullptr,true,voice,plan,
            o.change==DiceChange::Wild ? o.density : DiceDensity::Normal,clamp(o.groove,0,100),rng,true);
          if(sections) section(rows,voice,spec,o.form,i);
        }
        const auto speed=toSpeed(dst.trackSpeed[t]);
        const int stepTicks=ticksPerStep(dst.res)*speedDiv(speed)/speedMul(speed);
        for(int voice=0;voice<kDiceRoles;++voice) if(layout.drums&bit(voice))
          dice_detail::developVoice(rows.steps[voice],rows.length,stepTicks,voice,recipe,v,d.scaleRoot,d.scaleType,0,plan,rows.steps);
        const bool fill=(o.form==DiceForm::Classic && i==1) || (o.form==DiceForm::Develop && i==2) || (o.form==DiceForm::Arrange && i==3);
        dice_detail::packKit(dst,rows,layout,seed,fill);
      }
      Rng blendRng(seed ^ (0x9E3779B9u*(t+1)));blend(prior,dst,t,o.change,source==kDiceKitTrack,blendRng);
    }
  }
  if(layers&kDiceSong) song(d.song,o.form);
  if(!s.cfgMask) layers &= ~kDiceSounds;
  if(!s.patMask) layers &= ~(kDicePattern|kDiceSong);
  s.drafted=layers;
  result.written=s.cfgMask|s.patMask|s.muteMask;
  return result;
}
uint8_t diceApply(Project& p, DiceSnap& s, uint16_t bpm) {
  if(!s.drafted) return 0;
  s.layers=s.drafted; s.drafted=0;
  swapWritten(p,s,bpm ? bpm : p.bpm);
  return s.layers;
}
DiceResult diceRoll(Project& p, const DiceOpts& o, int edit, uint32_t seed, DiceSnap& s, uint16_t bpm) {
  const auto result=diceDraft(p,o,edit,seed,s,bpm); diceApply(p,s,bpm); return result;
}
uint8_t diceUndo(Project& p, DiceSnap& s, uint16_t bpm) {
  const uint8_t layers=s.layers;
  if(!layers) return 0;
  swapWritten(p,s,bpm ? bpm : p.bpm);
  s.layers=0; s.cfgMask=s.patMask=s.muteMask=0; s.instrMask=0;
  return layers;
}
}  // namespace mt
