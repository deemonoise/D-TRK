#include <unity.h>
#include <cstring>
#include <initializer_list>
#include <string>
#include <set>
#include "dice.h"
#include "dice_hybrid.h"
#include "dice_sounds.h"
#include "templates.h"
#include "sequencer.h"
#include "../../src/ui/dice_dialog_state.h"
using namespace mt;
static Project p, before;
static DiceSnap snap;
void setUp() {}
void tearDown() {}
void test_entire_guide_catalogue() {
  TEST_ASSERT_EQUAL(54, diceGenreCount());
  int total = 0;
  for (int g = 0; g < diceGenreCount(); ++g) total += diceStyleCount(g);
  TEST_ASSERT_EQUAL(317, total);
}
static DiceOpts style(const char* name) {
  DiceOpts o; o.form = DiceForm::Classic; o.groove = 0; o.variety=0;o.keepKey=true;
  for (int g=0; g<diceGenreCount(); ++g) for (int s=0; s<diceStyleCount(g); ++s)
    if (!strcmp(name,diceStyleName(g,s))) { o.genre=g; o.style=s; return o; }
  TEST_FAIL_MESSAGE(name); return o;
}
void test_acid_house_has_six_source_roles_and_canonical_rhythm() {
  p.reset(); auto o=style("Acid House");
  diceRoll(p,o,15,123,snap);
  TEST_ASSERT_EQUAL(122,p.bpm);
  const char* roles[]={"DRUMS","Bass","Stab"};
  for(int t=0;t<3;++t) TEST_ASSERT_EQUAL_STRING(roles[t],p.tracks[t].name);
  TEST_ASSERT_TRUE(p.trackIsDrum(0));
  for(int i=0;i<64;++i) TEST_ASSERT_EQUAL(i%4==0 || i==63,laneOn(p.patterns[0].steps[0][i],0));
  TEST_ASSERT_EQUAL(120,p.patterns[0].steps[0][0].note);
  TEST_ASSERT_TRUE(laneOn(p.patterns[0].steps[0][2],5));
  TEST_ASSERT_FALSE(laneOn(p.patterns[0].steps[0][18],5));
  TEST_ASSERT_EQUAL(3,p.patterns[0].steps[2][6].note-p.patterns[0].steps[2][2].note);
}
void test_all_5072_variants_have_valid_facts_and_synthesis_recipes() {
  int count=0;
  for(int g=0;g<diceGenreCount();++g) for(int s=0;s<diceStyleCount(g);++s)
    for(int v=0;v<16;++v) {
      const auto* variant=diceVariant(g,s,v); TEST_ASSERT_NOT_NULL(variant);
      TEST_ASSERT_INT_WITHIN(135,165,variant->bpm);
      for(int t=0;t<6;++t) {
        const auto& voice=diceVoice(*variant,t);const auto& r=diceTimbre(voice);
        Instrument m;TrackCfg cfg;Rng rng(1);dice_detail::buildVoice(voice,m,cfg,true,0,rng);
        TEST_ASSERT_EQUAL(r.type,m.type);TEST_ASSERT_GREATER_THAN(0,m.vol);
        TEST_ASSERT_EQUAL(r.machine,m.machine);TEST_ASSERT_EQUAL_STRING(r.name,m.name);
        TEST_ASSERT_LESS_THAN(InstrType::Count,m.type);TEST_ASSERT_LESS_THAN(FltMode::Count,m.fltMode);
        TEST_ASSERT_LESS_OR_EQUAL(127,m.cutoff);TEST_ASSERT_LESS_OR_EQUAL(127,m.reso);
        for(auto param:m.fmParam) TEST_ASSERT_LESS_OR_EQUAL(127,param);
        if(m.type==InstrType::Fm) TEST_ASSERT_LESS_THAN(FmMachine::Count,m.machine);
        if(m.type==InstrType::Drum) TEST_ASSERT_LESS_THAN(DrumMachine::Count,m.machine);
        if(m.type==InstrType::String) TEST_ASSERT_LESS_THAN(StringModel::Count,m.machine);
        int previous=-1;
        for(int i=0;i<diceEventCount(voice);++i) {
          const auto& e=diceEvent(voice,i);
          TEST_ASSERT_GREATER_THAN(previous,e.at); previous=e.at;
          TEST_ASSERT_LESS_THAN(64,e.at); TEST_ASSERT_GREATER_THAN(0,e.velocity);
          TEST_ASSERT_LESS_OR_EQUAL(127,e.velocity); TEST_ASSERT_LESS_OR_EQUAL(100,e.probability);
        }
      }
      ++count;
    }
  TEST_ASSERT_EQUAL(5072,count);
  TEST_ASSERT_NULL(diceVariant(54,0,0)); TEST_ASSERT_NULL(diceVariant(0,0,16));
}
void test_lock_target_timing_and_external_instruments_are_preserved() {
  templateBuild(0,p); p.tracks[6].out=TrackOut::Int; p.tracks[6].instr=0;
  p.tracks[7].out=TrackOut::Int; p.tracks[7].instr=20; p.instruments[20].type=InstrType::Kit;
  p.instruments[20].kit[0].instr=1;
  for(int i=0;i<4;++i) { p.patterns[i].length=16*(i+1); p.patterns[i].swing=53+i; }
  before=p; auto o=style("Acid House"); o.lockMask=4; o.targetMask=31;
  diceRoll(p,o,9,123,snap);
  TEST_ASSERT_EQUAL_MEMORY(&before.instruments[0],&p.instruments[0],sizeof(Instrument));
  TEST_ASSERT_EQUAL_MEMORY(&before.instruments[1],&p.instruments[1],sizeof(Instrument));
  TEST_ASSERT_EQUAL_MEMORY(&before.tracks[2],&p.tracks[2],sizeof(TrackCfg));
  TEST_ASSERT_EQUAL(before.bpm,p.bpm);
  for(int i=0;i<kPatterns;++i) {
    TEST_ASSERT_EQUAL(before.patterns[i].length,p.patterns[i].length);
    TEST_ASSERT_EQUAL(before.patterns[i].swing,p.patterns[i].swing);
    for(int t=0;t<kTracks;++t) if(t==2 || t>=5 || i>=4)
      TEST_ASSERT_EQUAL_MEMORY(before.patterns[i].steps[t],p.patterns[i].steps[t],sizeof(p.patterns[i].steps[t]));
  }
  TEST_ASSERT_TRUE(diceUndo(p,snap)); TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(Project));
}
void test_seed_replay_layers_and_undo() {
  auto o=style("Deep House"); o.variety=60; o.form=DiceForm::Arrange; o.layers|=kDiceSong;
  templateBuild(0,p); before=p;
  diceDraft(p,o,15,43,snap); TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(Project));
  diceApply(p,snap); static Project generated; generated=p;
  TEST_ASSERT_EQUAL(6,snap.patCount); TEST_ASSERT_GREATER_THAN(0,p.songLen);
  TEST_ASSERT_TRUE(diceUndo(p,snap)); TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(Project));
  diceRoll(p,o,15,43,snap); TEST_ASSERT_EQUAL_MEMORY(&generated,&p,sizeof(Project));
  diceUndo(p,snap); o.layers=kDiceTempo; diceRoll(p,o,0,40,snap);
  for(int i=0;i<kPatterns;++i) TEST_ASSERT_EQUAL_MEMORY(&before.patterns[i],&p.patterns[i],sizeof(Pattern));
  TEST_ASSERT_EQUAL_MEMORY(before.instruments,p.instruments,sizeof(p.instruments));
  diceUndo(p,snap); TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(Project));
}
void test_fm_decay_and_pad_duration_reach_the_sound_engine() {
  p.reset(); auto o=style("Acid House"); diceRoll(p,o,0,1,snap);
  const auto& voice=diceVoice(*diceVariant(o.genre,o.style,0),0);
  const auto& m=p.instruments[p.kitOf(0)->kit[0].instr];
  TEST_ASSERT_EQUAL(InstrType::Drum,m.type);TEST_ASSERT_EQUAL(DrumMachine::Bd9,m.machine);
  TEST_ASSERT_EQUAL(diceTimbre(voice).params[0],m.macro[kMacDec]);
  o=style("Deep House"); diceRoll(p,o,0,1,snap);
  const auto& variant=*diceVariant(o.genre,o.style,0);const auto layout=diceLayout(variant);
  for(int t=0;t<6;++t) {
    if(layout.source[t]<0) continue;
    const auto& v=diceVoice(variant,layout.source[t]);
    if(static_cast<DiceVoiceKind>(v.kind)!=DiceVoiceKind::Pad) continue;
    const auto* gate=p.patterns[0].steps[t][0].find(Fx::GAT);
    TEST_ASSERT_NOT_NULL(gate); TEST_ASSERT_GREATER_OR_EQUAL(400,gatePercent(gate->val));
  }
}
void test_fixed_seed_overrides_the_call_seed() {
  p.reset(); auto o=style("Acid House"); o.seed=888; o.variety=100;
  auto result=diceRoll(p,o,0,1,snap); TEST_ASSERT_EQUAL(888,result.seed);
  before=p; p.reset(); diceRoll(p,o,0,2,snap); TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(Project));
}
void test_small_changes_have_bounded_effect() {
  auto o=style("Acid House");
  p.reset();diceRoll(p,o,0,1,snap);
  for(auto& step:p.patterns[0].steps[0]) step=Step();
  p.patterns[0].steps[0][0].note=100;setLaneMask(p.patterns[0].steps[0][0],1);
  before=p;o.seed=0;o.layers=kDicePattern;o.targetMask=1;o.change=DiceChange::Subtle;
  diceRoll(p,o,0,123,snap); int changes=0,attacks=0;
  for(int i=0;i<64;++i) {
    changes+=memcmp(&before.patterns[0].steps[0][i],&p.patterns[0].steps[0][i],sizeof(Step))!=0;
    attacks+=p.patterns[0].steps[0][i].hasNote();
  }
  TEST_ASSERT_LESS_OR_EQUAL(1,changes); TEST_ASSERT_EQUAL(1,attacks);
}
void test_every_style_rolls_and_roundtrips_undo_at_all_lengths() {
  for(int g=0;g<diceGenreCount();++g) for(int s=0;s<diceStyleCount(g);++s) {
    p.reset(); before=p; DiceOpts o;o.genre=g;o.style=s;o.variant=s%16;o.root=s%12;
    o.len=16u<<(s%4);o.form=DiceForm::Classic;o.layers|=kDiceSong;
    diceRoll(p,o,15,123,snap);
    TEST_ASSERT_EQUAL(63,snap.patMask); TEST_ASSERT_EQUAL(63,snap.cfgMask);
    TEST_ASSERT_EQUAL(o.len,p.patterns[0].length);
    for(int t=0;t<6;++t) for(int i=0;i<kMaxSteps;++i) {
      const auto& step=p.patterns[0].steps[t][i];
      if(i>=o.len) TEST_ASSERT_TRUE(step.isEmpty());
      if(step.hasNote()) { if(p.trackIsDrum(t)) TEST_ASSERT_NOT_EQUAL(0,laneMask(step)); else {TEST_ASSERT_LESS_OR_EQUAL(127,step.vel); TEST_ASSERT_EQUAL(0,step.lanesHi);} }
    }
    TEST_ASSERT_TRUE(diceUndo(p,snap)); TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(Project));
  }
}
void test_develop_has_a_distinct_variation_and_withdraws_at_the_end() {
  p.reset(); auto o=style("Acid House");o.form=DiceForm::Develop;
  diceRoll(p,o,0,123,snap);
  bool different=false;
  for(int t=0;t<6;++t) different|=memcmp(p.patterns[1].steps[t],p.patterns[2].steps[t],sizeof(p.patterns[1].steps[t]))!=0;
  TEST_ASSERT_TRUE(different);
  for(int t=0;t<6;++t) for(int i=49;i<64;++i) TEST_ASSERT_FALSE(p.patterns[3].steps[t][i].hasNote());
}
void test_auto_variant_uses_seed_and_explicit_tempo_ignores_role_locks() {
  auto o=style("Acid House");o.variant=16; bool different=false;
  p.reset(); diceRoll(p,o,0,1,snap);before=p;
  for(int seed=2;seed<=8;++seed) {
    p.reset();diceRoll(p,o,0,seed,snap);different|=memcmp(&before,&p,sizeof(Project))!=0;
  }
  TEST_ASSERT_TRUE(different);
  p.reset();o.layers=kDiceTempo;o.lockMask=63;o.change=DiceChange::Subtle;
  diceRoll(p,o,0,1,snap);TEST_ASSERT_EQUAL(122,p.bpm);
}
void test_targeted_grid_conversion_accounts_for_track_speed() {
  p.reset();auto o=style("Acid House");o.layers=kDicePattern;o.targetMask=1u<<1;
  p.patterns[0].length=64;p.patterns[0].res=Resolution::Quarter;p.patterns[0].trackSpeed[1]=uint8_t(TrackSpeed::X2);
  diceRoll(p,o,0,123,snap);
  const auto& pt=p.patterns[0];
  TEST_ASSERT_EQUAL(Resolution::Quarter,pt.res);TEST_ASSERT_EQUAL(TrackSpeed::X2,pt.trackSpeed[1]);
  TEST_ASSERT_FALSE(pt.steps[1][1].hasNote());TEST_ASSERT_TRUE(pt.steps[1][2].hasNote());
}
void test_variety_changes_music_and_sounds_beyond_velocity() {
  auto o=style("Acid House");o.variety=0;p.reset();diceRoll(p,o,0,123,snap);before=p;
  o.variety=100;diceRoll(p,o,0,123,snap);
  int music=0,sounds=0;
  for(int t=0;t<6;++t) {
    for(int i=0;i<64;++i) music+=p.trackIsDrum(t) ? laneMask(before.patterns[0].steps[t][i])!=laneMask(p.patterns[0].steps[t][i]) : before.patterns[0].steps[t][i].note!=p.patterns[0].steps[t][i].note;
    sounds+=memcmp(&before.instruments[before.tracks[t].instr],&p.instruments[p.tracks[t].instr],sizeof(Instrument))!=0;
  }
  TEST_ASSERT_GREATER_THAN(0,music); TEST_ASSERT_GREATER_THAN(0,sounds);
}
void test_percussion_is_one_kit_with_simultaneous_lanes() {
  p.reset();auto o=style("Acid House");diceRoll(p,o,0,123,snap);
  TEST_ASSERT_TRUE(p.trackIsDrum(0));
  TEST_ASSERT_EQUAL_STRING("DRUMS",p.tracks[0].name);
  TEST_ASSERT_EQUAL_STRING("Bass",p.tracks[1].name);
  TEST_ASSERT_EQUAL_STRING("Stab",p.tracks[2].name);
  TEST_ASSERT_TRUE(laneOn(p.patterns[0].steps[0][4],0));
  TEST_ASSERT_TRUE(laneOn(p.patterns[0].steps[0][4],4));
  for(int t=3;t<6;++t) { TEST_ASSERT_FALSE(p.trackIsDrum(t));TEST_ASSERT_FALSE(p.tracks[t].mute); }
}
void test_repeated_rolls_reuse_safe_kit_slots_and_fixed_seed_is_audible() {
  p.reset();auto o=style("Acid House");o.variety=100;o.seed=123;
  diceRoll(p,o,0,123,snap);before=p;
  uint32_t slots=0;
  for(const auto& lane:p.kitOf(0)->kit) if(lane.instr<kInstruments) slots|=1u<<lane.instr;
  for(int seed=1;seed<=24;++seed) {
    o.seed=0;diceRoll(p,o,15,seed,snap);uint32_t used=0;
    for(const auto& lane:p.kitOf(0)->kit) if(lane.instr<kInstruments) used|=1u<<lane.instr;
    TEST_ASSERT_EQUAL(slots,used);
  }
  o.seed=123;diceRoll(p,o,0,123,snap);
  for(int t=0;t<6;++t) TEST_ASSERT_EQUAL_MEMORY(before.patterns[0].steps[t],p.patterns[0].steps[t],sizeof(p.patterns[0].steps[t]));
  const auto* old=before.kitOf(0);const auto* now=p.kitOf(0);
  for(int lane=0;lane<kKitLanes;++lane) if(old->kit[lane].instr<kInstruments)
    TEST_ASSERT_EQUAL_MEMORY(&before.instruments[old->kit[lane].instr],&p.instruments[now->kit[lane].instr],sizeof(Instrument));
}
void test_drum_lock_preserves_kit_lanes_and_auto_avoids_previous_variant() {
  p.reset();auto o=style("Acid House");o.variant=kDiceVariantAuto;
  const auto first=diceRoll(p,o,0,123,snap);o.avoidVariant=first.variant;
  const auto second=diceRoll(p,o,0,123,snap);TEST_ASSERT_NOT_EQUAL(first.variant,second.variant);
  before=p;o.lockMask=1;o.variety=100;diceRoll(p,o,0,456,snap);
  TEST_ASSERT_EQUAL_MEMORY(before.patterns[0].steps[0],p.patterns[0].steps[0],sizeof(p.patterns[0].steps[0]));
  TEST_ASSERT_EQUAL_MEMORY(before.kitOf(0),p.kitOf(0),sizeof(Instrument));
  for(const auto& lane:before.kitOf(0)->kit) if(lane.instr<kInstruments)
    TEST_ASSERT_EQUAL_MEMORY(&before.instruments[lane.instr],&p.instruments[lane.instr],sizeof(Instrument));
}
void test_full_roll_auditions_new_main_when_old_pattern_is_selected() {
  struct Sink:MidiSink {void send(const uint8_t*,uint8_t) override {}} sink;
  p.reset();Sequencer seq(p);seq.selectPattern(15);auto o=style("Acid House");o.form=DiceForm::Develop;
  diceRoll(p,o,15,123,snap);
  const int target=ui::dice_dialog::audition(o,snap.layers,seq.heardPattern(),snap.first);
  TEST_ASSERT_EQUAL(1,target);seq.selectPattern(target);
  TEST_ASSERT_EQUAL(1,seq.heardPattern());seq.start(0,sink);seq.process(0,sink);
  TEST_ASSERT_TRUE(seq.playing());
}
void test_kit_retains_recipe_space_sends() {
  p.reset();auto o=style("Deep House");diceRoll(p,o,0,123,snap);
  const auto& variant=*diceVariant(o.genre,o.style,0);const auto layout=diceLayout(variant);
  int delay=0,reverb=0,n=0;
  for(int i=0;i<6;++i) if(layout.drums&(1u<<i)) {delay+=diceVoice(variant,i).delay;reverb+=diceVoice(variant,i).reverb;++n;}
  TEST_ASSERT_GREATER_THAN(0,delay+reverb);
  TEST_ASSERT_EQUAL(delay/n,p.kitOf(0)->send);TEST_ASSERT_EQUAL(reverb/n,p.kitOf(0)->rsend);
}
void test_variety_keeps_sound_architecture_and_original_onsets() {
  p.reset();auto o=style("Acid House");diceRoll(p,o,0,123,snap);before=p;
  o.variety=100;diceRoll(p,o,0,456,snap);
  for(int t=1;t<3;++t) {
    const auto& a=before.instruments[before.tracks[t].instr];const auto& b=p.instruments[p.tracks[t].instr];
    TEST_ASSERT_EQUAL(a.type,b.type);TEST_ASSERT_EQUAL_MEMORY(a.synOsc,b.synOsc,sizeof(a.synOsc));
    TEST_ASSERT_EQUAL_STRING(a.name,b.name);
  }
  for(int t=0;t<3;++t) for(int i=0;i<64;++i) if(before.patterns[0].steps[t][i].hasNote()) {
    TEST_ASSERT_TRUE(p.patterns[0].steps[t][i].hasNote());
    if(!t) TEST_ASSERT_EQUAL(laneMask(before.patterns[0].steps[t][i]),laneMask(p.patterns[0].steps[t][i])&laneMask(before.patterns[0].steps[t][i]));
  }
}
void test_sample_descriptions_build_distinct_synthesis_architectures() {
  p.reset();auto o=style("Folktronica");diceRoll(p,o,0,123,snap);
  TEST_ASSERT_EQUAL(InstrType::String,p.instruments[p.tracks[2].instr].type);
  o=style("Deep House");diceRoll(p,o,0,123,snap);
  const auto& sub=p.instruments[p.tracks[1].instr];
  TEST_ASSERT_EQUAL(InstrType::Synth,sub.type);TEST_ASSERT_EQUAL(SynOsc::Sine,sub.synOsc[0]);
}
void test_variety_preserves_attacks_and_follows_the_played_chord() {
  auto o=style("Deep House");o.seed=123;
  p.reset();o.variety=0;diceRoll(p,o,0,123,snap);before=p;o.variety=100;diceRoll(p,o,0,123,snap);
  for(int at=0;at<64;++at) {
    const auto& old=before.patterns[0].steps[1][at];const auto& note=p.patterns[0].steps[1][at];
    if(!old.hasNote())continue;
    TEST_ASSERT_TRUE(note.hasNote());TEST_ASSERT_INT_WITHIN(24,old.note,note.note);
    int latest=-1,track=-1;
    for(int t=2;t<4;++t)for(int i=0;i<=at;++i)if(p.patterns[0].steps[t][i].hasNote() && p.patterns[0].steps[t][i].find(Fx::CHD) && i>=latest){latest=i;track=t;}
    if(latest<0)continue;
    const auto& c=p.patterns[0].steps[track][latest];uint8_t tones[4];int n=chordNotes(c.note,c.find(Fx::CHD)->val,p.scaleRoot,static_cast<ScaleType>(p.scaleType),tones);
    bool inside=false;for(int i=0;i<n;++i)inside|=tones[i]%12==note.note%12;
    // Source guide notes may be passing notes; the generated notes must follow the sounding chord.
    if(old.note!=note.note) TEST_ASSERT_TRUE(inside);
  }
}
void test_every_variant_variety_keeps_attacks_harmony_and_bar_openings() {
  for(int g=0;g<diceGenreCount();++g) for(int style=0;style<diceStyleCount(g);++style) for(int variant=0;variant<16;++variant) {
    DiceOpts o;o.keepKey=true;o.genre=g;o.style=style;o.variant=variant;o.form=DiceForm::Classic;o.groove=0;o.variety=0;
    p.reset();diceRoll(p,o,0,123,snap);before=p;o.variety=100;diceRoll(p,o,0,123,snap);
    const auto& v=*diceVariant(g,style,variant);const auto layout=diceLayout(v);
    for(int t=0;t<6;++t) {
      const int source=layout.source[t];if(diceSupplement(source))continue; // support rhythms develop separately
      const bool harmonic=source>=0 && diceChord(diceVoice(v,source))!=255;
      const auto speed=toSpeed(p.patterns[0].trackSpeed[t]);
      const int ticks=ticksPerStep(p.patterns[0].res)*speedDiv(speed)/speedMul(speed);
      for(int at=0;at<p.patterns[0].length;++at) {
        const auto& a=before.patterns[0].steps[t][at];const auto& b=p.patterns[0].steps[t][at];
        if(a.hasNote()) {
          TEST_ASSERT_TRUE_MESSAGE(b.hasNote(),diceStyleName(g,style));
          if(p.trackIsDrum(t)) TEST_ASSERT_EQUAL(laneMask(a),laneMask(a)&laneMask(b));
          else TEST_ASSERT_INT_WITHIN(24,a.note,b.note);
        }
        if(harmonic) {
          TEST_ASSERT_INT_WITHIN(24,a.note,b.note);
          if(const auto* chord=a.find(Fx::CHD)) {TEST_ASSERT_NOT_NULL(b.find(Fx::CHD));TEST_ASSERT_EQUAL(chordType(chord->val),chordType(b.find(Fx::CHD)->val));}
        } else if(p.trackIsDrum(t) && (at*ticks)%(4*kPpqn)<kPpqn) TEST_ASSERT_EQUAL_MEMORY(&a,&b,sizeof(Step));
      }
    }
  }
}
void test_variety_creates_significant_melodic_changes_in_representative_styles() {
  for(const char* name:{"Folktronica","Acid House","Deep House","Neurofunk","Techno","Dub Techno"}) {
    auto o=style(name);p.reset();diceRoll(p,o,0,123,snap);before=p;o.variety=100;diceRoll(p,o,0,123,snap);
    int changed=0,eligible=0;
    for(int t=0;t<6;++t) if(!p.trackIsDrum(t)) for(int at=0;at<64;++at) {
      const auto& a=before.patterns[0].steps[t][at];const auto& b=p.patterns[0].steps[t][at];
      if(a.hasNote() && (!a.find(Fx::CHD) || !strcmp(name,"Techno"))) {++eligible;changed+=a.note!=b.note;}
    }
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(eligible/5+1,changed,name);
  }
}
void test_variety_changes_the_motif_in_a_one_bar_pattern_and_replays_seed() {
  auto o=style("Acid House");o.seed=123;o.len=16;p.reset();diceRoll(p,o,0,123,snap);before=p;o.variety=100;diceRoll(p,o,0,123,snap);
  int changes=0;for(int at=0;at<16;++at) changes+=before.patterns[0].steps[1][at].note!=p.patterns[0].steps[1][at].note;
  TEST_ASSERT_GREATER_THAN(0,changes);before=p;
  p.reset();diceRoll(p,o,0,123,snap);TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(Project));
  o.seed=456;diceRoll(p,o,0,456,snap);changes=0;for(int at=0;at<16;++at) changes+=before.patterns[0].steps[1][at].note!=p.patterns[0].steps[1][at].note;
  TEST_ASSERT_GREATER_THAN(0,changes);
}
void test_variety_amount_increases_note_changes_with_the_same_seed() {
  for(const char* name:{"Acid House","Folktronica"}) for(int seed=1;seed<=16;++seed) {
    auto o=style(name);o.seed=seed;p.reset();diceRoll(p,o,0,seed,snap);before=p;int previous=0;
    for(int amount:{25,50,100}) {
      o.variety=amount;diceRoll(p,o,0,seed,snap);int changes=0;
      for(int t=0;t<6;++t) if(!p.trackIsDrum(t)) for(int at=0;at<64;++at)
        changes+=before.patterns[0].steps[t][at].note!=p.patterns[0].steps[t][at].note;
      TEST_ASSERT_GREATER_OR_EQUAL(previous,changes);previous=changes;
    }
    TEST_ASSERT_GREATER_THAN(0,previous);
  }
}
void test_new_full_rolls_choose_fresh_key_and_mode_for_reported_acid_styles() {
  for(const char* name:{"Acid","Acid Rave","Acid Breaks","Acid Trance"}) {
    auto o=style(name);o.keepKey=false;o.variety=35;p.reset();
    for(int seed=1;seed<=16;++seed) {
      const int root=p.scaleRoot,scale=p.scaleType;
      diceRoll(p,o,0,seed,snap);
      TEST_ASSERT_NOT_EQUAL_MESSAGE(root,p.scaleRoot,name);
      TEST_ASSERT_NOT_EQUAL_MESSAGE(scale,p.scaleType,name);
    }
  }
}
void test_new_rolls_generate_distinct_acid_motifs_in_the_same_key() {
  for(const char* name:{"Acid","Acid Rave","Acid Breaks","Acid Trance"}) {
    auto o=style(name);o.variety=35;o.layers=kDicePattern; // hold key,compare actual note contours
    p.reset();o.layers=kDiceAll;diceRoll(p,o,0,1,snap);o.layers=kDicePattern;
    const auto layout=diceLayout(*diceVariant(o.genre,o.style,0));
    for(int seed=2;seed<=16;++seed) {
      before=p;diceRoll(p,o,0,seed,snap);
      for(int t=0;t<6;++t) if(!p.trackIsDrum(t) && p.patterns[0].steps[t][0].note!=kNoteOff) {
        if(diceSupplement(layout.source[t]))continue; // this test compares the guide's fixed onsets
        bool melodic=false;for(const auto& step:p.patterns[0].steps[t]) melodic|=step.hasNote() && !step.find(Fx::CHD);
        if(!melodic)continue;
        int changes=0;for(int at=0;at<64;++at)if(before.patterns[0].steps[t][at].hasNote() && p.patterns[0].steps[t][at].hasNote())changes+=before.patterns[0].steps[t][at].note!=p.patterns[0].steps[t][at].note;
        TEST_ASSERT_GREATER_THAN_MESSAGE(0,changes,name);
      }
    }
  }
}
void test_key_keep_fixed_replay_and_non_global_scopes_preserve_tonality() {
  auto o=style("Acid Trance");o.variety=35;p.reset();p.scaleRoot=5;p.scaleType=uint8_t(ScaleType::Dorian);
  for(int seed=1;seed<=8;++seed) {diceRoll(p,o,0,seed,snap);TEST_ASSERT_EQUAL(5,p.scaleRoot);TEST_ASSERT_EQUAL(ScaleType::Dorian,p.scaleType);}
  o.keepKey=false;o.seed=123;p.reset();diceRoll(p,o,0,99,snap);before=p;
  p.reset();diceRoll(p,o,0,999,snap);TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(Project));
  o.seed=0;
  for(int layer:{kDiceSounds,kDicePattern}) {
    o.layers=layer;const int root=p.scaleRoot,scale=p.scaleType;diceRoll(p,o,0,456,snap);
    TEST_ASSERT_EQUAL(root,p.scaleRoot);TEST_ASSERT_EQUAL(scale,p.scaleType);
  }
  o.layers=kDiceAll;o.lockMask=1;const int root=p.scaleRoot,scale=p.scaleType;diceRoll(p,o,0,456,snap);
  TEST_ASSERT_EQUAL(root,p.scaleRoot);TEST_ASSERT_EQUAL(scale,p.scaleType);
}
void test_roll_while_playing_sends_the_new_acid_notes_to_the_synth() {
  struct Sink:MidiSink {
    std::string notes[kTracks];void send(const uint8_t*,uint8_t) override {}
    void synth(uint64_t,uint8_t track,const uint8_t* b,uint8_t n) override {
      if(n==3 && (b[0]&0xF0)==0x90 && b[2] && track<kTracks) notes[track].push_back(char(b[1]));
    }
  } sink;
  auto o=style("Acid Trance");o.variety=35;p.reset();diceRoll(p,o,0,1,snap);Sequencer seq(p);seq.selectPattern(0);seq.start(0,sink);
  const uint64_t span=uint64_t(60000000u/p.bpm)*16;
  for(uint64_t now=0;now<span;now+=1000)seq.process(now,sink);
  std::string old[kTracks];for(int t=0;t<kTracks;++t){old[t]=sink.notes[t];sink.notes[t].clear();}
  diceRoll(p,o,0,2,snap);seq.setBpm(p.bpm);seq.releaseTies(span,sink);
  for(int t=0;t<6;++t){seq.trackOutChanged(span,t,sink);seq.sendProgram(span,t,sink);}
  seq.genEdited(span,sink);seq.songEdited(span,sink,0,SongOp::Edit);seq.selectPattern(0);
  for(uint64_t now=span;now<2*span;now+=1000)seq.process(now,sink);
  for(int t=1;t<4;++t) {TEST_ASSERT_FALSE(old[t].empty());TEST_ASSERT_FALSE(sink.notes[t].empty());TEST_ASSERT_TRUE(old[t]!=sink.notes[t]);}
  TEST_ASSERT_TRUE(seq.playing());
}
void test_all_styles_generate_new_melodic_roles_on_repeated_rolls_in_a_fixed_key() {
  for(int g=0;g<diceGenreCount();++g)for(int st=0;st<diceStyleCount(g);++st) {
    DiceOpts o;o.genre=g;o.style=st;o.variant=0;o.form=DiceForm::Classic;o.variety=35;o.groove=0;o.keepKey=true;
    p.reset();diceRoll(p,o,0,1,snap);
    const auto& v=*diceVariant(g,st,0);const auto layout=diceLayout(v);
    for(int seed=2;seed<=8;++seed) {
      before=p;diceRoll(p,o,0,seed,snap);
      for(int t=0;t<6;++t) {
        const int src=layout.source[t];if(src<0)continue;const auto& voice=diceVoice(v,src);
        const auto kind=static_cast<DiceVoiceKind>(voice.kind);
        if(diceChord(voice)!=255 || (kind!=DiceVoiceKind::Bass && kind!=DiceVoiceKind::Lead && kind!=DiceVoiceKind::Bell && kind!=DiceVoiceKind::Stab))continue;
        const int played=p.patterns[0].trackLen[t] ? p.patterns[0].trackLen[t] : p.patterns[0].length;
        bool attacks=false,different=false;for(int at=0;at<played;++at){attacks|=p.patterns[0].steps[t][at].hasNote();different|=p.patterns[0].steps[t][at].note!=before.patterns[0].steps[t][at].note;}
        if(attacks) TEST_ASSERT_TRUE_MESSAGE(different,diceStyleName(g,st));
      }
    }
  }
}
void test_dice_drum_children_live_in_the_last_eight_instrument_slots() {
  p.reset();auto o=style("Acid House");diceRoll(p,o,0,123,snap);
  const auto* kit=p.kitOf(0);TEST_ASSERT_NOT_NULL(kit);
  for(const auto& lane:kit->kit) if(lane.instr<kInstruments) TEST_ASSERT_GREATER_OR_EQUAL(24,lane.instr);
}
void test_full_drum_bank_is_protected_without_falling_back_to_low_slots() {
  p.reset();p.instruments[16].type=InstrType::Kit;kitSetDefaults(p.instruments[16]);p.tracks[6].instr=16;
  for(int i=0;i<8;++i)p.instruments[16].kit[i].instr=24+i;
  before=p;auto o=style("Acid House");auto result=diceRoll(p,o,0,123,snap);
  TEST_ASSERT_TRUE(result.missingMask&1);
  TEST_ASSERT_EQUAL_MEMORY(before.instruments+24,p.instruments+24,sizeof(Instrument)*8);
  TEST_ASSERT_EQUAL_MEMORY(&before.tracks[0],&p.tracks[0],sizeof(TrackCfg));
  TEST_ASSERT_EQUAL_MEMORY(before.patterns[0].steps[0],p.patterns[0].steps[0],sizeof(p.patterns[0].steps[0]));
  TEST_ASSERT_TRUE(diceUndo(p,snap));TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(Project));
}
void test_hybrid_completes_acid_house_to_six_playable_parts() {
  p.reset();auto o=style("Acid House");o.variety=35;diceRoll(p,o,0,123,snap);
  for(int t=0;t<6;++t) {
    bool notes=false;for(const auto& step:p.patterns[0].steps[t])notes|=step.hasNote();
    TEST_ASSERT_TRUE_MESSAGE(notes,p.tracks[t].name);TEST_ASSERT_FALSE(p.tracks[t].mute);
    TEST_ASSERT_LESS_THAN(16,p.tracks[t].instr);
  }
  TEST_ASSERT_EQUAL_STRING("Bass",p.tracks[1].name);TEST_ASSERT_EQUAL_STRING("Stab",p.tracks[2].name);
}
void test_hybrid_keeps_all_native_acid_trance_voices_and_adds_support() {
  p.reset();auto o=style("Acid Trance");o.variety=35;diceRoll(p,o,0,123,snap);
  TEST_ASSERT_EQUAL_STRING("Bass",p.tracks[1].name);TEST_ASSERT_EQUAL_STRING("Acid",p.tracks[2].name);TEST_ASSERT_EQUAL_STRING("Lead",p.tracks[3].name);
  for(int t=4;t<6;++t){bool notes=false;for(const auto& step:p.patterns[0].steps[t])notes|=step.hasNote();TEST_ASSERT_TRUE(notes);}
}
void test_all_styles_fill_six_tracks_without_losing_native_parts() {
  static_assert(sizeof(DiceSnap)<=sizeof(Project),"DICE must fit the reserved PSRAM draft");
  for(int g=0;g<diceGenreCount();++g)for(int st=0;st<diceStyleCount(g);++st) {
    DiceOpts o;o.genre=g;o.style=st;o.form=DiceForm::Classic;o.variety=35;
    p.reset();const auto result=diceRoll(p,o,0,71,snap);
    TEST_ASSERT_EQUAL_MESSAGE(0,result.missingMask,diceStyleName(g,st));
    const auto& v=*diceVariant(g,st,0);const auto layout=diceLayout(v);uint8_t native=0;
    TEST_ASSERT_EQUAL(63,layout.used);
    for(int t=0;t<6;++t) {
      const auto source=layout.source[t];if(source>=0)native|=1u<<source;
      int hits=0;for(const auto& s:p.patterns[0].steps[t])hits+=s.hasNote();
      char context[96];snprintf(context,sizeof(context),"%s / %s",diceStyleName(g,st),p.tracks[t].name);
      TEST_ASSERT_GREATER_THAN_MESSAGE(0,hits,context);TEST_ASSERT_FALSE(p.tracks[t].mute);
      TEST_ASSERT_LESS_THAN_MESSAGE(16,p.tracks[t].instr,context);
      if(diceSupplement(source)) {
        TEST_ASSERT_TRUE(dice_detail::companionPlayable(p.tracks[t],&p.instruments[p.tracks[t].instr],dicePartKind(source)));
        TEST_ASSERT_LESS_OR_EQUAL(74,p.tracks[t].vol);
      }
    }
    for(int i=0;i<6;++i)if(!dicePercussion(diceVoice(v,i)) && diceEventCount(diceVoice(v,i))) TEST_ASSERT_TRUE(native&(1u<<i));
    if(const auto* kit=p.kitOf(0))for(const auto& lane:kit->kit)if(lane.instr<kInstruments)TEST_ASSERT_GREATER_OR_EQUAL(24,lane.instr);
  }
}
void test_companions_follow_harmony_and_generate_different_phrases() {
  for(const char* name:{"Acid House","Acid","Acid Trance","Deep House","Dark Ambient"}) {
    auto o=style(name);o.variety=70;o.seed=0;const auto& v=*diceVariant(o.genre,o.style,0);const auto layout=diceLayout(v);
    std::set<std::string> phrases[6];p.reset();
    for(int seed=1;seed<=8;++seed) {
      diceRoll(p,o,0,seed,snap);
      const auto plan=dice_detail::phrasePlan(v,seed,o.variety);
      for(int t=0;t<6;++t)if(diceSupplement(layout.source[t])) {
        const auto part=dicePartKind(layout.source[t]);std::string phrase;
        for(int at=0;at<p.patterns[0].length;++at) {
          const auto& s=p.patterns[0].steps[t][at];phrase.push_back(s.note);
          if(!s.hasNote())continue;
          dice_detail::DiceChord c;
          if(part!=DicePart::Bass && dice_detail::harmonyAt(v,*diceRecipe(o.genre,o.style),at*ticksPerStep(p.patterns[0].res),p.scaleRoot,p.scaleType,plan,c)) {
            uint8_t notes[4];const int n=chordNotes(c.root,c.chord,p.scaleRoot,static_cast<ScaleType>(p.scaleType),notes);bool inside=false;
            for(int i=0;i<n;++i)inside|=notes[i]%12==s.note%12;
            char context[96];snprintf(context,sizeof(context),"%s %s seed %d step %d note %d chord %d:%d",name,p.tracks[t].name,seed,at,s.note,c.root,c.chord);
            TEST_ASSERT_TRUE_MESSAGE(inside,context);
          }
        }
        phrases[t].insert(phrase);
      }
    }
    for(int t=0;t<6;++t)if(diceSupplement(layout.source[t]))TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(3,phrases[t].size(),name);
  }
}
void test_old_low_slot_kit_children_move_to_the_high_bank() {
  p.reset();p.instruments[0].type=InstrType::Kit;kitSetDefaults(p.instruments[0]);
  p.instruments[0].kit[0].instr=4;p.instruments[0].kit[4].instr=5;
  p.instruments[4].type=InstrType::Drum;p.instruments[5].type=InstrType::Drum;
  before=p;auto o=style("Acid House");diceRoll(p,o,0,71,snap);
  for(const auto& lane:p.kitOf(0)->kit)if(lane.instr<kInstruments)TEST_ASSERT_GREATER_OR_EQUAL(24,lane.instr);
  for(int t=1;t<6;++t)TEST_ASSERT_FALSE(p.trackIsDrum(t));
  TEST_ASSERT_TRUE(diceUndo(p,snap));TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(Project));
}
void test_patterns_only_preserves_incompatible_old_drum_sounds_and_rows() {
  p.reset();auto o=style("Acid House");diceRoll(p,o,0,71,snap);
  p.instruments[p.tracks[4].instr].type=InstrType::Drum;before=p;o.layers=kDicePattern;o.variety=80;
  const auto result=diceRoll(p,o,0,93,snap);TEST_ASSERT_TRUE(result.missingMask&(1u<<4));
  TEST_ASSERT_EQUAL_MEMORY(before.instruments,p.instruments,sizeof(p.instruments));
  for(int i=0;i<kDicePats;++i)TEST_ASSERT_EQUAL_MEMORY(before.patterns[i].steps[4],p.patterns[i].steps[4],sizeof(p.patterns[i].steps[4]));
  TEST_ASSERT_TRUE(diceUndo(p,snap));TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(Project));
}
void test_companion_timbre_palettes_resolve_to_catalogue_recipes() {
  for(const char* name:{"ANALOGBS","SUB","808 SUB","REESE","E PIANO","ORGAN","STAB","WARM PAD","PLUCK","PULSE","ACOU GTR","FM BELL","FM KEYS","MALLET","303","TEXTURE","VOX FORM","DRONE"})
    TEST_ASSERT_EQUAL_STRING(name,diceTimbre(diceVoiceForTimbre(name,123)).name);
}
int main() { UNITY_BEGIN();
  RUN_TEST(test_entire_guide_catalogue);
  RUN_TEST(test_acid_house_has_six_source_roles_and_canonical_rhythm);
  RUN_TEST(test_all_5072_variants_have_valid_facts_and_synthesis_recipes);
  RUN_TEST(test_lock_target_timing_and_external_instruments_are_preserved);
  RUN_TEST(test_seed_replay_layers_and_undo);
  RUN_TEST(test_fm_decay_and_pad_duration_reach_the_sound_engine);
  RUN_TEST(test_fixed_seed_overrides_the_call_seed);
  RUN_TEST(test_small_changes_have_bounded_effect);
  RUN_TEST(test_every_style_rolls_and_roundtrips_undo_at_all_lengths);
  RUN_TEST(test_develop_has_a_distinct_variation_and_withdraws_at_the_end);
  RUN_TEST(test_auto_variant_uses_seed_and_explicit_tempo_ignores_role_locks);
  RUN_TEST(test_targeted_grid_conversion_accounts_for_track_speed);
  RUN_TEST(test_variety_changes_music_and_sounds_beyond_velocity);
  RUN_TEST(test_percussion_is_one_kit_with_simultaneous_lanes);
  RUN_TEST(test_repeated_rolls_reuse_safe_kit_slots_and_fixed_seed_is_audible);
  RUN_TEST(test_drum_lock_preserves_kit_lanes_and_auto_avoids_previous_variant);
  RUN_TEST(test_full_roll_auditions_new_main_when_old_pattern_is_selected);
  RUN_TEST(test_kit_retains_recipe_space_sends);
  RUN_TEST(test_variety_keeps_sound_architecture_and_original_onsets);
  RUN_TEST(test_sample_descriptions_build_distinct_synthesis_architectures);
  RUN_TEST(test_variety_preserves_attacks_and_follows_the_played_chord);
  RUN_TEST(test_every_variant_variety_keeps_attacks_harmony_and_bar_openings);
  RUN_TEST(test_variety_creates_significant_melodic_changes_in_representative_styles);
  RUN_TEST(test_variety_changes_the_motif_in_a_one_bar_pattern_and_replays_seed);
  RUN_TEST(test_variety_amount_increases_note_changes_with_the_same_seed);
  RUN_TEST(test_new_full_rolls_choose_fresh_key_and_mode_for_reported_acid_styles);
  RUN_TEST(test_new_rolls_generate_distinct_acid_motifs_in_the_same_key);
  RUN_TEST(test_key_keep_fixed_replay_and_non_global_scopes_preserve_tonality);
  RUN_TEST(test_roll_while_playing_sends_the_new_acid_notes_to_the_synth);
  RUN_TEST(test_all_styles_generate_new_melodic_roles_on_repeated_rolls_in_a_fixed_key);
  RUN_TEST(test_dice_drum_children_live_in_the_last_eight_instrument_slots);
  RUN_TEST(test_full_drum_bank_is_protected_without_falling_back_to_low_slots);
  RUN_TEST(test_hybrid_completes_acid_house_to_six_playable_parts);
  RUN_TEST(test_hybrid_keeps_all_native_acid_trance_voices_and_adds_support);
  RUN_TEST(test_all_styles_fill_six_tracks_without_losing_native_parts);
  RUN_TEST(test_companions_follow_harmony_and_generate_different_phrases);
  RUN_TEST(test_old_low_slot_kit_children_move_to_the_high_bank);
  RUN_TEST(test_patterns_only_preserves_incompatible_old_drum_sounds_and_rows);
  RUN_TEST(test_companion_timbre_palettes_resolve_to_catalogue_recipes);
  return UNITY_END(); }
