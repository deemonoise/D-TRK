#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "dice_library.h"

size_t strlcpy(char* d,const char* s,size_t n) {
  size_t len=strlen(s);if(n){size_t m=std::min(n-1,len);memcpy(d,s,m);d[m]=0;}return len;
}
#define FILE_WRITE "w"
enum UploadStatus {UPLOAD_FILE_START,UPLOAD_FILE_WRITE,UPLOAD_FILE_END,UPLOAD_FILE_ABORTED};
struct HTTPUpload {
  UploadStatus status=UPLOAD_FILE_START;
  std::string filename="dice-library.dml";
  uint8_t* buf=nullptr;
  size_t currentSize=0,totalSize=0;
};
struct Server {HTTPUpload value;HTTPUpload& upload(){return value;}} server;
Server* srv=&server;
bool diceUploading=false;
uint32_t restartAt=0;
struct {bool started=false;int code=0;} fw;
bool synthBusyFlag=false,inSynthFlash=false;
bool synthBusy(){return synthBusyFlag;}
struct {bool running=false;bool isRunning(){return running;}} Update;
namespace slink {bool up=true;bool synthUp(){return up;}}
namespace engine {
struct Status{bool playing=false,paused=false;};
Status current;
Status status(){return current;}
}
int response=0;std::string responseText,screen;
void reply(int code,const char* message){response=code;responseText=message;}
void logFail(const char*,int){}
void log(const char*,const char*,const char* = ""){}
void busy(const char* message){screen=message;}
bool failAllocation=false;int allocated=0;
constexpr int MALLOC_CAP_SPIRAM=1;
void* heap_caps_malloc(size_t n,int){if(failAllocation)return nullptr;void*p=malloc(n);if(p)++allocated;return p;}
void heap_caps_free(void*p){if(p)--allocated;free(p);}

namespace fs {
struct Card {
  std::map<std::string,std::vector<uint8_t>> files;
  bool shortWrite=false,shortRead=false;
  std::string failRenameFrom,failRenameTo;
} card;
struct File {
  std::string name;size_t pos=0;bool opened=false;
  explicit operator bool()const{return opened&&card.files.count(name);}
  bool isDirectory()const{return false;}
  size_t size()const{return card.files.at(name).size();}
  size_t write(uint8_t*p,size_t n){if(card.shortWrite)return 0;auto&v=card.files[name];v.insert(v.end(),p,p+n);return n;}
  size_t read(uint8_t*p,size_t n){if(card.shortRead)return 0;auto&v=card.files[name];n=std::min(n,v.size()-pos);memcpy(p,v.data()+pos,n);pos+=n;return n;}
  void close(){opened=false;}
};
struct FS {
  bool exists(const char*p){return !strcmp(p,"/midi")||card.files.count(p);}
  bool mkdir(const char*){return true;}
  bool remove(const char*p){return card.files.erase(p);}
  bool rename(const char*a,const char*b){
    if(card.failRenameFrom==a && card.failRenameTo==b){card.failRenameFrom.clear();card.failRenameTo.clear();return false;}
    auto i=card.files.find(a);if(i==card.files.end()||card.files.count(b))return false;
    card.files[b]=i->second;card.files.erase(i);return true;
  }
  File open(const char*p,const char*mode="r"){
    if(!strcmp(mode,"w"))card.files[p]={};
    return File{p,0,card.files.count(p)>0};
  }
};
}
namespace storage {fs::FS& remoteFs(){static fs::FS fs;return fs;}}
/* HANDLERS */

std::vector<uint8_t> validLibrary(uint8_t velocity){
  std::vector<uint8_t>b(88,0);
  memcpy(b.data(),"DML1",4);b[4]=1;b[6]=64;b[8]=1;
  b[16]=1;b[18]=1;b[19]=0;b[20]=2;b[21]=1;b[24]=80;b[28]=8;
  memcpy(b.data()+32,"TEST",4);b[82]=24;b[84]=36;b[85]=velocity;b[86]=255;
  assert(mt::DiceMidiLibrary(b.data(),b.size()).valid());return b;
}
void reset(){
 if(diceUpload.file)diceUpload.file.close();
 diceUpload=DiceUpload{};diceUploading=false;fw.started=false;fw.code=0;Update.running=false;
 synthBusyFlag=inSynthFlash=false;restartAt=0;engine::current={};slink::up=true;
 fs::card={};response=0;responseText.clear();failAllocation=false;
 server.value={};server.value.filename="dice-library.dml";
 assert(allocated==0);
}
void phase(UploadStatus s){server.value.status=s;handleDiceChunk();}
void start(){phase(UPLOAD_FILE_START);}
void write(std::vector<uint8_t>&b){server.value.buf=b.data();server.value.currentSize=b.size();phase(UPLOAD_FILE_WRITE);}
void finish(){phase(UPLOAD_FILE_END);handleDiceDone();}
void assertOld(const std::vector<uint8_t>&old){
 assert(fs::card.files.at(kDicePath)==old);assert(!fs::card.files.count(kDiceTmp));assert(allocated==0);
}
int main(){
 auto old=validLibrary(80),newer=validLibrary(110);
 reset();fs::card.files[kDicePath]=old;start();write(newer);finish();
 assert(response==200 && fs::card.files.at(kDicePath)==newer && fs::card.files.at(kDiceBak)==old && !diceUploading && allocated==0);
 reset();fs::card.files[kDicePath]=old;auto invalid=newer;invalid[0]='X';start();write(invalid);finish();
 assert(response==400);assertOld(old);
 reset();fs::card.files[kDicePath]=old;start();write(newer);phase(UPLOAD_FILE_ABORTED);handleDiceDone();
 assert(response==400);assertOld(old);
 reset();fs::card.files[kDicePath]=old;start();server.value.currentSize=mt::kDiceLibraryMaxBytes+1;phase(UPLOAD_FILE_WRITE);handleDiceDone();
 assert(response==413);assertOld(old);
 reset();fs::card.files[kDicePath]=old;start();fs::card.shortWrite=true;write(newer);handleDiceDone();
 assert(response==500);assertOld(old);
 reset();fs::card.files[kDicePath]=old;start();write(newer);phase(UPLOAD_FILE_END);fs::card.shortRead=true;handleDiceDone();
 assert(response==400);assertOld(old);
 reset();fs::card.files[kDicePath]=old;start();write(newer);phase(UPLOAD_FILE_END);failAllocation=true;handleDiceDone();
 assert(response==503);assertOld(old);
 reset();fs::card.files[kDicePath]=old;start();write(newer);phase(UPLOAD_FILE_END);
 fs::card.failRenameFrom=kDiceTmp;fs::card.failRenameTo=kDicePath;handleDiceDone();
 assert(response==500);assertOld(old);
 reset();fs::card.files[kDicePath]=old;start();write(newer);phase(UPLOAD_FILE_END);
 fs::card.failRenameFrom=kDicePath;fs::card.failRenameTo=kDiceBak;handleDiceDone();
 assert(response==500);assertOld(old);
 reset();fs::card.files[kDicePath]=old;start();write(newer);start();handleDiceDone();
 assert(response==400);assertOld(old);
 reset();fs::card.files[kDicePath]=old;fw.started=true;start();handleDiceDone();assert(response==409);assertOld(old);
 reset();fs::card.files[kDicePath]=old;synthBusyFlag=true;start();handleDiceDone();assert(response==409);assertOld(old);
 reset();fs::card.files[kDicePath]=old;engine::current.playing=true;start();handleDiceDone();assert(response==409);assertOld(old);
 reset();fs::card.files[kDicePath]=old;server.value.filename="../dice-library.dml";start();handleDiceDone();assert(response==400);assertOld(old);
 reset();fs::card.files[kDicePath]=old;start();phase(UPLOAD_FILE_END);handleDiceDone();assert(response==400);assertOld(old);
 reset();fs::card.files[kDiceBak]=old;start();write(newer);phase(UPLOAD_FILE_END);
 fs::card.failRenameFrom=kDiceTmp;fs::card.failRenameTo=kDicePath;handleDiceDone();
 assert(response==500);assertOld(old);
 reset();fw.started=true;fw.code=400;start();write(newer);finish();assert(response==200);
 reset();restartAt=100;start();handleDiceDone();assert(response==409 && fs::card.files.empty());
 reset();slink::up=false;start();handleDiceDone();assert(response==503 && fs::card.files.empty());
 reset();engine::current.paused=true;start();handleDiceDone();assert(response==409 && fs::card.files.empty());
 reset();handleDiceDone();assert(response==400 && fs::card.files.empty());
 puts("Dice upload: validation, fixed path, size cap, failure preservation, rollback, contention: PASS");
}
