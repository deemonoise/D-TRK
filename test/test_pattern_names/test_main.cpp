#include <cstring>
#include <cstdio>
#include <vector>
#include <unity.h>
#include "project_io.h"
#include "undo.h"
#include "dice.h"
#include "templates.h"
using namespace mt;
struct Sink : ByteSink {
  std::vector<uint8_t> bytes;
  bool write(const void* d, size_t n) override {
    const auto* b = static_cast<const uint8_t*>(d); bytes.insert(bytes.end(), b, b+n); return true;
  }
};
struct Source : ByteSource {
  const std::vector<uint8_t>& bytes; size_t pos=0;
  explicit Source(const std::vector<uint8_t>& b): bytes(b) {}
  bool read(void* d, size_t n) override { if(pos+n>bytes.size()) return false; memcpy(d,bytes.data()+pos,n); pos+=n; return true; }
  bool skip(size_t n) override { if(pos+n>bytes.size()) return false; pos+=n; return true; }
};
static Project p;
static void u32(std::vector<uint8_t>& b,uint32_t v) { for(int i=0;i<4;++i)b.push_back(v>>(8*i)); }
static void chunk(std::vector<uint8_t>& b,const char* id,const std::vector<uint8_t>& data) {
  b.insert(b.end(),id,id+4); u32(b,data.size()); b.insert(b.end(),data.begin(),data.end());
}
static std::vector<uint8_t> fileWithNames(size_t size=16*kPatterns) {
  Sink out; p.reset(); saveProject(p,out); out.bytes.resize(out.bytes.size()-12);
  std::vector<uint8_t> names(size,0); if(size>=5)memcpy(names.data(),"INTRO",5);
  chunk(out.bytes,"PNAM",names); const uint32_t crc=crc32(out.bytes.data(),out.bytes.size());
  std::vector<uint8_t> c;u32(c,crc);chunk(out.bytes,"CRC ",c);return out.bytes;
}
static bool hasChunk(const std::vector<uint8_t>& b,const char* id) {
  for(size_t pos=8;pos+8<=b.size();) {
    if(!memcmp(b.data()+pos,id,4))return true;
    uint32_t n=0;for(int i=0;i<4;++i)n|=uint32_t(b[pos+4+i])<<(8*i);
    pos+=8+n;
  }return false;
}
void setUp(){p.reset();}
void tearDown(){}
void test_loaded_pattern_name_survives_saving_an_empty_pattern() {
  auto bytes=fileWithNames();Source in(bytes);TEST_ASSERT_EQUAL(int(LoadErr::Ok),int(loadProject(in,p)));
  Sink out;TEST_ASSERT_TRUE(saveProject(p,out));TEST_ASSERT_TRUE(hasChunk(out.bytes,"PNAM"));
}
void test_short_pattern_names_chunk_is_rejected() {
  auto bytes=fileWithNames(15);Source in(bytes);TEST_ASSERT_EQUAL(int(LoadErr::BadValue),int(loadProject(in,p)));
}

static void finish(std::vector<uint8_t>& b) {
  const uint32_t c=crc32(b.data(),b.size());std::vector<uint8_t> data;u32(data,c);chunk(b,"CRC ",data);
}
void test_all_names_roundtrip_without_changing_pattern_music() {
  for(int i=0;i<kPatterns;++i) {
    snprintf(p.patterns[i].name,sizeof(p.patterns[i].name),"Pattern-%02d-Abcde",i+1);
    p.patterns[i].steps[1][3].note=uint8_t(40+i);
  }
  strcpy(p.patterns[15].name,"ABCDEFGHIJKLMNOP");
  Sink out;TEST_ASSERT_TRUE(saveProject(p,out));p.reset();Source in(out.bytes);
  TEST_ASSERT_EQUAL(int(LoadErr::Ok),int(loadProject(in,p)));
  for(int i=0;i<kPatterns;++i) {
    char name[17];snprintf(name,sizeof(name),"Pattern-%02d-Abcde",i+1);
    TEST_ASSERT_EQUAL_STRING(i==15?"ABCDEFGHIJKLMNOP":name,p.patterns[i].name);
    TEST_ASSERT_EQUAL(40+i,p.patterns[i].steps[1][3].note);
    TEST_ASSERT_EQUAL(0,p.patterns[i].name[16]);
  }
}
void test_legacy_file_has_empty_names_even_when_loading_over_named_project() {
  Sink out;TEST_ASSERT_TRUE(saveProject(p,out));TEST_ASSERT_FALSE(hasChunk(out.bytes,"PNAM"));
  strcpy(p.patterns[2].name,"old in RAM");Source in(out.bytes);
  TEST_ASSERT_EQUAL(int(LoadErr::Ok),int(loadProject(in,p)));
  for(const auto& pt:p.patterns)TEST_ASSERT_EQUAL_STRING("",pt.name);
}
void test_names_before_patn_are_preserved_and_duplicate_names_replace_table() {
  auto bytes=fileWithNames();bytes.resize(bytes.size()-12);
  // A legal header-only PATN follows the name table and resets musical data.
  chunk(bytes,"PATN",{0,16,uint8_t(Resolution::Sixteenth),50});finish(bytes);
  Source in(bytes);TEST_ASSERT_EQUAL(int(LoadErr::Ok),int(loadProject(in,p)));
  TEST_ASSERT_EQUAL_STRING("INTRO",p.patterns[0].name);
  bytes.resize(bytes.size()-12);std::vector<uint8_t> names(16*kPatterns+4,0);
  memcpy(names.data()+16*15,"LAST",4);chunk(bytes,"PNAM",names);finish(bytes);
  Source again(bytes);TEST_ASSERT_EQUAL(int(LoadErr::Ok),int(loadProject(again,p)));
  TEST_ASSERT_EQUAL_STRING("",p.patterns[0].name);TEST_ASSERT_EQUAL_STRING("LAST",p.patterns[15].name);
}
void test_nonterminated_and_nonprintable_names_load_safely() {
  auto bytes=fileWithNames();bytes.resize(bytes.size()-12);std::vector<uint8_t> names(16*kPatterns,'A');
  names[0]=1;names[1]=255;chunk(bytes,"PNAM",names);finish(bytes);
  Source in(bytes);TEST_ASSERT_EQUAL(int(LoadErr::Ok),int(loadProject(in,p)));
  TEST_ASSERT_EQUAL_STRING("??AAAAAAAAAAAAAA",p.patterns[0].name);
  TEST_ASSERT_EQUAL_STRING("AAAAAAAAAAAAAAAA",p.patterns[15].name);
}
void test_pattern_copy_clear_and_undo_include_name() {
  static Undo::Entry entries[Undo::kDepth];Undo undo(entries);
  strcpy(p.patterns[0].name,"INTRO");p.patterns[0].steps[0][2].note=60;
  p.patterns[1]=p.patterns[0];TEST_ASSERT_EQUAL_STRING("INTRO",p.patterns[1].name);
  undo.push(1,p.patterns[1]);p.patterns[1].clear();TEST_ASSERT_EQUAL_STRING("",p.patterns[1].name);
  uint8_t pattern=0;TEST_ASSERT_TRUE(undo.pop(pattern,p.patterns[1]));TEST_ASSERT_EQUAL(1,pattern);
  TEST_ASSERT_EQUAL_STRING("INTRO",p.patterns[1].name);TEST_ASSERT_EQUAL(60,p.patterns[1].steps[0][2].note);
}
static void handwrittenNames() {
  templateBuild(0,p);
  for(int i=0;i<kPatterns;++i)snprintf(p.patterns[i].name,sizeof(p.patterns[i].name),"Section-%02d",i+1);
}
void test_dice_names_each_form_and_undo_restores_original_labels() {
  static DiceSnap snap;
  const char* labels[][6]={{"MAIN","FILL","BREAK","DROP"},{"INTRO","MAIN","VAR","OUTRO"},{"INTRO","MAIN","BREAK","BUILD","DROP","OUTRO"}};
  for(int f=0;f<3;++f) {
    handwrittenNames();DiceOpts o;o.form=static_cast<DiceForm>(f);
    diceDraft(p,o,7,777,snap);
    TEST_ASSERT_EQUAL_STRING("Section-01",p.patterns[0].name);
    diceApply(p,snap);
    const int count=dicePatternCount(o);
    for(int i=0;i<count;++i)TEST_ASSERT_EQUAL_STRING(labels[f][i],p.patterns[i].name);
    for(int i=count;i<kPatterns;++i){char name[17];snprintf(name,sizeof(name),"Section-%02d",i+1);TEST_ASSERT_EQUAL_STRING(name,p.patterns[i].name);}
    TEST_ASSERT_TRUE(diceUndo(p,snap));
    for(int i=0;i<kPatterns;++i){char name[17];snprintf(name,sizeof(name),"Section-%02d",i+1);TEST_ASSERT_EQUAL_STRING(name,p.patterns[i].name);}
  }
}
void test_partial_and_nonpattern_dice_rolls_preserve_names() {
  static DiceSnap snap;
  for(int mode=0;mode<5;++mode) {
    handwrittenNames();DiceOpts o;o.form=DiceForm::Arrange;
    if(mode==0)o.targetMask=2;
    if(mode==1)o.change=DiceChange::Subtle;
    if(mode==2)o.change=DiceChange::Normal;
    if(mode==3)o.layers=kDiceSounds;
    if(mode==4)o.layers=kDiceTempo;
    diceRoll(p,o,7,777,snap);
    for(int i=0;i<kPatterns;++i){char name[17];snprintf(name,sizeof(name),"Section-%02d",i+1);TEST_ASSERT_EQUAL_STRING(name,p.patterns[i].name);}
  }
}
void test_dice_noop_keeps_correct_name_undo() {
  static DiceSnap snap;handwrittenNames();DiceOpts o;o.form=DiceForm::Arrange;
  diceRoll(p,o,15,777,snap);TEST_ASSERT_EQUAL_STRING("BUILD",p.patterns[3].name);
  o.lockMask=63;
  auto result=diceRoll(p,o,7,778,snap);TEST_ASSERT_EQUAL(0,result.written);
  TEST_ASSERT_EQUAL_STRING("BUILD",p.patterns[3].name);
  TEST_ASSERT_TRUE(diceUndo(p,snap));
  for(int i=0;i<kPatterns;++i){char name[17];snprintf(name,sizeof(name),"Section-%02d",i+1);TEST_ASSERT_EQUAL_STRING(name,p.patterns[i].name);}
}
int main(){UNITY_BEGIN();
 RUN_TEST(test_loaded_pattern_name_survives_saving_an_empty_pattern);
 RUN_TEST(test_short_pattern_names_chunk_is_rejected);
 RUN_TEST(test_all_names_roundtrip_without_changing_pattern_music);
 RUN_TEST(test_legacy_file_has_empty_names_even_when_loading_over_named_project);
 RUN_TEST(test_names_before_patn_are_preserved_and_duplicate_names_replace_table);
 RUN_TEST(test_nonterminated_and_nonprintable_names_load_safely);
 RUN_TEST(test_pattern_copy_clear_and_undo_include_name);
 RUN_TEST(test_dice_names_each_form_and_undo_restores_original_labels);
 RUN_TEST(test_partial_and_nonpattern_dice_rolls_preserve_names);
 RUN_TEST(test_dice_noop_keeps_correct_name_undo);
 return UNITY_END();}
