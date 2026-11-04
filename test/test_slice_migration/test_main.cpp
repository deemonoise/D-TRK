#include <unity.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include "inst_codec.h"
#include "preset_io.h"
#include "project_io.h"
#include "sample_region.h"
#include "sample_source.h"
#include "sample_store.h"
#include "slices.h"
#include "synth.h"
#include "synth_model.h"

using namespace mt;
namespace {
struct Sink : ByteSink {
  std::vector<uint8_t> data;
  bool write(const void* p,size_t n)override{const auto*b=static_cast<const uint8_t*>(p);data.insert(data.end(),b,b+n);return true;}
};
struct Source : ByteSource {
  const std::vector<uint8_t>& data;size_t pos=0;
  explicit Source(const std::vector<uint8_t>& d):data(d){}
  bool read(void*p,size_t n)override{if(pos+n>data.size())return false;memcpy(p,data.data()+pos,n);pos+=n;return true;}
  bool skip(size_t n)override{if(pos+n>data.size())return false;pos+=n;return true;}
};
static Project model, loaded;
uint32_t read32(const uint8_t*p){return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
void write32(uint8_t*p,uint32_t v){for(int i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
std::vector<uint8_t> fixture(const char* path) {
  FILE* f=std::fopen(path,"rb");TEST_ASSERT_NOT_NULL(f);
  std::fseek(f,0,SEEK_END);const long length=std::ftell(f);std::rewind(f);
  std::vector<uint8_t> data(length);TEST_ASSERT_EQUAL(data.size(),std::fread(data.data(),1,data.size(),f));
  std::fclose(f);return data;
}
void fullSlices(Instrument&m){
  TEST_ASSERT_EQUAL(128,kMaxSlices);
  instrSetType(m,InstrType::Sample);strcpy(m.sample,"LEGACY32");m.root=48;
  m.sliceMode=uint8_t(SliceMode::Fx);m.chopN=128;m.sliceCount=128;
  for(int i=0;i<128;++i)m.slices[i]=uint16_t(i*512);
}
void sameSlices(const Instrument&a,const Instrument&b){
  TEST_ASSERT_EQUAL(a.sliceMode,b.sliceMode);TEST_ASSERT_EQUAL(a.chopN,b.chopN);
  TEST_ASSERT_EQUAL(a.sliceCount,b.sliceCount);TEST_ASSERT_EQUAL_UINT16_ARRAY(a.slices,b.slices,kMaxSlices);
}
size_t chunk(const std::vector<uint8_t>&file,const char*id){
  for(size_t pos=8;pos+8<=file.size();){if(!memcmp(file.data()+pos,id,4))return pos;pos+=8+read32(file.data()+pos+4);}
  return file.size();
}
void fileCrc(std::vector<uint8_t>&file){write32(file.data()+file.size()-4,crc32(file.data(),file.size()-12));}
std::vector<uint8_t> without(const std::vector<uint8_t>&file,const char*id){
  auto out=file;const size_t at=chunk(out,id);TEST_ASSERT_TRUE(at<out.size());
  out.erase(out.begin()+at,out.begin()+at+8+read32(out.data()+at+4));fileCrc(out);return out;
}
void checkLegacy(const Instrument&m){
  TEST_ASSERT_EQUAL_STRING("LEGACY32",m.sample);TEST_ASSERT_EQUAL(48,m.root);
  TEST_ASSERT_EQUAL(32,m.sliceCount);TEST_ASSERT_EQUAL(32,m.chopN);
  for(int i=0;i<32;++i)TEST_ASSERT_EQUAL_UINT16(i*2000,m.slices[i]);
  for(int i=32;i<kMaxSlices;++i)TEST_ASSERT_EQUAL_UINT16(0,m.slices[i]);
}
}
void setUp(){model.reset();loaded.reset();}
void tearDown(){}

void test_capacity_128_last_marker_chop_and_note_mapping(){
  TEST_ASSERT_EQUAL(128,kMaxSlices);Instrument m;
  for(int i=0;i<128;++i)TEST_ASSERT_EQUAL(i,sliceInsert(m,uint16_t(i*512)));
  TEST_ASSERT_EQUAL(128,m.sliceCount);TEST_ASSERT_EQUAL(-1,sliceInsert(m,65535));
  uint32_t from,to;TEST_ASSERT_TRUE(sliceRegion(m,127,65535,from,to));
  TEST_ASSERT_EQUAL_UINT32(65024,from);TEST_ASSERT_EQUAL_UINT32(65535,to);
  m.root=0;TEST_ASSERT_EQUAL(127,sliceNote(m,127));
  m.root=60;TEST_ASSERT_EQUAL(kNoteEmpty,sliceNote(m,127));
  m.root=127;TEST_ASSERT_EQUAL(kNoteEmpty,sliceNote(m,1));
  TEST_ASSERT_EQUAL(127,sliceNote(m,0));TEST_ASSERT_EQUAL(kNoteEmpty,sliceNote(m,-1));
  TEST_ASSERT_EQUAL(kNoteEmpty,sliceNote(m,128));
  m.chopN=128;chopEqual(m);TEST_ASSERT_EQUAL(128,m.sliceCount);
}

void test_frozen_legacy_record_is_72_bytes_and_new_extension_264(){
  TEST_ASSERT_EQUAL(72,kSliceRecSize);TEST_ASSERT_EQUAL(264,kSliceExtRecSize);
  Instrument m;fullSlices(m);uint8_t legacy[74];memset(legacy,0xA5,sizeof legacy);
  packSlices(m,legacy+1);TEST_ASSERT_EQUAL(0xA5,legacy[0]);TEST_ASSERT_EQUAL(0xA5,legacy[73]);
  TEST_ASSERT_EQUAL(32,legacy[5]);TEST_ASSERT_EQUAL(32,legacy[3]);
  Instrument old;memset(old.slices,0x55,sizeof old.slices);unpackSlices(legacy+1,old);
  TEST_ASSERT_EQUAL(32,old.sliceCount);TEST_ASSERT_EQUAL(32,old.chopN);
  for(int i=32;i<128;++i)TEST_ASSERT_EQUAL_UINT16(0,old.slices[i]);
  uint8_t ext[kSliceExtRecSize+2];memset(ext,0xA5,sizeof ext);packSlicesExt(m,ext+1);
  TEST_ASSERT_EQUAL(0xA5,ext[0]);TEST_ASSERT_EQUAL(0xA5,ext[sizeof ext-1]);
  Instrument result;unpackSlicesExt(ext+1,result);sameSlices(m,result);
  ext[1+4]=255;unpackSlicesExt(ext+1,result);TEST_ASSERT_EQUAL(128,result.sliceCount);
  ext[1+8+2*70]=ext[1+8+2*69];ext[1+9+2*70]=ext[1+9+2*69];
  unpackSlicesExt(ext+1,result);TEST_ASSERT_EQUAL(70,result.sliceCount);
  for(int i=70;i<128;++i)TEST_ASSERT_EQUAL_UINT16(0,result.slices[i]);
}

void test_frozen_v9_and_project_fixtures_load_exact_legacy_markers(){
  const auto preset=fixture("test/test_slice_migration/fixtures/legacy-v9.mti");
  TEST_ASSERT_EQUAL(248,preset.size());TEST_ASSERT_EQUAL(9,preset[4]);
  Instrument m;Source pin(preset);TEST_ASSERT_EQUAL(LoadErr::Ok,loadPreset(pin,m));checkLegacy(m);
  const auto file=fixture("test/test_slice_migration/fixtures/legacy-32.mtp");
  TEST_ASSERT_EQUAL(20303,file.size());Source in(file);
  TEST_ASSERT_EQUAL(LoadErr::Ok,loadProject(in,loaded));checkLegacy(loaded.instruments[0]);
  TEST_ASSERT_EQUAL(1,loaded.sampleCount);TEST_ASSERT_EQUAL_HEX32(0x12345678,loaded.samples[0].crc);
  TEST_ASSERT_EQUAL_UINT32(441000,loaded.samples[0].frames);
}

void test_v10_preset_512_bytes_roundtrips_all_markers_and_preserves_v9_fmpa(){
  TEST_ASSERT_EQUAL(10,kPresetVersion);TEST_ASSERT_EQUAL(248,kPresetSizeV9);TEST_ASSERT_EQUAL(512,kPresetSize);
  Instrument m;fullSlices(m);m.type=InstrType::Fm;
  for(int i=0;i<kFmParams;++i)m.fmParam[i]=uint8_t(13+i*11);
  Sink out;TEST_ASSERT_TRUE(savePreset(m,out));TEST_ASSERT_EQUAL(512,out.data.size());
  Source in(out.data);Instrument restored;TEST_ASSERT_EQUAL(LoadErr::Ok,loadPreset(in,restored));
  sameSlices(m,restored);TEST_ASSERT_EQUAL_UINT8_ARRAY(m.fmParam,restored.fmParam,kFmParams);
  std::vector<uint8_t> old(out.data.begin(),out.data.begin()+kPresetSizeV9);
  old[4]=9;write32(old.data()+old.size()-4,crc32(old.data(),old.size()-4));
  Source legacy(old);TEST_ASSERT_EQUAL(LoadErr::Ok,loadPreset(legacy,restored));
  TEST_ASSERT_EQUAL(32,restored.sliceCount);TEST_ASSERT_EQUAL(32,restored.chopN);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(m.fmParam,restored.fmParam,kFmParams);
}

void test_new_project_keeps_legacy_prefix_and_full_extension_with_order_independent_precedence(){
  fullSlices(model.instruments[0]);model.instruments[1].sliceCount=1;model.instruments[1].slices[0]=12345;
  Sink out;TEST_ASSERT_TRUE(saveProject(model,out));
  const size_t old=chunk(out.data,"SLCE"),ext=chunk(out.data,"SLX1");
  TEST_ASSERT_TRUE(old<ext&&ext<out.data.size());
  TEST_ASSERT_EQUAL_UINT32(1+kInstruments*72,read32(out.data.data()+old+4));
  TEST_ASSERT_EQUAL_UINT32(1+kInstruments*264,read32(out.data.data()+ext+4));
  Source in(out.data);TEST_ASSERT_EQUAL(LoadErr::Ok,loadProject(in,loaded));
  sameSlices(model.instruments[0],loaded.instruments[0]);
  const auto legacyFile=without(out.data,"SLX1");Source legacy(legacyFile);
  TEST_ASSERT_EQUAL(LoadErr::Ok,loadProject(legacy,loaded));
  TEST_ASSERT_EQUAL(32,loaded.instruments[0].sliceCount);TEST_ASSERT_EQUAL(32,loaded.instruments[0].chopN);
  // Move a one-instrument SLX1 before legacy SLCE. Its record wins; other legacy records still load.
  auto reordered=legacyFile;
  std::vector<uint8_t> payload(1+kSliceExtRecSize);payload[0]=1;
  packSlicesExt(model.instruments[0],payload.data()+1);
  std::vector<uint8_t> inserted(8+payload.size());memcpy(inserted.data(),"SLX1",4);
  write32(inserted.data()+4,payload.size());memcpy(inserted.data()+8,payload.data(),payload.size());
  reordered.insert(reordered.begin()+8,inserted.begin(),inserted.end());fileCrc(reordered);
  Source reverse(reordered);TEST_ASSERT_EQUAL(LoadErr::Ok,loadProject(reverse,loaded));
  sameSlices(model.instruments[0],loaded.instruments[0]);
  TEST_ASSERT_EQUAL(1,loaded.instruments[1].sliceCount);TEST_ASSERT_EQUAL(12345,loaded.instruments[1].slices[0]);
}

void test_truncated_or_corrupt_extended_records_are_rejected(){
  fullSlices(model.instruments[0]);Sink out;TEST_ASSERT_TRUE(saveProject(model,out));
  const size_t ext=chunk(out.data,"SLX1");TEST_ASSERT_TRUE(ext<out.data.size());
  auto invalid=out.data;invalid[ext+8]=255;fileCrc(invalid);Source bad(invalid);
  TEST_ASSERT_EQUAL(LoadErr::BadValue,loadProject(bad,loaded));
  Sink preset;TEST_ASSERT_TRUE(savePreset(model.instruments[0],preset));
  preset.data.pop_back();Source cut(preset.data);Instrument m;
  TEST_ASSERT_EQUAL(LoadErr::Truncated,loadPreset(cut,m));
}

void test_128_metadata_does_not_expand_physical_head_cache(){
  TEST_ASSERT_EQUAL(128,kMaxSlices);TEST_ASSERT_EQUAL(40,kMaxHeads);TEST_ASSERT_EQUAL(1024,kHeadPool);
  TEST_ASSERT_EQUAL(30624,sizeof(SynthModel));
}

void test_slc_127_and_midi_note_127_address_the_last_logical_slice(){
  TEST_ASSERT_EQUAL(128,kMaxSlices);
  struct Samples:SampleSource{
    std::vector<int16_t> pcm=std::vector<int16_t>(65535,10000);
    bool find(const char*,SampleData&out)const override{out=SampleData();out.frames=pcm.size();out.rate=44100;out.whole=pcm.data();return true;}
  } source;
  model.tracks[0].out=TrackOut::Int;model.tracks[0].instr=0;
  auto&m=model.instruments[0];fullSlices(m);m.root=60;m.attack=0;
  auto*synth=new Synth(model);synth->setBank(&source);
  const uint8_t effect[]={0xF5,uint8_t(Fx::SLC),127},note[]={0x90,60,127};
  TEST_ASSERT_TRUE(synth->event(0,0,effect,3));TEST_ASSERT_TRUE(synth->event(0,0,note,3));
  int16_t left[Synth::kBlock],right[Synth::kBlock];synth->render(left,right);
  int voice=synth->trackVoice(0);TEST_ASSERT_TRUE(voice>=0);
  TEST_ASSERT_EQUAL_UINT32(65024,synth->voice(voice).from);TEST_ASSERT_EQUAL_UINT32(65535,synth->voice(voice).to);
  delete synth;m.sliceMode=uint8_t(SliceMode::Note);m.root=0;
  synth=new Synth(model);synth->setBank(&source);
  const uint8_t high[]={0x90,127,127};TEST_ASSERT_TRUE(synth->event(0,0,high,3));
  synth->render(left,right);
  voice=synth->trackVoice(0);TEST_ASSERT_TRUE(voice>=0);
  TEST_ASSERT_EQUAL_UINT32(65024,synth->voice(voice).from);
  delete synth;
}

int main(){UNITY_BEGIN();
 RUN_TEST(test_capacity_128_last_marker_chop_and_note_mapping);
 RUN_TEST(test_frozen_legacy_record_is_72_bytes_and_new_extension_264);
 RUN_TEST(test_frozen_v9_and_project_fixtures_load_exact_legacy_markers);
 RUN_TEST(test_v10_preset_512_bytes_roundtrips_all_markers_and_preserves_v9_fmpa);
 RUN_TEST(test_new_project_keeps_legacy_prefix_and_full_extension_with_order_independent_precedence);
 RUN_TEST(test_truncated_or_corrupt_extended_records_are_rejected);
 RUN_TEST(test_128_metadata_does_not_expand_physical_head_cache);
 RUN_TEST(test_slc_127_and_midi_note_127_address_the_last_logical_slice);
 return UNITY_END();}
