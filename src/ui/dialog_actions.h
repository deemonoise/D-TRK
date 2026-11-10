#pragma once
#include <string.h>
#include "dialog_focus.h"
#include "param_list.h"
namespace ui {
// Shared footer. The ParamList contains parameter rows only; encoder focus also visits both buttons.
class DialogActions {
 public:
  static constexpr int kH = 32;
  void reset(ParamList& list) { focus_ = -1; list.setFocusVisible(true); }
  bool footerFocused() const { return focus_>=0; }
  void focusList(ParamList& list) {focus_=-1;list.setFocusVisible(true);}
  int listRows(int header) const { return (kAreaH-header-kH)/ParamList::kRowH; }
  // -1 = forwarded/consumed; 0 = primary; 1 = Cancel.
  int input(ParamList& list, int rows, const hw::InputEvent& ev) {
    if (ev.type == hw::InputType::EncTurn && !list.editing()) {
      const int cur = focus_ >= 0 ? rows+focus_ : list.sel();
      const int next = dialogNextFocus(cur,ev.delta,rows);
      focus_ = next >= rows ? next-rows : -1;
      list.setFocusVisible(focus_<0);
      if (focus_ < 0) list.selectRow(next);
      return -1;
    }
    if (focus_ >= 0) {
      if (ev.type == hw::InputType::EncClick) return focus_;
      return -1;
    }
    list.onInput(ev); return -1;
  }
  int touch(ParamList& list, const TouchEvent& ev, int y0) {
    const int y=y0+kAreaH-kH;
    if (ev.y >= y) {
      if(ev.type==TouchType::Tap){ focus_=ev.x<kScreenW/2 ? 1 : 0; list.setEdit(false);list.setFocusVisible(false);return focus_; }
      return -1;
    }
    if(ev.type==TouchType::Tap || ev.type==TouchType::Drag) focusList(list);
    list.onTouch(ev);return -1;
  }
  void draw(LGFX_Sprite& s,int y0,const char* primary,const char* cancel="CANCEL") const {
    const int y=y0+kAreaH-kH;
    s.fillRect(0,y,kScreenW,kH,kBg);s.drawFastHLine(0,y,kScreenW,kDim);
    button(s,8,y+4,kScreenW/2-12,cancel,focus_==1);
    button(s,kScreenW/2+4,y+4,kScreenW/2-12,primary,focus_==0);
  }
 private:
  static void button(LGFX_Sprite& s,int x,int y,int w,const char* label,bool selected){
    s.fillRect(x,y,w,kH-8,selected?kPlayBg:kMenuBg);s.drawRect(x,y,w,kH-8,selected?kCursor:kDim);
    s.setTextColor(selected?kCursor:kText);
    s.drawString(label,x+(w-static_cast<int>(strlen(label))*kCharW)/2,y+(kH-8-kCharH)/2);
  }
  int focus_=-1;
};
}
