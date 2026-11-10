#pragma once
#include <string>
#include <cassert>
#include "esp_heap_caps.h"
#include "model.h"
#include "undo.h"
#include "battery_gauge.h"
#include "hw/battery.h"
#include "menu.h"
#include "engine/engine.h"
namespace ui {
class App {
 public:
 explicit App(mt::Project& p):project_(p),undo_(entries_){}
 ~App(){heap_caps_free(frame);}
 void prepareProjectLoadMemory(){assert(frameLoanDepth==0);if(hadFrame)assert(frame);}
 void allocateFrame(){hadFrame=true;frame=heap_caps_malloc(307202,MALLOC_CAP_SPIRAM);assert(frame);}
 class FrameBufferLoan {
  public:
   explicit FrameBufferLoan(App& a):app(a){++app.frameLoanDepth;++app.frameLoans;heap_caps_free(app.frame);app.frame=nullptr;}
   ~FrameBufferLoan(){if(app.hadFrame)app.frame=heap_caps_malloc(307202,MALLOC_CAP_SPIRAM);--app.frameLoanDepth;}
  private:App& app;
 };
 mt::Project& project(){return project_;}
 Menu& menu(){return menu_;}
 mt::Undo* undo(){return memory ? &undo_:nullptr;}
 void pushUndo(uint8_t p){undo_.push(p,project_.patterns[p]);}
 void dropUndo(){undo_.drop();}
 void setRecording(bool,bool){}
 uint8_t editPattern() const{return 0;}
 const engine::Status& status() const{return engine::current;}
 bool shift()const{return false;}
 bool songPlays()const{return false;}
 void invalidate(){}
 void markDirty(){++edits;}
 void markSaved(){saved=edits;}
 bool projectDirty()const{return edits!=saved;}
 void toast(const char* t){message=t;}
 void showBusy(const char*){}
 void endProgress(){}
 void projectReplaced(){++replacements;}
 void loadedToast(const char*,int,bool){}
 void showProgress(const char*,uint32_t,uint32_t){}
 uint32_t editSeq()const{return edits;}
 uint32_t batterySeq()const{return 0;}
 bool batterySample(hw::BatterySample&)const{return false;}
 const mt::BatteryGauge& battery()const{return battery_;}
 int theme()const{return 0;}
 void setTheme(int){}
 int phones()const{return 100;}
 void setPhones(int){}
 bool preview()const{return true;}
 void setPreview(bool){}
 bool perfVisuals()const{return true;}
 void setPerfVisuals(bool){}
 int autosaveMin()const{return 0;}
 void setAutosaveMin(int){}
 static bool renderProgress(uint32_t,uint32_t,void*){return true;}
 static bool labeledProgress(const char*,uint32_t,uint32_t,void*){return true;}
 static void syncProgress(const char*,uint32_t,uint32_t,void*){}
 int edits=0,saved=0,replacements=0,frameLoans=0,frameLoanDepth=0;
 void* frame=nullptr;
 bool hadFrame=false;
 bool memory=true;
 std::string message;
 private:
 mt::BatteryGauge battery_;
 mt::Project& project_;
 Menu menu_;
 mt::Undo undo_;
 mt::Undo::Entry entries_[mt::Undo::kDepth];
};
}
