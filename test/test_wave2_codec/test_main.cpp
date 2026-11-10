#include <unity.h>
#include <type_traits>
#include <utility>
#include <vector>
#include <cstring>
#include "preset_io.h"
using namespace mt;
void setUp() {}
void tearDown() {}
template<class T, class = void> struct HasMatrix : std::false_type {};
template<class T> struct HasMatrix<T, std::void_t<decltype(std::declval<T>().mod)>> : std::true_type {};
struct Sink : ByteSink { std::vector<uint8_t> bytes; bool write(const void* p,size_t n) override { const auto* b=static_cast<const uint8_t*>(p);bytes.insert(bytes.end(),b,b+n);return true;} };
struct Source : ByteSource { const std::vector<uint8_t>& bytes;size_t pos=0;explicit Source(const std::vector<uint8_t>& b):bytes(b){} bool read(void* p,size_t n)override {if(n>bytes.size()-pos)return false;memcpy(p,bytes.data()+pos,n);pos+=n;return true;} bool skip(size_t n)override {if(n>bytes.size()-pos)return false;pos+=n;return true;} };
void test_extended_slice_roundtrip() {
 TEST_ASSERT_EQUAL(128,kMaxSlices);
 if(kMaxSlices<128)return;
 Instrument m;m.type=InstrType::Sample;m.sliceCount=128;m.chopN=128;
 for(int i=0;i<128;++i)m.slices[i]=i*500;
 Sink sink;TEST_ASSERT_TRUE(savePreset(m,sink));Instrument out;Source source(sink.bytes);
 TEST_ASSERT_EQUAL(LoadErr::Ok,loadPreset(source,out));TEST_ASSERT_EQUAL(128,out.sliceCount);TEST_ASSERT_EQUAL(128,out.chopN);TEST_ASSERT_EQUAL(63500,out.slices[127]);
}
void test_classic_fm_presets_have_explicit_extension_family() {
 Instrument m;m.type=InstrType::Fm;m.machine=static_cast<uint8_t>(FmMachine::Chord);m.macro[0]=97;
 Sink sink;TEST_ASSERT_TRUE(savePreset(m,sink));TEST_ASSERT_EQUAL(12,sink.bytes[4]);TEST_ASSERT_EQUAL('E',sink.bytes[6]);TEST_ASSERT_EQUAL('8',sink.bytes[7]);
 Instrument out;Source source(sink.bytes);TEST_ASSERT_EQUAL(LoadErr::Ok,loadPreset(source,out));TEST_ASSERT_EQUAL(m.machine,out.machine);TEST_ASSERT_EQUAL(97,out.macro[0]);
}
template<class T> void matrixPresetRoundtrip() {
 if constexpr(HasMatrix<T>::value){
  T m;m.mod[0].source=1;m.mod[0].dest=7;m.mod[0].amount=-63;m.mod[0].polarity=2;m.mod[0].via=7;
  Sink sink;TEST_ASSERT_TRUE(savePreset(m,sink));T out;Source source(sink.bytes);TEST_ASSERT_EQUAL(LoadErr::Ok,loadPreset(source,out));
  TEST_ASSERT_EQUAL_MEMORY(m.mod,out.mod,sizeof(m.mod));
 }else{TEST_FAIL_MESSAGE("Instrument has no modulation matrix");}
}
void test_matrix_preset_roundtrip(){matrixPresetRoundtrip<Instrument>();}
static void crcTail(std::vector<uint8_t>& bytes) {
 const auto c=crc32(bytes.data(),bytes.size()-4);
 for(int i=0;i<4;++i)bytes[bytes.size()-4+i]=static_cast<uint8_t>(c>>(8*i));
}
void test_v8_load_retains_classic_fm_and_clears_extensions() {
 Instrument m; m.type=InstrType::Fm; m.machine=static_cast<uint8_t>(FmMachine::Chord);
 m.fmSat=113; m.macro[0]=97; m.sliceCount=2; m.slices[1]=49000;
 Sink sink; TEST_ASSERT_TRUE(savePreset(m,sink));
 sink.bytes.resize(kPresetSizeV8);sink.bytes[4]=8;sink.bytes[6]=sink.bytes[7]=0;crcTail(sink.bytes);
 Instrument out;out.mod[0].source=7;out.sliceCount=128;out.slices[127]=65000;
 Source source(sink.bytes);TEST_ASSERT_EQUAL(LoadErr::Ok,loadPreset(source,out));
 TEST_ASSERT_EQUAL(m.machine,out.machine);TEST_ASSERT_EQUAL(97,out.macro[0]);TEST_ASSERT_EQUAL(113,out.fmSat);
 TEST_ASSERT_EQUAL(2,out.sliceCount);TEST_ASSERT_EQUAL(49000,out.slices[1]);TEST_ASSERT_EQUAL(0,out.slices[127]);
 for(const auto& route:out.mod)TEST_ASSERT_EQUAL(0,route.source);
}
void test_other_fm_family_and_invalid_extensions_leave_destination() {
 Instrument m;Sink sink;TEST_ASSERT_TRUE(savePreset(m,sink));
 for(int version:{9,10,11,13}){
  auto b=sink.bytes;b[4]=version;crcTail(b);Instrument out;strcpy(out.name,"KEEP");out.fmSat=91;
  Source source(b);TEST_ASSERT_EQUAL(LoadErr::BadVersion,loadPreset(source,out));
  TEST_ASSERT_EQUAL_STRING("KEEP",out.name);TEST_ASSERT_EQUAL(91,out.fmSat);
 }
 auto b=sink.bytes;b[6]=0;crcTail(b);Instrument out;strcpy(out.name,"KEEP");Source source(b);
 TEST_ASSERT_EQUAL(LoadErr::BadVersion,loadPreset(source,out));TEST_ASSERT_EQUAL_STRING("KEEP",out.name);
 b=sink.bytes;b[kPresetSizeV8+10]^=0x80;Source corrupt(b);
 TEST_ASSERT_EQUAL(LoadErr::BadCrc,loadPreset(corrupt,out));TEST_ASSERT_EQUAL_STRING("KEEP",out.name);
 b=sink.bytes;b.resize(kPresetSize-1);Source truncated(b);
 TEST_ASSERT_EQUAL(LoadErr::Truncated,loadPreset(truncated,out));TEST_ASSERT_EQUAL_STRING("KEEP",out.name);
}
void test_route_record_rejects_invalid_values() {
 Instrument out;uint8_t b[kModRecSize];memset(b,255,sizeof(b));unpackMod(b,sizeof(b),out);
 for(const auto& r:out.mod){TEST_ASSERT_EQUAL(0,r.source);TEST_ASSERT_EQUAL(0,r.dest);TEST_ASSERT_EQUAL(-1,r.amount);TEST_ASSERT_EQUAL(0,r.polarity);TEST_ASSERT_EQUAL(0,r.via);}
}
void test_loaded_routes_disable_targets_missing_from_current_engine() {
 Instrument m;m.type=InstrType::Sample;
 m.mod[0]={7,static_cast<uint8_t>(LfoDest::Sub),63,0,0};
 m.mod[1]={7,static_cast<uint8_t>(LfoDest::Fold),63,0,0};
 m.mod[2]={7,static_cast<uint8_t>(LfoDest::Sat),63,0,0};
 m.mod[3]={7,static_cast<uint8_t>(LfoDest::Cutoff),63,0,0};
 Sink sink;TEST_ASSERT_TRUE(savePreset(m,sink));Source source(sink.bytes);Instrument out;
 TEST_ASSERT_EQUAL(LoadErr::Ok,loadPreset(source,out));
 for(int i=0;i<3;++i)TEST_ASSERT_EQUAL(0,out.mod[i].source);
 TEST_ASSERT_EQUAL(7,out.mod[3].source);TEST_ASSERT_EQUAL(63,out.mod[3].amount);
}
int main(){UNITY_BEGIN();RUN_TEST(test_loaded_routes_disable_targets_missing_from_current_engine);RUN_TEST(test_extended_slice_roundtrip);RUN_TEST(test_classic_fm_presets_have_explicit_extension_family);RUN_TEST(test_matrix_preset_roundtrip);RUN_TEST(test_v8_load_retains_classic_fm_and_clears_extensions);RUN_TEST(test_other_fm_family_and_invalid_extensions_leave_destination);RUN_TEST(test_route_record_rejects_invalid_values);return UNITY_END();}
