#include <cstring>
#include <initializer_list>
#include <unity.h>
#include "dice_variation.h"
using namespace mt;
static Project p;
static DiceSnap draft, saved;
void setUp() {
  p.reset(); draft = DiceSnap(); draft.first=0; draft.patMask=0x3f;
  for(int pat=0;pat<kDicePats;++pat) {
    p.patterns[pat].length=32; draft.parts.pats[pat].length=32;
    for(int role=0;role<kDiceRoles;++role) for(int i=0;i<32;++i) {
      auto& old=p.patterns[pat].steps[role][i]; old.note=60; old.vel=90;
      auto& fresh=draft.parts.pats[pat].steps[role][i]; fresh.note=72; fresh.vel=110;
    }
  }
}
void tearDown() {}
static int unchanged(int pat,int role) {
  int n=0; for(int i=0;i<32;++i) n+=!memcmp(&p.patterns[pat].steps[role][i],&draft.parts.pats[pat].steps[role][i],sizeof(Step)); return n;
}
void test_subtle_preserves_motif_and_changes_minor_part() {
  DiceOpts o; o.change=DiceChange::Subtle; diceVaryDraft(p,o,71,draft);
  for(int pat=0;pat<4;++pat) for(int role=0;role<6;++role) {
    TEST_ASSERT_GREATER_OR_EQUAL(24,unchanged(pat,role));
    TEST_ASSERT_LESS_THAN(32,unchanged(pat,role));
  }
}
void test_normal_changes_half_and_is_deterministic() {
  DiceOpts o; o.change=DiceChange::Normal; saved=draft; diceVaryDraft(p,o,123,draft);
  for(int role=0;role<6;++role) TEST_ASSERT_EQUAL(16,unchanged(0,role));
  const DiceParts expected=draft.parts; draft=saved; diceVaryDraft(p,o,123,draft);
  TEST_ASSERT_EQUAL_MEMORY(&expected,&draft.parts,sizeof(expected));
}
void test_wild_does_not_blend() {
  DiceOpts o; o.change=DiceChange::Wild; saved=draft; diceVaryDraft(p,o,1,draft);
  TEST_ASSERT_EQUAL_MEMORY(&saved,&draft,sizeof(draft));
}
void test_locks_targets_and_masks_are_untouched() {
  DiceOpts o; o.change=DiceChange::Subtle; o.lockMask=1; o.targetMask=7; draft.patMask=0x3b; saved=draft;
  diceVaryDraft(p,o,9,draft);
  for(int role : {0,2,3,4,5}) TEST_ASSERT_EQUAL_MEMORY(saved.parts.pats[0].steps[role],draft.parts.pats[0].steps[role],sizeof(Step)*kMaxSteps);
}
void test_empty_original_uses_generation_and_tail_untouched() {
  DiceOpts o; o.change=DiceChange::Subtle;
  for(int i=0;i<32;++i)p.patterns[0].steps[1][i]=Step();
  draft.parts.pats[0].steps[2][80].note=99; saved=draft; diceVaryDraft(p,o,55,draft);
  TEST_ASSERT_EQUAL_MEMORY(saved.parts.pats[0].steps[1],draft.parts.pats[0].steps[1],sizeof(Step)*kMaxSteps);
  TEST_ASSERT_EQUAL(99,draft.parts.pats[0].steps[2][80].note);
}
void test_changed_strong_beats_follow_preserved_chords() {
  DiceOpts o; o.change=DiceChange::Subtle; o.lockMask=4;
  for(int i=0;i<32;++i) { p.patterns[0].steps[2][i]=Step(); draft.parts.pats[0].steps[1][i].note=66; }
  p.patterns[0].steps[2][0].note=60; p.patterns[0].steps[2][0].fx[0]={Fx::CHD,chordValue(kChordMaj,0)};
  diceVaryDraft(p,o,11,draft);
  for(int i=0;i<32;i+=4) {
    const int pc=draft.parts.pats[0].steps[1][i].note%12;
    TEST_ASSERT_TRUE(pc==0||pc==4||pc==7);
  }
}
void test_retained_part_keeps_original_mute() {
  DiceOpts o; o.change=DiceChange::Subtle; p.tracks[1].mute=false; draft.parts.tracks[1].mute=true;
  diceVaryDraft(p,o,92,draft); TEST_ASSERT_FALSE(draft.parts.tracks[1].mute);
}
int main(){ UNITY_BEGIN(); RUN_TEST(test_subtle_preserves_motif_and_changes_minor_part); RUN_TEST(test_normal_changes_half_and_is_deterministic); RUN_TEST(test_wild_does_not_blend); RUN_TEST(test_retained_part_keeps_original_mute); RUN_TEST(test_locks_targets_and_masks_are_untouched); RUN_TEST(test_empty_original_uses_generation_and_tail_untouched); RUN_TEST(test_changed_strong_beats_follow_preserved_chords); return UNITY_END(); }
