#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include "app.h"
#include "esp_heap_caps.h"
#include "bank_screen.h"
#include "render_dialog.h"
#include "dialog_actions.h"
#include "bounce_dialog.h"
#include "file_screen.h"
#include "action_dialog.h"
#include "audio/audio.h"
#include "audio/bank.h"
#include "storage/crashlog.h"
#include "hw/sdcard.h"
#include "storage/render_io.h"
#include "storage/storage.h"
namespace storage {
int renderCalls=0,bounceCalls=0,loadCalls=0;
bool budgetLoad=false,backupAvailable=false,lastBackup=false;
Result loadResult=Result::ReadFail;
std::string loadedName, profileHeader;
int profileAppends=0;
bool validName(const char*){return true;}
bool exists(const char*,bool bak){return bak&&backupAvailable;}
bool autosaveExists(const char*){return false;}
Result save(mt::Project&,const char*,SyncProgress,void*){return Result::WriteFail;}
Result load(mt::Project& p,const char* name,bool bak,int*,SyncProgress,void*){
 ++loadCalls;loadedName=name;lastBackup=bak;
 if(budgetLoad){
   // The real staging Project must fit after releasing the browser, including on retry.
   assert(!fileUiHeap::hasAllocation(512*(96+4)));
   void* stage=heap_caps_malloc(sizeof(mt::Project),MALLOC_CAP_SPIRAM);
   if(!stage)return Result::NoMemory;heap_caps_free(stage);
 }
 if(loadResult==Result::Ok)strlcpy(p.name,name,sizeof(p.name));
 return loadResult;
}
Result loadAutosave(mt::Project&,int*,SyncProgress,void*){return Result::ReadFail;}
Result newProject(mt::Project&,int){return Result::Ok;}
Result newDemo(mt::Project&,int){return Result::Ok;}
Result newFromTemplate(mt::Project&,const char*,int*){return Result::Ok;}
Result saveTemplate(const mt::Project&,const char*){return Result::Ok;}
const char* firmwareRev(){return "TEST";}
const char* lastResetText(){return "POWER ON";}
bool appendCpuProfile(const char* head,const audio::Profile& p){assert(p.blocks==10);profileHeader=head;++profileAppends;return true;}
bool stopEngine(){engine::post(engine::Cmd::Stop);return true;}
const char* resultText(Result){return "ERROR";}
bool sanitize(const char* in,char* out){strlcpy(out,in,17);return in[0];}
void renderPath(const mt::Project&,const mt::RenderSpec&,char* o,int n,int){snprintf(o,n,"/samples/render/test.wav");}
Result renderWav(mt::Project&,const mt::RenderSpec&,const char*,RenderStats&,RenderProgress,void*){++renderCalls;return Result::Cancelled;}
Result bounce(mt::Project&,const mt::BounceSpec&,const char*,RenderStats&,RenderProgress,void*,const mt::Instrument*,bool){++bounceCalls;return Result::Cancelled;}
}
namespace audio {
struct NoFlash:mt::BankFlash {
 uint32_t size()const override{return 8192;}
 bool read(uint32_t,void*,uint32_t)override{return false;}
 bool write(uint32_t,const void*,uint32_t)override{return false;}
 bool erase(uint32_t,uint32_t)override{return false;}
 const uint8_t* mapped()const override{return nullptr;}
};
mt::SampleBank& bank(){static NoFlash flash;static mt::SampleBank bank(flash);return bank;}
bool bankMounted(){return false;}
const char* bankResultText(BankResult){return "ERROR";}
BankResult importToCache(const char*,const mt::Project&,ImportOut&,const uint32_t*,BankProgressFn,void*){return BankResult::ReadFail;}
BankResult compactBank(BankProgressFn,void*){return BankResult::Ok;}
BankResult clearCache(const mt::Project&,int*){return BankResult::Ok;}
BankResult loadWavPreview(const char*,uint32_t,int16_t**,uint32_t*,uint32_t*){return BankResult::ReadFail;}
bool previewStop(){return true;}
bool previewPlaying(){return false;}
void previewBuffer(const int16_t*,uint32_t,uint32_t){}
bool synthIn=true,reverbIn=true,profiling=false;
int profileStarts=0,profileStops=0;
bool synthInternal(){return synthIn;}
bool reverbInternal(){return reverbIn;}
bool profileRunning(){return profiling;}
void profileStart(){assert(!profiling);profiling=true;++profileStarts;}
bool profileStop(Profile& p){assert(profiling);profiling=false;++profileStops;p={};p.blocks=10;p.us[0]=1000;return true;}
}
namespace hw {bool batteryPresent(){return false;}}
namespace {
hw::InputEvent click{hw::InputType::EncClick,0,false};
hw::InputEvent turn(int n){return {hw::InputType::EncTurn,static_cast<int8_t>(n),false};}
bool label(const LGFX_Sprite& s,const char* text,int y=-1){for(const auto& l:s.labels)if(l.text==text&&(y<0||l.y==y))return true;return false;}
int labelY(const LGFX_Sprite& s,const char* text){for(const auto& l:s.labels)if(l.text==text)return l.y;return -1;}
void tap(ui::FileScreen& file,int y,int x=32){file.onTouch({ui::TouchType::Tap,static_cast<int16_t>(x),static_cast<int16_t>(y+8),0,0,0,0,0});}
void renamePattern(ui::App& app,ui::BankScreen& bank,const char* text){
 bank.onInput({hw::InputType::EncLong,0,false});
 LGFX_Sprite menu;app.menu().draw(menu);
 assert(label(menu,"Rename..."));
 app.menu().onInput(click);
 LGFX_Sprite keyboard;bank.draw(keyboard,ui::kAreaY,ui::kAreaH);
 assert(label(keyboard,"P01 NAME:"));
 // Touch the actual keyboard keys, then OK. Initial name is empty in these scenarios.
 for(const char* p=text;*p;++p){
   const int key=*p-'A';const int row=1+key/10,col=key%10;
   bank.onTouch({ui::TouchType::Tap,static_cast<int16_t>(10+col*46+10),static_cast<int16_t>(ui::kAreaY+40+row*40+10),0,0,0,0,0});
 }
 bank.onTouch({ui::TouchType::Tap,300,static_cast<int16_t>(ui::kAreaY+40+4*40+10),0,0,0,0,0});
}
}
int main(int argc,char** argv){
 assert(argc==2);auto project=std::make_unique<mt::Project>();
 auto app=std::make_unique<ui::App>(*project);ui::BankScreen bank(*app);bank.onEnter();
 const std::string test=argv[1];
 if(test=="rename"){
   project->patterns[0].steps[2][3].note=64;
   renamePattern(*app,bank,"VERSE");
   assert(!strcmp(project->patterns[0].name,"VERSE"));assert(app->edits==1);
   assert(project->patterns[0].steps[2][3].note==64);
   auto previous=std::make_unique<mt::Pattern>();uint8_t pattern=255;
   assert(app->undo()->pop(pattern,*previous));assert(pattern==0);assert(!previous->name[0]);assert(previous->steps[2][3].note==64);
   LGFX_Sprite tiles;bank.draw(tiles,ui::kAreaY,ui::kAreaH);assert(label(tiles,"VERSE"));
 }else if(test=="rename_nomemory"){
   app->memory=false;renamePattern(*app,bank,"VERSE");assert(!project->patterns[0].name[0]);assert(app->edits==0);assert(app->message=="NO MEMORY");
 }else if(test=="render_footer"){
   ui::RenderDialog dialog(*app);dialog.open();LGFX_Sprite sprite;dialog.draw(sprite,ui::kAreaY);
   assert(label(sprite,"CANCEL",ui::kAreaY+ui::kAreaH-ui::DialogActions::kH+8));
   dialog.onTouch({ui::TouchType::Tap,40,ui::kAreaY+ui::kAreaH-8,0,0,0,0,0});assert(!dialog.isOpen());
 }else if(test=="bounce_footer"){
   ui::BounceDialog dialog(*app);dialog.open(0,60,mt::kTracks);LGFX_Sprite sprite;dialog.draw(sprite,ui::kAreaY);
   assert(label(sprite,"CANCEL",ui::kAreaY+ui::kAreaH-32+8));
   dialog.onTouch({ui::TouchType::Tap,40,ui::kAreaY+ui::kAreaH-8,0,0,0,0,0});assert(!dialog.isOpen());
 }else if(test=="import_footer"){
   fs::files["/midi/test.mid"]={'M','T','h','d',0,0,0,6,0,0,0,1,0,96,'M','T','r','k',0,0,0,12,0,0x90,60,100,96,0x80,60,0,0,0xff,0x2f,0};
   ui::ImportDialog dialog(*app);assert(dialog.open("/midi","test.mid"));LGFX_Sprite sprite;dialog.draw(sprite,ui::kAreaY);
   assert(label(sprite,"IMPORT",ui::kAreaY+ui::kAreaH-32+8));
   dialog.onTouch({ui::TouchType::Tap,40,ui::kAreaY+ui::kAreaH-8,0,0,0,0,0});assert(!dialog.isOpen());
 }else if(test=="system_diagnostics"){
   ui::FileScreen file(*app);file.onEnter();file.onPage(2);LGFX_Sprite system;file.draw(system,ui::kAreaY,ui::kAreaH);
   const int y=labelY(system,"Diagnostics...");assert(y>=0);assert(!label(system,"Firmware"));
   tap(file,y);LGFX_Sprite diag;file.draw(diag,ui::kAreaY,ui::kAreaH);assert(label(diag,"Firmware"));assert(label(diag,"< SYSTEM / DIAGNOSTICS"));
   file.onBack();file.onInput(click);LGFX_Sprite reopened;file.draw(reopened,ui::kAreaY,ui::kAreaH);assert(label(reopened,"Firmware"));
   tap(file,ui::kAreaY+28);LGFX_Sprite back;file.draw(back,ui::kAreaY,ui::kAreaH);assert(label(back,"Diagnostics..."));assert(app->edits==0);
 }else if(test=="wifi_system"){
   hw::ready=false;ui::FileScreen file(*app);file.onEnter();LGFX_Sprite projects;file.draw(projects,ui::kAreaY,ui::kAreaH);assert(!label(projects,"Wi-Fi transfer..."));
   file.onPage(2);LGFX_Sprite system;file.draw(system,ui::kAreaY,ui::kAreaH);const int y=labelY(system,"Wi-Fi transfer...");assert(y>=0);tap(file,y);assert(file.wifiOpen());
 }else if(test=="render_stops_transport"){
   engine::current.playing=true;ui::RenderDialog dialog(*app);dialog.open();
   dialog.onTouch({ui::TouchType::Tap,400,ui::kAreaY+ui::kAreaH-8,0,0,0,0,0});
   assert(engine::stops==1);assert(storage::renderCalls==1);
 }else if(test=="bounce_stops_countin"){
   engine::current.countIn=4;ui::BounceDialog dialog(*app);dialog.open(0,60,mt::kTracks);
   dialog.onTouch({ui::TouchType::Tap,400,ui::kAreaY+ui::kAreaH-8,0,0,0,0,0});
   assert(engine::stops==1);assert(storage::bounceCalls==1);
 }else if(test=="action_apply_touch" || test=="action_cancel_touch"){
   ui::ActionDialog dialog(*app);int local=10,applied=0;
   const ui::Param param={"Amount",[&](char* out,int n){snprintf(out,n,"%d",local);},[&](int d){local+=d;}};
   dialog.open("CHANGE",&param,1,[&]{assert(!dialog.isOpen());applied=local;});
   dialog.onInput(click);dialog.onInput(turn(3));
   LGFX_Sprite sprite;dialog.draw(sprite,ui::kAreaY);assert(label(sprite,"APPLY",ui::kAreaY+ui::kAreaH-32+8));
   dialog.onTouch({ui::TouchType::Tap,static_cast<int16_t>(test=="action_apply_touch"?400:40),ui::kAreaY+ui::kAreaH-8,0,0,0,0,0});
   assert(!dialog.isOpen());assert(applied==(test=="action_apply_touch"?13:0));assert(app->edits==0);
 }else if(test=="project_load_budget" || test=="project_load_failure" || test=="project_load_backup"){
   fs::files["/projects/ALPHA.mtp"]={1};fs::files["/projects/BETA.mtp"]={2};
   ui::FileScreen file(*app);file.onEnter();LGFX_Sprite projects;file.draw(projects,ui::kAreaY,ui::kAreaH);
   const int y=labelY(projects,"Load...");assert(y>=0);tap(file,y);
   assert(fileUiHeap::hasAllocation(512*(96+4)));
   file.onInput(turn(1)); // BETA selected, not the first row.
   storage::budgetLoad=true;fileUiHeap::limit=sizeof(mt::Project)+512*(96+4)-1;
   storage::loadResult=test=="project_load_budget"?storage::Result::Ok:
                       test=="project_load_backup"?storage::Result::BadCrc:storage::Result::NoMemory;
   storage::backupAvailable=test=="project_load_backup";
   const std::string original=project->name;
   file.onInput(click);assert(storage::loadCalls==(test=="project_load_failure"?2:1)&&storage::loadedName=="BETA");
   if(test=="project_load_budget"){
     assert(app->replacements==1&&!strcmp(project->name,"BETA"));
     assert(!fileUiHeap::hasAllocation(512*(96+4)));
   }else{
     assert(app->replacements==0&&project->name==original);
     assert(fileUiHeap::hasAllocation(512*(96+4)));
     if(test=="project_load_backup"){
       LGFX_Sprite choices;app->menu().draw(choices);assert(label(choices,"Load backup"));
       storage::loadResult=storage::Result::Ok;app->menu().onInput(turn(1));app->menu().onInput(click);
       assert(storage::lastBackup&&app->replacements==1);
     }else{
       storage::loadResult=storage::Result::Ok;file.onInput(click);
       assert(app->replacements==1); // The restored selection still loads BETA on retry.
     }
     assert(storage::loadCalls==(test=="project_load_failure"?3:2)&&storage::loadedName=="BETA");
   }
   file.onLeave();
 }else if(test=="project_frame_retry" || test=="project_frame_both_fail"){
   app->allocateFrame();fs::files["/projects/ALPHA.mtp"]={1};fs::files["/projects/BETA.mtp"]={2};
   ui::FileScreen file(*app);file.onEnter();LGFX_Sprite projects;file.draw(projects,ui::kAreaY,ui::kAreaH);
   tap(file,labelY(projects,"Load..."));file.onInput(turn(1));
   storage::budgetLoad=true;storage::loadResult=storage::Result::Ok;
   fileUiHeap::limit=test=="project_frame_retry" ? 307202+sizeof(mt::Project)-1 : 307202+51200+1;
   const std::string original=project->name;
   file.onInput(click);assert(storage::loadCalls==2&&app->frameLoans==1);
   assert(fileUiHeap::hasAllocation(307202));
   if(test=="project_frame_both_fail"){
     assert(app->replacements==0&&project->name==original);
     assert(fileUiHeap::hasAllocation(51200));
     fileUiHeap::limit=307202+sizeof(mt::Project)-1;file.onInput(click);
     assert(storage::loadCalls==4&&app->frameLoans==2);
   }
   assert(app->replacements==1&&!strcmp(project->name,"BETA"));
   assert(!fileUiHeap::hasAllocation(51200));file.onLeave();
 }else if(test=="profile_metadata"){
   strlcpy(project->name,"TUTU",sizeof(project->name));
   ui::FileScreen file(*app);file.onEnter();file.onPage(2);LGFX_Sprite system;file.draw(system,ui::kAreaY,ui::kAreaH);
   tap(file,labelY(system,"Diagnostics..."));
   file.onInput(turn(8)); // Firmware -> CPU profile, using the real diagnostics list.
   LGFX_Sprite diag;file.draw(diag,ui::kAreaY,ui::kAreaH);assert(label(diag,"CPU profile"));
   for(int run=0;run<2;++run){
     file.onInput(click);assert(audio::profiling&&audio::profileStarts==run+1);assert(storage::profileAppends==run);
     // Snapshot must reflect STOP, before a later Wi-Fi dialog changes placement.
     audio::synthIn=run!=0;engine::seqIn=run==0;audio::reverbIn=run!=0;
     fileUiHeap::internalFree=65537;fileUiHeap::internalLargest=32769;
     fileUiHeap::psramFree=131073;fileUiHeap::psramLargest=98305;
     file.onInput(click);assert(!audio::profiling&&audio::profileStops==run+1);assert(storage::profileAppends==run+1);
     const std::string first="firmware TEST, 0 s, 10 blocks, render 1000 us = 25.0 %\n";
     assert(storage::profileHeader.compare(0,first.size(),first)==0);
     const std::string placement=run==0 ? "project TUTU, synth PSRAM, seq INTERNAL, rvb PSRAM, " :
                                                      "project TUTU, synth INTERNAL, seq PSRAM, rvb INT, ";
     assert(storage::profileHeader.substr(first.size())==placement+
            "internal free 65537 largest 32769 bytes, PSRAM free 131073 largest 98305 bytes\n");
     assert(app->message.find("SAVED 25%") == 0);
   }
 }else if(test=="psram_diagnostics"){
   ui::FileScreen file(*app);file.onEnter();file.onPage(2);LGFX_Sprite system;file.draw(system,ui::kAreaY,ui::kAreaH);
   tap(file,labelY(system,"Diagnostics..."));LGFX_Sprite diag;file.draw(diag,ui::kAreaY,ui::kAreaH);
   assert(label(diag,"PSRAM"));assert(label(diag,"128K FREE, MAX 96K"));
 }else if(test=="action_encoder"){
   ui::ActionDialog dialog(*app);int applied=0;const ui::Param p={"Source",nullptr,nullptr};
   dialog.open("APPLY",&p,1,[&]{++applied;});dialog.onInput(turn(1));dialog.onInput(click);assert(!dialog.isOpen());assert(applied==1);
   dialog.open("CANCEL",&p,1,[&]{++applied;});dialog.onInput(turn(2));dialog.onInput(click);assert(!dialog.isOpen());assert(applied==1);
 }else assert(false);
 puts(argv[1]);
}
