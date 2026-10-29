#include "action_dialog.h"
#include "app.h"
#include <string.h>
namespace ui {
void ActionDialog::open(const char* title, const Param* params, int count, std::function<void()> apply) {
  count_=count<0 ? 0 : count>3 ? 3 : count;
  for(int i=0;i<count_;++i)params_[i]=params[i];
  params_[count_]={"Apply",nullptr,nullptr};
  params_[count_+1]={"Cancel",nullptr,nullptr};
  strlcpy(title_,title,sizeof(title_)); apply_=std::move(apply);
  list_.setParams(params_,count_+2);list_.setEdit(false);list_.setSel(0);open_=true;
}
void ActionDialog::cancel() { open_=false;list_.setEdit(false);apply_=nullptr; }
bool ActionDialog::action(int row) {
  if(row<count_)return false;
  auto cb=std::move(apply_);
  const bool apply=row==count_;
  cancel();if(apply && cb)cb();
  app_.invalidate();return true;
}
void ActionDialog::onInput(const hw::InputEvent& ev) {
  if(!open_)return;
  if(ev.type==hw::InputType::EncLong){cancel();app_.invalidate();return;}
  if(ev.type==hw::InputType::EncClick && !list_.editing() && action(list_.sel()))return;
  list_.onInput(ev);
}
void ActionDialog::onTouch(const TouchEvent& ev) {
  if(!open_)return;
  if(ev.type==TouchType::Tap){const int row=list_.rowAt(ev.y);if(row>=count_ && row<count_+2 && action(row))return;}
  list_.onTouch(ev);
}
void ActionDialog::draw(LGFX_Sprite& s,int y0) {
  if(!open_)return;
  s.fillRect(0,y0,kScreenW,24,kBeatBg);s.setTextColor(kText);s.drawString(title_,16,y0+4);
  list_.draw(s,y0+28);
}
}
