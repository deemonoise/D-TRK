#include <unity.h>
#include <cstring>
#include <initializer_list>
#include <string>
#include <set>
#include <map>
#include "dice.h"
#include "dice_arrangement.h"
#include "dice_band.h"
#include "gen_util.h"
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
void test_curated_menu_keeps_saved_choices_and_recovers_retired_styles() {
  using namespace ui::dice_dialog;
  static std::map<std::string, uint8_t> nvs;
  auto read = [](const char* key, uint8_t fallback) {
    const auto found = nvs.find(key);
    return found == nvs.end() ? fallback : found->second;
  };
  auto write = [](const char* key, uint8_t value) { nvs[key] = value; };
  const auto retired = style("Techno");
  const auto acid = style("Acid Techno");
  nvs.clear();
  Prefs choice;
  loadSelection(choice, read);
  TEST_ASSERT_EQUAL_STRING("Acid", diceStyleName(choice.genre, choice.style));
  choice.genre = acid.genre;
  choice.style = acid.style;
  choice.variant = 15;
  saveSelection(choice, Prefs(), write);
  Prefs reloaded;
  loadSelection(reloaded, read);
  TEST_ASSERT_EQUAL_STRING("Acid Techno", diceStyleName(reloaded.genre, reloaded.style));
  TEST_ASSERT_EQUAL(15, reloaded.variant);
  nvs = {{"dg2gen", retired.genre}, {"dg2sty", retired.style}, {"dg2variant", 255}};
  p.reset();
  p.patterns[11].steps[6][8].note = 65;
  before = p;
  loadSelection(reloaded, read);
  TEST_ASSERT_EQUAL_STRING("Warehouse Techno", diceStyleName(reloaded.genre, reloaded.style));
  TEST_ASSERT_EQUAL(kDiceVariantAuto, reloaded.variant);
  TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof(Project));
  Prefs next = reloaded;
  const auto hypnotic = style("Hypnotic Techno");
  next.genre = hypnotic.genre;
  next.style = hypnotic.style;
  saveSelection(next, reloaded, write);
  loadSelection(reloaded, read);
  TEST_ASSERT_EQUAL_STRING("Hypnotic Techno", diceStyleName(reloaded.genre, reloaded.style));
  const auto powerNoise = style("Power Noise");
  nvs = {{"dg2gen", powerNoise.genre}, {"dg2sty", powerNoise.style}, {"dg2variant", 12}};
  loadSelection(reloaded, read);
  TEST_ASSERT_EQUAL_STRING("Electro-Industrial", diceStyleName(reloaded.genre, reloaded.style));
  TEST_ASSERT_EQUAL(12, reloaded.variant);
}

void test_dice_dialog_encoder_wraps_all_genres_and_styles() {
  using namespace ui::dice_dialog;
  Prefs choice;
  choice.variant = 15;
  clampSelection(choice);
  const char* genres[] = {"Acid", "Techno", "Drum & Bass", "Trance", "Hardcore & Gabber",
                          "IDM", "Ambient", "Industrial", "UK Garage", "Rave"};
  bool fullOn = false;
  for (const char* genre : genres) {
    TEST_ASSERT_EQUAL_STRING(genre, genreLabel(choice));
    const auto position = menuPosition(choice);
    TEST_ASSERT_EQUAL(0, position.style);
    const int count = diceFamilyStyleCount(position.family);
    for (int i = 0; i < count; ++i) {
      TEST_ASSERT_EQUAL(i, menuPosition(choice).style);
      const char* label = styleLabel(choice);
      TEST_ASSERT_NOT_NULL(label);
      TEST_ASSERT_NOT_EQUAL(0, strcmp(label, "Power Noise"));
      if (!strcmp(label, "Full-On")) {
        TEST_ASSERT_EQUAL_STRING("Trance", genre);
        fullOn = true;
      }
      TEST_ASSERT_NOT_NULL(diceVariant(choice.genre, choice.style, choice.variant));
      stepStyle(choice, 1);
    }
    TEST_ASSERT_EQUAL(0, menuPosition(choice).style);
    stepStyle(choice, -1);
    TEST_ASSERT_EQUAL(count - 1, menuPosition(choice).style);
    stepGenre(choice, 1);
  }
  TEST_ASSERT_TRUE(fullOn);
  TEST_ASSERT_EQUAL_STRING("Acid", genreLabel(choice));
  stepGenre(choice, -1);
  TEST_ASSERT_EQUAL_STRING("Rave", genreLabel(choice));
  TEST_ASSERT_EQUAL(15, choice.variant);
}
void test_curated_all_roll_puts_acid_filter_movement_on_the_foreground_riff() {
  p.reset();
  auto o = style("Acid");
  o.form = DiceForm::Develop;
  o.keepKey = false;
  o.variety = kDiceVarietyDefault;
  o.variant = kDiceVariantAuto;
  const auto result = diceRoll(p, o, 0, 17321, snap);
  const auto layout = diceLayout(*diceVariant(o.genre, o.style, result.variant));
  for (int t = 0; t < kDiceRoles; ++t) {
    const int source = layout.source[t];
    if (source < 0 || strcmp(p.instruments[p.tracks[t].instr].name, "303")) continue;
    std::set<int> filters;
    int higherInVariation = 0;
    for (int at = 0; at < p.patterns[1].length; ++at) {
      const auto* main = p.patterns[1].steps[t][at].find(Fx::FLT);
      const auto* var = p.patterns[2].steps[t][at].find(Fx::FLT);
      if (main) filters.insert(main->val);
      if (main && var && var->val > main->val) ++higherInVariation;
    }
    TEST_ASSERT_GREATER_OR_EQUAL(3, filters.size());
    TEST_ASSERT_GREATER_THAN(0, higherInVariation);
    return;
  }
  TEST_FAIL_MESSAGE("Missing 303");
}

void test_curated_techno_ducks_the_bass_from_the_kit_only_on_the_full_sound_roll() {
  p.reset();
  auto o = style("Warehouse Techno");
  o.form = DiceForm::Develop;
  o.variety = kDiceVarietyDefault;
  diceRoll(p, o, 0, 17321, snap);
  TEST_ASSERT_EQUAL(0, p.tracks[0].folSrc);
  TEST_ASSERT_EQUAL(1, p.tracks[1].folSrc);
  TEST_ASSERT_EQUAL(LfoDest::Vol, p.tracks[1].folDest);
  TEST_ASSERT_LESS_THAN(0, p.tracks[1].folAmt);
  o.targetMask = 2;
  o.clean = false;
  p.tracks[1].folAmt = 7;
  diceRoll(p, o, 0, 60839, snap);
  TEST_ASSERT_EQUAL(7, p.tracks[1].folAmt);
}

void test_curated_trance_leaves_its_native_arp_behind_the_lead() {
  p.reset();
  auto o = style("Classic Trance");
  o.form = DiceForm::Develop;
  o.variety = kDiceVarietyDefault;
  o.variant = kDiceVariantAuto;
  diceRoll(p, o, 0, 60839, snap);
  TEST_ASSERT_EQUAL_STRING("Lead", p.tracks[2].name);
  TEST_ASSERT_EQUAL_STRING("Arp", p.tracks[3].name);
  TEST_ASSERT_GREATER_THAN(p.tracks[3].vol, p.tracks[2].vol);
  int matches = 0;
  for (int at = 0; at < p.patterns[1].length; ++at) {
    const auto& main = p.patterns[1].steps[2][at];
    const auto& var = p.patterns[2].steps[2][at];
    if (main.hasNote() && var.hasNote() && main.note == var.note) ++matches;
  }
  TEST_ASSERT_GREATER_THAN(1, matches);
}

void test_curated_ambient_lets_textures_ring_without_an_added_drum_fill() {
  p.reset();
  auto o = style("Space Ambient");
  o.form = DiceForm::Develop;
  o.variety = kDiceVarietyDefault;
  o.variant = kDiceVariantAuto;
  diceRoll(p, o, 0, 17321, snap);
  for (int section : {1, 2}) {
    const auto& pat = p.patterns[section];
    int hits = 0;
    for (int at = 0; at < pat.length; ++at) hits += pat.steps[0][at].hasNote();
    TEST_ASSERT_GREATER_THAN(0, hits);
    TEST_ASSERT_LESS_OR_EQUAL(pat.length / 8, hits);
  }
}

void test_jump_up_and_dark_psy_have_dedicated_voice_design() {
  for (const auto name : {"Jump Up", "Dark Psy"}) {
    const bool jump = !strcmp(name, "Jump Up");
    auto o = style(name);
    o.keepKey = false;
    o.form = DiceForm::Develop;
    o.variant = kDiceVariantAuto;
    o.variety = kDiceVarietyDefault;
    const auto choice = DiceChoice{o.genre, o.style};
    const auto menu = diceMenuPosition(choice);
    const auto listed = diceMenuChoice(menu);
    TEST_ASSERT_EQUAL(choice.genre, listed.genre);
    TEST_ASSERT_EQUAL(choice.style, listed.style);
    for (uint32_t seed : {17321, 60839}) {
      p.reset();
      o.clean = false;
      diceRoll(p, o, 0, seed, snap);
      before = p;
      p.reset();
      o.clean = true;
      const auto result = diceRoll(p, o, 0, seed, snap);
      TEST_ASSERT_EQUAL(0, result.missingMask);
      if (jump)
        for (int section : {1, 2}) {
          TEST_ASSERT_EQUAL_MEMORY(before.patterns[section].steps[0], p.patterns[section].steps[0],
                                   sizeof(p.patterns[section].steps[0]));
          for (int t = 1; t < kDiceRoles; ++t)
            for (int at = 0; at < p.patterns[section].length; ++at) {
              const auto& note = p.patterns[section].steps[t][at];
              if (note.hasNote()) TEST_ASSERT_EQUAL(before.patterns[section].steps[t][at].note, note.note);
            }
        }
      const auto& variant = *diceVariant(o.genre, o.style, result.variant);
      const auto layout = diceLayout(variant);
      bool foundBass = false, foundAnswer = false;
      for (int t = 0; t < kDiceRoles; ++t) {
        const int source = layout.source[t];
        const auto& patch = p.instruments[p.tracks[t].instr];
        if (source >= 0 && diceVoice(variant, source).kind == uint8_t(DiceVoiceKind::Bass)) {
          foundBass = true;
          TEST_ASSERT_EQUAL(SynPhaseMode::Retrig, patch.synPhaseMode);
          TEST_ASSERT_EQUAL(0, patch.synSub);
          TEST_ASSERT_EQUAL(SynOsc::Sine, patch.synOsc[0]);
          if (!jump) {
            TEST_ASSERT_EQUAL(SynOsc::Tri, patch.synOsc[1]);
            TEST_ASSERT_EQUAL(12, patch.synSemi);
            TEST_ASSERT_EQUAL(SynSatType::Off, patch.synSatType);
          }
        }
        if (jump && source >= 0 && !strcmp(diceVoice(variant, source).name, "Pluck")) {
          foundAnswer = true;
          TEST_ASSERT_EQUAL(FltMode::Formant, patch.fltMode);
          TEST_ASSERT_EQUAL(LfoDest::Cutoff, patch.lfoDest);
        }
        if (!jump && source == dicePartSource(DicePart::Fx)) {
          foundAnswer = true;
          TEST_ASSERT_EQUAL(LfoDest::Pan, patch.lfo[0].dest);
          TEST_ASSERT_GREATER_THAN(0, patch.lfo[0].depth);
        }
      }
      TEST_ASSERT_TRUE(foundBass);
      TEST_ASSERT_TRUE(foundAnswer);
      if (!jump) {
        const auto& kick = p.instruments[p.kitOf(0)->kit[0].instr];
        TEST_ASSERT_EQUAL(InstrType::Drum, kick.type);
        TEST_ASSERT_EQUAL(DrumMachine::Bds, kick.machine);
      }
    }
  }
}

void test_dark_psy_holds_the_roll_and_breaks_at_the_section_turn() {
  DiceOpts o;
  const auto dark = style("Dark Psy");
  o.genre = dark.genre;
  o.style = dark.style;
  o.variant = kDiceVariantAuto;
  o.layers = kDiceAll | kDiceSong;
  o.clean = false;
  p.reset();
  diceRoll(p, o, 0, 17321, snap);
  before = p;
  o.clean = true;
  p.reset();
  diceRoll(p, o, 0, 17321, snap);
  const auto& main = p.patterns[1];
  for (int at = 0; at < main.length; at += 4) {
    TEST_ASSERT_TRUE(main.steps[0][at].hasNote() && laneOn(main.steps[0][at], 0));
    TEST_ASSERT_FALSE(main.steps[1][at].hasNote());
    for (int slot = 1; slot < 4; ++slot) {
      const auto& old = before.patterns[1].steps[1][at + slot];
      const auto& note = main.steps[1][at + slot];
      TEST_ASSERT_TRUE(note.hasNote());
      if (old.hasNote()) TEST_ASSERT_EQUAL(old.note, note.note);
    }
  }
  const auto& turn = p.patterns[2];
  const int pickup = turn.length - 4;
  TEST_ASSERT_FALSE(turn.steps[0][pickup].hasNote() && laneOn(turn.steps[0][pickup], 0));
  for (int at = pickup; at < turn.length; ++at)
    TEST_ASSERT_FALSE(turn.steps[1][at].hasNote());
  bool heardPickup = false;
  for (int at = pickup; at < turn.length; ++at) heardPickup |= turn.steps[5][at].hasNote();
  TEST_ASSERT_TRUE(heardPickup);
}

void test_garage_styles_distinguish_four_on_the_floor_from_two_step() {
  for (const char* name : {"UK Garage", "Speed Garage", "2-Step Garage"}) {
    auto o = style(name);
    o.form = DiceForm::Develop;
    o.variety = kDiceVarietyDefault;
    o.variant = kDiceVariantAuto;
    p.reset();
    diceRoll(p, o, 0, 17321, snap);
    const auto& main = p.patterns[1];
    int quarterKicks = 0, otherKicks = 0;
    for (int at = 0; at < main.length; ++at) {
      if (!main.steps[0][at].hasNote() || !laneOn(main.steps[0][at], 0)) continue;
      if (at % 4 == 0) ++quarterKicks;
      else ++otherKicks;
    }
    if (!strcmp(name, "2-Step Garage")) {
      TEST_ASSERT_LESS_THAN(main.length / 4, quarterKicks);
      TEST_ASSERT_GREATER_THAN(0, otherKicks);
    } else TEST_ASSERT_EQUAL(main.length / 4, quarterKicks);
  }
}

void test_minimal_dub_and_liquid_keep_distinct_mixes() {
  auto state = [](const char* name) {
    p.reset();
    auto o = style(name);
    o.form = DiceForm::Develop;
    o.variant = kDiceVariantAuto;
    o.variety = kDiceVarietyDefault;
    return diceRoll(p, o, 0, 17321, snap);
  };
  const auto minimal = state("Minimal Techno");
  TEST_ASSERT_EQUAL(DiceFamily::Techno, diceFamilyOf({uint8_t(minimal.genre), uint8_t(minimal.style)}));
  TEST_ASSERT_EQUAL(0, p.kitOf(0)->send);
  TEST_ASSERT_EQUAL(0, p.instruments[p.tracks[2].instr].send);
  TEST_ASSERT_LESS_THAN(p.tracks[2].vol, p.tracks[4].vol);
  state("Dub Techno");
  TEST_ASSERT_LESS_OR_EQUAL(10, p.kitOf(0)->send);
  const auto& chord = p.instruments[p.tracks[1].instr];
  TEST_ASSERT_EQUAL(InstrType::Synth, chord.type);
  TEST_ASSERT_EQUAL(SynOsc::Tri, chord.synOsc[0]);
  TEST_ASSERT_EQUAL_STRING("MOTIF", p.tracks[1].name);
  TEST_ASSERT_EQUAL_STRING("BED", p.tracks[3].name);
  const auto& main = p.patterns[1];
  bool hasBed = false;
  std::set<int> attacks;
  for (int i = 0; i < main.length; ++i) {
    const auto& stab = main.steps[1][i];
    if (stab.hasNote()) {
      TEST_ASSERT_NOT_NULL(stab.find(Fx::CHD));
      TEST_ASSERT_NOT_NULL(stab.find(Fx::GAT));
      attacks.insert(stab.find(Fx::GAT)->val);
    }
    const auto& bed = main.steps[3][i];
    if (bed.hasNote()) {
      hasBed = true;
      TEST_ASSERT_NOT_NULL(bed.find(Fx::GAT));
      TEST_ASSERT_GREATER_OR_EQUAL(300, gatePercent(bed.find(Fx::GAT)->val));
    }
    TEST_ASSERT_TRUE(main.steps[5][i].isEmpty());
  }
  TEST_ASSERT_TRUE(hasBed);
  TEST_ASSERT_GREATER_THAN(1, attacks.size());
  state("Liquid DnB");
  const auto& bass = p.instruments[p.tracks[2].instr];
  TEST_ASSERT_EQUAL(SynOsc::Sine, bass.synOsc[0]);
  TEST_ASSERT_EQUAL(0, bass.send);
  TEST_ASSERT_EQUAL(0, bass.rsend);
  TEST_ASSERT_GREATER_THAN(32, p.instruments[p.tracks[1].instr].rsend);
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
        Instrument m;TrackCfg cfg;Rng rng(1);dice_detail::buildVoice(voice,m,cfg,true,0,rng,diceRecipe(g,s)->feel);
        TEST_ASSERT_EQUAL(r.type,m.type);TEST_ASSERT_GREATER_THAN(0,m.vol);
        TEST_ASSERT_EQUAL(r.machine,m.machine);TEST_ASSERT_EQUAL_STRING(r.name,m.name);
        TEST_ASSERT_LESS_THAN(InstrType::Count,m.type);TEST_ASSERT_LESS_THAN(FltMode::Count,m.fltMode);
        TEST_ASSERT_LESS_OR_EQUAL(127,m.cutoff);TEST_ASSERT_LESS_OR_EQUAL(127,m.reso);
        for(auto param:m.fmParam) TEST_ASSERT_LESS_OR_EQUAL(127,param);
        if(m.type==InstrType::Fm) TEST_ASSERT_LESS_THAN(FmMachine::Count,m.machine);
        if(m.type==InstrType::Drum) TEST_ASSERT_LESS_THAN(DrumMachine::Count,m.machine);
        if(m.type==InstrType::String) TEST_ASSERT_LESS_THAN(StringModel::Count,m.machine);
        if(!dicePercussion(voice) && voice.lfo[3] && voice.lfo[0]==uint8_t(LfoDest::Pitch)) {
          TEST_ASSERT_EQUAL(uint8_t(LfoDest::Fine),m.lfoDest);
          TEST_ASSERT_LESS_OR_EQUAL(24,m.lfoDepth);
        }
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
void test_sound_roll_can_revoice_the_same_phrase_at_default_variety() {
  p.reset();
  auto opts=style("Chillhop");
  opts.variety=kDiceVarietyDefault;
  opts.seed=1;
  diceRoll(p,opts,0,1,snap);
  before=p;
  opts.layers=kDiceSounds;
  std::set<int> cutoffs;
  for(int seed=1;seed<=16;++seed) {
    opts.seed=seed;
    diceRoll(p,opts,0,seed,snap);
    const auto& keys=p.instruments[p.tracks[2].instr];
    TEST_ASSERT_EQUAL(SynEnvDest::Ring,keys.synEnvDest);
    TEST_ASSERT_GREATER_THAN(64,keys.macro[kMacSenv]);
    TEST_ASSERT_GREATER_THAN(0,keys.velCut);
    TEST_ASSERT_EQUAL(19,keys.synSemi);
    cutoffs.insert(keys.cutoff);
    TEST_ASSERT_EQUAL_MEMORY(before.patterns,p.patterns,sizeof(p.patterns));
  }
  TEST_ASSERT_GREATER_OR_EQUAL(6,cutoffs.size());
  opts.seed=1;
  diceRoll(p,opts,0,1,snap);
  TEST_ASSERT_EQUAL_MEMORY(before.instruments,p.instruments,sizeof(p.instruments));
}

void test_unison_is_reserved_for_the_trance_hook() {
  p.reset();auto opts=style("Hard Trance");
  opts.seed=1;diceRoll(p,opts,0,1,snap);
  const auto& lead=p.instruments[p.tracks[2].instr];
  const auto& arp=p.instruments[p.tracks[3].instr];
  TEST_ASSERT_EQUAL_STRING("Lead",p.tracks[2].name);
  TEST_ASSERT_EQUAL_STRING("Arp",p.tracks[3].name);
  TEST_ASSERT_EQUAL(SynOsc::HyperSaw,lead.synOsc[0]);
  TEST_ASSERT_EQUAL(SynPhaseMode::Random,lead.synPhaseMode);
  TEST_ASSERT_EQUAL(SynOsc::Saw,arp.synOsc[0]);
  TEST_ASSERT_EQUAL(SynEnvDest::Mix,arp.synEnvDest);
}

void test_acid_accompaniment_uses_distinct_patches_even_on_a_targeted_roll() {
  auto o=style("Acid House");o.seed=1;o.variety=kDiceVarietyDefault;
  p.reset();diceRoll(p,o,0,1,snap);
  TEST_ASSERT_EQUAL_STRING("303",p.instruments[p.tracks[1].instr].name);
  std::set<std::string> names;
  for(int t=1;t<6;++t) names.insert(p.instruments[p.tracks[t].instr].name);
  TEST_ASSERT_EQUAL(5,names.size());
  // The player kept another instance of the arp's patch on the FX track.
  auto& fx=p.instruments[p.tracks[5].instr];
  std::strcpy(fx.name,p.instruments[p.tracks[4].instr].name);
  before=p;o.targetMask=1u<<4;o.layers=kDiceSounds;
  diceRoll(p,o,0,1,snap);
  TEST_ASSERT_NOT_EQUAL(0,std::strcmp(fx.name,p.instruments[p.tracks[4].instr].name));
  TEST_ASSERT_EQUAL_MEMORY(&before.instruments[before.tracks[5].instr],&fx,sizeof(fx));
  TEST_ASSERT_EQUAL_MEMORY(before.patterns,p.patterns,sizeof(p.patterns));
}

void test_dice_places_backing_tracks_around_the_hook_and_respects_manual_pan() {
  auto o=style("Hard Trance");o.seed=1;
  p.reset();diceRoll(p,o,0,1,snap);
  TEST_ASSERT_EQUAL(64,p.tracks[0].pan); // kit
  TEST_ASSERT_EQUAL(64,p.tracks[1].pan); // bass
  TEST_ASSERT_EQUAL(64,p.tracks[2].pan); // main lead
  TEST_ASSERT_LESS_THAN(56,p.tracks[3].pan);    // arp
  TEST_ASSERT_GREATER_THAN(72,p.tracks[4].pan); // chords
  o.layers=kDiceSounds;o.clean=false;
  for(int t=0;t<6;++t) p.tracks[t].pan=28+9*t;
  diceRoll(p,o,0,1,snap);
  for(int t=0;t<6;++t) TEST_ASSERT_EQUAL(28+9*t,p.tracks[t].pan);
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
void test_chillhop_variation_answers_the_keys_on_the_current_chord() {
  auto o=style("Chillhop");o.form=DiceForm::Develop;o.seed=1;o.variety=kDiceVarietyDefault;
  p.reset();p.scaleRoot=9;p.scaleType=uint8_t(ScaleType::Minor);diceRoll(p,o,0,1,snap);
  const auto& main=p.patterns[1];const auto& var=p.patterns[2];
  const auto& recipe=*diceRecipe(o.genre,o.style);const auto& variant=*diceVariant(o.genre,o.style,0);
  const auto plan=dice_detail::phrasePlan(variant,o.seed,o.variety,recipe.feel);
  for(int t:{2,3}) TEST_ASSERT_EQUAL_MEMORY(main.steps[t],var.steps[t],32*sizeof(Step));
  for(int begin:{32,48}) {
    int attacks[2]={};
    int previous[2]={};
    for(int at=begin;at<begin+16;++at) for(int t:{2,3}) {
      const auto& step=var.steps[t][at];if(!step.hasNote()) continue;
      TEST_ASSERT_EQUAL(at<begin+8 ? 2 : 3,t);
      if(attacks[t-2]) {
        if(t==2) TEST_ASSERT_GREATER_THAN(previous[0],step.note);
        else TEST_ASSERT_LESS_THAN(previous[1],step.note);
      }
      previous[t-2]=step.note;++attacks[t-2];
      dice_detail::DiceChord chord;
      TEST_ASSERT_TRUE(dice_detail::harmonyAt(variant,recipe,at*ticksPerStep(var.res),p.scaleRoot,p.scaleType,plan,chord));
      uint8_t tones[4];const int n=chordNotes(chord.root,chord.chord,p.scaleRoot,ScaleType::Minor,tones);
      bool fits=false;for(int i=0;i<n;++i) fits|=(step.note%12==tones[i]%12);
      TEST_ASSERT_TRUE(fits);
    }
    TEST_ASSERT_EQUAL(2,attacks[0]);TEST_ASSERT_EQUAL(3,attacks[1]);
  }
  before=p;diceRoll(p,o,0,999,snap);
  TEST_ASSERT_EQUAL_MEMORY(before.patterns,p.patterns,sizeof(p.patterns));
}

void test_arp_register_uses_the_bass_already_present_in_the_target_pattern() {
  auto o=style("Acid House");o.seed=1;o.variety=kDiceVarietyDefault;
  p.reset();diceRoll(p,o,0,1,snap);
  o.layers=kDicePattern;o.targetMask=1u<<4;
  auto& pat=p.patterns[0];
  for(auto& s:pat.steps[1]) s=Step();
  diceRoll(p,o,0,1,snap);before=p;  // reference with no bass notes
  int lowest=127;
  for(const auto& s:pat.steps[4]) if(s.hasNote()) lowest=std::min(lowest,int(s.note));
  TEST_ASSERT_LESS_THAN(48,lowest);
  for(int bassNote:{40,55}) {
    p=before;pat.steps[1][0].note=bassNote;
    diceRoll(p,o,0,1,snap);
    TEST_ASSERT_EQUAL(bassNote,pat.steps[1][0].note);
    for(int at=0;at<pat.length;++at) {
      auto expected=before.patterns[0].steps[4][at];const auto& arp=pat.steps[4][at];
      if(expected.hasNote()) {
        TEST_ASSERT_TRUE(arp.hasNote());
        TEST_ASSERT_GREATER_OR_EQUAL(bassNote==40 ? 55 : 65,arp.note);
        TEST_ASSERT_EQUAL(expected.note%12,arp.note%12);
        expected.note=arp.note;
      }
      TEST_ASSERT_EQUAL_MEMORY(&expected,&arp,sizeof(Step));
    }
  }
  p.tracks[1].mute=true;diceRoll(p,o,0,1,snap);
  TEST_ASSERT_EQUAL_MEMORY(before.patterns[0].steps[4],pat.steps[4],sizeof(pat.steps[4]));
  p.tracks[1].mute=false;p.instruments[p.tracks[1].instr].vol=0;diceRoll(p,o,0,1,snap);
  TEST_ASSERT_EQUAL_MEMORY(before.patterns[0].steps[4],pat.steps[4],sizeof(pat.steps[4]));
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
void test_high_variety_keeps_sound_architecture_and_drum_anchors() {
  p.reset();auto o=style("Acid House");diceRoll(p,o,0,123,snap);before=p;
  o.variety=100;diceRoll(p,o,0,456,snap);
  for(int t=1;t<3;++t) {
    const auto& a=before.instruments[before.tracks[t].instr];const auto& b=p.instruments[p.tracks[t].instr];
    TEST_ASSERT_EQUAL(a.type,b.type);TEST_ASSERT_EQUAL_MEMORY(a.synOsc,b.synOsc,sizeof(a.synOsc));
    TEST_ASSERT_EQUAL_STRING(a.name,b.name);
  }
  for(int i=0;i<64;++i) if(before.patterns[0].steps[0][i].hasNote()) {
    TEST_ASSERT_TRUE(p.patterns[0].steps[0][i].hasNote());
    TEST_ASSERT_EQUAL(laneMask(before.patterns[0].steps[0][i]),laneMask(p.patterns[0].steps[0][i])&laneMask(before.patterns[0].steps[0][i]));
  }
}
void test_sample_descriptions_build_distinct_synthesis_architectures() {
  p.reset();auto o=style("Folktronica");diceRoll(p,o,0,123,snap);
  TEST_ASSERT_EQUAL(InstrType::String,p.instruments[p.tracks[2].instr].type);
  o=style("Deep House");diceRoll(p,o,0,123,snap);
  const auto& sub=p.instruments[p.tracks[1].instr];
  TEST_ASSERT_EQUAL(InstrType::Synth,sub.type);TEST_ASSERT_EQUAL(SynOsc::Sine,sub.synOsc[0]);
}
void test_high_variety_bass_follows_the_chord_at_its_new_attack() {
  auto o=style("Deep House");o.seed=123;
  p.reset();o.variety=0;diceRoll(p,o,0,123,snap);before=p;o.variety=100;diceRoll(p,o,0,123,snap);
  for(int at=0;at<64;++at) {
    const auto& old=before.patterns[0].steps[1][at];const auto& note=p.patterns[0].steps[1][at];
    if(!note.hasNote())continue;
    if(old.hasNote()) TEST_ASSERT_INT_WITHIN(24,old.note,note.note);
    int latest=-1,track=-1;
    for(int t=2;t<4;++t)for(int i=0;i<=at;++i)if(p.patterns[0].steps[t][i].hasNote() && p.patterns[0].steps[t][i].find(Fx::CHD) && i>=latest){latest=i;track=t;}
    if(latest<0)continue;
    const auto& c=p.patterns[0].steps[track][latest];uint8_t tones[4];int n=chordNotes(c.note,c.find(Fx::CHD)->val,p.scaleRoot,static_cast<ScaleType>(p.scaleType),tones);
    bool inside=false;for(int i=0;i<n;++i)inside|=tones[i]%12==note.note%12;
    TEST_ASSERT_TRUE(inside);
  }
}
void test_every_variant_variety_keeps_attacks_harmony_and_bar_openings() {
  for(int g=0;g<diceGenreCount();++g) for(int style=0;style<diceStyleCount(g);++style) for(int variant=0;variant<16;++variant) {
    DiceOpts o;o.keepKey=true;o.genre=g;o.style=style;o.variant=variant;o.form=DiceForm::Classic;o.groove=0;o.variety=0;
    p.reset();diceRoll(p,o,0,123,snap);before=p;
    const auto& v=*diceVariant(g,style,variant);const auto layout=diceLayout(v);
    for(int amount : {60,100}) {
      p=before; o.variety=amount; diceRoll(p,o,0,123,snap);
      const auto& recipe=*diceRecipe(g,style);
      const auto band=dice_detail::rollBand(diceGenreName(g),recipe,123,amount,0);
      uint8_t ride=0;
      for(int i=0;i<6;++i) if(layout.lane[i]>=0 && dice_detail::bandRide(diceVoice(v,i),recipe,band))
        ride|=1u<<layout.lane[i];
      for(int t=0;t<6;++t) {
        const int source=layout.source[t];if(diceSupplement(source))continue; // support rhythms develop separately
        const bool harmonic=source>=0 && diceChord(diceVoice(v,source))!=255;
        const auto speed=toSpeed(p.patterns[0].trackSpeed[t]);
        const int ticks=ticksPerStep(p.patterns[0].res)*speedDiv(speed)/speedMul(speed);
        // Recomped DnB bass/chords and Gabber calls have new, style-specific attacks.
        if(source>=0 && ticks==kPpqn/4 && !diceVoice(v,source).length && !diceVoice(v,source).speed) {
          const auto kind=static_cast<DiceVoiceKind>(diceVoice(v,source).kind);
          if(band.family==dice_detail::BandFamily::Gabber ||
             (band.family==dice_detail::BandFamily::Dnb && (harmonic || (band.rephraseBass && kind==DiceVoiceKind::Bass)))) {
            for(const auto& step:p.patterns[0].steps[t]) if(step.hasNote()) {
              TEST_ASSERT_LESS_OR_EQUAL(127,step.note);
              if(const auto* chord=step.find(Fx::CHD)) TEST_ASSERT_LESS_OR_EQUAL(kChordMin9,chordType(chord->val));
            }
            continue;
          }
        }
        // At high Variety freely phrased melodic voices can leave the guide grid.
        // Chord attacks, percussion and independent loops keep their contracts.
        bool rewrite=false;
        if(amount>60 && source>=0 && !harmonic && ticks==kPpqn/4) {
          const auto& voice=diceVoice(v,source);
          const auto kind=static_cast<DiceVoiceKind>(voice.kind);
          const bool melodic=kind==DiceVoiceKind::Bass || kind==DiceVoiceKind::Lead ||
                             kind==DiceVoiceKind::Bell || kind==DiceVoiceKind::Stab;
          rewrite=melodic && !voice.length && !voice.speed;
          if(rewrite && kind!=DiceVoiceKind::Bass) {
            for(int bar=0;bar+16<=p.patterns[0].length;bar+=16) {
              int oldHits=0,newHits=0;
              for(int i=bar;i<bar+16;++i) {
                oldHits+=before.patterns[0].steps[t][i].hasNote();
                newHits+=p.patterns[0].steps[t][i].hasNote();
              }
              TEST_ASSERT_EQUAL_MESSAGE(oldHits,newHits,diceStyleName(g,style));
            }
          }
        }
        for(int at=0;at<p.patterns[0].length;++at) {
          const auto& a=before.patterns[0].steps[t][at];const auto& b=p.patterns[0].steps[t][at];
          if(p.trackIsDrum(t) && ride) {
            const unsigned anchors=a.hasNote() ? laneMask(a)&~ride : 0;
            TEST_ASSERT_EQUAL(anchors,anchors&(b.hasNote() ? laneMask(b) : 0));
            continue;
          }
          if(a.hasNote() && !rewrite) {
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
      const auto plan=dice_detail::phrasePlan(v,seed,o.variety,diceRecipe(o.genre,o.style)->feel);
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
  for(const char* name:{"ANALOGBS","SUB","808 SUB","REESE","E PIANO","ORGAN","STAB","WARM PAD","PLUCK","PULSE","ACOU GTR","FM BELL","FM KEYS","MALLET","303","SOFT RIM","VOX FORM","DRONE","SAW LEAD"})
    TEST_ASSERT_EQUAL_STRING(name,diceTimbre(diceVoiceForTimbre(name,123)).name);
}
void test_fx_hit_is_a_damped_percussion_instrument_and_support_has_no_noise_oscillator() {
  for(const char* name:{"Neurofunk","Acid Rave","Harsh Noise","Dark Ambient"}) {
    auto o=style(name); o.variety=kDiceVarietyDefault;
    for(int seed=1;seed<=8;++seed) {
      p.reset(); diceRoll(p,o,0,seed,snap);
      const auto& variant=*diceVariant(o.genre,o.style,0);
      const auto layout=diceLayout(variant);
      for(int t=0;t<kDiceRoles;++t) {
        if(diceSupplement(layout.source[t])) {
          const auto& m=p.instruments[p.tracks[t].instr];
          TEST_ASSERT_FALSE(m.type==InstrType::Chip && m.wave==uint8_t(Wave::Noise));
        }
        if(diceVoice(variant,t).kind!=uint8_t(DiceVoiceKind::Fx)) continue;
        const auto& drum=p.instruments[p.kitOf(0)->kit[layout.lane[t]].instr];
        TEST_ASSERT_EQUAL(InstrType::Drum,drum.type);
        TEST_ASSERT_EQUAL(DrumMachine::Rs9,drum.machine);
        TEST_ASSERT_LESS_THAN(10,drum.macro[kMacShp]);
      }
    }
  }
}

void test_ambient_pads_voice_the_drone_chord_at_their_attacks() {
  auto o=style("Dark Ambient");o.variety=kDiceVarietyDefault;o.seed=17;
  p.reset();p.scaleType=uint8_t(ScaleType::Minor);diceRoll(p,o,0,17,snap);
  const auto& recipe=*diceRecipe(o.genre,o.style);
  const auto& variant=*diceVariant(o.genre,o.style,0);
  const auto plan=dice_detail::phrasePlan(variant,o.seed,o.variety,recipe.feel);
  TEST_ASSERT_TRUE(plan.padHarmony);
  const auto layout=diceLayout(variant);
  std::set<int> openingNotes;
  for(int t=0;t<6;++t) {
    const int src=layout.source[t];
    if(src<0 || diceVoice(variant,src).kind!=uint8_t(DiceVoiceKind::Pad)) continue;
    const auto& pat=p.patterns[0];
    for(int at=0;at<pat.length;++at) {
      const auto& step=pat.steps[t][at]; if(!step.hasNote()) continue;
      dice_detail::DiceChord chord;
      TEST_ASSERT_TRUE(dice_detail::harmonyAt(variant,recipe,at*ticksPerStep(pat.res),p.scaleRoot,p.scaleType,plan,chord));
      uint8_t notes[4];
      const int n=chordNotes(chord.root,chord.chord,p.scaleRoot,static_cast<ScaleType>(p.scaleType),notes);
      bool inside=false;
      for(int i=0;i<n;++i) inside|=notes[i]%12==step.note%12;
      TEST_ASSERT_TRUE(inside);
      if(!at) openingNotes.insert(step.note%12);
    }
  }
  TEST_ASSERT_GREATER_THAN(1,openingNotes.size());
}

void test_default_variety_develops_repeated_riffs_in_a_fixed_key() {
  for(const char* name:{"UK Garage","Chillhop"}) {
    auto o=style(name);o.variety=kDiceVarietyDefault;
    const auto layout=diceLayout(*diceVariant(o.genre,o.style,0));
    int repeated=0, comparable=0;
    std::set<std::string> phrases;
    for(int seed=1;seed<=16;++seed) {
      p.reset();p.scaleRoot=9;p.scaleType=uint8_t(ScaleType::Minor);
      o.seed=seed;diceRoll(p,o,0,seed,snap);
      const auto& pat=p.patterns[0];std::string phrase;
      for(int t=0;t<kDiceRoles;++t) {
        if(layout.source[t]<0) continue;
        int first=-1;
        for(int at=0;at<pat.length;++at) {
          const auto& note=pat.steps[t][at];
          if(!note.hasNote() || note.find(Fx::CHD)) continue;
          if(first<0) first=note.note;
          phrase.push_back(char(64+note.note-first)); // invariant to transposing the track
          if(at<16 || !pat.steps[t][at-16].hasNote()) continue;
          ++comparable;repeated+=note.note==pat.steps[t][at-16].note;
        }
      }
      phrases.insert(phrase);
    }
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(10,phrases.size(),name);
    TEST_ASSERT_GREATER_THAN(0,comparable);
    TEST_ASSERT_LESS_THAN_MESSAGE(comparable*3/4,repeated,name);
  }
}

void test_fill_preserves_the_kick_and_only_rolls_the_solo_snare() {
  auto o = style("UK Garage");
  o.variety = kDiceVarietyDefault;
  o.seed = 1;
  for (const auto form : {DiceForm::Classic, DiceForm::Develop, DiceForm::Arrange}) {
    p.reset();
    o.form = form;
    diceRoll(p, o, 0, 1, snap);
    const int fill = form == DiceForm::Classic ? 1 : form == DiceForm::Develop ? 2 : 3;
    const Step* row = p.patterns[fill].steps[0];
    const Step* main = p.patterns[form == DiceForm::Classic ? 0 : 1].steps[0];
    int rolls = 0;
    for (int at = 48; at < 64; ++at) {
      if (form != DiceForm::Arrange) TEST_ASSERT_EQUAL(laneOn(main[at], 0), laneOn(row[at], 0));
      const auto* rat = row[at].find(Fx::RAT);
      if (!rat) continue;
      TEST_ASSERT_EQUAL(2, laneMask(row[at]));
      TEST_ASSERT_EQUAL((kRatUp << 4) | 2, rat->val);
      ++rolls;
    }
    TEST_ASSERT_EQUAL(1, rolls);
    TEST_ASSERT_EQUAL(2, laneMask(row[59]));
    TEST_ASSERT_LESS_THAN(row[63].note, row[59].note);
  }
}

void test_deep_house_answers_on_percussion_and_zero_variety_keeps_the_kit() {
  auto o = style("Deep House");
  o.form = DiceForm::Develop;
  o.seed = 1;
  for (int amount : {0, kDiceVarietyDefault}) {
    p.reset();
    o.variety = amount;
    diceRoll(p, o, 0, 1, snap);
    const Step* main = p.patterns[1].steps[0];
    const Step* var = p.patterns[2].steps[0];
    if (!amount) {
      TEST_ASSERT_EQUAL_MEMORY(main + 61, var + 61, 2 * sizeof(Step));
      for (int at = 48; at < 64; ++at) TEST_ASSERT_NULL(var[at].find(Fx::RAT));
      continue;
    }
    for (int at = 48; at < 64; ++at) {
      TEST_ASSERT_EQUAL(laneOn(main[at], 0), laneOn(var[at], 0));
      TEST_ASSERT_NULL(var[at].find(Fx::RAT));
    }
    TEST_ASSERT_EQUAL(1 << 5, laneMask(var[61]));
    TEST_ASSERT_EQUAL(1 << 5, laneMask(var[62]));
    TEST_ASSERT_LESS_THAN(var[62].note, var[61].note);
  }
}

void test_busy_fill_leaves_space_for_returning_bass_and_respects_programmed_repeats() {
  auto o = style("UK Garage");
  o.form = DiceForm::Develop;
  o.variety = kDiceVarietyDefault;
  p.reset();
  const auto& v = *diceVariant(o.genre, o.style, 0);
  const auto layout = diceLayout(v);
  const auto feel = diceRecipe(o.genre, o.style)->feel;
  int bass = -1;
  for (int t = 0; t < kDiceRoles; ++t)
    if (layout.source[t] >= 0 && diceVoice(v, layout.source[t]).kind == uint8_t(DiceVoiceKind::Bass)) bass = t;
  TEST_ASSERT_GREATER_OR_EQUAL(0, bass);
  for (int seed : {1, 2, 3}) for (int bassAt : {0, 3}) {
    o.seed = seed;
    diceDraft(p, o, 0, seed, snap);
    auto& dst = snap.parts.pats[2];
    auto& next = snap.parts.pats[1];
    // Identical dense bar for every pass, ending in a programmed shaker ratchet.
    Step* row = dst.steps[0];
    for (int at = 48; at < 64; ++at) {
      row[at] = Step{};
      row[at].note = 80;
      setLane(row[at], 7, true);
      if (at % 4 == 0) setLane(row[at], at % 8 ? 1 : 0, true);
    }
    putFx(row[63], Fx::RAT, 3);
    const Step authored = row[63];
    for (auto& cell : next.steps[bass]) cell = Step{};
    next.trackSpeed[bass] = 0;
    next.steps[bass][bassAt].note = 40;
    const auto plan = dice_detail::phrasePlan(v, seed, o.variety, feel);
    dice_detail::drumFill(dst, next, p.patterns[1], snap.parts, layout, v, plan, snap.patMask);
    TEST_ASSERT_EQUAL_MEMORY(&authored, row + 63, sizeof(Step));
    TEST_ASSERT_TRUE(laneOn(row[60], 1));
    if (seed == 3) {
      TEST_ASSERT_EQUAL(1 << 7, laneMask(row[62]));
    } else if (seed == 2 && bassAt == 0) {
      TEST_ASSERT_TRUE(row[62].isEmpty());
    } else {
      TEST_ASSERT_EQUAL(1 << 1, laneMask(row[62]));
      TEST_ASSERT_NOT_NULL(row[62].find(Fx::RAT));
    }
  }
}

void test_minimal_techno_low_end_and_support_in_the_playable_mix() {
  for (int seed : {1, 2, 7, 19}) {
    p.reset();
    auto o = style("Minimal Techno");
    o.form = DiceForm::Develop;
    o.variety = kDiceVarietyDefault;
    o.keepKey = false;
    o.seed = seed;
    o.variant = seed == 1 ? 0 : kDiceVariantAuto;
    const auto r = diceRoll(p, o, 0, seed, snap);
    const auto layout = diceLayout(*diceVariant(o.genre, o.style, r.variant));
    const auto& main = p.patterns[1];
    int sum = 0, bassHits = 0;
    for (const auto& step : main.steps[1]) if (step.hasNote()) {
      sum += step.note;
      ++bassHits;
    }
    TEST_ASSERT_GREATER_THAN(0, bassHits);
    TEST_ASSERT_INT_WITHIN(8, 40, sum / bassHits);
    for (int t = 0; t < kDiceRoles; ++t) if (layout.source[t] == dicePartSource(DicePart::Arp)) {
      int attacks = 0;
      for (const auto& step : main.steps[t]) attacks += step.hasNote();
      TEST_ASSERT_GREATER_THAN(0, attacks);
      TEST_ASSERT_LESS_OR_EQUAL(12, attacks);
    }
    const auto& kick = p.instruments[p.kitOf(0)->kit[0].instr];
    if (kick.type == InstrType::Fm) {
      TEST_ASSERT_LESS_THAN(64, kick.fmParam[0]);  // BD Modern at neutral tune settles at 65 Hz
      TEST_ASSERT_GREATER_THAN(0, kick.fmParam[7]);
    }
  }
}

// Compare the audible parts with variant, scale and key fixed across rolls.
void test_dnb_changes_bass_instrument_and_comping_in_a_fixed_variant() {
  for (const char* name : {"Drum & Bass", "Minimal DnB", "Liquid DnB"}) {
    auto o = style(name);
    o.variant=0; o.variety=100; o.len=128; o.density=DiceDensity::Sparse;
    const auto& variant=*diceVariant(o.genre,o.style,0);
    const auto layout=diceLayout(variant);
    std::set<std::string> basses, rhythms, comping;
    for (unsigned seed=1;seed<=20;++seed) {
      p.reset(); diceRoll(p,o,0,seed,snap);
      for (int t=0;t<6;++t) {
        const int src=layout.source[t];
        if (src<0) continue;
        const auto& voice=diceVoice(variant,src);
        std::string attacks;
        for (const auto& step:p.patterns[0].steps[t]) attacks+=step.hasNote() ? 'x' : '.';
        if (voice.kind==uint8_t(DiceVoiceKind::Bass)) {
          basses.insert(p.instruments[p.tracks[t].instr].name);
          rhythms.insert(attacks);
        } else if (diceChord(voice)!=255) comping.insert(attacks);
      }
    }
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(3,basses.size(),name);
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(18,rhythms.size(),name);
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(5,comping.size(),name);
  }
}

void test_minimal_dnb_can_leave_space_for_a_drone_or_a_single_note() {
  auto o=style("Minimal DnB");
  o.variant=0; o.variety=100; o.len=128; o.density=DiceDensity::Sparse;
  std::set<std::string> roles;
  for(int seed=1;seed<=20;++seed) {
    p.reset(); diceRoll(p,o,0,seed,snap);
    const auto& main=p.patterns[0];
    const auto& chordPatch=p.instruments[p.tracks[2].instr];
    int notes=0, chords=0, longest=0, ride=0;
    for(int at=0;at<main.length;++at) {
      ride+=laneOn(main.steps[0][at],3);
      const auto& step=main.steps[2][at];
      if(!step.hasNote()) continue;
      ++notes; chords+=step.find(Fx::CHD)!=nullptr;
      if(const auto* gate=step.find(Fx::GAT)) longest=std::max(longest,int(gatePercent(gate->val)));
    }
    TEST_ASSERT_GREATER_THAN(0,notes);
    TEST_ASSERT_GREATER_THAN(0,ride);
    TEST_ASSERT_LESS_OR_EQUAL(36,ride);
    if(!strcmp(chordPatch.name,"DRONE")) {
      TEST_ASSERT_EQUAL(0,chords); TEST_ASSERT_LESS_OR_EQUAL(4,notes);
      TEST_ASSERT_GREATER_THAN(600,longest); roles.insert("drone");
    } else if(!strcmp(chordPatch.name,"PLUCK")) {
      TEST_ASSERT_EQUAL(0,chords); roles.insert("pluck");
    } else {
      TEST_ASSERT_EQUAL(notes,chords); roles.insert("chords");
    }
  }
  TEST_ASSERT_EQUAL(3,roles.size());
}

void test_gabber_kick_follows_the_key_and_backing_leaves_it_room() {
  auto o=style("Gabber"); o.variety=100; o.variant=0; o.len=128;
  for (int key=0;key<12;++key) {
    p.reset(); p.scaleRoot=key; diceRoll(p,o,0,key+1,snap);
    const auto& kick=p.instruments[p.kitOf(0)->kit[0].instr];
    TEST_ASSERT_EQUAL(key,(31+kick.transpose)%12);
    const auto& pat=p.patterns[0];
    for(int bar=0;bar<pat.length;bar+=16) {
      int bass=0, upper=0;
      for(int at=bar;at<bar+16;++at) {
        const auto& answer=pat.steps[3][at];
        if(answer.hasNote()) {++bass; TEST_ASSERT_FALSE(laneOn(pat.steps[0][at],0));}
        for(int t:{1,2,4,5}) upper+=pat.steps[t][at].hasNote();
      }
      TEST_ASSERT_LESS_OR_EQUAL(2,bass);
      TEST_ASSERT_LESS_OR_EQUAL(7,upper);
    }
  }
}

void test_gabber_rolls_vary_the_909_attack_and_keep_the_driven_tail() {
  auto o = style("Gabber");
  o.variant = 0;
  std::set<int> attacks;
  for (int seed : {1, 7, 19, 43}) {
    p.reset();
    o.variety = seed == 1 ? 0 : kDiceVarietyDefault;
    o.seed = seed;
    diceRoll(p, o, 0, seed, snap);
    const auto& kick = p.instruments[p.kitOf(0)->kit[0].instr];
    TEST_ASSERT_EQUAL(DrumMachine::Bd9, kick.machine);
    TEST_ASSERT_GREATER_OR_EQUAL(98, kick.macro[kMacShp]);
    TEST_ASSERT_GREATER_OR_EQUAL(62, kick.macro[kMacDec]);
    attacks.insert(kick.macro[kMacSwp]);
  }
  TEST_ASSERT_GREATER_THAN(1, attacks.size());
}

void test_dark_psy_bass_retriggers_and_leaves_room_after_the_pickup() {
  p.reset();
  auto o = style("Dark Psy");
  o.variant = 0;
  o.variety = kDiceVarietyDefault;
  diceRoll(p, o, 0, 1, snap);
  const auto& main = p.patterns[0];
  const auto& bass = p.instruments[p.tracks[1].instr];
  TEST_ASSERT_EQUAL(SynPhaseMode::Retrig, bass.synPhaseMode);
  int body = 0, pickups = 0;
  for (int i = 0; i < main.length; ++i) {
    const auto& step = main.steps[1][i];
    if (!step.hasNote()) continue;
    const auto* gate = step.find(Fx::GAT);
    TEST_ASSERT_NOT_NULL(gate);
    if (i % 4 == 3) {
      TEST_ASSERT_EQUAL(54, gatePercent(gate->val));
      ++pickups;
    } else {
      TEST_ASSERT_EQUAL(72, gatePercent(gate->val));
      ++body;
    }
  }
  TEST_ASSERT_GREATER_THAN(0, body);
  TEST_ASSERT_GREATER_THAN(0, pickups);
}

void test_full_on_high_variety_writes_different_bass_grooves_and_hook_rhythms() {
  auto o=style("Full-On");
  o.form=DiceForm::Arrange; o.len=128; o.groove=100;
  o.density=DiceDensity::Sparse; o.variety=100; o.variant=0;
  o.layers|=kDiceSong;
  std::set<int> bassPulses, hookEntrances;
  std::set<std::string> arrangements;
  for(int seed=1;seed<=24;++seed) {
    p.reset(); o.seed=seed;
    diceRoll(p,o,0,seed,snap);
    const auto& main=p.patterns[1];
    int pulse=0, first=-1;
    for(int at=0;at<main.length;++at) {
      if(main.steps[1][at].hasNote()) {
        TEST_ASSERT_NOT_EQUAL(0,at%4);  // kick owns the downbeat
        pulse|=1<<(at%4);
      }
      if(first<0 && main.steps[2][at].hasNote()) first=at;
    }
    TEST_ASSERT_GREATER_OR_EQUAL(0,first);
    bassPulses.insert(pulse);
    hookEntrances.insert(first);
    arrangements.emplace(reinterpret_cast<const char*>(p.song),p.songLen);
    TEST_ASSERT_EQUAL(kArrangeIntro,p.song[0]);
    TEST_ASSERT_EQUAL(kArrangeOutro,p.song[p.songLen-1]);
    for(int i=0;i<p.songLen-1;++i) if(p.song[i]==kArrangeBuild) TEST_ASSERT_EQUAL(kArrangeDrop,p.song[i+1]);
    before=p; p.reset(); diceRoll(p,o,0,0,snap);
    TEST_ASSERT_EQUAL_MEMORY(&before,&p,sizeof(Project));
  }
  TEST_ASSERT_GREATER_OR_EQUAL(3,bassPulses.size());
  TEST_ASSERT_GREATER_OR_EQUAL(4,hookEntrances.size());
  TEST_ASSERT_EQUAL(4,arrangements.size());
}

int main() { UNITY_BEGIN();
  RUN_TEST(test_full_on_high_variety_writes_different_bass_grooves_and_hook_rhythms);
  RUN_TEST(test_dnb_changes_bass_instrument_and_comping_in_a_fixed_variant);
  RUN_TEST(test_minimal_dnb_can_leave_space_for_a_drone_or_a_single_note);
  RUN_TEST(test_gabber_kick_follows_the_key_and_backing_leaves_it_room);
  RUN_TEST(test_gabber_rolls_vary_the_909_attack_and_keep_the_driven_tail);
  RUN_TEST(test_dark_psy_bass_retriggers_and_leaves_room_after_the_pickup);
  RUN_TEST(test_minimal_techno_low_end_and_support_in_the_playable_mix);
  RUN_TEST(test_busy_fill_leaves_space_for_returning_bass_and_respects_programmed_repeats);
  RUN_TEST(test_fill_preserves_the_kick_and_only_rolls_the_solo_snare);
  RUN_TEST(test_deep_house_answers_on_percussion_and_zero_variety_keeps_the_kit);
  RUN_TEST(test_entire_guide_catalogue);
  RUN_TEST(test_curated_menu_keeps_saved_choices_and_recovers_retired_styles);
  RUN_TEST(test_dice_dialog_encoder_wraps_all_genres_and_styles);
  RUN_TEST(test_curated_all_roll_puts_acid_filter_movement_on_the_foreground_riff);
  RUN_TEST(test_curated_techno_ducks_the_bass_from_the_kit_only_on_the_full_sound_roll);
  RUN_TEST(test_curated_trance_leaves_its_native_arp_behind_the_lead);
  RUN_TEST(test_curated_ambient_lets_textures_ring_without_an_added_drum_fill);
  RUN_TEST(test_jump_up_and_dark_psy_have_dedicated_voice_design);
  RUN_TEST(test_dark_psy_holds_the_roll_and_breaks_at_the_section_turn);
  RUN_TEST(test_garage_styles_distinguish_four_on_the_floor_from_two_step);
  RUN_TEST(test_minimal_dub_and_liquid_keep_distinct_mixes);
  RUN_TEST(test_fx_hit_is_a_damped_percussion_instrument_and_support_has_no_noise_oscillator);
  RUN_TEST(test_default_variety_develops_repeated_riffs_in_a_fixed_key);
  RUN_TEST(test_ambient_pads_voice_the_drone_chord_at_their_attacks);
  RUN_TEST(test_acid_house_has_six_source_roles_and_canonical_rhythm);
  RUN_TEST(test_all_5072_variants_have_valid_facts_and_synthesis_recipes);
  RUN_TEST(test_lock_target_timing_and_external_instruments_are_preserved);
  RUN_TEST(test_seed_replay_layers_and_undo);
  RUN_TEST(test_fm_decay_and_pad_duration_reach_the_sound_engine);
  RUN_TEST(test_fixed_seed_overrides_the_call_seed);
  RUN_TEST(test_sound_roll_can_revoice_the_same_phrase_at_default_variety);
  RUN_TEST(test_unison_is_reserved_for_the_trance_hook);
  RUN_TEST(test_acid_accompaniment_uses_distinct_patches_even_on_a_targeted_roll);
  RUN_TEST(test_dice_places_backing_tracks_around_the_hook_and_respects_manual_pan);
  RUN_TEST(test_small_changes_have_bounded_effect);
  RUN_TEST(test_every_style_rolls_and_roundtrips_undo_at_all_lengths);
  RUN_TEST(test_develop_has_a_distinct_variation_and_withdraws_at_the_end);
  RUN_TEST(test_chillhop_variation_answers_the_keys_on_the_current_chord);
  RUN_TEST(test_arp_register_uses_the_bass_already_present_in_the_target_pattern);
  RUN_TEST(test_auto_variant_uses_seed_and_explicit_tempo_ignores_role_locks);
  RUN_TEST(test_targeted_grid_conversion_accounts_for_track_speed);
  RUN_TEST(test_variety_changes_music_and_sounds_beyond_velocity);
  RUN_TEST(test_percussion_is_one_kit_with_simultaneous_lanes);
  RUN_TEST(test_repeated_rolls_reuse_safe_kit_slots_and_fixed_seed_is_audible);
  RUN_TEST(test_drum_lock_preserves_kit_lanes_and_auto_avoids_previous_variant);
  RUN_TEST(test_full_roll_auditions_new_main_when_old_pattern_is_selected);
  RUN_TEST(test_kit_retains_recipe_space_sends);
  RUN_TEST(test_high_variety_keeps_sound_architecture_and_drum_anchors);
  RUN_TEST(test_sample_descriptions_build_distinct_synthesis_architectures);
  RUN_TEST(test_high_variety_bass_follows_the_chord_at_its_new_attack);
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
