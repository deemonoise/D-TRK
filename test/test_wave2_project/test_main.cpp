#include <unity.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "project_io.h"
#include "inst_codec.h"
#include "perf_fx.h"
using namespace mt;
namespace {
struct Sink final : ByteSink {
 std::vector<uint8_t> bytes;
 bool write(const void* p,size_t n) override {const auto* b=static_cast<const uint8_t*>(p);bytes.insert(bytes.end(),b,b+n);return true;}
};
struct Source final : ByteSource {
 const std::vector<uint8_t>& bytes;size_t pos=0;
 explicit Source(const std::vector<uint8_t>& b):bytes(b){}
 bool read(void* p,size_t n) override {if(pos>bytes.size()||n>bytes.size()-pos)return false;memcpy(p,bytes.data()+pos,n);pos+=n;return true;}
 bool skip(size_t n) override {if(pos>bytes.size()||n>bytes.size()-pos)return false;pos+=n;return true;}
};
uint32_t u32(const uint8_t* b){return b[0]|uint32_t(b[1])<<8|uint32_t(b[2])<<16|uint32_t(b[3])<<24;}
void put32(std::vector<uint8_t>& b,uint32_t v){for(int i=0;i<4;++i)b.push_back(static_cast<uint8_t>(v>>(8*i)));}
struct Chunk {std::string id;std::vector<uint8_t> body;};
std::vector<Chunk> chunks(const std::vector<uint8_t>& bytes){
 std::vector<Chunk> out;
 for(size_t pos=8;pos+8<=bytes.size();){
  std::string id(reinterpret_cast<const char*>(bytes.data()+pos),4);uint32_t size=u32(bytes.data()+pos+4);
  TEST_ASSERT_TRUE(size<=bytes.size()-pos-8);if(id=="CRC ")break;
  out.push_back({id,{bytes.begin()+pos+8,bytes.begin()+pos+8+size}});pos+=8+size;
 }
 return out;
}
std::vector<uint8_t> rebuild(const std::vector<uint8_t>& original,const std::vector<Chunk>& list){
 std::vector<uint8_t> out(original.begin(),original.begin()+8);
 for(const auto& c:list){out.insert(out.end(),c.id.begin(),c.id.end());put32(out,c.body.size());out.insert(out.end(),c.body.begin(),c.body.end());}
 uint32_t crc=crc32(out.data(),out.size());out.insert(out.end(),{'C','R','C',' '});put32(out,4);put32(out,crc);return out;
}
Chunk& chunk(std::vector<Chunk>& list,const char* id){auto it=std::find_if(list.begin(),list.end(),[&](const Chunk& c){return c.id==id;});TEST_ASSERT_TRUE(it!=list.end());return *it;}
std::unique_ptr<Project> project(){auto p=std::make_unique<Project>();p->reset();
 auto& sample=p->instruments[0];sample.type=InstrType::Sample;sample.sliceMode=static_cast<uint8_t>(SliceMode::Fx);sample.chopN=sample.sliceCount=128;
 for(int i=0;i<128;++i)sample.slices[i]=static_cast<uint16_t>(i*500);
 sample.mod[0]={static_cast<uint8_t>(ModSource::Velocity),static_cast<uint8_t>(LfoDest::Cutoff),-31,static_cast<uint8_t>(ModPolarity::Bipolar),static_cast<uint8_t>(ModSource::Lfo2)};
 p->instruments[31].mod[7]={static_cast<uint8_t>(ModSource::Follower),static_cast<uint8_t>(LfoDest::Pan),63,static_cast<uint8_t>(ModPolarity::Unipolar),0};
 p->instruments[1].type=InstrType::Fm;p->instruments[1].machine=static_cast<uint8_t>(FmMachine::Chord);p->instruments[1].fmSat=37;
 p->songLen=1;p->songBpm[0]=175;p->songTrn[0]=-7;strcpy(p->patterns[0].name,"DROP");return p;}
std::vector<uint8_t> saved(const Project& p){Sink sink;TEST_ASSERT_TRUE(saveProject(p,sink));return sink.bytes;}
void load(const std::vector<uint8_t>& bytes,Project& p){Source src(bytes);TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok),static_cast<int>(loadProject(src,p)));}
void same(const Project& expected,const Project& actual){
 const auto& a=expected.instruments[0];const auto& b=actual.instruments[0];TEST_ASSERT_EQUAL(128,b.sliceCount);TEST_ASSERT_EQUAL(128,b.chopN);
 TEST_ASSERT_EQUAL_UINT16_ARRAY(a.slices,b.slices,128);TEST_ASSERT_EQUAL_MEMORY(a.mod,b.mod,sizeof(a.mod));
 TEST_ASSERT_EQUAL_MEMORY(expected.instruments[31].mod,actual.instruments[31].mod,sizeof(a.mod));
 TEST_ASSERT_EQUAL(static_cast<uint8_t>(FmMachine::Chord),actual.instruments[1].machine);TEST_ASSERT_EQUAL(37,actual.instruments[1].fmSat);
 TEST_ASSERT_EQUAL(175,actual.songBpm[0]);TEST_ASSERT_EQUAL(-7,actual.songTrn[0]);TEST_ASSERT_EQUAL_STRING("DROP",actual.patterns[0].name);
}
}
void setUp(){}
void tearDown(){}
void test_extended_project_roundtrip_keeps_old_fm(){auto p=project(),out=project();load(saved(*p),*out);same(*p,*out);}
void test_extended_slices_win_before_or_after_legacy_slices(){
 auto p=project();auto bytes=saved(*p);auto list=chunks(bytes);auto ext=chunk(list,"SLX1");
 list.erase(std::remove_if(list.begin(),list.end(),[](const Chunk& c){return c.id=="SLX1";}),list.end());list.insert(list.begin(),ext);
 auto out=project();load(rebuild(bytes,list),*out);same(*p,*out);
}
void test_legacy_project_resets_extended_slice_tail_and_matrix(){
 auto p=project();auto bytes=saved(*p);auto list=chunks(bytes);list.erase(std::remove_if(list.begin(),list.end(),[](const Chunk& c){return c.id=="SLX1"||c.id=="MODM";}),list.end());
 auto out=project();load(rebuild(bytes,list),*out);const auto& m=out->instruments[0];TEST_ASSERT_EQUAL(32,m.sliceCount);TEST_ASSERT_EQUAL(32,m.chopN);
 for(int i=0;i<32;++i)TEST_ASSERT_EQUAL(i*500,m.slices[i]);for(int i=32;i<128;++i)TEST_ASSERT_EQUAL(0,m.slices[i]);
 for(const auto& instrument:out->instruments)for(const auto& route:instrument.mod){TEST_ASSERT_EQUAL(0,route.source);TEST_ASSERT_EQUAL(0,route.amount);}
 TEST_ASSERT_EQUAL(static_cast<uint8_t>(FmMachine::Chord),out->instruments[1].machine);TEST_ASSERT_EQUAL(37,out->instruments[1].fmSat);
}
void test_matrix_before_instrument_base_is_preserved(){
 auto p=project();auto bytes=saved(*p);auto list=chunks(bytes);auto mod=chunk(list,"MODM");list.erase(std::remove_if(list.begin(),list.end(),[](const Chunk& c){return c.id=="MODM";}),list.end());list.insert(list.begin(),mod);
 auto out=project();load(rebuild(bytes,list),*out);same(*p,*out);
}
void test_partial_slice_extension_only_overrides_its_own_instruments(){
 auto p=project();p->instruments[1].sliceCount=3;p->instruments[1].slices[0]=0;p->instruments[1].slices[1]=1000;p->instruments[1].slices[2]=2000;
 auto bytes=saved(*p);auto list=chunks(bytes);auto ext=chunk(list,"SLX1");ext.body.resize(1+kSliceExtRecSize);ext.body[0]=1;
 list.erase(std::remove_if(list.begin(),list.end(),[](const Chunk& c){return c.id=="SLX1";}),list.end());list.insert(list.begin(),ext);
 auto out=project();load(rebuild(bytes,list),*out);TEST_ASSERT_EQUAL(128,out->instruments[0].sliceCount);TEST_ASSERT_EQUAL(3,out->instruments[1].sliceCount);TEST_ASSERT_EQUAL(2000,out->instruments[1].slices[2]);
}
void test_short_matrix_chunk_is_rejected_with_valid_crc(){auto p=project();auto bytes=saved(*p);auto list=chunks(bytes);chunk(list,"MODM").body.pop_back();auto broken=rebuild(bytes,list);Source src(broken);auto out=project();TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue),static_cast<int>(loadProject(src,*out)));}
void test_legacy_perf_triplets_normalize_to_straight_ids_and_stay_stable(){
 auto p=project();auto bytes=saved(*p);auto list=chunks(bytes);auto& perf=chunk(list,"PRF2").body;
 perf[0]=perf[1]=static_cast<uint8_t>(PerfFx::Repeat);perf[2]=perf[3]=static_cast<uint8_t>(PerfFx::Gate);
 perf[kPerfButtons]=3;perf[kPerfButtons+1]=5;perf[kPerfButtons+2]=2;perf[kPerfButtons+3]=4;
 auto out=project();load(rebuild(bytes,list),*out);
 const int expected[]={4,6,3,5};for(int i=0;i<4;++i)TEST_ASSERT_EQUAL(expected[i],out->perfDepth[i]);
 auto again=project();load(saved(*out),*again);for(int i=0;i<4;++i){TEST_ASSERT_EQUAL(expected[i],again->perfDepth[i]);TEST_ASSERT_EQUAL(out->perfMap[i],again->perfMap[i]);}
}
void test_new_teensy_fm_project_is_explicitly_rejected(){
 auto p=project();auto bytes=saved(*p);auto list=chunks(bytes);
 list.insert(list.begin(),{"FMPA",{0,0}});auto file=rebuild(bytes,list);Source src(file);auto out=project();
 TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadVersion),static_cast<int>(loadProject(src,*out)));
}
int main(){UNITY_BEGIN();RUN_TEST(test_new_teensy_fm_project_is_explicitly_rejected);RUN_TEST(test_legacy_perf_triplets_normalize_to_straight_ids_and_stay_stable);RUN_TEST(test_extended_project_roundtrip_keeps_old_fm);RUN_TEST(test_extended_slices_win_before_or_after_legacy_slices);RUN_TEST(test_legacy_project_resets_extended_slice_tail_and_matrix);RUN_TEST(test_matrix_before_instrument_base_is_preserved);RUN_TEST(test_partial_slice_extension_only_overrides_its_own_instruments);RUN_TEST(test_short_matrix_chunk_is_rejected_with_valid_crc);return UNITY_END();}
