#include <unity.h>
#include "synth_voice.h"
#include "synth.h"
#include <memory>
#include <cstring>
using namespace mt;
static Voice voices[kVoices];
void setUp(){for(auto& v:voices)v=Voice();}
void tearDown(){}
static void start(int i,int cost,bool stolen){auto&v=voices[i];v.on=true;v.fm=true;v.cost=cost;v.age=i+1;v.stolen=stolen;v.env.set(0,0,1,100);v.env.gate(true);v.env.next();v.env.next();}
void test_already_stolen_engines_still_consume_cpu_budget(){start(0,90,true);start(1,90,false);enforceBudget(voices,2,90,200);TEST_ASSERT_FALSE(voices[0].fm);TEST_ASSERT_TRUE(voices[0].stolen);TEST_ASSERT_TRUE(voices[1].fm);}
void test_budget_off_preserves_full_engine_tail(){start(0,90,true);enforceBudget(voices,1,90,kVoiceBudgetOff);TEST_ASSERT_TRUE(voices[0].fm);}
void test_hard_reused_note_stolen_before_first_render_has_no_previous_pcm() {
  struct Bank:SampleSource {
    int16_t positive[64], silent[64]{};
    Bank(){for(auto& x:positive)x=1000;}
    const int16_t* find(const char* name,uint32_t& n,uint32_t& rate) const override {
      n=64;rate=kSynthRate;return strcmp(name,"old")==0?positive:silent;
    }
  } bank;
  auto p=std::make_unique<Project>();
  auto& m=p->instruments[0];instrSetType(m,InstrType::Sample);strcpy(m.sample,"old");
  m.root=60;m.loop=1;m.vol=127;m.attack=0;m.decay=0;m.sustain=127;m.mono=false;
  for(int t=0;t<kTracks;++t){p->tracks[t].out=TrackOut::Int;p->tracks[t].instr=0;p->tracks[t].vol=127;}
  auto s=std::make_unique<Synth>(*p);s->setBank(&bank);
  int16_t pcm[Synth::kBlock];const uint8_t first[]={0x90,60,127};
  TEST_ASSERT_TRUE(s->event(0,0,first,3));s->render(pcm);TEST_ASSERT_TRUE(pcm[0]>0);
  strcpy(m.sample,"silent");
  for(int i=1;i<kVoices;++i){const uint8_t note[]={0x90,static_cast<uint8_t>(60+i/kTracks),127};TEST_ASSERT_TRUE(s->event(0,i%kTracks,note,3));}
  s->render(pcm);TEST_ASSERT_EQUAL_INT(kVoices,s->activeVoices());
  s->setVoiceBudget(kCostSample);
  const uint8_t replacement[]={0x90,80,127};
  TEST_ASSERT_TRUE(s->event(0,0,replacement,3));TEST_ASSERT_TRUE(s->event(0,1,replacement,3));
  s->render(pcm);
  for(int16_t x:pcm)TEST_ASSERT_EQUAL_INT(0,x);
}
int main(){UNITY_BEGIN();RUN_TEST(test_already_stolen_engines_still_consume_cpu_budget);RUN_TEST(test_budget_off_preserves_full_engine_tail);RUN_TEST(test_hard_reused_note_stolen_before_first_render_has_no_previous_pcm);return UNITY_END();}
