#include <unity.h>
#include <cstring>
#include <initializer_list>
#include <cstdio>
#include "dice.h"
#include "templates.h"
using namespace mt;
static Project p,normal,initial;
static DiceSnap snap;
void setUp(){templateBuild(0,p);}
void tearDown(){}
static int attacks(const Project& q,int pattern=-1,int role=-1){
  int n=0;
  for(int pat=0;pat<4;++pat)if(pattern<0||pattern==pat)
    for(int r=0;r<6;++r)if(role<0||r==role)
      for(int i=0;i<q.patterns[pat].length;++i)n+=q.patterns[pat].steps[r][i].hasNote();
  return n;
}
static uint32_t fingerprint(const Project& q){
  uint32_t hash=2166136261u;
  for(int pat=0;pat<4;++pat)for(int r=0;r<6;++r){
    const auto* bytes=reinterpret_cast<const uint8_t*>(q.patterns[pat].steps[r]);
    for(size_t i=0;i<sizeof(q.patterns[pat].steps[r]);++i)hash=(hash^bytes[i])*16777619u;
  }
  return hash;
}
static DiceOpts opts(){DiceOpts o;o.root=0;o.variety=30;o.len=64;return o;}
void test_normal_retains_existing_generated_steps(){
  DiceOpts o=opts();diceRoll(p,o,0,777,snap);
  TEST_ASSERT_EQUAL_UINT32(2550594343u,fingerprint(p));
  TEST_ASSERT_GREATER_THAN(0,attacks(p));
}
void test_sparse_thins_without_emptying_roles_or_changing_kept_notes(){
  DiceOpts o=opts();diceRoll(p,o,0,777,snap);normal=p;
  templateBuild(0,p);o.density=DiceDensity::Sparse;diceRoll(p,o,0,777,snap);
  TEST_ASSERT_LESS_THAN(attacks(normal),attacks(p));
  for(int pat=0;pat<4;++pat)for(int role=0;role<6;++role){
    if(attacks(normal,pat,role))TEST_ASSERT_GREATER_THAN(0,attacks(p,pat,role));
    for(int i=0;i<p.patterns[pat].length;++i){
      const Step& old=normal.patterns[pat].steps[role][i];
      const Step& now=p.patterns[pat].steps[role][i];
      if(now.hasNote())TEST_ASSERT_EQUAL_MEMORY(&old,&now,sizeof(Step));
      if(old.hasNote()&&(role==2||role==5||(i*ticksPerStep(p.patterns[pat].res))%96==0))
        TEST_ASSERT_EQUAL_MEMORY(&old,&now,sizeof(Step));
    }
  }
}
void test_busy_adds_bounded_soft_notes_and_retains_existing_steps(){
  DiceOpts o=opts();diceRoll(p,o,0,777,snap);normal=p;
  templateBuild(0,p);o.density=DiceDensity::Busy;diceRoll(p,o,0,777,snap);
  TEST_ASSERT_GREATER_THAN(attacks(normal),attacks(p));
  TEST_ASSERT_LESS_OR_EQUAL(attacks(normal)+80,attacks(p));
  for(int pat=0;pat<4;++pat)for(int role=0;role<6;++role){
    for(int i=0;i<p.patterns[pat].length;++i){
      const Step& old=normal.patterns[pat].steps[role][i];const Step& now=p.patterns[pat].steps[role][i];
      if(!old.isEmpty()||role==2||role==5)TEST_ASSERT_EQUAL_MEMORY(&old,&now,sizeof(Step));
      else if(now.hasNote()){
        TEST_ASSERT_LESS_OR_EQUAL(80,role==0?now.note:now.vel);
        TEST_ASSERT_NOT_NULL(now.find(Fx::GAT));
        TEST_ASSERT_LESS_OR_EQUAL(100,gatePercent(now.find(Fx::GAT)->val));
      }
      for(const auto& fx:now.fx)TEST_ASSERT_LESS_THAN(static_cast<int>(Fx::Count),static_cast<int>(fx.cmd));
    }
  }
}
void test_density_is_repeatable_and_does_not_touch_locked_roles(){
  DiceOpts o=opts();diceRoll(p,o,0,777,snap);initial=p;o.layers=kDicePattern;
  for(DiceDensity density:{DiceDensity::Sparse,DiceDensity::Busy}){
    p=initial;o.density=density;o.targetMask=8;diceRoll(p,o,0,123,snap);normal=p;
    p=initial;diceRoll(p,o,0,123,snap);TEST_ASSERT_EQUAL_MEMORY(&normal,&p,sizeof(Project));
    for(int pat=0;pat<4;++pat)for(int role=0;role<6;++role)if(role!=3)
      TEST_ASSERT_EQUAL_MEMORY(initial.patterns[pat].steps[role],p.patterns[pat].steps[role],sizeof(p.patterns[pat].steps[role]));
    TEST_ASSERT_EQUAL_MEMORY(initial.instruments,p.instruments,sizeof(p.instruments));
  }
}
void test_density_is_disabled_for_bounded_motif_changes(){
  DiceOpts o=opts();diceRoll(p,o,0,777,snap);initial=p;
  for(DiceChange change:{DiceChange::Subtle,DiceChange::Normal}){
    o.change=change;o.density=DiceDensity::Normal;p=initial;diceRoll(p,o,0,123,snap);normal=p;
    for(DiceDensity density:{DiceDensity::Sparse,DiceDensity::Busy}){
      o.density=density;p=initial;diceRoll(p,o,0,123,snap);
      TEST_ASSERT_EQUAL_MEMORY(&normal,&p,sizeof(Project));
    }
  }
}
struct DensityMidi : DicePhraseSource {
  bool pick(int,DiceRole role,uint32_t,DicePhrase& phrase)const override{
    phrase=DicePhrase();phrase.bars=1;
    if(role==DiceRole::Chords){phrase.count=3;phrase.events[0]={0,384,60,85,255};phrase.events[1]={0,384,64,80,255};phrase.events[2]={0,384,67,75,255};return true;}
    if(role==DiceRole::Bass){phrase.count=1;phrase.events[0]={0,384,36,100,255};return true;}
    if(role!=DiceRole::Lead)return false;
    phrase.count=8;
    for(int i=0;i<8;++i)phrase.events[i]={static_cast<uint16_t>(i*48+6),12,static_cast<uint8_t>(60+i%3*2),static_cast<uint8_t>(70+i),255};
    return true;
  }
};
void test_density_respects_imported_articulation_and_held_chords(){
  DensityMidi library;DiceOpts o=opts();o.source=DiceSource::Midi;o.form=DiceForm::Classic;o.targetMask=14;
  diceRoll(p,o,0,777,snap,0,&library);normal=p;
  for(DiceDensity density:{DiceDensity::Sparse,DiceDensity::Busy}){
    templateBuild(0,p);o.density=density;diceRoll(p,o,0,777,snap,0,&library);
    for(int pat=0;pat<4;++pat){
      TEST_ASSERT_EQUAL_MEMORY(normal.patterns[pat].steps[2],p.patterns[pat].steps[2],sizeof(p.patterns[pat].steps[2]));
      if(density==DiceDensity::Busy)
        TEST_ASSERT_EQUAL_MEMORY(normal.patterns[pat].steps[1],p.patterns[pat].steps[1],sizeof(p.patterns[pat].steps[1]));
      for(int i=0;i<p.patterns[pat].length;++i){
        const Step& old=normal.patterns[pat].steps[3][i];const Step& now=p.patterns[pat].steps[3][i];
        if(old.hasNote()&&now.hasNote())TEST_ASSERT_EQUAL_MEMORY(&old,&now,sizeof(Step));
      }
    }
  }
}
void test_density_preserves_roles_and_grid_bounds_in_every_style(){
  for(int genre=0;genre<diceGenreCount();++genre)for(int style=0;style<diceStyleCount(genre);++style){
    DiceOpts o=opts();o.genre=genre;o.style=style;
    templateBuild(0,p);diceRoll(p,o,0,777,snap);normal=p;
    for(DiceDensity density:{DiceDensity::Sparse,DiceDensity::Busy}){
      templateBuild(0,p);o.density=density;diceRoll(p,o,0,777,snap);
      for(int pat=0;pat<4;++pat)for(int role=0;role<6;++role){
        if(attacks(normal,pat,role))TEST_ASSERT_GREATER_THAN(0,attacks(p,pat,role));
        else TEST_ASSERT_EQUAL(0,attacks(p,pat,role));
        for(int i=p.patterns[pat].length;i<kMaxSteps;++i)TEST_ASSERT_TRUE(p.patterns[pat].steps[role][i].isEmpty());
      }
    }
  }
}
void test_busy_does_not_interrupt_ties_from_actual_recipes(){
  int heldSpaces=0;
  for(int genre=0;genre<diceGenreCount();++genre)for(int style=0;style<diceStyleCount(genre);++style){
    DiceOpts o=opts();o.genre=genre;o.style=style;
    templateBuild(0,p);diceRoll(p,o,0,777,snap);normal=p;
    templateBuild(0,p);o.density=DiceDensity::Busy;diceRoll(p,o,0,777,snap);
    for(int pat=0;pat<4;++pat)for(int role:{1,3,4}){
      const Step* old=normal.patterns[pat].steps[role];
      const int len=normal.patterns[pat].length;
      for(int at=0;at<len;++at){
        if(!old[at].isEmpty())continue;
        for(int back=1;back<len;++back){
          const Step& prior=old[(at+len-back)%len];
          if(prior.note==kNoteOff||prior.find(Fx::OFF))break;
          if(!prior.hasNote())continue;
          if(prior.find(Fx::TIE)){
            ++heldSpaces;
            TEST_ASSERT_FALSE_MESSAGE(p.patterns[pat].steps[role][at].hasNote(),diceStyleName(genre,style));
          }
          break;
        }
      }
    }
  }
  TEST_ASSERT_GREATER_THAN(0,heldSpaces);
}
int main(){UNITY_BEGIN();RUN_TEST(test_normal_retains_existing_generated_steps);RUN_TEST(test_sparse_thins_without_emptying_roles_or_changing_kept_notes);RUN_TEST(test_busy_adds_bounded_soft_notes_and_retains_existing_steps);RUN_TEST(test_density_is_repeatable_and_does_not_touch_locked_roles);RUN_TEST(test_density_is_disabled_for_bounded_motif_changes);RUN_TEST(test_density_respects_imported_articulation_and_held_chords);RUN_TEST(test_density_preserves_roles_and_grid_bounds_in_every_style);RUN_TEST(test_busy_does_not_interrupt_ties_from_actual_recipes);return UNITY_END();}
