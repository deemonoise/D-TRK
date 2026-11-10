#include "wt_draw.h"
#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "esp_heap_caps.h"
#include "hw/sdcard.h"
#include "audio/bank.h"
#include "audio/audio.h"
#include "menu.h"
#include "project_io.h"
#include "sample_set.h"
#include "storage/storage.h"
#include "wt_picker.h"

namespace ui {
namespace {
int clampi(int v,int lo,int hi) { return v<lo?lo:(v>hi?hi:v); }

}
bool WtDraw::open(const char* source,std::function<void(const char*)> onDone,bool morph) {
  close();
  storage_=static_cast<int16_t*>(heap_caps_malloc(2*sizeof(int16_t)*mt::kWtSrcSamples,MALLOC_CAP_SPIRAM));
  if (!storage_) { app_.toast(storage::resultText(storage::Result::NoMemory)); return false; }
  doc_.attach(storage_,storage_+mt::kWtSrcSamples);
  if (source && source[0]) {
    for (int f=0;f<mt::kWtFrames;++f) {
      app_.showProgress("READ WT",f,mt::kWtFrames);
      const auto r=audio::wtFrame16(source,f,storage_+f*mt::kWtFrameLen);
      if (r!=audio::BankResult::Ok) {
        app_.endProgress(); close(); app_.toast(audio::bankResultText(r)); return false;
      }
    }
    app_.endProgress(); doc_.loaded();
  } else { doc_.newWave(); if (morph) doc_.setMode(mt::WtEditMode::Morph); }
  const char* b=source&&source[0]?(source[0]=='*'?source+1:source):"";
  if (!storage::sanitize(b,base_)) strlcpy(base_,"DRAW",sizeof(base_));
  onDone_=std::move(onDone); cur_=128; action_=Form; copyFrom_=-1;
  editing_=playing_=stroke_=false; gesture_=0xFFFF;
  app_.invalidate(); return true;
}
void WtDraw::stop() { if (playing_) audio::previewStop(); playing_=false; }
void WtDraw::close() {
  if (!storage_) return;
  stop(); kb_.close(); doc_.detach(); heap_caps_free(storage_); storage_=nullptr;
  onDone_=nullptr; stroke_=false; app_.invalidate();
}
bool WtDraw::onPlay() {
  if (!isOpen()) return false;
  if (playing_ && static_cast<int32_t>(millis()-playUntil_)>=0) playing_=false;
  if (playing_) { stop(); app_.invalidate(); return true; }
  const auto& st=app_.status();
  if (st.playing||st.paused) { app_.toast(audio::bankResultText(audio::BankResult::Busy)); return true; }
  doc_.endStroke(); stroke_=false;
  const auto r=audio::wtAudition(doc_.frame());
  if (r!=audio::BankResult::Ok) app_.toast(audio::bankResultText(r));
  else { playing_=true; playUntil_=millis()+650; }
  app_.invalidate(); return true;
}
void WtDraw::select(int frame) {
  stop(); doc_.endStroke(); stroke_=false;
  doc_.select(clampi(frame,0,63)); app_.invalidate();
}
void WtDraw::frameMenu() {
  const MenuItem items[]={{"Add anchor",0},{"Remove anchor",1},{"Copy this frame",2},{"Paste to empty frame",3}};
  app_.menu().open("FRAME ACTIONS",items,4,[this](int i) {
    stop(); bool ok=false;
    if (i==0) ok=doc_.addAnchor();
    if (i==1) ok=doc_.deleteAnchor();
    if (i==2) { copyFrom_=doc_.selected(); app_.toast("SELECT TARGET, THEN PASTE"); return; }
    if (i==3 && copyFrom_>=0) ok=doc_.copyAnchor(copyFrom_,doc_.selected());
    if (!ok) app_.toast(i==1?"ENDPOINT / NOT AN ANCHOR":i==3?"TARGET OCCUPIED / NO COPY":"ALREADY AN ANCHOR");
    app_.invalidate();
  });
}
void WtDraw::press(int a) {
  doc_.endStroke(); stroke_=false; gesture_=0xFFFF;
  action_=a;
  if (a!=Play) stop();
  switch (a) {
    case Mode:
      if (doc_.mode()==mt::WtEditMode::Wave) doc_.setMode(mt::WtEditMode::Morph);
      else {
        const MenuItem items[]={{"Keep morph",0},{"Use current for all frames",1}};
        app_.menu().open("FLATTEN TO ONE WAVE?",items,2,[this](int i) {
          if (i==1) doc_.setMode(mt::WtEditMode::Wave);
          app_.invalidate();
        });
      }
      break;
    case Form: {
      const MenuItem items[]={{"Sine",0},{"Triangle",1},{"Saw",2},{"Square",3}};
      app_.menu().open("REPLACE CURRENT FORM",items,4,[this](int i) {
        stop(); doc_.shape(static_cast<mt::WtShape>(i)); app_.invalidate();
      }); break;
    }
    case Smooth: doc_.smooth(); break;
    case Undo: if (!doc_.undo()) app_.toast("NOTHING TO UNDO"); break;
    case Frames:
      if (doc_.mode()==mt::WtEditMode::Morph) frameMenu(); else app_.toast("WAVE: ALL FRAMES ARE IDENTICAL");
      break;
    case Play: onPlay(); break;
    case Save: ok(); break;
    case Cancel: cancel(); break;
  }
  app_.invalidate();
}
void WtDraw::point(int x,int y) {
  int p=clampi((x-kCanX)*255/(kCanW-1),0,255);
  int16_t v=static_cast<int16_t>(clampi(32767-((y-y0_-kCanTop)*65535/(kCanH-1)),-32768,32767));
  doc_.drawLine(lastPt_,lastV_,p,v); lastPt_=cur_=p; lastV_=v;
}
void WtDraw::brush(int delta) {
  stop(); const int16_t v=static_cast<int16_t>(clampi(doc_.frame()[cur_]+delta*1024,-32768,32767));
  doc_.beginStroke(); doc_.drawLine(cur_,v,cur_,v); doc_.endStroke();
}
void WtDraw::cancel() {
  stop();
  if (!doc_.dirty()) { close(); return; }
  const MenuItem items[]={{"Keep drawing",0},{"Discard changes",1}};
  app_.menu().open("DISCARD WT CHANGES?",items,2,[this](int i) { if (i==1) close(); });
}
void WtDraw::ok() {
  const auto& st=app_.status();
  if (st.playing||st.paused) { app_.toast(audio::bankResultText(audio::BankResult::Busy)); return; }
  kb_.open("TABLE NAME:",base_,[this](const char* t) {
    char nm[mt::kSampleNameMax+1];
    if (!storage::sanitize(t,nm)) strlcpy(nm,"DRAW",sizeof(nm));
    save(nm);
  }); app_.invalidate();
}
void WtDraw::save(const char* name) {
  if (!hw::sdReady()) { app_.toast(audio::bankResultText(audio::BankResult::NoSd)); return; }
  const bool shared=mt::projWtFind(app_.project(),name)>=0;
  if (!shared && app_.project().wavetableCount>=mt::kProjWavetables) {
    app_.toast("TABLE LIST FULL"); return;
  }
  strlcpy(base_,name,sizeof(base_)); snprintf(path_,sizeof(path_),"/wavetables/%s.wav",base_);
  auto write=[this] {
    stop(); app_.showBusy("WT WRITE...");
    char nm[mt::kSampleNameMax+1];
    const bool imported=wtSaveSourceFile(app_,path_,base_,doc_.data(),nm,true);
    app_.endProgress();
    if (!imported) return;
    auto cb=onDone_; close(); if(cb) cb(nm);
  };
  if (shared || hw::sdFs().exists(path_)) {
    const MenuItem items[]={{"Cancel",0},{shared?"Update shared project table":"Overwrite file",1}};
    app_.menu().open(shared?"REPLACE SHARED WT + FILE?":"FILE EXISTS",items,2,[write](int i) { if (i==1) write(); });
  } else write();
}
void WtDraw::onInput(const hw::InputEvent& ev) {
  if (!isOpen()) return;
  if (kb_.isOpen()) { kb_.onInput(ev); app_.invalidate(); return; }
  doc_.endStroke(); stroke_=false; gesture_=0xFFFF;
  using hw::InputType;
  switch (ev.type) {
    case InputType::EncLong: cancel(); break;
    case InputType::EncTurn:
      if (ev.shift) action_=(action_+ev.delta%Actions+Actions)%Actions;
      else if (editing_) { cur_=(cur_+ev.delta*2+256)%256; }
      else select(doc_.selected()+ev.delta);
      break;
    case InputType::EditTurn: brush(ev.delta); break;
    case InputType::EncClick:
      if (ev.shift) editing_=!editing_;
      else {
        const MenuItem items[]={{"WAVE / MORPH",Mode},{"Form",Form},{"Smooth",Smooth},{"Undo",Undo},
          {"Frame actions",Frames},{"Play / stop",Play},{"Save",Save},{"Cancel",Cancel},{"Draw cursor on / off",Actions}};
        app_.menu().open("WT ACTIONS",items,9,[this](int i) {
          if (i==Actions) { editing_=!editing_; app_.invalidate(); }
          else press(i);
        });
      }
      break;
    default: break;
  }
  app_.invalidate();
}
void WtDraw::onTouch(const TouchEvent& ev) {
  if (!isOpen()) return;
  int y=ev.y-y0_;
  if (ev.type==TouchType::Down) {
    doc_.endStroke();
    gesture_=ev.id; tapX_=ev.x; tapY_=ev.y; tapMoved_=false; stroke_=false;
    if (!kb_.isOpen() && y>=kCanTop && y<kCanTop+kCanH && ev.x>=kCanX && ev.x<kCanX+kCanW) {
      stop(); doc_.beginStroke(); stroke_=true;
      lastPt_=clampi((ev.x-kCanX)*255/(kCanW-1),0,255);
      lastV_=static_cast<int16_t>(clampi(32767-((y-kCanTop)*65535/(kCanH-1)),-32768,32767));
      point(ev.x,ev.y);
    }
    app_.invalidate(); return;
  }
  if (ev.type==TouchType::Move && ev.id==gesture_) {
    if (abs(ev.x-tapX_)>8||abs(ev.y-tapY_)>8) tapMoved_=true;
    if (stroke_) point(ev.x,ev.y);
    app_.invalidate(); return;
  }
  TouchEvent tap=ev;
  if (ev.type==TouchType::Up) {
    if (ev.id!=gesture_) return;
    gesture_=0xFFFF;
    if (stroke_) { point(ev.x,ev.y); doc_.endStroke(); stroke_=false; app_.invalidate(); return; }
    if (tapMoved_ || abs(ev.x-tapX_)>8 || abs(ev.y-tapY_)>8 || ev.y<y0_ || ev.y>=y0_+kAreaH || ev.x<0 || ev.x>=kScreenW) return;
    // Activate the original target only; a release cannot drift onto a neighbour.
    auto target=[&](int x,int yy) {
      yy-=y0_;
      if (yy<24) return 0;
      if (yy<56) return 1+x/120;
      if (yy>=kStripTop && yy<kStripTop+kStripH) return 10+x*64/kScreenW;
      if (yy>=kFooterTop) return 80+x/160;
      return -1;
    };
    if (!kb_.isOpen() && target(tapX_,tapY_)!=target(ev.x,ev.y)) return;
    tap.x=static_cast<int16_t>(tapX_); tap.y=static_cast<int16_t>(tapY_);
    tap.type=TouchType::Tap;
  }
  if (tap.type!=TouchType::Tap) return;
  y=tap.y-y0_;
  if (kb_.isOpen()) { kb_.onTouch(tap,app_.shift()); app_.invalidate(); return; }
  if (y<24) press(Mode);
  else if (y<56) press(Form+clampi(tap.x/120,0,3));
  else if (y>=kStripTop && y<kStripTop+kStripH) {
    if (doc_.mode()==mt::WtEditMode::Morph) select(clampi(tap.x*64/kScreenW,0,63));
  } else if (y>=kFooterTop) press(Play+clampi(tap.x/160,0,2));
}
void WtDraw::draw(LGFX_Sprite& s,int y0) {
  if (!isOpen()) return;
  if (playing_ && static_cast<int32_t>(millis()-playUntil_)>=0) playing_=false;
  y0_=y0;
  if (kb_.isOpen()) { kb_.draw(s,y0); return; }
  s.fillRect(0,y0,kScreenW,kAreaH,kBg);
  char text[64]; const bool morph=doc_.mode()==mt::WtEditMode::Morph;
  snprintf(text,sizeof(text),"%s  %s  %s%d%s",morph?"MORPH":"WAVE",base_,morph?"FRAME ":"ALL ",morph?doc_.selected()+1:64,doc_.dirty()?" *":"");
  s.fillRect(0,y0,480,24,kBeatBg); s.setTextColor(kText); s.drawString(text,8,y0+4);
  auto button=[&](int x,int top,int w,int h,const char* label,int a,bool enabled=true) {
    const bool selected=action_==a;
    s.fillRect(x+1,y0+top+1,w-2,h-2,selected?kPlayBg:kMenuBg);
    s.drawRect(x+1,y0+top+1,w-2,h-2,selected?kCursor:kDim);
    s.setTextColor(enabled?(selected?kCursor:kText):kDim);
    s.drawString(label,x+(w-static_cast<int>(strlen(label))*kCharW)/2,y0+top+(h-kCharH)/2);
  };
  button(0,24,120,32,"FORM",Form); button(120,24,120,32,"SMOOTH",Smooth);
  button(240,24,120,32,"UNDO",Undo,doc_.canUndo()); button(360,24,120,32,"FRAMES",Frames,morph);
  const int mid=y0+kCanTop+kCanH/2;
  s.drawRect(kCanX-1,y0+kCanTop-1,kCanW+2,kCanH+2,kDim);
  s.drawFastHLine(kCanX,mid,kCanW,kMenuBg);
  auto plot=[&](const int16_t* pts,uint16_t c) {
    int px=kCanX,py=mid-pts[0]*(kCanH/2-2)/32768;
    for (int i=1;i<256;++i) { int x=kCanX+i*(kCanW-1)/255,y=mid-pts[i]*(kCanH/2-2)/32768; s.drawLine(px,py,x,y,c); px=x;py=y; }
  };
  if (morph) {
    int a=doc_.selected()-1,b=doc_.selected()+1;
    while (a>=0&&!doc_.anchor(a)) --a;
    while (b<64&&!doc_.anchor(b)) ++b;
    if (a>=0) plot(doc_.frame(a),kDim);
    if (b<64) plot(doc_.frame(b),kDim);
  }
  plot(doc_.frame(),doc_.anchor(doc_.selected())?kGreen:kCyan);
  if (editing_) s.drawFastVLine(kCanX+cur_*(kCanW-1)/255,y0+kCanTop,kCanH,kCursor);
  if (morph) {
    for (int f=0;f<64;++f) { int x=f*480/64,w=(f+1)*480/64-x; if (doc_.anchor(f)) s.fillRect(x+1,y0+kStripTop+3,w-2,17,f==doc_.selected()?kCursor:kDim); }
    for (int t=0;t<8;++t) {
      const int f=t*63/7; const int16_t* pts=doc_.frame(f);
      int px=t*60+2,py=y0+kStripTop+11-pts[0]*8/32768;
      for (int q=1;q<28;++q) {
        const int x=t*60+2+q*2, yy=y0+kStripTop+11-pts[q*255/27]*8/32768;
        s.drawLine(px,py,x,yy,kText); px=x;py=yy;
      }
    }
    const int x=doc_.selected()*480/64; s.drawRect(x,y0+kStripTop,8,24,kGreen);
    snprintf(text,sizeof(text),"%s  TURN:FRAME CLICK:MENU A:DRAW",doc_.anchor(doc_.selected())?"ANCHOR":"BETWEEN");
  } else snprintf(text,sizeof(text),"ONE WAVE / ALL FRAMES   CLICK: ACTIONS");
  s.setTextColor(kDim); s.drawString(text,8,y0+209);
  button(0,kFooterTop,160,44,playing_?"STOP":"PLAY",Play);
  button(160,kFooterTop,160,44,"SAVE",Save); button(320,kFooterTop,160,44,"CANCEL",Cancel);
}
} // namespace ui
