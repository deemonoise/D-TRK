#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "ab_keys.h"
#include "automation.h"
#include "fx_info.h"
#include "note_name.h"
#include "scale.h"
#include "engine/engine.h"
#include "hw/input.h"
#include "ui/theme.h"
#include "ui/touch.h"
#include "ui/grid_overview_layout.h"

uint32_t millis() { return 1000; }
namespace mt {
void Project::reset() {}
bool Project::anySolo() const {
  for (const auto& t : tracks) if (t.solo) return true;
  return false;
}
}
namespace engine {
void lockProject() {}
void unlockProject() {}
bool post(Cmd, uint16_t, uint8_t) { return true; }
}
namespace audio {
int previews = 0;
uint8_t lastNote = 0;
void preview(uint8_t, uint8_t note, uint8_t) { ++previews; lastNote = note; }
}
namespace ui {
const char* const kFieldNames[] = {"NOTE", "VEL", "FX1", "FX1 VAL", "FX2", "FX2 VAL", "FX3", "FX3 VAL",
                                 "FX4", "FX4 VAL", "FX5", "FX5 VAL", "FX6", "FX6 VAL"};
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
struct Modal {
  bool isOpen() const { return false; }
  void onInput(const hw::InputEvent&) {}
  void onTouch(const TouchEvent&) {}
  void cancel() {}
};
struct Perf {
  bool enabled=false;
  void setOn(bool on) { enabled=on; }
  bool press(int) { return false; }
  bool turn(int, bool) { return false; }
  bool xfadeTurn(int, bool) { return false; }
};
struct App {
  std::unique_ptr<mt::Project> p = std::make_unique<mt::Project>();
  engine::Status status_{};
  Perf perf_;
  int track_ = 0, undos = 0, dirties = 0;
  bool preview_ = true;
  bool shift_ = false;
  App() {
    p->patterns[0].length = 16;
    for (int i = 0; i < mt::kTracks; ++i) {
      p->tracks[i].instr = static_cast<uint8_t>(i);
      p->tracks[i].out = mt::TrackOut::Int;
      p->tracks[i].mute = p->tracks[i].solo = false;
      p->gen[i].mode = 0;
      std::snprintf(p->tracks[i].name, sizeof(p->tracks[i].name), "T%02d", i + 1);
    }
    p->instruments[0].type = mt::InstrType::Kit;
    p->instruments[1].type = mt::InstrType::Chip;
    for (int lane = 0; lane < mt::kKitLanes; ++lane) p->instruments[0].kit[lane].note = 36 + lane;
  }
  mt::Project& project() { return *p; }
  const engine::Status& status() { return status_; }
  Perf& perf() { return perf_; }
  int editPattern() const { return 0; }
  int curTrack() const { return track_; }
  void setCurTrack(int t) { track_ = t; }
  bool preview() const { return preview_; }
  void pushUndo() { ++undos; }
  void dropUndo() { assert(undos > 0); --undos; }
  void markDirty() { ++dirties; }
  void toast(const char*) {}
  void invalidate() {}
  bool shift() const { return shift_; }
  void setMotionRecording(bool) {}
  void syncRecordArm() {}
};
struct GridScreen {
  enum Field : uint8_t { kNote, kVel, kFx1, kFields = kFx1 + 2 * mt::kFxSlots };
  enum class View : uint8_t { Overview, Detail, Roll, Automation };
  static constexpr int kNamesH=16,kRowH=16,kNumW=32,kColW=56,kOverviewTracks=8;
  static constexpr int kFieldW=32,kKbH=48,kPadW=60,kPadCols=8,kPadRowH=24;
  static constexpr int kInspectorH=32;
  App& app_;
  Modal fill_, action_, transpose_;
  View view_ = View::Overview;
  int curStep_=0,curField_=kNote,lane_=0,editStep_=1,autoParam_=0,top_=0,rows_=2;
  int cellPat_=-1,cellTr_=0,cellStep_=0;
  int y0_=24,h_=272,autoAnchor_=-1,selT0_=0,selT1_=0,selS0_=0,selS1_=0;
  int focusFx_=0;
  bool rec_=false,dragFrozen_=false;
  uint32_t recLoop_=UINT32_MAX;
  bool edit_=false,holdEdit_=false,editPushed_=false,rollHeld_=false,selOn_=false;
  bool cellOthers_=false,fxCycled_=false,needVisible_=false;
  bool laneHeld_=false,laneHadUndo_=false;
  int lanePat_=-1,laneTr_=0,laneStep_=0;
  mt::Step cellOrig_{},laneOrig_{};
  uint8_t lastNote_[mt::kTracks]{};
  mt::FxSlot lastFx_[mt::kTracks][mt::kFxSlots]{};
  explicit GridScreen(App& app):app_(app) { for(auto& n:lastNote_)n=60; }
  mt::Pattern& pat() { return app_.p->patterns[0]; }
  mt::Step& step() { return pat().steps[track()][cur()]; }
  int track() const { return app_.curTrack(); }
  int cur() const { return curStep_; }
  int len() const { return app_.p->patterns[0].length; }
  bool drumAt(int tr) const { return app_.p->trackIsDrum(tr); }
  bool drum() const { return drumAt(track()); }
  bool roll() const { return view_==View::Roll; }
  bool detail() const { return view_==View::Detail; }
  int half() const { return track()/8; }
  int firstTrack() const { return half()*8; }
  bool dialogOpen() const { return false; }
  bool selected(int,int) const { return false; }
  int drawBadge(LGFX_Sprite&,int) { return 0; }
  /* UI INLINE */
  void drawOverviewFocus(LGFX_Sprite&,int,int,const mt::Step&,bool,bool);
  void focusFieldSelect(int);
  void drawInspector(LGFX_Sprite&,int);
  void inspectorTouch(const TouchEvent&);
  bool perf() const { return app_.perf_.enabled; }
  bool recordKey(int,bool) { return false; }
  void cursorMoved() { finishLaneEdit(false);if(edit_)snapCell();needVisible_=true; }
  void setRec(bool);
  void setPerf(bool);
  void onPatternChange();
  void moveStep(int d) { curStep_=(curStep_+d+len())%len(); }
  void moveTrack(int d) { app_.track_+=d; }
  void moveField(int d) { curField_+=d; }
  void rollMove(int) {}
  void rollTouch(const TouchEvent&) {}
  void rollStretch(int,bool) {}
  void rollPlace() {}
  void openMenu() {}
  void toggleView() { view_=detail()?View::Overview:View::Detail; }
  bool chordClear() { assert(false && "lane gesture cancellation must not clear the step"); return false; }
  void automationInput(const hw::InputEvent&) {}
  void automationEdit(int,bool=false) {}
  void automationTouch(const TouchEvent&) {}
  bool trackKey(int,bool);
  bool hit(int,int,int&,int&,int&) const;
  int rowsFor(int) const;
  uint8_t degreeNote(int,bool,int) const;
  void enterDegree(int,bool);
  void setEdit(bool);
  void snapCell();
  void cancelCell();
  void writeStep(const mt::Step&);
  void toggleLane(int);
  void setNote(uint8_t);
  void previewNote(uint8_t);
  void editTurn(int,bool);
  void editLane(int);
  void finishLaneEdit(bool);
  void onInput(const hw::InputEvent&);
  void onATap(bool);
  void onBack();
  void onTouch(const TouchEvent&);
  void drawOverview(LGFX_Sprite&,int);
  void drawMask(LGFX_Sprite&,int,int,uint16_t,bool,int,bool);
};
/* METHODS */
}

struct Controls {
  ui::App app;
  ui::GridScreen grid{app};
  mt::AbKeys keys;
  void dispatch(mt::AbOut o) {
    using mt::AbAction;
    if(o.act==AbAction::ATap) grid.onATap(o.shift);
    else if(o.act==AbAction::EditTurn) grid.onInput({hw::InputType::EditTurn,o.delta,o.shift});
    else if(o.act==AbAction::EditEnd) grid.onInput({hw::InputType::EditEnd,0,false});
    else if(o.act==AbAction::EditCancel) grid.onInput({hw::InputType::EditCancel,0,false});
    else if(o.act==AbAction::Back) grid.onBack();
  }
  void aDown() { dispatch(keys.aDown()); }
  void aUp() { dispatch(keys.aUp(false)); }
  void aTap() { aDown();aUp(); }
  void turn(int d,bool shift=false) { grid.onInput({hw::InputType::EncTurn,static_cast<int8_t>(d),shift}); }
  void aTurn(int d,bool shift=false) { dispatch(keys.turn(d,shift)); }
  void click(bool shift=false) { grid.onInput({hw::InputType::EncClick,0,shift}); }
  void cancel() { dispatch(keys.bDown());dispatch(keys.bUp(false));aUp(); }
  void back() { dispatch(keys.bDown());dispatch(keys.bUp(false)); }
};

int headerX(const LGFX_Sprite& sprite,const char* name) {
  for(const auto& t:sprite.texts)if(t.y==24&&t.value==name)return t.x;
  assert(false && "all eight track headers must remain visible");return -1;
}
void focusLayout() {
  for(int bank=0;bank<2;++bank)for(int selected=0;selected<8;++selected){
    Controls c;auto& g=c.grid;c.app.track_=bank*8+selected;
    LGFX_Sprite idle;g.drawOverview(idle,40);
    for(int col=0;col<8;++col)assert(headerX(idle,c.app.p->tracks[bank*8+col].name)==36+56*col);
    c.click();LGFX_Sprite editing;g.drawOverview(editing,40);
    const int x=headerX(editing,c.app.p->tracks[c.app.track_].name)-4;
    const int end=selected==7?480:headerX(editing,c.app.p->tracks[c.app.track_+1].name)-4;
    assert(end-x>56 && "entering edit must expand only the selected track");
    for(int col=0;col<8;++col){
      int step,tr,field;const int hx=headerX(editing,c.app.p->tracks[bank*8+col].name);
      assert(g.hit(hx,41,step,tr,field)&&tr==bank*8+col&&step==0&&field==0);
    }
    c.back();assert(!g.edit_);LGFX_Sprite closed;g.drawOverview(closed,40);
    for(int col=0;col<8;++col)assert(headerX(closed,c.app.p->tracks[bank*8+col].name)==36+56*col);
  }
}
void physicalButtons() {
  Controls c;auto& g=c.grid;
  assert(g.trackKey(1,false)&&c.app.track_==1&&!g.edit_);
  assert(!g.trackKey(2,true)&&c.app.track_==1);
  c.click();g.trackKey(0,false);
  assert(g.pat().steps[1][0].note==60&&g.curStep_==1&&c.app.track_==1);
  c.back();g.trackKey(0,false);c.click();g.trackKey(3,false);g.trackKey(2,true);
  assert(mt::laneOn(g.step(),3)&&mt::laneOn(g.step(),10)&&c.app.track_==0);
  c.back();assert(!g.padShown());
}
void compactFooter() {
  Controls c;auto& g=c.grid;c.app.track_=1;
  assert(g.rowsFor(272)==16);
  c.click();assert(g.rowsFor(272)==14 && "note editing must replace the mini keyboard with two inspector rows");
  assert(g.inspectorShown());c.back();assert(g.rowsFor(272)==16);
  c.aDown();c.aTurn(1);assert(g.rowsFor(272)==16&&!g.inspectorShown());c.aUp();assert(!g.edit_);
  g.view_=ui::GridScreen::View::Detail;c.click();assert(g.rowsFor(272)==16);c.back();
  g.view_=ui::GridScreen::View::Overview;c.app.track_=0;c.click();
  assert(g.padShown()&&g.rowsFor(272)==13); // Drum pads retain their original size and location.
}
void tap(ui::GridScreen& g,int x,int y) {
  g.onTouch({ui::TouchType::Tap,static_cast<int16_t>(x),static_cast<int16_t>(y),0,0,0,0,0});
}
void focusFields() {
  Controls c;auto& g=c.grid;c.app.track_=1;
  g.step().note=60;g.step().vel=96;g.step().fx[5]={mt::Fx::FLT,72};const mt::Step original=g.step();
  c.click();g.rows_=g.rowsFor(g.h_);
  tap(g,g.overviewLayout().x(1)+36,41);assert(g.edit_&&g.curField_==g.kVel);
  c.turn(1);assert(g.step().vel==97&&g.step().note==60);
  tap(g,410,288);assert(g.edit_&&g.curField_==g.kFx1+10&&g.focusFx_==5);
  tap(g,420,270);assert(g.curField_==g.kFx1+11);c.turn(1);
  assert(g.step().fx[5].val==73&&g.step().vel==97&&c.app.undos==1);
  LGFX_Sprite sprite;g.drawOverview(sprite,40);g.drawInspector(sprite,264);
  bool value=false;for(const auto& t:sprite.texts)if(t.y==40&&t.value.find("73")!=std::string::npos)value=true;
  assert(value);
  c.click(true);assert(!g.edit_&&g.curField_==g.kNote&&c.app.undos==0);
  assert(std::memcmp(&g.step(),&original,sizeof original)==0);
  // Re-entering never edits a hidden FX field after the column collapses.
  c.aDown();c.aTurn(1);c.aUp();assert(g.step().fx[5].val==72&&g.step().note!=60);
}
void focusTouch() {
  for(int bank=0;bank<2;++bank)for(int selected=0;selected<8;++selected){
    Controls c;auto& g=c.grid;c.app.track_=bank*8+selected;c.click();g.rows_=g.rowsFor(g.h_);
    for(int col=0;col<8;++col){
      const int x=g.overviewLayout().x(col)+4,tr=bank*8+col;
      tap(g,x,25);assert(c.app.p->tracks[tr].mute);
      tap(g,x,25);assert(!c.app.p->tracks[tr].mute);
      c.app.shift_=true;tap(g,x,25);assert(c.app.p->tracks[tr].solo);c.app.shift_=false;
    }
    const int other=(selected+1)%8;
    tap(g,g.overviewLayout().x(other)+4,57);
    assert(!g.edit_&&c.app.track_==bank*8+other&&g.curStep_==1&&g.curField_==g.kNote);
    c.click();tap(g,g.overviewLayout().x(other)+36,41);
    assert(!g.edit_&&g.curStep_==0&&g.curField_==g.kNote);
  }
}
void layoutBounds() {
  for(int bank=0;bank<2;++bank)for(int selected=0;selected<8;++selected){
    Controls c;auto& g=c.grid;c.app.track_=bank*8+selected;c.click();g.rows_=g.rowsFor(g.h_);
    const auto layout=g.overviewLayout();assert(layout.x(0)==32&&layout.x(8)==480);
    int step,tr,field;
    assert(!g.hit(31,41,step,tr,field)&&!g.hit(480,41,step,tr,field));
    assert(!g.hit(32,39,step,tr,field)&&!g.hit(32,40+g.rows_*16,step,tr,field));
    for(int col=0;col<8;++col){
      if(col==selected)assert(layout.width(col)==128);
      else assert(layout.width(col)>=45&&layout.width(col)<=46);
      for(int x=layout.x(col);x<layout.x(col+1);++x){
        assert(g.hit(x,41,step,tr,field)&&tr==bank*8+col&&step==0);
        assert(field==(col==selected?(x-layout.x(col))/32:0));
      }
    }
    LGFX_Sprite sprite;g.drawOverview(sprite,40);
    for(const auto& r:sprite.rects)assert(r.x>=0&&r.x+r.w<=480);
    for(const auto& t:sprite.texts)assert(t.x>=0&&t.x+8*static_cast<int>(t.value.size())<=480);
  }
}
void modeTransitions() {
  for(int transition=0;transition<3;++transition){
    Controls c;auto& g=c.grid;c.app.track_=1;c.click();g.focusFieldSelect(g.kFx1+11);
    if(transition==0)g.onPatternChange();else if(transition==1)g.setRec(true);else g.setPerf(true);
    assert(!g.edit_&&g.curField_==g.kNote && "collapsed Overview must never retain a hidden FX field");
    c.click();c.turn(1);assert(g.step().hasNote()&&g.step().fx[5].cmd==mt::Fx::None);
  }
}

void holdLane() {
  Controls c;auto& g=c.grid;auto& s=g.step();
  s.note=93;s.fx[0]={mt::Fx::FLT,80};mt::setLane(s,0,true);
  c.aDown();c.aTurn(2);
  assert(g.lane_==2 && mt::laneMask(s)==((1<<0)|(1<<2)));
  assert(audio::previews==1 && audio::lastNote==38);
  c.aTurn(7);
  assert(g.lane_==9 && mt::laneMask(s)==((1<<0)|(1<<9)));
  assert(s.note==93 && s.fx[0].cmd==mt::Fx::FLT && s.fx[0].val==80);
  assert(audio::previews==2 && audio::lastNote==45);
  c.aUp();assert(!g.edit_ && c.app.undos==1 && mt::laneOn(s,9));
  c.aDown();c.aTurn(1);c.aUp();
  assert(mt::laneMask(s)==((1<<0)|(1<<9)|(1<<10)) && c.app.undos==2);
  c.app.preview_=false;c.aDown();c.aTurn(1);c.aUp();assert(audio::previews==3);
}
void tapLastLane() {
  Controls c;auto& s=c.grid.step();s.note=100;mt::setLane(s,0,true);c.grid.lane_=12;
  c.aTap();assert(mt::laneMask(s)==((1<<0)|(1<<12)) && !c.grid.edit_);
  assert(audio::lastNote==48 && c.app.undos==1);
  c.aTap();assert(mt::laneOn(s,12) && c.app.undos==1);
}
void persistentEdit() {
  Controls c;auto& g=c.grid;auto& s=g.step();c.click();assert(g.padShown());
  c.turn(3);assert(g.lane_==3 && audio::previews==0);
  c.aTap();assert(mt::laneOn(s,3) && g.padShown() && g.curStep_==0);
  c.aTap();assert(!mt::laneOn(s,3) && g.padShown());
  c.aTap();c.turn(5);c.aTap();
  assert(mt::laneMask(s)==((1<<3)|(1<<8)) && g.edit_ && c.app.undos==1);
  c.click();assert(!g.edit_ && !g.padShown());
}
void cancelGesture() {
  Controls c;auto& g=c.grid;auto& s=g.step();s.note=77;mt::setLane(s,4,true);s.fx[0]={mt::Fx::PRB,55};
  const mt::Step before=s;
  c.aDown();c.aTurn(2);c.cancel();
  assert(std::memcmp(&s,&before,sizeof s)==0 && !g.edit_ && c.app.undos==0);
  c.click();c.turn(1);c.aTap();const mt::Step manual=s;assert(c.app.undos==1);
  c.aDown();c.aTurn(7);c.aTurn(1);c.cancel();
  assert(std::memcmp(&s,&manual,sizeof s)==0 && g.padShown() && c.app.undos==1);
  c.click(true);assert(std::memcmp(&s,&before,sizeof s)==0 && !g.edit_ && c.app.undos==0);
}
void gestureEdges() {
  Controls c;auto& g=c.grid;auto& s=g.step();s.fx[0]={mt::Fx::PRB,55};
  const mt::Step empty=s;
  c.aDown();c.aTurn(3);c.cancel();
  assert(std::memcmp(&s,&empty,sizeof s)==0 && c.app.undos==0);
  c.aDown();c.aTurn(-127);assert(g.lane_==0 && mt::laneMask(s)==1);
  const int previews=audio::previews;c.aTurn(-1);assert(audio::previews==previews);
  c.aUp();assert(c.app.undos==1);
  const mt::Step before=s;
  c.aDown();c.aTurn(127);assert(g.lane_==15 && mt::laneOn(s,15));
  c.aTurn(-127);c.aUp();
  assert(std::memcmp(&s,&before,sizeof s)==0 && c.app.undos==1);
  c.app.p->tracks[0].out=mt::TrackOut::Midi;
  const int silent=audio::previews;c.aDown();c.aTurn(8);c.aUp();
  assert(mt::laneOn(s,8) && audio::previews==silent);
  c.click();c.aDown();c.aTurn(1);c.aUp();assert(g.padShown());
  c.click();assert(!g.edit_);
}
void renderCursor() {
  for(int theme=0;theme<ui::themeCount();++theme){
    ui::applyTheme(theme);Controls c;c.click();c.turn(9);LGFX_Sprite sprite;c.grid.drawOverview(sprite,40);
    int selected=0;for(const auto& r:sprite.rects)if(r.w==3&&r.h==5&&r.color==ui::kEditCursor){
      ++selected;assert(r.x==37 && r.y==49);
    }
    assert(selected==1);
    c.click();LGFX_Sprite idle;c.grid.drawOverview(idle,40);
    for(const auto& r:idle.rects)assert(r.w!=5||r.h!=5);
  }
}
void melodicRegression() {
  Controls c;c.app.track_=1;c.click();c.aTap();
  assert(c.app.p->patterns[0].steps[1][0].note==60 && c.grid.curStep_==1 && c.grid.edit_);
  c.click();c.aDown();c.aTurn(1);c.aUp();assert(!c.grid.edit_);
}
void shiftSelectsFields() {
  for(int tr:{1,7,8,15}){
    Controls c;auto& g=c.grid;c.app.track_=tr;g.curStep_=5;
    g.step().note=60;g.step().vel=96;g.step().fx[0]={mt::Fx::FLT,72};
    const mt::Step original=g.step();c.click();
    for(int field=1;field<=g.kFields;++field){
      c.turn(1,true);
      assert(g.curField_==field%g.kFields && "Shift+turn must select the next field, not change its value");
      if(g.curField_>=g.kFx1)assert(g.focusFx_==(g.curField_-g.kFx1)/2);
      assert(g.edit_&&g.curStep_==5&&c.app.track_==tr);
      assert(std::memcmp(&g.step(),&original,sizeof original)==0);
      assert(c.app.undos==0&&c.app.dirties==0&&audio::previews==0);
    }
    c.turn(-1,true);assert(g.curField_==g.kFields-1&&g.focusFx_==5);
    c.turn(-127,true);assert(g.curField_==12&&g.focusFx_==5);
    c.turn(127,true);assert(g.curField_==13);
    c.turn(1,true);assert(g.curField_==g.kNote&&c.app.track_==tr);
  }
}
void shiftEditUndo() {
  Controls c;auto& g=c.grid;c.app.track_=1;
  g.step().note=60;g.step().vel=96;g.step().fx[0]={mt::Fx::FLT,72};const mt::Step original=g.step();
  c.click();c.turn(1,true);c.turn(1);assert(g.step().vel==97&&g.curField_==g.kVel);
  c.turn(2,true);c.turn(1);assert(g.step().fx[0].val==73&&c.app.undos==1);
  // A+Shift+turn retains the coarse-value edit gesture in the expanded column.
  c.aDown();c.aTurn(1,true);c.aUp();assert(g.curField_==g.kFx1+1&&g.step().fx[0].val==83&&g.edit_);
  c.click(true);assert(!g.edit_&&c.app.undos==0&&g.curField_==g.kNote);
  assert(std::memcmp(&g.step(),&original,sizeof original)==0);
  c.click();c.turn(1,true);c.turn(1);c.back();assert(!g.edit_&&g.step().vel==97);
}
void shiftDrumAndDetail() {
  Controls c;auto& g=c.grid;g.step().note=100;mt::setLane(g.step(),2,true);g.lane_=2;
  c.click();c.turn(1,true);
  assert(g.curField_==g.kVel&&mt::laneMask(g.step())==(1<<2)&&g.step().note==100);
  assert(!g.padShown()&&g.inspectorShown());c.turn(1);assert(g.step().note==101);
  c.turn(-1,true);assert(g.curField_==g.kNote&&g.padShown());
  c.aTap();assert(mt::laneMask(g.step())==0&&g.edit_);c.click(true);
  assert(mt::laneOn(g.step(),2)&&g.step().note==100);
  // Outside Overview editing, established Shift behavior remains in place.
  c.turn(1,true);assert(c.app.track_==1&&!g.edit_);
  g.view_=ui::GridScreen::View::Detail;g.step().note=60;g.step().vel=70;g.curField_=g.kVel;
  c.click();c.turn(1,true);assert(g.step().vel==80&&g.curField_==g.kVel);c.back();
  c.app.track_=0;g.curField_=g.kNote;c.click();c.turn(1,true);
  assert(!mt::laneOn(g.step(),2)&&g.curField_==g.kNote);
}
int main(int argc,char** argv) {
  assert(argc==2);std::string mode=argv[1];
  if(mode=="hold")holdLane();else if(mode=="tap")tapLastLane();else if(mode=="edit")persistentEdit();
  else if(mode=="cancel")cancelGesture();else if(mode=="render")renderCursor();else if(mode=="melodic")melodicRegression();
  else if(mode=="edges")gestureEdges();
  else if(mode=="focus")focusLayout();else if(mode=="buttons")physicalButtons();
  else if(mode=="footer")compactFooter();
  else if(mode=="fields")focusFields();else if(mode=="touch")focusTouch();else if(mode=="bounds")layoutBounds();
  else if(mode=="transitions")modeTransitions();
  else if(mode=="shift-fields")shiftSelectsFields();else if(mode=="shift-undo")shiftEditUndo();
  else if(mode=="shift-regression")shiftDrumAndDetail();
  else return 2;
}
