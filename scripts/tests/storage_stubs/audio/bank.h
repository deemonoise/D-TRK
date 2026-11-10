#pragma once
#include <vector>
#include "sample_bank.h"
#include "model.h"
namespace audio {
struct TestFlash : mt::BankFlash {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(65536, 0xff);
  uint32_t size() const override { return bytes.size(); }
  bool read(uint32_t off, void* dst, uint32_t n) override { memcpy(dst, bytes.data()+off,n);return true; }
  bool erase(uint32_t off,uint32_t n) override {memset(bytes.data()+off,0xff,n);return true;}
  bool write(uint32_t off,const void* src,uint32_t n) override {memcpy(bytes.data()+off,src,n);return true;}
  const uint8_t* mapped() const override {return bytes.data();}
};
enum class BankResult : uint8_t {Ok, Busy, WriteFail};
extern BankResult assetResult;
mt::SampleBank& bank();
bool bankMounted();
const char* bankResultText(BankResult);
using BankProgressFn = void (*)(uint32_t,uint32_t,void*);
struct ImportOut {uint32_t crc,frames;uint8_t root;};
BankResult exportWav(int,const char*);
BankResult exportWt(const char*,const mt::Project&,const char*);
BankResult migrateProject(mt::Project&,int*);
bool legacySource(uint32_t,uint32_t,char[17],char[17]);
BankResult importToCache(const char*,const mt::Project&,ImportOut&,const uint32_t*,BankProgressFn,void*);
BankResult importWtToCache(const char*,const mt::Project&,uint32_t&,const uint32_t*,BankProgressFn,void*);
}
