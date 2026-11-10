#include <unity.h>
#include <initializer_list>
#include <cstring>
#include "dice.h"
#include "templates.h"
using namespace mt;
static Project p, base;
static DiceSnap s;
void setUp() { templateBuild(0,p); }
void tearDown() {}
static int hits(int pat,int role) { int n=0;for(int i=0;i<p.patterns[pat].length;++i)n+=p.patterns[pat].steps[role][i].hasNote();return n; }
void test_develop_intro_is_sparse_and_main_retains_motif() {
  DiceOpts o; o.root=0;o.variety=0;o.len=64;
  diceRoll(p,o,0,777,s);
  int intro=0,main=0;
  for(int r=0;r<5;++r){intro+=hits(0,r);main+=hits(1,r);}
  TEST_ASSERT_LESS_THAN(main,intro);
  TEST_ASSERT_GREATER_THAN(0,hits(0,3));
  TEST_ASSERT_GREATER_THAN(0,hits(1,1));
  for(int i=0;i<64;++i) if(p.patterns[1].steps[3][i].hasNote()&&p.patterns[2].steps[3][i].hasNote())
    TEST_ASSERT_INT_WITHIN(12,p.patterns[1].steps[3][i].note,p.patterns[2].steps[3][i].note);
}
void test_develop_groove_is_shared_and_bounded() {
  DiceOpts o;o.root=0;o.variety=0;o.len=64;
  diceRoll(p,o,0,777,s);
  int delayed=0;
  for(int r=0;r<5;++r)for(int i=0;i<64;++i) if(const auto* f=p.patterns[1].steps[r][i].find(Fx::NDG)) {
    TEST_ASSERT_INT_WITHIN(12,0,static_cast<int8_t>(f->val));++delayed;
  }
  TEST_ASSERT_GREATER_THAN(0,delayed);
}
struct HarmonySource : DicePhraseSource {
  bool pick(int, DiceRole role, uint32_t, DicePhrase& phrase) const override {
    phrase = DicePhrase();
    if (role == DiceRole::Chords) {
      phrase.count = 1; phrase.events[0] = {0, 384, 60, 100, 255}; return true;
    }
    if (role != DiceRole::Lead) return false;
    phrase.count = 4;
    phrase.events[0] = {0, 12, 62, 90, 255};
    phrase.events[1] = {30, 12, 62, 80, 255};
    phrase.events[2] = {96, 12, 62, 90, 255};
    phrase.events[3] = {126, 12, 62, 80, 255};
    return true;
  }
};
void test_harmony_retains_passing_notes_and_midi_timing() {
  HarmonySource source;
  DiceOpts o; o.layers=kDicePattern;o.source=DiceSource::Midi;o.form=DiceForm::Classic;o.groove=100;
  o.targetMask=(1u<<2)|(1u<<3);o.len=16;
  diceRoll(p,o,0,777,s,0,&source);
  const Step* lead=p.patterns[0].steps[3];
  TEST_ASSERT_NOT_EQUAL(62,lead[0].note);
  TEST_ASSERT_EQUAL(62,lead[1].note);
  TEST_ASSERT_NOT_NULL(lead[1].find(Fx::NDG));
  TEST_ASSERT_EQUAL(25,lead[1].find(Fx::NDG)->val);
  TEST_ASSERT_EQUAL(80,lead[1].vel);
}
void test_zero_groove_and_locked_roles_are_exact() {
  DiceOpts o;o.form=DiceForm::Classic;o.groove=0;o.variety=0;o.len=64;
  diceRoll(p,o,0,777,s);
  base=p;
  o.targetMask=1u<<3;
  diceRoll(p,o,0,999,s);
  for(int pat=0;pat<4;++pat)for(int r=0;r<6;++r) if(r!=3)
    TEST_ASSERT_EQUAL_MEMORY(base.patterns[pat].steps[r],p.patterns[pat].steps[r],sizeof(p.patterns[pat].steps[r]));
  for(int r=0;r<6;++r)if(r!=3)
    TEST_ASSERT_EQUAL_MEMORY(&base.tracks[r],&p.tracks[r],sizeof(TrackCfg));
  TEST_ASSERT_EQUAL_MEMORY(base.instruments,p.instruments,sizeof(p.instruments));
}
void test_short_phrases_have_audible_attack_and_bounded_release() {
  HarmonySource source;
  DiceOpts o;o.targetMask=1u<<3;o.source=DiceSource::Midi;o.groove=0;
  diceRoll(p,o,0,777,s,0,&source);
  const Instrument& voice=p.instruments[p.tracks[3].instr];
  TEST_ASSERT_LESS_OR_EQUAL(25,envTimeMs(voice.attack));
  TEST_ASSERT_LESS_OR_EQUAL(80,envTimeMs(voice.release));
}
void test_target_lead_exists_in_a_style_that_omits_it() {
  DiceOpts o;o.genre=0;o.style=1;o.variety=0;o.targetMask=1u<<3;
  diceRoll(p,o,0,777,s);
  TEST_ASSERT_FALSE(p.tracks[3].mute);
  TEST_ASSERT_GREATER_THAN(0,hits(0,3));
  TEST_ASSERT_GREATER_THAN(0,hits(1,3));
}
void test_explicit_roles_are_playable_in_every_style() {
  for(int genre=0;genre<diceGenreCount();++genre)
    for(int style=0;style<diceStyleCount(genre);++style)
      for(int role=0;role<kDiceRoles;++role) {
        templateBuild(0,p);
        DiceOpts o;o.genre=genre;o.style=style;o.variety=0;o.targetMask=1u<<role;
        diceRoll(p,o,0,777,s);
        int notes=0;for(int pat=0;pat<4;++pat)notes+=hits(pat,role);
        TEST_ASSERT_GREATER_THAN_MESSAGE(0,notes,diceStyleName(genre,style));
        TEST_ASSERT_FALSE_MESSAGE(p.tracks[role].mute,diceStyleName(genre,style));
        if(role==0) {
          int initial=0;for(int i=0;i<8;++i)initial+=p.patterns[0].steps[0][i].hasNote();
          TEST_ASSERT_GREATER_THAN_MESSAGE(0,initial,diceStyleName(genre,style));
        }
      }
}
void test_full_roll_has_no_interior_empty_roles_in_any_pattern() {
  for(int g=0;g<diceGenreCount();++g)for(int style=0;style<diceStyleCount(g);++style)
    for(uint32_t seed:{1u,777u,8765u}) for(DiceForm form:{DiceForm::Classic,DiceForm::Develop}) {
      templateBuild(0,p);DiceOpts o;o.genre=g;o.style=style;o.form=form;
      diceRoll(p,o,0,seed,s);
      for(int pat=0;pat<kDicePats;++pat){
        int first=6,last=-1;
        for(int r=0;r<6;++r)if(hits(pat,r)){if(first==6)first=r;last=r;}
        for(int r=first;r<=last;++r){
          TEST_ASSERT_GREATER_THAN_MESSAGE(0,hits(pat,r),diceStyleName(g,style));
          TEST_ASSERT_FALSE(p.tracks[r].mute);
        }
      }
    }
}
void test_gap_fill_keeps_locked_empty_role_and_ambient_leading_space() {
  templateBuild(0,p);DiceOpts o;o.genre=0;o.style=1;o.lockMask=4;
  for(auto& pat:p.patterns)for(auto& step:pat.steps[2])step=Step();
  diceRoll(p,o,0,777,s);
  for(int pat=0;pat<4;++pat)TEST_ASSERT_EQUAL(0,hits(pat,2));
  templateBuild(0,p);o=DiceOpts();o.genre=7;o.style=0;
  diceRoll(p,o,0,777,s);
  for(int pat=0;pat<4;++pat){TEST_ASSERT_EQUAL(0,hits(pat,0));TEST_ASSERT_EQUAL(0,hits(pat,1));}
}
void test_gap_fill_uses_each_locked_pattern_length() {
  templateBuild(0,p);const uint8_t lengths[]={4,7,64,128};
  for(int pat=0;pat<4;++pat){p.patterns[pat].length=lengths[pat];p.patterns[pat].steps[5][0].note=60;}
  DiceOpts o;o.lockMask=32;diceRoll(p,o,0,777,s);
  for(int pat=0;pat<4;++pat){
    TEST_ASSERT_EQUAL(lengths[pat],p.patterns[pat].length);
    for(int role=0;role<5;++role){
      TEST_ASSERT_GREATER_THAN(0,hits(pat,role));
      for(int at=lengths[pat];at<kMaxSteps;++at)TEST_ASSERT_TRUE(p.patterns[pat].steps[role][at].isEmpty());
    }
  }
}
int main(){UNITY_BEGIN();RUN_TEST(test_gap_fill_uses_each_locked_pattern_length);RUN_TEST(test_full_roll_has_no_interior_empty_roles_in_any_pattern);RUN_TEST(test_gap_fill_keeps_locked_empty_role_and_ambient_leading_space);RUN_TEST(test_develop_intro_is_sparse_and_main_retains_motif);RUN_TEST(test_develop_groove_is_shared_and_bounded);RUN_TEST(test_harmony_retains_passing_notes_and_midi_timing);RUN_TEST(test_zero_groove_and_locked_roles_are_exact);RUN_TEST(test_short_phrases_have_audible_attack_and_bounded_release);RUN_TEST(test_target_lead_exists_in_a_style_that_omits_it);RUN_TEST(test_explicit_roles_are_playable_in_every_style);return UNITY_END();}
