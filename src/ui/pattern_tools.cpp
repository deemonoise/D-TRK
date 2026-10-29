#include "app.h"
#include "grid_screen.h"
#include <stdio.h>
namespace ui {
namespace { int clamp(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;} }
mt::Sel GridScreen::toolScope() const {
  auto s=menuCtx_.processScope();
  if(!menuCtx_.selected){
    const int own=app_.project().patterns[menuCtx_.pattern].trackLen[menuCtx_.track];
    if(own && own-1<s.s1)s.s1=own-1;
  }
  return s;
}
void GridScreen::applyTool(std::function<mt::ToolResult(mt::Pattern&,mt::Pattern&)> operation) {
  if(!menuTargetValid())return;
  auto* scratch=app_.undoScratch();
  if(!scratch || !app_.undo()){app_.toast("NO MEMORY");return;}
  app_.setRecording(false,false);app_.pushUndo();
  engine::lockProject();const auto result=operation(pat(),*scratch);engine::unlockProject();
  editPushed_=false;
  if(result!=mt::ToolResult::Changed)app_.dropUndo();
  switch(result){
    case mt::ToolResult::Changed:app_.markDirty();engine::post(engine::Cmd::ReleaseTies);app_.toast("APPLIED");break;
    case mt::ToolResult::Unchanged:app_.toast("NO CHANGE");break;
    case mt::ToolResult::NoRoom:app_.toast("NOT ENOUGH STEPS");break;
    case mt::ToolResult::NoSlot:app_.toast("FX FULL: NO CHANGE");break;
    case mt::ToolResult::Invalid:app_.toast("INVALID TARGET");break;
  }
  app_.invalidate();
}
void GridScreen::openPasteMenu(){
  const MenuItem rows[]={{"Paste all",0},{"Paste notes only",1},{"Paste automation only",2}};
  char title[28];menuScopeTitle(title,sizeof(title),menuCtx_.editScope());
  app_.menu().openChild(title,rows,3,[this](int id){
    auto* clip=app_.clipboard();if(!clip)return;
    const auto s=menuCtx_.editScope();
    applyTool([clip,s,id](mt::Pattern& p,mt::Pattern& scratch){
      return mt::pastePart(p,scratch,*clip,s.t0,s.s0,static_cast<mt::StepPart>(id));
    });
  });
}
void GridScreen::openDoublePattern(){
  char label[28],title[28];snprintf(label,sizeof(label),"Apply: %d -> %d steps",menuCtx_.length,menuCtx_.length*2);
  snprintf(title,sizeof(title),"PATTERN P%d x2",menuCtx_.pattern+1);
  const MenuItem rows[]={{"All 16 tracks",0,false},{label,1,menuCtx_.length*2<=mt::kMaxSteps}};
  app_.menu().openChild(title,rows,2,[this](int){
    applyTool([](mt::Pattern& p,mt::Pattern&){return mt::doublePattern(p);});
  });
}
void GridScreen::openRotateDialog(){
  const auto s=toolScope();const int max=s.s1-s.s0;
  rotateAmount_=0;
  const Param rows[]={
    {"Steps",[this](char* o,int n){snprintf(o,n,"%+d",rotateAmount_);},
      [this,max](int d){rotateAmount_=clamp(rotateAmount_+d,-max,max);}},
    {"Move",[this](char* o,int n){const char* names[]={"ALL","NOTES ONLY","AUTOMATION"};snprintf(o,n,"%s",names[static_cast<int>(rotatePart_)]);},
      [this](int d){rotatePart_=static_cast<mt::StepPart>(clamp(static_cast<int>(rotatePart_)+d,0,2));}}};
  char scope[28],title[48];menuScopeTitle(scope,sizeof(scope),s,!menuCtx_.selected);
  snprintf(title,sizeof(title),"ROTATE %s",scope);setEdit(false);
  action_.open(title,rows,2,[this,s]{
    applyTool([this,s](mt::Pattern& p,mt::Pattern& scratch){return mt::rotateSteps(p,scratch,s,rotateAmount_,rotatePart_);});
  });
}
void GridScreen::openVelocityDialog(){
  const auto s=toolScope();
  const Param rows[]={
    {"Mode",[this](char* o,int n){const char* modes[]={"SET","ADD","RAMP","HUMANIZE"};snprintf(o,n,"%s",modes[static_cast<int>(velocitySpec_.mode)]);},
      [this](int d){
        const int mode=clamp(static_cast<int>(velocitySpec_.mode)+d,0,3);
        if(mode==static_cast<int>(velocitySpec_.mode))return;
        velocitySpec_.mode=static_cast<mt::VelocityMode>(mode);
        velocitySpec_.amount=mode==0?100:mode==1?0:mode==2?40:5;
      }},
    {"Value / amount",[this](char* o,int n){
        const auto mode=velocitySpec_.mode;
        snprintf(o,n,mode==mt::VelocityMode::Humanize?"+/-%d":mode==mt::VelocityMode::Add?"%+d":"%d",velocitySpec_.amount);
      },[this](int d){
        const int min=velocitySpec_.mode==mt::VelocityMode::Add?-127:velocitySpec_.mode==mt::VelocityMode::Humanize?0:1;
        velocitySpec_.amount=clamp(velocitySpec_.amount+d,min,127);
      }},
    {"Ramp end",[this](char* o,int n){snprintf(o,n,"%d",velocitySpec_.end);},
      [this](int d){if(velocitySpec_.mode==mt::VelocityMode::Ramp)velocitySpec_.end=clamp(velocitySpec_.end+d,1,127);},
      [this]{return velocitySpec_.mode!=mt::VelocityMode::Ramp;}}};
  char scope[28],title[48];menuScopeTitle(scope,sizeof(scope),s,!menuCtx_.selected);
  snprintf(title,sizeof(title),"VELOCITY %s",scope);setEdit(false);
  action_.open(title,rows,3,[this,s]{
    velocitySpec_.seed=micros();
    applyTool([this,s](mt::Pattern& p,mt::Pattern&){
      uint8_t defaults[mt::kTracks];bool drums[mt::kTracks];
      for(int t=0;t<mt::kTracks;++t){defaults[t]=app_.project().tracks[t].defVel;drums[t]=drumAt(t);}
      return mt::changeVelocity(p,s,velocitySpec_,defaults,drums);
    });
  });
}
}
