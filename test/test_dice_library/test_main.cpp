#include <unity.h>
#include <vector>
#include <cstring>
#include "dice_library.h"
using namespace mt;
void setUp(){} void tearDown(){}
static void put16(std::vector<uint8_t>&b,int at,int v){b[at]=v;b[at+1]=v>>8;}
static void put32(std::vector<uint8_t>&b,int at,uint32_t v){put16(b,at,v);put16(b,at+2,v>>16);}
static std::vector<uint8_t> library(){
 std::vector<uint8_t>b(16+64+16);memcpy(b.data(),"DML1",4);put16(b,4,1);put16(b,6,64);put32(b,8,1);
 put16(b,16,1);b[18]=1;b[19]=0;b[20]=(int)ScaleType::Minor;b[21]=1;put16(b,22,120);put32(b,24,80);put32(b,28,16);
 memcpy(b.data()+32,"Test bass",10);put16(b,80,0);put16(b,82,24);b[84]=36;b[85]=100;b[86]=255;
 put16(b,88,96);put16(b,90,48);b[92]=39;b[93]=70;b[94]=255;return b;
}
void test_valid_pick_and_bounds(){
 auto b=library();DiceMidiLibrary lib(b.data(),b.size());TEST_ASSERT_TRUE(lib.valid());TEST_ASSERT_EQUAL(1,lib.count());
 DicePhrase ph;TEST_ASSERT_TRUE(lib.pick(0,DiceRole::Bass,123,ph));TEST_ASSERT_EQUAL(2,ph.count);TEST_ASSERT_EQUAL(39,ph.events[1].note);
 TEST_ASSERT_FALSE(lib.pick(1,DiceRole::Bass,123,ph));TEST_ASSERT_FALSE(lib.pick(0,DiceRole::Drums,123,ph));
 b[4]=2;TEST_ASSERT_FALSE(DiceMidiLibrary(b.data(),b.size()).valid());
 b=library();put32(b,24,95);TEST_ASSERT_FALSE(DiceMidiLibrary(b.data(),b.size()).valid());
 b=library();b[85]=0;TEST_ASSERT_FALSE(DiceMidiLibrary(b.data(),b.size()).valid());
}
void test_phrase_timing_velocity_transpose(){
 auto b=library();DiceMidiLibrary lib(b.data(),b.size());DicePhrase ph;lib.pick(0,DiceRole::Bass,123,ph);
 ph.events[0].tick=3;Step out[kMaxSteps];auto r=diceWritePhrase(ph,DiceRole::Bass,out,32,Resolution::Sixteenth,2,ScaleType::Minor,nullptr);
 TEST_ASSERT_TRUE(r.changed);TEST_ASSERT_EQUAL(38,out[0].note);TEST_ASSERT_EQUAL(100,out[0].vel);TEST_ASSERT_NOT_NULL(out[0].find(Fx::NDG));
 TEST_ASSERT_NOT_NULL(out[0].find(Fx::GAT));TEST_ASSERT_EQUAL(100,gatePercent(out[0].find(Fx::GAT)->val));
 TEST_ASSERT_INT_WITHIN(3,200,gatePercent(out[4].find(Fx::GAT)->val));
 TEST_ASSERT_EQUAL(41,out[4].note);TEST_ASSERT_EQUAL(38,out[16].note);
}
void test_chords_keep_polyphony_and_mark_reduction(){
 DicePhrase p{};p.bars=1;p.root=0;p.scale=(uint8_t)ScaleType::Major;p.count=3;
 p.events[0]={0,96,60,100,255};p.events[1]={0,96,64,90,255};p.events[2]={0,96,67,80,255};
 Step out[kMaxSteps];auto r=diceWritePhrase(p,DiceRole::Chords,out,16,Resolution::Sixteenth,0,ScaleType::Major,nullptr);
 TEST_ASSERT_TRUE(r.changed);TEST_ASSERT_FALSE(r.approximate);TEST_ASSERT_NOT_NULL(out[0].find(Fx::CHD));
 uint8_t ns[4];int n=chordNotes(out[0].note,out[0].find(Fx::CHD)->val,0,ScaleType::Major,ns);
 TEST_ASSERT_EQUAL(3,n);TEST_ASSERT_EQUAL(60,ns[0]);TEST_ASSERT_EQUAL(64,ns[1]);TEST_ASSERT_EQUAL(67,ns[2]);
 p.count=5;p.events[3]={0,96,71,90,255};p.events[4]={0,96,74,90,255};
 r=diceWritePhrase(p,DiceRole::Chords,out,16,Resolution::Sixteenth,0,ScaleType::Major,nullptr);
 TEST_ASSERT_TRUE(r.approximate);TEST_ASSERT_TRUE(r.changed);
}
void test_drums_use_lanes_and_polyphonic_mask(){
 DicePhrase p{};p.bars=1;p.count=2;p.events[0]={0,24,36,100,0};p.events[1]={0,24,42,80,2};
 Instrument kit;kitSetDefaults(kit);kit.kit[0].instr=24;kit.kit[2].instr=25;
 Step out[kMaxSteps];auto r=diceWritePhrase(p,DiceRole::Drums,out,16,Resolution::Sixteenth,0,ScaleType::Minor,&kit);
 TEST_ASSERT_TRUE(r.changed);TEST_ASSERT_TRUE(laneOn(out[0],0));TEST_ASSERT_TRUE(laneOn(out[0],2));TEST_ASSERT_EQUAL(100,out[0].note);
}

void test_long_pad_keeps_duration_with_marked_continuations(){
 DicePhrase p{};p.bars=4;p.root=0;p.scale=(uint8_t)ScaleType::Major;p.count=3;
 p.events[0]={0,1536,60,100,255};p.events[1]={0,1536,64,90,255};p.events[2]={0,1536,67,80,255};
 Step out[kMaxSteps];auto r=diceWritePhrase(p,DiceRole::Chords,out,64,Resolution::Sixteenth,0,ScaleType::Major,nullptr);
 TEST_ASSERT_TRUE(r.changed);TEST_ASSERT_TRUE(r.approximate);
 for(int step=0;step<64;step+=8){TEST_ASSERT_TRUE(out[step].hasNote());TEST_ASSERT_NOT_NULL(out[step].find(Fx::CHD));TEST_ASSERT_EQUAL(800,gatePercent(out[step].find(Fx::GAT)->val));}
}
// All three candidates are considered; bass phrases outside the playable register
// must lose to a compact bass line independently of the lottery seed.
void test_matching_prefers_role_register_over_random_pick(){
 const int count=3, table=16+64*count;std::vector<uint8_t>b(table+count*32);
 memcpy(b.data(),"DML1",4);put16(b,4,1);put16(b,6,64);put32(b,8,count);
 for(int c=0;c<count;++c){int at=16+c*64;put16(b,at,1);b[at+2]=1;b[at+4]=(int)ScaleType::Minor;b[at+5]=1;
 put32(b,at+8,table+c*32);put32(b,at+12,32);strcpy((char*)b.data()+at+16,c==1?"musical bass":"extreme");
 for(int j=0;j<4;++j){int e=table+c*32+j*8;put16(b,e,j*96);put16(b,e+2,48);b[e+4]=c==0?0:c==1?36+j:127;b[e+5]=100;b[e+6]=255;}}
 DiceMidiLibrary lib(b.data(),b.size());TEST_ASSERT_TRUE(lib.valid());
 for(uint32_t seed=1;seed<=20;++seed){DicePhrase phrase;TEST_ASSERT_TRUE(lib.pick(0,DiceRole::Bass,seed,phrase));TEST_ASSERT_EQUAL_STRING("musical bass",phrase.name);}
}
void test_matching_seed_replays_but_keeps_phrase_variety(){
 const int table=16+3*64;std::vector<uint8_t>b(table+18*8);
 memcpy(b.data(),"DML1",4);put16(b,4,1);put16(b,6,64);put32(b,8,3);int event=table;
 for(int c=0;c<3;++c){int at=16+c*64,count=4+2*c;put16(b,at,1);b[at+2]=1;b[at+4]=(int)ScaleType::Minor;b[at+5]=1;
 put32(b,at+8,event);put32(b,at+12,count*8);b[at+16]='A'+c;
 for(int j=0;j<count;++j,event+=8){put16(b,event,j*384/count);put16(b,event+2,24);b[event+4]=36;b[event+5]=100;b[event+6]=255;}}
 DiceMidiLibrary lib(b.data(),b.size());TEST_ASSERT_TRUE(lib.valid());unsigned seen=0;
 for(uint32_t seed=1;seed<=200;++seed){DicePhrase a,b;TEST_ASSERT_TRUE(lib.pick(0,DiceRole::Bass,seed,a));TEST_ASSERT_TRUE(lib.pick(0,DiceRole::Bass,seed,b));
 TEST_ASSERT_EQUAL_STRING(a.name,b.name);seen|=1u<<(a.name[0]-'A');}
 TEST_ASSERT_TRUE_MESSAGE((seen&(seen-1))!=0,"matching must preserve variety across seeds");
}
int main(){UNITY_BEGIN();RUN_TEST(test_matching_seed_replays_but_keeps_phrase_variety);RUN_TEST(test_matching_prefers_role_register_over_random_pick);RUN_TEST(test_long_pad_keeps_duration_with_marked_continuations);RUN_TEST(test_valid_pick_and_bounds);RUN_TEST(test_phrase_timing_velocity_transpose);RUN_TEST(test_chords_keep_polyphony_and_mark_reduction);RUN_TEST(test_drums_use_lanes_and_polyphonic_mask);return UNITY_END();}
