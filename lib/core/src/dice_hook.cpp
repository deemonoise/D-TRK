#include "dice_hook.h"
#include "rng.h"
#include <algorithm>
#include <cstdlib>

namespace mt { namespace dice_detail {
namespace {
// Positions are in order of musical importance. Removing the later positions gives a
// sparse version of the same rhythm. Pairs of voices can phrase on opposite half-bars.
constexpr uint8_t accents[][8]={
  {0,8,12,4,14,6,2,10}, {0,6,12,3,10,15,8,5}, {2,10,6,14,7,12,4,0},
  {0,3,7,11,14,5,9,1}, {4,12,7,15,0,9,2,5}, {0,10,5,14,3,8,12,7},
  {1,7,13,4,10,15,2,9}, {0,12,8,15,6,4,11,2}
};
// Pocket, Broken, Air, Keys, Echo, Motor, Anthem.
constexpr uint8_t rhythms[][4]={{0,1,2,4},{1,3,5,6},{7,4,5,0},{4,1,7,5},
                                {2,4,5,1},{0,1,3,6},{0,7,5,3}};
// Four beats of bass. Bit 0 is reserved for the kick. Separate masks give a
// gallop, an offbeat pedal, a rolling sixteenth, or a displaced response.
constexpr uint8_t bassBeats[][4]={{4,4,4,4},{6,4,6,4},{12,12,4,12},{2,8,2,8},
                                  {14,14,14,12},{4,2,8,4},{8,12,8,6},{6,6,2,8}};
void shapeBass(Step* row,int bar,const RollMotif& motif) {
  for(int beat=0;beat<4;++beat) {
    Step* line=row+beat*4;
    Step notes[4]; int n=0;
    for(int i=0;i<4;++i) if(line[i].hasNote()) notes[n++]=line[i];
    // An empty beat is an intentional breath in the source or in SPARSE.
    if(!n) continue;
    const int rotation=(bar%motif.answerBars==motif.answerBars-1)+(bar>=4);
    int mask=bassBeats[motif.bassRhythm][(beat+rotation)%4];
    if(motif.sparse && mask==14) mask=(beat%2) ? 12 : 6;
    std::fill(line,line+4,Step{});
    int played=0;
    for(int i=1;i<4;++i) if(mask&(1<<i)) {
      auto& step=line[i];
      step=notes[std::min(played,n-1)];
      if(played>=n) {  // a new subdivision is a clean continuation of the beat
        step=Step{};step.note=notes[0].note;step.vel=std::max(1,int(notes[0].vel)-12);
      }
      ++played;
    }
  }
}
void shapeUpper(Step* row,int bar,const RollMotif& motif,bool reply) {
  Step notes[16]; int count=0;
  for(int i=0;i<16;++i) if(row[i].hasNote()) notes[count++]=row[i];
  if(!count) return;
  bool attack[16]={}; int placed=0;
  for(int rank=0;rank<8 && placed<count;++rank) {
    attack[motif.accent(rank,bar,reply)]=true;
    ++placed;
  }
  // Dense original phrases keep their additional articulations.
  for(int i=0;i<16 && placed<count;++i) if(!attack[i] && row[i].hasNote()) {attack[i]=true;++placed;}
  for(int i=0,n=0;i<16;++i) row[i]=attack[i] ? notes[n++] : Step{};
}
void phraseBass(Step* row,int bar,const RollMotif& motif) {
  // Keep the start of the bar and the pulse of alternate beats. Move the
  // pickup inside the other beats: a new offbeat never lands on the kick.
  for(int beat=0;beat<4;++beat) {
    if((beat+bar%motif.answerBars+motif.phase)%2==0) continue;
    Step* start=row+4*beat+1;
    const int direction=(motif.bassRhythm+beat/2)%2 ? 2 : 1;
    std::rotate(start,start+direction,start+3);
  }
}
}

RollMotif rollMotif(uint32_t seed,DiceFeel feel,int variety,bool sparse) {
  RollMotif out;
  if(variety<=60) return out;
  Rng rng(seed ^ 0x761ACD35u);
  out.sparse=sparse;out.length=3+rng.below(6);
  out.rhythm=rhythms[unsigned(feel)][rng.below(4)];
  out.phase=rng.below(4);out.answerBars=rng.below(2) ? 2 : 4;
  out.bassRhythm=rng.below(8);
  const int sign=rng.below(2) ? 1 : -1;
  const int reach=2+rng.below(feel==DiceFeel::Air || feel==DiceFeel::Echo ? 3 : 5);
  const int shape=rng.below(5);
  for(int i=1;i<out.length;++i) {
    int degree=0;
    switch(shape) {
      case 0: degree=i%2 ? reach : 0; break;                    // pedal and leap
      case 1: degree=i*reach/(out.length-1); break;               // staircase
      case 2: degree=reach-std::abs(2*i-out.length)*reach/out.length; break; // arch
      case 3: degree=i<out.length/2 ? 0 : reach-(i-out.length/2); break;    // late answer
      default: degree=(i%3==1 ? reach : i%3==2 ? -2 : 1); break; // alternating registers
    }
    out.degrees[i]=sign*degree;
  }
  for(auto& active:out.rewrite) active=int(rng.below(40))<variety-60;
  return out;
}
int RollMotif::degree(int bar,int attack,bool reply) const {
  const bool answer=bar%answerBars==answerBars-1;
  const int index=(attack+(reply ? length/2 : 0)+(bar>=4 ? 1 : 0))%length;
  const int degree=degrees[answer ? length-1-index : index];
  return reply ? -degree : degree;
}
int RollMotif::accent(int rank,int bar,bool reply) const {
  const int answer=bar%answerBars==answerBars-1 ? 2 : 0;
  return (accents[rhythm][rank]+phase+answer+(reply ? 8 : 0))%16;
}
void phraseRhythm(Step* row,int length,const RollMotif& motif,PhraseRole role) {
  for(int start=0;start+16<=length;start+=16) {
    const int bar=start/16;
    if(!motif.active(bar)) continue;
    auto* line=row+start;
    // Generated arpeggios can carry standalone releases or tie commands.
    // Keep those bars together with the events whose duration they control.
    if(std::any_of(line,line+16,[](const Step& step){return !step.hasNote() && !step.isEmpty();})) continue;
    if(role==PhraseRole::RollingBass) shapeBass(line,bar,motif);
    else if(role==PhraseRole::Bass) phraseBass(line,bar,motif);
    else shapeUpper(line,bar,motif,role==PhraseRole::Reply);
  }
}
}}
