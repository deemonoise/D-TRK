#include "app.h"
#include "grid_screen.h"
#include <stdio.h>
namespace ui {
void App::toggleAutomationMute(int tr) {
  if (tr < 0 || tr >= mt::kTracks || !p_->trackInternal(tr)) return;
  const uint16_t mask = automationMute_ ^ static_cast<uint16_t>(1u << tr);
  if (!engine::postWait(engine::Cmd::AutomationMute, mask)) { toast("ENGINE BUSY"); return; }
  automationMute_ = mask;
  char msg[32]; snprintf(msg,sizeof(msg),"TRK%d AUTOMATION %s",tr+1,automationMuted(tr)?"OFF":"ON");
  toast(msg);
}
void GridScreen::openEditTools(const mt::Sel& scope, bool wholeTrack) {
  char title[28]; menuScopeTitle(title,sizeof(title),scope,wholeTrack);
  const MenuItem items[] = {{"Note OFF",3,!wholeTrack},{"Clear notes",0},{"Clear automation",1},{"Clear all",4}};
  app_.menu().openChild(title,items + (wholeTrack ? 1 : 0),wholeTrack ? 3 : 4,[this,scope](int id) {
    if (!menuTargetValid()) return;
    app_.setRecording(false,false);
    if (id == 3 || id == 4) { apply(scope,id == 3 ? kNoteOff : kClearSel); editPushed_=false; return; }
    app_.pushUndo();
    engine::lockProject();
    if (id == 0) mt::clearNotesSel(pat(),scope);
    else mt::clearAutomationSel(pat(),scope);
    engine::unlockProject();
    engine::post(engine::Cmd::ReleaseTies);
    app_.markDirty(); editPushed_=false;
    app_.toast(id == 0 ? "NOTES CLEARED" : "AUTOMATION CLEARED");
  });
}
void GridScreen::openQuantizeMenu(bool refresh) {
  static const char* const grids[] = {"1/4","1/8","1/16","1/32","1/8T","1/16T"};
  char grid[28],strength[28];
  snprintf(grid,sizeof(grid),"Grid: %s",grids[static_cast<int>(quantizeSpec_.grid)]);
  snprintf(strength,sizeof(strength),"Strength: %u%%",quantizeSpec_.strength);
  const MenuItem rows[] = {{grid,0},{strength,1},{"Apply",2}};
  auto cb = [this](int id) {
    if (id == 0) {
      const MenuItem choices[] = {{"1/4",0},{"1/8",1},{"1/16",2},{"1/32",3},{"1/8 TRIPLET",4},{"1/16 TRIPLET",5}};
      app_.menu().openChild("QUANTIZE GRID",choices,6,[this](int n) {
        quantizeSpec_.grid=static_cast<mt::Resolution>(n); app_.menu().back(); openQuantizeMenu(true);
      }); return;
    }
    if (id == 1) {
      const MenuItem choices[] = {{"0%",0},{"25%",25},{"50%",50},{"75%",75},{"100%",100}};
      app_.menu().openChild("QUANTIZE STRENGTH",choices,5,[this](int n) {
        quantizeSpec_.strength=static_cast<uint8_t>(n); app_.menu().back(); openQuantizeMenu(true);
      }); return;
    }
    if (quantizePattern_ != app_.editPattern() || !menuTargetValid()) { app_.toast("PATTERN CHANGED"); return; }
    auto* scratch=app_.undoScratch();
    if (!scratch) { app_.toast("NO MEMORY"); return; }
    app_.setRecording(false,false);
    app_.pushUndo();
    engine::lockProject();
    const auto result=mt::quantizeNotes(pat(),*scratch,quantizeSel_,quantizeSpec_);
    engine::unlockProject();
    editPushed_=false;
    if (result != mt::QuantizeResult::Ok) app_.dropUndo();
    switch (result) {
      case mt::QuantizeResult::Ok: app_.markDirty(); engine::post(engine::Cmd::ReleaseTies); app_.toast("QUANTIZED"); break;
      case mt::QuantizeResult::Unchanged: app_.toast("NO CHANGE"); break;
      case mt::QuantizeResult::Collision: app_.toast("NOTES COLLIDE: NO CHANGE"); break;
      case mt::QuantizeResult::NoSlot: app_.toast("FX CONFLICT/FULL: NO CHANGE"); break;
      case mt::QuantizeResult::Invalid: app_.toast("NO GRID IN RANGE"); break;
    }
  };
  char title[28]; menuScopeTitle(title,sizeof(title),quantizeSel_,!menuCtx_.selected);
  if (refresh) app_.menu().replace(title,rows,3,cb);
  else app_.menu().openChild(title,rows,3,cb);
}
}
