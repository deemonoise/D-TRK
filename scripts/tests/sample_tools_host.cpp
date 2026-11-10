#include <Arduino.h>
#include <cassert>
#include <cstring>
#include <memory>
#include <vector>
#include "audio/sample_tools.h"
#include "audio/bank.h"
#include "hw/sdcard.h"
#include "sample_set.h"
#include "storage/storage.h"
#include "wav.h"
namespace {
struct MemoryFlash final : mt::BankFlash {
 std::vector<uint8_t> bytes = std::vector<uint8_t>(1024*1024,0xff);
 uint32_t size() const override{return bytes.size();}
 bool read(uint32_t at,void* dst,uint32_t n) override{if(at>bytes.size()||n>bytes.size()-at)return false;memcpy(dst,bytes.data()+at,n);return true;}
 bool erase(uint32_t at,uint32_t n) override{if(at>bytes.size()||n>bytes.size()-at)return false;memset(bytes.data()+at,0xff,n);return true;}
 bool write(uint32_t at,const void* src,uint32_t n) override{if(at>bytes.size()||n>bytes.size()-at)return false;memcpy(bytes.data()+at,src,n);return true;}
 const uint8_t* mapped() const override{return bytes.data();}
} flash;
mt::SampleBank sampleBank(flash);
bool failImport = false;
}
namespace audio {
mt::SampleBank& bank(){return sampleBank;}
bool bankMounted(){return true;}
BankResult importToCache(const char* path,const mt::Project&,ImportOut& output,const uint32_t*,BankProgressFn progress,void* ctx){
 if(failImport)return BankResult::WriteFail;
 fs::File file=hw::sdFs().open(path);if(!file)return BankResult::ReadFail;
 mt::WavInfo info;hw::FileSource src(file);if(mt::wavParse(src,info)!=mt::WavErr::Ok)return BankResult::Unsupported;
 output.crc=info.crc;output.frames=info.frames();output.root=info.root;
 if(progress)progress(0,info.frames(),ctx);
 char key[17];mt::sampleKey(info.crc,key);
 if(sampleBank.find(key)<0){
  if(!sampleBank.begin(key,info.frames(),info.rate,info.root))return BankResult::Full;
  const auto& bytes=fs::files.at(path);
  if(!sampleBank.write(reinterpret_cast<const int16_t*>(bytes.data()+info.dataOffset),info.frames())||!sampleBank.commit())return BankResult::WriteFail;
 }
 if(progress)progress(info.frames(),info.frames(),ctx);
 return BankResult::Ok;
}
}
namespace storage {bool stopEngine(){return true;}}
struct Fixture {
 std::unique_ptr<mt::Project> project=std::make_unique<mt::Project>();
 mt::SampleExtractTarget target;
 std::vector<int16_t> pcm;
 Fixture(uint32_t frames=2000,uint32_t rate=16000,bool pulses=false){
  assert(sampleBank.mount());project->reset();strcpy(project->name,"song");
  pcm.resize(frames);for(uint32_t i=0;i<frames;++i)pcm[i]=static_cast<int16_t>(int(i%200)*200-20000);
  if(pulses){std::fill(pcm.begin(),pcm.end(),0);for(uint32_t at=0;at<frames;at+=rate/2)for(uint32_t i=0;i<64&&at+i<frames;++i)pcm[at+i]=static_cast<int16_t>(30000-int(i)*450);}
  else if(frames>101){pcm[100]=-32768;pcm[101]=32767;}
  uint32_t crc=mt::sampleCrc(pcm.data(),frames);char key[17];mt::sampleKey(crc,key);
  assert(sampleBank.begin(key,frames,rate,47));assert(sampleBank.write(pcm.data(),frames));assert(sampleBank.commit());
  project->sampleCount=1;strcpy(project->samples[0].name,"SOURCE");project->samples[0].crc=crc;project->samples[0].frames=frames;
  auto& instrument=project->instruments[0];instrument.type=mt::InstrType::Sample;strcpy(instrument.sample,"SOURCE");instrument.root=21;instrument.vol=81;instrument.reverse=true;
  target.instrument=0;target.index=0;target.source=project->samples[0];target.from=100;target.to=frames-100;
 }
};
std::vector<int16_t> created(const char* name,uint32_t* rate=nullptr){
 char path[80];snprintf(path,sizeof(path),"/projects/song/%s.wav",name);
 auto file=hw::sdFs().open(path);mt::WavInfo info;hw::FileSource source(file);assert(mt::wavParse(source,info)==mt::WavErr::Ok);
 if(rate)*rate=info.rate;const auto& bytes=fs::files.at(path);std::vector<int16_t> output(info.frames());memcpy(output.data(),bytes.data()+info.dataOffset,output.size()*2);return output;
}
bool cancel(uint32_t,uint32_t,void*){return false;}
void exact(){Fixture f;auto before=*f.project;auto result=audio::extractSample(*f.project,f.target,"COPY",false);assert(result==audio::SampleToolResult::Ok);uint32_t rate;auto pcm=created("COPY",&rate);assert(rate==16000&&pcm.size()==1800);assert(!memcmp(pcm.data(),f.pcm.data()+100,pcm.size()*2));assert(f.project->sampleCount==2&&!strcmp(f.project->instruments[0].sample,"COPY"));assert(f.project->instruments[0].root==21&&f.project->instruments[0].vol==81&&f.project->instruments[0].reverse);assert(!memcmp(&f.project->samples[0],&before.samples[0],sizeof(mt::ProjSample)));}
void normalize(){Fixture f;auto result=audio::extractSample(*f.project,f.target,"NORM",true);assert(result==audio::SampleToolResult::Ok);auto pcm=created("NORM");int peak=0;for(auto x:pcm){int a=x<0?-int(x):x;if(a>peak)peak=a;}assert(peak==29204);}
void stretching(){Fixture f;f.target.ratioQ16=131072;assert(audio::stretchSample(*f.project,f.target,"LONG")==audio::SampleToolResult::Ok);uint32_t rate;auto pcm=created("LONG",&rate);assert(rate==16000&&pcm.size()==3600);}
void failure(const std::string& op,bool importing=false){Fixture f;auto before=*f.project;failImport=importing;if(!op.empty())fs::failures.insert(op);assert(audio::extractSample(*f.project,f.target,"BAD",false)!=audio::SampleToolResult::Ok);assert(!memcmp(&before,f.project.get(),sizeof(before)));assert(!fs::files.count("/projects/song/BAD.wav"));assert(!fs::files.count("/projects/song/BAD.wav.tmp"));}
void cancelled(){Fixture f;auto before=*f.project;assert(audio::extractSample(*f.project,f.target,"BAD",false,cancel)==audio::SampleToolResult::Cancelled);assert(!memcmp(&before,f.project.get(),sizeof(before)));assert(!fs::files.count("/projects/song/BAD.wav"));assert(!fs::files.count("/projects/song/BAD.wav.tmp"));}
void collision(){Fixture f;fs::files["/projects/song/COPY.wav"]={1,2};assert(audio::extractSample(*f.project,f.target,"COPY",false)==audio::SampleToolResult::NameTaken);assert((fs::files.at("/projects/song/COPY.wav")==fs::Bytes{1,2}));}
bool cancelImport(uint32_t done,uint32_t,void*){return done!=0;}
bool cancelPublish(uint32_t,uint32_t total,void*){return total!=1;}
void cancelledLate(bool importing){Fixture f;auto before=*f.project;assert(audio::extractSample(*f.project,f.target,"BAD",false,importing?cancelImport:cancelPublish)==audio::SampleToolResult::Cancelled);assert(!memcmp(&before,f.project.get(),sizeof(before)));assert(!fs::files.count("/projects/song/BAD.wav"));assert(!fs::files.count("/projects/song/BAD.wav.tmp"));}
void stretchIdentity(){Fixture f;f.target.ratioQ16=65536;assert(audio::stretchSample(*f.project,f.target,"SAME")==audio::SampleToolResult::Ok);auto pcm=created("SAME");assert(pcm.size()==1800&&!memcmp(pcm.data(),f.pcm.data()+100,pcm.size()*2));}
void badSource(){Fixture f;strcpy(f.project->instruments[0].sample,"OTHER");auto before=*f.project;assert(audio::extractSample(*f.project,f.target,"BAD",false)==audio::SampleToolResult::BadSource);assert(!memcmp(&before,f.project.get(),sizeof(before)));assert(fs::files.empty());}
void tempoCancelled(){Fixture f;mt::TempoEstimate output;output.bpm10=1234;auto before=*f.project;assert(audio::detectSampleTempo(*f.project,f.target,output,cancel)==audio::SampleToolResult::Cancelled);assert(output.bpm10==0&&!memcmp(&before,f.project.get(),sizeof(before)));}
void corruptPayload(){Fixture f;auto before=*f.project;fs::corruptOnClose="/projects/song/BAD.wav.tmp";fs::corruptOffset=mt::kWavHeaderBytes+50;assert(audio::extractSample(*f.project,f.target,"BAD",false)==audio::SampleToolResult::ReadFail);assert(!memcmp(&before,f.project.get(),sizeof(before)));assert(!fs::files.count("/projects/song/BAD.wav"));assert(!fs::files.count("/projects/song/BAD.wav.tmp"));}
void tempoPositive(){Fixture f(128000,16000,true);auto before=*f.project;mt::TempoEstimate output;assert(audio::detectSampleTempo(*f.project,f.target,output)==audio::SampleToolResult::Ok);assert(output.bpm10>=1190&&output.bpm10<=1210&&output.confidence>=500);assert(output.halfBpm10&&output.doubleBpm10);assert(!memcmp(&before,f.project.get(),sizeof(before)));}
void tempoShort(){Fixture f;mt::TempoEstimate output;assert(audio::detectSampleTempo(*f.project,f.target,output)==audio::SampleToolResult::Ok);assert(output.bpm10==0);}
int main(int argc,char** argv){assert(argc==2);std::string c=argv[1];if(c=="exact")exact();else if(c=="normalize")normalize();else if(c=="stretch")stretching();else if(c=="write")failure("write:/projects/song/BAD.wav.tmp");else if(c=="rename")failure("rename:/projects/song/BAD.wav.tmp:/projects/song/BAD.wav");else if(c=="cache")failure("",true);else if(c=="cancel")cancelled();else if(c=="collision")collision();else if(c=="cancel_import")cancelledLate(true);else if(c=="cancel_publish")cancelledLate(false);else if(c=="identity")stretchIdentity();else if(c=="bad_source")badSource();else if(c=="tempo_cancel")tempoCancelled();else if(c=="corrupt")corruptPayload();else if(c=="tempo_positive")tempoPositive();else if(c=="tempo_short")tempoShort();else assert(false);}
