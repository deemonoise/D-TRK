#include <Arduino.h>
#include <cassert>
#include <string>
#include <vector>
#include "hw/sdcard.h"
#include "storage/wt_source_file.h"
#include "wav.h"
#include "wt_make.h"
using storage::WtSourceSaveResult;
namespace {
constexpr const char* destination="/wavetables/TABLE.wav";
constexpr const char* temporary="/wavetables/TABLE.wav.tmp";
constexpr const char* backup="/wavetables/TABLE.wav.bak";
int imports=0;
bool accepts(const char*,void*){++imports;return true;} // models canonical cache-hit path: never reads payload
bool rejects(const char*,void*){++imports;return false;}
std::vector<int16_t> table(){std::vector<int16_t> out(mt::kWtSrcSamples);for(size_t i=0;i<out.size();++i)out[i]=static_cast<int16_t>((int(i%256)-128)*200);return out;}
void oldFile(){fs::files[destination]={1,2,3,4};}
void assertOld(){assert((fs::files.at(destination)==fs::Bytes{1,2,3,4}));}
void corruptPayload(size_t offset=mt::kWtWavBytes-20){oldFile();auto source=table();fs::corruptOnClose=temporary;fs::corruptOffset=offset;
 auto result=storage::saveWtSourceFile(destination,source.data(),accepts,nullptr);
 assert(result==WtSourceSaveResult::ReadbackFail);assert(imports==0);assertOld();assert(!fs::files.count(backup));assert(!fs::files.count(temporary));
}
void good(){oldFile();auto source=table();assert(storage::saveWtSourceFile(destination,source.data(),accepts,nullptr)==WtSourceSaveResult::Ok);assert(imports==1&&fs::files.at(destination).size()==mt::kWtWavBytes);assert(!fs::files.count(backup));}
void rejected(){oldFile();auto source=table();assert(storage::saveWtSourceFile(destination,source.data(),rejects,nullptr)==WtSourceSaveResult::ImportFail);assertOld();assert(imports==1&&!fs::files.count(backup));}
void renameFailure(){oldFile();auto source=table();fs::failures.insert(std::string("rename:")+temporary+":"+destination);assert(storage::saveWtSourceFile(destination,source.data(),accepts,nullptr)==WtSourceSaveResult::WriteFail);assertOld();assert(imports==0);}
void rollbackFailure(){oldFile();auto source=table();fs::failures.insert(std::string("rename:")+backup+":"+destination);assert(storage::saveWtSourceFile(destination,source.data(),rejects,nullptr)==WtSourceSaveResult::RecoveryNeeded);assert((fs::files.at(backup)==fs::Bytes{1,2,3,4}));}
void existingBackup(){oldFile();auto source=table();fs::files[backup]={9};assert(storage::saveWtSourceFile(destination,source.data(),accepts,nullptr)==WtSourceSaveResult::RecoveryNeeded);assertOld();assert((fs::files.at(backup)==fs::Bytes{9}));assert(imports==0);}
}
int main(int argc,char** argv){assert(argc==2);std::string c=argv[1];if(c=="payload")corruptPayload();else if(c=="metadata")corruptPayload(24);else if(c=="good")good();else if(c=="reject")rejected();else if(c=="rename")renameFailure();else if(c=="rollback")rollbackFailure();else if(c=="backup")existingBackup();else assert(false);}
